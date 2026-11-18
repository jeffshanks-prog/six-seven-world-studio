"""The 6|7 Phone Companion: a small web app on the King Jahmir gateway that a
host and their team open on a phone, for an event's RSVPs, a calendar
reminder, its shot list and a private team chat.

No Second Life password or session ever reaches it. Three ways in:

- The host's viewer, signed in to 6|7, sends an event here and asks for a
  pairing code (`/v1/phone/...`, the 6|7 session token). The host types the
  code at /phone on the phone, which then holds a phone session cookie.
- The host invites a team member from the phone: a one-use link, sent
  privately, that gives whoever opens it that member's place on that one
  event's team. The host can take it back at any time.
- Guests answer the event's public RSVP page (/r/<code>), only while the host
  keeps it open. An answer is what the guest typed: it is marked as not
  verified, and nobody is signed up for announcements unless they tick the
  box themselves.

Pages are server-rendered HTML forms with no scripts. Every form post
checks its Origin, and the cookie is SameSite=Strict and HttpOnly.
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import html
import json
import re
import secrets
import time
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from urllib.parse import quote, urlsplit

from .auth import Principal
from .ratelimit import RateLimiter
from .store import Store

PAIR_TTL = 10 * 60
HOST_SESSION = 30 * 86400
MEMBER_SESSION = 30 * 86400
COOKIE = "sw67phone"
MAX_EVENTS_PER_HOST = 50
MAX_SHOTS = 100
MAX_MEMBERS = 30
MAX_RSVPS = 1000
MAX_CHAT_KEPT = 500
CHAT_SHOWN = 100
EVENT_KEEP_AFTER_END = 30 * 86400

ROLES = {"dj": "DJ", "host": "Host", "cohost": "Co-host", "staff": "Staff"}
ANSWERS = {"going": "Going", "maybe": "Maybe", "not_going": "Not going"}
_UUID = re.compile(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")
_VID = re.compile(r"^[A-Za-z0-9_.:-]{1,64}$")
_EID = re.compile(r"^ev_[A-Za-z0-9_-]{8,24}$")
_MID = re.compile(r"^tm_[A-Za-z0-9_-]{8,24}$")
_CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
_CTRL = re.compile(r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f\u200b-\u200f\u202a-\u202e\u2066-\u2069]")

class PhoneError(Exception):
    def __init__(self, status: int, code: str, message: str, retry_after: float | None = None):
        super().__init__(message)
        self.status, self.code, self.message, self.retry_after = status, code, message, retry_after


@dataclass(frozen=True)
class Session:
    kind: str  # "host" or "member"
    ident: str  # the host's agent id, or the member id
    eid: str  # the member's event; empty for a host
    name: str
    role: str  # "host" for the host, a ROLES key for a member


@dataclass
class Page:
    status: int
    body: str
    content_type: str = "text/html; charset=utf-8"
    headers: dict | None = None


def _clean(value, limit: int, multiline: bool = False) -> str:
    text = value if isinstance(value, str) else ""
    text = _CTRL.sub("", text.replace("\r\n", "\n"))
    if not multiline:
        text = " ".join(text.split())
    return text.strip()[:limit]


def _hash(text: str) -> str:
    return hashlib.sha256(text.encode()).hexdigest()


def _b64(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def _unb64(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def account_id(text: str) -> str:
    """The account ID in an ID or a profile link, lower case; empty when there is none."""
    m = _UUID.search((text or "").lower())
    return m.group(0) if m else ""


def location_link(location: str) -> str:
    """Only a Second Life map or app link becomes a link."""
    low = location.lower()
    if low.startswith(("https://maps.secondlife.com/", "http://maps.secondlife.com/", "secondlife:///", "secondlife://")):
        return location
    return ""


def slt(epoch: float) -> str:
    """The time as Second Life time (US Pacific), and UTC beside it."""
    utc = datetime.fromtimestamp(epoch, timezone.utc)
    try:
        from zoneinfo import ZoneInfo
        local = utc.astimezone(ZoneInfo("America/Los_Angeles"))
        label = "SLT"
    except Exception:  # noqa: BLE001 - no time zone data on this host
        local, label = utc, "UTC"
    text = local.strftime("%a %d %b, %I:%M %p").replace(" 0", " ") + f" {label}"
    if label != "UTC":
        text += utc.strftime(" (%H:%M UTC)")
    return text


def countdown(starts: float, minutes: int, now: float) -> str:
    def span(sec: float) -> str:
        sec = int(sec)
        d, rem = divmod(sec, 86400)
        h, rem = divmod(rem, 3600)
        m = rem // 60
        parts = []
        if d:
            parts.append(f"{d} day{'s' if d != 1 else ''}")
        if h:
            parts.append(f"{h} hour{'s' if h != 1 else ''}")
        if not d and (m or not parts):
            parts.append(f"{m} minute{'s' if m != 1 else ''}")
        return " ".join(parts)

    if now < starts:
        return "Starts in " + span(starts - now)
    if now < starts + minutes * 60:
        return "LIVE now, started " + span(now - starts) + " ago"
    return "Ended"


class Phone:
    def __init__(self, cfg, store: Store):
        self.cfg = cfg
        self.store = store
        self._key = hmac.new(cfg.auth_secret, b"sw67-phone-session-v1", hashlib.sha256).digest()
        self.pair_limiter = RateLimiter(store, "phonepair", 10, 10, 60)
        self.rsvp_limiter = RateLimiter(store, "rsvp", 10, 10, 100)
        self.post_limiter = RateLimiter(store, "phonepost", 30, 15, 2000)
        origin = urlsplit(cfg.public_url)
        self.origin = f"{origin.scheme}://{origin.netloc}"
        self.secure = origin.scheme == "https"

    # ---- storage helpers ----------------------------------------------------
    def _q(self, sql: str, args: tuple = ()) -> list[dict]:
        return self.store._q(sql, args)  # noqa: SLF001 - the phone's tables live in the same store

    def _n(self, sql: str, args: tuple = ()) -> int:
        return self.store._n(sql, args)  # noqa: SLF001

    def _event(self, eid: str) -> dict | None:
        if not _EID.match(eid or ""):
            return None
        rows = self._q("SELECT * FROM kj_phone_events WHERE eid=?", (eid,))
        return rows[0] if rows else None

    # ---- sessions -------------------------------------------------------------
    def make_cookie(self, kind: str, ident: str, eid: str = "", lifetime: int = HOST_SESSION,
                    now: float | None = None) -> str:
        now = int(time.time() if now is None else now)
        payload = _b64(json.dumps({"k": kind, "i": ident, "e": eid, "x": now + lifetime},
                                  separators=(",", ":")).encode())
        sig = _b64(hmac.new(self._key, payload.encode(), hashlib.sha256).digest())
        return f"{payload}.{sig}"

    def set_cookie_header(self, value: str, max_age: int) -> str:
        flags = f"{COOKIE}={value}; Path=/; Max-Age={max_age}; HttpOnly; SameSite=Strict"
        return flags + ("; Secure" if self.secure else "")

    def session(self, cookie_header: str | None, now: float | None = None) -> Session | None:
        raw = ""
        for part in (cookie_header or "").split(";"):
            k, _, v = part.strip().partition("=")
            if k == COOKIE:
                raw = v
        if not raw or len(raw) > 1024 or raw.count(".") != 1:
            return None
        payload, sig = raw.split(".")
        expected = _b64(hmac.new(self._key, payload.encode(), hashlib.sha256).digest())
        if not hmac.compare_digest(expected, sig):
            return None
        try:
            data = json.loads(_unb64(payload))
        except (ValueError, UnicodeDecodeError):
            return None
        now = time.time() if now is None else now
        if not isinstance(data, dict) or not isinstance(data.get("x"), int) or data["x"] < now:
            return None
        kind, ident, eid = data.get("k"), data.get("i"), data.get("e", "")
        if kind == "host" and isinstance(ident, str) and _UUID.fullmatch(ident):
            return Session("host", ident, "", "", "host")
        if kind == "member" and isinstance(ident, str) and _MID.match(ident):
            rows = self._q("SELECT name,role,eid FROM kj_phone_members WHERE mid=? AND revoked=0 "
                           "AND joined IS NOT NULL", (ident,))
            if rows and rows[0]["eid"] == eid:
                return Session("member", ident, eid, rows[0]["name"], rows[0]["role"])
        return None

    def _can_see(self, s: Session, ev: dict) -> bool:
        return (s.kind == "host" and ev["host"] == s.ident) or (s.kind == "member" and ev["eid"] == s.eid)

    # ---- the viewer's API (6|7 session token) -------------------------------
    def pair(self, p: Principal, body: dict) -> dict:
        code = "".join(secrets.choice(_CODE_ALPHABET) for _ in range(8))
        now = time.time()
        self._n("DELETE FROM kj_phone_pairs WHERE expires<?", (now,))
        self._n("INSERT INTO kj_phone_pairs(code_hash,host,host_name,expires,used) VALUES(?,?,?,?,0)",
                (_hash(code), p.sub, _clean(body.get("host_name"), 64), now + PAIR_TTL))
        return {"code": f"{code[:4]}-{code[4:]}", "expires_in": PAIR_TTL, "url": f"{self.cfg.public_url}/phone"}

    def put_event(self, p: Principal, body: dict) -> dict:
        vid = body.get("id")
        if not isinstance(vid, str) or not _VID.match(vid):
            raise PhoneError(400, "bad_request", "The event needs an id.")
        title = _clean(body.get("title"), 120)
        if not title:
            raise PhoneError(400, "bad_request", "The event needs a name.")
        starts = body.get("starts")
        if isinstance(starts, bool) or not isinstance(starts, (int, float)) or not 946684800 <= starts <= 4102444800:
            raise PhoneError(400, "bad_request", "The event needs a start time.")
        minutes = body.get("minutes", 120)
        if isinstance(minutes, bool) or not isinstance(minutes, int) or not 1 <= minutes <= 24 * 60:
            minutes = 120
        location = _clean(body.get("location"), 300)
        details = _clean(body.get("details"), 2000, multiline=True)
        host_name = _clean(body.get("host_name"), 64)
        shots_in = body.get("shots", [])
        if not isinstance(shots_in, list):
            raise PhoneError(400, "bad_request", "shots must be a list.")
        shots = [s for s in (_clean(x, 200) for x in shots_in[:MAX_SHOTS]) if s]
        now = time.time()
        with self.store._tx():  # noqa: SLF001
            rows = self._q("SELECT eid FROM kj_phone_events WHERE host=? AND vid=?", (p.sub, vid))
            if rows:
                eid = rows[0]["eid"]
                self._n("UPDATE kj_phone_events SET title=?,starts=?,minutes=?,location=?,details=?,host_name=?,"
                        "updated=? WHERE eid=?", (title, float(starts), minutes, location, details, host_name, now, eid))
            else:
                n = int(self._q("SELECT COUNT(*) AS n FROM kj_phone_events WHERE host=?", (p.sub,))[0]["n"])
                if n >= MAX_EVENTS_PER_HOST:
                    raise PhoneError(409, "too_many", "The phone holds 50 events. Remove an old one first.")
                eid = "ev_" + secrets.token_urlsafe(9)
                self._n("INSERT INTO kj_phone_events(eid,host,vid,host_name,title,starts,minutes,location,details,"
                        "rsvp_code,rsvp_open,created,updated) VALUES(?,?,?,?,?,?,?,?,?,?,0,?,?)",
                        (eid, p.sub, vid, host_name, title, float(starts), minutes, location, details,
                         secrets.token_urlsafe(9), now, now))
            # The viewer owns the shot list; the phone owns what was ticked off.
            done = {r["text"]: (r["done"], r["done_by"])
                    for r in self._q("SELECT text,done,done_by FROM kj_phone_shots WHERE eid=?", (eid,))}
            self._n("DELETE FROM kj_phone_shots WHERE eid=?", (eid,))
            for i, s in enumerate(shots):
                d, by = done.get(s, (0, ""))
                self._n("INSERT INTO kj_phone_shots(eid,pos,text,done,done_by) VALUES(?,?,?,?,?)", (eid, i, s, d, by))
        return {"event": eid, "phone_url": f"{self.cfg.public_url}/phone/e/{eid}", "shots": len(shots)}

    def _host_event(self, p: Principal, vid: str) -> dict:
        if not _VID.match(vid or ""):
            raise PhoneError(404, "not_found", "That event is not on the phone.")
        rows = self._q("SELECT * FROM kj_phone_events WHERE host=? AND vid=?", (p.sub, vid))
        if not rows:
            raise PhoneError(404, "not_found", "That event is not on the phone.")
        return rows[0]

    def delete_event(self, p: Principal, vid: str) -> dict:
        ev = self._host_event(p, vid)
        self._delete(ev["eid"])
        return {"deleted": True}

    def _delete(self, eid: str) -> None:
        with self.store._tx():  # noqa: SLF001 - SQLite's cascade needs foreign keys; delete children anyway
            for t in ("kj_phone_shots", "kj_phone_members", "kj_phone_messages", "kj_phone_rsvps"):
                self._n(f"DELETE FROM {t} WHERE eid=?", (eid,))
            self._n("DELETE FROM kj_phone_events WHERE eid=?", (eid,))

    def rsvps(self, p: Principal, vid: str) -> dict:
        """The event's RSVPs in the file format Party Contacts imports."""
        ev = self._host_event(p, vid)
        rows = self._q("SELECT account,name,answer,announcements FROM kj_phone_rsvps WHERE eid=? ORDER BY updated",
                       (ev["eid"],))
        return {"format": "sw67.rsvps", "version": 1, "event": vid, "verified": False,
                "rsvps": [{"id": r["account"], "name": r["name"], "answer": r["answer"],
                           "announcements": bool(r["announcements"])} for r in rows]}

    def purge(self, now: float | None = None) -> int:
        now = time.time() if now is None else now
        old = self._q("SELECT eid FROM kj_phone_events WHERE starts + minutes*60 < ?", (now - EVENT_KEEP_AFTER_END,))
        for r in old:
            self._delete(r["eid"])
        self._n("DELETE FROM kj_phone_pairs WHERE expires<?", (now,))
        return len(old)

    # ---- pages ------------------------------------------------------------
    def check_origin(self, origin: str | None, referer: str | None) -> None:
        """A form post must come from these pages (SameSite cookies already stop most of the rest)."""
        source = origin or ""
        if not source and referer:
            parts = urlsplit(referer)
            source = f"{parts.scheme}://{parts.netloc}"
        if source and source != self.origin:
            raise PhoneError(403, "forbidden", "That form came from another site.")

    def _limit(self, limiter: RateLimiter, key: str) -> None:
        wait = limiter.check(key)
        if wait:
            raise PhoneError(429, "rate_limited", "That's quick. Wait a moment and try again.", wait)

    def handle(self, method: str, path: str, form: dict, cookie: str | None, ip: str,
               origin: str | None = None, referer: str | None = None) -> Page:
        """Every /phone, /r route. `form` is the posted form, already parsed."""
        now = time.time()
        s = self.session(cookie, now)
        try:
            if method == "POST":
                self.check_origin(origin, referer)
            if path in ("/phone", "/phone/") and method == "GET":
                return self._home(s, now)
            if path == "/phone/pair" and method == "POST":
                return self._pair(form, ip)
            if path == "/phone/signout" and method == "POST":
                return Page(303, "", headers={"Location": "/phone",
                                              "Set-Cookie": self.set_cookie_header("", 0)})
            m = re.fullmatch(r"/phone/join/([A-Za-z0-9_-]{16,64})", path)
            if m:
                return self._join(m.group(1), method, now)
            m = re.fullmatch(r"/r/([A-Za-z0-9_-]{8,24})", path)
            if m:
                return self._rsvp_page(m.group(1), method, form, ip, now)
            m = re.fullmatch(r"/phone/e/([A-Za-z0-9_-]{8,30})(/[a-z_]+)?(/[A-Za-z0-9_-]+)?(/[a-z]+)?", path)
            if m:
                ev = self._event(m.group(1))
                if s is None:
                    return self._signin("Sign in to see this event.")
                if ev is None or not self._can_see(s, ev):
                    return self._message(404, "Not found", "That event is not on this phone, or it was removed.")
                action = (m.group(2) or "")[1:]
                arg = (m.group(3) or "")[1:]
                verb = (m.group(4) or "")[1:]
                if method == "GET" and not action:
                    return self._event_page(s, ev, now)
                if method == "GET" and action == "calendar":
                    return self._ics(ev)
                if method == "POST":
                    self._limit(self.post_limiter, s.ident)
                    return self._event_post(s, ev, action, arg, verb, form, now)
            return self._message(404, "Not found", "There is no such page.")
        except PhoneError as e:
            if e.status in (403, 429):
                return self._message(e.status, "Not now" if e.status == 429 else "Not allowed", e.message)
            raise

    # ---- page parts -------------------------------------------------------
    def _layout(self, title: str, body: str, status: int = 200, headers: dict | None = None) -> Page:
        style = ("body{font-family:system-ui,sans-serif;max-width:40em;margin:0 auto;padding:1em;background:#111111;"
                 "color:#FFFFFF;line-height:1.5;font-size:1.05em}h1,h2{color:#D4AF37;line-height:1.2}"
                 "a{color:#D4AF37}label{display:block;margin-top:.8em}"
                 "input[type=text],textarea,select{width:100%;box-sizing:border-box;padding:.6em;"
                 "background:#2a2a2a;color:#FFFFFF;border:1px solid #D4AF37;font-size:1em}"
                 "button{margin-top:.8em;min-height:44px;padding:.5em 1.1em;background:#D4AF37;color:#111111;border:0;"
                 "font-weight:bold;font-size:1em}button.plain{background:#333333;color:#FFFFFF;border:1px solid #888888}"
                 "ul{padding-left:1.2em}li{margin:.4em 0}.box{border:1px solid #555555;padding:.8em;margin:1em 0}"
                 ".note{color:#CCCCCC;font-size:.95em}.err{color:#FFB3B3}.row{display:flex;gap:.5em;align-items:center;"
                 "flex-wrap:wrap}form.inline{display:inline}.msg{border-bottom:1px solid #333333;padding:.4em 0}"
                 ":focus{outline:3px solid #FFFFFF;outline-offset:2px}"
                 "@media (prefers-reduced-motion:reduce){*{transition:none!important;animation:none!important}}")
        page = (f"<!doctype html><html lang=en><meta charset=utf-8><meta name=viewport "
                f"content='width=device-width,initial-scale=1'><meta name=robots content=noindex>"
                f"<title>{html.escape(title)}</title><style>{style}</style><body>{body}</body></html>")
        return Page(status, page, headers=headers)

    def _message(self, status: int, title: str, text: str) -> Page:
        return self._layout(title, f"<h1>{html.escape(title)}</h1><p>{html.escape(text)}</p>"
                                   "<p><a href=/phone>6|7 Phone</a></p>", status)

    def _signin(self, error: str = "", status: int = 200) -> Page:
        err = f"<p class=err role=alert>{html.escape(error)}</p>" if error else ""
        body = ("<h1>6|7 Phone</h1>"
                "<p>Your event's RSVPs, reminders, shot list and team chat. This never asks for your Second Life "
                "password.</p>" + err +
                "<form method=post action=/phone/pair><label for=code>Pairing code</label>"
                "<input id=code name=code type=text inputmode=text autocomplete=off autocapitalize=characters "
                "required maxlength=12 placeholder=ABCD-2345>"
                "<button type=submit>Pair this phone</button></form>"
                "<p class=note>Get a code in the viewer: Event Control Room &gt; Phone &gt; Pair a phone. A code works "
                "once, for 10 minutes. Team members join from the link their host sends them.</p>")
        return self._layout("6|7 Phone", body, status)

    def _pair(self, form: dict, ip: str) -> Page:
        self._limit(self.pair_limiter, ip)
        code = re.sub(r"[^A-Z0-9]", "", (form.get("code") or "").upper())
        if len(code) != 8:
            return self._signin("That code is not right. It looks like ABCD-2345.", 400)
        now = time.time()
        rows = self._q("SELECT host,host_name FROM kj_phone_pairs WHERE code_hash=? AND used=0 AND expires>=?",
                       (_hash(code), now))
        if not rows or not self._n("UPDATE kj_phone_pairs SET used=1 WHERE code_hash=? AND used=0", (_hash(code),)):
            return self._signin("That code is not right, was used already, or is over 10 minutes old. "
                                "Get a new one in the viewer.", 400)
        cookie = self.make_cookie("host", rows[0]["host"], lifetime=HOST_SESSION)
        return Page(303, "", headers={"Location": "/phone",
                                      "Set-Cookie": self.set_cookie_header(cookie, HOST_SESSION)})

    def _home(self, s: Session | None, now: float) -> Page:
        if s is None:
            return self._signin()
        if s.kind == "member":
            return Page(303, "", headers={"Location": f"/phone/e/{s.eid}"})
        rows = self._q("SELECT eid,title,starts,minutes FROM kj_phone_events WHERE host=? ORDER BY starts", (s.ident,))
        upcoming = [r for r in rows if r["starts"] + r["minutes"] * 60 >= now]
        ended = [r for r in rows if r["starts"] + r["minutes"] * 60 < now]

        def item(r):
            return (f'<li><a href="/phone/e/{r["eid"]}">{html.escape(r["title"])}</a><br>'
                    f'<span class=note>{html.escape(slt(r["starts"]))}. '
                    f'{html.escape(countdown(r["starts"], r["minutes"], now))}</span></li>')

        listing = ("<ul>" + "".join(item(r) for r in upcoming) + "</ul>") if upcoming else \
            "<p>No upcoming events yet. In the viewer, open Event Control Room, pick an event and press " \
            "<b>Send to phone</b>.</p>"
        if ended:
            listing += "<h2>Ended</h2><ul>" + "".join(item(r) for r in reversed(ended)) + "</ul>"
        body = (f"<h1>Your events</h1>{listing}"
                "<form method=post action=/phone/signout><button class=plain type=submit>Sign out of this phone"
                "</button></form>")
        return self._layout("6|7 Phone", body)

    def _event_page(self, s: Session, ev: dict, now: float, notice: str = "", invite_link: str = "") -> Page:
        e = html.escape
        eid = ev["eid"]
        host = s.kind == "host"
        parts = [f"<p><a href=/phone>All events</a></p><h1>{e(ev['title'])}</h1>",
                 f"<p><b>{e(countdown(ev['starts'], ev['minutes'], now))}</b><br>{e(slt(ev['starts']))}, "
                 f"{ev['minutes']} minutes</p>"]
        if notice:
            parts.append(f"<p role=status>{e(notice)}</p>")
        if ev["location"]:
            link = location_link(ev["location"])
            loc = f'<a href="{e(link)}">{e(ev["location"])}</a>' if link else e(ev["location"])
            parts.append(f"<p>Where: {loc}</p>")
        if ev["details"]:
            parts.append(f"<p>{e(ev['details']).replace(chr(10), '<br>')}</p>")
        if not host:
            parts.append(f"<p class=note>You're on the team as {e(ROLES.get(s.role, s.role))}: {e(s.name)}.</p>")

        parts.append("<div class=box><h2>Reminder</h2><p>Add the event to your phone's calendar, with an alert "
                     f"15 minutes before.</p><p><a href=/phone/e/{eid}/calendar>Add to calendar</a></p></div>")

        shots = self._q("SELECT id,text,done,done_by FROM kj_phone_shots WHERE eid=? ORDER BY pos", (eid,))
        if shots:
            items = []
            for sh in shots:
                state = "DONE" if sh["done"] else "TO DO"
                by = f" ({e(sh['done_by'])})" if sh["done"] and sh["done_by"] else ""
                label = "Mark not done" if sh["done"] else "Mark done"
                items.append(f"<li><b>[{state}]</b> {e(sh['text'])}{by}"
                             f"<form class=inline method=post action=/phone/e/{eid}/shot/{sh['id']}/toggle> "
                             f"<button class=plain type=submit aria-label='{label}: {e(sh['text'])}'>{label}</button>"
                             "</form></li>")
            done_n = sum(1 for sh in shots if sh["done"])
            parts.append(f"<div class=box><h2>Shot list</h2><p>{done_n} of {len(shots)} done.</p>"
                         f"<ul>{''.join(items)}</ul></div>")
        else:
            parts.append("<div class=box><h2>Shot list</h2><p>No shots yet. The host adds them in the viewer.</p></div>")

        msgs = self._q("SELECT author_name,text,created FROM kj_phone_messages WHERE eid=? ORDER BY id DESC LIMIT ?",
                       (eid, CHAT_SHOWN))
        chat = "".join(f"<div class=msg><b>{e(m['author_name'])}</b> <span class=note>"
                       f"{e(datetime.fromtimestamp(m['created'], timezone.utc).strftime('%d %b %H:%M UTC'))}</span>"
                       f"<br>{e(m['text'])}</div>" for m in reversed(msgs)) or "<p class=note>No messages yet.</p>"
        parts.append(f"<div class=box><h2>Team chat</h2><p class=note>Only this event's host and team see it. "
                     f"Messages are deleted {self.cfg.retention_days} days after they're sent.</p>{chat}"
                     f"<form method=post action=/phone/e/{eid}/chat><label for=msg>Message</label>"
                     "<textarea id=msg name=text rows=3 maxlength=1000 required></textarea>"
                     "<button type=submit>Send to team</button></form>"
                     f"<p><a href=/phone/e/{eid}>Refresh</a></p></div>")

        if host:
            rsvps = self._q("SELECT name,account,answer,announcements FROM kj_phone_rsvps WHERE eid=? "
                            "ORDER BY updated DESC", (eid,))
            counts = {k: sum(1 for r in rsvps if r["answer"] == k) for k in ANSWERS}
            summary = ", ".join(f"{v} {ANSWERS[k].lower()}" for k, v in counts.items())
            listing = "".join(f"<li>{e(r['name'])}: {ANSWERS.get(r['answer'], r['answer'])}"
                              f"{' (wants announcements)' if r['announcements'] else ''}</li>" for r in rsvps)
            link = f"{self.cfg.public_url}/r/{ev['rsvp_code']}"
            if ev["rsvp_open"]:
                state = (f"<p><b>RSVPs are OPEN.</b> Share this link: <br><input type=text readonly "
                         f"aria-label='RSVP link' value='{e(link)}'></p>")
                toggle = "Close RSVPs"
            else:
                state = "<p><b>RSVPs are CLOSED.</b> Nobody can answer until you open them.</p>"
                toggle = "Open RSVPs"
            parts.append(f"<div class=box><h2>RSVPs</h2>{state}"
                         f"<form method=post action=/phone/e/{eid}/rsvp_toggle><button type=submit>{toggle}</button>"
                         f"</form><p>{e(summary) if rsvps else 'No answers yet.'}</p><ul>{listing}</ul>"
                         "<p class=note>Answers are what guests typed and aren't verified. Bring them into Party "
                         "Contacts with <b>Get RSVPs from phone</b> in Event Control Room.</p></div>")
            members = self._q("SELECT mid,name,role,joined,revoked FROM kj_phone_members WHERE eid=? AND revoked=0 "
                              "ORDER BY created", (eid,))
            mlist = "".join(
                f"<li>{e(m['name'])}, {ROLES.get(m['role'], m['role'])}: "
                f"{'joined' if m['joined'] else 'invited, not joined yet'}"
                f"<form class=inline method=post action=/phone/e/{eid}/member/{m['mid']}/revoke> "
                f"<button class=plain type=submit aria-label='Remove {e(m['name'])}'>Remove</button></form></li>"
                for m in members) or "<li>Nobody yet.</li>"
            invite = ""
            if invite_link:
                invite = (f"<p role=status><b>Send this link privately.</b> It works once: whoever opens it first joins "
                          f"as this member. <br><input type=text readonly aria-label='Invite link' "
                          f"value='{e(invite_link)}'></p>")
            options = "".join(f"<option value={k}>{v}</option>" for k, v in ROLES.items() if k != "host")
            parts.append(f"<div class=box><h2>Team</h2><ul>{mlist}</ul>{invite}"
                         f"<form method=post action=/phone/e/{eid}/invite><label for=mname>Name</label>"
                         "<input id=mname name=name type=text maxlength=64 required>"
                         f"<label for=mrole>Role</label><select id=mrole name=role>{options}</select>"
                         "<button type=submit>Make an invite link</button></form></div>")
        parts.append("<form method=post action=/phone/signout><button class=plain type=submit>Sign out of this phone"
                     "</button></form>")
        return self._layout(ev["title"], "".join(parts))

    def _event_post(self, s: Session, ev: dict, action: str, arg: str, verb: str, form: dict, now: float) -> Page:
        eid = ev["eid"]
        host = s.kind == "host"
        back = {"Location": f"/phone/e/{eid}"}
        if action == "shot" and verb == "toggle" and arg.isdigit():
            who = "host" if host else s.name
            self._n("UPDATE kj_phone_shots SET done=1-done, done_by=CASE WHEN done=0 THEN ? ELSE '' END "
                    "WHERE id=? AND eid=?", (who[:64], int(arg), eid))
            return Page(303, "", headers=back)
        if action == "chat" and not arg:
            text = _clean(form.get("text"), 1000, multiline=True)
            if text:
                name = (ev["host_name"] or "Host") + " (host)" if host else \
                    f"{s.name} ({ROLES.get(s.role, s.role)})"
                self._n("INSERT INTO kj_phone_messages(eid,author,author_name,text,created) VALUES(?,?,?,?,?)",
                        (eid, s.ident, name[:90], text, now))
                self._n("DELETE FROM kj_phone_messages WHERE eid=? AND id NOT IN "
                        "(SELECT id FROM kj_phone_messages WHERE eid=? ORDER BY id DESC LIMIT ?)",
                        (eid, eid, MAX_CHAT_KEPT))
            return Page(303, "", headers=back)
        if not host:
            return self._message(403, "Not allowed", "Only the event's host can do that.")
        if action == "rsvp_toggle" and not arg:
            self._n("UPDATE kj_phone_events SET rsvp_open=1-rsvp_open WHERE eid=?", (eid,))
            return Page(303, "", headers=back)
        if action == "invite" and not arg:
            name = _clean(form.get("name"), 64)
            role = form.get("role") if form.get("role") in ROLES and form.get("role") != "host" else "staff"
            if not name:
                return self._event_page(s, ev, now, notice="Give the team member a name first.")
            n = int(self._q("SELECT COUNT(*) AS n FROM kj_phone_members WHERE eid=? AND revoked=0", (eid,))[0]["n"])
            if n >= MAX_MEMBERS:
                return self._event_page(s, ev, now, notice="A team holds 30 people. Remove someone first.")
            token = secrets.token_urlsafe(24)
            self._n("INSERT INTO kj_phone_members(mid,eid,name,role,invite_hash,created) VALUES(?,?,?,?,?,?)",
                    ("tm_" + secrets.token_urlsafe(9), eid, name, role, _hash(token), now))
            return self._event_page(s, ev, now, notice=f"Invite made for {name}.",
                                    invite_link=f"{self.cfg.public_url}/phone/join/{token}")
        if action == "member" and verb == "revoke" and _MID.match(arg):
            self._n("UPDATE kj_phone_members SET revoked=1, invite_hash=NULL WHERE mid=? AND eid=?", (arg, eid))
            return self._event_page(s, ev, now, notice="Removed. Their phone can't open this event any more.")
        return self._message(404, "Not found", "There is no such action.")

    def _join(self, token: str, method: str, now: float) -> Page:
        rows = self._q("SELECT m.mid,m.name,m.role,m.eid,e.title FROM kj_phone_members m JOIN kj_phone_events e "
                       "ON e.eid=m.eid WHERE m.invite_hash=? AND m.revoked=0 AND m.joined IS NULL", (_hash(token),))
        if not rows:
            return self._message(404, "Invite not valid", "This invite was used already or taken back. Ask the host "
                                                          "for a new one.")
        r = rows[0]
        if method == "GET":
            # Joining is a button, so a link preview can't use up the invite.
            body = (f"<h1>Join the team</h1><p>{html.escape(r['title'])}: join as <b>{html.escape(r['name'])}</b>, "
                    f"{ROLES.get(r['role'], r['role'])}.</p><p class=note>You'll see the event, its shot list and the "
                    "team chat. Nothing here asks for your Second Life password.</p>"
                    f"<form method=post action=/phone/join/{html.escape(token)}><button type=submit>Join</button></form>")
            return self._layout("Join the team", body)
        if method != "POST":
            return self._message(404, "Not found", "There is no such page.")
        if not self._n("UPDATE kj_phone_members SET joined=?, invite_hash=NULL WHERE mid=? AND joined IS NULL "
                       "AND revoked=0", (now, r["mid"])):
            return self._message(404, "Invite not valid", "This invite was used already.")
        cookie = self.make_cookie("member", r["mid"], r["eid"], lifetime=MEMBER_SESSION)
        return Page(303, "", headers={"Location": f"/phone/e/{r['eid']}",
                                      "Set-Cookie": self.set_cookie_header(cookie, MEMBER_SESSION)})

    def _rsvp_page(self, code: str, method: str, form: dict, ip: str, now: float) -> Page:
        rows = self._q("SELECT eid,title,starts,minutes,location,rsvp_open,host_name FROM kj_phone_events "
                       "WHERE rsvp_code=?", (code,))
        if not rows or not rows[0]["rsvp_open"]:
            return self._message(404, "RSVPs closed", "This event isn't taking RSVPs right now.")
        ev = rows[0]
        e = html.escape
        error, done = "", ""
        if method == "POST":
            self._limit(self.rsvp_limiter, ip)
            name = _clean(form.get("name"), 64)
            acct = account_id(form.get("account", ""))
            answer = form.get("answer") if form.get("answer") in ANSWERS else ""
            wants = form.get("announcements") == "yes"
            if not name or not acct or not answer:
                error = "Fill in your name, your profile link or account ID, and your answer."
            else:
                n = int(self._q("SELECT COUNT(*) AS n FROM kj_phone_rsvps WHERE eid=?", (ev["eid"],))[0]["n"])
                exists = self._q("SELECT 1 AS one FROM kj_phone_rsvps WHERE eid=? AND account=?", (ev["eid"], acct))
                if not exists and n >= MAX_RSVPS:
                    error = "This event has all the RSVPs it can take."
                else:
                    with self.store._tx():  # noqa: SLF001
                        self._n("DELETE FROM kj_phone_rsvps WHERE eid=? AND account=?", (ev["eid"], acct))
                        self._n("INSERT INTO kj_phone_rsvps(eid,account,name,answer,announcements,updated) "
                                "VALUES(?,?,?,?,?,?)", (ev["eid"], acct, name, answer, 1 if wants else 0, now))
                    done = (f"Thanks, {name}. You answered: {ANSWERS[answer]}. "
                            + ("You asked for announcements about this host's events. "
                               if wants else "You won't get announcements. ")
                            + "Answer again with the same account ID to change it.")
        elif method != "GET":
            return self._message(404, "Not found", "There is no such page.")
        host = f" by {e(ev['host_name'])}" if ev["host_name"] else ""
        status = f"<p role=status><b>{e(done)}</b></p>" if done else ""
        err = f"<p class=err role=alert>{e(error)}</p>" if error else ""
        radios = "".join(f"<label><input type=radio name=answer value={k} required> {v}</label>"
                         for k, v in ANSWERS.items())
        body = (f"<h1>RSVP: {e(ev['title'])}</h1><p>{e(slt(ev['starts']))}{host}</p>{status}{err}"
                f"<form method=post action=/r/{e(code)}>"
                "<label for=name>Your name</label><input id=name name=name type=text maxlength=64 required "
                "autocomplete=nickname>"
                "<label for=acct>Your Second Life profile link or account ID</label>"
                "<input id=acct name=account type=text maxlength=200 required autocomplete=off>"
                "<p class=note>In the viewer: open your profile, then copy its link (secondlife:///app/agent/...). "
                "Never type your password here; this page doesn't need it.</p>"
                f"<fieldset><legend>Are you coming?</legend>{radios}</fieldset>"
                "<label><input type=checkbox name=announcements value=yes> Send me announcements about this host's "
                "events (you can say stop at any time)</label>"
                "<button type=submit>Send my answer</button></form>"
                "<p class=note>The host sees your name, account ID and answer. They can't see anything else "
                "about you from this page.</p>")
        return self._layout(f"RSVP: {ev['title']}", body, 200 if not error else 400)

    def _ics(self, ev: dict) -> Page:
        def esc(t: str) -> str:
            return t.replace("\\", "\\\\").replace(";", "\\;").replace(",", "\\,").replace("\n", "\\n")

        start = datetime.fromtimestamp(ev["starts"], timezone.utc)
        end = start + timedelta(minutes=ev["minutes"])
        fmt = "%Y%m%dT%H%M%SZ"
        stamp = datetime.now(timezone.utc).strftime(fmt)
        lines = ["BEGIN:VCALENDAR", "VERSION:2.0", "PRODID:-//6|7 World Studio//Phone//EN", "BEGIN:VEVENT",
                 f"UID:{ev['eid']}@sw67-phone", f"DTSTAMP:{stamp}", f"DTSTART:{start.strftime(fmt)}",
                 f"DTEND:{end.strftime(fmt)}", f"SUMMARY:{esc(ev['title'])}"]
        if ev["location"]:
            lines.append(f"LOCATION:{esc(ev['location'])}")
        lines.append(f"DESCRIPTION:{esc((ev['details'] or '') + chr(10) + self.cfg.public_url + '/phone/e/' + ev['eid'])}")
        lines += ["BEGIN:VALARM", "ACTION:DISPLAY", f"DESCRIPTION:{esc(ev['title'])} starts in 15 minutes",
                  "TRIGGER:-PT15M", "END:VALARM", "END:VEVENT", "END:VCALENDAR"]
        name = re.sub(r"[^A-Za-z0-9_-]+", "-", ev["title"])[:40].strip("-") or "event"
        return Page(200, "\r\n".join(lines) + "\r\n", "text/calendar; charset=utf-8",
                    {"Content-Disposition": f"attachment; filename=\"{quote(name)}.ics\""})

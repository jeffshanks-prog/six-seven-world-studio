"""The HTTP gateway. JSON in and out; every /v1 route needs a 6|7 session token
(`Authorization: Bearer ...`). Put it behind a TLS reverse proxy in
production; it listens on localhost by default.

Routes:
  GET  /healthz                          liveness, no auth
  GET  /help/ , /help/<id>               help pages as plain HTML, no auth
  GET  /v1/bootstrap                     greeting, starters, limits, mode
  POST /v1/chat                          one turn (Idempotency-Key required)
  GET  /v1/conversations                 the caller's conversations
  GET  /v1/conversations/<id>            one conversation's messages
  POST /v1/conversations/<id>/delete     delete one (also DELETE /v1/conversations/<id>)
  POST /v1/conversations/delete_all      delete all; body {"confirm": true}
  POST /v1/actions/<id>/confirm          carry out a confirmed draft
  POST /v1/actions/<id>/cancel
  GET  /v1/preferences
  POST /v1/preferences/<key>             body {"value": "..."}
  POST /v1/preferences/<key>/delete      (also DELETE /v1/preferences/<key>)
  GET  /v1/owner/support_requests        owner only
"""

from __future__ import annotations

import hmac
import html
import json
import logging
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qsl

from . import __version__, prompt
from .account import make_provider
from .assistant import Assistant, AssistantError, history_ok, make_client
from .auth import AuthError, Principal, issue, verify
from . import drafts
from .knowledge import KnowledgeBase
from .ratelimit import RateLimiter
from .store import Store
from .tools import PREF_KEY, Flags, ToolContext, confirm_action

log = logging.getLogger("kingjahmir")

MAX_BODY = 32 * 1024
_IDEM = re.compile(r"^[A-Za-z0-9_-]{8,80}$")
_CONV = re.compile(r"^conv_[A-Za-z0-9_-]{8,40}$")
_ACT = re.compile(r"^act_[A-Za-z0-9_-]{8,40}$")
_VERSION = re.compile(r"^[0-9][0-9A-Za-z.+-]{0,39}$")


class HttpError(Exception):
    def __init__(self, status: int, code: str, message: str, retryable: bool = False,
                 retry_after: float | None = None):
        super().__init__(message)
        self.status, self.code, self.message = status, code, message
        self.retryable, self.retry_after = retryable, retry_after

    def body(self) -> dict:
        err = {"code": self.code, "message": self.message, "retryable": self.retryable}
        if self.retry_after is not None:
            err["retry_after"] = round(self.retry_after, 1)
        return {"error": err}


class App:
    """Everything the routes do, without the socket layer (tests call it directly)."""

    def __init__(self, cfg, client=None, provider=None, store: Store | None = None):
        self.cfg = cfg
        self.kb = KnowledgeBase(cfg.docs_dir, cfg.public_url)
        self.store = store or Store(cfg.db_path, cfg.database_url)
        self.provider = provider or make_provider(cfg)
        self.assistant = Assistant(cfg, client if client is not None else make_client(cfg))
        self.limiter = RateLimiter(self.store, "u", cfg.rate_per_minute, cfg.rate_burst, cfg.daily_messages)
        self.ip_limiter = RateLimiter(self.store, "ip", cfg.rate_per_minute * 4, cfg.rate_burst * 4,
                                      cfg.daily_messages * 10)
        self.setup_limiter = RateLimiter(self.store, "setup", 5, 5, 20)

    # ---- helpers ----------------------------------------------------------
    def authenticate(self, header: str | None) -> Principal:
        if not header or not header.startswith("Bearer "):
            raise HttpError(401, "not_signed_in", "Sign in to 6|7 to talk with King Jahmir.")
        try:
            p = verify(header[7:].strip(), self.cfg.auth_secret, self.cfg.auth_issuer, self.cfg.owner_ids,
                       self.cfg.dev_mode)
        except AuthError as e:
            raise HttpError(401, e.code, e.message) from e
        if not p.can("chat"):
            raise HttpError(403, "forbidden", "This 6|7 session may not use King Jahmir.")
        return p

    def _conv_or_404(self, p: Principal, cid: str) -> None:
        if not _CONV.match(cid or "") or not self.store.owns_conversation(p.sub, cid):
            raise HttpError(404, "not_found", "That conversation does not exist or was deleted.")

    # ---- routes -----------------------------------------------------------
    def bootstrap(self, p: Principal) -> dict:
        return {
            "assistant": "King Jahmir",
            "is_ai": True,
            "greeting": prompt.GREETING,
            "starters": prompt.STARTERS,
            "development_mode": self.cfg.dev_mode,
            "mock_model": self.cfg.dev_mock_model,
            "account_records": self.provider.name,
            "retention_days": self.cfg.retention_days,
            "max_message_chars": self.cfg.max_message_chars,
            "owner": p.owner,
            "voice": "UNAVAILABLE",
            "help_index": f"{self.cfg.public_url}/help/",
        }

    def draft_lsl(self, p: Principal, body: dict, ip: str) -> dict:
        """A draft LSL script for the Creator Workbench. Text only: nothing here or
        in the viewer compiles, uploads or runs it, and the answer says so."""
        try:
            req, existing = drafts.check_request(body)
        except drafts.DraftError as e:
            raise HttpError(e.status, e.code, e.message) from e
        if not self.store.try_busy(p.sub):
            raise HttpError(429, "busy", "Wait for the answer you asked for before asking again.", True, 2)
        try:
            wait = self.limiter.check(p.sub) or self.ip_limiter.check(ip)
            if wait:
                raise HttpError(429, "rate_limited", "You're asking quickly. Try again shortly.", True, wait)
            try:
                text = self.assistant.complete(drafts.SYSTEM, drafts.messages_for(req, existing))
            except AssistantError as e:
                raise HttpError(e.status, e.code, e.message, e.retryable, e.retry_after) from e
        finally:
            self.store.clear_busy(p.sub)
        code, notes = drafts.split_reply(text)
        if not code:
            return {"code": "", "notes": notes or "No script came back. Try describing it differently.",
                    "compiled": False, "tested": False, "draft_note": drafts.DRAFT_NOTE}
        return {"code": code, "notes": notes, "compiled": False, "tested": False, "draft_note": drafts.DRAFT_NOTE}

    def chat(self, p: Principal, body: dict, idem_key: str | None, ip: str) -> dict:
        if not idem_key or not _IDEM.match(idem_key):
            raise HttpError(400, "bad_request", "Idempotency-Key header (8-80 letters, digits, - or _) is required.")
        text = body.get("message")
        if not isinstance(text, str) or not text.strip():
            raise HttpError(400, "bad_request", "Type a message first.")
        text = text.strip()
        if len(text) > self.cfg.max_message_chars:
            raise HttpError(400, "message_too_long",
                            f"That message is too long. Keep it under {self.cfg.max_message_chars} characters.")
        flags_in = body.get("flags") if isinstance(body.get("flags"), dict) else {}
        flags = Flags(contextual=flags_in.get("contextual") is True,
                      account_help=flags_in.get("account_help") is True,
                      memory=flags_in.get("memory") is True)
        client = body.get("client") if isinstance(body.get("client"), dict) else {}
        version = client.get("viewer_version", "")
        version = version if isinstance(version, str) and _VERSION.match(version) else ""
        screen = body.get("context") if isinstance(body.get("context"), dict) else None
        cid = body.get("conversation_id")

        state, cached = self.store.begin_request(p.sub, idem_key)
        if state == "done":
            return {**cached, "replayed": True}
        if state == "pending":
            raise HttpError(409, "in_progress", "King Jahmir is still answering that message.", True, 2)
        # One message at a time per resident, across every gateway instance.
        if not self.store.try_busy(p.sub):
            self.store.finish_request(p.sub, idem_key, None)
            raise HttpError(429, "busy", "Wait for King Jahmir's answer before sending another message.", True, 2)
        try:
            wait = self.limiter.check(p.sub) or self.ip_limiter.check(ip)
            if wait:
                raise HttpError(429, "rate_limited", "You're sending messages quickly. Try again shortly.",
                                True, wait)
            if cid is None:
                cid = self.store.create_conversation(p.sub, text[:60])
                history: list[dict] = []
            else:
                if not isinstance(cid, str):
                    raise HttpError(400, "bad_request", "conversation_id must be a string.")
                self._conv_or_404(p, cid)
                if self.store.user_turn_count(cid) >= self.cfg.max_turns_per_conversation:
                    raise HttpError(409, "conversation_full",
                                    "This conversation is full. Start a new conversation to keep going.")
                history = self.store.api_messages(cid)
                if not history_ok(history):
                    raise HttpError(409, "conversation_damaged", "Start a new conversation.")
            ctx = ToolContext(principal=p, flags=flags, conversation_id=cid, viewer_version=version,
                              store=self.store, kb=self.kb, provider=self.provider, dev_mode=self.cfg.dev_mode)
            prefs = self.store.preferences(p.sub) if flags.memory else {}
            started = time.time()
            try:
                result = self.assistant.turn(ctx, history, text, screen, prefs)
            except AssistantError as e:
                self.store.cancel_actions(ctx.created_actions)
                raise HttpError(e.status, e.code, e.message, e.retryable, e.retry_after) from e
            except Exception:
                self.store.cancel_actions(ctx.created_actions)
                log.exception("turn failed")
                raise HttpError(500, "internal", "Something went wrong on King Jahmir's side.", True)
            self.store.append_turns(cid, result["new_messages"])
            response = {
                "conversation_id": cid,
                "reply": result["reply"],
                "links": result["links"],
                "actions": result["events"],
                "development_mode": self.cfg.dev_mode,
                "elapsed_ms": int((time.time() - started) * 1000),
            }
            self.store.finish_request(p.sub, idem_key, response)
            return response
        except HttpError:
            self.store.finish_request(p.sub, idem_key, None)
            raise
        finally:
            self.store.clear_busy(p.sub)

    def conversations(self, p: Principal) -> dict:
        return {"conversations": self.store.list_conversations(p.sub), "retention_days": self.cfg.retention_days}

    def conversation(self, p: Principal, cid: str) -> dict:
        self._conv_or_404(p, cid)
        return {"conversation_id": cid, "messages": self.store.history(p.sub, cid)}

    def delete_conversation(self, p: Principal, cid: str) -> dict:
        if not _CONV.match(cid or "") or not self.store.delete_conversation(p.sub, cid):
            # Deleting what is already gone is a success: repeats are safe.
            return {"deleted": False, "already_gone": True}
        return {"deleted": True}

    def delete_all(self, p: Principal, body: dict) -> dict:
        if body.get("confirm") is not True:
            raise HttpError(400, "confirmation_required", "Confirm deleting every conversation.")
        return {"deleted": self.store.delete_all_conversations(p.sub)}

    def action(self, p: Principal, aid: str, verb: str) -> dict:
        if not _ACT.match(aid or ""):
            raise HttpError(404, "not_found", "That request no longer exists.")
        if verb == "cancel":
            ok = self.store.resolve_action(p.sub, aid, "cancelled", None)
            a = self.store.get_action(p.sub, aid)
            if a is None:
                raise HttpError(404, "not_found", "That request no longer exists.")
            return {"status": "cancelled" if ok else a["status"]}
        out = confirm_action(self.store, p.sub, aid)
        if out["status"] == "not_found":
            raise HttpError(404, "not_found", "That request no longer exists.")
        if out["status"] in ("cancelled", "expired"):
            raise HttpError(409, out["status"], f"That request was {out['status']}; ask King Jahmir again.")
        return out

    def preferences(self, p: Principal) -> dict:
        return {"preferences": self.store.preferences(p.sub)}

    def set_preference(self, p: Principal, key: str, body: dict) -> dict:
        value = body.get("value")
        if not PREF_KEY.match(key or "") or not isinstance(value, str) or not value.strip() or len(value) > 200:
            raise HttpError(400, "bad_request", "A preference needs a lower_snake_case name and a value "
                                                "of at most 200 characters.")
        self.store.set_preference(p.sub, key, value.strip())
        return {"saved": True, "preferences": self.store.preferences(p.sub)}

    def delete_preference(self, p: Principal, key: str) -> dict:
        return {"deleted": self.store.delete_preference(p.sub, key), "preferences": self.store.preferences(p.sub)}

    def owner_support(self, p: Principal) -> dict:
        if not p.owner:
            raise HttpError(403, "forbidden", "Only the 6|7 owner can see support requests.")
        return {"support_requests": self.store.support_requests()}

    def cron_purge(self, header: str | None) -> dict:
        """The retention clean-up, for hosts that run it on a schedule (Vercel
        Cron sends `Authorization: Bearer $CRON_SECRET`)."""
        secret = self.cfg.cron_secret
        if not secret or not hmac.compare_digest((header or "").encode(), f"Bearer {secret}".encode()):
            raise HttpError(401, "unauthorized", "Not allowed.")
        n = self.store.purge(self.cfg.retention_days)
        log.info(json.dumps({"event": "retention_purge", "conversations": n}))
        return {"ok": True, "deleted_conversations": n}

    # ---- owner sign-in page ------------------------------------------------
    def setup_token(self, code: str, agent_id: str, ip: str) -> str:
        """An owner's sign-in token, for KJ_SETUP_CODE and an id in KJ_OWNER_IDS.
        Until the 6|7 account service issues tokens, this lets the owner sign
        in without the command line."""
        if not self.cfg.setup_code:
            raise HttpError(404, "not_found", "The sign-in page is turned off.")
        wait = self.setup_limiter.check(ip)
        if wait:
            raise HttpError(429, "rate_limited", "Too many tries. Wait a minute and try again.", True, wait)
        agent_id = (agent_id or "").strip().lower()
        if not hmac.compare_digest(code.strip().encode(), self.cfg.setup_code.encode()) \
                or agent_id not in self.cfg.owner_ids:
            raise HttpError(403, "forbidden", "That setup code or avatar key is not right.")
        log.info(json.dumps({"event": "owner_token_issued"}))
        return issue(self.cfg.auth_secret, self.cfg.auth_issuer, agent_id, scopes=["chat", "account:read"],
                     roles=["owner"], lifetime=30 * 86400)

    def setup_page(self, token: str | None = None, error: str | None = None) -> str:
        style = ("body{font-family:sans-serif;max-width:40em;margin:2em auto;padding:0 1em;background:#111111;"
                 "color:#FFFFFF;line-height:1.5}h1{color:#D4AF37}label{display:block;margin-top:1em}"
                 "input,textarea{width:100%;padding:.5em;background:#333333;color:#FFFFFF;border:1px solid #D4AF37}"
                 "button{margin-top:1em;padding:.6em 1.2em;background:#D4AF37;color:#111111;border:0;"
                 "font-weight:bold}.err{color:#FFB3B3}")
        if token:
            body = ("<h1>Your King Jahmir sign-in</h1><p>Copy all of this text. In the viewer open "
                    "<b>King Jahmir &gt; Settings &amp; privacy</b>, paste it under <b>6|7 sign-in</b> and press "
                    "<b>Sign in</b>. It works for 30 days. Keep it private, like a password.</p>"
                    f"<textarea rows=6 readonly>{html.escape(token)}</textarea>")
        else:
            err = f'<p class=err role=alert>{html.escape(error)}</p>' if error else ""
            body = ("<h1>King Jahmir owner sign-in</h1>"
                    "<p>Enter the setup code you saved in Vercel as KJ_SETUP_CODE and your avatar's key.</p>"
                    f"{err}<form method=post action=/setup>"
                    "<label for=code>Setup code</label><input id=code name=code type=password required "
                    "autocomplete=off>"
                    "<label for=agent>Avatar key (UUID)</label><input id=agent name=agent required "
                    "autocomplete=off>"
                    "<button type=submit>Get my sign-in</button></form>")
        return (f"<!doctype html><html lang=en><meta charset=utf-8><meta name=viewport "
                f"content='width=device-width,initial-scale=1'><title>King Jahmir sign-in</title>"
                f"<style>{style}</style><body>{body}</body></html>")

    # ---- help pages -------------------------------------------------------
    def help_page(self, page_id: str | None) -> str:
        style = ("body{font-family:sans-serif;max-width:46em;margin:2em auto;padding:0 1em;background:#111111;"
                 "color:#FFFFFF;line-height:1.5}a{color:#D4AF37}pre{white-space:pre-wrap;font-family:inherit}"
                 "h1{color:#D4AF37}")
        if not page_id:
            items = "".join(f'<li><a href="/help/{html.escape(pid)}">{html.escape(pg.title)}</a></li>'
                            for pid, pg in sorted(self.kb.pages.items(), key=lambda x: x[1].title))
            body = f"<h1>6|7 World Studio help</h1><ul>{items}</ul>"
            title = "6|7 help"
        else:
            page = self.kb.pages.get(page_id)
            if page is None:
                raise HttpError(404, "not_found", "No such help page.")
            title = page.title
            body = (f"<h1>{html.escape(page.title)}</h1><pre>{html.escape(page.body)}</pre>"
                    f'<p><a href="/help/">All help pages</a></p>')
        return (f"<!doctype html><html lang=en><meta charset=utf-8><title>{html.escape(title)}</title>"
                f"<style>{style}</style><body>{body}</body></html>")


class Handler(BaseHTTPRequestHandler):
    app: App = None  # set by serve()
    server_version = f"KingJahmir/{__version__}"
    sys_version = ""
    timeout = 30  # seconds a client may take to send its request

    def log_message(self, *_args):  # replaced by _log: no paths with ids, no bodies
        pass

    def _log(self, status: int, started: float, who: str = "-") -> None:
        route = re.sub(r"/(conv|act)_[A-Za-z0-9_-]+", r"/\1_*", self.path.split("?")[0])
        route = re.sub(r"^/v1/preferences/[^/]+", "/v1/preferences/*", route)
        log.info(json.dumps({"route": f"{self.command} {route}", "status": status,
                             "ms": int((time.time() - started) * 1000), "user": who}))

    def _client_ip(self) -> str:
        if self.app.cfg.trust_forwarded_for:
            fwd = self.headers.get("X-Forwarded-For", "")
            if fwd:
                return fwd.split(",")[0].strip()[:64]
        return self.client_address[0]

    def _send(self, status: int, payload, content_type="application/json; charset=utf-8", extra=None):
        data = payload.encode() if isinstance(payload, str) else json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        if content_type.startswith("text/html"):
            self.send_header("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def _body(self) -> dict:
        length = self.headers.get("Content-Length")
        if length is None:
            return {}
        try:
            n = int(length)
        except ValueError as e:
            raise HttpError(400, "bad_request", "Bad Content-Length.") from e
        if n < 0 or n > MAX_BODY:
            raise HttpError(413, "too_large", "Request too large.")
        raw = self.rfile.read(n) if n else b""
        if not raw:
            return {}
        try:
            data = json.loads(raw)
        except ValueError as e:
            raise HttpError(400, "bad_request", "Body must be JSON.") from e
        if not isinstance(data, dict):
            raise HttpError(400, "bad_request", "Body must be a JSON object.")
        return data

    def _form(self) -> dict:
        """A small urlencoded form body."""
        try:
            n = int(self.headers.get("Content-Length") or 0)
        except ValueError as e:
            raise HttpError(400, "bad_request", "Bad Content-Length.") from e
        if n < 0 or n > 4096:
            raise HttpError(413, "too_large", "Request too large.")
        raw = self.rfile.read(n).decode("utf-8", "replace") if n else ""
        return {k: v for k, v in parse_qsl(raw, keep_blank_values=True)}

    def _dispatch(self):
        started = time.time()
        who = "-"
        try:
            path = self.path.split("?")[0]
            if path == "/healthz" and self.command == "GET":
                self._send(200, {"ok": True, "version": __version__, "development_mode": self.app.cfg.dev_mode,
                                 "ai_client": self.app.assistant.client is not None,
                                 "mock_model": self.app.cfg.dev_mock_model})
                return self._log(200, started)
            if path in ("/help", "/help/") and self.command == "GET":
                self._send(200, self.app.help_page(None), "text/html; charset=utf-8")
                return self._log(200, started)
            m = re.fullmatch(r"/help/([a-z0-9][a-z0-9_-]{0,63})", path)
            if m and self.command == "GET":
                self._send(200, self.app.help_page(m.group(1)), "text/html; charset=utf-8")
                return self._log(200, started)
            if path == "/cron/purge" and self.command == "GET":
                self._send(200, self.app.cron_purge(self.headers.get("Authorization")))
                return self._log(200, started)
            if path == "/setup" and self.command in ("GET", "POST"):
                if not self.app.cfg.setup_code:
                    raise HttpError(404, "not_found", "No such route.")
                if self.command == "GET":
                    self._send(200, self.app.setup_page(), "text/html; charset=utf-8")
                    return self._log(200, started)
                form = self._form()
                try:
                    page = self.app.setup_page(self.app.setup_token(form.get("code", ""), form.get("agent", ""),
                                                                    self._client_ip()))
                    status = 200
                except HttpError as e:
                    page, status = self.app.setup_page(error=e.message), e.status
                self._send(status, page, "text/html; charset=utf-8")
                return self._log(status, started)
            if not path.startswith("/v1/"):
                raise HttpError(404, "not_found", "No such route.")
            body = self._body() if self.command in ("POST", "PUT") else {}
            p = self.app.authenticate(self.headers.get("Authorization"))
            who = p.log_id
            out = self._route(p, self.command, path, body)
            self._send(200, out)
            self._log(200, started, who)
        except HttpError as e:
            extra = {"Retry-After": str(max(1, int(e.retry_after)))} if e.retry_after else None
            self._send(e.status, e.body(), extra=extra)
            self._log(e.status, started, who)
        except Exception:
            log.exception("unhandled error")
            self._send(500, HttpError(500, "internal", "Something went wrong.", True).body())
            self._log(500, started, who)

    def _route(self, p: Principal, method: str, path: str, body: dict) -> dict:
        a = self.app
        if method == "GET" and path == "/v1/bootstrap":
            return a.bootstrap(p)
        if method == "POST" and path == "/v1/chat":
            return a.chat(p, body, self.headers.get("Idempotency-Key"), self._client_ip())
        if method == "GET" and path == "/v1/conversations":
            return a.conversations(p)
        if method == "POST" and path == "/v1/conversations/delete_all":
            return a.delete_all(p, body)
        m = re.fullmatch(r"/v1/conversations/([^/]+)(/delete)?", path)
        if m:
            if method == "GET" and not m.group(2):
                return a.conversation(p, m.group(1))
            if (method == "POST" and m.group(2)) or (method == "DELETE" and not m.group(2)):
                return a.delete_conversation(p, m.group(1))
        m = re.fullmatch(r"/v1/actions/([^/]+)/(confirm|cancel)", path)
        if m and method == "POST":
            return a.action(p, m.group(1), m.group(2))
        if method == "GET" and path == "/v1/preferences":
            return a.preferences(p)
        m = re.fullmatch(r"/v1/preferences/([^/]+)(/delete)?", path)
        if m:
            if (method == "POST" and m.group(2)) or method == "DELETE":
                return a.delete_preference(p, m.group(1))
            if method in ("POST", "PUT"):
                return a.set_preference(p, m.group(1), body)
        if method == "GET" and path == "/v1/owner/support_requests":
            return a.owner_support(p)
        if method == "POST" and path == "/v1/draft/lsl":
            return a.draft_lsl(p, body, self._client_ip())
        raise HttpError(404, "not_found", "No such route.")

    do_GET = do_POST = do_PUT = do_DELETE = _dispatch


def _purge_loop(app: App, stop: threading.Event) -> None:
    while not stop.is_set():
        try:
            n = app.store.purge(app.cfg.retention_days)
            if n:
                log.info(json.dumps({"event": "retention_purge", "conversations": n}))
        except Exception:
            log.exception("purge failed")
        stop.wait(3600)


def serve(cfg, app: App | None = None) -> None:
    app = app or App(cfg)
    if cfg.dev_mode:
        log.warning("DEVELOPMENT MODE: development tokens accepted%s",
                    "; mock model, no AI" if cfg.dev_mock_model else "")
    if app.assistant.client is None:
        log.warning("No Anthropic credentials: chat answers 'ai_not_configured' until ANTHROPIC_API_KEY is set")
    Handler.app = app
    httpd = ThreadingHTTPServer((cfg.host, cfg.port), Handler)
    httpd.daemon_threads = True
    stop = threading.Event()
    threading.Thread(target=_purge_loop, args=(app, stop), daemon=True).start()
    log.info(json.dumps({"event": "listening", "host": cfg.host, "port": cfg.port}))
    try:
        httpd.serve_forever()
    finally:
        stop.set()
        httpd.server_close()

"""Phone Companion tests, through the real HTTP handler: the viewer's API with
a 6|7 token, then the phone's pages with the session cookie they set.

    cd services/king-jahmir && python -m unittest tests.test_phone -v
"""

from __future__ import annotations

import http.client
import re
import threading
import time
import unittest
from http.server import ThreadingHTTPServer
from urllib.parse import urlencode

from kingjahmir.server import App, Handler
from tests.test_gateway import OTHER, USER, bearer, cfg, reset_db

ATTACKER = "https://evil.example"


class Client:
    """A browser stand-in: keeps the phone cookie, never follows redirects."""

    def __init__(self, port: int, origin: str):
        self.port, self.origin, self.cookie = port, origin, ""

    def req(self, method: str, path: str, form: dict | None = None, origin: str | None = None,
            auth: str | None = None, json_body: str | None = None):
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        headers = {}
        body = None
        if form is not None:
            body = urlencode(form).encode()
            headers["Content-Type"] = "application/x-www-form-urlencoded"
            headers["Origin"] = origin or self.origin
        if json_body is not None:
            body = json_body.encode()
            headers["Content-Type"] = "application/json"
        if auth:
            headers["Authorization"] = auth
        if self.cookie:
            headers["Cookie"] = self.cookie
        c.request(method, path, body=body, headers=headers)
        r = c.getresponse()
        text = r.read().decode()
        set_cookie = r.getheader("Set-Cookie")
        if set_cookie:
            value = set_cookie.split(";")[0]
            self.cookie = "" if value.endswith("=") else value
        c.close()
        return r.status, text, r


class PhoneTests(unittest.TestCase):
    def setUp(self):
        reset_db()
        self.srv = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        port = self.srv.server_port
        self.app = App(cfg(KJ_PUBLIC_URL=f"http://127.0.0.1:{port}"))
        Handler.app = self.app
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()
        self.origin = f"http://127.0.0.1:{port}"
        self.viewer = Client(port, self.origin)
        self.phone = Client(port, self.origin)

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()
        self.app.store.close()

    def api(self, method, path, body="{}", sub=USER):
        import json
        status, text, _ = self.viewer.req(method, path, auth=bearer(sub), json_body=body)
        return status, json.loads(text)

    def send_event(self, sub=USER, **over):
        import json
        ev = {"id": "evt-1700000000-1", "title": "Friday Night Live", "starts": int(time.time()) + 3 * 3600,
              "minutes": 120, "location": "https://maps.secondlife.com/secondlife/Club/128/128/22",
              "details": "Bring the glow sticks.", "host_name": "Jeff", "shots": ["Wide of the stage", "DJ close-up"]}
        ev.update(over)
        status, out = self.api("POST", "/v1/phone/events", json.dumps(ev), sub)
        self.assertEqual(status, 200, out)
        return out["event"]

    def pair_phone(self, sub=USER):
        status, out = self.api("POST", "/v1/phone/pair", '{"host_name": "Jeff"}', sub)
        self.assertEqual(status, 200)
        self.assertRegex(out["code"], r"^[A-Z2-9]{4}-[A-Z2-9]{4}$")
        status, _, r = self.phone.req("POST", "/phone/pair", {"code": out["code"].lower()})
        self.assertEqual(status, 303)
        cookie = r.getheader("Set-Cookie")
        for flag in ("HttpOnly", "SameSite=Strict", "Path=/"):
            self.assertIn(flag, cookie)
        return out["code"]

    # ---- pairing and sessions ---------------------------------------------
    def test_pairing_code_works_once_and_needs_no_sl_password(self):
        status, page, _ = self.phone.req("GET", "/phone")
        self.assertEqual(status, 200)
        self.assertIn("never asks for your Second Life password", page)
        self.assertNotIn("type=password", page)
        code = self.pair_phone()
        status, page, _ = self.phone.req("GET", "/phone")
        self.assertIn("Your events", page)
        other = Client(self.phone.port, self.origin)
        status, page, _ = other.req("POST", "/phone/pair", {"code": code})
        self.assertEqual(status, 400)
        self.assertIn("used already", page)
        self.assertEqual(other.cookie, "")

    def test_wrong_and_old_codes_are_refused(self):
        status, _, _ = self.phone.req("POST", "/phone/pair", {"code": "AAAA-AAAA"})
        self.assertEqual(status, 400)
        status, out = self.api("POST", "/v1/phone/pair")
        self.app.store._n("UPDATE kj_phone_pairs SET expires=?", (time.time() - 1,))
        status, _, _ = self.phone.req("POST", "/phone/pair", {"code": out["code"]})
        self.assertEqual(status, 400)

    def test_viewer_api_needs_a_6_7_token(self):
        status, text, _ = self.viewer.req("POST", "/v1/phone/pair", json_body="{}")
        self.assertEqual(status, 401)

    def test_forged_or_expired_cookies_are_ignored(self):
        self.pair_phone()
        payload, sig = self.phone.cookie.split("=", 1)[1].split(".")
        self.phone.cookie = f"sw67phone={payload}.{sig[:-2]}AA"
        self.assertIn("Pairing code", self.phone.req("GET", "/phone")[1])
        old = self.app.phone.make_cookie("host", USER, lifetime=10, now=time.time() - 3600)
        self.phone.cookie = f"sw67phone={old}"
        self.assertIn("Pairing code", self.phone.req("GET", "/phone")[1])

    def test_sign_out_clears_the_cookie(self):
        self.pair_phone()
        status, _, _ = self.phone.req("POST", "/phone/signout", {})
        self.assertEqual(status, 303)
        self.assertEqual(self.phone.cookie, "")

    # ---- events, reminders, shot list ------------------------------------
    def test_event_page_reminder_and_shot_list(self):
        eid = self.send_event()
        self.pair_phone()
        status, page, _ = self.phone.req("GET", "/phone")
        self.assertIn("Friday Night Live", page)
        status, page, _ = self.phone.req("GET", f"/phone/e/{eid}")
        self.assertEqual(status, 200)
        self.assertIn("Starts in 2 hours", page)
        self.assertIn("SLT", page)
        self.assertIn('href="https://maps.secondlife.com/secondlife/Club/128/128/22"', page)
        self.assertIn("0 of 2 done", page)
        self.assertIn("[TO DO]", page)  # state in words, not colour alone
        status, ics, r = self.phone.req("GET", f"/phone/e/{eid}/calendar")
        self.assertEqual(status, 200)
        self.assertIn("text/calendar", r.getheader("Content-Type"))
        self.assertIn("TRIGGER:-PT15M", ics)
        self.assertIn("SUMMARY:Friday Night Live", ics)
        shot = re.search(r"/phone/e/[^/]+/shot/(\d+)/toggle", page).group(1)
        status, _, _ = self.phone.req("POST", f"/phone/e/{eid}/shot/{shot}/toggle", {})
        self.assertEqual(status, 303)
        page = self.phone.req("GET", f"/phone/e/{eid}")[1]
        self.assertIn("1 of 2 done", page)
        self.assertIn("[DONE]", page)
        # Sending the event again keeps what was ticked off, and takes new shots.
        self.send_event(shots=["Wide of the stage", "DJ close-up", "Crowd"])
        page = self.phone.req("GET", f"/phone/e/{eid}")[1]
        self.assertIn("1 of 3 done", page)

    def test_only_the_host_sees_their_events(self):
        eid = self.send_event(sub=OTHER)
        self.pair_phone(USER)
        status, page, _ = self.phone.req("GET", f"/phone/e/{eid}")
        self.assertEqual(status, 404)
        self.assertNotIn("Friday", self.phone.req("GET", "/phone")[1])
        status, out = self.api("GET", "/v1/phone/events/evt-1700000000-1/rsvps", sub=USER)
        self.assertEqual(status, 404)

    def test_bad_events_are_refused(self):
        import json
        for bad in ({"id": "../x", "title": "t", "starts": 1800000000}, {"id": "a", "title": "", "starts": 1800000000},
                    {"id": "a", "title": "t", "starts": "soon"}, {"id": "a", "title": "t", "starts": True}):
            with self.subTest(bad):
                status, _ = self.api("POST", "/v1/phone/events", json.dumps(bad))
                self.assertEqual(status, 400)

    def test_text_is_escaped_and_odd_locations_are_not_links(self):
        eid = self.send_event(title="<script>alert(1)</script>", location="javascript:alert(1)")
        self.pair_phone()
        page = self.phone.req("GET", f"/phone/e/{eid}")[1]
        self.assertNotIn("<script>alert", page)
        self.assertIn("&lt;script&gt;", page)
        self.assertNotIn('href="javascript', page)

    # ---- team -----------------------------------------------------------------
    def invite(self, eid, name="Mia", role="dj"):
        status, page, _ = self.phone.req("POST", f"/phone/e/{eid}/invite", {"name": name, "role": role})
        self.assertEqual(status, 200)
        return re.search(r"value='(http://[^']+/phone/join/[A-Za-z0-9_-]+)'", page).group(1)

    def test_team_member_joins_once_chats_and_can_be_removed(self):
        eid = self.send_event()
        self.pair_phone()
        link = self.invite(eid)
        path = link.split(self.origin)[1]
        member = Client(self.phone.port, self.origin)
        status, page, _ = member.req("GET", path)  # a link preview: shows a button, uses nothing
        self.assertIn("Join", page)
        self.assertEqual(member.cookie, "")
        status, _, _ = member.req("POST", path, {})
        self.assertEqual(status, 303)
        late = Client(self.phone.port, self.origin)
        self.assertEqual(late.req("POST", path, {})[0], 404)  # one use

        page = member.req("GET", f"/phone/e/{eid}")[1]
        self.assertIn("on the team as DJ: Mia", page)
        self.assertNotIn("RSVPs are", page)  # host-only sections stay hidden
        self.assertNotIn("Make an invite link", page)
        self.assertEqual(member.req("POST", f"/phone/e/{eid}/rsvp_toggle", {})[0], 403)
        member.req("POST", f"/phone/e/{eid}/chat", {"text": "Mic check <b>ok</b>"})
        self.phone.req("POST", f"/phone/e/{eid}/chat", {"text": "Thanks!"})
        page = self.phone.req("GET", f"/phone/e/{eid}")[1]
        self.assertIn("Mia (DJ)", page)
        self.assertIn("Mic check &lt;b&gt;ok&lt;/b&gt;", page)
        self.assertIn("Jeff (host)", page)

        mid = re.search(r"/member/(tm_[A-Za-z0-9_-]+)/revoke", page).group(1)
        self.phone.req("POST", f"/phone/e/{eid}/member/{mid}/revoke", {})
        self.assertEqual(member.req("GET", f"/phone/e/{eid}")[0], 200)
        self.assertIn("Pairing code", member.req("GET", f"/phone/e/{eid}")[1])  # signed out

    def test_member_sees_only_their_event(self):
        eid = self.send_event()
        other = self.send_event(id="evt-2", title="Sunday Brunch")
        self.pair_phone()
        path = self.invite(eid).split(self.origin)[1]
        member = Client(self.phone.port, self.origin)
        member.req("POST", path, {})
        self.assertEqual(member.req("GET", f"/phone/e/{other}")[0], 404)
        status, _, r = member.req("GET", "/phone")
        self.assertEqual(r.getheader("Location"), f"/phone/e/{eid}")

    def test_posts_from_other_sites_are_refused(self):
        eid = self.send_event()
        self.pair_phone()
        status, page, _ = self.phone.req("POST", f"/phone/e/{eid}/chat", {"text": "hi"}, origin=ATTACKER)
        self.assertEqual(status, 403)
        self.assertIn("No messages yet", self.phone.req("GET", f"/phone/e/{eid}")[1])

    # ---- RSVPs ---------------------------------------------------------------
    def test_rsvps_are_closed_until_opened_and_import_into_party_contacts(self):
        eid = self.send_event()
        self.pair_phone()
        page = self.phone.req("GET", f"/phone/e/{eid}")[1]
        self.assertIn("RSVPs are CLOSED", page)
        code = self.app.store._q("SELECT rsvp_code FROM kj_phone_events WHERE eid=?", (eid,))[0]["rsvp_code"]
        guest = Client(self.phone.port, self.origin)
        self.assertEqual(guest.req("GET", f"/r/{code}")[0], 404)
        self.phone.req("POST", f"/phone/e/{eid}/rsvp_toggle", {})
        page = self.phone.req("GET", f"/phone/e/{eid}")[1]
        self.assertIn("RSVPs are OPEN", page)
        self.assertIn(f"/r/{code}", page)

        status, form, _ = guest.req("GET", f"/r/{code}")
        self.assertEqual(status, 200)
        self.assertNotIn("checked", form)  # announcements are never ticked for anyone
        self.assertNotIn("type=password", form)
        status, page, _ = guest.req("POST", f"/r/{code}", {"name": "Sky", "account": "nope", "answer": "going"})
        self.assertEqual(status, 400)
        a = "12345678-aaaa-bbbb-cccc-1234567890ab"
        guest.req("POST", f"/r/{code}", {"name": "Sky", "answer": "going",
                                         "account": f"secondlife:///app/agent/{a.upper()}/about"})
        status, page, _ = guest.req("POST", f"/r/{code}", {"name": "Sky", "account": a, "answer": "maybe",
                                                          "announcements": "yes"})
        self.assertIn("You answered: Maybe", page)
        b = "22345678-aaaa-bbbb-cccc-1234567890ab"
        guest.req("POST", f"/r/{code}", {"name": "Rae", "account": b, "answer": "not_going"})

        status, out = self.api("GET", "/v1/phone/events/evt-1700000000-1/rsvps")
        self.assertEqual(out["format"], "sw67.rsvps")
        self.assertFalse(out["verified"])
        by_id = {r["id"]: r for r in out["rsvps"]}
        self.assertEqual(len(by_id), 2)  # answering again replaced the first answer
        self.assertEqual(by_id[a], {"id": a, "name": "Sky", "answer": "maybe", "announcements": True})
        self.assertFalse(by_id[b]["announcements"])

        self.phone.req("POST", f"/phone/e/{eid}/rsvp_toggle", {})
        self.assertEqual(guest.req("GET", f"/r/{code}")[0], 404)

    # ---- removal and retention -----------------------------------------------
    def test_delete_and_purge(self):
        eid = self.send_event()
        self.pair_phone()
        status, out = self.api("POST", "/v1/phone/events/evt-1700000000-1/delete")
        self.assertTrue(out["deleted"])
        self.assertEqual(self.phone.req("GET", f"/phone/e/{eid}")[0], 404)
        old = self.send_event(id="evt-old", starts=int(time.time()) - 60 * 86400)
        self.assertEqual(self.app.phone.purge(), 1)
        self.assertIsNone(self.app.phone._event(old))

    def test_invite_and_rsvp_codes_stay_out_of_logs(self):
        eid = self.send_event()
        self.pair_phone()
        path = self.invite(eid).split(self.origin)[1]
        with self.assertLogs("kingjahmir", "INFO") as logs:
            Client(self.phone.port, self.origin).req("GET", path)
            Client(self.phone.port, self.origin).req("GET", "/r/abcdefghij")
        joined = "\n".join(logs.output)
        self.assertNotIn(path.rsplit("/", 1)[1], joined)
        self.assertNotIn("abcdefghij", joined)


if __name__ == "__main__":
    unittest.main()

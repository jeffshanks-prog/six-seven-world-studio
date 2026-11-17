"""Vercel entry point: every request is rewritten here (vercel.json) and
served by the same handler the standalone gateway uses."""

from __future__ import annotations

import logging
import sys
from pathlib import Path
from urllib.parse import parse_qsl, urlencode, urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import os  # noqa: E402

from kingjahmir.config import Config, ConfigError  # noqa: E402
from kingjahmir.server import App, Handler  # noqa: E402

logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s", stream=sys.stderr)

# The settings the owner types into Vercel. Only their names are ever shown.
REQUIRED = ("ANTHROPIC_API_KEY", "KJ_DATABASE_URL", "KJ_AUTH_SECRET", "CRON_SECRET", "KJ_SETUP_CODE")


def setup_problem(env: dict) -> str:
    """What stops the gateway starting, in words safe to show anyone (names, never values)."""
    missing = [n for n in REQUIRED if not env.get(n, "").strip()] if env.get("VERCEL") else []
    if missing:
        return "These settings are not saved yet: " + ", ".join(missing) + "."
    try:
        Config.from_env(env)
    except ConfigError as e:
        return str(e) + "."
    return ""


DB_PROBLEM = (
    "King Jahmir can't reach his database. On Vercel, KJ_DATABASE_URL must be Supabase's "
    "Transaction pooler address (it ends in pooler.supabase.com:6543), not the Direct connection."
)

SETUP_PROBLEM = setup_problem(dict(os.environ))
if SETUP_PROBLEM:
    logging.error("King Jahmir is not set up: %s", SETUP_PROBLEM)


def _start() -> str:
    """Build the app once per instance; on a database failure, say so and try again next request."""
    if SETUP_PROBLEM:
        return SETUP_PROBLEM
    if getattr(Handler, "app", None) is None:
        try:
            Handler.app = App(Config.from_env())  # reused by this instance's requests
        except Exception as e:  # noqa: BLE001 - shown to the owner, logged in full
            if type(e).__module__.split(".")[0] != "psycopg":
                raise
            logging.error("King Jahmir can't reach the database: %s", e)
            return DB_PROBLEM
    return ""


_start()


class handler(Handler):  # noqa: N801 - the name Vercel's Python runtime looks for
    def _not_set_up(self, problem: str):
        """Says what is missing, instead of Vercel's crash page."""
        body = (
            "King Jahmir is not set up yet.\n\n" + problem + "\n\n"
            "Fix it in Vercel (king-jahmir > Settings > Environment Variables), "
            "then redeploy.\n"
        ).encode()
        self.send_response(503)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _dispatch(self):
        problem = _start()
        if problem:
            return self._not_set_up(problem)
        # vercel.json passes the original path as ?__kjpath=...; put it back.
        parts = urlsplit(self.path)
        query = parse_qsl(parts.query, keep_blank_values=True)
        original = next((v for k, v in query if k == "__kjpath"), None)
        if original is not None:
            rest = urlencode([(k, v) for k, v in query if k != "__kjpath"])
            self.path = "/" + original.lstrip("/") + (f"?{rest}" if rest else "")
        return super()._dispatch()

    do_GET = do_POST = do_PUT = do_DELETE = do_HEAD = _dispatch

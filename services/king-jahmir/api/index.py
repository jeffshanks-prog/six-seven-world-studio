"""Vercel entry point: every request is rewritten here (vercel.json) and
served by the same handler the standalone gateway uses."""

from __future__ import annotations

import logging
import sys
from pathlib import Path
from urllib.parse import parse_qsl, urlencode, urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import os  # noqa: E402
from http.server import BaseHTTPRequestHandler  # noqa: E402

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


SETUP_PROBLEM = setup_problem(dict(os.environ))
if SETUP_PROBLEM:
    logging.error("King Jahmir is not set up: %s", SETUP_PROBLEM)
else:
    Handler.app = App(Config.from_env())  # once per instance, reused by its requests


class _NotSetUp(BaseHTTPRequestHandler):
    """Answers every request with what is missing, instead of Vercel's crash page."""

    def _dispatch(self):
        body = (
            "King Jahmir is not set up yet.\n\n" + SETUP_PROBLEM + "\n\n"
            "Add them in Vercel (king-jahmir > Settings > Environment Variables), "
            "then redeploy.\n"
        ).encode()
        self.send_response(503)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    do_GET = do_POST = do_PUT = do_DELETE = do_HEAD = _dispatch


class _Gateway(Handler):
    def _dispatch(self):
        # vercel.json passes the original path as ?__kjpath=...; put it back.
        parts = urlsplit(self.path)
        query = parse_qsl(parts.query, keep_blank_values=True)
        original = next((v for k, v in query if k == "__kjpath"), None)
        if original is not None:
            rest = urlencode([(k, v) for k, v in query if k != "__kjpath"])
            self.path = "/" + original.lstrip("/") + (f"?{rest}" if rest else "")
        return super()._dispatch()

    do_GET = do_POST = do_PUT = do_DELETE = _dispatch


handler = _NotSetUp if SETUP_PROBLEM else _Gateway  # the name Vercel's Python runtime looks for

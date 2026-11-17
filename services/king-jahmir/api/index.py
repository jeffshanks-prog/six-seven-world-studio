"""Vercel entry point: every request is rewritten here (vercel.json) and
served by the same handler the standalone gateway uses."""

from __future__ import annotations

import logging
import sys
from pathlib import Path
from urllib.parse import parse_qsl, urlencode, urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from kingjahmir.config import Config  # noqa: E402
from kingjahmir.server import App, Handler  # noqa: E402

logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s", stream=sys.stderr)

Handler.app = App(Config.from_env())  # once per instance, reused by its requests


class handler(Handler):  # noqa: N801 - the name Vercel's Python runtime looks for
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

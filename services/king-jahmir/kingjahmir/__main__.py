"""python -m kingjahmir serve | issue-token | check-docs | bundle-docs"""

from __future__ import annotations

import argparse
import logging
import sys

from .auth import issue
from .config import Config, ConfigError


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="kingjahmir")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("serve", help="run the gateway")
    sub.add_parser("check-docs", help="load and validate the help pages and feature registry")
    sub.add_parser("bundle-docs", help="copy doc/sw67/help into the package (for Vercel)")
    t = sub.add_parser("issue-token", help="issue a 6|7 session token (until the account service exists)")
    t.add_argument("agent_id", help="the resident's Second Life agent UUID")
    t.add_argument("--account", action="store_true", help="add the account:read scope")
    t.add_argument("--owner", action="store_true", help="add the owner role (also needs KJ_OWNER_IDS)")
    t.add_argument("--hours", type=int, default=12)
    t.add_argument("--dev", action="store_true", help="development token (accepted only with KJ_DEV_MODE=1)")
    args = ap.parse_args(argv)

    if args.cmd == "bundle-docs":  # needs no configuration
        from .knowledge import bundle_docs
        n = bundle_docs()
        print(f"copied {n} files into kingjahmir/help")
        return 0
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s", stream=sys.stderr)
    try:
        cfg = Config.from_env()
    except ConfigError as e:
        print(f"configuration error: {e}", file=sys.stderr)
        return 2
    if args.cmd == "issue-token":
        scopes = ["chat"] + (["account:read"] if args.account else [])
        roles = ["owner"] if args.owner else []
        print(issue(cfg.auth_secret, cfg.auth_issuer, args.agent_id, scopes=scopes, roles=roles,
                    lifetime=args.hours * 3600, dev=args.dev))
        return 0
    if args.cmd == "check-docs":
        from .knowledge import KnowledgeBase
        kb = KnowledgeBase(cfg.docs_dir, cfg.public_url)
        print(f"{len(kb.pages)} help pages, {len(kb.sections)} sections, {len(kb.features)} features, "
              f"{len(kb.screens)} screens: OK")
        return 0
    from .server import serve
    serve(cfg)
    return 0


if __name__ == "__main__":
    sys.exit(main())

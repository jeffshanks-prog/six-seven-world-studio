"""Configuration, read once from the environment. See README.md for each variable."""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path

_REPO_DOCS = Path(__file__).resolve().parents[3] / "doc" / "sw67" / "help"
# A copy of the help pages inside the package, for hosts that deploy only this
# folder (Vercel). `python -m kingjahmir bundle-docs` refreshes it; a test
# fails while it differs from doc/sw67/help.
BUNDLED_DOCS = Path(__file__).resolve().parent / "help"
DEFAULT_DOCS = _REPO_DOCS if (_REPO_DOCS / "features.json").is_file() else BUNDLED_DOCS

EFFORTS = ("low", "medium", "high", "xhigh", "max")


class ConfigError(ValueError):
    pass


def _bool(env: dict, name: str, default: bool = False) -> bool:
    v = env.get(name)
    if v is None or v == "":
        return default
    return v.strip().lower() in ("1", "true", "yes", "on")


def _int(env: dict, name: str, default: int, lo: int, hi: int) -> int:
    v = env.get(name)
    if v is None or v == "":
        return default
    try:
        n = int(v)
    except ValueError as e:
        raise ConfigError(f"{name} must be a whole number") from e
    if not lo <= n <= hi:
        raise ConfigError(f"{name} must be between {lo} and {hi}")
    return n


@dataclass(frozen=True)
class Config:
    host: str = "127.0.0.1"
    port: int = 8767
    public_url: str = "http://127.0.0.1:8767"
    docs_dir: Path = DEFAULT_DOCS
    db_path: str = "kingjahmir.sqlite3"
    database_url: str = ""
    cron_secret: str = ""
    auth_secret: bytes = b""
    auth_issuer: str = "sw67-accounts"
    owner_ids: frozenset = field(default_factory=frozenset)
    dev_mode: bool = False
    dev_mock_model: bool = False
    model: str = "claude-opus-5-5"
    effort: str = "low"
    max_tokens: int = 4096
    model_timeout: float = 60.0
    max_tool_rounds: int = 6
    web_search: bool = False
    rate_per_minute: int = 8
    rate_burst: int = 4
    daily_messages: int = 200
    retention_days: int = 30
    max_message_chars: int = 4000
    max_turns_per_conversation: int = 60
    account_api_url: str = ""
    account_api_token: str = ""
    trust_forwarded_for: bool = False

    @staticmethod
    def from_env(env: dict | None = None) -> "Config":
        env = dict(os.environ if env is None else env)
        dev = _bool(env, "KJ_DEV_MODE")
        secret = env.get("KJ_AUTH_SECRET", "").encode()
        if len(secret) < 32:
            raise ConfigError(
                "KJ_AUTH_SECRET must be at least 32 bytes; it signs 6|7 session tokens"
            )
        effort = env.get("KJ_EFFORT", "low")
        if effort not in EFFORTS:
            raise ConfigError(f"KJ_EFFORT must be one of {', '.join(EFFORTS)}")
        mock = _bool(env, "KJ_DEV_MOCK_MODEL")
        if mock and not dev:
            raise ConfigError("KJ_DEV_MOCK_MODEL needs KJ_DEV_MODE=1")
        port = _int(env, "KJ_PORT", 8767, 1, 65535)
        host = env.get("KJ_HOST", "127.0.0.1")
        owners = frozenset(
            s.strip().lower() for s in env.get("KJ_OWNER_IDS", "").split(",") if s.strip()
        )
        account_url = env.get("KJ_ACCOUNT_API_URL", "").rstrip("/")
        if account_url and not (account_url.startswith("https://") or dev):
            raise ConfigError("KJ_ACCOUNT_API_URL must be https outside development mode")
        database_url = env.get("KJ_DATABASE_URL", "").strip()
        if database_url and not database_url.startswith(("postgres://", "postgresql://")):
            raise ConfigError("KJ_DATABASE_URL must be a postgresql:// connection string")
        if env.get("VERCEL") and not database_url:
            raise ConfigError("On Vercel, set KJ_DATABASE_URL: a function's own disk does not keep data")
        public_url = env.get("KJ_PUBLIC_URL", "")
        if not public_url and env.get("VERCEL_PROJECT_PRODUCTION_URL"):
            public_url = "https://" + env["VERCEL_PROJECT_PRODUCTION_URL"]
        public_url = public_url or f"http://{host}:{port}"
        return Config(
            host=host,
            port=port,
            public_url=public_url.rstrip("/"),
            docs_dir=Path(env.get("KJ_DOCS_DIR") or str(DEFAULT_DOCS)),
            db_path=env.get("KJ_DB_PATH", "kingjahmir.sqlite3"),
            database_url=database_url,
            cron_secret=env.get("CRON_SECRET", ""),
            auth_secret=secret,
            auth_issuer=env.get("KJ_AUTH_ISSUER", "sw67-accounts"),
            owner_ids=owners,
            dev_mode=dev,
            dev_mock_model=mock,
            model=env.get("KJ_MODEL", "claude-opus-5-5"),
            effort=effort,
            max_tokens=_int(env, "KJ_MAX_TOKENS", 4096, 256, 64000),
            model_timeout=float(_int(env, "KJ_MODEL_TIMEOUT_SECONDS", 60, 5, 600)),
            max_tool_rounds=_int(env, "KJ_MAX_TOOL_ROUNDS", 6, 1, 12),
            web_search=_bool(env, "KJ_WEB_SEARCH"),
            rate_per_minute=_int(env, "KJ_RATE_PER_MINUTE", 8, 1, 600),
            rate_burst=_int(env, "KJ_RATE_BURST", 4, 1, 100),
            daily_messages=_int(env, "KJ_DAILY_MESSAGES", 200, 1, 100000),
            retention_days=_int(env, "KJ_RETENTION_DAYS", 30, 1, 3650),
            max_message_chars=_int(env, "KJ_MAX_MESSAGE_CHARS", 4000, 100, 20000),
            max_turns_per_conversation=_int(env, "KJ_MAX_TURNS", 60, 2, 1000),
            account_api_url=account_url,
            account_api_token=env.get("KJ_ACCOUNT_API_TOKEN", ""),
            # Vercel sets X-Forwarded-For itself, so it can be trusted there.
            trust_forwarded_for=_bool(env, "KJ_TRUST_FORWARDED_FOR", default=bool(env.get("VERCEL"))),
        )

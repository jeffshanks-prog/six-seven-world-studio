"""6|7 session tokens: compact JWTs signed with HS256.

There is no 6|7 account service yet (the billing and account service is
Milestone 5). Until it exists, tokens are issued with `python -m kingjahmir
issue-token` by whoever holds KJ_AUTH_SECRET. The account service will issue
the same format after a resident signs in, so the viewer and this gateway
will not change when it arrives.

Claims:
  iss    must equal KJ_AUTH_ISSUER
  sub    the resident's Second Life agent UUID
  name   display name (optional, never sent to the model)
  iat, exp  seconds since the epoch; lifetime at most 30 days
  scopes list: "chat", "account:read"
  roles  list: "owner" grants owner tools only if sub is also in KJ_OWNER_IDS
  dev    true for development tokens; refused unless KJ_DEV_MODE=1
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import re
import time
from dataclasses import dataclass

MAX_LIFETIME = 30 * 24 * 3600
LEEWAY = 60
_UUID = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")
KNOWN_SCOPES = ("chat", "account:read")


class AuthError(Exception):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


@dataclass(frozen=True)
class Principal:
    sub: str
    scopes: frozenset
    roles: frozenset
    dev: bool
    owner: bool

    def can(self, scope: str) -> bool:
        return scope in self.scopes

    @property
    def log_id(self) -> str:
        """A short, non-reversible id for logs."""
        return hashlib.sha256(self.sub.encode()).hexdigest()[:12]


def _b64(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def _unb64(text: str) -> bytes:
    if not re.fullmatch(r"[A-Za-z0-9_-]*", text):
        raise AuthError("invalid_token", "Malformed token.")
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def issue(secret: bytes, issuer: str, sub: str, *, scopes=("chat",), roles=(),
          lifetime: int = 12 * 3600, dev: bool = False, now: float | None = None) -> str:
    sub = sub.lower()
    if not _UUID.match(sub):
        raise ValueError("sub must be a Second Life agent UUID")
    if lifetime <= 0 or lifetime > MAX_LIFETIME:
        raise ValueError("lifetime out of range")
    for s in scopes:
        if s not in KNOWN_SCOPES:
            raise ValueError(f"unknown scope {s}")
    now = int(time.time() if now is None else now)
    header = {"alg": "HS256", "typ": "JWT"}
    claims = {"iss": issuer, "sub": sub, "iat": now, "exp": now + lifetime,
              "scopes": list(scopes), "roles": list(roles)}
    if dev:
        claims["dev"] = True
    signing = _b64(json.dumps(header, separators=(",", ":")).encode()) + "." + \
        _b64(json.dumps(claims, separators=(",", ":"), sort_keys=True).encode())
    sig = hmac.new(secret, signing.encode(), hashlib.sha256).digest()
    return signing + "." + _b64(sig)


def verify(token: str, secret: bytes, issuer: str, owner_ids: frozenset,
           dev_mode: bool, now: float | None = None) -> Principal:
    if not token or len(token) > 4096:
        raise AuthError("invalid_token", "Missing or oversized token.")
    parts = token.split(".")
    if len(parts) != 3:
        raise AuthError("invalid_token", "Malformed token.")
    try:
        header = json.loads(_unb64(parts[0]))
        claims = json.loads(_unb64(parts[1]))
    except (ValueError, UnicodeDecodeError) as e:
        raise AuthError("invalid_token", "Malformed token.") from e
    if not isinstance(header, dict) or header.get("alg") != "HS256":
        raise AuthError("invalid_token", "Unsupported token algorithm.")
    expected = hmac.new(secret, (parts[0] + "." + parts[1]).encode(), hashlib.sha256).digest()
    if not hmac.compare_digest(expected, _unb64(parts[2])):
        raise AuthError("invalid_token", "Token signature does not match.")
    if not isinstance(claims, dict):
        raise AuthError("invalid_token", "Malformed token.")
    now = time.time() if now is None else now
    if claims.get("iss") != issuer:
        raise AuthError("invalid_token", "Token was not issued for this service.")
    exp, iat = claims.get("exp"), claims.get("iat")
    if not isinstance(exp, int) or not isinstance(iat, int):
        raise AuthError("invalid_token", "Token has no valid lifetime.")
    if exp - iat > MAX_LIFETIME or iat > now + LEEWAY:
        raise AuthError("invalid_token", "Token lifetime is not acceptable.")
    if now > exp + LEEWAY:
        raise AuthError("token_expired", "Your 6|7 session has expired. Sign in again.")
    sub = claims.get("sub")
    if not isinstance(sub, str) or not _UUID.match(sub):
        raise AuthError("invalid_token", "Token has no valid resident id.")
    dev = claims.get("dev") is True
    if dev and not dev_mode:
        raise AuthError("invalid_token", "Development tokens are not accepted here.")
    scopes = claims.get("scopes", [])
    roles = claims.get("roles", [])
    if not isinstance(scopes, list) or not isinstance(roles, list):
        raise AuthError("invalid_token", "Malformed token.")
    scopes = frozenset(s for s in scopes if s in KNOWN_SCOPES)
    roles = frozenset(r for r in roles if isinstance(r, str))
    # Owner needs both the signed role and the server's own list.
    owner = "owner" in roles and sub in owner_ids
    return Principal(sub=sub, scopes=scopes, roles=roles, dev=dev, owner=owner)

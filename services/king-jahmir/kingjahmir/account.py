"""Account records: the signed-in resident's Avatar Life data, and owner-only
records. These are read-only, and only ever for the token's own `sub`.

No Avatar Life records service exists yet, so the default provider answers
UNAVAILABLE for everything. `HttpAccountProvider` is the client for the
service once it is built (contract in README.md). `DevMockProvider` exists
only in development mode and labels every value as mock data.
"""

from __future__ import annotations

import json
import urllib.error
import urllib.request

USER_SECTIONS = ("stats", "level", "careers_skills", "activities", "activity_results", "entitlements")
OWNER_SECTIONS = ("approval_queue", "products", "suspensions", "audit")
MOCK_LABEL = "DEVELOPMENT MOCK DATA - not real records"


class Unavailable(Exception):
    def __init__(self, reason: str):
        super().__init__(reason)
        self.reason = reason


class AccountProvider:
    name = "none"

    def user_section(self, sub: str, section: str) -> dict:
        raise Unavailable("No Avatar Life records service is connected yet, so there are no records to read.")

    def owner_section(self, section: str) -> dict:
        raise Unavailable("No approvals or records service is connected yet.")


class HttpAccountProvider(AccountProvider):
    """GET {base}/v1/residents/{sub}/{section} and GET {base}/v1/owner/{section},
    with a service bearer token. Responses must be JSON objects under 64 KB."""

    name = "http"
    MAX_BYTES = 64 * 1024

    def __init__(self, base_url: str, token: str, timeout: float = 5.0):
        self.base = base_url.rstrip("/")
        self.token = token
        self.timeout = timeout

    def _get(self, path: str) -> dict:
        req = urllib.request.Request(self.base + path, headers={
            "Authorization": f"Bearer {self.token}", "Accept": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:  # noqa: S310 (https enforced in config)
                body = resp.read(self.MAX_BYTES + 1)
        except urllib.error.HTTPError as e:
            if e.code == 404:
                raise Unavailable("The records service has no record of this for you.") from e
            raise Unavailable(f"The records service answered with an error ({e.code}).") from e
        except (urllib.error.URLError, TimeoutError, OSError) as e:
            raise Unavailable("The records service could not be reached.") from e
        if len(body) > self.MAX_BYTES:
            raise Unavailable("The records service sent too much data.")
        try:
            data = json.loads(body)
        except ValueError as e:
            raise Unavailable("The records service sent data that could not be read.") from e
        if not isinstance(data, dict):
            raise Unavailable("The records service sent data that could not be read.")
        return data

    def user_section(self, sub: str, section: str) -> dict:
        return self._get(f"/v1/residents/{sub}/{section}")

    def owner_section(self, section: str) -> dict:
        return self._get(f"/v1/owner/{section}")


class DevMockProvider(AccountProvider):
    name = "dev-mock"

    _USER = {
        "stats": {"mana": 62, "wellness": 70, "energy": 45, "hygiene": 38, "mood": 66,
                  "reputation": 12, "dedication": 30, "social": 55},
        "level": {"level": 4, "xp": 1240, "xp_for_next_level": 1500,
                  "next_level_requirements": ["Reach 1500 XP", "Complete 2 career shifts"]},
        "careers_skills": {"careers": [{"id": "dj", "name": "DJ", "rank": 1}],
                           "skills": [{"id": "mixing", "name": "Mixing", "level": 2}]},
        "activities": {"active": [], "cooldowns": [{"activity": "shower", "product": "Mock Shower v1",
                                                    "ends_in_minutes": 12}]},
        "activity_results": {"recent": [{"activity": "shower", "product": "Mock Shower v1",
                                         "product_approved": True, "session_completed": False,
                                         "result": "no_change",
                                         "reason": "Session ended after 20 s; 60 s are required."}]},
        "entitlements": {"plan": "Basic (sandbox)", "enforced": False},
    }
    _OWNER = {
        "approval_queue": {"pending": [{"product": "Mock Bed", "creator": "mock-creator", "submitted": "2026-11-16"}]},
        "products": {"registered": [{"product": "Mock Shower v1", "approved": True}]},
        "suspensions": {"active": []},
        "audit": {"events": [{"event": "approved", "product": "Mock Shower v1", "by": "owner"}]},
    }

    def user_section(self, sub: str, section: str) -> dict:
        return {"data_source": MOCK_LABEL, **self._USER[section]}

    def owner_section(self, section: str) -> dict:
        return {"data_source": MOCK_LABEL, **self._OWNER[section]}


def make_provider(cfg) -> AccountProvider:
    if cfg.account_api_url:
        return HttpAccountProvider(cfg.account_api_url, cfg.account_api_token)
    if cfg.dev_mode:
        return DevMockProvider()
    return AccountProvider()

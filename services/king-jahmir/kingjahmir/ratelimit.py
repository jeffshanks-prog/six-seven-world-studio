"""Rate limits: a token bucket per key and a daily message cap, kept in the
store so that every gateway instance (or serverless invocation) shares them."""

from __future__ import annotations

from .store import Store


class RateLimiter:
    def __init__(self, store: Store, prefix: str, per_minute: int, burst: int, daily: int):
        self.store = store
        self.prefix = prefix
        self.rate = per_minute / 60.0
        self.burst = float(burst)
        self.daily = daily

    def check(self, key: str, now: float | None = None) -> float:
        """0 when allowed (and counted), else the seconds to wait."""
        return self.store.take_token(f"{self.prefix}:{key}"[:200], self.rate, self.burst, self.daily, now)

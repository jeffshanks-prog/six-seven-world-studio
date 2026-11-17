"""In-memory rate limits: a token bucket per key, and a daily message cap.

One gateway process holds them. Running several processes behind a load
balancer needs a shared store (Redis or the like); see README.md.
"""

from __future__ import annotations

import threading
import time


class RateLimiter:
    def __init__(self, per_minute: int, burst: int, daily: int):
        self.rate = per_minute / 60.0
        self.burst = float(burst)
        self.daily = daily
        self._buckets: dict[str, tuple[float, float]] = {}
        self._days: dict[str, tuple[int, int]] = {}
        self._lock = threading.Lock()

    def check(self, key: str, now: float | None = None) -> float:
        """0 when allowed (and counted), else the seconds to wait."""
        now = time.time() if now is None else now
        day = int(now // 86400)
        with self._lock:
            d, n = self._days.get(key, (day, 0))
            if d != day:
                d, n = day, 0
            if n >= self.daily:
                return float(86400 - now % 86400)
            tokens, last = self._buckets.get(key, (self.burst, now))
            tokens = min(self.burst, tokens + (now - last) * self.rate)
            if tokens < 1.0:
                self._buckets[key] = (tokens, now)
                return (1.0 - tokens) / self.rate
            self._buckets[key] = (tokens - 1.0, now)
            self._days[key] = (d, n + 1)
            if len(self._buckets) > 50000:  # bound memory under abuse
                self._buckets.clear()
            return 0.0

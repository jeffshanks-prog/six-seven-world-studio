"""Storage: conversations, retry records, pending actions, preferences,
support requests, rate-limit buckets and the one-message-at-a-time marks.

Two backends with the same SQL:

- SQLite (`KJ_DB_PATH`), for one long-running gateway process;
- PostgreSQL (`KJ_DATABASE_URL`), for serverless hosting (Vercel with
  Supabase), where every request may land on a different instance, so
  everything shared between requests lives in the database.

Statements are written with `?` placeholders; the PostgreSQL backend turns
them into `%s`. Every check-then-write that two requests could race on is a
single conditional statement (an upsert with a condition, or an update with
the old state in its WHERE), so it is safe on either backend without locks.

Nothing here stores tokens or secrets. Conversation text is kept only for
KJ_RETENTION_DAYS and is deleted at once when the user asks.
"""

from __future__ import annotations

import json
import secrets
import sqlite3
import threading
import time
from contextlib import contextmanager
from typing import Any, Iterator

# Column types differ only in these names; {id} is the turns key.
_TABLES = """
CREATE TABLE IF NOT EXISTS kj_conversations (
  id TEXT PRIMARY KEY,
  uid TEXT NOT NULL,
  created {real} NOT NULL,
  updated {real} NOT NULL,
  title TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS kj_conversations_uid ON kj_conversations(uid, updated);
CREATE TABLE IF NOT EXISTS kj_turns (
  id {id},
  conv TEXT NOT NULL REFERENCES kj_conversations(id) ON DELETE CASCADE,
  role TEXT NOT NULL,
  display TEXT,
  api_json TEXT NOT NULL,
  created {real} NOT NULL
);
CREATE INDEX IF NOT EXISTS kj_turns_conv ON kj_turns(conv, id);
CREATE TABLE IF NOT EXISTS kj_requests (
  uid TEXT NOT NULL,
  idem_key TEXT NOT NULL,
  status TEXT NOT NULL,
  response_json TEXT,
  created {real} NOT NULL,
  PRIMARY KEY (uid, idem_key)
);
CREATE TABLE IF NOT EXISTS kj_pending_actions (
  id TEXT PRIMARY KEY,
  uid TEXT NOT NULL,
  conv TEXT,
  kind TEXT NOT NULL,
  payload_json TEXT NOT NULL,
  status TEXT NOT NULL,
  result_json TEXT,
  created {real} NOT NULL,
  resolved {real}
);
CREATE TABLE IF NOT EXISTS kj_preferences (
  uid TEXT NOT NULL,
  key TEXT NOT NULL,
  value TEXT NOT NULL,
  updated {real} NOT NULL,
  PRIMARY KEY (uid, key)
);
CREATE TABLE IF NOT EXISTS kj_support_requests (
  id TEXT PRIMARY KEY,
  uid TEXT NOT NULL,
  category TEXT NOT NULL,
  summary TEXT NOT NULL,
  details TEXT NOT NULL,
  viewer_version TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL,
  created {real} NOT NULL
);
CREATE TABLE IF NOT EXISTS kj_rate_buckets (
  key TEXT PRIMARY KEY,
  tokens {real} NOT NULL,
  last {real} NOT NULL,
  day INTEGER NOT NULL,
  n INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS kj_busy (
  uid TEXT PRIMARY KEY,
  until {real} NOT NULL
);
"""

ACTION_TTL = 24 * 3600
REQUEST_TTL = 24 * 3600
STALE_PENDING = 15 * 60  # a request still pending after this crashed; let it run again
BUSY_TTL = 5 * 60  # a message mark older than this belongs to a request that died


def new_id(prefix: str) -> str:
    return f"{prefix}_{secrets.token_urlsafe(12)}"


class _SQLite:
    name = "sqlite"
    for_update = ""

    def __init__(self, path: str):
        self._db = sqlite3.connect(path, check_same_thread=False, isolation_level=None)
        self._db.row_factory = sqlite3.Row
        self._db.execute("PRAGMA foreign_keys=ON")
        if path != ":memory:":
            self._db.execute("PRAGMA journal_mode=WAL")
        self._db.executescript(_TABLES.format(real="REAL", id="INTEGER PRIMARY KEY AUTOINCREMENT"))

    def run(self, sql: str, args: tuple) -> tuple[list[dict], int]:
        cur = self._db.execute(sql, args)
        rows = [dict(r) for r in cur.fetchall()] if cur.description else []
        return rows, cur.rowcount

    def begin(self) -> None:
        self._db.execute("BEGIN IMMEDIATE")

    def commit(self) -> None:
        self._db.execute("COMMIT")

    def rollback(self) -> None:
        self._db.execute("ROLLBACK")

    def close(self) -> None:
        self._db.close()


class _Postgres:
    """One connection, opened on first use and opened again if it drops.
    Prepared statements are off so it works through Supabase's transaction
    pooler (port 6543), which serverless functions should use."""

    name = "postgres"
    for_update = " FOR UPDATE"

    def __init__(self, url: str):
        import psycopg  # only needed when PostgreSQL is configured
        from psycopg.rows import dict_row

        self._psycopg = psycopg
        self._dict_row = dict_row
        self._url = url
        self._conn = None
        self._in_tx = False
        with self._lock_free_conn() as conn:
            with conn.cursor() as cur:
                # One statement at a time: the pooler runs no multi-statement scripts.
                for stmt in _TABLES.format(real="DOUBLE PRECISION", id="BIGSERIAL PRIMARY KEY").split(";"):
                    if stmt.strip():
                        cur.execute(stmt)

    @contextmanager
    def _lock_free_conn(self) -> Iterator[Any]:
        if self._conn is None or self._conn.closed:
            self._conn = self._psycopg.connect(self._url, autocommit=True, prepare_threshold=None,
                                               row_factory=self._dict_row, connect_timeout=10)
        yield self._conn

    def _exec(self, sql: str, args: tuple) -> tuple[list[dict], int]:
        with self._lock_free_conn() as conn, conn.cursor() as cur:
            cur.execute(sql, args)
            rows = list(cur.fetchall()) if cur.description else []
            return rows, cur.rowcount

    def run(self, sql: str, args: tuple) -> tuple[list[dict], int]:
        sql = sql.replace("?", "%s")
        try:
            return self._exec(sql, args)
        except self._psycopg.OperationalError:
            if self._in_tx:  # a dropped connection loses the transaction: let it fail
                raise
            self._drop()  # the connection dropped between requests: one fresh try
            return self._exec(sql, args)

    def _drop(self) -> None:
        try:
            if self._conn is not None:
                self._conn.close()
        except Exception:
            pass
        self._conn = None

    def begin(self) -> None:
        self.run("BEGIN", ())
        self._in_tx = True

    def commit(self) -> None:
        self._in_tx = False
        self.run("COMMIT", ())

    def rollback(self) -> None:
        self._in_tx = False
        try:
            self._exec("ROLLBACK", ())
        except Exception:
            self._drop()

    def close(self) -> None:
        self._drop()


class Store:
    def __init__(self, path: str = ":memory:", database_url: str = ""):
        self._db = _Postgres(database_url) if database_url else _SQLite(path)
        self._lock = threading.RLock()  # one connection: one statement or transaction at a time

    @property
    def backend(self) -> str:
        return self._db.name

    def close(self) -> None:
        with self._lock:
            self._db.close()

    def _q(self, sql: str, args: tuple = ()) -> list[dict]:
        with self._lock:
            return self._db.run(sql, args)[0]

    def _n(self, sql: str, args: tuple = ()) -> int:
        """Runs a write; the number of rows it changed."""
        with self._lock:
            return self._db.run(sql, args)[1]

    @contextmanager
    def _tx(self) -> Iterator[None]:
        with self._lock:
            self._db.begin()
            try:
                yield
                self._db.commit()
            except BaseException:
                self._db.rollback()
                raise

    # ---- conversations -------------------------------------------------
    def create_conversation(self, user: str, title: str) -> str:
        cid = new_id("conv")
        now = time.time()
        self._n("INSERT INTO kj_conversations(id,uid,created,updated,title) VALUES(?,?,?,?,?)",
                (cid, user, now, now, title[:80]))
        return cid

    def owns_conversation(self, user: str, cid: str) -> bool:
        return bool(self._q("SELECT 1 AS one FROM kj_conversations WHERE id=? AND uid=?", (cid, user)))

    def list_conversations(self, user: str) -> list[dict]:
        return self._q("SELECT id,title,created,updated FROM kj_conversations WHERE uid=? "
                       "ORDER BY updated DESC LIMIT 50", (user,))

    def api_messages(self, cid: str) -> list[dict]:
        rows = self._q("SELECT api_json FROM kj_turns WHERE conv=? ORDER BY id", (cid,))
        return [json.loads(r["api_json"]) for r in rows]

    def user_turn_count(self, cid: str) -> int:
        return int(self._q("SELECT COUNT(*) AS n FROM kj_turns WHERE conv=? AND role='user' "
                           "AND display IS NOT NULL", (cid,))[0]["n"])

    def history(self, user: str, cid: str) -> list[dict] | None:
        if not self.owns_conversation(user, cid):
            return None
        rows = self._q("SELECT id,role,display,created FROM kj_turns WHERE conv=? AND display IS NOT NULL "
                       "ORDER BY id", (cid,))
        return [{"id": r["id"], "role": r["role"], "text": r["display"], "created": r["created"]}
                for r in rows]

    def append_turns(self, cid: str, turns: list[tuple[str, str | None, dict]]) -> None:
        """Appends (role, display text or None, API message) in one transaction.
        History is append-only: earlier messages are never rewritten."""
        now = time.time()
        with self._tx():
            for role, display, msg in turns:
                self._db.run("INSERT INTO kj_turns(conv,role,display,api_json,created) VALUES(?,?,?,?,?)",
                             (cid, role, display, json.dumps(msg, separators=(",", ":")), now))
            self._db.run("UPDATE kj_conversations SET updated=? WHERE id=?", (now, cid))

    def delete_conversation(self, user: str, cid: str) -> bool:
        with self._tx():
            n = self._db.run("DELETE FROM kj_conversations WHERE id=? AND uid=?", (cid, user))[1]
            self._db.run("DELETE FROM kj_pending_actions WHERE conv=? AND uid=? AND status='pending'", (cid, user))
        return n > 0

    def delete_all_conversations(self, user: str) -> int:
        with self._tx():
            n = self._db.run("DELETE FROM kj_conversations WHERE uid=?", (user,))[1]
            self._db.run("DELETE FROM kj_pending_actions WHERE uid=? AND status='pending'", (user,))
            self._db.run("DELETE FROM kj_requests WHERE uid=?", (user,))
        return n

    # ---- idempotent requests -------------------------------------------
    def begin_request(self, user: str, key: str) -> tuple[str, dict | None]:
        """('new', None) when this key is new or its last attempt failed or
        died; ('done', response) to replay; ('pending', None) while it runs.
        Two requests with one key never both get 'new'."""
        now = time.time()
        if self._n("INSERT INTO kj_requests(uid,idem_key,status,created) VALUES(?,?,'pending',?) "
                   "ON CONFLICT (uid,idem_key) DO NOTHING", (user, key, now)) == 1:
            return "new", None
        if self._n("UPDATE kj_requests SET status='pending', created=? WHERE uid=? AND idem_key=? "
                   "AND (status='failed' OR (status='pending' AND created<?))",
                   (now, user, key, now - STALE_PENDING)) == 1:
            return "new", None
        rows = self._q("SELECT status,response_json FROM kj_requests WHERE uid=? AND idem_key=?", (user, key))
        if rows and rows[0]["status"] == "done":
            return "done", json.loads(rows[0]["response_json"])
        return "pending", None

    def finish_request(self, user: str, key: str, response: dict | None) -> None:
        if response is None:
            self._n("UPDATE kj_requests SET status='failed' WHERE uid=? AND idem_key=?", (user, key))
        else:
            self._n("UPDATE kj_requests SET status='done', response_json=? WHERE uid=? AND idem_key=?",
                    (json.dumps(response), user, key))

    # ---- one message at a time per resident ------------------------------
    def try_busy(self, user: str) -> bool:
        """Marks the resident as having a message in flight; False if one already is."""
        now = time.time()
        return self._n("INSERT INTO kj_busy(uid,until) VALUES(?,?) ON CONFLICT (uid) DO UPDATE "
                       "SET until=excluded.until WHERE kj_busy.until<?", (user, now + BUSY_TTL, now)) == 1

    def clear_busy(self, user: str) -> None:
        self._n("DELETE FROM kj_busy WHERE uid=?", (user,))

    # ---- rate limits -----------------------------------------------------
    def take_token(self, key: str, rate: float, burst: float, daily: int, now: float | None = None) -> float:
        """Token bucket plus a daily cap, shared by every gateway instance.
        0 when allowed (and counted), else the seconds to wait."""
        now = time.time() if now is None else now
        day = int(now // 86400)
        with self._tx():
            self._db.run("INSERT INTO kj_rate_buckets(key,tokens,last,day,n) VALUES(?,?,?,?,0) "
                         "ON CONFLICT (key) DO NOTHING", (key, burst, now, day))
            row = self._db.run("SELECT tokens,last,day,n FROM kj_rate_buckets WHERE key=?" + self._db.for_update,
                               (key,))[0][0]
            n = int(row["n"]) if int(row["day"]) == day else 0
            if n >= daily:
                return float(86400 - now % 86400)
            tokens = min(burst, float(row["tokens"]) + max(0.0, now - float(row["last"])) * rate)
            if tokens < 1.0:
                self._db.run("UPDATE kj_rate_buckets SET tokens=?, last=?, day=?, n=? WHERE key=?",
                             (tokens, now, day, n, key))
                return (1.0 - tokens) / rate
            self._db.run("UPDATE kj_rate_buckets SET tokens=?, last=?, day=?, n=? WHERE key=?",
                         (tokens - 1.0, now, day, n + 1, key))
            return 0.0

    # ---- pending actions ------------------------------------------------
    def add_action(self, user: str, conv: str | None, kind: str, payload: dict) -> str:
        aid = new_id("act")
        self._n("INSERT INTO kj_pending_actions(id,uid,conv,kind,payload_json,status,created) "
                "VALUES(?,?,?,?,?,'pending',?)", (aid, user, conv, kind, json.dumps(payload), time.time()))
        return aid

    def get_action(self, user: str, aid: str) -> dict | None:
        rows = self._q("SELECT id,uid,conv,kind,payload_json,status,result_json,created,resolved "
                       "FROM kj_pending_actions WHERE id=? AND uid=?", (aid, user))
        if not rows:
            return None
        r = dict(rows[0])
        r["user"] = r.pop("uid")
        r["payload"] = json.loads(r.pop("payload_json"))
        result = r.pop("result_json")
        r["result"] = json.loads(result) if result else None
        if r["status"] == "pending" and time.time() - r["created"] > ACTION_TTL:
            r["status"] = "expired"
        return r

    def resolve_action(self, user: str, aid: str, status: str, result: dict | None) -> bool:
        """Moves a pending action to its end state once; False if it was not pending."""
        now = time.time()
        return self._n("UPDATE kj_pending_actions SET status=?, result_json=?, resolved=? "
                       "WHERE id=? AND uid=? AND status='pending' AND created>?",
                       (status, json.dumps(result) if result is not None else None, now,
                        aid, user, now - ACTION_TTL)) == 1

    def set_action_result(self, aid: str, result: dict) -> None:
        self._n("UPDATE kj_pending_actions SET result_json=? WHERE id=?", (json.dumps(result), aid))

    def cancel_actions(self, ids: list[str]) -> None:
        for aid in ids:
            self._n("UPDATE kj_pending_actions SET status='cancelled', resolved=? WHERE id=? AND status='pending'",
                    (time.time(), aid))

    # ---- preferences ----------------------------------------------------
    def preferences(self, user: str) -> dict[str, str]:
        return {r["key"]: r["value"] for r in
                self._q("SELECT key,value FROM kj_preferences WHERE uid=? ORDER BY key", (user,))}

    def set_preference(self, user: str, key: str, value: str) -> None:
        self._n("INSERT INTO kj_preferences(uid,key,value,updated) VALUES(?,?,?,?) "
                "ON CONFLICT (uid,key) DO UPDATE SET value=excluded.value, updated=excluded.updated",
                (user, key, value, time.time()))

    def delete_preference(self, user: str, key: str) -> bool:
        return self._n("DELETE FROM kj_preferences WHERE uid=? AND key=?", (user, key)) > 0

    def count_preferences(self, user: str) -> int:
        return int(self._q("SELECT COUNT(*) AS n FROM kj_preferences WHERE uid=?", (user,))[0]["n"])

    # ---- support requests ----------------------------------------------
    def add_support_request(self, user: str, payload: dict[str, Any]) -> str:
        sid = new_id("sup")
        self._n("INSERT INTO kj_support_requests(id,uid,category,summary,details,viewer_version,status,created) "
                "VALUES(?,?,?,?,?,?,'new',?)",
                (sid, user, payload["category"], payload["summary"], payload["details"],
                 payload.get("viewer_version", ""), time.time()))
        return sid

    def support_requests(self, limit: int = 100) -> list[dict]:
        return self._q('SELECT id,uid AS "user",category,summary,details,viewer_version,status,created '
                       "FROM kj_support_requests ORDER BY created DESC LIMIT ?", (limit,))

    # ---- retention ------------------------------------------------------
    def purge(self, retention_days: int) -> int:
        now = time.time()
        keep = now - retention_days * 86400
        with self._tx():
            n = self._db.run("DELETE FROM kj_conversations WHERE updated<?", (keep,))[1]
            self._db.run("DELETE FROM kj_requests WHERE created<?", (now - REQUEST_TTL,))
            self._db.run("UPDATE kj_pending_actions SET status='expired' WHERE status='pending' AND created<?",
                         (now - ACTION_TTL,))
            self._db.run("DELETE FROM kj_pending_actions WHERE created<?", (keep,))
            self._db.run("DELETE FROM kj_rate_buckets WHERE last<?", (now - 2 * 86400,))
            self._db.run("DELETE FROM kj_busy WHERE until<?", (now,))
        return n

"""SQLite storage: conversations, retry records, pending actions, preferences
and support requests. One file, one connection guarded by a lock; the gateway
serves one viewer request per thread and every statement here is short.

Nothing here stores tokens or secrets. Conversation text is kept only for
KJ_RETENTION_DAYS and is deleted at once when the user asks.
"""

from __future__ import annotations

import json
import secrets
import sqlite3
import threading
import time
from typing import Any

SCHEMA = """
PRAGMA journal_mode=WAL;
PRAGMA foreign_keys=ON;
CREATE TABLE IF NOT EXISTS conversations (
  id TEXT PRIMARY KEY,
  user TEXT NOT NULL,
  created REAL NOT NULL,
  updated REAL NOT NULL,
  title TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS conversations_user ON conversations(user, updated);
CREATE TABLE IF NOT EXISTS turns (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  conv TEXT NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
  role TEXT NOT NULL,
  display TEXT,
  api_json TEXT NOT NULL,
  created REAL NOT NULL
);
CREATE INDEX IF NOT EXISTS turns_conv ON turns(conv, id);
CREATE TABLE IF NOT EXISTS requests (
  user TEXT NOT NULL,
  idem_key TEXT NOT NULL,
  status TEXT NOT NULL,
  response_json TEXT,
  created REAL NOT NULL,
  PRIMARY KEY (user, idem_key)
);
CREATE TABLE IF NOT EXISTS pending_actions (
  id TEXT PRIMARY KEY,
  user TEXT NOT NULL,
  conv TEXT,
  kind TEXT NOT NULL,
  payload_json TEXT NOT NULL,
  status TEXT NOT NULL,
  result_json TEXT,
  created REAL NOT NULL,
  resolved REAL
);
CREATE TABLE IF NOT EXISTS preferences (
  user TEXT NOT NULL,
  key TEXT NOT NULL,
  value TEXT NOT NULL,
  updated REAL NOT NULL,
  PRIMARY KEY (user, key)
);
CREATE TABLE IF NOT EXISTS support_requests (
  id TEXT PRIMARY KEY,
  user TEXT NOT NULL,
  category TEXT NOT NULL,
  summary TEXT NOT NULL,
  details TEXT NOT NULL,
  viewer_version TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL,
  created REAL NOT NULL
);
"""

ACTION_TTL = 24 * 3600
REQUEST_TTL = 24 * 3600
STALE_PENDING = 15 * 60  # a request still pending after this crashed; let it run again


def new_id(prefix: str) -> str:
    return f"{prefix}_{secrets.token_urlsafe(12)}"


class Store:
    def __init__(self, path: str):
        self._db = sqlite3.connect(path, check_same_thread=False, isolation_level=None)
        self._db.row_factory = sqlite3.Row
        self._lock = threading.RLock()
        with self._lock:
            self._db.executescript(SCHEMA)

    def close(self) -> None:
        with self._lock:
            self._db.close()

    def _q(self, sql: str, args: tuple = ()) -> list[sqlite3.Row]:
        with self._lock:
            return list(self._db.execute(sql, args))

    # ---- conversations -------------------------------------------------
    def create_conversation(self, user: str, title: str) -> str:
        cid = new_id("conv")
        now = time.time()
        self._q("INSERT INTO conversations(id,user,created,updated,title) VALUES(?,?,?,?,?)",
                (cid, user, now, now, title[:80]))
        return cid

    def owns_conversation(self, user: str, cid: str) -> bool:
        return bool(self._q("SELECT 1 FROM conversations WHERE id=? AND user=?", (cid, user)))

    def list_conversations(self, user: str) -> list[dict]:
        rows = self._q("SELECT id,title,created,updated FROM conversations WHERE user=? "
                       "ORDER BY updated DESC LIMIT 50", (user,))
        return [dict(r) for r in rows]

    def api_messages(self, cid: str) -> list[dict]:
        rows = self._q("SELECT api_json FROM turns WHERE conv=? ORDER BY id", (cid,))
        return [json.loads(r["api_json"]) for r in rows]

    def user_turn_count(self, cid: str) -> int:
        return self._q("SELECT COUNT(*) AS n FROM turns WHERE conv=? AND role='user' "
                       "AND display IS NOT NULL", (cid,))[0]["n"]

    def history(self, user: str, cid: str) -> list[dict] | None:
        if not self.owns_conversation(user, cid):
            return None
        rows = self._q("SELECT id,role,display,created FROM turns WHERE conv=? AND display IS NOT NULL "
                       "ORDER BY id", (cid,))
        return [{"id": r["id"], "role": r["role"], "text": r["display"], "created": r["created"]}
                for r in rows]

    def append_turns(self, cid: str, turns: list[tuple[str, str | None, dict]]) -> None:
        """Appends (role, display text or None, API message) in one transaction.
        History is append-only: earlier messages are never rewritten."""
        now = time.time()
        with self._lock:
            self._db.execute("BEGIN")
            try:
                for role, display, msg in turns:
                    self._db.execute("INSERT INTO turns(conv,role,display,api_json,created) VALUES(?,?,?,?,?)",
                                     (cid, role, display, json.dumps(msg, separators=(",", ":")), now))
                self._db.execute("UPDATE conversations SET updated=? WHERE id=?", (now, cid))
                self._db.execute("COMMIT")
            except Exception:
                self._db.execute("ROLLBACK")
                raise

    def delete_conversation(self, user: str, cid: str) -> bool:
        with self._lock:
            cur = self._db.execute("DELETE FROM conversations WHERE id=? AND user=?", (cid, user))
            self._db.execute("DELETE FROM pending_actions WHERE conv=? AND user=? AND status='pending'",
                             (cid, user))
            return cur.rowcount > 0

    def delete_all_conversations(self, user: str) -> int:
        with self._lock:
            cur = self._db.execute("DELETE FROM conversations WHERE user=?", (user,))
            self._db.execute("DELETE FROM pending_actions WHERE user=? AND status='pending'", (user,))
            self._db.execute("DELETE FROM requests WHERE user=?", (user,))
            return cur.rowcount

    # ---- idempotent requests -------------------------------------------
    def begin_request(self, user: str, key: str) -> tuple[str, dict | None]:
        """('new', None) when this key is new or its last attempt failed;
        ('done', response) to replay; ('pending', None) while it runs."""
        with self._lock:
            rows = list(self._db.execute("SELECT status,response_json,created FROM requests "
                                         "WHERE user=? AND idem_key=?", (user, key)))
            if rows:
                status = rows[0]["status"]
                if status == "done":
                    return "done", json.loads(rows[0]["response_json"])
                if status == "pending" and time.time() - rows[0]["created"] < STALE_PENDING:
                    return "pending", None
                self._db.execute("UPDATE requests SET status='pending', created=? WHERE user=? AND idem_key=?",
                                 (time.time(), user, key))
                return "new", None
            self._db.execute("INSERT INTO requests(user,idem_key,status,created) VALUES(?,?,'pending',?)",
                             (user, key, time.time()))
            return "new", None

    def finish_request(self, user: str, key: str, response: dict | None) -> None:
        if response is None:
            self._q("UPDATE requests SET status='failed' WHERE user=? AND idem_key=?", (user, key))
        else:
            self._q("UPDATE requests SET status='done', response_json=? WHERE user=? AND idem_key=?",
                    (json.dumps(response), user, key))

    # ---- pending actions ------------------------------------------------
    def add_action(self, user: str, conv: str | None, kind: str, payload: dict) -> str:
        aid = new_id("act")
        self._q("INSERT INTO pending_actions(id,user,conv,kind,payload_json,status,created) "
                "VALUES(?,?,?,?,?,'pending',?)", (aid, user, conv, kind, json.dumps(payload), time.time()))
        return aid

    def get_action(self, user: str, aid: str) -> dict | None:
        rows = self._q("SELECT * FROM pending_actions WHERE id=? AND user=?", (aid, user))
        if not rows:
            return None
        r = dict(rows[0])
        r["payload"] = json.loads(r.pop("payload_json"))
        r["result"] = json.loads(r.pop("result_json")) if r["result_json"] else None
        if r["status"] == "pending" and time.time() - r["created"] > ACTION_TTL:
            r["status"] = "expired"
        return r

    def resolve_action(self, user: str, aid: str, status: str, result: dict | None) -> bool:
        """Moves a pending action to its end state once; False if it was not pending."""
        with self._lock:
            cur = self._db.execute("UPDATE pending_actions SET status=?, result_json=?, resolved=? "
                                   "WHERE id=? AND user=? AND status='pending' AND created>?",
                                   (status, json.dumps(result) if result is not None else None, time.time(),
                                    aid, user, time.time() - ACTION_TTL))
            return cur.rowcount == 1

    def set_action_result(self, aid: str, result: dict) -> None:
        self._q("UPDATE pending_actions SET result_json=? WHERE id=?", (json.dumps(result), aid))

    def cancel_actions(self, ids: list[str]) -> None:
        for aid in ids:
            self._q("UPDATE pending_actions SET status='cancelled', resolved=? WHERE id=? AND status='pending'",
                    (time.time(), aid))

    # ---- preferences ----------------------------------------------------
    def preferences(self, user: str) -> dict[str, str]:
        return {r["key"]: r["value"] for r in
                self._q("SELECT key,value FROM preferences WHERE user=? ORDER BY key", (user,))}

    def set_preference(self, user: str, key: str, value: str) -> None:
        self._q("INSERT INTO preferences(user,key,value,updated) VALUES(?,?,?,?) "
                "ON CONFLICT(user,key) DO UPDATE SET value=excluded.value, updated=excluded.updated",
                (user, key, value, time.time()))

    def delete_preference(self, user: str, key: str) -> bool:
        with self._lock:
            return self._db.execute("DELETE FROM preferences WHERE user=? AND key=?", (user, key)).rowcount > 0

    def count_preferences(self, user: str) -> int:
        return self._q("SELECT COUNT(*) AS n FROM preferences WHERE user=?", (user,))[0]["n"]

    # ---- support requests ----------------------------------------------
    def add_support_request(self, user: str, payload: dict[str, Any]) -> str:
        sid = new_id("sup")
        self._q("INSERT INTO support_requests(id,user,category,summary,details,viewer_version,status,created) "
                "VALUES(?,?,?,?,?,?,'new',?)",
                (sid, user, payload["category"], payload["summary"], payload["details"],
                 payload.get("viewer_version", ""), time.time()))
        return sid

    def support_requests(self, limit: int = 100) -> list[dict]:
        return [dict(r) for r in self._q("SELECT id,user,category,summary,details,viewer_version,status,created "
                                         "FROM support_requests ORDER BY created DESC LIMIT ?", (limit,))]

    # ---- retention ------------------------------------------------------
    def purge(self, retention_days: int) -> int:
        now = time.time()
        with self._lock:
            n = self._db.execute("DELETE FROM conversations WHERE updated<?",
                                 (now - retention_days * 86400,)).rowcount
            self._db.execute("DELETE FROM requests WHERE created<?", (now - REQUEST_TTL,))
            self._db.execute("UPDATE pending_actions SET status='expired' WHERE status='pending' AND created<?",
                             (now - ACTION_TTL,))
            self._db.execute("DELETE FROM pending_actions WHERE created<?", (now - retention_days * 86400,))
            return n

"""NEED Chat Translate v1 - relay state in one SQLite file (survives restarts).

Tables
  state     key/value: paused, clock offset (tests only), console block, alert marks
  pilot     the 168 h window (UTC), stop reason, final report path
  ledger    one row per relay decision: status, tokens, cost, latency, hash - NEVER the text
  cache     translation cache (normalised source hash -> translated line), TTL
  stats     daily (Asia/Seoul) counters: map-server STATS frames + relay counters

The chat text itself is only kept in `cache` (translated form) and never in ledger/stats/reports.
"""
import json
import os
import sqlite3
import time
from datetime import datetime, timedelta, timezone

KST = timezone(timedelta(hours=9))	# Korea has no DST; tzdata is not needed

SCHEMA = """
CREATE TABLE IF NOT EXISTS state (k TEXT PRIMARY KEY, v TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS pilot (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  started_utc TEXT, ends_utc TEXT, stopped_utc TEXT, stop_reason TEXT, final_report TEXT);
CREATE TABLE IF NOT EXISTS ledger (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts_utc TEXT NOT NULL, day_kst TEXT NOT NULL, chan INTEGER, src TEXT, tgt TEXT,
  status TEXT NOT NULL, in_tok INTEGER DEFAULT 0, out_tok INTEGER DEFAULT 0,
  cw_tok INTEGER DEFAULT 0, cr_tok INTEGER DEFAULT 0, cost_micro INTEGER DEFAULT 0,
  latency_ms INTEGER DEFAULT 0, text_hash TEXT, request_id TEXT);
CREATE INDEX IF NOT EXISTS ledger_day ON ledger(day_kst);
CREATE TABLE IF NOT EXISTS cache (
  key TEXT PRIMARY KEY, tgt TEXT NOT NULL, out TEXT NOT NULL, created REAL NOT NULL, hits INTEGER DEFAULT 0);
CREATE TABLE IF NOT EXISTS stats (day_kst TEXT NOT NULL, k TEXT NOT NULL, v INTEGER NOT NULL, PRIMARY KEY (day_kst, k));
"""


class Clock:
    """UTC clock with a test-only offset kept in the state table (pilot end tests)."""

    def __init__(self, store=None):
        self.store = store

    def now(self) -> datetime:
        off = 0.0
        if self.store is not None:
            off = float(self.store.get("clock_offset_s", "0") or 0)
        return datetime.now(timezone.utc) + timedelta(seconds=off)


def iso(dt: datetime) -> str:
    return dt.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_iso(s):
    if not s:
        return None
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)


def day_kst(dt: datetime) -> str:
    return dt.astimezone(KST).strftime("%Y-%m-%d")


def month_kst(dt: datetime) -> str:
    return dt.astimezone(KST).strftime("%Y-%m")


class Store:
    def __init__(self, path: str):
        d = os.path.dirname(os.path.abspath(path))
        os.makedirs(d, exist_ok=True)
        old = os.umask(0o077) if hasattr(os, "umask") else None
        try:
            self.db = sqlite3.connect(path, isolation_level=None, timeout=5)
        finally:
            if old is not None:
                os.umask(old)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=NORMAL")
        self.db.executescript(SCHEMA)
        self.db.execute("INSERT OR IGNORE INTO pilot (id) VALUES (1)")

    # ------------------------------------------------------------ state
    def get(self, k, default=None):
        r = self.db.execute("SELECT v FROM state WHERE k=?", (k,)).fetchone()
        return r[0] if r else default

    def set(self, k, v):
        self.db.execute("INSERT INTO state (k, v) VALUES (?, ?) ON CONFLICT(k) DO UPDATE SET v=excluded.v", (k, str(v)))

    def delete(self, k):
        self.db.execute("DELETE FROM state WHERE k=?", (k,))

    # ------------------------------------------------------------ pilot
    def pilot(self):
        r = self.db.execute("SELECT started_utc, ends_utc, stopped_utc, stop_reason, final_report FROM pilot WHERE id=1").fetchone()
        return {"started": parse_iso(r[0]), "ends": parse_iso(r[1]), "stopped": parse_iso(r[2]), "stop_reason": r[3], "final_report": r[4]}

    def pilot_start(self, now: datetime, hours: float):
        self.db.execute("UPDATE pilot SET started_utc=?, ends_utc=?, stopped_utc=NULL, stop_reason=NULL, final_report=NULL WHERE id=1",
                        (iso(now), iso(now + timedelta(hours=hours))))
        for k in [r[0] for r in self.db.execute("SELECT k FROM state WHERE k LIKE 'alert:%'")]:
            self.delete(k)

    def pilot_stop(self, now: datetime, reason: str):
        self.db.execute("UPDATE pilot SET stopped_utc=?, stop_reason=? WHERE id=1 AND stopped_utc IS NULL", (iso(now), reason))

    def pilot_set_report(self, path: str):
        self.db.execute("UPDATE pilot SET final_report=? WHERE id=1", (path,))

    # ------------------------------------------------------------ ledger / money
    def ledger_add(self, now, chan, src, tgt, status, usage=None, cost_micro=0, latency_ms=0, text_hash=None, request_id=None):
        u = usage or {}
        self.db.execute(
            "INSERT INTO ledger (ts_utc, day_kst, chan, src, tgt, status, in_tok, out_tok, cw_tok, cr_tok, cost_micro, latency_ms, text_hash, request_id)"
            " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (iso(now), day_kst(now), chan, src, tgt, status, int(u.get("input_tokens", 0)), int(u.get("output_tokens", 0)),
             int(u.get("cache_creation_input_tokens", 0)), int(u.get("cache_read_input_tokens", 0)), int(cost_micro), int(latency_ms),
             (text_hash or "")[:16], request_id))

    def spent_micro(self, since_utc: datetime = None, day: str = None, month: str = None) -> int:
        if day is not None:
            r = self.db.execute("SELECT COALESCE(SUM(cost_micro),0) FROM ledger WHERE day_kst=?", (day,)).fetchone()
        elif month is not None:
            r = self.db.execute("SELECT COALESCE(SUM(cost_micro),0) FROM ledger WHERE substr(day_kst,1,7)=?", (month,)).fetchone()
        elif since_utc is not None:
            r = self.db.execute("SELECT COALESCE(SUM(cost_micro),0) FROM ledger WHERE ts_utc>=?", (iso(since_utc),)).fetchone()
        else:
            r = self.db.execute("SELECT COALESCE(SUM(cost_micro),0) FROM ledger").fetchone()
        return int(r[0])

    # ------------------------------------------------------------ cache
    def cache_get(self, key, ttl_days):
        r = self.db.execute("SELECT out, created FROM cache WHERE key=?", (key,)).fetchone()
        if not r:
            return None
        if time.time() - r[1] > ttl_days * 86400:
            self.db.execute("DELETE FROM cache WHERE key=?", (key,))
            return None
        self.db.execute("UPDATE cache SET hits=hits+1 WHERE key=?", (key,))
        return r[0]

    def cache_put(self, key, tgt, out):
        self.db.execute("INSERT INTO cache (key, tgt, out, created) VALUES (?,?,?,?) ON CONFLICT(key) DO UPDATE SET out=excluded.out, created=excluded.created",
                        (key, tgt, out, time.time()))

    def cache_purge(self, ttl_days):
        self.db.execute("DELETE FROM cache WHERE created < ?", (time.time() - ttl_days * 86400,))

    # ------------------------------------------------------------ stats
    def stat_add(self, day, values: dict):
        for k, v in values.items():
            if not v:
                continue
            self.db.execute("INSERT INTO stats (day_kst, k, v) VALUES (?,?,?) ON CONFLICT(day_kst, k) DO UPDATE SET v=v+excluded.v", (day, k, int(v)))

    def stat_max(self, day, k, v):
        self.db.execute("INSERT INTO stats (day_kst, k, v) VALUES (?,?,?) ON CONFLICT(day_kst, k) DO UPDATE SET v=MAX(v, excluded.v)", (day, k, int(v)))

    def stats_by_day(self, first_day=None, last_day=None):
        q = "SELECT day_kst, k, v FROM stats"
        args = []
        if first_day:
            q += " WHERE day_kst BETWEEN ? AND ?"
            args = [first_day, last_day]
        out = {}
        for d, k, v in self.db.execute(q, args):
            out.setdefault(d, {})[k] = v
        return out

    def ledger_rows(self, first_day, last_day):
        return self.db.execute(
            "SELECT day_kst, status, in_tok, out_tok, cw_tok, cr_tok, cost_micro, latency_ms FROM ledger WHERE day_kst BETWEEN ? AND ?",
            (first_day, last_day)).fetchall()

    def alert_once(self, key) -> bool:
        k = "alert:" + key
        if self.get(k):
            return False
        self.set(k, "1")
        return True

    def dump_json(self):
        return json.dumps({"pilot": {k: (iso(v) if isinstance(v, datetime) else v) for k, v in self.pilot().items()}})

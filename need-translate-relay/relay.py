#!/usr/bin/env python3
"""NEED Chat Translate v1 - need-translate-relay

The only process that calls the Claude API. It connects to the map-server's 127.0.0.1 listener
(need_chat_translate_port), receives translation requests, answers them, and keeps the budget,
the 168 h pilot window, the cache and the usage ledger in one SQLite file.

  python3 relay.py --config relay.conf run              # the service (systemd ExecStart)
  python3 relay.py --config relay.conf status
  python3 relay.py --config relay.conf pilot start      # 168 h from now (operator)
  python3 relay.py --config relay.conf pilot stop
  python3 relay.py --config relay.conf pause | resume   # one-command stop / restart of translating
  python3 relay.py --config relay.conf report           # interim report (Markdown, no chat text)
  python3 relay.py --config relay.conf test-clock --hours 167.9   # mock backend only

Secrets come from the environment only: ANTHROPIC_API_KEY, NEED_CHAT_TRANSLATE_TOKEN.
Failure policy: anything unexpected => no translation line; the game chat never waits for us.
"""
import argparse
import asyncio
import configparser
import json
import logging
import os
import signal
import struct
import sys
import time
from datetime import datetime, timedelta, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import nct_backend  # noqa: E402
import nct_report  # noqa: E402
import nct_store  # noqa: E402
import nct_text  # noqa: E402

PROTO = 1
CMD_HELLO, CMD_HELLO_ACK = 0x7B00, 0x7B01
CMD_REQ, CMD_RES = 0x7B10, 0x7B11
CMD_STATUS, CMD_ALERT, CMD_STATS = 0x7B20, 0x7B21, 0x7B30

ST_OK, ST_CACHED, ST_SKIP, ST_QUOTA, ST_BUDGET, ST_DISABLED, ST_ERROR, ST_TIMEOUT, ST_REFUSED = range(9)
ST_NAMES = ["ok", "cached", "skip", "quota", "budget", "disabled", "error", "timeout", "refused"]

# STATUS reason codes (shown by @translateadm)
R_OK, R_PAUSED, R_NO_PILOT, R_PILOT_ENDED, R_DAILY, R_TOTAL, R_MONTH, R_CONSOLE, R_AUTH, R_BACKOFF, R_NO_KEY = range(11)
R_NAMES = ["ok", "paused", "pilot not started", "pilot ended", "daily cap", "pilot cap", "month cap",
           "console spend limit", "API auth error", "API backoff", "no API key"]

MAP_STATS = ["msg_say", "msg_chatroom", "msg_channel", "msg_party", "msg_guild", "msg_bg", "msg_clan",
             "eligible", "skip_no_recipient", "skip_sender_block", "skip_sender_quota", "skip_no_lang",
             "skip_command", "skip_inflight", "skip_service_off", "requests", "res_ok", "res_other",
             "expired", "lines", "delivery_empty", "toggle_on", "toggle_off", "block_on", "block_off",
             "hook_us_total", "hook_us_max", "hook_calls", "recipients_kr", "recipients_en",
             "subscribers_kr", "subscribers_en", "online", "rtt_ms_total", "rtt_count"]
MAP_STATS_MAX_KEYS = {"hook_us_max", "recipients_kr", "recipients_en", "subscribers_kr", "subscribers_en", "online"}

LANG = {0: "ko", 1: "en"}
LANG_ID = {"ko": 0, "en": 1}

log = logging.getLogger("need-translate-relay")


# ---------------------------------------------------------------- configuration

DEFAULTS = {
    "relay": {"map_host": "127.0.0.1", "map_port": "6906", "db_path": "/var/lib/need-translate-relay/relay.sqlite",
              "report_dir": "/var/lib/need-translate-relay/reports", "backend": "mock", "log_level": "INFO",
              "allow_test_clock": "0"},
    "claude": {"model": "claude-haiku-5-5", "max_tokens": "200", "effort": "low", "api_timeout_s": "3.5",
               "max_concurrency": "8", "price_input": "0.10", "price_output": "0.50",
               "price_cache_write": "0.125", "price_cache_read": "0.01"},
    "budget": {"pilot_total_usd": "20", "daily_usd": "3", "month_usd": "80", "alert_pcts": "50,80,100",
               "reserve_input_tokens": "1200"},
    "limits": {"speaker_per_min": "6", "speaker_per_hour": "60", "global_per_min": "120", "max_queue": "64",
               "max_out_bytes": "180", "cache_days": "30", "spike_per_min": "90", "error_alert_streak": "10"},
    "pilot": {"hours": "168"},
    "mock": {"delay_ms": "150"},
}


def load_config(path):
    cp = configparser.ConfigParser()
    cp.read_dict(DEFAULTS)
    if path and os.path.exists(path):
        cp.read(path, encoding="utf-8")
    return cp


# ---------------------------------------------------------------- the relay

class Relay:
    def __init__(self, cfg, store=None):
        self.cfg = cfg
        r = cfg["relay"]
        self.host = r.get("map_host")
        self.port = int(os.environ.get("NEED_CHAT_TRANSLATE_PORT") or r.getint("map_port"))
        self.store = store or nct_store.Store(r.get("db_path"))
        self.clock = nct_store.Clock(self.store)
        self.report_dir = r.get("report_dir")
        c = cfg["claude"]
        self.prices = {"input": c.getfloat("price_input"), "output": c.getfloat("price_output"),
                       "cache_write": c.getfloat("price_cache_write"), "cache_read": c.getfloat("price_cache_read")}
        self.max_tokens = c.getint("max_tokens")
        self.api_timeout = c.getfloat("api_timeout_s")
        self.sem = asyncio.Semaphore(c.getint("max_concurrency"))
        b = cfg["budget"]
        self.cap_pilot = int(b.getfloat("pilot_total_usd") * 1e6)
        self.cap_day = int(b.getfloat("daily_usd") * 1e6)
        self.cap_month = int(b.getfloat("month_usd") * 1e6)
        self.alert_pcts = [int(x) for x in b.get("alert_pcts").split(",") if x.strip()]
        self.reserve_in = b.getint("reserve_input_tokens")
        lim = cfg["limits"]
        self.lim = {k: lim.getint(k) for k in lim}
        self.pilot_hours = cfg["pilot"].getfloat("hours")
        self.backend_name = r.get("backend")
        self.backend = None
        self.backend_error = None
        self.reserved = 0			# micro-USD reserved by calls in progress
        self.queue = 0
        self.inflight = {}			# cache key -> Future (coalesce identical lines)
        self.speaker = {}			# cid -> [minute_start, minute_n, hour_start, hour_n]
        self.global_win = [0.0, 0]
        self.backoff_until = 0.0
        self.error_streak = 0
        self.writer = None
        self.wlock = asyncio.Lock()
        self.counters = {}
        self.last_reason = None
        self.stopping = False

    # ------------------------------------------------------------ backend
    def make_backend(self):
        if self.backend_name == "mock":
            self.backend = nct_backend.MockBackend(self.cfg["mock"].getint("delay_ms"))
            return
        key = os.environ.get("ANTHROPIC_API_KEY", "")
        if not key:
            self.backend_error = R_NO_KEY
            log.error("backend=claude but ANTHROPIC_API_KEY is not set: translation stays off")
            return
        c = self.cfg["claude"]
        self.backend = nct_backend.ClaudeBackend(c.get("model"), self.max_tokens, c.get("effort"), key)

    # ------------------------------------------------------------ gate
    def reason(self):
        """Why translation is off right now (R_OK = translating)."""
        now = self.clock.now()
        if self.backend_error is not None:
            return self.backend_error
        if self.store.get("paused") == "1":
            return R_PAUSED
        p = self.store.pilot()
        if p["started"] is None:
            return R_NO_PILOT
        if p["stopped"] is not None or now >= p["ends"]:
            return R_PILOT_ENDED
        if self.store.get("auth_block") == "1":
            return R_AUTH
        cb = self.store.get("console_block_month")
        if cb and cb == nct_store.month_kst(now):
            return R_CONSOLE
        if self.store.spent_micro(since_utc=p["started"]) >= self.cap_pilot:
            return R_TOTAL
        if self.store.spent_micro(day=nct_store.day_kst(now)) >= self.cap_day:
            return R_DAILY
        if self.store.spent_micro(month=nct_store.month_kst(now)) >= self.cap_month:
            return R_MONTH
        if time.monotonic() < self.backoff_until:
            return R_BACKOFF
        return R_OK

    def budget_reserve(self, est):
        """Reserve the worst case of one call before making it (spend never passes a cap)."""
        now = self.clock.now()
        p = self.store.pilot()
        if self.store.spent_micro(since_utc=p["started"]) + self.reserved + est > self.cap_pilot:
            return False
        if self.store.spent_micro(day=nct_store.day_kst(now)) + self.reserved + est > self.cap_day:
            return False
        if self.store.spent_micro(month=nct_store.month_kst(now)) + self.reserved + est > self.cap_month:
            return False
        self.reserved += est
        return True

    def speaker_take(self, cid, now):
        w = self.speaker.setdefault(cid, [now, 0, now, 0])
        if now - w[0] >= 60:
            w[0], w[1] = now, 0
        if now - w[2] >= 3600:
            w[2], w[3] = now, 0
        if w[1] >= self.lim["speaker_per_min"] or w[3] >= self.lim["speaker_per_hour"]:
            return False
        w[1] += 1
        w[3] += 1
        return True

    def global_take(self, now):
        if now - self.global_win[0] >= 60:
            if self.global_win[1] >= self.lim["spike_per_min"]:
                self.alert(2, "request spike: %d translation requests in one minute" % self.global_win[1], "spike:%d" % int(now // 3600))
            self.global_win = [now, 0]
        if self.global_win[1] >= self.lim["global_per_min"]:
            return False
        self.global_win[1] += 1
        return True

    def heartbeat(self, connected, r=None):
        """읽기 전용 관리 콘솔용 생존 표시: state 키 'hb' (JSON, 같은 키를 덮어씀).

        번역 가능 여부 사유·백엔드 이름·대기열 길이만 기록한다 - 채팅 원문, ID, 금액 없음.
        시각은 실제 시계(테스트 시계 오프셋은 파일럿 기간용이지 생존 판정용이 아님).
        map-server 링크가 살아 있으면 5초마다, 끊겨 있으면 재접속 시도 앞뒤로 기록한다.
        예외를 밖으로 내지 않는다: 표시 실패가 번역에 영향을 주면 안 된다."""
        try:
            if r is None:
                r = self.reason()
            self.store.set("hb", json.dumps({
                "v": 1, "ts": nct_store.iso(datetime.now(timezone.utc)), "connected": 1 if connected else 0,
                "code": r, "reason": R_NAMES[r], "backend": self.backend_name, "queue": self.queue},
                separators=(",", ":")))
        except Exception:
            log.debug("heartbeat mark failed", exc_info=True)

    def count(self, k, n=1):
        self.counters[k] = self.counters.get(k, 0) + n

    def flush_counters(self):
        if self.counters:
            self.store.stat_add(nct_store.day_kst(self.clock.now()), self.counters)
            self.counters = {}

    # ------------------------------------------------------------ one request
    async def handle_req(self, req_id, chan, src_id, tgt_id, cid, deadline_ms, raw):
        t0 = time.monotonic()
        deadline = t0 + deadline_ms / 1000.0 - 0.3	# answer before the map-server gives up
        src, tgt = LANG.get(src_id), LANG.get(tgt_id)
        status, out = ST_ERROR, b""
        try:
            status, out = await self.process(chan, src, tgt, cid, raw, deadline)
        except Exception:
            log.exception("request %d failed", req_id)
            status, out = ST_ERROR, b""
        self.count("relay_" + ST_NAMES[status])
        await self.send_res(req_id, status, out)

    async def process(self, chan, src, tgt, cid, raw, deadline):
        now_dt = self.clock.now()
        if src is None or tgt is None or src == tgt:
            return ST_SKIP, b""
        if self.reason() != R_OK:
            return ST_DISABLED, b""
        text = nct_text.normalize(nct_text.decode(raw))
        kind, value = nct_text.prefilter(text, src, tgt)
        if kind == "skip":
            self.store.ledger_add(now_dt, chan, src, tgt, "skip")
            return ST_SKIP, b""
        if kind == "phrase":
            self.store.ledger_add(now_dt, chan, src, tgt, "phrase")
            return ST_CACHED, nct_text.encode_cut(value, self.lim["max_out_bytes"])
        mono = time.monotonic()
        if not self.speaker_take(cid, mono):
            self.store.ledger_add(now_dt, chan, src, tgt, "quota_speaker")
            return ST_QUOTA, b""
        masked, originals = nct_text.mask(text)
        key = nct_text.text_hash(masked, src, tgt)
        cached = self.store.cache_get(key, self.lim["cache_days"])
        if cached is not None:
            self.store.ledger_add(now_dt, chan, src, tgt, "cache", text_hash=key)
            restored = nct_text.unmask(cached, originals)
            if restored is not None:
                return ST_CACHED, nct_text.encode_cut(restored, self.lim["max_out_bytes"])
        fut = self.inflight.get(key)
        if fut is not None:		# the same line is already being translated: share it
            try:
                res = await asyncio.wait_for(asyncio.shield(fut), max(0.05, deadline - time.monotonic()))
            except (asyncio.TimeoutError, Exception):
                return ST_TIMEOUT, b""
            self.store.ledger_add(now_dt, chan, src, tgt, "coalesced", text_hash=key)
            if res is None:
                return ST_ERROR, b""
            restored = nct_text.unmask(res, originals)
            return (ST_CACHED, nct_text.encode_cut(restored, self.lim["max_out_bytes"])) if restored is not None else (ST_ERROR, b"")
        if not self.global_take(mono):
            self.store.ledger_add(now_dt, chan, src, tgt, "quota_global")
            return ST_QUOTA, b""
        if self.queue >= self.lim["max_queue"]:
            self.store.ledger_add(now_dt, chan, src, tgt, "queue_full")
            return ST_QUOTA, b""
        est = int(self.reserve_in * max(self.prices["input"], self.prices["cache_write"]) + self.max_tokens * self.prices["output"]) + 1
        if not self.budget_reserve(est):
            self.store.ledger_add(now_dt, chan, src, tgt, "budget_block")
            self.check_budget_alerts()
            return ST_BUDGET, b""

        loop = asyncio.get_running_loop()
        fut = loop.create_future()
        self.inflight[key] = fut
        self.queue += 1
        result_text = None
        try:
            try:
                await asyncio.wait_for(self.sem.acquire(), max(0.05, deadline - time.monotonic()))
            except asyncio.TimeoutError:
                self.store.ledger_add(now_dt, chan, src, tgt, "timeout_queue", text_hash=key)
                return ST_TIMEOUT, b""
            try:
                status, out_text = await self.call_backend(chan, src, tgt, masked, key, deadline, now_dt)
            finally:
                self.sem.release()
            if status != ST_OK:
                return status, b""
            restored = nct_text.unmask(out_text, originals)
            if restored is None or not nct_text.plausible(masked, out_text):
                self.store.ledger_add(self.clock.now(), chan, src, tgt, "invalid_output", text_hash=key)
                return ST_ERROR, b""
            result_text = out_text
            self.store.cache_put(key, tgt, out_text)
            return ST_OK, nct_text.encode_cut(restored, self.lim["max_out_bytes"])
        finally:
            self.queue -= 1
            self.reserved -= est
            self.inflight.pop(key, None)
            if not fut.done():
                fut.set_result(result_text)

    async def call_backend(self, chan, src, tgt, masked, key, deadline, now_dt):
        attempt = 0
        while True:
            remaining = deadline - time.monotonic()
            if remaining < 0.3:
                self.store.ledger_add(now_dt, chan, src, tgt, "timeout", text_hash=key)
                return ST_TIMEOUT, None
            t = time.monotonic()
            try:
                res = await asyncio.wait_for(self.backend.translate(masked, src, tgt, min(self.api_timeout, remaining)), remaining)
            except asyncio.TimeoutError:
                res = nct_backend.BackendResult("timeout")
            lat = int((time.monotonic() - t) * 1000)
            cost = nct_backend.cost_micro(res.usage, self.prices) if res.usage else 0
            self.store.ledger_add(self.clock.now(), chan, src, tgt, "api_" + res.status, res.usage, cost, lat, key, res.request_id)
            if res.status == "ok":
                self.error_streak = 0
                self.check_budget_alerts()
                cleaned = nct_text.clean_output(res.text)
                if cleaned is None:
                    return ST_SKIP, None
                return ST_OK, cleaned
            if res.status == "refused":
                return ST_REFUSED, None
            if res.status == "rate_limited":
                self.backoff_until = time.monotonic() + max(1.0, res.retry_after)
                self.alert(2, "API rate limited (%s), pausing %.0fs" % (res.detail, res.retry_after), "ratelimit:%d" % int(time.time() // 600))
                return ST_QUOTA, None
            if res.status == "spend_limit":
                self.store.set("console_block_month", nct_store.month_kst(self.clock.now()))
                self.alert(3, "Console spend limit / credits exhausted (%s): translation stopped until next month" % res.detail, "console:" + nct_store.month_kst(self.clock.now()))
                return ST_BUDGET, None
            if res.status == "auth":
                self.store.set("auth_block", "1")
                self.alert(3, "API authentication failed (%s): translation stopped, fix the key and run resume" % res.detail, "auth")
                return ST_DISABLED, None
            # error / timeout: one retry if there is time left
            self.error_streak += 1
            if self.error_streak >= self.lim["error_alert_streak"]:
                self.alert(2, "API errors: %d in a row (last: %s %s)" % (self.error_streak, res.status, res.detail), "errors:%d" % int(time.time() // 1800))
            attempt += 1
            if attempt > 1:
                return (ST_TIMEOUT if res.status == "timeout" else ST_ERROR), None

    # ------------------------------------------------------------ alerts / budget
    def alert(self, level, msg, once_key=None):
        if once_key is not None and not self.store.alert_once(once_key):
            return
        log.warning("ALERT %s", msg)
        self.count("alerts")
        try:
            asyncio.get_running_loop().create_task(self.send_frame(CMD_ALERT, struct.pack("<B3x", level) + msg.encode("ascii", "replace")[:400]))
        except RuntimeError:
            pass	# CLI context: the log line is enough

    def check_budget_alerts(self):
        now = self.clock.now()
        p = self.store.pilot()
        if p["started"] is None:
            return
        spent = self.store.spent_micro(since_utc=p["started"])
        for pct in self.alert_pcts:
            if spent >= self.cap_pilot * pct / 100:
                self.alert(2 if pct < 100 else 3, "pilot budget %d%% used ($%.2f of $%.2f)" % (pct, spent / 1e6, self.cap_pilot / 1e6), "pilot%d" % pct)
        day = nct_store.day_kst(now)
        dspent = self.store.spent_micro(day=day)
        for pct in (80, 100):
            if dspent >= self.cap_day * pct / 100:
                self.alert(2 if pct < 100 else 3, "daily budget %d%% used ($%.2f of $%.2f, %s KST)" % (pct, dspent / 1e6, self.cap_day / 1e6, day), "day%d:%s" % (pct, day))
        mspent = self.store.spent_micro(month=nct_store.month_kst(now))
        if mspent >= self.cap_month * 0.8:
            self.alert(3, "month budget %.0f%% used ($%.2f of $%.2f)" % (100 * mspent / self.cap_month, mspent / 1e6, self.cap_month / 1e6), "month80:" + nct_store.month_kst(now))

    # ------------------------------------------------------------ frames
    async def send_frame(self, cmd, body: bytes):
        w = self.writer
        if w is None:
            return
        data = struct.pack("<HH", cmd, 4 + len(body)) + body
        async with self.wlock:
            try:
                w.write(data)
                await w.drain()
            except (ConnectionError, OSError):
                pass

    async def send_res(self, req_id, status, out: bytes):
        await self.send_frame(CMD_RES, struct.pack("<IBBH", req_id, status, 0, 0) + out)

    def status_body(self):
        r = self.reason()
        now = self.clock.now()
        p = self.store.pilot()
        remaining = 0
        total = 0
        if p["started"] is not None:
            total = self.store.spent_micro(since_utc=p["started"])
            if p["stopped"] is None and now < p["ends"]:
                remaining = int((p["ends"] - now).total_seconds())
        day = self.store.spent_micro(day=nct_store.day_kst(now))
        msg = "%s | backend=%s | pilot %s | today $%.2f/%.2f | pilot $%.2f/%.2f | queue %d" % (
            R_NAMES[r], self.backend_name,
            ("%dh%02dm left" % (remaining // 3600, remaining % 3600 // 60)) if remaining else ("not started" if p["started"] is None else "ended"),
            day / 1e6, self.cap_day / 1e6, total / 1e6, self.cap_pilot / 1e6, self.queue)
        return r, struct.pack("<BBHIII", 1 if r == R_OK else 0, r, 0, remaining, min(day, 0xFFFFFFFF), min(total, 0xFFFFFFFF)) + msg.encode("ascii", "replace")[:300]

    async def status_loop(self):
        while not self.stopping:
            r, body = self.status_body()
            if r != self.last_reason:
                log.info("translation state: %s", R_NAMES[r])
                self.last_reason = r
            await self.send_frame(CMD_STATUS, body)
            self.flush_counters()
            self.check_budget_alerts()
            self.check_pilot_end()
            self.heartbeat(True)
            await asyncio.sleep(5)

    def check_pilot_end(self):
        p = self.store.pilot()
        if p["started"] is None or p["final_report"]:
            return
        now = self.clock.now()
        if p["stopped"] is None and now < p["ends"]:
            return
        if p["stopped"] is None:
            self.store.pilot_stop(p["ends"], "168h elapsed")
        self.flush_counters()
        path = nct_report.write_report(self.store, self.report_dir, self.cfg, final=True)
        self.store.pilot_set_report(path)
        self.alert(3, "pilot finished: automatic translation is now OFF. Report: %s" % os.path.basename(path), "pilot_end")
        log.info("pilot finished, report %s", path)

    def on_stats(self, body):
        if len(body) < 4:
            return
        version, n = struct.unpack_from("<HH", body, 0)
        vals = struct.unpack_from("<%dI" % n, body, 4) if len(body) >= 4 + 4 * n else ()
        day = nct_store.day_kst(self.clock.now())
        add = {}
        for i, v in enumerate(vals):
            if i >= len(MAP_STATS):
                break
            k = "map_" + MAP_STATS[i]
            if MAP_STATS[i] in MAP_STATS_MAX_KEYS:
                self.store.stat_max(day, k, v)
            else:
                add[k] = v
        self.store.stat_add(day, add)

    # ------------------------------------------------------------ session
    async def session(self):
        reader, writer = await asyncio.wait_for(asyncio.open_connection(self.host, self.port), 5)
        token = os.environ.get("NEED_CHAT_TRANSLATE_TOKEN", "").encode("ascii", "ignore")[:32].ljust(32, b"\0")
        writer.write(struct.pack("<HHHH", CMD_HELLO, 8 + 32, PROTO, 0) + token)
        await writer.drain()
        hdr = await asyncio.wait_for(reader.readexactly(4), 5)
        cmd, ln = struct.unpack("<HH", hdr)
        body = await reader.readexactly(ln - 4)
        if cmd != CMD_HELLO_ACK or struct.unpack_from("<HH", body, 0)[1] != 0:
            writer.close()
            raise RuntimeError("map-server rejected HELLO (token or protocol)")
        self.writer = writer
        log.info("connected to map-server %s:%d", self.host, self.port)
        status_task = asyncio.create_task(self.status_loop())
        try:
            while True:
                hdr = await reader.readexactly(4)
                cmd, ln = struct.unpack("<HH", hdr)
                if ln < 4 or ln > 8192:
                    raise RuntimeError("bad frame length %d" % ln)
                body = await reader.readexactly(ln - 4)
                if cmd == CMD_REQ:
                    req_id, chan, src, tgt, _flags, cid, deadline_ms = struct.unpack_from("<IBBBBII", body, 0)
                    asyncio.create_task(self.handle_req(req_id, chan, src, tgt, cid, deadline_ms, body[16:]))
                elif cmd == CMD_STATS:
                    self.on_stats(body)
                else:
                    log.warning("unknown frame 0x%04x ignored", cmd)
        finally:
            status_task.cancel()
            self.writer = None
            try:
                writer.close()
            except Exception:
                pass

    async def run(self):
        self.make_backend()
        if self.backend is not None and self.backend_name == "claude":
            log.info("backend: Claude %s (live API, costs money)", self.cfg["claude"].get("model"))
        else:
            log.info("backend: %s", self.backend_name)
        delay = 1
        while not self.stopping:
            self.heartbeat(False)		# 프로세스는 살아 있고 map-server 링크는 아직 없음
            try:
                await self.session()
                delay = 1
            except (ConnectionError, OSError, asyncio.IncompleteReadError, asyncio.TimeoutError, RuntimeError) as e:
                log.warning("map-server link: %s (retry in %ds)", e, delay)
            self.flush_counters()
            self.heartbeat(False)
            await asyncio.sleep(delay)
            delay = min(delay * 2, 30)


# ---------------------------------------------------------------- CLI

def kst(dt):
    return dt.astimezone(nct_store.KST).strftime("%Y-%m-%d %H:%M KST") if dt else "-"


def cmd_status(relay):
    p = relay.store.pilot()
    r, body = relay.status_body()
    print("state      :", R_NAMES[r])
    print("backend    :", relay.backend_name)
    print("pilot      : start %s / end %s / stopped %s (%s)" % (kst(p["started"]), kst(p["ends"]), kst(p["stopped"]), p["stop_reason"] or "-"))
    print("budget     : pilot $%.2f, day $%.2f, month $%.2f" % (relay.cap_pilot / 1e6, relay.cap_day / 1e6, relay.cap_month / 1e6))
    now = relay.clock.now()
    if p["started"]:
        print("spent      : pilot $%.4f, today $%.4f, month $%.4f" % (
            relay.store.spent_micro(since_utc=p["started"]) / 1e6, relay.store.spent_micro(day=nct_store.day_kst(now)) / 1e6,
            relay.store.spent_micro(month=nct_store.month_kst(now)) / 1e6))
    print("paused     :", relay.store.get("paused") == "1")
    print("final repo :", p["final_report"] or "-")


def main(argv=None):
    ap = argparse.ArgumentParser(description="NEED Chat Translate relay")
    ap.add_argument("--config", default=os.path.join(HERE, "relay.conf"))
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("run")
    sub.add_parser("status")
    pp = sub.add_parser("pilot")
    pp.add_argument("action", choices=["start", "stop"])
    pp.add_argument("--hours", type=float, default=None)
    pp.add_argument("--reason", default="operator stop")
    pp.add_argument("--yes", action="store_true", help="confirm a live (backend=claude) pilot start")
    sub.add_parser("pause")
    sub.add_parser("resume")
    rp = sub.add_parser("report")
    rp.add_argument("--final", action="store_true")
    tc = sub.add_parser("test-clock")
    tc.add_argument("--hours", type=float, required=True)
    a = ap.parse_args(argv)

    cfg = load_config(a.config)
    logging.basicConfig(level=getattr(logging, cfg["relay"].get("log_level").upper(), logging.INFO),
                        format="%(asctime)s %(levelname)s %(message)s")
    relay = Relay(cfg)

    if a.cmd == "run":
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        for sig in (getattr(signal, "SIGTERM", None), getattr(signal, "SIGINT", None)):
            if sig is not None:
                try:
                    loop.add_signal_handler(sig, lambda: setattr(relay, "stopping", True) or loop.stop())
                except (NotImplementedError, RuntimeError):
                    pass
        try:
            loop.run_until_complete(relay.run())
        except KeyboardInterrupt:
            pass
        finally:
            relay.flush_counters()
        return 0
    if a.cmd == "status":
        cmd_status(relay)
        return 0
    if a.cmd == "pilot":
        now = relay.clock.now()
        if a.action == "start":
            if relay.backend_name == "claude" and not a.yes:
                print("backend=claude: this starts a LIVE pilot that spends API credit. Re-run with --yes to confirm.")
                return 2
            hours = a.hours if a.hours is not None else relay.pilot_hours
            relay.store.pilot_start(now, hours)
            relay.store.delete("auth_block")
            print("pilot started %s, ends %s (%.1f h)" % (kst(now), kst(now + timedelta(hours=hours)), hours))
        else:
            relay.store.pilot_stop(now, a.reason)
            print("pilot stopped:", a.reason)
        return 0
    if a.cmd in ("pause", "resume"):
        relay.store.set("paused", "1" if a.cmd == "pause" else "0")
        if a.cmd == "resume":
            relay.store.delete("auth_block")
        print("translation", "paused" if a.cmd == "pause" else "resumed", "(the running relay applies it within 5 s)")
        return 0
    if a.cmd == "report":
        path = nct_report.write_report(relay.store, relay.report_dir, cfg, final=a.final)
        print(path)
        return 0
    if a.cmd == "test-clock":
        if cfg["relay"].get("backend") != "mock" or cfg["relay"].get("allow_test_clock") != "1":
            print("test-clock needs backend=mock and allow_test_clock=1")
            return 2
        off = float(relay.store.get("clock_offset_s", "0") or 0) + a.hours * 3600
        relay.store.set("clock_offset_s", off)
        print("test clock offset now %.1f h" % (off / 3600))
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())

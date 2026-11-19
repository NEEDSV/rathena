"""NEED Chat Translate v1 - relay unit tests. No network, no API, no cost (MockBackend only).

    python -I need-translate-relay/tests/test_relay.py -v
"""
import asyncio
import os
import shutil
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import nct_backend  # noqa: E402
import nct_store  # noqa: E402
import nct_text  # noqa: E402
import relay as R  # noqa: E402


def make_cfg(tmp, **over):
    cfg = R.load_config(None)
    cfg["relay"]["db_path"] = os.path.join(tmp, "relay.sqlite")
    cfg["relay"]["report_dir"] = os.path.join(tmp, "reports")
    cfg["relay"]["allow_test_clock"] = "1"
    cfg["mock"]["delay_ms"] = "20"
    for k, v in over.items():
        sec, key = k.split("__")
        cfg[sec][key] = str(v)
    return cfg


def cp(s):
    return s.encode("cp949")


class TextTests(unittest.TestCase):
    def test_detect(self):
        self.assertEqual(nct_text.detect("BG 같이 가요"), "ko")
        self.assertEqual(nct_text.detect("lfg bio"), "en")
        self.assertEqual(nct_text.detect("ㄱㄱ"), "ko")
        self.assertIsNone(nct_text.detect("123 !!"))

    def test_prefilter(self):
        self.assertEqual(nct_text.prefilter("ㅋㅋㅋㅋ", "ko", "en")[0], "skip")
        self.assertEqual(nct_text.prefilter("12345 !!", "ko", "en")[0], "skip")
        self.assertEqual(nct_text.prefilter("MVP", "en", "ko")[0], "skip")	# keep term
        self.assertEqual(nct_text.prefilter("@alootid", "en", "ko")[0], "skip")
        self.assertEqual(nct_text.prefilter("ㄱㄱ", "ko", "en"), ("phrase", "let's go"))
        self.assertEqual(nct_text.prefilter("ㅋㅋㅋ", "ko", "en")[0], "skip")
        self.assertEqual(nct_text.prefilter("gg", "en", "ko"), ("phrase", "수고하셨습니다"))
        self.assertEqual(nct_text.prefilter("hello there", "ko", "en")[0], "skip")	# language mismatch
        self.assertIsNone(nct_text.prefilter("BG 같이 가실 분 구해요", "ko", "en")[0])

    def test_mask_roundtrip(self):
        src = "wts <ITEML>0000y1nC+9&01(abc)</ITEML> +9 ^FF0000red^000000 id 1234567 see https://needro.net/x @alootid"
        masked, orig = nct_text.mask(src)
        self.assertNotIn("ITEML", masked)
        self.assertNotIn("https", masked)
        self.assertNotIn("^FF0000", masked)
        self.assertNotIn("1234567", masked)
        self.assertIn("⟦0⟧", masked)
        self.assertEqual(nct_text.unmask(masked, orig), src)
        # the model moved them around: still fine
        moved = " ".join(reversed(masked.split(" ")))
        self.assertIsNotNone(nct_text.unmask(moved, orig))

    def test_unmask_rejects_loss_and_dup(self):
        masked, orig = nct_text.mask("sell +9 sword 12345")
        self.assertEqual(len(orig), 2)
        self.assertIsNone(nct_text.unmask(masked.replace("⟦1⟧", ""), orig))
        self.assertIsNone(nct_text.unmask(masked + " ⟦0⟧", orig))
        self.assertIsNone(nct_text.unmask(masked + " ⟦7⟧", orig))

    def test_unmask_tidies_space_before_comma(self):
        # Gate C 2026-11-19: "item ⟦0⟧ , I reported it to ⟦1⟧" -> no gap before the comma
        _, orig = nct_text.mask("아이템 501번 버그 https://needro.net 에 제보")
        self.assertEqual(nct_text.unmask("item ⟦0⟧ , reported to ⟦1⟧ .", orig), "item 501, reported to https://needro.net .")
        # a restored span keeps its own spacing
        self.assertEqual(nct_text.unmask("a ⟦0⟧ b ⟦1⟧", ["x , y", "z"]), "a x , y b z")

    def test_prompt_glossary_korean_names(self):
        from nct_backend import SYSTEM_PROMPT
        self.assertIn("옛 글래스트헤임(상급) = Old Glast Heim (Hard)", SYSTEM_PROMPT)
        self.assertIn("생체 연구소", SYSTEM_PROMPT)
        self.assertNotIn("올드 글래스트 헤임", SYSTEM_PROMPT)
        self.assertIn("T_T", SYSTEM_PROMPT)

    def test_encode_cut(self):
        b = nct_text.encode_cut("가나다abc", 5)
        self.assertEqual(b, "가나".encode("cp949"))	# no half character, no reordering
        b = nct_text.encode_cut("가😀a", 10)
        self.assertEqual(b, "가a".encode("cp949"))	# emoji (not in CP949) dropped
        b = nct_text.encode_cut("가" * 200, 181)
        self.assertEqual(len(b), 180)
        b.decode("cp949")

    def test_clean_output(self):
        self.assertEqual(nct_text.clean_output('"[TL] hello\nworld"'), "hello world")
        self.assertEqual(nct_text.clean_output("[번역] [TL] 안녕"), "안녕")
        self.assertIsNone(nct_text.clean_output("<skip>"))
        self.assertIsNone(nct_text.clean_output("   "))
        self.assertEqual(nct_text.clean_output("a\x00b\x07c"), "abc")


class RelayTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="nct_test_")
        self.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self.loop)

    def tearDown(self):
        self.loop.close()
        try:
            self.relay.store.db.close()
        except Exception:
            pass
        shutil.rmtree(self.tmp, ignore_errors=True)

    def mk(self, **over):
        self.relay = R.Relay(make_cfg(self.tmp, **over))
        self.relay.make_backend()
        return self.relay

    def run_(self, coro):
        return self.loop.run_until_complete(coro)

    def proc(self, text, src="ko", tgt="en", cid=1, deadline_ms=4000):
        import time
        return self.run_(self.relay.process(0, src, tgt, cid, cp(text), time.monotonic() + deadline_ms / 1000.0 - 0.3))

    def start_pilot(self, hours=168):
        self.relay.store.pilot_start(self.relay.clock.now(), hours)

    def test_disabled_without_pilot(self):
        self.mk()
        self.assertEqual(self.proc("같이 사냥 가실 분")[0], R.ST_DISABLED)
        self.assertEqual(self.relay.backend.calls, 0)

    def test_translate_cache_and_ledger_has_no_text(self):
        r = self.mk()
        self.start_pilot()
        st, out = self.proc("같이 사냥 가실 분")
        self.assertEqual(st, R.ST_OK)
        self.assertTrue(out.decode("cp949").startswith("(EN) "))
        st2, out2 = self.proc("같이  사냥 가실 분")	# same after normalisation
        self.assertEqual(st2, R.ST_CACHED)
        self.assertEqual(out2, out)
        self.assertEqual(r.backend.calls, 1)
        dump = b"".join(str(row).encode() for row in r.store.db.execute("SELECT * FROM ledger"))
        self.assertNotIn("사냥".encode("utf-8"), dump)

    def test_en_to_ko_cp949(self):
        self.mk()
        self.start_pilot()
        st, out = self.proc("anyone for bio?", src="en", tgt="ko")
        self.assertEqual(st, R.ST_OK)
        self.assertTrue(out.decode("cp949").startswith("(한) "))

    def test_coalesce_identical_lines(self):
        r = self.mk(mock__delay_ms=200)
        self.start_pilot()
        import time

        async def both():
            d = time.monotonic() + 3.7
            return await asyncio.gather(r.process(0, "ko", "en", 1, cp("오늘 길드전 몇시"), d), r.process(0, "ko", "en", 2, cp("오늘 길드전 몇시"), d))
        res = self.run_(both())
        self.assertEqual(r.backend.calls, 1)
        self.assertEqual(sorted(x[0] for x in res), [R.ST_OK, R.ST_CACHED])

    def test_speaker_quota(self):
        self.mk(limits__speaker_per_min=2)
        self.start_pilot()
        self.assertEqual(self.proc("첫번째 문장입니다", cid=7)[0], R.ST_OK)
        self.assertEqual(self.proc("두번째 문장입니다", cid=7)[0], R.ST_OK)
        self.assertEqual(self.proc("세번째 문장입니다", cid=7)[0], R.ST_QUOTA)
        self.assertEqual(self.proc("다른 사람 문장입니다", cid=8)[0], R.ST_OK)

    def test_budget_reservation_blocks_before_call(self):
        r = self.mk(budget__daily_usd="0.00005")	# smaller than one worst-case reservation
        self.start_pilot()
        self.assertEqual(self.proc("예산 테스트 문장")[0], R.ST_BUDGET)
        self.assertEqual(r.backend.calls, 0)

    def test_pilot_cap_reached_disables(self):
        r = self.mk(budget__pilot_total_usd="0.001")
        self.start_pilot()
        now = r.clock.now()
        r.store.ledger_add(now, 0, "ko", "en", "api_ok", cost_micro=1000)
        self.assertEqual(r.reason(), R.R_TOTAL)
        self.assertEqual(self.proc("한도 넘은 뒤 문장")[0], R.ST_DISABLED)

    def test_faults(self):
        r = self.mk()
        self.start_pilot()
        self.assertEqual(self.proc("거절 테스트 MOCKREFUSE")[0], R.ST_REFUSED)
        self.assertEqual(self.proc("자리표시자 손실 +9 MOCKLOSE")[0], R.ST_ERROR)
        self.assertEqual(self.proc("장황한 출력 MOCKLONG")[0], R.ST_ERROR)
        self.assertEqual(self.proc("건너뛰기 MOCKSKIP")[0], R.ST_SKIP)
        self.assertEqual(self.proc("서버 오류 MOCKFAIL")[0], R.ST_ERROR)		# retried once, then error
        st = self.proc("느린 응답 MOCKSLOW", deadline_ms=1500)[0]
        self.assertEqual(st, R.ST_TIMEOUT)

    def test_rate_limit_backoff(self):
        r = self.mk()
        self.start_pilot()
        self.assertEqual(self.proc("제한 테스트 MOCKRATE")[0], R.ST_QUOTA)
        self.assertEqual(r.reason(), R.R_BACKOFF)
        self.assertEqual(self.proc("제한 중 다른 문장")[0], R.ST_DISABLED)

    def test_console_spend_limit_and_auth(self):
        r = self.mk()
        self.start_pilot()
        self.assertEqual(self.proc("크레딧 소진 MOCKSPEND")[0], R.ST_BUDGET)
        self.assertEqual(r.reason(), R.R_CONSOLE)
        r.store.delete("console_block_month")
        self.assertEqual(self.proc("인증 실패 MOCKAUTH")[0], R.ST_DISABLED)
        self.assertEqual(r.reason(), R.R_AUTH)

    def test_pilot_end_and_report(self):
        r = self.mk()
        self.start_pilot(hours=168)
        self.assertEqual(self.proc("파일럿 문장 하나")[0], R.ST_OK)
        r.store.set("clock_offset_s", 168 * 3600 + 60)
        self.assertEqual(r.reason(), R.R_PILOT_ENDED)

        async def end():
            r.check_pilot_end()
        self.run_(end())
        p = r.store.pilot()
        self.assertIsNotNone(p["final_report"])
        text = open(p["final_report"], encoding="utf-8").read()
        self.assertIn("최종", text)
        self.assertNotIn("파일럿 문장 하나", text)
        self.assertEqual(self.proc("종료 후 문장")[0], R.ST_DISABLED)

    def test_pilot_window_survives_restart(self):
        r = self.mk()
        self.start_pilot(hours=168)
        ends = r.store.pilot()["ends"]
        r.store.db.close()
        r2 = R.Relay(make_cfg(self.tmp))
        r2.make_backend()
        self.relay = r2
        self.assertEqual(r2.store.pilot()["ends"], ends)
        self.assertEqual(r2.reason(), R.R_OK)

    def test_heartbeat_mark(self):
        import json
        r = self.mk()
        r.heartbeat(True)
        hb = json.loads(r.store.get("hb"))
        self.assertEqual(set(hb), {"v", "ts", "connected", "code", "reason", "backend", "queue"})	# 원문·ID·금액 없음
        self.assertEqual((hb["connected"], hb["reason"], hb["backend"]), (1, "pilot not started", "mock"))
        self.start_pilot()
        r.heartbeat(False)
        hb = json.loads(r.store.get("hb"))
        self.assertEqual((hb["connected"], hb["code"]), (0, R.R_OK))
        # 표시 기록이 실패해도 번역은 계속된다
        r.store.set = lambda *a, **k: (_ for _ in ()).throw(RuntimeError("disk full"))
        r.heartbeat(True)
        self.assertEqual(self.proc("같이 사냥 가실 분")[0], R.ST_OK)

    def test_cost_formula(self):
        prices = {"input": 0.10, "output": 0.50, "cache_write": 0.125, "cache_read": 0.01}
        # 45 in, 15 out, 800 cache read -> 4.5 + 7.5 + 8 = 20 micro-USD (report 6.2 short chat)
        self.assertEqual(nct_backend.cost_micro({"input_tokens": 45, "output_tokens": 15, "cache_read_input_tokens": 800}, prices), 20)


class ProtocolTests(unittest.TestCase):
    """A fake map-server end of the 0x7Bxx protocol against Relay.session()."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="nct_proto_")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_hello_req_res_status(self):
        async def scenario():
            got = {"status": [], "res": []}
            done = asyncio.Event()

            async def fake_map(reader, writer):
                hdr = await reader.readexactly(4)
                cmd, ln = struct.unpack("<HH", hdr)
                body = await reader.readexactly(ln - 4)
                assert cmd == R.CMD_HELLO and struct.unpack_from("<H", body, 0)[0] == R.PROTO
                writer.write(struct.pack("<HHHHI", R.CMD_HELLO_ACK, 12, R.PROTO, 0, 0))
                text = cp("같이 사냥 가실 분")
                writer.write(struct.pack("<HHIBBBBII", R.CMD_REQ, 20 + len(text), 42, 0, 0, 1, 0, 1001, 4000) + text)
                stats = struct.pack("<HH", 1, 2) + struct.pack("<II", 5, 1)
                writer.write(struct.pack("<HH", R.CMD_STATS, 4 + len(stats)) + stats)
                await writer.drain()
                while not done.is_set():
                    hdr = await reader.readexactly(4)
                    cmd, ln = struct.unpack("<HH", hdr)
                    body = await reader.readexactly(ln - 4)
                    if cmd == R.CMD_STATUS:
                        got["status"].append(body[0])
                    elif cmd == R.CMD_RES:
                        got["res"].append((struct.unpack_from("<I", body, 0)[0], body[4], body[8:]))
                    if got["res"] and got["status"]:
                        done.set()
                writer.close()

            server = await asyncio.start_server(fake_map, "127.0.0.1", 0)
            port = server.sockets[0].getsockname()[1]
            cfg = make_cfg(self.tmp)
            cfg["relay"]["map_port"] = str(port)
            os.environ.pop("NEED_CHAT_TRANSLATE_PORT", None)
            relay = R.Relay(cfg)
            relay.make_backend()
            relay.store.pilot_start(relay.clock.now(), 168)
            task = asyncio.create_task(relay.session())
            await asyncio.wait_for(done.wait(), 10)
            await asyncio.sleep(0.05)		# 상태 루프 첫 회차가 끝나도록 잠깐 대기
            task.cancel()
            server.close()
            relay.flush_counters()
            day = nct_store.day_kst(relay.clock.now())
            st = relay.store.stats_by_day().get(day, {})
            st["_hb"] = relay.store.get("hb")
            relay.store.db.close()
            return got, st

        loop = asyncio.new_event_loop()
        try:
            got, st = loop.run_until_complete(scenario())
        finally:
            loop.close()
        self.assertEqual(got["status"][0], 1)		# translating
        req_id, status, text = got["res"][0]
        self.assertEqual((req_id, status), (42, R.ST_OK))
        self.assertTrue(text.decode("cp949").startswith("(EN) "))
        self.assertEqual(st.get("map_msg_say"), 5)
        self.assertEqual(st.get("map_msg_chatroom"), 1)
        import json
        hb = json.loads(st["_hb"])			# 링크 연결 중 관리 콘솔 생존 표시
        self.assertEqual((hb["connected"], hb["reason"]), (1, "ok"))


if __name__ == "__main__":
    unittest.main()

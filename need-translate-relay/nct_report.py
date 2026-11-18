"""NEED Chat Translate v1 - pilot report (Markdown) from the relay database.

Uses only counters, token sums, costs and latencies. No chat text, no character names, no ids.
The layout follows md/chat_translate_20261118/NEED_Chat_Translate_Pilot_Metrics_Template.md.
"""
import os
from datetime import datetime, timedelta, timezone

import nct_store

API_STATUSES = ("api_ok", "api_refused", "api_error", "api_timeout", "api_rate_limited", "api_spend_limit", "api_auth")


def _pct(values, p):
    if not values:
        return None
    v = sorted(values)
    k = max(0, min(len(v) - 1, int(round(p / 100.0 * (len(v) - 1)))))
    return v[k]


def _days(first: datetime, last: datetime):
    d = first.astimezone(nct_store.KST).date()
    end = last.astimezone(nct_store.KST).date()
    out = []
    while d <= end:
        out.append(d.strftime("%Y-%m-%d"))
        d += timedelta(days=1)
    return out


def collect(store, first_day, last_day):
    stats = store.stats_by_day(first_day, last_day)
    rows = store.ledger_rows(first_day, last_day)
    per = {}
    for day, status, i, o, cw, cr, cost, lat in rows:
        d = per.setdefault(day, {"status": {}, "in": 0, "out": 0, "cw": 0, "cr": 0, "cost": 0, "lat": []})
        d["status"][status] = d["status"].get(status, 0) + 1
        d["in"] += i
        d["out"] += o
        d["cw"] += cw
        d["cr"] += cr
        d["cost"] += cost
        if status == "api_ok":
            d["lat"].append(lat)
    return stats, per


def write_report(store, report_dir, cfg, final=False, now=None):
    os.makedirs(report_dir, exist_ok=True)
    clock = nct_store.Clock(store)
    now = now or clock.now()
    p = store.pilot()
    start = p["started"] or now
    end = p["stopped"] or (min(now, p["ends"]) if p["ends"] else now)
    days = _days(start, end)
    stats, per = collect(store, days[0], days[-1])
    b = cfg["budget"]

    def S(day, k):
        return stats.get(day, {}).get(k, 0)

    def L(day, k):
        return per.get(day, {}).get("status", {}).get(k, 0)

    lines = []
    title = "최종" if final else "중간"
    lines.append("# NEED Chat Translate 7일 파일럿 운영 보고서 (%s)" % title)
    lines.append("")
    lines.append("- 생성: %s" % now.astimezone(nct_store.KST).strftime("%Y-%m-%d %H:%M KST"))
    lines.append("- 파일럿: %s ~ %s (%s)" % (
        start.astimezone(nct_store.KST).strftime("%Y-%m-%d %H:%M KST"),
        end.astimezone(nct_store.KST).strftime("%Y-%m-%d %H:%M KST"),
        p["stop_reason"] or ("진행 중" if p["started"] else "시작 전")))
    lines.append("- 모델/백엔드: %s / %s" % (cfg["claude"].get("model"), cfg["relay"].get("backend")))
    lines.append("- 예산 상한: 파일럿 누적 $%s, 일 $%s, 월 $%s" % (b.get("pilot_total_usd"), b.get("daily_usd"), b.get("month_usd")))
    lines.append("- 이 보고서에는 채팅 원문·캐릭터 이름·계정 ID가 들어 있지 않습니다. 금액은 `response.usage` × 단가로 산정한 추정치이며, Console 사용 내역과 대조해야 합니다(`ledger.request_id`).")
    lines.append("")

    # ---- daily table
    lines.append("## 1. 일별 지표 (Asia/Seoul)")
    lines.append("")
    hdr = ["날짜", "원문(주변/채널/파티/길드/전장/클랜/채팅방)", "적격", "수신자없음", "거부", "과다차단", "필터", "정형구문", "캐시", "API 호출", "성공", "실패", "시간초과", "거절", "표시 줄", "고유 수신 KR/EN", "구독 KR/EN(최대)", "ON/OFF", "p50/p95 ms", "입력/출력/캐시읽기/캐시쓰기 tok", "금액 USD"]
    lines.append("| " + " | ".join(hdr) + " |")
    lines.append("|" + "---|" * len(hdr))
    tot = {}

    def acc(k, v):
        tot[k] = tot.get(k, 0) + v

    all_lat = []
    for d in days:
        msgs = [S(d, "map_msg_say"), S(d, "map_msg_channel"), S(d, "map_msg_party"), S(d, "map_msg_guild"), S(d, "map_msg_bg"), S(d, "map_msg_clan"), S(d, "map_msg_chatroom")]
        api = sum(L(d, s) for s in API_STATUSES)
        ok = L(d, "api_ok")
        fail = L(d, "api_error") + L(d, "invalid_output") + L(d, "api_auth") + L(d, "api_spend_limit") + L(d, "api_rate_limited")
        tmo = L(d, "api_timeout") + L(d, "timeout") + L(d, "timeout_queue") + S(d, "map_expired")
        flt = L(d, "skip") + S(d, "map_skip_no_lang") + S(d, "map_skip_command")
        quota = S(d, "map_skip_sender_quota") + L(d, "quota_speaker") + L(d, "quota_global") + L(d, "queue_full")
        cache = L(d, "cache") + L(d, "coalesced")
        pd = per.get(d, {"in": 0, "out": 0, "cw": 0, "cr": 0, "cost": 0, "lat": []})
        lat = pd["lat"]
        all_lat += lat
        p50, p95 = _pct(lat, 50), _pct(lat, 95)
        row = [d, "/".join(str(x) for x in msgs), S(d, "map_eligible"), S(d, "map_skip_no_recipient"), S(d, "map_skip_sender_block"),
               quota, flt, L(d, "phrase"), cache, api, ok, fail, tmo, L(d, "api_refused"), S(d, "map_lines"),
               "%d/%d" % (S(d, "map_recipients_kr"), S(d, "map_recipients_en")),
               "%d/%d" % (S(d, "map_subscribers_kr"), S(d, "map_subscribers_en")),
               "%d/%d" % (S(d, "map_toggle_on"), S(d, "map_toggle_off")),
               "%s/%s" % (p50 if p50 is not None else "-", p95 if p95 is not None else "-"),
               "%d/%d/%d/%d" % (pd["in"], pd["out"], pd["cr"], pd["cw"]), "%.4f" % (pd["cost"] / 1e6)]
        lines.append("| " + " | ".join(str(x) for x in row) + " |")
        for k, v in (("msgs", sum(msgs)), ("eligible", S(d, "map_eligible")), ("api", api), ("ok", ok), ("fail", fail), ("tmo", tmo),
                     ("cache", cache), ("phrase", L(d, "phrase")), ("flt", flt), ("quota", quota), ("lines", S(d, "map_lines")),
                     ("in", pd["in"]), ("out", pd["out"]), ("cr", pd["cr"]), ("cw", pd["cw"]), ("cost", pd["cost"]),
                     ("hook_us", S(d, "map_hook_us_total")), ("hook_calls", S(d, "map_hook_calls")), ("rtt", S(d, "map_rtt_ms_total")),
                     ("rtt_n", S(d, "map_rtt_count")), ("refused", L(d, "api_refused")), ("budget_block", L(d, "budget_block")),
                     ("on", S(d, "map_toggle_on")), ("off", S(d, "map_toggle_off")), ("block_on", S(d, "map_block_on"))):
            acc(k, v)
        tot["hook_max"] = max(tot.get("hook_max", 0), S(d, "map_hook_us_max"))
        tot["rec_kr"] = max(tot.get("rec_kr", 0), S(d, "map_recipients_kr"))
        tot["rec_en"] = max(tot.get("rec_en", 0), S(d, "map_recipients_en"))
        tot["sub_kr"] = max(tot.get("sub_kr", 0), S(d, "map_subscribers_kr"))
        tot["sub_en"] = max(tot.get("sub_en", 0), S(d, "map_subscribers_en"))
        tot["online"] = max(tot.get("online", 0), S(d, "map_online"))
    lines.append("")

    # ---- totals
    n_days = max(1.0, (end - start).total_seconds() / 86400.0)
    cost = tot.get("cost", 0) / 1e6
    month = cost / n_days * 30.0
    api = tot.get("api", 0)
    reuse = tot.get("cache", 0) + tot.get("phrase", 0)
    lines.append("## 2. 기간 합계와 월 환산")
    lines.append("")
    lines.append("| 항목 | 값 |")
    lines.append("|---|---|")
    lines.append("| 기간(일) | %.2f |" % n_days)
    lines.append("| 원문 메시지 | %d |" % tot.get("msgs", 0))
    lines.append("| 번역 적격 | %d |" % tot.get("eligible", 0))
    lines.append("| 실제 API 호출 | %d |" % api)
    lines.append("| 캐시·정형구문 재사용 | %d (API 대비 %.1f%%) |" % (reuse, 100.0 * reuse / max(1, api + reuse)))
    lines.append("| 성공 / 실패 / 시간초과 / 거절 | %d / %d / %d / %d |" % (tot.get("ok", 0), tot.get("fail", 0), tot.get("tmo", 0), tot.get("refused", 0)))
    lines.append("| 예산 차단 | %d |" % tot.get("budget_block", 0))
    lines.append("| 표시된 번역 줄 | %d |" % tot.get("lines", 0))
    lines.append("| API 지연 p50 / p95 (ms) | %s / %s |" % (_pct(all_lat, 50) or "-", _pct(all_lat, 95) or "-"))
    lines.append("| 토큰 입력 / 출력 / 캐시읽기 / 캐시쓰기 | %d / %d / %d / %d |" % (tot.get("in", 0), tot.get("out", 0), tot.get("cr", 0), tot.get("cw", 0)))
    lines.append("| 산정 금액 (USD) | %.4f |" % cost)
    lines.append("| 월 환산 (USD, 기간 평균 × 30) | %.2f |" % month)
    lines.append("| 호출당 평균 (USD) | %.6f |" % (cost / api if api else 0))
    lines.append("")
    lines.append("## 3. 사용자 채택")
    lines.append("")
    lines.append("| 항목 | 값 |")
    lines.append("|---|---|")
    lines.append("| 하루 최대 고유 수신자 KR / EN | %d / %d |" % (tot.get("rec_kr", 0), tot.get("rec_en", 0)))
    lines.append("| 동시 구독자 최대 KR / EN | %d / %d |" % (tot.get("sub_kr", 0), tot.get("sub_en", 0)))
    lines.append("| 동시 접속 최대 | %d |" % tot.get("online", 0))
    lines.append("| 번역 ON / OFF 변경 | %d / %d |" % (tot.get("on", 0), tot.get("off", 0)))
    lines.append("| 발신자 번역거부 설정 | %d |" % tot.get("block_on", 0))
    lines.append("")
    lines.append("## 4. 서버 영향")
    lines.append("")
    hook_avg = tot.get("hook_us", 0) / max(1, tot.get("hook_calls", 0))
    lines.append("| 항목 | 값 |")
    lines.append("|---|---|")
    lines.append("| 채팅 1건당 번역 훅 평균 (µs, 원문 전송·로그 이후 실행) | %.1f |" % hook_avg)
    lines.append("| 번역 훅 최대 (µs, 분 단위 최대값 중 최대) | %d |" % tot.get("hook_max", 0))
    lines.append("| map-server 기준 요청→응답 평균 (ms) | %.1f |" % (tot.get("rtt", 0) / max(1, tot.get("rtt_n", 0))))
    lines.append("")

    # ---- recommendation (mechanical, the operator decides)
    lines.append("## 5. 자동 판정 참고값 (운영자 최종 결정)")
    lines.append("")
    month_cap = float(b.get("month_usd"))
    rec = []
    if month > month_cap:
        rec.append("월 환산 $%.2f > 월 상한 $%.0f: **채널 제한 또는 수동 중심 권장**" % (month, month_cap))
    elif month > month_cap * 0.6:
        rec.append("월 환산 $%.2f: 월 상한의 60%% 초과, **채널 제한 검토**" % month)
    else:
        rec.append("월 환산 $%.2f: 월 상한 안쪽" % month)
    users = tot.get("rec_kr", 0) + tot.get("rec_en", 0)
    if users == 0:
        rec.append("번역을 받은 사용자가 없음: **수동 중심(기본 OFF) 권장**")
    if tot.get("tmo", 0) + tot.get("fail", 0) > max(10, 0.05 * max(1, api)):
        rec.append("실패·시간초과가 API 호출의 5%% 초과: 지연·오류 원인 확인 전 확대 금지")
    for r in rec:
        lines.append("- " + r)
    lines.append("")
    lines.append("선택지: (A) 자동 유지 / (B) 채널별 제한 유지 / (C) 수동 중심(기본 OFF, 원하는 사람만 @번역 켜기). 파일럿 종료 후 유료 운영은 자동으로 연장되지 않습니다.")
    lines.append("")

    name = "NEED_Chat_Translate_Pilot_Report_%s%s.md" % (now.astimezone(nct_store.KST).strftime("%Y%m%d_%H%M"), "_final" if final else "")
    path = os.path.join(report_dir, name)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    return path

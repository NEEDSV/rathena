"""Apply the 2026-10-21 refine-economy policy to npc/NEED/need_lucky_ceiling.txt.

Byte-safe: the script is CP949 + CRLF. Every replacement anchor must occur exactly once in the
original, otherwise nothing is written. Run once on a clean checkout.

Usage: python -I apply_ceiling_patch.py <path/to/need_lucky_ceiling.txt>
"""
import sys
from pathlib import Path

T = "\t"


def block(*lines):
    return "\r\n".join(lines)


REPLACEMENTS = []


def rep(old, new):
    REPLACEMENTS.append((old, new))


# --- header ------------------------------------------------------------------
rep(block("// [상품 구성]",
          "// 공용 희귀 3종 (PID 100~102) + 시즌 천장 44종 (PID 200, 202~244)"),
    block("// [상품 구성]",
          "// 공용 희귀 5종 (PID 100~104) + 시즌 천장 (PID 200, 202~)",
          "//   103 대축 1개 -> +7 보장권 / 104 대축 15개 -> +9 보장권 (2026-10-21 운영자 확정)",
          "//   104 는 102(+9 포인트 천장)의 서버 월간 재고를 함께 쓴다. ([재고 공유] 참고)",
          "// (아래 44종 설명은 최초 등록 당시 기준)"))

# --- PID policy comment -------------------------------------------------------
rep(block("//   현재 사용 : 100 ~ 102 (공용) / 200, 202 ~ 244 (시즌)",
          "//   다음 신규 상품은 245 부터 시작한다."),
    block("//   현재 사용 : 100 ~ 104 (공용) / 200, 202 ~ 328 (시즌, 2026-10-21 기준)",
          "//   시즌 신규 PID 는 행알 출시 도구 정책(last_assigned_pid)을 따른다. 공용 신규는 105 부터."))
rep("//   공용 희귀(PID 100~102) 는 이번 개편 대상이 아니다.",
    block("//   공용 희귀(PID 100~102) 는 이번 개편 대상이 아니다.",
          "//   2026-10-21 운영자 확정 공용 희귀 변경:",
          "//     100 대축 200P + 1천만 Zeny (기존 1억) / 102 +9 2000P + 1억 Zeny (기존 5000P + 5억)",
          "//     103 대축 1개 -> +7 보장권 (무제한) / 104 대축 15개 -> +9 보장권 (102 와 월 1개 공유)"))

# --- S_Detail ----------------------------------------------------------------
rep(block("S_Detail:",
          T + ".@i = getarg(0);",
          T + ".@pid = .Pid[.@i];",
          "",
          T + "callsub S_Normalize, .@i;"),
    block("S_Detail:",
          T + ".@i = getarg(0);",
          T + ".@pid = .Pid[.@i];",
          "",
          T + "callsub S_Normalize, .@i;",
          T + ".@skey = callsub(S_SKey, .@i);"))

rep(block(T + 'mes needtr("^FF0000[필요 재료]^000000", "^FF0000[Materials Needed]^000000");',
          T + 'mes "- " + getitemname(.PointItem) + " : " + .Point[.@i] + " P"',
          T + T + '+ needtr("  (보유 ", "  (have ") + countitem(.PointItem) + ")";'),
    block(T + 'mes needtr("^FF0000[필요 재료]^000000", "^FF0000[Materials Needed]^000000");',
          T + "// 포인트/Zeny 가 0 인 상품(대축 교환)은 해당 줄을 표시하지 않는다.",
          T + "if (.Point[.@i] > 0)",
          T + T + 'mes "- " + getitemname(.PointItem) + " : " + .Point[.@i] + " P"',
          T + T + T + '+ needtr("  (보유 ", "  (have ") + countitem(.PointItem) + ")";'))

rep(block(T + 'mes "- Zeny : " + callfunc("F_InsertComma", .Zeny[.@i])',
          T + T + '+ needtr("  (보유 ", "  (have ") + callfunc("F_InsertComma", Zeny) + ")";'),
    block(T + "if (.Zeny[.@i] > 0)",
          T + T + 'mes "- Zeny : " + callfunc("F_InsertComma", .Zeny[.@i])',
          T + T + T + '+ needtr("  (보유 ", "  (have ") + callfunc("F_InsertComma", Zeny) + ")";'))

rep(block(T + T + 'mes needtr("- 남은 수량 : ", "- stock left : ") + $NeedLPStock[.@pid] + " / " + .Stock[.@i]',
          T + T + T + '+ " (" + ((.Cycle[.@i] == 1) ? needtr("주간", "weekly") : needtr("월간", "monthly")) + ")";',
          T + T + 'mes needtr("- 다음 재입고 : ", "- next restock : ") + callsub(S_NextResetStr, .@i);',
          T + "}"),
    block(T + T + 'mes needtr("- 남은 수량 : ", "- stock left : ") + $NeedLPStock[.@skey] + " / " + .Stock[.@i]',
          T + T + T + '+ " (" + ((.Cycle[.@i] == 1) ? needtr("주간", "weekly") : needtr("월간", "monthly")) + ")";',
          T + T + 'mes needtr("- 다음 재입고 : ", "- next restock : ") + callsub(S_NextResetStr, .@i);',
          T + T + "callsub S_ShareNote, .@i;",
          T + "}"))

rep(T + "if (.Cycle[.@i] != 0 && $NeedLPStock[.@pid] <= 0) {",
    T + "if (.Cycle[.@i] != 0 && $NeedLPStock[.@skey] <= 0) {")

rep(block(T + 'mes "- " + getitemname(.PointItem) + " " + .Point[.@i] + " P";',
          T + "if (.EggId[.@i] > 0)",
          T + T + 'mes "- " + mesitemlink(.EggId[.@i]) + " " + .EggAmt[.@i] + needtr("개", "");',
          T + 'mes "- Zeny " + callfunc("F_InsertComma", .Zeny[.@i]);'),
    block(T + "if (.Point[.@i] > 0)",
          T + T + 'mes "- " + getitemname(.PointItem) + " " + .Point[.@i] + " P";',
          T + "if (.EggId[.@i] > 0)",
          T + T + 'mes "- " + mesitemlink(.EggId[.@i]) + " " + .EggAmt[.@i] + needtr("개", "");',
          T + "if (.Zeny[.@i] > 0)",
          T + T + 'mes "- Zeny " + callfunc("F_InsertComma", .Zeny[.@i]);',
          T + "callsub S_ShareNote, .@i;"))

rep(block(T + T + "if (.Cycle[.@i] != 0)",
          T + T + T + 'mes needtr("서버 남은 수량 : ", "Server stock left : ") + $NeedLPStock[.@pid] + " / " + .Stock[.@i];'),
    block(T + T + "if (.Cycle[.@i] != 0)",
          T + T + T + 'mes needtr("서버 남은 수량 : ", "Server stock left : ") + $NeedLPStock[.@skey] + " / " + .Stock[.@i];'))

# --- S_Buy -------------------------------------------------------------------
rep(block(T + "callsub S_Normalize, .@i;",
          "",
          T + 'if (.Choice$[.@i] != "") {',
          T + T + "explode(.@parts$, .Choice$[.@i], \",\");",
          T + T + ".@nparts = getarraysize(.@parts$);",
          T + T + "if (getarg(1) < 0 || getarg(1) >= .@nparts)"),
    block(T + "callsub S_Normalize, .@i;",
          "",
          T + "// 재고 키: 공유 상품(104)은 102 의 재고를 쓴다. 공유 설정이 깨졌으면 0 -> 판매 거부",
          T + ".@skey = callsub(S_SKey, .@i);",
          T + "if (.@skey <= 0)",
          T + T + "return 7;",
          "",
          T + 'if (.Choice$[.@i] != "") {',
          T + T + "explode(.@parts$, .Choice$[.@i], \",\");",
          T + T + ".@nparts = getarraysize(.@parts$);",
          T + T + "if (getarg(1) < 0 || getarg(1) >= .@nparts)"))

rep(T + "if (.Cycle[.@i] != 0 && $NeedLPStock[.@pid] <= 0)\r\n" + T + T + "return 2;",
    T + "if (.Cycle[.@i] != 0 && $NeedLPStock[.@skey] <= 0)\r\n" + T + T + "return 2;")

rep(block(T + "// (1) 서버 재고 차감을 SQL 에 먼저 확정한다.",
          T + "//     저장/검증에 실패하면 여기서 끝내므로 유저 재화는 그대로 남는다.",
          T + "if (.Cycle[.@i] != 0) {",
          T + T + ".@newstock = $NeedLPStock[.@pid] - 1;",
          "",
          T + T + "if (!callsub(S_SaveStock, .@i, $NeedLPCyc[.@pid], .@newstock))",
          T + T + T + "return 9;",
          "",
          T + T + "$NeedLPStock[.@pid] = .@newstock;",
          T + "}"),
    block(T + "// (1) 서버 재고 차감을 SQL 에 먼저 확정한다.",
          T + "//     저장/검증에 실패하면 여기서 끝내므로 유저 재화는 그대로 남는다.",
          T + "//     2026-10-21: 메모리 값으로 절대값을 덮어쓰던 방식을 DB 조건부 차감",
          T + "//     (stock >= 1 일 때만 -1, ROW_COUNT 확인)으로 바꿨다. 맵서버가 여러 개이거나",
          T + "//     공유 재고(102/104)를 두 경로가 동시에 노려도 DB 재고 아래로는 팔 수 없다.",
          T + "if (.Cycle[.@i] != 0) {",
          T + T + ".@took = callsub(S_TakeStock, .@i);",
          T + T + "if (.@took == 2)",
          T + T + T + "return 2;",
          T + T + "if (.@took != 1)",
          T + T + T + "return 9;",
          T + "}"))

rep(block(T + T + '+ " left=" + ((.Cycle[.@i] != 0) ? $NeedLPStock[.@pid] : -1);'),
    block(T + T + '+ " left=" + ((.Cycle[.@i] != 0) ? $NeedLPStock[.@skey] : -1)',
          T + T + '+ ((.@skey != .@pid) ? " skey=" + .@skey : "");'))

# --- S_Rules -----------------------------------------------------------------
rep(T + 'mes needtr("- 행운의 알 없이 포인트와 Zeny로만 교환합니다.", "- Traded with points and Zeny only - no egg needed.");',
    block(T + 'mes needtr("- 행운의 알 없이 포인트·Zeny 또는 대장장이의 축복으로 교환합니다.", "- Traded with points and Zeny, or with Blacksmith\'s Blessings - no egg needed.");',
          T + 'mes needtr("- +9 보장권은 포인트 교환과 대축 15개 교환을 합쳐", "- The +9 certificate stock is shared by the point trade");',
          T + 'mes needtr("  ^FF0000서버 전체 월 1개^000000입니다.", "  and the 15-Blessing trade: ^FF00001 per month server-wide^000000.");'))

# --- S_StockTag ----------------------------------------------------------------
rep(block(T + ".@pid = .Pid[.@i];",
          "",
          T + "if ($NeedLPStock[.@pid] <= 0)",
          T + T + 'return needtr(" (품절)", " (sold out)");',
          "",
          T + 'return " (" + $NeedLPStock[.@pid] + "/" + .Stock[.@i] + ")";'),
    block(T + ".@pid = callsub(S_SKey, .@i);",
          "",
          T + "if (.@pid <= 0 || $NeedLPStock[.@pid] <= 0)",
          T + T + 'return needtr(" (품절)", " (sold out)");',
          "",
          T + 'return " (" + $NeedLPStock[.@pid] + "/" + .Stock[.@i] + ")";',
          "",
          "",
          "//============================================================",
          "// [재고 공유] 재고 키",
          "// getarg(0) : 상품 슬롯 index",
          "// 반환 : 서버 재고를 저장/차감하는 PID. 공유 설정이 깨진 상품은 0.",
          "//",
          "// .StockShareSrc[] 의 PID 는 같은 위치 .StockShareKey[] PID 의 재고를 함께 쓴다.",
          "//   104 (대축 15개 -> +9 보장권) -> 102 (+9 포인트 천장) : 서버 월간 1개를 공유",
          "// 공유 상품은 자기 PID 로 재고 행을 만들지 않으므로 월 재고가 늘어나지 않는다.",
          "//============================================================",
          "S_SKey:",
          T + ".@i = getarg(0);",
          T + ".@pos = inarray(.StockShareSrc[0], .Pid[.@i]);",
          T + "if (.@pos < 0)",
          T + T + "return .Pid[.@i];",
          T + "if (!.StockShareOK[.@pos])",
          T + T + "return 0;",
          T + "return .StockShareKey[.@pos];",
          "",
          "",
          "//============================================================",
          "// [재고 공유] 안내 문구 (재고를 공유하는 상품에서만 출력)",
          "//============================================================",
          "S_ShareNote:",
          T + ".@i = getarg(0);",
          T + ".@key = callsub(S_SKey, .@i);",
          T + ".@shared = (inarray(.StockShareSrc[0], .Pid[.@i]) >= 0 || inarray(.StockShareKey[0], .Pid[.@i]) >= 0);",
          T + "if (!.@shared || .@key <= 0)",
          T + T + "return;",
          T + 'mes needtr("^FF0000※ 서버 전체 공유 재고^000000", "^FF0000* Server-wide shared stock^000000");',
          T + 'mes needtr("포인트 교환과 대축 교환이 같은 재고를 씁니다.", "The point trade and the Blessing trade use the same stock.");',
          T + 'mes needtr("어느 쪽이든 먼저 교환하면 나머지는 품절됩니다.", "Whichever is traded first sells the other out too.");',
          T + "return;",
          "",
          "",
          "//============================================================",
          "// [재고 공유] DB 조건부 재고 차감",
          "// getarg(0) : 상품 슬롯 index",
          "// 반환 1 = 차감 성공 / 2 = 재고 없음 / 9 = DB 오류 또는 주기 불일치",
          "//",
          "// UPDATE ... WHERE stock >= 1 은 DB 안에서 원자적이다. 바로 다음 SELECT ROW_COUNT()",
          "// 는 같은 연결에서 실행되며, 그 사이에 이 맵서버의 다른 스크립트가 끼어들 수 없다.",
          "// 차감 결과와 상관없이 DB 값을 다시 읽어 인메모리 사본을 맞춘다.",
          "// [중요] 이 라벨 안에 next / select / input / sleep 을 넣지 말 것.",
          "//============================================================",
          "S_TakeStock:",
          T + ".@i = getarg(0);",
          T + ".@pid = callsub(S_SKey, .@i);",
          T + "if (.@pid <= 0 || !.SqlReady)",
          T + T + "return 9;",
          T + ".@key = $NeedLPCyc[.@pid];",
          "",
          T + ".@r = query_sql(",
          T + T + '"UPDATE `need_lucky_ceiling_stock` SET `stock`=`stock`-1,`updated_at`=NOW() "',
          T + T + '+ "WHERE `product_id`=" + .@pid + " AND `period_key`=" + .@key + " AND `stock`>=1");',
          T + "if (.@r < 0) {",
          T + T + 'logmes "[LuckyCeil] SQL TAKE FAILED pid=" + .@pid + " key=" + .@key;',
          T + T + "return 9;",
          T + "}",
          "",
          T + ".@rc = -1;",
          T + '.@n = query_sql("SELECT ROW_COUNT()", .@rc);',
          "",
          T + ".@dbstock = -1;",
          T + ".@dbkey = 0;",
          T + ".@m = query_sql(",
          T + T + '"SELECT `stock`,`period_key` FROM `need_lucky_ceiling_stock` WHERE `product_id`=" + .@pid,',
          T + T + ".@dbstock, .@dbkey);",
          T + "if (.@m >= 1 && .@dbkey == .@key)",
          T + T + "$NeedLPStock[.@pid] = .@dbstock;",
          "",
          T + "if (.@n >= 1 && .@rc == 1)",
          T + T + "return 1;",
          "",
          T + 'logmes "[LuckyCeil] TAKE REJECTED pid=" + .@pid + " key=" + .@key + " rc=" + .@rc',
          T + T + '+ " db_stock=" + .@dbstock + " db_key=" + .@dbkey;',
          T + "if (.@n >= 1 && .@rc == 0 && .@m >= 1 && .@dbkey == .@key)",
          T + T + "return 2;",
          T + "return 9;",
          "",
          "",
          "//============================================================",
          "// [재고 공유] 공유 설정 검사 (OnInit)",
          "// 공유 상품과 원본 상품의 .Cycle / .Stock 이 다르거나 원본이 없으면",
          "// 공유 상품을 판매 거부(S_SKey = 0) 상태로 둔다.",
          "//============================================================",
          "S_CheckShare:",
          T + "for (.@s = 0; .@s < getarraysize(.StockShareSrc); .@s++) {",
          T + T + ".@a = inarray(.Pid[0], .StockShareSrc[.@s]);",
          T + T + ".@b = inarray(.Pid[0], .StockShareKey[.@s]);",
          T + T + ".StockShareOK[.@s] = 0;",
          T + T + "if (.@a < 0 || .@b < 0 || .@a == .@b) {",
          T + T + T + 'debugmes "[LuckyCeil] share config: PID " + .StockShareSrc[.@s] + " -> " + .StockShareKey[.@s] + " not found. sale blocked.";',
          T + T + T + "continue;",
          T + T + "}",
          T + T + "if (inarray(.StockShareSrc[0], .StockShareKey[.@s]) >= 0) {",
          T + T + T + 'debugmes "[LuckyCeil] share config: PID " + .StockShareKey[.@s] + " is itself shared. sale blocked.";',
          T + T + T + "continue;",
          T + T + "}",
          T + T + "if (.Cycle[.@a] != .Cycle[.@b] || .Stock[.@a] != .Stock[.@b] || .Cycle[.@b] == 0) {",
          T + T + T + 'debugmes "[LuckyCeil] share config: PID " + .StockShareSrc[.@s] + " cycle/stock differs from " + .StockShareKey[.@s] + ". sale blocked.";',
          T + T + T + "continue;",
          T + T + "}",
          T + T + ".StockShareOK[.@s] = 1;",
          T + "}",
          T + "return;"))

# --- S_SaveStock (admin) -----------------------------------------------------
rep(block("S_SaveStock:",
          T + "if (!.SqlReady)",
          T + T + "return 0;",
          "",
          T + ".@i = getarg(0);",
          T + ".@pid = .Pid[.@i];"),
    block("S_SaveStock:",
          T + "if (!.SqlReady)",
          T + T + "return 0;",
          "",
          T + ".@i = getarg(0);",
          T + ".@pid = callsub(S_SKey, .@i);",
          T + "if (.@pid <= 0)",
          T + T + "return 0;"))

# --- S_Normalize ---------------------------------------------------------------
rep(block("S_Normalize:",
          T + ".@i = getarg(0);",
          T + ".@pid = .Pid[.@i];"),
    block("S_Normalize:",
          T + ".@i = getarg(0);",
          T + ".@pid = callsub(S_SKey, .@i);",
          T + "if (.@pid <= 0)",
          T + T + "return 0;"))

rep(block(T + "if ($NeedLPCyc[.@pid] != .@key) {",
          T + T + "if (!callsub(S_SaveStock, .@i, .@key, .Stock[.@i]))",
          T + T + T + "return 0;",
          "",
          T + T + "$NeedLPCyc[.@pid] = .@key;",
          T + T + "$NeedLPStock[.@pid] = .Stock[.@i];",
          "",
          T + T + 'logmes "[LuckyCeil] CYCLE RESET pid=" + .@pid + " key=" + .@key + " stock=" + .Stock[.@i];',
          T + "}"),
    block(T + "if ($NeedLPCyc[.@pid] != .@key) {",
          T + T + "// 2026-10-21: DB 행의 주기가 다를 때만 기본 재고로 되돌린다.",
          T + T + "// (다른 맵서버가 이미 새 주기로 리셋하고 판매했다면 그 값을 유지한다)",
          T + T + "if (!.SqlReady)",
          T + T + T + "return 0;",
          T + T + ".@r = query_sql(",
          T + T + T + '"INSERT INTO `need_lucky_ceiling_stock` "',
          T + T + T + '+ "(`product_id`,`period_type`,`period_key`,`stock`,`base_stock`,`updated_at`) VALUES ("',
          T + T + T + '+ .@pid + "," + .Cycle[.@i] + "," + .@key + "," + .Stock[.@i] + "," + .Stock[.@i] + ",NOW()) "',
          T + T + T + '+ "ON DUPLICATE KEY UPDATE "',
          T + T + T + '+ "`stock`=IF(`period_key`=VALUES(`period_key`),`stock`,VALUES(`stock`)),"',
          T + T + T + '+ "`updated_at`=IF(`period_key`=VALUES(`period_key`),`updated_at`,NOW()),"',
          T + T + T + '+ "`period_type`=VALUES(`period_type`),`base_stock`=VALUES(`base_stock`),"',
          T + T + T + '+ "`period_key`=VALUES(`period_key`)");',
          T + T + "if (.@r < 0) {",
          T + T + T + 'logmes "[LuckyCeil] SQL SAVE FAILED pid=" + .@pid + " key=" + .@key + " stock=" + .Stock[.@i];',
          T + T + T + "return 0;",
          T + T + "}",
          T + T + ".@chk = -999999;",
          T + T + ".@ck = 0;",
          T + T + ".@n = query_sql(",
          T + T + T + '"SELECT `stock`,`period_key` FROM `need_lucky_ceiling_stock` WHERE `product_id`=" + .@pid,',
          T + T + T + ".@chk, .@ck);",
          T + T + "if (.@n < 1 || .@ck != .@key) {",
          T + T + T + 'logmes "[LuckyCeil] SQL VERIFY FAILED pid=" + .@pid + " expect_key=" + .@key + " got_key=" + .@ck;',
          T + T + T + "return 0;",
          T + T + "}",
          "",
          T + T + "$NeedLPCyc[.@pid] = .@key;",
          T + T + "$NeedLPStock[.@pid] = .@chk;",
          "",
          T + T + 'logmes "[LuckyCeil] CYCLE RESET pid=" + .@pid + " key=" + .@key + " stock=" + .@chk;',
          T + "}"))

# --- S_AdminItem (stock view/edit follows the stock key) ---------------------
rep(block("S_AdminItem:",
          T + ".@i = getarg(0);",
          T + ".@pid = .Pid[.@i];",
          "",
          T + "while (1) {",
          T + T + "callsub S_Normalize, .@i;",
          "",
          T + T + 'mes "[" + needtr(.Name$[.@i], .Name_en$[.@i]) + "]";'),
    block("S_AdminItem:",
          T + ".@i = getarg(0);",
          T + ".@pid = .Pid[.@i];",
          T + ".@skey = callsub(S_SKey, .@i);",
          "",
          T + "while (1) {",
          T + T + "callsub S_Normalize, .@i;",
          "",
          T + T + 'mes "[" + needtr(.Name$[.@i], .Name_en$[.@i]) + "]";',
          T + T + "if (.@skey != .@pid)",
          T + T + T + 'mes needtr("^FF0000재고 공유 : PID ", "^FF0000Stock shared with PID ") + .@skey + needtr(" (0 = 공유 설정 오류)^000000", " (0 = share config error)^000000");'))


def main():
    path = Path(sys.argv[1])
    raw = path.read_bytes()
    text = raw.decode("cp949")
    assert text.count("\r\n") == text.count("\n"), "mixed newlines"
    for old, new in REPLACEMENTS:
        c = text.count(old)
        if c != 1:
            raise SystemExit(f"anchor count {c} != 1:\n{old[:200]}")
    for old, new in REPLACEMENTS:
        text = text.replace(old, new)

    # admin-scope stock references: only inside S_AdminItem
    a = text.index("S_AdminItem:")
    b = text.index("S_AdminSetMode:")
    seg = text[a:b]
    seg2 = seg.replace("$NeedLPStock[.@pid]", "$NeedLPStock[.@skey]").replace("$NeedLPCyc[.@pid]", "$NeedLPCyc[.@skey]")
    text = text[:a] + seg2 + text[b:]

    text = apply_arrays(text)
    out = text.encode("cp949")
    path.write_bytes(out)
    print(f"patched {path} {len(raw)} -> {len(out)} bytes, {len(REPLACEMENTS)} anchors")


NEW_ROWS = [
    # (array, value_103, value_104)
    ("Pid", "103", "104"),
    ("Code$", '"BS_TO_REFINE7"', '"BS_TO_REFINE9"'),
    ("Name$", '"대축 1개 → +7 보장권"', '"대축 15개 → +9 보장권"'),
    ("Name_en$", '"Blessing x1 -> +7 Certificate"', '"Blessing x15 -> +9 Certificate"'),
    ("Enabled", "1", "1"),
    ("Cat", "2", "2"),
    ("Point", "0", "0"),
    ("Zeny", "0", "0"),
    ("EggId", "6635", "6635"),
    ("EggAmt", "1", "15"),
    ("Stock", "0", "1"),
    ("Cycle", "0", "2"),
    ("Bound", "0", "0"),
    ("Reward", "0", "0"),
    ("RewardAmt", "1", "1"),
    ("Choice$", '"6230,6234"', '"6228,6232"'),
    ("RateText$", '""', '""'),
]
COMMENT = {"103": "103 대축 1개 → +7 보장권 (2026-10-21)", "104": "104 대축 15개 → +9 보장권 / 102 재고 공유 (2026-10-21)"}

PRICE_EDITS = [
    # (array, old_line, new_line)
    ("Zeny", "\t\t100000000,\t// 100 대장장이의 축복", "\t\t10000000,\t// 100 대장장이의 축복 (2026-10-21 1억 -> 1천만)"),
    ("Point", "\t\t5000,\t// 102 +9 제련 교환권", "\t\t2000,\t// 102 +9 제련 교환권 (2026-10-21 5000 -> 2000)"),
    ("Zeny", "\t\t500000000,\t// 102 +9 제련 교환권", "\t\t100000000,\t// 102 +9 제련 교환권 (2026-10-21 5억 -> 1억)"),
]


def array_span(lines, name):
    head = f"\tsetarray .{name}[0],"
    starts = [i for i, l in enumerate(lines) if l == head]
    if len(starts) != 1:
        raise SystemExit(f"array {name}: {len(starts)} heads")
    s = starts[0]
    e = s + 1
    while not lines[e].split("//")[0].rstrip().endswith(";"):
        e += 1
    return s, e


def apply_arrays(text):
    lines = text.split("\r\n")
    for name, old, new in PRICE_EDITS:
        s, e = array_span(lines, name)
        hits = [i for i in range(s + 1, e + 1) if lines[i] == old]
        if len(hits) != 1:
            raise SystemExit(f"price edit {name}: {len(hits)} hits for {old!r}")
        lines[hits[0]] = new
    sizes = {}
    for name, v103, v104 in reversed(NEW_ROWS):  # bottom-up keeps earlier indices valid
        s, e = array_span(lines, name)
        last = lines[e]
        body, sep, com = last.partition("\t//")
        if not body.rstrip().endswith(";"):
            raise SystemExit(f"array {name} last line has no ';': {last!r}")
        lines[e] = body.rstrip()[:-1] + "," + (sep + com if sep else "")
        lines[e + 1:e + 1] = [f"\t\t{v103},\t// {COMMENT['103']}", f"\t\t{v104};\t// {COMMENT['104']}"]
    # share map + validation, outside the product-array region (before S_SqlLoad call in OnInit)
    anchor = "\tcallsub S_SqlLoad;"
    idx = [i for i, l in enumerate(lines) if l == anchor]
    if len(idx) != 1:
        raise SystemExit("OnInit S_SqlLoad anchor")
    lines[idx[0]:idx[0]] = [
        "\t// [재고 공유] (2026-10-21 운영자 확정)",
        "\t// 104 대축 15개 -> +9 보장권은 102 +9 포인트 천장의 서버 월간 재고 1개를 함께 쓴다.",
        "\t// 공유 상품의 .Cycle / .Stock 은 원본과 같아야 한다. 다르면 S_CheckShare 가 판매를 막는다.",
        "\tsetarray .StockShareSrc[0], 104;",
        "\tsetarray .StockShareKey[0], 102;",
        "\tcallsub S_CheckShare;",
        "",
    ]
    return "\r\n".join(lines)


if __name__ == "__main__":
    main()

"""Static checks for the 2026-10-21 refine-economy policy implementation (read-only).

Usage: python -I static_check.py <repo_root> [<baseline_repo_root>]
Prints one line per check: PASS/FAIL name detail. Exit code 1 if any FAIL.
"""
import re
import sys
from pathlib import Path

import yaml

ARRAYS = ["Pid", "Code$", "Name$", "Name_en$", "Enabled", "Cat", "Point", "Zeny", "EggId", "EggAmt",
          "Stock", "Cycle", "Bound", "Reward", "RewardAmt", "Choice$", "RateText$"]
ENTRY = re.compile(r"^\t\t(.*?)([,;])(?:\t(//.*))?$")
RESULTS = []


def check(name, ok, detail=""):
    RESULTS.append((name, bool(ok), detail))


def parse_arrays(text):
    lines = text.split("\r\n")
    cols = {}
    for name in ARRAYS:
        head = f"\tsetarray .{name}[0],"
        idx = [i for i, l in enumerate(lines) if l == head]
        if len(idx) != 1:
            cols[name] = None
            continue
        vals, j = [], idx[0] + 1
        while True:
            m = ENTRY.match(lines[j])
            if not m:
                raise ValueError(f"{name}: bad line {j + 1}: {lines[j]!r}")
            vals.append(m.group(1).strip('"'))
            if m.group(2) == ";":
                break
            j += 1
        cols[name] = vals
    return cols


def product(cols, pid):
    i = cols["Pid"].index(str(pid))
    return {a: cols[a][i] for a in ARRAYS}


def main():
    root = Path(sys.argv[1])
    base = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    ceil = root / "npc/NEED/need_lucky_ceiling.txt"
    raw = ceil.read_bytes()
    check("ceiling CRLF only", raw.count(b"\r\n") == raw.count(b"\n"), f"{raw.count(b'\n')} lines")
    try:
        text = raw.decode("cp949")
        check("ceiling decodes as CP949", True)
    except UnicodeDecodeError as e:
        check("ceiling decodes as CP949", False, str(e))
        return finish()
    cols = parse_arrays(text)
    missing = [a for a in ARRAYS if cols[a] is None]
    check("17 product arrays present", not missing, ",".join(missing))
    sizes = {a: len(cols[a]) for a in ARRAYS if cols[a] is not None}
    check("product array lengths equal", len(set(sizes.values())) == 1, str(sorted(set(sizes.values()))))
    pids = [int(p) for p in cols["Pid"]]
    check("PIDs unique", len(pids) == len(set(pids)), f"{len(pids)} products")
    check("PID 201 not reused", 201 not in pids)

    want = {
        100: {"Point": "200", "Zeny": "10000000", "Stock": "100", "Cycle": "1", "Reward": "6635", "Bound": "0", "Cat": "2", "Enabled": "1"},
        102: {"Point": "2000", "Zeny": "100000000", "Stock": "1", "Cycle": "2", "Choice$": "6228,6232", "Bound": "0", "Cat": "2", "Enabled": "1"},
        103: {"Point": "0", "Zeny": "0", "EggId": "6635", "EggAmt": "1", "Stock": "0", "Cycle": "0", "Choice$": "6230,6234",
              "Reward": "0", "RewardAmt": "1", "Bound": "0", "Cat": "2", "Enabled": "1", "Code$": "BS_TO_REFINE7"},
        104: {"Point": "0", "Zeny": "0", "EggId": "6635", "EggAmt": "15", "Stock": "1", "Cycle": "2", "Choice$": "6228,6232",
              "Reward": "0", "RewardAmt": "1", "Bound": "0", "Cat": "2", "Enabled": "1", "Code$": "BS_TO_REFINE9"},
    }
    for pid, fields in want.items():
        if pid not in pids:
            check(f"PID {pid} exists", False)
            continue
        p = product(cols, pid)
        bad = {k: (p[k], v) for k, v in fields.items() if p[k] != v}
        check(f"PID {pid} values", not bad, str(bad) if bad else ", ".join(f"{k}={v}" for k, v in fields.items()))
    p103, p104 = product(cols, 103), product(cols, 104)
    check("PID 103 name shows '대축 1개 → +7 보장권'", p103["Name$"] == "대축 1개 → +7 보장권", p103["Name$"])
    check("PID 104 name shows '대축 15개 → +9 보장권'", p104["Name$"] == "대축 15개 → +9 보장권", p104["Name$"])
    p102 = product(cols, 102)
    check("104 Cycle/Stock equal to 102 (shared stock precondition)",
          (p104["Cycle"], p104["Stock"]) == (p102["Cycle"], p102["Stock"]))
    check("share map 104 -> 102 declared once",
          text.count("setarray .StockShareSrc[0], 104;") == 1 and text.count("setarray .StockShareKey[0], 102;") == 1)
    lines = text.split("\r\n")
    last_arr = max(i for i, l in enumerate(lines) if l == "\tsetarray .RateText$[0],")
    share_line = lines.index("\tsetarray .StockShareSrc[0], 104;")
    check("share map placed after the product-array region", share_line > last_arr)

    # labels
    calls = set(re.findall(r"callsub\s*\(?\s*(S_\w+)", text))
    labels = set(re.findall(r"^(S_\w+):\s*$", text, re.M))
    check("every callsub target has a label", calls <= labels, ",".join(sorted(calls - labels)))
    dup = [l for l in labels if len(re.findall(rf"^{l}:\s*$", text, re.M)) > 1]
    check("no duplicate labels", not dup, ",".join(dup))
    check("old blessing price line gone", "\t\t100000000,\t// 100 대장장이의 축복" not in text)
    check("old +9 point/zeny lines gone",
          "\t\t5000,\t// 102 +9 제련 교환권" not in text and "\t\t500000000,\t// 102 +9 제련 교환권" not in text)
    check("stock decrement uses conditional UPDATE + ROW_COUNT",
          "AND `stock`>=1" in text and 'query_sql("SELECT ROW_COUNT()"' in text)
    check("S_Buy no longer writes memory-derived absolute stock",
          ".@newstock = $NeedLPStock[.@pid] - 1;" not in text)
    body = text[text.index("S_Buy:"):text.index("S_Rules:")]
    seg = body[body.index("// ---------- 원자적 구간 시작"):body.index("// ---------- 원자적 구간 끝")]
    check("atomic purchase segment has no next/select/input/sleep",
          not re.search(r"\b(next|select|input|sleep2?|menu)\b\s*[;(]", seg))
    take = text[text.index("S_TakeStock:"):text.index("S_CheckShare:")]
    check("S_TakeStock has no next/select/input/sleep", not re.search(r"\b(next|select|input|sleep2?|menu)\b\s*[;(]", take))
    check("+7 -> blessing reverse product absent",
          not any(product(cols, int(p))["Reward"] == "6635" and product(cols, int(p))["EggId"] in ("6230", "6234")
                  for p in cols["Pid"]))

    # unchanged products vs baseline
    if base:
        bcols = parse_arrays((base / "npc/NEED/need_lucky_ceiling.txt").read_bytes().decode("cp949"))
        changed = []
        for i, pid in enumerate(bcols["Pid"]):
            old = {a: bcols[a][i] for a in ARRAYS}
            new = product(cols, int(pid))
            diff = {a: (old[a], new[a]) for a in ARRAYS if old[a] != new[a]}
            if diff:
                changed.append((pid, diff))
        allowed = {("100", "Zeny"), ("102", "Point"), ("102", "Zeny")}
        unexpected = [(p, d) for p, d in changed if any((p, k) not in allowed for k in d)]
        check("only PID 100 Zeny / 102 Point+Zeny changed among existing products", not unexpected,
              str(unexpected[:3]) if unexpected else str(changed))
        check("product count = baseline + 2", len(cols["Pid"]) == len(bcols["Pid"]) + 2,
              f"{len(bcols['Pid'])} -> {len(cols['Pid'])}")

    # barter
    bar = root / "npc/NEED/need_barters.yml"
    try:
        doc = yaml.safe_load(bar.read_text(encoding="utf-8"))
        check("need_barters.yml loads (PyYAML)", True)
    except Exception as e:
        check("need_barters.yml loads (PyYAML)", False, str(e))
        return finish()
    shop = next(s for s in doc["Body"] if s["Name"] == "need_lucky_mileage_shop")
    items = {it["Index"]: it for it in shop["Items"]}
    for idx, aeg in ((0, "Guarantee_Weapon_7Up"), (1, "Guarantee_Armor_7Up")):
        it = items[idx]
        req = it.get("RequiredItems", [])
        ok = (it["Item"] == aeg and it.get("Zeny") == 0 and len(req) == 1
              and req[0]["Item"] == "Lucky_Egg_Point" and req[0]["Amount"] == 200)
        check(f"barter index {idx} {aeg} = 200P + 0z", ok, str(it))
    gold = items.get(2)
    check("barter index 2 (gold coin) unchanged at 200P + 1천만",
          gold and gold.get("Zeny") == 10000000 and gold["RequiredItems"][0]["Amount"] == 200)
    bg = next(s for s in doc["Body"] if s["Name"] == "BG_Victory_Token")
    amounts = [(it["Item"], it["RequiredItems"][0]["Amount"]) for it in bg["Items"][:3]]
    check("BG_Victory_Token ratios unchanged (50/500/500)",
          amounts == [("Blacksmith_Blessing", 50), ("Guarantee_Weapon_9Up", 500), ("Guarantee_Armor_9Up", 500)],
          str(amounts))
    return finish()


def finish():
    fails = 0
    for name, ok, detail in RESULTS:
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  [{detail}]" if detail else ""))
        fails += not ok
    print(f"\n{len(RESULTS) - fails}/{len(RESULTS)} PASS")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()

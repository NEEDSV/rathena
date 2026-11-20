"""Check the active Old Payon topology and inventory every archived quest.

This is an offline data/dependency audit, not the server script parser.
"""
import hashlib
import json
import re
import runpy
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ctx = runpy.run_path(str(ROOT / "doc/need_doram/audit.py"))
read, loaded, quests, items = (ctx[k] for k in ("read", "loaded", "quests", "items"))
out = ROOT / "docs/reports/need-doram-oldpayon-20261118"


def uncomment(text):
    text = text.replace("\r\n", "\n")
    return re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda m: m[0] if m[0].startswith('"') else "\n" * m[0].count("\n"), text)


archived = uncomment(read("npc/re/custom/lasagna/lasagna_npcs.txt"))
helpers = uncomment(read("npc/NEED/need_doram_exchange.txt"))
shop = uncomment(read("npc/NEED/shop.txt"))
beginner = uncomment(read("npc/NEED/need_beginner.txt"))
assert "npc/re/custom/lasagna/lasagna_npcs.txt" not in loaded
assert loaded.count("npc/NEED/need_doram_exchange.txt") == 1
assert loaded.count("npc/NEED/shop.txt") == 1
assert "npc/re/custom/lasagna/lasa_fild.txt" in loaded
assert "npc/re/custom/lasagna/lasa_dun.txt" in loaded
assert "npc/re/warps/cities/lasagna.txt" in loaded
assert not re.search(r"^(?:\w+,\d+,\d+,\d+|-)\t(?:script|duplicate\(|shop\t)", helpers, re.M)
assert not re.search(r"\b(?:setquest|completequest|erasequest|changequest|getitem|questinfo)\b",
                     helpers[helpers.index("function\tscript\tF_NEED_DoramFirstAid"):])
assert beginner.index('callfunc "F_NEED_DoramFirstAid"') < beginner.index('if (!callfunc("F_NEED_IsBeginnerAccount"))', beginner.index('payo2,96,123'))
assert 'payo2,76,125,5\tscript\t도구 상인' in shop
assert 'payo2,76,121,5\tscript\t교환 자판기#1' in shop

# The old top-level exchange choices and shop inventory must survive unchanged.
before = (out / "before/npc/NEED/shop.txt").read_bytes().decode("cp949").replace("\r\n", "\n")
old_menu = next(l for l in before.splitlines() if "switch( select(" in l)
new_menu = next(l for l in shop.splitlines() if "switch( select(" in l)
assert new_menu.replace(',needtr("도람 장비/부적", "Doram Equipment/Charms")', '') == old_menu
for name in ["1페인트", "1총알", "1기타잡템", "1블스", "Pet_Items", "Wedding", "Chef", "1닌자무기", "1건슬무기", "2블스", "1룬스톤", "1독초"]:
    old = next(l for l in before.splitlines() if l.startswith("-\tshop\t" + name + "\t"))
    new = next(l for l in shop.splitlines() if l.startswith("-\tshop\t" + name + "\t"))
    assert new == old + (",515:-1,567:-1,11602:-1" if name == "1기타잡템" else ""), name

# Compare every turn-in pair to the dormant original, including the historical amounts.
def pairs(text):
    text = uncomment(text)
    return list(map(int, re.findall(r"\b\d+\b", text)))


old_arrays = re.findall(r"setarray \.@item_data\[0\],([\s\S]*?);", archived)
new_arrays = re.findall(r"setarray \.@data\[0\],([\s\S]*?);", helpers)
assert len(old_arrays) == len(new_arrays) == 2
for old, new in zip(old_arrays, new_arrays):
    assert pairs(old) == pairs(new)
assert len(set(pairs(new_arrays[1])[::2])) == len(pairs(new_arrays[1])) // 2

# Record each quest's actual definition, start NPC, references, guards and objectives.
query = re.compile(r"\b(isbegin_quest|checkquest|setquest|erasequest|completequest|changequest)\s*\(?\s*(\d+)")
headers = list(re.finditer(r"^([^\n]+)\tscript\t([^\t]+)\t[^\n]+", archived, re.M))
inventory = {}
for i, header in enumerate(headers):
    start = header.start()
    end = headers[i+1].start() if i+1 < len(headers) else len(archived)
    block = archived[start:end]
    line = archived[:start].count("\n") + 1
    guards = [l.strip() for l in block.splitlines() if re.match(r"\s*(?:if|else if|switch)\s*\(", l)
              and re.search(r"Class|BaseLevel|lasagna_quest|conch_in_|sous_chef_quest|checkquest|isbegin_quest", l)]
    for match in query.finditer(block):
        q = int(match[2])
        entry = inventory.setdefault(q, {"id": q, "definition": quests[q], "starts": [], "references": []})
        ref = {"npc": header[2], "line": line + block[:match.start()].count("\n"),
               "operation": match[1], "guards_in_npc": guards}
        entry["references"].append(ref)
        if match[1] == "setquest":
            entry["starts"].append(ref)
assert len(inventory) == 87
for q in (14560, 14561, 14562):
    inventory[q]["dynamic_start"] = "Shaman meow#dorcon: rand(14560,14562), conch_in_boatswain == 4, expired 14559"

external_quests, external_npc_calls = [], []
old_names = {h[2] for h in headers}
for f in dict.fromkeys(loaded):
    text = uncomment(read(f))
    for match in query.finditer(text):
        q = int(match[2])
        if q in inventory:
            external_quests.append({"file": f, "quest": q, "operation": match[1]})
    for call in re.finditer(r'\b(?:donpcevent|doevent|getnpcid|classchange|enablenpc|disablenpc|hideonnpc|hideoffnpc)\s*\(?[^;]*;', text):
        if any('"'+name+'"' in call[0] for name in old_names):
            external_npc_calls.append({"file": f, "call": call[0]})
assert not external_quests, external_quests
assert not external_npc_calls, external_npc_calls

hashes = json.loads((out / "preserved_hashes.json").read_text())
for f, digest in hashes.items():
    assert hashlib.sha256((ROOT / f).read_bytes()).hexdigest() == digest, f

result = {"quests": [inventory[k] for k in sorted(inventory)],
          "script_definitions": len(headers),
          "duplicate_definitions": len(re.findall(r"^[^\n]+\tduplicate\(", archived, re.M)),
          "external_quest_refs": external_quests, "external_npc_calls": external_npc_calls,
          "preserved_files": list(hashes)}
(out / "quest_inventory.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
print("PASS: original 19 exchange choices and 12 shop inventories retained; exactly 3 stock additions")
print("PASS: every weapon/armor turn-in recipe matches the archived original; no duplicate 28423")
print(f"PASS: {len(inventory)} archived quests, no active literal quest dependencies or NPC event calls")
print(f"PASS: {len(headers)} script definitions + {result['duplicate_definitions']} duplicates unloaded; {len(hashes)} preserved file hashes match")
print("NOT TESTED: real NPC parser, live purchases, persistence, KR/EN client, Linux build")

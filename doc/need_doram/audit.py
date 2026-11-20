"""Bounded offline DB/loading audit. Requires PyYAML; never connects to SQL."""
import json
import re
import subprocess
from collections import Counter
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs/reports/need-doram-oldpayon-20261118"
OUT.mkdir(parents=True, exist_ok=True)


def read(path):
    data = (ROOT / path).read_bytes()
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return data.decode("cp949")


def entries(path):
    db = yaml.load(read(path), Loader=yaml.CSafeLoader)
    yield from db.get("Body", []) or []
    for imp in db.get("Footer", {}).get("Imports", []):
        if imp.get("Mode", "Renewal") == "Renewal":
            yield from entries(imp["Path"])


def indexed(path, key):
    result = {}
    for row in entries(path):
        result.setdefault(row[key], {}).update(row)
    return result


def loaded_npcs(path, seen=None):
    seen = set() if seen is None else seen
    if path in seen:
        return []
    seen.add(path)
    result = []
    for line in read(path).splitlines():
        line = line.split("//", 1)[0].strip()
        if line.startswith("import:"):
            result += loaded_npcs(line.split(":", 1)[1].strip(), seen)
        elif line.startswith("npc:"):
            result.append(line.split(":", 1)[1].strip())
    return result


def balanced(text):
    # Validate delimiters outside strings/comments, including all needtr rewrites.
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/|[{}()\[\]]', text)
    stack = []
    for token in tokens:
        if len(token) != 1:
            continue
        if token in "{([":
            stack.append(token)
        else:
            assert stack and stack.pop() == {"}": "{", ")": "(", "]": "["}[token], token
    assert not stack, stack


items = indexed("db/item_db.yml", "Id")
skills = indexed("db/skill_db.yml", "Id")
mobs = indexed("db/mob_db.yml", "Id")
quests = indexed("db/quest_db.yml", "Id")
names = {row["AegisName"]: row for row in items.values()}
equipment_ids = [1681, *range(1683, 1688), *range(1690, 1698), 15126, 15129, 15156,
                 20788, 20789, 20790, 22083, 22084, 22085, 28380, 28381, 28382,
                 28374, *range(28413, 28425)]
equipment_ids += sorted({int(n) for n in re.findall(r'getitem\s+(\d+)', read("npc/re/custom/lasagna/lasagna_npcs.txt"))
                         if int(n) in items and items[int(n)].get("Type") in ["Weapon", "Armor"]
                         and items[int(n)].get("Jobs", {}).get("Summoner")})
equipment_ids = sorted(set(equipment_ids))
routes = {}
for item_id in equipment_ids:
    row = items[item_id]
    routes[item_id] = [{"mob": m["Id"], "name": m["AegisName"], "rate": d["Rate"]}
                       for m in mobs.values() for d in (m.get("Drops") or [])
                       if d.get("Item") == row["AegisName"]]

loaded = loaded_npcs("npc/re/scripts_main.conf")
assert Counter(loaded)["npc/re/custom/lasagna/lasagna_npcs.txt"] == 0
assert Counter(loaded)["npc/NEED/need_doram_exchange.txt"] == 1
assert loaded.index("npc/NEED/need_doram_exchange.txt") < loaded.index("npc/NEED/shop.txt")
npc = read("npc/re/custom/lasagna/lasagna_npcs.txt")
helpers = read("npc/NEED/need_doram_exchange.txt")
shop = read("npc/NEED/shop.txt")
balanced(helpers)
balanced(shop)
# The archived Lasagna file is read only as historical quest inventory, never loaded.
active = "\n".join(l for l in npc.splitlines() if not l.lstrip().startswith("//"))
assert "515:-1,567:-1,11602:-1" in shop
stock = next(l for l in shop.splitlines() if l.startswith("-\tshop\t1기타잡템\t"))
assert "6417:" not in stock
assert 'callfunc "F_NEED_DoramMenu"' in shop
assert not re.search(r'^\w+,\d+,\d+,\d+\s+(?:script|duplicate)', helpers, re.M)
assert [items[i]["Buy"] for i in (515, 567, 11602)] == [15, 500, 15]
assert 'bSkillVariableCast,"SU_CN_METEOR",-1000' in items[28382]["Script"]
for i in [28380, 28381, 28382, *range(28413, 28425)]:
    assert not items[i].get("Refineable", False), i
    assert items[i]["Slots"] == 1
    assert items[i]["Jobs"]["Summoner"]
for i in range(5018, 5057):
    assert not skills[i].get("CopyFlags", {}).get("Skill", {}), i
assert skills[5040]["Requires"]["ItemCost"] == [{"Item": "Prawn", "Amount": 1}]
assert not skills[5028]["Requires"].get("ItemCost")
assert not skills[5036]["Requires"].get("ItemCost")
assert skills[5042]["Requires"]["ItemCost"] == [{"Item": "Nepeta_Cataria", "Amount": 1}]
assert skills[5043]["Requires"]["ItemCost"] == [{"Item": "Carrot", "Amount": 1}]
assert "28422,2, 28423,2, 28424,2" in helpers
assert "delitem .@your_charm_ID" not in active
assert "delitem .@item_data[.@i]" not in active
quest_ids = sorted(set(int(n) for n in re.findall(r'\b(?:setquest|changequest|completequest|erasequest)\s*(?:\(\s*)?(\d+)', active)))
assert all(i in quests for i in quest_ids), [i for i in quest_ids if i not in quests]
maps = sorted(set(re.findall(r'^(\w+),\d+,\d+,\d+\s+(?:script|duplicate)', active, re.M)))
missing_maps = [m for m in maps if not re.search(r'^map:\s*' + re.escape(m) + r'\s*$', read("conf/maps_athena.conf"), re.M)]
assert not missing_maps, missing_maps
starter_line = re.search(r'^start_items_doram:\s*(.*)$', read('conf/import/char_conf.txt'), re.M).group(1)
starter = [list(map(int, part.split(','))) for part in starter_line.split(':')]
assert len(starter) <= 32
assert [r for r in starter if r[2] == 2] == [[1681, 1, 2]]
assert items[1681]['Jobs']['Summoner'] and items[1681]['EquipLevelMin'] == 1
combos = [x for x in entries("db/item_combos.yml") if any(
    any(names.get(n, {}).get("Id") in equipment_ids for n in c["Combo"])
    for c in x["Combos"])]
tree = next(x for x in entries("db/skill_tree.yml") if x["Job"] == "Summoner")
data = {"commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "items": [items[i] for i in equipment_ids], "drops": routes,
        "skills": [skills[i] for i in range(5018, 5057)], "combos": combos,
        "tree": tree, "quest_ids": quest_ids, "maps": maps,
        "npc_count": len(re.findall(r'^.*\tscript\t', active, re.M)),
        "loaded_npcs": len(loaded)}
(OUT / "inventory.json").write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
print(f"PASS: YAML effective imports, {len(equipment_ids)} equipment, 39 skill definitions, 15 unrefineable accessories")
print(f"PASS: Lasagna unloaded, helpers before Old Payon shop, 3 stock prices, no 6417, all {len(quest_ids)} archived quest IDs retained")
print(f"PASS: active menu delimiter balance; {data['npc_count']} archived scripts inactive; {len(maps)} referenced maps retained; 28424 token route")
print("LIMIT: delimiter validation is not the rAthena script parser; in-game quest/transaction checks remain pending")

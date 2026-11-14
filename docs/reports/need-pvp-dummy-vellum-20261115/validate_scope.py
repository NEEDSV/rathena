"""Read-only data/scope smoke. Does not start a server, connect SQL, or parse NPC bytecode."""
from pathlib import Path
import json
import re
import struct
import subprocess
import zlib

ROOT = next(p for p in Path(__file__).resolve().parents if (p/'src/map/battle.cpp').is_file())
OUT = Path(__file__).resolve().parent
read = lambda f: (ROOT/f).read_bytes()
norm = lambda b: b.replace(b'\r\n', b'\n')
def unchanged(f):
    return norm(read(f)) == norm(subprocess.check_output(['git','show','HEAD:'+f], cwd=ROOT))

checks = {}
mob = read('db/re/mob_db.yml')
imported = read('db/import/mob_db.yml')
avail = read('db/import/mob_avail.yml')
npc = read('npc/NEED/need_pvp_dummy.txt').decode('cp949')
script = read('src/map/script.cpp').decode('utf-8')
checks['original_training_spawns_unchanged'] = unchanged('npc/re/mobs/tra_fild_123.txt')
checks['original_mob_database_unchanged'] = unchanged('db/re/mob_db.yml')
checks['original_item_database_unchanged'] = unchanged('db/re/item_db_equip.yml')
checks['new_id_only_one_db_definition'] = len(re.findall(rb'^  - Id: 31900\r?$', mob+imported, re.M)) == 1
entry = imported.split(b'  - Id: 31900\n')[1]
checks['no_rewards_in_db'] = all(v in entry for v in [b'BaseExp: 0',b'JobExp: 0',b'MvpExp: 0']) and b'Drops:' not in entry
checks['real_race_stays_formless'] = b'Race: Formless' in entry
checks['view_alias_reuses_existing_dummy'] = b'Mob: NEED_PVP_TEST_DUMMY' in avail and b'Sprite: S_DUMMY_100_MEDIUM' in avail
checks['loader_enabled_once'] = read('npc/scripts_need.conf').count(b'npc: npc/NEED/need_pvp_dummy.txt') == 1
commands = ['mes','needtr','next','select','unitexists','setunitdata','close','monster','strnpcinfo','end']
checks['npc_builtins_registered'] = all(re.search(r'BUILDIN_DEF(?:2)?\([^\n]*\b'+c+r'\b', script) for c in commands)
checks['npc_known_constant'] = b'export_constant(UMOB_HP);' in read('src/map/script_constants.hpp')
scrub = re.sub(r'"(?:[^"\\]|\\.)*"', '""', re.sub(r'//[^\r\n]*','',npc))
checks['npc_brackets_balanced'] = all(scrub.count(a)==scrub.count(b) for a,b in [('(',')'),('{','}')])
checks['single_file_reload_reuses_live_gid'] = 'unitexists($@NEED_PVP_DUMMY_GID)' in npc
cache = read('db/map_cache.dat')
pos, coords = 8, {}
for _ in range(struct.unpack_from('<H',cache,4)[0]):
    name, xs, ys, length = struct.unpack_from('<12shhi',cache,pos)
    if name.rstrip(b'\0') == b'hero_tra':
        cells = zlib.decompress(cache[pos+20:pos+20+length])
        coords = {f'{x},{y}': cells[y*xs+x] for x,y in [(95,144),(95,147)]}
        break
    pos += 20+length
checks['dummy_and_guide_cells_walkable'] = len(coords)==2 and all(v in (0,3) for v in coords.values())
battle = read('src/map/battle.cpp').decode('utf-8')
checks['only_scoped_race_optins'] = battle.count('need_pvp_dummy_attack_race(target, tstatus->race)') == 2
checks['no_global_status_or_mapflag_change'] = unchanged('src/map/status.cpp') and unchanged('src/map/map.cpp')
items = read('db/re/item_db_equip.yml').decode('cp949')
vellum = []
for match in re.finditer(r'^  - Id: (\d+)\r?\n    AegisName: (Velum_\w+)',items,re.M):
    end = items.find('\n  - Id: ',match.end())
    body = items[match.start():end if end>=0 else None]
    lines = body.splitlines()
    index = next((i for i,line in enumerate(lines) if 'Script: |' in line),None)
    vellum.append(dict(id=int(match[1]),aegis_name=match[2],line=items[:match.start()].count('\n')+1,
        script='\n'.join(lines[index+1:]) if index is not None else '',
        explicit_map_test=bool(re.search('getmapflag|MF_PVP|MF_GVG|mf_pvp|mf_gvg|battleground',body))))
checks['all_17_vellum_scripts_examined'] = len(vellum) == 17
checks['no_vellum_script_mapflag_requirement'] = not any(v['explicit_map_test'] for v in vellum)
result = dict(checks=checks,passed=sum(checks.values()),failed=sum(not v for v in checks.values()),
    cells=coords,vellum_items=vellum,comparison_note='Checkout CRLF normalized to LF.',
    limits=['NPC lexical/builtin checks only; no runtime parser/client.','No SQL/server connection.'])
(OUT/'data_scope_checks.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(f"DATA {result['passed']} passed, {result['failed']} failed")
raise SystemExit(0 if all(checks.values()) else 1)

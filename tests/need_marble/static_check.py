"""NEED marble board Phase B/C - static checks (G1). Reads files only; no server, no SQL.

Usage: python -I tests/need_marble/static_check.py <worktree root>
Exit code 0 = every check passed.
"""
import os
import re
import sys
from pathlib import Path

ROOT = Path(sys.argv[1] if len(sys.argv) > 1 else '.').resolve()
results = []


def check(name, ok, detail=''):
    results.append((name, bool(ok), detail))


def read(rel, enc='utf-8'):
    return (ROOT / rel).read_bytes().decode(enc)


logic = read('src/map/need_marble_logic.hpp')
cpp = read('src/map/need_marble.cpp')
hpp = read('src/map/need_marble.hpp')
sql = read('sql-files/need_marble_phase_bc_v1.sql')
rollback = read('sql-files/need_marble_phase_bc_v1_revert.sql')
npc_raw = (ROOT / 'npc/NEED/marble/need_marble_board_v2.txt').read_bytes()
v12 = read('npc/NEED/need_marble_motion_v1_2.txt')
inc = read('src/custom/script.inc')
inc_def = read('src/custom/script_def.inc')

# 1. NPC encoding and board tables
try:
    npc = npc_raw.decode('cp949')
    check('npc is CP949', True)
except UnicodeDecodeError as e:
    npc = npc_raw.decode('cp949', errors='replace')
    check('npc is CP949', False, str(e))


def setarray(text, name):
    m = re.search(r'setarray\s+' + re.escape(name) + r'\[0\],\s*(.*?);', text)
    if not m:
        return None
    return [x.strip().strip('"') for x in m.group(1).split(',')]


for arr in ('.x', '.y', '.tile$'):
    check(f'v2 {arr} equals v1.2 {arr}', setarray(npc, arr) == setarray(v12, arr))

enum = {m.group(1): int(m.group(2)) for m in re.finditer(r'\b(TILE_[A-Z]+)\s*=\s*(\d+)', logic)}
board_block = re.search(r'BOARD\s*=\s*\{(.*?)\};', logic, re.S).group(1)
board = [enum[t] for t in re.findall(r'\b(TILE_[A-Z]+)\b', board_block)]
npc_kind = [int(x) for x in setarray(npc, '.kind')]
check('NPC .kind equals logic BOARD', npc_kind == board, f'{npc_kind} vs {board}')
names = setarray(v12, '.tile$')
kind_by_name = {'START': 'TILE_START', 'Event': 'TILE_EVENT', 'Poring': 'TILE_PORING', 'Treasure': 'TILE_TREASURE',
                'Warp': 'TILE_WARP', 'BONUS': 'TILE_BONUS', 'Monster': 'TILE_MONSTER', 'Tax': 'TILE_TAX',
                'BOSS': 'TILE_BOSS', 'Dungeon': 'TILE_DUNGEON', 'JAIL': 'TILE_JAIL'}
expected = [enum[kind_by_name.get(n, 'TILE_CITY')] for n in names]
check('logic BOARD matches v1.2 tile names', board == expected)
check('NPC tile_ko$ has 33 names', len(setarray(npc, '.tile_ko$') or []) == 33)

# 2. Policy keys used by the NPC
keys = {m.group(1): int(m.group(2)) for m in re.finditer(r'\b(P_[A-Z0-9_]+)\s*=\s*(\d+)', logic)}
for k, name in ((56, 'P_DUNGEON_SAFE_CP'), (57, 'P_DUNGEON_CHALLENGE_RATE'), (58, 'P_DUNGEON_CHALLENGE_CP')):
    check(f'NPC need_marble_policy({k}) is {name}', keys.get(name) == k and f'need_marble_policy({k})' in npc)

# 3. Schema: same table list as the C++ check, InnoDB, numeric primary keys only
tables = re.findall(r'CREATE TABLE IF NOT EXISTS `(\w+)` \((.*?)\) ENGINE=(\w+);', sql, re.S)
cpp_tables = re.findall(r'"(need_marble_\w+)"', re.search(r'LEDGER_TABLES\[\] = \{(.*?)\};', cpp, re.S).group(1))
check('schema tables == C++ LEDGER_TABLES', sorted(t[0] for t in tables) == sorted(cpp_tables),
      f'{len(tables)} vs {len(cpp_tables)}')
check('every table InnoDB', all(t[2] == 'InnoDB' for t in tables))
for name, body, _ in tables:
    cols = dict(re.findall(r'^\s*`(\w+)`\s+([A-Z]+)', body, re.M))
    pk = re.search(r'PRIMARY KEY \(([^)]*)\)', body).group(1)
    pk_cols = re.findall(r'`(\w+)`', pk)
    check(f'{name}: numeric primary key', all(cols.get(c) in ('INT', 'BIGINT', 'SMALLINT', 'TINYINT') for c in pk_cols),
          str({c: cols.get(c) for c in pk_cols}))
check('rollback drops exactly the created tables',
      sorted(re.findall(r'DROP TABLE IF EXISTS `(\w+)`', rollback)) == sorted(t[0] for t in tables))
check('schema alters no existing table', not re.search(r'\bALTER\s+TABLE\b|\bDROP\b', sql, re.I))
check('hunt batch key includes period_day',
      'PRIMARY KEY (`account_id`, `collect_session`, `batch_no`, `period_day`)' in sql)
check('dungeon daily eligibility key',
      'PRIMARY KEY (`account_id`, `season_id`, `period_day`, `dungeon_code`)' in sql)
check('next_tier is SMALLINT', re.search(r'`next_tier` SMALLINT', sql) is not None)
check('master switch seeded OFF', re.search(r"VALUES \(1, 0, 0, 'migration'", sql) is not None)

# 4. No cash point / registry write path anywhere in the marble code
def strip_comments(text):
    return re.sub(r'//[^\n]*', '', text)


for rel, text in (('need_marble.cpp', cpp), ('need_marble.hpp', hpp), ('board_v2 NPC', npc)):
    check(f'{rel}: no CASHPOINTS / pc_getcash / pc_paycash in code',
          not re.search(r'CASHPOINT|cashPoints|pc_getcash|pc_paycash|SP_CASHPOINTS|acc_reg_num', strip_comments(text)))
check('cp_payout rows only inserted with status 1 (DECIDED)',
      re.search(r"INSERT INTO `need_marble_cp_payout`.*?'1','%u',NOW\(\)", cpp, re.S) is not None and
      not re.search(r'UPDATE `need_marble_cp_payout`', cpp))
check('CP apply switch must be 0 for a valid policy', "if (v[P_CP_APPLY_ENABLED] != 0)" in logic)

# 5. Transaction ownership: START/COMMIT/ROLLBACK only inside ledger_tx
for word in ('START TRANSACTION', 'COMMIT', 'ROLLBACK'):
    n = len(re.findall(r'"' + word + r'"', cpp))
    check(f'"{word}" literal appears once (ledger_tx)', n == 1, str(n))
check('nested transaction refused', 'nested transaction refused' in cpp)
in_tx_funcs = re.findall(r'^int32 (\w+_in_tx)\(', cpp, re.M)
for f in in_tx_funcs:
    body = cpp[cpp.index('int32 ' + f + '('):]
    body = body[:body.index('\n}\n')]
    check(f'{f} requires an open transaction and opens none',
          'MARBLE_REQUIRE_TX();' in body and 'ledger_tx' not in body)

# 6. Script command table
funcs = set(re.findall(r'BUILDIN_FUNC\((need_marble_\w+)\)', inc))
defs = set(re.findall(r'BUILDIN_DEF\((need_marble_\w+),', inc_def))
check('every marble BUILDIN_DEF has a body', funcs == defs and len(funcs) == 8, f'{sorted(funcs ^ defs)}')
used = set(re.findall(r'\b(need_marble_\w+)\s*\(', npc))
check('NPC only calls defined marble commands', used <= defs, str(sorted(used - defs)))

# 7. Not auto-loaded, GM v1.2 untouched
load_list = read('npc/scripts_need.conf') + read('npc/re/scripts_main.conf')
check('main load lists do not load the marble v2 NPC', 'NEED/marble/' not in load_list and 'board_v2' not in load_list)
check('v2 does not bind the GM commands', not re.search(r'bindatcmd\s+"mb(roll|dice|status|reset)"', npc))
check('v2 binds @marble only', re.findall(r'bindatcmd\s+"(\w+)"', npc) == ['marble'])

# 8. No hard-coded secrets / hosts / absolute paths in new files
new_files = ['src/map/need_marble.cpp', 'src/map/need_marble.hpp', 'src/map/need_marble_logic.hpp',
             'sql-files/need_marble_phase_bc_v1.sql', 'sql-files/need_marble_phase_bc_v1_revert.sql',
             'npc/NEED/marble/scripts_marble.conf', 'tests/need_marble/logic_test.cpp']
pattern = re.compile(r'[A-Za-z]:[\\/]|password|passwd|\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b|ragnarok\.|@ndream|github\.com', re.I)
for rel in new_files:
    text = read(rel)
    hits = [m.group(0) for m in pattern.finditer(text)]
    check(f'{rel}: no host / password / absolute path', not hits, str(hits[:3]))
check('board_v2 NPC: no host / password / absolute path', not pattern.search(npc))

# 9. G-1(b) test hooks never reach the repository sources or build files
leak = []
for base, _, fs in os.walk(ROOT / 'src'):
    for fn in fs:
        if fn.endswith(('.cpp', '.hpp', '.inc', '.vcxproj', '.filters', '.txt', '.in')):
            t = (Path(base) / fn).read_bytes()
            if b'need_marble_test' in t or b'NEED_MARBLE_TEST_HOOKS' in t:
                leak.append(str(Path(base, fn).relative_to(ROOT)))
check('src/ has no G-1(b) test hook code', not leak, str(leak[:5]))
build_files = [p for p in ('CMakeLists.txt', 'configure', 'configure.ac', 'Makefile.in', 'rAthena.sln') if (ROOT / p).exists()]
check('build files never define NEED_MARBLE_TEST_HOOKS',
      not any(b'NEED_MARBLE_TEST_HOOKS' in (ROOT / p).read_bytes() for p in build_files), str(build_files))

failed = [r for r in results if not r[1]]
for name, ok, detail in results:
    print(('PASS ' if ok else 'FAIL ') + name + (f'  [{detail}]' if detail and not ok else ''))
print(f'static_check: {len(results) - len(failed)} passed, {len(failed)} failed')
sys.exit(1 if failed else 0)

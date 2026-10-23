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

# 10. F1: marble hunt hook runs before the experience share, with the same final-kill test as `rebirth`
mob = (ROOT / 'src/map/mob.cpp').read_bytes().decode('utf-8', 'replace')
mob_lines = mob.splitlines()
hook_at = [i for i, l in enumerate(mob_lines) if 'need_marble_hunt_on_kill(' in l]
exp_at = [i for i, l in enumerate(mob_lines) if l.strip().startswith('if(!(type&2) &&')]
check('mob.cpp calls the marble hunt hook exactly once', len(hook_at) == 1, str(hook_at))
check('marble hunt hook is before the experience share block', hook_at and exp_at and hook_at[0] < exp_at[0], f'{hook_at} vs {exp_at}')
reb = re.search(r'rebirth =\s*\((.*?)\);', mob)
fin = re.search(r'need_marble_final_kill = !\((.*?)\);', mob)
norm = lambda s: re.sub(r'\s+', '', s)
check('marble final-kill test equals the rebirth expression', reb and fin and norm(reb.group(1)) == norm(fin.group(1)),
      f'{reb.group(1) if reb else None} / {fin.group(1) if fin else None}')
rebirth_at = [i for i, l in enumerate(mob_lines) if l.strip().startswith('rebirth =')]
other = [i for i, l in enumerate(mob_lines) if 'need_summer_hunt_on_kill(' in l or 'need_chuseok_hunt_on_kill(' in l]
check('summer / chuseok hooks stay after the rebirth test (unchanged rule)', len(other) == 2 and all(i > rebirth_at[0] for i in other), str(other))

# 11. F2: the checkpoint locks the progress row before writing a batch row
cp = cpp[cpp.index('int32 hunt_checkpoint('):]
cp = cp[:cp.index('\n}\n')]
batch_insert = cp.find('INSERT IGNORE INTO `need_marble_hunt_batch`')
progress_lock = cp.find("FROM `need_marble_hunt_progress` WHERE `account_id`='%u' AND `season_id`='%u' AND `period_day`='%u' FOR UPDATE")
check('hunt_checkpoint: progress FOR UPDATE before batch INSERT',
      -1 < progress_lock < batch_insert and 'hunt_day_maxed(next_tier, max_tiers)' in cp)

# 11b. V21 (G2-B2): a used batch key is judged before anything of the batch is written (find(): a missing piece is a FAIL, not a crash)
pre = cp.find('SELECT `period_day`,`kills` FROM `need_marble_hunt_batch`')
first_write = cp.find('INSERT IGNORE INTO `need_marble_hunt_progress`')
judge = cp.find('hunt_resend_check(stored, batch, capped)')
loop = cp.find('const uint32 day = entry.first, kills = entry.second;')
check('hunt_checkpoint: resend check (stored rows + hunt_resend_check) before any write of the batch',
      -1 < pre < judge < loop and -1 < first_write < batch_insert and judge < first_write)
check('hunt_checkpoint: a key taken during the batch is a CONFLICT, never ALREADY',
      'return NEED_MARBLE_CONFLICT;  // the key was free' in cp and 'return NEED_MARBLE_ALREADY;   // the whole batch' not in cp)
check('NEED_MARBLE_CONFLICT is a new code (-21), REJECTED keeps -20', 'NEED_MARBLE_CONFLICT = -21' in hpp and 'NEED_MARBLE_REJECTED = -20' in hpp)
fl_at = cpp.find('void hunt_flush(')
fl = cpp[fl_at:cpp.find('\n}\n', fl_at)] if fl_at >= 0 else ''
check('hunt_flush: CONFLICT stops hunt credit for the login and keeps the batch unsent',
      'if (b.conflict)\n\t\treturn;' in fl and 'rc == NEED_MARBLE_CONFLICT' in fl and 'b.conflict = true;' in fl and
      'b.done_day == day || b.conflict' in cpp)
check('resend rule is the pure function in need_marble_logic.hpp (unit-tested)', 'inline int32_t hunt_resend_check(' in logic)
check('hunt_on_kill: one capped-day lookup per login and day', 'b.checked_day != day' in cpp and 'hunt_day_maxed_in_db(aid, day)' in cpp)

# 12. Kill-path order inside mob_dead after F1: marble -> exp share -> rebirth test -> macro hunt -> summer -> chuseok
dead_at = next(i for i, l in enumerate(mob_lines) if l.startswith('int32 mob_dead('))
dead_end = next(i for i in range(dead_at + 1, len(mob_lines)) if mob_lines[i] == '}')
first = lambda pat: next((i for i in range(dead_at, dead_end) if pat in mob_lines[i]), None)
order = [('marble', first('need_marble_hunt_on_kill(')), ('exp share', first('if(!(type&2) &&')), ('gainexp', first('pc_gainexp(')),
         ('rebirth', first('rebirth =')), ('macro hunt', first('pc_need_macro_hunt_on_mob_kill(')),
         ('summer', first('need_summer_hunt_on_kill(')), ('chuseok', first('need_chuseok_hunt_on_kill('))]
check('mob_dead kill-path order: marble, exp, rebirth, macro, summer, chuseok',
      all(i is not None for _, i in order) and [i for _, i in order] == sorted(i for _, i in order), str(order))

# 13. V22 runner (follow-up): expected rows from a snapshot on dedicated accounts, never absolute counts
runner = read('tests/need_marble/g2/bc_runner.py')
v22 = runner[runner.index('def cmd_v22('):]
v22 = v22[:v22.index('\ndef ', 1)]
check('V22 runner: no absolute kills / batch expectation', not re.search(r'==\s*400\b|\bb1\s*==\s*20\b|== \[\[\'0\', \'100\'\]', v22))
check('V22 runner: compares through v22_model with a snapshot', 'v22_fixture(' in v22 and 'v22_model.compare(' in runner and 'v22_snapshot(' in runner)
blocks = runner.split('\ndef ')[1:]
users = [b.split('(', 1)[0] for b in blocks if re.search(r"'v22[ab]'", b)]
check('V22 dedicated accounts used by V22 only', set(users) <= {'cmd_v22', 'v22_fixture'} and 'cmd_v22' in users, str(users))
hook = read('tests/need_marble/g2/test_hooks/need_marble_test.cpp')
feed = hook[hook.index('int32 need_marble_test_hunt_feed(map_session_data* sd, int32 count) {'):]
check('test feed refuses a dead caller and reports counted', 'pc_isdead(sd)' in feed and '@MBQA_COUNTED' in feed)
extras = read('tests/need_marble/g2/bc_stack_extras.py')
check('harness prints counted= and binds @mbqastate', 'counted=' in extras and '"mbqastate"' in extras)
synth = read('tests/need_marble/g2/bc_synth_v22.sql')
check('bc_synth_v22.sql: no literal secret, ids outside the other fixtures',
      "'{{user}}'" in synth and not re.search(r'password|[A-Za-z]:[\\/]', synth, re.I) and '2000906' not in synth)

# 14. G2-A runners (V11, V18-C): dedicated accounts, isolated-only captcha settings, no secrets
v11_users = [b.split('(', 1)[0] for b in blocks if re.search(r"'v11'", b)]
check('V11 dedicated account used by V11 only', v11_users == ['cmd_v11'], str(v11_users))
heal_users = [b.split('(', 1)[0] for b in blocks if re.search(r"'heal'", b)]
check('V18-C healer used by V18-C only', heal_users == ['cmd_v18c'], str(heal_users))
g2a = read('tests/need_marble/g2/bc_synth_g2a.sql')
check('bc_synth_g2a.sql: no literal secret, new ids only for new accounts',
      "'{{user}}'" in g2a and not re.search(r'password|[A-Za-z]:[\\/]', g2a, re.I) and
      re.findall(r'^\s*\((\d{7}),', g2a, re.M) == ['2000909', '2000910'])
repo_force = re.search(r'^need_macro_hunt_force_kill_count:\s*(\d+)', read('conf/battle/client.conf'), re.M)
check('captcha settings only through the stack copy (bc_stack_extras.py --captcha), repo value stays 0',
      'need_macro_hunt_force_kill_count: 5' in extras and "os.path.join(S, 'conf', 'import', 'battle_conf.txt')" in extras and
      repo_force is not None and repo_force.group(1) == '0')
v11m = read('tests/need_marble/g2/v11_model.py')
check('V11 analysis reads the policy from need_marble_logic.hpp (no copied rate table)', 'policy_from_source' in v11m and '5000, 4000' not in v11m)

# 15. G2-B (V16 / V21 / V24): scratch-only patch anchors, fault triggers, accounts, time zone independence
apply = read('tests/need_marble/g2/test_hooks/apply_test_hooks.py')
anchors = re.findall(r"replace_once\('src/map/need_marble\.cpp', '((?:[^'\\]|\\.)*)'", apply)
anchor_counts = [cpp.count(bytes(x, 'utf-8').decode('unicode_escape')) for x in anchors]
check('G2-B patch anchors each match need_marble.cpp exactly once', len(anchors) == 3 and anchor_counts == [1, 1, 1], str(anchor_counts))
g2b_inc = read('tests/need_marble/g2/test_hooks/need_marble_test_g2b.inc')
inc_body = g2b_inc.strip()
check('G2-B hook code is entirely inside #ifdef NEED_MARBLE_TEST_HOOKS', inc_body.startswith('#ifdef NEED_MARBLE_TEST_HOOKS') and
      inc_body.endswith('#endif  // NEED_MARBLE_TEST_HOOKS') and inc_body.count('#ifdef') == 1)
check('G2-B anchored edits are inside #ifdef NEED_MARBLE_TEST_HOOKS', apply.count("'#ifdef NEED_MARBLE_TEST_HOOKS\\n'") == 3)
faults = read('tests/need_marble/g2/bc_faults_g2b.sql')
trig = re.findall(r'CREATE TRIGGER `(\w+)` (BEFORE|AFTER) (\w+) ON `(\w+)`', faults)
check('fault triggers: plain UPDATE only, one per fault point, armed per account',
      len(trig) == 4 and all(t[1] == 'BEFORE' and t[2] == 'UPDATE' for t in trig) and faults.count('`account_id` = NEW.`account_id`') == 4,
      str(trig))
check('fault SQL never drops / alters / deletes ledger data',
      not re.search(r'\b(DROP|ALTER|TRUNCATE|DELETE|UPDATE\s+`need_marble)', re.sub(r'BEFORE UPDATE ON|--[^\n]*', '', faults)))
g2b = read('tests/need_marble/g2/bc_synth_g2b.sql')
check('bc_synth_g2b.sql: no literal secret, new ids only', "'{{user}}'" in g2b and not re.search(r'password|[A-Za-z]:[\\/]', g2b, re.I) and
      re.findall(r'^\s*\((\d{7}),', g2b, re.M) == ['2000911', '2000912', '2000913', '2000914', '2000915'])
for acc, cmds in (("'v16'", {'cmd_v16a', 'cmd_v16b', 'cmd_v16c'}), ("'v21'", {'cmd_v21a', 'cmd_v21b', 'cmd_v21c'}), ("'v24'", {'cmd_v24'})):
    users = {b.split('(', 1)[0] for b in blocks if acc in b}
    check(f'G2-B account {acc} used by its own scenario only', users == cmds, str(sorted(users)))
check('marble logical day ignores the OS time zone (no localtime / gmtime / mktime in marble code)',
      not re.search(r'\b(localtime|gmtime|mktime|strftime)\s*\(', strip_comments(cpp) + strip_comments(logic)))
check('runner never starts / stops a process for G2-B (restarts are operator steps)',
      not re.search(r'subprocess|bc_servers\.ps1\s*-|os\.kill|taskkill', runner.split('# ---------------------------------------------------------------------------- G2-B')[1]))

failed = [r for r in results if not r[1]]
for name, ok, detail in results:
    print(('PASS ' if ok else 'FAIL ') + name + (f'  [{detail}]' if detail and not ok else ''))
print(f'static_check: {len(results) - len(failed)} passed, {len(failed)} failed')
sys.exit(1 if failed else 0)

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


def cpp_func(name, ret='int32'):
    at = cpp.find('\n' + ret + ' ' + name + '(')
    return cpp[at:cpp.find('\n}\n', at)] if at >= 0 else ''


char_result_fn = cpp_func('need_marble_cp_char_result', 'void')
cpp_rest = cpp.replace(char_result_fn, '') if char_result_fn else cpp
for rel, text in (('need_marble.cpp outside need_marble_cp_char_result', cpp_rest), ('need_marble.hpp', hpp), ('board_v2 NPC', npc)):
    check(f'{rel}: no CASHPOINTS / pc_getcash / pc_paycash in code',
          not re.search(r'CASHPOINT|cashPoints|pc_getcash|pc_paycash|SP_CASHPOINTS|acc_reg_num|pc_setaccountreg', strip_comments(text)))
check('V20-REAL: the only #CASHPOINTS code of the module mirrors a char-server APPLIED answer into the login that sent it',
      bool(char_result_fn) and re.findall(r'pc_setaccountreg\(sd, add_str\(CASHPOINT_VAR\), cash\)', char_result_fn) != [] and
      char_result_fn.count('pc_setaccountreg(') == 1 and
      char_result_fn.find('if (sd != nullptr && written == CPW_APPLIED) {') < char_result_fn.find('pc_setaccountreg(') and
      'if (sd != nullptr && !cp_same_login(sd, f->second))\n\t\t\tsd = nullptr;' in char_result_fn and
      not re.search(r'pc_getcash|pc_paycash|acc_reg_num', strip_comments(char_result_fn)))


apply_fn, resolve_fn = cpp_func('need_marble_cp_apply'), cpp_func('need_marble_cp_resolve')
updates = [m.start() for m in re.finditer(r'UPDATE `need_marble_cp_payout`', cpp)]
settle_fn = cpp_func('cp_settle')
void_result_fn = cpp_func('need_marble_cp_void_result', 'void')
check('cp_payout rows only inserted with status 1 (DECIDED); status changed only by need_marble_cp_apply (claim), cp_settle (answer), '
      'need_marble_cp_resolve, need_marble_cp_void_result (MANUAL_VOID after the char-server tombstone)',
      re.search(r"INSERT INTO `need_marble_cp_payout`.*?'1','%u',NOW\(\)", cpp, re.S) is not None and len(updates) == 4 and
      void_result_fn.count('UPDATE `need_marble_cp_payout`') == 1 and
      apply_fn.count('UPDATE `need_marble_cp_payout`') == 1 and settle_fn.count('UPDATE `need_marble_cp_payout`') == 1 and
      resolve_fn.count('UPDATE `need_marble_cp_payout`') == 1)
check('CP switch: 1 is valid only with a registered writer, anything else stays invalid',
      "if (v[P_CP_APPLY_ENABLED] != 0 && !(cp_writer && v[P_CP_APPLY_ENABLED] == 1))" in logic and
      'validate_policy(v, &bad_key, g_cp_writer != nullptr)' in cpp)

# 15. V20 prep: CP apply state machine (mock writer in the test build only)
writer_calls = [m.start() for m in re.finditer(r'need_marble_cp_set_writer\(', cpp + hpp + inc + inc_def)]
macro_blocks = re.findall(r'#if defined\(NEED_MARBLE_CP_CHAR_WRITER\) && NEED_MARBLE_CP_CHAR_WRITER\n(.*?)\n#endif', cpp, re.S)
check('the repository registers only the char writer, only in a NEED_MARBLE_CP_CHAR_WRITER build (default build: no writer)',
      len(writer_calls) == 3 and 'g_cp_writer = writer;' in cpp and 'need_marble_cp_writer g_cp_writer = nullptr;' in cpp and
      len(macro_blocks) == 2 and any('need_marble_cp_set_writer(&cp_char_writer, true);' in b for b in macro_blocks) and
      any(b.lstrip().startswith('// V20-REAL: hands one payout') and 'int32 cp_char_writer(' in b for b in macro_blocks) and
      cpp.count('cp_char_writer') == sum(b.count('cp_char_writer') for b in macro_blocks))
check('need_marble_cp_apply: DISABLED without writer / audit table / key 5 = 1, before anything is read',
      'if (!ready() || !g_cp_audit_ok || g_cp_writer == nullptr || P(P_CP_APPLY_ENABLED) != 1)\n\t\treturn NEED_MARBLE_DISABLED;' in apply_fn)
claim = apply_fn.find("CPS_UNCERTAIN, aid, payout_no, CPS_DECIDED")
check('need_marble_cp_apply: DECIDED -> UNCERTAIN committed (claim audit) before the writer runs, final record in a second transaction',
      -1 < claim < apply_fn.find('cp_audit_in_tx(aid, payout_no, CPS_DECIDED, CPS_UNCERTAIN, CPR_CLAIM') < apply_fn.find('if (!tx.commit())') <
      apply_fn.find('g_cp_writer(aid, cid, payout_no, amount)') < apply_fn.find('cp_settle(aid, cid, payout_no, amount, written, false)') and
      apply_fn.count('g_cp_writer(') == 1 and settle_fn.count('ledger_tx tx;') == 1)
check('need_marble_cp_apply: only DECIDED reaches the writer (UNCERTAIN -> -22, APPLIED / MANUAL_APPLIED -> ALREADY, VOID -> BAD_STATE)',
      'if (status == CPS_APPLIED || status == CPS_MANUAL_APPLIED)\n\t\t\treturn NEED_MARBLE_ALREADY;' in apply_fn and
      'if (status != CPS_DECIDED)\n\t\t\treturn status == CPS_UNCERTAIN ? NEED_MARBLE_UNCERTAIN : NEED_MARBLE_BAD_STATE;' in apply_fn)
check('writer answer -> status only through cp_status_after_write (unknown / pending -> UNCERTAIN), UNCERTAIN back to DECIDED only on CPW_NO_EFFECT',
      'const int32 to = cp_status_after_write(written);' in settle_fn and 'return CPS_UNCERTAIN;' in logic and
      'if (writer_result == CPW_NO_EFFECT)\n\t\treturn CPS_DECIDED;' in logic)
check('need_marble_cp_resolve: UNCERTAIN -> MANUAL_APPLIED / MANUAL_VOID only, ticket required, audited',
      '!cp_resolvable(CPS_UNCERTAIN, to_status)' in resolve_fn and 'ticket == 0' in resolve_fn and 'if (status != CPS_UNCERTAIN)' in resolve_fn and
      'cp_audit_in_tx(account_id, payout_no, CPS_UNCERTAIN, to_status, CPR_OPERATOR' in resolve_fn and
      'return from_status == CPS_UNCERTAIN && (to_status == CPS_MANUAL_APPLIED || to_status == CPS_MANUAL_VOID);' in logic)
check('need_marble_cp_audit is not a ledger table: the module keeps working where the V20 migration is not applied',
      'need_marble_cp_audit' not in cpp[cpp.find('LEDGER_TABLES[]'):cpp.find('};', cpp.find('LEDGER_TABLES[]'))] and
      'cp_table_check("need_marble_cp_audit", "operator_aid")' in cpp and 'need_marble_cp_grant' not in cpp[cpp.find('LEDGER_TABLES[]'):cpp.find('};', cpp.find('LEDGER_TABLES[]'))])
hooks_v20 = read('tests/need_marble/g2/test_hooks/need_marble_test_v20.inc')
check('mock CP writer lives only in the test hooks, under NEED_MARBLE_TEST_HOOKS, and refuses outside ragnarok_mbqa',
      hooks_v20.startswith('#ifdef NEED_MARBLE_TEST_HOOKS') and hooks_v20.rstrip().endswith('#endif  // NEED_MARBLE_TEST_HOOKS') and
      'strcmp(db, "ragnarok_mbqa") == 0' in hooks_v20 and 'need_marble_cp_set_writer(&need_marble_test_cp_writer)' in hooks_v20 and
      'need_marble_test_cp_writer' not in cpp + inc + inc_def and not re.search(r'CASHPOINT|pc_getcash|acc_reg_num', strip_comments(hooks_v20)))

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
g2b_block = apply[apply.find('if a.g2b:'):apply.find('INC_V20 = ')]
check('G2-B anchored edits are inside #ifdef NEED_MARBLE_TEST_HOOKS', g2b_block.count("'#ifdef NEED_MARBLE_TEST_HOOKS\\n'") == 3)
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
      not re.search(r'subprocess|bc_servers\.ps1\s*-|os\.kill|taskkill',
                    runner.split('# ---------------------------------------------------------------------------- G2-B')[1].split('# ' + '-' * 100 + ' V20-REAL')[0]))

# 13. G2-C v1.1: one finished battleground match = BG points per account, credited from the reward functions
bg_common = read('npc/battleground/bg_common.txt', 'cp949')
kvm = read('npc/battleground/kvm/kvm01.txt', 'cp949')
old_users = []
for base, _, fs in os.walk(ROOT / 'src'):
    for fn in fs:
        if fn.endswith(('.cpp', '.hpp', '.inc')) and b'need_marble_bg_match' in (Path(base) / fn).read_bytes():
            old_users.append(fn)
for base, _, fs in os.walk(ROOT / 'npc'):
    for fn in fs:
        if fn.endswith('.txt') and b'need_marble_bg_match' in (Path(base) / fn).read_bytes():
            old_users.append(fn)
check('need_marble_bg_match (map-wide sample ratio) is gone from src/ and npc/', not old_users, str(old_users))


def func_body(text, name):
    at = text.find('function\tscript\t' + name + '\t{')
    return text[at:text.find('\n}', at)] if at >= 0 else ''


badge = func_body(bg_common, 'F_BG_Badge')
grant = func_body(kvm, 'F_KvM_GrantReward')
hook_badge = 'if ($@BG_RM_CYCLE_ACTIVE)\r\n\t\tneed_marble_bg_credit(.@bg_type$, $@BG_RM_TOKEN);'
hook_kvm = 'if ($@BG_RM_CYCLE_ACTIVE)\r\n\t\tneed_marble_bg_credit("KVM", $@BG_RM_TOKEN);'
check('F_BG_Badge credits the marble once the match type is known, before close2 waits for the player (random match cycle only)',
      badge.count('need_marble_bg_credit(') == 1 and hook_badge in badge and
      -1 < badge.find('else\r\n\t\treturn;') < badge.find(hook_badge) < badge.find('if (getarg(0) == 1)') < badge.find('close2;'))
auto_rb = func_body(bg_common, 'F_BG_RM_AutoRewardBadge')
check('F_BG_RM_AutoRewardBadge credits every member on the map before the reward loop (G2-C M1)',
      auto_rb.count('need_marble_bg_credit(.@bg_type$, $@BG_RM_TOKEN);') == 1 and
      -1 < auto_rb.find('if (!$@BG_RM_CYCLE_ACTIVE)') < auto_rb.find('need_marble_bg_credit(') < auto_rb.find('F_BG_RM_TryClaimReward') <
      auto_rb.find('callfunc "F_BG_Badge"'))
check('F_KvM_GrantReward credits the marble after the existing rewards (random match cycle only)',
      grant.count('need_marble_bg_credit(') == 1 and hook_kvm in grant and grant.find('F_NEED_Chuseok_BG_Reward') < grant.find(hook_kvm))
other_npc = []
for base, _, fs in os.walk(ROOT / 'npc'):
    for fn in fs:
        if fn.endswith('.txt') and b'need_marble_bg_credit' in (Path(base) / fn).read_bytes():
            other_npc.append(fn)
check('need_marble_bg_credit is called only from bg_common.txt (F_BG_Badge, F_BG_RM_AutoRewardBadge) and kvm01.txt',
      sorted(other_npc) == ['bg_common.txt', 'kvm01.txt'] and bg_common.count('need_marble_bg_credit(') == 2, str(other_npc))
check('script command need_marble_bg_credit is defined as "si"',
      'BUILDIN_DEF(need_marble_bg_credit,"si"),' in inc_def and 'BUILDIN_FUNC(need_marble_bg_credit)' in inc)
bgc_at = cpp.find('int32 need_marble_bg_credit(')
bgc = cpp[bgc_at:cpp.find('\n}\n', bgc_at)] if bgc_at >= 0 else ''
check('need_marble_bg_credit: switch off -> DISABLED, AFK rule from need_marble_logic.hpp, key (boot, token, type)',
      'P(P_BG_ENABLED) != 1)\n\t\treturn NEED_MARBLE_DISABLED;' in bgc and 'bg_afk_excluded(' in bgc and
      'bg_credit(sd, boot, match_token, type_code)' in bgc and 'inline bool bg_afk_excluded(' in logic)
check('G2-C adds no participation / kill / heal count rule', 'P_BG_MIN_SAMPLES' not in bgc and 'P_BG_MIN_ACTIVE_PERMIL' not in bgc and
      'kill' not in bgc.lower() and 'heal' not in bgc.lower())

# 14. BG auto reward fix: the auto reward never waits for a player (one AFK member held back the rest of the team)
DIALOG_CMDS = ('mes ', 'close', 'next;', 'select(', 'input ', 'menu ')
gate = badge.find('\tif (!getarg(3,0)) {')
gate_end = badge.find('\r\n\t}\r\n', gate)
in_gate = lambda s: all(gate < m.start() < gate_end for m in re.finditer(re.escape(s), badge))
check('F_BG_Badge: every mes / close2 sits inside the dialog block skipped by the auto reward (getarg(3) = 1)',
      gate > 0 and badge.count('close2;') == 1 and badge.count('mes ') > 0 and in_gate('mes ') and in_gate('close2;'))
check('F_BG_Badge: amounts, the loser BG point and every getitem are outside the dialog block (auto and NPC pay the same)',
      -1 < badge.find('set #BATTLEGROUND_POINT, #BATTLEGROUND_POINT + 3;') < gate and
      all(m.start() > gate_end for m in re.finditer('getitem ', badge)) and badge.count('getitem ') == 3)
auto_kvm = func_body(kvm, 'F_KvM_AutoRewardRandom')
check('F_BG_RM_AutoRewardBadge calls F_BG_Badge without the dialog, and has no dialog command itself',
      auto_rb.count('callfunc "F_BG_Badge", .@win, .@team$, .@bg_type$, 1;') == 1 and auto_rb.count('F_BG_Badge"') == 1 and
      not any(c in strip_comments(auto_rb) for c in DIALOG_CMDS))
check('KVM auto reward path (F_KvM_AutoRewardRandom -> F_KvM_GrantReward) has no dialog command (left unchanged)',
      bool(auto_kvm) and not any(c in strip_comments(auto_kvm) + strip_comments(grant) for c in DIALOG_CMDS))
manual = []
for rel in ('npc/battleground/tierra/tierra01.txt', 'npc/battleground/flavius/flavius01.txt'):
    manual += re.findall(r'callfunc "F_BG_Badge",[^;]*;', read(rel, 'cp949'))
check('reward NPCs (tierra01 / flavius01) still call F_BG_Badge with 3 arguments: dialog shown as before',
      len(manual) == 16 and all(re.fullmatch(r'callfunc "F_BG_Badge",[01],"(Guillaume|Croix)","(Tierra|Flavius)";', m) for m in manual), str(len(manual)))

# 16. V20-REAL: char-server grant (idempotency row + #CASHPOINTS in one transaction), conditional saves, gates
grant_h = read('src/common/need_marble_cp_grant.hpp')
inter_c = read('src/char/inter.cpp').replace('\r\n', '\n')
intif_c = read('src/map/intif.cpp').replace('\r\n', '\n')
chrif_c = read('src/map/chrif.cpp').replace('\r\n', '\n')
mig = read('sql-files/need_marble_v20_real_cp_grant.sql')
num = lambda name: int(re.search(name + r' = (0x[0-9A-Fa-f]+|\d+);', grant_h).group(1), 0)
row3000 = re.search(r'\n\t(-1,-1, 7,-1, -1,13,36, \(2\+4\+4\+4\+NAME_LENGTH\),[^\n]*)', inter_c).group(1).split(',')
row3800 = re.search(r'\n\t(-1,-1,27,-1, -1, 0,37,-1, 10\+NAME_LENGTH,[^\n]*)', intif_c).group(1).split(',')
check('V20-REAL packets: 0x300A / 0x300B / 0x380A free before, lengths in the shared header = inter / intif length tables',
      num('PKT_GRANT') == 0x300A and num('PKT_CONDSAVE') == 0x300B and num('PKT_GRANT_ACK') == 0x380A and
      int(row3000[10]) == num('PKT_GRANT_LEN') == 30 and int(row3000[11]) == num('PKT_CONDSAVE_LEN') == 34 and
      int(row3800[10]) == num('PKT_GRANT_ACK_LEN') == 35 and 'need_marble_cp::PKT_GRANT: mapif_parse_need_cp_grant(fd)' in inter_c and
      'need_marble_cp::PKT_CONDSAVE: mapif_parse_need_cp_condsave(fd)' in inter_c and 'case need_marble_cp::PKT_GRANT_ACK:' in intif_c)
gapply = inter_c[inter_c.find('static uint8 need_cp_grant_apply('):inter_c.find('\n}\n', inter_c.find('static uint8 need_cp_grant_apply('))]
writes = re.findall(r'"(REPLACE INTO|INSERT INTO|UPDATE|DELETE)[^"]*', gapply)
check('char grant: switch off by default, ready only with the switch and need_marble_cp_grant + acc_reg_num both InnoDB',
      'static bool need_cp_grant_enabled = false;' in inter_c and 'need_cp_grant_enabled = config_switch(w2) != 0;' in inter_c and
      'need_cp_grant_ready = need_cp_grant_enabled && need_cp_grant_table && accreg_innodb;' in inter_c and
      gapply.find('if (!need_cp_grant_ready)\n\t\treturn GR_DISABLED;') < gapply.find('START TRANSACTION'))
check('char grant: one transaction, both writes guarded by @need_cp_tx (silent reconnect), idempotency row read FOR UPDATE first',
      gapply.count('"START TRANSACTION"') == 1 and gapply.count('"COMMIT"') == 1 and len(writes) == 2 and
      gapply.count("WHERE @need_cp_tx='%\" PRIu64 \"'") == 2 and
      gapply.find('FROM `need_marble_cp_grant` WHERE') < gapply.find('FROM `%s` WHERE `account_id`') < gapply.find('REPLACE INTO') <
      gapply.find('INSERT INTO `need_marble_cp_grant`') < gapply.find('"COMMIT"'))
check('char grant: APPLIED only after COMMIT and a read-back of this request\'s row; same key -> ALREADY / CONFLICT with no write',
      gapply.rfind('return GR_APPLIED;') > gapply.find('AND `req_id`=') > gapply.find('"COMMIT"') and
      'result = stored == amount ? GR_ALREADY : GR_CONFLICT;' in gapply and 'grant_new_balance(old_balance, amount, &new_balance)' in gapply)
cond = inter_c[inter_c.find('static void mapif_parse_need_cp_condsave('):inter_c.find('\n}\n', inter_c.find('static void mapif_parse_need_cp_condsave('))]
check('char conditional save: adds the grant only when the row of this very request exists; lookup error writes nothing',
      'AND `req_id`=' in cond and 'condsave_value(base, amount > 0, static_cast<int32>(amount))' in cond and
      cond.find('return;') < cond.find('inter_savereg(') and 'inter_savereg(aid, cid, need_marble_cp::CASHPOINT_KEY, 0, value, nullptr, false);' in cond)
sreg = intif_c[intif_c.find('int32 intif_saveregistry(map_session_data *sd)'):intif_c.find('\n}\n', intif_c.find('int32 intif_saveregistry(map_session_data *sd)'))]
check('map registry save: while a grant of this login is unanswered #CASHPOINTS leaves 0x3004 and goes as a conditional save after it',
      'const bool cp_fence = need_marble_cp_fence(sd, &cp_payout, &cp_req);' in sreg and
      'if( cp_fence && !src->type && script_getvaridx(key.i64) == 0 && strcmp(varname, CASHPOINT_VAR) == 0 ) {' in sreg and
      sreg.rfind('WFIFOSET(inter_fd, plen);') < sreg.find('intif_need_cp_condsave(') and sreg.count('intif_need_cp_condsave(') == 1)
map_src = ''
for fn in sorted(os.listdir(ROOT / 'src' / 'map')):
    if fn.endswith(('.cpp', '.hpp')):
        map_src += (ROOT / 'src' / 'map' / fn).read_bytes().decode('utf-8', 'replace')
check('map-server never writes acc_reg_num itself (no direct UPDATE / REPLACE of the char registry table)',
      not re.search(r'(UPDATE|REPLACE INTO|INSERT INTO)\s+`?acc_reg_num', map_src))
fence_fn = cpp_func('need_marble_cp_fence', 'bool')
lost_fn = cpp_func('need_marble_cp_char_lost', 'void')
kick_fn = cpp_func('cp_kick', 'void')
check('fence = same login (char_id + login ids), not only the account; lost connection clears awaited answers, keeps the fences',
      'if (it == g_cp_inflight.end() || !cp_same_login(sd, it->second))' in fence_fn and
      'return sd->status.char_id == f.char_id && sd->login_id1 == f.login1 && sd->login_id2 == f.login2;' in cpp and
      'g_cp_sent.clear();' in lost_fn and 'g_cp_inflight' not in lost_fn.replace('// g_cp_inflight stays', '') and
      'need_marble_cp_char_lost(); // NEED marble V20-REAL' in chrif_c[chrif_c.find('void chrif_on_disconnect(void) {'):])
check('automatic payout picks only DECIDED rows within GRANT_MAX, one request per login, only with key 5 = 1 and the char writer',
      "`status`='%d' AND `amount`<='%d'" in kick_fn and 'CPS_DECIDED, need_marble_cp::GRANT_MAX' in kick_fn and
      '!g_cp_auto' in kick_fn and 'P(P_CP_APPLY_ENABLED) != 1' in kick_fn and 'cp_busy(sd)' in kick_fn and 'CPS_UNCERTAIN' not in kick_fn)
check('operator resolve: the char-server row decides (APPLIED needs its applied row, VOID refused while one exists), BUSY while an answer may still come',
      'if ((to_status == CPS_MANUAL_APPLIED && !applied) || (to_status == CPS_MANUAL_VOID && state != 0 && !voided)) {' in resolve_fn and
      'DIFF_TICK(gettick(), sent->second.sent) < CP_ANSWER_WAIT_SEC * 1000' in resolve_fn and 'constexpr int32_t CP_ANSWER_WAIT_SEC = 60;' in logic and
      'if (!g_cp_grant_visible && g_cp_auto)\n\t\treturn NEED_MARBLE_DISABLED;' in resolve_fn)
check('char answer: only a request this map-server sent (key, req_id, amount) is recorded, and only on a row still UNCERTAIN',
      'sent->second.req_id != req_id || sent->second.amount != amount' in char_result_fn and
      'if (status != CPS_UNCERTAIN || stored != amount) {' in settle_fn and 'cp_write_from_grant(result)' in char_result_fn)
check('migration: acc_reg_num -> InnoDB, need_marble_cp_grant InnoDB keyed (account_id, payout_no), rollback written down',
      'ALTER TABLE `acc_reg_num` ENGINE=InnoDB;' in mig and 'PRIMARY KEY (`account_id`, `payout_no`)' in mig and
      mig.count('ENGINE=InnoDB') == 2 and 'Rollback: DROP TABLE `need_marble_cp_grant`;' in mig)
check('policy key 5 (CP switch) still defaults to 0', 'v[P_CP_APPLY_ENABLED] = 0;' in logic)
hooks_real = read('tests/need_marble/g2/test_hooks/need_marble_test_v20real.inc')
hooks_char = read('tests/need_marble/g2/test_hooks/need_marble_test_char.inc')
real_block = apply[apply.find('if a.v20real:'):apply.find("append('src/custom/script.inc', INC)")]
check('V20-REAL test hooks: only in tests/, entirely under NEED_MARBLE_TEST_HOOKS, every anchored edit too; char faults only in ragnarok_mbqa',
      all(h.strip().startswith('#ifdef NEED_MARBLE_TEST_HOOKS') and h.strip().endswith('#endif  // NEED_MARBLE_TEST_HOOKS') for h in (hooks_real, hooks_char)) and
      real_block.count("replace_once(") == 10 and real_block.count("'#ifdef NEED_MARBLE_TEST_HOOKS\\n'") == 11 and
      "DATABASE()='ragnarok_mbqa'" in hooks_char and 'getenv("NEED_MARBLE_TEST_HOOKS")' in hooks_char and
      'need_cp_test' not in inter_c and 'need_marble_test' not in cpp + intif_c + chrif_c)

# 17. V20 safety: a late request can never pay after an operator VOID; GM @mbcp
vapply = inter_c[inter_c.find('static uint8 need_cp_void_apply('):inter_c.find('\n}\n', inter_c.find('static uint8 need_cp_void_apply('))]
check('V20 safety packets: 0x300C (30) / 0x380B (19) were free and match the shared header',
      num('PKT_VOID') == 0x300C and num('PKT_VOID_ACK') == 0x380B and int(row3000[12]) == num('PKT_VOID_LEN') == 30 and
      int(row3800[11]) == num('PKT_VOID_ACK_LEN') == 19 and 'need_marble_cp::PKT_VOID: mapif_parse_need_cp_void(fd)' in inter_c and
      'case need_marble_cp::PKT_VOID_ACK:' in intif_c)
check('char grant: a VOID tombstone of the key refuses the request before any write (GR_VOIDED)',
      gapply.find('if (state == GRANT_STATE_VOID)\n\t\t\t\tresult = GR_VOIDED;') > 0 and
      gapply.find('if (state == GRANT_STATE_VOID)') < gapply.find('REPLACE INTO') and "`state`='%d'\"," in gapply.replace("`state`='%d'\",\n", "`state`='%d'\","))
check('char VOID: same key lock (FOR UPDATE), an applied row refuses it, tombstone write guarded by @need_cp_tx, committed then read back',
      'FOR UPDATE' in vapply and 'result = VR_APPLIED_EXISTS;' in vapply and vapply.count("WHERE @need_cp_tx='%\" PRIu64 \"'") == 1 and
      vapply.find('"COMMIT"') < vapply.rfind('return VR_VOIDED;') and 'GRANT_STATE_VOID, amount) != 1)\n\t\treturn VR_DB_ERROR;' in vapply and
      'if (!need_cp_grant_table)\n\t\treturn VR_DISABLED;' in vapply)
check('char conditional save counts only an applied row of that very request (a tombstone never adds)',
      "AND `req_id`='%\" PRIu64 \"' AND `state`='%d'\",\n\t\t\taid, payout_no, req_id, need_marble_cp::GRANT_STATE_APPLIED)" in cond)
void_branch = resolve_fn[resolve_fn.find('if (to_status == CPS_MANUAL_VOID && state == 0) {'):resolve_fn.find('// MANUAL_APPLIED on its applied row')]
check('map VOID without a row: only a request to the char-server (audit 10), never a ledger change; no char-server -> NO_CHAR, nothing kept',
      'intif_need_cp_void(' in void_branch and 'UPDATE' not in void_branch and 'CPR_VOID_SENT' in void_branch and
      'return NEED_MARBLE_NO_CHAR;' in void_branch and void_branch.find('return NEED_MARBLE_NO_CHAR;') < void_branch.find('tx.commit();'))
check('MANUAL_VOID from a char answer only on VR_VOIDED of an UNCERTAIN row with the same amount; anything else audited, unchanged',
      'if (result == need_marble_cp::VR_VOIDED && status == CPS_UNCERTAIN && stored == amount) {' in void_result_fn and
      'const int32 reason = status == CPS_UNCERTAIN ? CPR_OPERATOR_REFUSED : CPR_LATE_ANSWER;' in void_result_fn and
      'pc_setaccountreg' not in void_result_fn and 'intif_need_cp_grant' not in void_result_fn)
check('late char answers on a closed row are audited (reason 9) and change nothing',
      'CPR_LATE_ANSWER, amount, cid, 0) == NEED_MARBLE_OK)' in settle_fn and 'CPR_LATE_ANSWER = 9,' in logic)
gm_fn = cpp_func('need_marble_cp_gm_command')
info_fn = cpp_func('cp_gm_info', 'void')
check('GM @mbcp: level 99 checked before anything (groups.yml all_commands also reaches level 0), registered as a custom atcommand',
      gm_fn.find('if (pc_get_group_level(sd) < GM_FORCE_LEVEL) {') < gm_fn.find('sscanf(') and gm_fn.find('sscanf(') > 0 and
      'ACMD_FUNC(mbcp)' in read('src/custom/atcommand.inc') and 'ACMD_DEF(mbcp),' in read('src/custom/atcommand_def.inc'))
check('GM @mbcp info is read only (no INSERT / UPDATE / DELETE, no CP), resolve needs a ticket and goes through need_marble_cp_resolve',
      bool(info_fn) and not re.search(r'INSERT|UPDATE|DELETE|pc_setaccountreg|intif_need', info_fn) and
      'need_marble_cp_resolve(aid, payout_no, to, ticket, sd)' in gm_fn and 'ticket != 0' in gm_fn)
check('migration drafts carry the new columns (grant state / operator, audit operator_aid)',
      '`state` TINYINT UNSIGNED NOT NULL DEFAULT 1' in mig and '`operator_aid` INT UNSIGNED NULL' in mig and
      '`operator_aid` INT UNSIGNED NULL' in read('sql-files/need_marble_v20_cp_apply.sql'))

failed = [r for r in results if not r[1]]
for name, ok, detail in results:
    print(('PASS ' if ok else 'FAIL ') + name + (f'  [{detail}]' if detail and not ok else ''))
print(f'static_check: {len(results) - len(failed)} passed, {len(failed)} failed')
sys.exit(1 if failed else 0)

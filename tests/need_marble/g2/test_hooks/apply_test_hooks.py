"""G-1(b): applies the TEST BUILD ONLY kill injection to a SCRATCH COPY of the sources (never the repository).

usage: python -I apply_test_hooks.py --src <scratch source copy root>

- copies need_marble_test.cpp into <src>/src/map/
- appends #ifdef NEED_MARBLE_TEST_HOOKS blocks to <src>/src/custom/script.inc and script_def.inc
- registers need_marble_test.cpp in <src>/src/map/map-server.vcxproj
- --g2b: anchored edits of <src>/src/map/need_marble.cpp (test clock in today(), hidden COMMIT outcome in
  ledger_tx::commit) and appends need_marble_test_g2b.inc; every edit sits under #ifdef NEED_MARBLE_TEST_HOOKS
Refuses to run on a path that is a git worktree of the repository (a .git entry at the root).
"""
import argparse
import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument('--src', required=True)
ap.add_argument('--g2b', action='store_true', help='G2-B: test clock, hidden COMMIT outcome, re-entry probes (need_marble.cpp scratch copy)')
a = ap.parse_args()
R = os.path.abspath(a.src)
if os.path.exists(os.path.join(R, '.git')):
    raise SystemExit(f'{R} is a git checkout - apply the test hooks only to a scratch copy')

shutil.copy2(os.path.join(HERE, 'need_marble_test.cpp'), os.path.join(R, 'src', 'map', 'need_marble_test.cpp'))

INC = '''
#ifdef NEED_MARBLE_TEST_HOOKS
// G-1(b) TEST BUILD ONLY (tests/need_marble/g2/test_hooks). Not in the repository sources.
int32 need_marble_test_kill(map_session_data* sd, int32 radius, int32 max_count);
int32 need_marble_test_hunt_feed(map_session_data* sd, int32 count);

/// need_marble_test_kill(<radius, 0 = whole map>,<max count>);
BUILDIN_FUNC(need_marble_test_kill)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, -1);
		return SCRIPT_CMD_SUCCESS;
	}
	script_pushint(st, need_marble_test_kill(sd, script_getnum(st, 2), script_getnum(st, 3)));
	return SCRIPT_CMD_SUCCESS;
}

/// need_marble_test_hunt_feed(<count>);
BUILDIN_FUNC(need_marble_test_hunt_feed)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, -1);
		return SCRIPT_CMD_SUCCESS;
	}
	script_pushint(st, need_marble_test_hunt_feed(sd, script_getnum(st, 2)));
	return SCRIPT_CMD_SUCCESS;
}

int32 need_marble_test_kill_as(map_session_data* sd, const char* name, int32 radius, int32 max_count);
int32 need_marble_test_view_of(map_session_data* sd, const char* name);

/// need_marble_test_kill_as("<char name>",<radius>,<max count>);  (G2-A: damage source = that character)
BUILDIN_FUNC(need_marble_test_kill_as)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, -1);
		return SCRIPT_CMD_SUCCESS;
	}
	script_pushint(st, need_marble_test_kill_as(sd, script_getstr(st, 2), script_getnum(st, 3), script_getnum(st, 4)));
	return SCRIPT_CMD_SUCCESS;
}

/// need_marble_test_view_of("<char name>");  (G2-A: that character's marble kill count today)
BUILDIN_FUNC(need_marble_test_view_of)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd)) {
		script_pushint(st, -1);
		return SCRIPT_CMD_SUCCESS;
	}
	script_pushint(st, need_marble_test_view_of(sd, script_getstr(st, 2)));
	return SCRIPT_CMD_SUCCESS;
}
#endif
'''
DEF = '''
#ifdef NEED_MARBLE_TEST_HOOKS
BUILDIN_DEF(need_marble_test_kill,"ii"),
BUILDIN_DEF(need_marble_test_hunt_feed,"i"),
BUILDIN_DEF(need_marble_test_kill_as,"sii"),
BUILDIN_DEF(need_marble_test_view_of,"s"),
#endif
'''


def append(rel, text):
    p = os.path.join(R, rel)
    raw = open(p, 'rb').read()
    if b'NEED_MARBLE_TEST_HOOKS' in raw:
        raise SystemExit(f'{rel}: already patched')
    eol = b'\r\n' if b'\r\n' in raw else b'\n'
    if not raw.endswith(b'\n'):
        raw += eol
    open(p, 'wb').write(raw + text.replace('\n', eol.decode()).encode())
    print('patched', rel)


INC_G2B = '''
#ifdef NEED_MARBLE_TEST_HOOKS
// G2-B TEST BUILD ONLY: defined in the scratch copy of need_marble.cpp (need_marble_test_g2b.inc).
bool need_marble_test_allowed(map_session_data* sd);
uint32 need_marble_test_set_clock(int64 offset);
void need_marble_test_arm_commit_fault(int32 mode);
int32 need_marble_test_replay(map_session_data* sd, uint64 session, uint32 batch_no, const char* spec, int32& credited);
int32 need_marble_test_reentry(map_session_data* sd);
int32 need_marble_test_out_of_tx(map_session_data* sd);
void need_marble_test_flags(map_session_data* sd);

/// need_marble_test_clock(<unix time the server clock should read now>) -> logical day then, -1 when refused
BUILDIN_FUNC(need_marble_test_clock)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd) || !need_marble_test_allowed(sd)) {
		script_pushint(st, -1);
		return SCRIPT_CMD_SUCCESS;
	}
	script_pushint(st, need_marble_test_set_clock(static_cast<int64>(script_getnum(st, 2))));
	return SCRIPT_CMD_SUCCESS;
}

/// need_marble_test_commit_fault(<mode 0|1|2>) -> mode, -1 when refused
BUILDIN_FUNC(need_marble_test_commit_fault)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd) || !need_marble_test_allowed(sd)) {
		script_pushint(st, -1);
		return SCRIPT_CMD_SUCCESS;
	}
	need_marble_test_arm_commit_fault(script_getnum(st, 2));
	script_pushint(st, script_getnum(st, 2));
	return SCRIPT_CMD_SUCCESS;
}

/// need_marble_test_replay(<collect session>,<batch no>,"<day:kills,...>") -> entry point result; @MBQA_CREDITED
BUILDIN_FUNC(need_marble_test_replay)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd) || !need_marble_test_allowed(sd)) {
		script_pushint(st, -1);
		return SCRIPT_CMD_SUCCESS;
	}
	int32 credited = 0;
	const int32 rc = need_marble_test_replay(sd, static_cast<uint64>(script_getnum(st, 2)), static_cast<uint32>(script_getnum(st, 3)),
		script_getstr(st, 4), credited);
	pc_setreg(sd, add_str("@MBQA_CREDITED"), credited);
	script_pushint(st, rc);
	return SCRIPT_CMD_SUCCESS;
}

/// need_marble_test_reentry() / need_marble_test_out_of_tx() -> ledger result of the refused call
BUILDIN_FUNC(need_marble_test_reentry)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd) || !need_marble_test_allowed(sd)) {
		script_pushint(st, -1);
		return SCRIPT_CMD_SUCCESS;
	}
	script_pushint(st, need_marble_test_reentry(sd));
	return SCRIPT_CMD_SUCCESS;
}

BUILDIN_FUNC(need_marble_test_out_of_tx)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd) || !need_marble_test_allowed(sd)) {
		script_pushint(st, -1);
		return SCRIPT_CMD_SUCCESS;
	}
	script_pushint(st, need_marble_test_out_of_tx(sd));
	return SCRIPT_CMD_SUCCESS;
}

/// need_marble_test_flags() -> 0; @MBQA_IN_TX @MBQA_FAULT @MBQA_DAY @MBQA_POL_DAY @MBQA_SESSION @MBQA_BATCH @MBQA_FROZEN @MBQA_PENDING @MBQA_PENDING_DAYS
BUILDIN_FUNC(need_marble_test_flags)
{
	map_session_data* sd = nullptr;
	if (!script_rid2sd(sd) || !need_marble_test_allowed(sd)) {
		script_pushint(st, -1);
		return SCRIPT_CMD_SUCCESS;
	}
	need_marble_test_flags(sd);
	script_pushint(st, 0);
	return SCRIPT_CMD_SUCCESS;
}
#endif
'''
DEF_G2B = '''
#ifdef NEED_MARBLE_TEST_HOOKS
BUILDIN_DEF(need_marble_test_clock,"i"),
BUILDIN_DEF(need_marble_test_commit_fault,"i"),
BUILDIN_DEF(need_marble_test_replay,"iis"),
BUILDIN_DEF(need_marble_test_reentry,""),
BUILDIN_DEF(need_marble_test_out_of_tx,""),
BUILDIN_DEF(need_marble_test_flags,""),
#endif
'''


def replace_once(rel, old, new):
    """Anchored edit of the scratch copy: the anchor must match exactly once, or nothing is written."""
    p = os.path.join(R, rel)
    raw = open(p, 'rb').read()
    eol = b'\r\n' if b'\r\n' in raw else b'\n'
    o, n = old.replace('\n', eol.decode()).encode(), new.replace('\n', eol.decode()).encode()
    if raw.count(o) != 1:
        raise SystemExit(f'{rel}: anchor found {raw.count(o)} times - not patched: {old.strip()[:60]!r}')
    open(p, 'wb').write(raw.replace(o, n, 1))
    print('patched', rel, '-', old.strip().splitlines()[0][:60])


if a.g2b:
    # V21 / V24 test clock, V16 / V21 hidden COMMIT outcome, V16 re-entry / out-of-transaction probes
    replace_once('src/map/need_marble.cpp', 'using namespace need_marble;\n\nnamespace {\n',
                 'using namespace need_marble;\n\n'
                 '#ifdef NEED_MARBLE_TEST_HOOKS\n'
                 'int64 need_marble_test_clock_offset();           // G2-B test build only (need_marble_test_g2b.inc)\n'
                 'bool need_marble_test_commit_fault(Sql* handle);\n'
                 '#endif\n\nnamespace {\n')
    replace_once('src/map/need_marble.cpp', '\treturn logical_day(static_cast<int64_t>(time(nullptr)));\n',
                 '#ifdef NEED_MARBLE_TEST_HOOKS\n'
                 '\treturn logical_day(static_cast<int64_t>(time(nullptr)) + need_marble_test_clock_offset());\n'
                 '#else\n'
                 '\treturn logical_day(static_cast<int64_t>(time(nullptr)));\n'
                 '#endif\n')
    replace_once('src/map/need_marble.cpp', '\t\tif (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {\n',
                 '#ifdef NEED_MARBLE_TEST_HOOKS\n'
                 '\t\tif (need_marble_test_commit_fault(mmysql_handle)) {\n'
                 '\t\t\tfault("COMMIT failed, outcome unknown");\n'
                 '\t\t\treturn false;\n'
                 '\t\t}\n'
                 '#endif\n'
                 '\t\tif (SQL_ERROR == Sql_QueryStr(mmysql_handle, "COMMIT")) {\n')
    p = os.path.join(R, 'src', 'map', 'need_marble.cpp')
    raw = open(p, 'rb').read()
    eol = b'\r\n' if b'\r\n' in raw else b'\n'
    add = open(os.path.join(HERE, 'need_marble_test_g2b.inc'), 'rb').read().replace(b'\r\n', b'\n').replace(b'\n', eol)
    open(p, 'wb').write(raw.rstrip(b'\r\n') + eol + add)
    print('patched src/map/need_marble.cpp - appended need_marble_test_g2b.inc')
    INC += INC_G2B
    DEF += DEF_G2B

append('src/custom/script.inc', INC)
append('src/custom/script_def.inc', DEF)

vp = os.path.join(R, 'src', 'map', 'map-server.vcxproj')
raw = open(vp, 'rb').read()
anchor = b'<ClCompile Include="need_marble.cpp" />'
assert raw.count(anchor) == 1
eol = b'\r\n' if b'\r\n' in raw else b'\n'
raw = raw.replace(anchor, anchor + eol + b'    <ClCompile Include="need_marble_test.cpp" />', 1)
open(vp, 'wb').write(raw)
print('patched src/map/map-server.vcxproj')

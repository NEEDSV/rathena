"""G-1(b): applies the TEST BUILD ONLY kill injection to a SCRATCH COPY of the sources (never the repository).

usage: python -I apply_test_hooks.py --src <scratch source copy root>

- copies need_marble_test.cpp into <src>/src/map/
- appends #ifdef NEED_MARBLE_TEST_HOOKS blocks to <src>/src/custom/script.inc and script_def.inc
- registers need_marble_test.cpp in <src>/src/map/map-server.vcxproj
Refuses to run on a path that is a git worktree of the repository (a .git entry at the root).
"""
import argparse
import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument('--src', required=True)
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
#endif
'''
DEF = '''
#ifdef NEED_MARBLE_TEST_HOOKS
BUILDIN_DEF(need_marble_test_kill,"ii"),
BUILDIN_DEF(need_marble_test_hunt_feed,"i"),
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

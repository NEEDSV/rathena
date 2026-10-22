"""G2: stack-copy-only additions (approved D1 / E-2 / G-2a). Writes only inside <out>/stack.

usage: python -I bc_stack_extras.py --out <scratch>/mbqa_bc --orig <original rathena tree (read only)>

D1  : `map: need_marbl` (conf/maps_athena.conf), `need_marbl` (db/map_index.txt) and the need_marbl
      entry of db/re/map_cache.dat, copied byte for byte from the original working tree (read only).
E-2 : npc/scripts_custom.conf loads npc/NEED/marble/need_marble_board_v2.txt.
G-2a: npc/custom/_mbqa_harness.txt - GM-only test commands that create a grade A instance for the
      caller and call need_marble_instance_clear() on its boss map (ledger path only).
"""
import argparse
import hashlib
import os
import struct

ap = argparse.ArgumentParser()
ap.add_argument('--out', required=True)
ap.add_argument('--orig', required=True)
ap.add_argument('--harness-only', action='store_true', help='rewrite only npc/custom/_mbqa_harness.txt')
ap.add_argument('--kill-hooks', action='store_true', help='G-1(b): add @mbqakill / @mbqafeed (test build only)')
ap.add_argument('--bind-local', action='store_true', help='add bind_ip: 127.0.0.1 to the login/char/map import conf')
ap.add_argument('--captcha', action='store_true', help='G2-A V18-C: hunt captcha at the 5th kill (stack battle import conf only)')
a = ap.parse_args()
S = os.path.join(os.path.abspath(a.out), 'stack')
O = os.path.abspath(a.orig)
assert os.path.isdir(S), S
report = []


def insert_after_line(path, anchor_line, new_line):
    raw = open(path, 'rb').read()
    eol = b'\r\n' if b'\r\n' in raw else b'\n'
    lines = raw.split(eol)
    if new_line.encode() in lines:
        raise SystemExit(f'{path}: already contains {new_line}')
    idx = [i for i, l in enumerate(lines) if l == anchor_line.encode()]
    if len(idx) != 1:
        raise SystemExit(f'{path}: anchor {anchor_line!r} found {len(idx)} times')
    lines.insert(idx[0] + 1, new_line.encode())
    open(path, 'wb').write(eol.join(lines))
    report.append(f'{os.path.relpath(path, S)}: +{new_line!r} after {anchor_line!r}')


def cache_entries(data):
    size, count = struct.unpack_from('<IH', data, 0)
    assert size == len(data), (size, len(data))
    off, out = 8, []
    for _ in range(count):
        name = data[off:off + 12].split(b'\0')[0].decode('latin1')
        xs, ys, ln = struct.unpack_from('<hhi', data, off + 12)
        out.append((name, data[off:off + 20 + ln]))
        off += 20 + ln
    assert off == len(data)
    return out


def apply_d1_e2():
    # D1
    insert_after_line(os.path.join(S, 'conf', 'maps_athena.conf'), 'map: pa2_in02', 'map: need_marbl')
    insert_after_line(os.path.join(S, 'db', 'map_index.txt'), 'pa2_in02', 'need_marbl')
    orig = cache_entries(open(os.path.join(O, 'db', 're', 'map_cache.dat'), 'rb').read())
    cp = os.path.join(S, 'db', 're', 'map_cache.dat')
    mine = cache_entries(open(cp, 'rb').read())
    src = [e for n, e in orig if n == 'need_marbl']
    assert len(src) == 1 and all(n != 'need_marbl' for n, _ in mine)
    body = b''.join(e for _, e in mine) + src[0]
    data = struct.pack('<IH', 8 + len(body), len(mine) + 1) + b'\0\0' + body
    open(cp, 'wb').write(data)
    report.append(f'db/re/map_cache.dat: {len(mine)} -> {len(mine) + 1} maps, +need_marbl entry sha1 '
                  f'{hashlib.sha1(src[0]).hexdigest()[:12]} ({len(src[0])} bytes)')
    # E-2
    sc = os.path.join(S, 'npc', 'scripts_custom.conf')
    raw = open(sc, 'rb').read()
    eol = b'\r\n' if b'\r\n' in raw else b'\n'
    add = (eol + b'// --- G2 isolated test only (bc_stack_extras.py) ---' + eol +
           b'npc: npc/NEED/marble/need_marble_board_v2.txt' + eol +
           b'npc: npc/custom/_mbqa_harness.txt' + eol)
    open(sc, 'wb').write(raw.rstrip(b'\r\n') + add)
    report.append('npc/scripts_custom.conf: + need_marble_board_v2.txt, + _mbqa_harness.txt')


def bind_local():
    for f in ('login_conf.txt', 'char_conf.txt', 'map_conf.txt'):
        p = os.path.join(S, 'conf', 'import', f)
        raw = open(p, 'rb').read()
        if b'bind_ip:' in raw:
            raise SystemExit(f'{f}: bind_ip already present')
        if not raw.endswith(b'\n'):
            raw += b'\r\n'      # these files end without a newline: never glue onto the last setting
        open(p, 'wb').write(raw + b'bind_ip: 127.0.0.1\r\n')
        report.append(f'conf/import/{f}: + bind_ip: 127.0.0.1')


def captcha_conf():
    """V18-C: the NEED hunt captcha fires on the 5th kill instead of at random (rate 0); the 1 h cooldown
    after a trigger (need_macro_hunt_delay) is left as configured."""
    p = os.path.join(S, 'conf', 'import', 'battle_conf.txt')
    raw = open(p, 'rb').read() if os.path.exists(p) else b''
    if b'need_macro_hunt_force_kill_count' in raw:
        raise SystemExit('battle_conf.txt: captcha settings already present')
    if raw and not raw.endswith(b'\n'):
        raw += b'\r\n'
    add = (b'// --- G2-A V18-C isolated test only (bc_stack_extras.py --captcha) ---\r\n'
           b'need_macro_hunt_force_kill_count: 5\r\nneed_macro_hunt_check_rate: 0\r\n'
           b'need_macro_hunt_debug: yes\r\nmacro_checker_debug: yes\r\n')
    open(p, 'wb').write(raw + add)
    report.append('conf/import/battle_conf.txt: + need_macro_hunt_force_kill_count 5, check_rate 0, debug on')


if not a.harness_only:
    apply_d1_e2()
    if a.bind_local:
        bind_local()
    if a.captcha:
        captcha_conf()


def instance_name(rel, needle):
    """The CP949 instance name exactly as the dungeon script passes it to instance_create."""
    text = open(os.path.join(S, rel), 'rb').read()
    i = text.index(needle)
    j = text.index(b'"', i + len(needle))
    return text[i + len(needle):j]


lastroom = instance_name('npc/NEED/instances/LastRoom.txt', b'instance_create("')
centrallab = instance_name('npc/NEED/instances/CentralLaboratory.txt', b'instance_create("')
harness = b'\r\n'.join([
    b'//===== G2 isolated test harness (stack copy only, never in the repository load lists) =====',
    b'// @mbqainst <1|2> creates a solo (IM_CHAR) instance and enters it: 1 = LASTROOM 1@uns, 2 = CENTRALLAB 1@lab.',
    b'// @mbqaclear <1|2> calls need_marble_instance_clear() for the boss map of the caller\'s instance.',
    b'// @mbqaleave destroys the caller\'s instance and warps to prontera.',
    b'-\tscript\tMBQA_Harness\t-1,{',
    b'OnInit:',
    b'\tsetarray .key$[1], "LASTROOM", "CENTRALLAB";',
    b'\tsetarray .name$[1], "' + lastroom + b'", "' + centrallab + b'";',
    b'\tsetarray .map$[1], "1@uns", "1@lab";',
    b'\tbindatcmd "mbqainst", strnpcinfo(3) + "::OnInst", 99, 100;',
    b'\tbindatcmd "mbqaclear", strnpcinfo(3) + "::OnClear", 99, 100;',
    b'\tbindatcmd "mbqaleave", strnpcinfo(3) + "::OnLeave", 99, 100;',
    b'\tend;',
    b'OnInst:',
    b'\t.@k = atoi(.@atcmd_parameters$[0]);',
    b'\tif (.@k < 1 || .@k > 2) end;',
    b'\t.@id = instance_create(.name$[.@k], IM_CHAR);',
    b'\tif (.@id > 0) @MBQA_IID = .@id;',
    b'\t.@e = instance_enter(.name$[.@k], -1, -1, getcharid(0), @MBQA_IID);',
    b'\tdispbottom "[MBQA] inst k=" + .@k + " create=" + .@id + " iid=" + @MBQA_IID + " enter=" + .@e + " map=" + instance_mapname(.map$[.@k], @MBQA_IID);',
    b'\tend;',
    b'OnClear:',
    b'\t.@k = atoi(.@atcmd_parameters$[0]);',
    b'\tif (.@k < 1 || .@k > 2) end;',
    b'\t.@m$ = instance_mapname(.map$[.@k], @MBQA_IID);',
    b'\t.@n = need_marble_instance_clear(.key$[.@k], .@m$);',
    b'\tdispbottom "[MBQA] clear k=" + .@k + " map=" + .@m$ + " iid=" + @MBQA_IID + " credited=" + .@n;',
    b'\tend;',
    b'OnLeave:',
    b'\tif (@MBQA_IID > 0) instance_destroy @MBQA_IID;',
    b'\t@MBQA_IID = 0;',
    b'\twarp "prontera", 156, 191;',
    b'\tend;',
    b'}',
    b''])
if a.kill_hooks:
    # G-1(b): needs the TEST BUILD map-server (need_marble_test_* commands exist only there)
    harness += b'\r\n'.join([
        b'// G-1(b) test build only: @mbqakill <radius 0=map> <max>, @mbqafeed <count>, @mbqastate',
        b'// n=-2: the caller is dead (refused). counted = growth of the marble count over the feed.',
        b'-\tscript\tMBQA_KillHarness\t-1,{',
        b'OnInit:',
        b'\tbindatcmd "mbqakill", strnpcinfo(3) + "::OnKill", 99, 100;',
        b'\tbindatcmd "mbqafeed", strnpcinfo(3) + "::OnFeed", 99, 100;',
        b'\tbindatcmd "mbqastate", strnpcinfo(3) + "::OnState", 99, 100;',
        b'\tbindatcmd "mbqakillas", strnpcinfo(3) + "::OnKillAs", 99, 100;',
        b'\tbindatcmd "mbqaviewof", strnpcinfo(3) + "::OnViewOf", 99, 100;',
        b'\tend;',
        b'// G2-A V18-C: @mbqakillas <char> <radius> <max>, @mbqaviewof <char> (a captcha blocks that character\'s own commands)',
        b'OnKillAs:',
        b'\t.@n = need_marble_test_kill_as(.@atcmd_parameters$[0], atoi(.@atcmd_parameters$[1]), atoi(.@atcmd_parameters$[2]));',
        b'\tdispbottom "[MBQA] killas n=" + .@n + " counted=" + @MBQA_COUNTED;',
        b'\tend;',
        b'OnViewOf:',
        b'\tdispbottom "[MBQA] viewof " + .@atcmd_parameters$[0] + " kills=" + need_marble_test_view_of(.@atcmd_parameters$[0]);',
        b'\tend;',
        b'OnKill:',
        b'\t.@n = need_marble_test_kill(atoi(.@atcmd_parameters$[0]), atoi(.@atcmd_parameters$[1]));',
        b'\tdispbottom "[MBQA] kill n=" + .@n + " counted=" + @MBQA_COUNTED;',
        b'\tend;',
        b'OnFeed:',
        b'\t.@n = need_marble_test_hunt_feed(atoi(.@atcmd_parameters$[0]));',
        b'\tdispbottom "[MBQA] feed n=" + .@n + " counted=" + @MBQA_COUNTED;',
        b'\tend;',
        b'OnState:',
        b'\tneed_marble_view();',
        b'\tdispbottom "[MBQA] state hp=" + Hp + " lv=" + BaseLevel + " map=" + strcharinfo(3) + " kills=" + @MB_HUNT_KILLS;',
        b'\tend;',
        b'}',
        b''])
hp = os.path.join(S, 'npc', 'custom', '_mbqa_harness.txt')
open(hp, 'wb').write(harness)
report.append(f'npc/custom/_mbqa_harness.txt written ({len(harness)} bytes, instance names copied as CP949 bytes)')

for r in report:
    print(r)

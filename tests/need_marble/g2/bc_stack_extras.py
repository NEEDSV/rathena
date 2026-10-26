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
ap.add_argument('--g2b', action='store_true', help='G2-B: @mbqaclock / @mbqacommitfault / @mbqareplay / @mbqareentry / @mbqaoutoftx / @mbqaflags (apply_test_hooks.py --g2b build only)')
ap.add_argument('--g2c', action='store_true', help='G2-C: @mbqabg* battleground commands that drive the real reward NPCs / auto reward functions')
ap.add_argument('--v20', action='store_true', help='V20-MOCK: @mbqacpmode / @mbqacpapply / @mbqacpresolve (apply_test_hooks.py --v20 build only)')
ap.add_argument('--v20real', action='store_true', help='V20-REAL: @mbqacpreal / @mbqacpcash / @mbqacpinfl / @mbqacpraw / @mbqacplast (apply_test_hooks.py --v20real build only)')
ap.add_argument('--cp-grant', action='store_true', help='V20-REAL: need_marble_cp_grant: yes in the stack inter import conf (char-server switch)')
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


def cp_grant_conf():
    """V20-REAL: the char-server grants CP only with this switch (default off) and both tables InnoDB."""
    p = os.path.join(S, 'conf', 'import', 'inter_conf.txt')
    raw = open(p, 'rb').read()
    if b'need_marble_cp_grant' in raw:
        raise SystemExit('inter_conf.txt: need_marble_cp_grant already present')
    if not raw.endswith(b'\n'):
        raw += b'\r\n'
    open(p, 'wb').write(raw + b'// --- V20-REAL isolated test only (bc_stack_extras.py --cp-grant) ---\r\nneed_marble_cp_grant: yes\r\n')
    report.append('conf/import/inter_conf.txt: + need_marble_cp_grant: yes')


if not a.harness_only:
    apply_d1_e2()
    if a.bind_local:
        bind_local()
    if a.cp_grant:
        cp_grant_conf()
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
if a.g2b:
    # G2-B: needs the map-server built from a scratch copy patched with apply_test_hooks.py --g2b
    harness += b'\r\n'.join([
        b'// G2-B test build only: @mbqaclock <unix time>, @mbqacommitfault <0|1|2>, @mbqareplay <session> <batch> <day:kills,...>,',
        b'// @mbqareentry, @mbqaoutoftx, @mbqaflags',
        b'-\tscript\tMBQA_G2BHarness\t-1,{',
        b'OnInit:',
        b'\tbindatcmd "mbqaclock", strnpcinfo(3) + "::OnClock", 99, 100;',
        b'\tbindatcmd "mbqacommitfault", strnpcinfo(3) + "::OnCommitFault", 99, 100;',
        b'\tbindatcmd "mbqareplay", strnpcinfo(3) + "::OnReplay", 99, 100;',
        b'\tbindatcmd "mbqareentry", strnpcinfo(3) + "::OnReentry", 99, 100;',
        b'\tbindatcmd "mbqaoutoftx", strnpcinfo(3) + "::OnOutOfTx", 99, 100;',
        b'\tbindatcmd "mbqaflags", strnpcinfo(3) + "::OnFlags", 99, 100;',
        b'\tend;',
        b'OnClock:',
        b'\tdispbottom "[MBQA] clock day=" + need_marble_test_clock(atoi(.@atcmd_parameters$[0]));',
        b'\tend;',
        b'OnCommitFault:',
        b'\tdispbottom "[MBQA] commitfault mode=" + need_marble_test_commit_fault(atoi(.@atcmd_parameters$[0]));',
        b'\tend;',
        b'OnReplay:',
        b'\t.@rc = need_marble_test_replay(atoi(.@atcmd_parameters$[0]), atoi(.@atcmd_parameters$[1]), .@atcmd_parameters$[2]);',
        b'\tdispbottom "[MBQA] replay rc=" + .@rc + " credited=" + @MBQA_CREDITED;',
        b'\tend;',
        b'OnReentry:',
        b'\tdispbottom "[MBQA] reentry rc=" + need_marble_test_reentry();',
        b'\tend;',
        b'OnOutOfTx:',
        b'\tdispbottom "[MBQA] outoftx rc=" + need_marble_test_out_of_tx();',
        b'\tend;',
        b'OnFlags:',
        b'\tneed_marble_test_flags();',
        b'\tdispbottom "[MBQA] flags in_tx=" + @MBQA_IN_TX + " fault=" + @MBQA_FAULT + " day=" + @MBQA_DAY + " polday=" + @MBQA_POL_DAY'
        b' + " session=" + @MBQA_SESSION + " batch=" + @MBQA_BATCH + " frozen=" + @MBQA_FROZEN + " pending=" + @MBQA_PENDING'
        b' + " pdays=" + @MBQA_PENDING_DAYS;',
        b'\tend;',
        b'}',
        b''])
if a.g2c:
    # G2-C: a random match battle set up like the addon does (bg_create / bg_join / $@BG_RM_* arrays), then the REAL end paths:
    # the reward NPCs of tierra01 / flavius01 / kvm01 (clicked by the player) and F_BG_RM_AutoRewardBadge / F_KvM_AutoRewardRandom.
    import re

    def npc_name(rel, tag):
        text = open(os.path.join(S, rel), 'rb').read()
        m = re.findall(rb'\n(bat_[abc]01,\d+,\d+,\d+)\tscript\t([^\t]*' + re.escape(tag) + rb')\t(\d+),\{', text)
        names = [n for _, n, sprite in m if sprite not in (b'95',)]   # 95 = the healer NPC sharing the tag
        assert len(names) == 1, (rel, tag, m)
        return names[0]
    ga = [npc_name('npc/battleground/tierra/tierra01.txt', b'#a01_a'), npc_name('npc/battleground/flavius/flavius01.txt', b'#b01_a'),
          npc_name('npc/battleground/kvm/kvm01.txt', b'#KVM01A')]
    ca = [npc_name('npc/battleground/tierra/tierra01.txt', b'#a01_b'), npc_name('npc/battleground/flavius/flavius01.txt', b'#b01_b'), ga[2]]
    q = lambda xs: b', '.join(b'"' + x + b'"' for x in xs)
    harness += b'\r\n'.join([
        b'// G2-C: @mbqabgstart <mode 1-3> <token> <Guillaume names,> <Croix names,> (- = nobody), @mbqabgend <result 1|2|3>,',
        b'// @mbqabgauto (real auto reward function), @mbqabgnpc <team 1|2> (gid of the real reward NPC), @mbqabgstop (real admin force stop),',
        b'// @mbqabgreset (real abort + finish), @mbqabgcredit <type> <token> (second end call for the caller), @mbqabgwhere <name>, @mbqabgitems <name>',
        b'-\tscript\tMBQA_BGHarness\t-1,{',
        b'OnInit:',
        b'\tsetarray .map$[1], "bat_a01", "bat_b01", "bat_c01";',
        b'\tsetarray .type$[1], "Tierra", "Flavius", "KVM";',
        b'\tsetarray .gx[1], 50, 10, 52;',
        b'\tsetarray .gy[1], 374, 290, 129;',
        b'\tsetarray .cx[1], 42, 390, 147;',
        b'\tsetarray .cy[1], 16, 10, 55;',
        b'\tsetarray .npc$[1], ' + q(ga) + b';',
        b'\tsetarray .npc2$[1], ' + q(ca) + b';',
        b'\tbindatcmd "mbqabgstart", strnpcinfo(3) + "::OnStart", 99, 100;',
        b'\tbindatcmd "mbqabgend", strnpcinfo(3) + "::OnEnd", 99, 100;',
        b'\tbindatcmd "mbqabgauto", strnpcinfo(3) + "::OnAuto", 99, 100;',
        b'\tbindatcmd "mbqabgnpc", strnpcinfo(3) + "::OnNpc", 99, 100;',
        b'\tbindatcmd "mbqabgstop", strnpcinfo(3) + "::OnStop", 99, 100;',
        b'\tbindatcmd "mbqabgreset", strnpcinfo(3) + "::OnReset", 99, 100;',
        b'\tbindatcmd "mbqabgcredit", strnpcinfo(3) + "::OnCredit", 99, 100;',
        b'\tbindatcmd "mbqabgwhere", strnpcinfo(3) + "::OnWhere", 99, 100;',
        b'\tbindatcmd "mbqabgitems", strnpcinfo(3) + "::OnItems", 99, 100;',
        b'\tend;',
        b'OnStart:',
        b'\t.@mode = atoi(.@atcmd_parameters$[0]);',
        b'\t.@tok = atoi(.@atcmd_parameters$[1]);',
        b'\tif (.@mode < 1 || .@mode > 3 || .@tok <= 0) end;',
        b'\t.@gm = getcharid(3);',
        b'\t.@m$ = .map$[.@mode];',
        b'\t.@g = bg_create(.@m$, .gx[.@mode], .gy[.@mode], "", "");',
        b'\t.@c = bg_create(.@m$, .cx[.@mode], .cy[.@mode], "", "");',
        b'\tif (.@mode == 1) { $@TierraBG1_id1 = .@g; $@TierraBG1_id2 = .@c; $@TierraBG1 = 1; $@TierraBG1_Victory = 0; }',
        b'\telse if (.@mode == 2) { $@FlaviusBG1_id1 = .@g; $@FlaviusBG1_id2 = .@c; $@FlaviusBG1 = 1; $@FlaviusBG1_Victory = 0; }',
        b'\telse { $@KvM01BG_id1 = .@g; $@KvM01BG_id2 = .@c; $@KvM01BG = 1; $@KvM01BG_Victory = 0; }',
        b'\t$@BG_RM_TOKEN = .@tok;',
        b'\t$@BG_RM_MODE = .@mode;',
        b'\t$@BG_RM_ACTIVE = 1;',
        b'\t$@BG_RM_CYCLE_ACTIVE = 1;',
        b'\t$@BG_RM_STATE = 3;',
        b'\tcallfunc "F_BG_RM_BeginRewardStage", .@mode;',
        b'\tdeletearray $@BG_RM_G_AID[0], 128; deletearray $@BG_RM_G_CID[0], 128; deletearray $@BG_RM_G_NAME$[0], 128;',
        b'\tdeletearray $@BG_RM_C_AID[0], 128; deletearray $@BG_RM_C_CID[0], 128; deletearray $@BG_RM_C_NAME$[0], 128;',
        b'\texplode(.@gn$, .@atcmd_parameters$[2], ",");',   # explode pushes no value: count with getarraysize
        b'\texplode(.@cn$, .@atcmd_parameters$[3], ",");',
        b'\t.@ng = getarraysize(.@gn$);',
        b'\t.@nc = getarraysize(.@cn$);',
        b'\t.@j = 0;',
        b'\tfor (.@i = 0; .@i < .@ng; .@i++) {',
        b'\t\t.@cid = getcharid(0, .@gn$[.@i]);',
        b'\t\tif (.@cid <= 0) continue;',
        b'\t\t.@aid = getcharid(3, .@gn$[.@i]);',
        b'\t\t.@joined += bg_join(.@g, .@m$, .gx[.@mode], .gy[.@mode], .@cid);',
        b'\t\t$@BG_RM_G_AID[.@j] = .@aid; $@BG_RM_G_CID[.@j] = .@cid; $@BG_RM_G_NAME$[.@j] = .@gn$[.@i]; .@j++;',
        b'\t\tif (attachrid(.@aid)) Bat_Team = 1;',
        b'\t\tattachrid .@gm;',
        b'\t}',
        b'\t$@BG_RM_G_COUNT = .@j;',
        b'\t.@j = 0;',
        b'\tfor (.@i = 0; .@i < .@nc; .@i++) {',
        b'\t\t.@cid = getcharid(0, .@cn$[.@i]);',
        b'\t\tif (.@cid <= 0) continue;',
        b'\t\t.@aid = getcharid(3, .@cn$[.@i]);',
        b'\t\t.@joined += bg_join(.@c, .@m$, .cx[.@mode], .cy[.@mode], .@cid);',
        b'\t\t$@BG_RM_C_AID[.@j] = .@aid; $@BG_RM_C_CID[.@j] = .@cid; $@BG_RM_C_NAME$[.@j] = .@cn$[.@i]; .@j++;',
        b'\t\tif (attachrid(.@aid)) Bat_Team = 2;',
        b'\t\tattachrid .@gm;',
        b'\t}',
        b'\t$@BG_RM_C_COUNT = .@j;',
        b'\tdispbottom "[MBQA] bgstart mode=" + .@mode + " token=" + .@tok + " g=" + .@g + " c=" + .@c + " gcount=" + $@BG_RM_G_COUNT + " ccount=" + $@BG_RM_C_COUNT + " joined=" + .@joined;',
        b'\tend;',
        b'OnEnd:',
        b'\t.@mode = $@BG_RM_MODE;',
        b'\t.@r = atoi(.@atcmd_parameters$[0]);',
        b'\tif (.@mode == 1) $@TierraBG1_Victory = .@r;',
        b'\telse if (.@mode == 2) $@FlaviusBG1_Victory = .@r;',
        b'\telse if (.@mode == 3) $@KvM01BG_Victory = .@r;',
        b'\t$@MBQA_BG_RESULT = .@r;',
        b'\tenablenpc .npc$[.@mode];',
        b'\tif (.@mode < 3) enablenpc .npc2$[.@mode];',
        b'\tdispbottom "[MBQA] bgend mode=" + .@mode + " result=" + .@r;',
        b'\tend;',
        b'OnAuto:',
        b'\t.@mode = $@BG_RM_MODE;',
        b'\tif (.@mode == 3) .@n = callfunc("F_KvM_AutoRewardRandom");',
        b'\telse .@n = callfunc("F_BG_RM_AutoRewardBadge", .@mode, .map$[.@mode], $@MBQA_BG_RESULT, .type$[.@mode]);',
        b'\tdispbottom "[MBQA] bgauto mode=" + .@mode + " rewarded=" + .@n;',
        b'\tend;',
        b'OnNpc:',
        b'\t.@mode = $@BG_RM_MODE;',
        b'\t.@t = atoi(.@atcmd_parameters$[0]);',
        b'\t.@n$ = (.@t == 2) ? .npc2$[.@mode] : .npc$[.@mode];',
        b'\tdispbottom "[MBQA] bgnpc mode=" + .@mode + " team=" + .@t + " gid=" + getnpcid(0, .@n$);',
        b'\tend;',
        b'OnStop:',
        b'\tdonpcevent "NEED_BG_RM_Core::OnAdminForceStop";',
        b'\tsleep2 500;',
        b'\tdispbottom "[MBQA] bgstop cycle=" + $@BG_RM_CYCLE_ACTIVE + " token=" + $@BG_RM_TOKEN;',
        b'\tend;',
        b'OnReset:',
        b'\t.@mode = $@BG_RM_MODE;',
        b'\tif (.@mode >= 1 && .@mode <= 3) {',
        b'\t\tdisablenpc .npc$[.@mode];',
        b'\t\tif (.@mode < 3) disablenpc .npc2$[.@mode];',
        b'\t\tcallfunc "F_BG_RM_AbortCreatedBattle", .@mode;',
        b'\t}',
        b'\tcallfunc "F_BG_RM_Finish";',
        b'\t$@BG_RM_CYCLE_ACTIVE = 0;',
        b'\t$@BG_RM_ACTIVE = 0;',
        b'\tdispbottom "[MBQA] bgreset mode=" + .@mode + " cycle=" + $@BG_RM_CYCLE_ACTIVE;',
        b'\tend;',
        b'OnCredit:',
        b'\tdispbottom "[MBQA] bgcredit rc=" + need_marble_bg_credit(.@atcmd_parameters$[0], atoi(.@atcmd_parameters$[1]));',
        b'\tend;',
        b'// @mbqabgitems <name>: the existing BG rewards held by that character (badges, KVM badge, 6378, 399990, #BATTLEGROUND_POINT)',
        b'OnItems:',
        b'\t.@aid = getcharid(3, .@atcmd_parameters$[0]);',
        b'\tif (.@aid <= 0) end;',
        b'\t.@gm = getcharid(3);',
        b'\tif (attachrid(.@aid)) {',
        b'\t\t.@s$ = "7828=" + countitem(7828) + " 7829=" + countitem(7829) + " 7773=" + countitem(7773) + " 6378=" + countitem(6378) + " 399990=" + countitem(399990) + " bgp=" + #BATTLEGROUND_POINT;',
        b'\t}',
        b'\tattachrid .@gm;',
        b'\tdispbottom "[MBQA] bgitems name=" + .@atcmd_parameters$[0] + " " + .@s$;',
        b'\tend;',
        b'OnWhere:',
        b'\t.@aid = getcharid(3, .@atcmd_parameters$[0]);',
        b'\t.@cid = getcharid(0, .@atcmd_parameters$[0]);',
        b'\t.@bg = 0;',
        b'\tif (.@aid > 0) {',
        b'\t\t.@gm = getcharid(3);',
        b'\t\tif (attachrid(.@aid)) { .@bg = getcharid(4); .@m$ = strcharinfo(3); getmapxy(.@mm$, .@x, .@y, BL_PC); .@team = Bat_Team; }',
        b'\t\tattachrid .@gm;',
        b'\t}',
        b'\tdispbottom "[MBQA] bgwhere name=" + .@atcmd_parameters$[0] + " aid=" + .@aid + " cid=" + .@cid + " bg=" + .@bg + " map=" + .@m$ + " x=" + .@x + " y=" + .@y + " team=" + .@team;',
        b'\tend;',
        b'}',
        b''])

if a.v20:
    # V20-MOCK: needs the map-server built from a scratch copy patched with apply_test_hooks.py --v20
    harness += b'\r\n'.join([
        b'// V20-MOCK test build only: @mbqacpmode <0|1|2|3> (one shot mock writer fault), @mbqacpapply <char> <payout_no>,',
        b'// @mbqacpresolve <account_id> <payout_no> <4|5> <ticket>',
        b'-\tscript\tMBQA_V20Harness\t-1,{',
        b'OnInit:',
        b'\tbindatcmd "mbqacpmode", strnpcinfo(3) + "::OnMode", 99, 100;',
        b'\tbindatcmd "mbqacpapply", strnpcinfo(3) + "::OnApply", 99, 100;',
        b'\tbindatcmd "mbqacpresolve", strnpcinfo(3) + "::OnResolve", 99, 100;',
        b'\tend;',
        b'OnMode:',
        b'\tdispbottom "[MBQA] cpmode mode=" + need_marble_test_cp_mode(atoi(.@atcmd_parameters$[0]));',
        b'\tend;',
        b'OnApply:',
        b'\tdispbottom "[MBQA] cpapply rc=" + need_marble_test_cp_apply(.@atcmd_parameters$[0], atoi(.@atcmd_parameters$[1]));',
        b'\tend;',
        b'OnResolve:',
        b'\tdispbottom "[MBQA] cpresolve rc=" + need_marble_test_cp_resolve(atoi(.@atcmd_parameters$[0]), atoi(.@atcmd_parameters$[1]),'
        b' atoi(.@atcmd_parameters$[2]), atoi(.@atcmd_parameters$[3]));',
        b'\tend;',
        b'}',
        b''])

if a.v20real:
    # V20-REAL: needs the map-server built from a scratch copy patched with apply_test_hooks.py --v20real
    harness += b'\r\n'.join([
        b'// V20-REAL test build only: @mbqacpreal <mode>, @mbqacpcash <char> <delta> <save>, @mbqacpinfl <char>,',
        b'// @mbqacpraw <char> <payout_no> <amount>, @mbqacplast <payout_no>',
        b'-\tscript\tMBQA_V20RealHarness\t-1,{',
        b'OnInit:',
        b'\tbindatcmd "mbqacpreal", strnpcinfo(3) + "::OnReal", 99, 100;',
        b'\tbindatcmd "mbqacpcash", strnpcinfo(3) + "::OnCash", 99, 100;',
        b'\tbindatcmd "mbqacpinfl", strnpcinfo(3) + "::OnInfl", 99, 100;',
        b'\tbindatcmd "mbqacpraw", strnpcinfo(3) + "::OnRaw", 99, 100;',
        b'\tbindatcmd "mbqacplast", strnpcinfo(3) + "::OnLast", 99, 100;',
        b'\tend;',
        b'OnReal:',
        b'\tdispbottom "[MBQA] cpreal r=" + need_marble_test_cp_real(atoi(.@atcmd_parameters$[0]));',
        b'\tend;',
        b'OnCash:',
        b'\tdispbottom "[MBQA] cpcash cash=" + need_marble_test_cp_cash(.@atcmd_parameters$[0], atoi(.@atcmd_parameters$[1]), atoi(.@atcmd_parameters$[2]));',
        b'\tend;',
        b'OnInfl:',
        b'\tdispbottom "[MBQA] cpinfl v=" + need_marble_test_cp_inflight(.@atcmd_parameters$[0]);',
        b'\tend;',
        b'OnRaw:',
        b'\tdispbottom "[MBQA] cpraw rc=" + need_marble_test_cp_raw(.@atcmd_parameters$[0], atoi(.@atcmd_parameters$[1]), atoi(.@atcmd_parameters$[2]));',
        b'\tend;',
        b'OnLast:',
        b'\tdispbottom "[MBQA] cplast r=" + need_marble_test_cp_last(atoi(.@atcmd_parameters$[0]));',
        b'\tend;',
        b'}',
        b''])

hp = os.path.join(S, 'npc', 'custom', '_mbqa_harness.txt')
open(hp, 'wb').write(harness)
report.append(f'npc/custom/_mbqa_harness.txt written ({len(harness)} bytes, instance names copied as CP949 bytes)')

for r in report:
    print(r)

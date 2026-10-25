"""G2 scenario runner for the isolated stack only (login 26901 / char 26121 / map 25121, DB 127.0.0.1:3327).
Never starts, stops or kills a process (bc_servers.ps1 does that) and writes only to <out>.

usage: python -u -I bc_runner.py --out <scratch>/mbqa_bc <cmd> [args]
"""
import argparse
import json
import os
import re
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bcclient  # noqa: E402
import bcdb  # noqa: E402
import v11_model  # noqa: E402
import v22_model  # noqa: E402

OUT = None
LOG = []
ACC = {'gm': ('mbqa_gm', 'gm'), 'user': ('mbqa_user', 'user'), 'p2': ('mbqa_p2', 'user'), 'p3': ('mbqa_p3', 'user'),
       'p4': ('mbqa_p4', 'user'), 'cap': ('mbqa_cap', 'user'), 'v22a': ('mbqa_v22a', 'user'), 'v22b': ('mbqa_v22b', 'user'),
       'v11': ('mbqa_v11', 'user'), 'heal': ('mbqa_heal', 'user'),
       'v16': ('mbqa_v16', 'user'), 'v21': ('mbqa_v21', 'user'), 'v24': ('mbqa_v24', 'user'), 'v24r': ('mbqa_v24r', 'user'),
       'v24s': ('mbqa_v24s', 'user')}
NAMES = {('gm', 0): 'MBQA_GM1', ('gm', 1): 'MBQA_GM2', ('user', 0): 'MBQA_U1', ('user', 1): 'MBQA_U2',
         ('p2', 0): 'MBQA_P2', ('p3', 0): 'MBQA_P3', ('p4', 0): 'MBQA_P4', ('cap', 0): 'MBQA_CAP',
         ('v22a', 0): 'MBQA_V22A', ('v22b', 0): 'MBQA_V22B', ('v11', 0): 'MBQA_V11', ('heal', 0): 'MBQA_HEAL',
         ('v16', 0): 'MBQA_V16', ('v21', 0): 'MBQA_V21A', ('v21', 1): 'MBQA_V21B', ('v24', 0): 'MBQA_V24A', ('v24', 1): 'MBQA_V24B',
         ('v24r', 0): 'MBQA_V24RA', ('v24r', 1): 'MBQA_V24RB', ('v24s', 0): 'MBQA_V24SA', ('v24s', 1): 'MBQA_V24SB'}
AID = {'gm': 2000901, 'user': 2000902, 'p2': 2000903, 'p3': 2000904, 'p4': 2000905, 'cap': 2000906,
       'v22a': 2000907, 'v22b': 2000908,   # v22a / v22b: V22 only (bc_synth_v22.sql)
       'v11': 2000909, 'heal': 2000910,     # v11: V11 only, heal: V18-C healer (bc_synth_g2a.sql)
       'v16': 2000911, 'v21': 2000912, 'v24': 2000913, 'v24r': 2000914, 'v24s': 2000915}   # G2-B only (bc_synth_g2b.sql)


def log(*a):
    s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a)
    print(s, flush=True)
    LOG.append(s)
    with open(os.path.join(OUT, 'runner.log'), 'a', encoding='utf-8') as f:
        f.write(s + '\n')


def sql(q):
    return bcdb.query(OUT, q)


def sec():
    return json.load(open(os.path.join(OUT, 'secrets.json')))


def record(tid, status, detail):
    p = os.path.join(OUT, 'results.json')
    res = json.load(open(p, encoding='utf-8')) if os.path.exists(p) else []
    res.append({'id': tid, 'status': status, 'detail': detail, 'time': time.strftime('%H:%M:%S')})
    json.dump(res, open(p, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
    log(f'==> {tid} {status}: {detail}')


def login(who, slot=0, tries=8):
    user, key = ACC[who]
    for n in range(tries):
        try:
            c = bcclient.Client(user, sec()[key], slot=slot, log=log, tag=f'{who}{slot}')
            c.connect(NAMES[(who, slot)])
            return c
        except (TimeoutError, ConnectionError, OSError) as e:
            log(f'   login {who}{slot} attempt {n + 1} failed: {e}')
            time.sleep(5)
    raise RuntimeError(f'login {who}{slot} failed')


def wait_offline(aid, timeout=20):
    end = time.time() + timeout
    while time.time() < end:
        if sql(f'SELECT COUNT(*) FROM `char` WHERE account_id={aid} AND online=1')[0][0] == '0':
            time.sleep(1.0)
            return True
        time.sleep(0.5)
    return False


def menu_picker(plan):
    """plan: list of labels / ints consumed in order; anything left over -> ESC."""
    plan = list(plan)
    seen = []

    def choose(kind, items, mes):
        seen.append((kind, items))
        if not plan:
            if kind == 'menu':
                return '닫기' if any('닫기' in x for x in items) else None
            return 0
        return plan.pop(0)
    choose.seen = seen
    return choose


def marble(c, plan, timeout=25):
    ch = menu_picker(plan)
    mes, texts, done = c.dialog('@marble', ch, timeout)
    for m in mes:
        log(f'   [{c.tag} mes] {m}')
    for k, items in ch.seen:
        log(f'   [{c.tag} {k}] {items}')
    return mes, texts, done


def state(aid):
    r = sql(f'SELECT state,pos,left_steps,die,roll_seq,step_seq,run_session,jump_seq,start_pass,laps,run_char_id '
            f'FROM need_marble_state WHERE account_id={aid}')
    if not r:
        return None
    k = ['state', 'pos', 'left', 'die', 'roll', 'steps', 'run', 'jump', 'spass', 'laps', 'run_char']
    return dict(zip(k, [int(x) for x in r[0]]))


def wallet(aid, season=1):
    r = sql(f'SELECT score,dice,pending_ack FROM need_marble_wallet WHERE account_id={aid} AND season_id={season}')
    return dict(zip(['score', 'dice', 'pack'], [int(x) for x in r[0]])) if r else None


def invariants():
    """Runs sql-files/need_marble_phase_bc_v1_invariants.sql and returns the violating rows."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'sql-files', 'need_marble_phase_bc_v1_invariants.sql')
    rc, o, e = bcdb.run(OUT, 'rt', open(path, encoding='utf-8').read())
    if rc:
        return ['SQL ERROR ' + e.strip()[-300:]]
    return [l for l in o.splitlines() if l.strip()]


def adjust(aid, score=0, dice=0, ticket=1, season=1):
    """Operator adjustment rows (kind 5) so I1 (wallet = sum of ledger) keeps holding."""
    sql(f"INSERT IGNORE INTO need_marble_wallet (account_id,season_id,updated_at) VALUES ({aid},{season},NOW())")
    sql(f"INSERT INTO need_marble_wallet_tx (account_id,season_id,kind,ref_type,ref_a,ref_b,ref_c,ref_d,char_id,d_score,d_dice,period_day,created_at) "
        f"VALUES ({aid},{season},5,5,{ticket},0,0,0,0,{score},{dice},0,NOW())")
    sql(f"UPDATE need_marble_wallet SET score=score+{score}, dice=dice+{dice} WHERE account_id={aid} AND season_id={season}")


# ---------------------------------------------------------------------------- commands
def cmd_smoke(a):
    c = login('gm')
    mes, texts, done = marble(c, [])
    ok = done and any('활동 점수' in m for m in mes)
    record('SMOKE', 'PASS' if ok else 'FAIL', f'menu shown={done}, lines={len(mes)}')
    c.logout()


def cmd_hunt_probe(a):
    """G-1(a): can the protocol client kill a permanent field mob?"""
    c = login('gm')
    for t in ('@warp prt_fild08 170 375', '@alive', '@heal'):
        c.command(t)
    time.sleep(3)
    c.walk(171, 375)
    time.sleep(2)
    log('mobs seen:', len(c.mobs))
    killed = 0
    tried = 0
    end = time.time() + 120
    while time.time() < end and killed < 3:
        cand = [g for g in c.mobs if g not in c.dead]
        if not cand:
            c.walk(170 + (tried % 5) * 3, 370 + (tried % 3) * 4)
            time.sleep(2)
            tried += 1
            continue
        g = cand[0]
        c.attack(g)
        t0 = time.time()
        while time.time() - t0 < 12 and g not in c.dead and g in c.mobs:
            time.sleep(0.2)
        if g in c.dead:
            killed += 1
            log('killed', g)
        else:
            c.mobs.pop(g, None)
        tried += 1
    log('dead set', len(c.dead), 'tried', tried)
    record('G1a-PROBE', 'PASS' if killed > 0 else 'FAIL', f'killed={killed} tried={tried} mobs_seen={len(c.mobs) + len(c.dead)}')
    c.logout()


def cmd_probe_units(a):
    """Debug: which unit-like packet ids appear after a warp to a field."""
    import collections
    import struct as st
    c = login('gm')
    out = c.command('@warp prt_fild08 170 375')
    log('warp texts', out)
    time.sleep(5)
    b = bytes(c.buf)
    cnt = collections.Counter()
    for pid in (0x09FD, 0x09FE, 0x09FF, 0x0091, 0x0092, 0x0AC7, 0x0B32):
        k = st.pack('<H', pid)
        i = b.find(k)
        while i >= 0:
            cnt[hex(pid)] += 1
            if pid in (0x09FD, 0x09FE, 0x09FF) and cnt[hex(pid)] <= 3:
                ln = b[i + 2] | (b[i + 3] << 8)
                log(hex(pid), 'len', ln, 'type', b[i + 4], 'aid', st.unpack_from('<I', b, i + 5)[0], 'gid', st.unpack_from('<I', b, i + 9)[0])
            i = b.find(k, i + 1)
    log('counts', dict(cnt), 'buf', len(b), 'mobs', len(c.mobs))
    c.logout()
    log('last_map', sql('SELECT last_map,last_x,last_y FROM `char` WHERE char_id=150901'))


def inst_rows(aid):
    return sql(f'SELECT key_a,key_b,key_c,credited,outcome,period_day FROM need_marble_activity WHERE account_id={aid} AND activity=2 ORDER BY created_at, key_a, key_b')


def harness(c, text, needle='[MBQA]', wait=8):
    m = c.mark()
    c.command(text, settle=wait)
    t = c.wait_text(m, needle, 2)
    log(f'   [{c.tag}] {text} -> {t}')
    return t


def cmd_inst(a):
    """V10 / V13 (G-2a): grade A instance credit through the ledger, run key vs daily eligibility key."""
    aid = AID['gm']
    sql("UPDATE `char` SET last_map='prontera', last_x=156, last_y=191 WHERE account_id=2000901 AND online=0")
    c = login('gm')
    harness(c, '@mbqainst 1')
    time.sleep(2)
    harness(c, '@mbqaclear 1')          # active check: freshly warped in (warp counts as activity? recorded below)
    r1 = inst_rows(aid)
    c.walk(100, 100)
    time.sleep(2)
    t2 = harness(c, '@mbqaclear 1')     # same run again -> ALREADY, no new row
    r2 = inst_rows(aid)
    harness(c, '@mbqaleave')
    time.sleep(2)
    harness(c, '@mbqainst 1')           # same dungeon, new instance, same day
    time.sleep(2)
    c.walk(100, 101)
    time.sleep(2)
    harness(c, '@mbqaclear 1')
    r3 = inst_rows(aid)
    harness(c, '@mbqaleave')
    time.sleep(2)
    harness(c, '@mbqainst 2')           # other grade A dungeon
    time.sleep(2)
    c.walk(100, 102)
    time.sleep(2)
    harness(c, '@mbqaclear 2')
    r4 = inst_rows(aid)
    harness(c, '@mbqaleave')
    c.logout()
    wait_offline(aid)
    c2 = login('gm', 1)                 # other character of the same account
    harness(c2, '@mbqainst 1')
    time.sleep(2)
    c2.walk(100, 103)
    time.sleep(2)
    harness(c2, '@mbqaclear 1')
    r5 = inst_rows(aid)
    harness(c2, '@mbqaleave')
    c2.logout()
    for k, r in (('after first clear', r1), ('repeat same run', r2), ('new instance same dungeon', r3),
                 ('other dungeon', r4), ('other char same dungeon', r5)):
        log(f'   {k}: {r}')
    w = wallet(aid)
    per = sql(f'SELECT activity,earned FROM need_marble_period WHERE account_id={aid} ORDER BY activity')
    daily = sql(f'SELECT dungeon_code,boot_id,instance_id FROM need_marble_dungeon_daily WHERE account_id={aid} ORDER BY dungeon_code')
    log('   wallet', w, 'period', per, 'daily', daily)
    json.dump({'r1': r1, 'r2': r2, 'r3': r3, 'r4': r4, 'r5': r5, 'wallet': w, 'period': per, 'daily': daily},
              open(os.path.join(OUT, 'inst_result.json'), 'w'), indent=1)


def reqs(aid):
    return sql(f'SELECT request_no,status,reject_code,acked,bundles,cost FROM need_marble_exchange_req WHERE account_id={aid} ORDER BY request_no')


def exch_tx(aid, req):
    return int(sql(f'SELECT COUNT(*) FROM need_marble_wallet_tx WHERE account_id={aid} AND kind=2 AND ref_a={req}')[0][0])


def cmd_exchange(a):
    aid = AID['gm']
    res = {}
    # E1 normal exchange 1 die
    c = login('gm')
    w0 = wallet(aid)
    marble(c, ['점수 → 주사위 교환', 1, '교환'])
    w1 = wallet(aid)
    r = reqs(aid)
    ok = w1['score'] == w0['score'] - 50 and w1['dice'] == w0['dice'] + 1 and w1['pack'] == 0 and r[-1][1] == '2' and r[-1][3] == '1'
    record('X-E1 normal exchange', 'PASS' if ok else 'FAIL', f'{w0} -> {w1}, req {r[-1]}')

    # V15 commit response lost -> other character of the same account
    c.drop_after = False
    ch = menu_picker(['점수 → 주사위 교환', 1])
    def choose(kind, items, mes, base=ch):
        if kind == 'menu' and any(x == '교환' for x in items):
            c.drop_after = True
            return '교환'
        return base(kind, items, mes)
    c.dialog('@marble', choose, 25)
    t_drop = time.time()
    wait_offline(aid)
    w2 = wallet(aid)
    r2 = reqs(aid)
    req = int(r2[-1][0])
    log('   after drop', w2, r2[-1])
    c2 = login('gm', 1)
    mes, texts, done = marble(c2, [])
    w3 = wallet(aid)
    r3 = reqs(aid)
    shown = any('교환 완료' in m for m in mes)
    ok = (r2[-1][1] == '2' and w2['pack'] == req and exch_tx(aid, req) == 1 and shown and
          w3['pack'] == 0 and r3[-1][3] == '1' and len(r3) == len(r2) and w3['dice'] == w1['dice'] + 1)
    record('V15 response loss + other char', 'PASS' if ok else 'FAIL',
           f'after drop {w2} req={r2[-1]}; relogin shown={shown}; after ack {w3} req={r3[-1]}; EXCHANGE rows={exch_tx(aid, req)}; requests {len(r2)}->{len(r3)}')

    # X-E2 drop before confirming (ISSUED) -> relogin shows it -> cancel
    ch = menu_picker(['점수 → 주사위 교환', 1])
    def choose2(kind, items, mes, base=ch):
        if kind == 'menu' and any(x == '교환' for x in items):
            c2.drop()
            raise SystemExit  # handled below
        return base(kind, items, mes)
    try:
        c2.dialog('@marble', choose2, 25)
    except SystemExit:
        pass
    wait_offline(aid)
    w4 = wallet(aid)
    r4 = reqs(aid)
    c3 = login('gm')
    mes, texts, done = marble(c3, ['요청 취소'])
    w5 = wallet(aid)
    r5 = reqs(aid)
    ok = r4[-1][1] == '1' and w4['pack'] == int(r4[-1][0]) and r5[-1][1] == '4' and r5[-1][3] == '1' and w5['pack'] == 0 and w5['dice'] == w4['dice'] and w5['score'] == w4['score']
    record('X-E2 unconfirmed request cancelled after relogin', 'PASS' if ok else 'FAIL', f'issued {r4[-1]} pack={w4["pack"]}; after cancel {r5[-1]} {w5}')

    # X-E3 hold limit enforced by the server (dice raised to 30 between issue and confirm), score untouched
    ch = menu_picker(['점수 → 주사위 교환', 1])
    def choose3(kind, items, mes, base=ch):
        if kind == 'menu' and any(x == '교환' for x in items):
            cur = wallet(aid)['dice']
            adjust(aid, dice=30 - cur, ticket=3)
            return '교환'
        return base(kind, items, mes)
    mes, texts, done = c3.dialog('@marble', choose3, 25)
    w6 = wallet(aid)
    r6 = reqs(aid)
    ok = r6[-1][1] == '3' and r6[-1][2] == '2' and w6['dice'] == 30 and w6['score'] == w5['score'] and w6['pack'] == 0 and exch_tx(aid, int(r6[-1][0])) == 0
    record('X-E3 hold limit 30 refused, score kept', 'PASS' if ok else 'FAIL', f'req {r6[-1]} wallet {w6}')
    c3.logout()
    inv = invariants()
    record('INV after exchange', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:5]}')


def cmd_exchange_b(a):
    aid = AID['gm']
    c2 = login('gm', 1)
    # X-E2 drop before confirming (ISSUED) -> relogin shows it -> cancel
    ch = menu_picker(['점수 → 주사위 교환', 1])
    adjust(aid, score=100, ticket=2)
    def choose2(kind, items, mes, base=ch):
        if kind == 'menu' and any(x == '교환' for x in items):
            c2.drop()
            raise SystemExit
        return base(kind, items, mes)
    try:
        c2.dialog('@marble', choose2, 25)
    except SystemExit:
        pass
    wait_offline(aid)
    w4 = wallet(aid)
    r4 = reqs(aid)
    c3 = login('gm')
    mes, texts, done = marble(c3, ['요청 취소'])
    w5 = wallet(aid)
    r5 = reqs(aid)
    ok = r4[-1][1] == '1' and w4['pack'] == int(r4[-1][0]) and r5[-1][1] == '4' and r5[-1][3] == '1' and w5['pack'] == 0 and w5['dice'] == w4['dice'] and w5['score'] == w4['score']
    record('X-E2 unconfirmed request cancelled after relogin', 'PASS' if ok else 'FAIL', f'issued {r4[-1]} pack={w4["pack"]}; after cancel {r5[-1]} {w5}')

    # X-E3 hold limit enforced by the server (dice raised to 30 between issue and confirm), score untouched
    ch = menu_picker(['점수 → 주사위 교환', 1])
    def choose3(kind, items, mes, base=ch):
        if kind == 'menu' and any(x == '교환' for x in items):
            cur = wallet(aid)['dice']
            adjust(aid, dice=30 - cur, ticket=3)
            return '교환'
        return base(kind, items, mes)
    mes, texts, done = c3.dialog('@marble', choose3, 25)
    for m in mes:
        log(f'   [gm0 mes] {m}')
    w6 = wallet(aid)
    r6 = reqs(aid)
    ok = r6[-1][1] == '3' and r6[-1][2] == '2' and w6['dice'] == 30 and w6['score'] == w5['score'] and w6['pack'] == 0 and exch_tx(aid, int(r6[-1][0])) == 0
    record('X-E3 hold limit 30 refused, score kept', 'PASS' if ok else 'FAIL', f'req {r6[-1]} wallet {w6}')
    c3.logout()
    inv = invariants()
    record('INV after exchange', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


BOARD = [10, 1, 2, 6, 1, 3, 1, 11, 7, 1, 4, 1, 2, 1, 8, 1, 5, 12, 3, 1, 2, 1, 11, 4, 1, 9, 1, 2, 1, 3, 1, 4, 1]


def wait_state(aid, pred, timeout):
    end = time.time() + timeout
    st = state(aid)
    while time.time() < end:
        st = state(aid)
        if pred(st):
            return st
        time.sleep(0.4)
    return st


def enter_board(c):
    if c.mapname.startswith('need_marbl'):
        return
    marble(c, ['게임판으로 이동'])
    time.sleep(3)
    c.mapname = 'need_marbl.gat'


def one_roll(c, aid, choice_mode):
    """Rolls (or continues) once and waits until the walk ends. Returns a dict of what changed."""
    s0 = state(aid) or {'state': 0, 'pos': 0, 'left': 0, 'die': 0, 'roll': 0, 'steps': 0, 'run': 0, 'jump': 0, 'spass': 0, 'laps': 0, 'run_char': 0}   # no row before the first roll
    w0 = wallet(aid)
    label = '주사위 굴리기' if s0['state'] == 0 else '이동 이어가기'
    marble(c, [label])
    s1 = wait_state(aid, lambda st: st['state'] != 1, 40)
    out = {'from': s0['pos'], 'die': s1['die'], 'roll': s1['roll'], 'to': s1['pos'], 'state': s1['state'],
           'steps': s1['steps'] - s0['steps'], 'dice_used': w0['dice'] - wallet(aid)['dice'], 'label': label}
    if s1['state'] == 4:
        mes, texts, done = marble(c, ['던전 칸', '도전' if choice_mode % 2 else '안전'])
        out['choice'] = '도전' if choice_mode % 2 else '안전'
        out['state'] = state(aid)['state']
    ev = sql(f'SELECT tile,tile_kind,result_code,cp_amount,choice,rng_value FROM need_marble_event WHERE account_id={aid} AND roll_seq={s1["roll"]}')
    out['event'] = ev
    pay = sql(f'SELECT amount,status FROM need_marble_cp_payout WHERE account_id={aid} AND src_type=1 AND src_a={s1["roll"]}')
    out['pay'] = pay
    return out


def cmd_rolls(a):
    aid = AID['gm']
    n = int(a.rest[0]) if a.rest else 25
    c = login('gm')
    enter_board(c)
    rows = []
    for i in range(n):
        r = one_roll(c, aid, i)
        rows.append(r)
        log(f'   roll {i + 1}: {r}')
        if r['state'] not in (0,):
            log('   not idle after roll, stop')
            break
    c.logout()
    json.dump(rows, open(os.path.join(OUT, 'rolls.json'), 'w'), indent=1, ensure_ascii=False)
    # checks per roll
    bad = []
    for r in rows:
        if not r['event'] or len(r['event']) != 1:
            bad.append(('event', r['roll']))
            continue
        tile, kind, res, cp = [int(x) for x in r['event'][0][:4]]
        if kind != BOARD[tile]:
            bad.append(('kind', r['roll']))
        if r['label'] == '주사위 굴리기' and r['dice_used'] != 1:
            bad.append(('dice', r['roll']))
        if kind == 11:
            if r['to'] not in (13, 28) or cp != 0 or r['pay']:
                bad.append(('warp', r['roll']))
        elif (cp > 0) != bool(r['pay']) or (r['pay'] and int(r['pay'][0][0]) != cp):
            bad.append(('pay', r['roll']))
        if r['steps'] != r['die']:
            bad.append(('steps', r['roll']))
    st = state(aid)
    laps = sql(f'SELECT src_a,amount,status FROM need_marble_cp_payout WHERE account_id={aid} AND src_type=2 ORDER BY src_a')
    kinds = {}
    for r in rows:
        if r['event']:
            kinds[int(r['event'][0][1])] = kinds.get(int(r['event'][0][1]), 0) + 1
    dice_hist = {}
    for r in rows:
        dice_hist[r['die']] = dice_hist.get(r['die'], 0) + 1
    record('ROLLS arrival/event/CP per roll', 'PASS' if not bad and rows else 'FAIL',
           f'{len(rows)} rolls, bad={bad[:6]}, kinds={kinds}, dice={dice_hist}')
    ok = len(laps) == st['steps'] // 33 and st['laps'] == len(laps) and all(int(x[1]) == 10000 and x[2] == '1' for x in laps)
    record('LAP 33-step bonus (L1)', 'PASS' if ok else 'FAIL', f'steps={st["steps"]} laps={st["laps"]} lap_rows={laps}')
    cash = sql("SELECT COUNT(*) FROM acc_reg_num WHERE `key`='#CASHPOINTS'")[0][0]
    pays = sql(f'SELECT status,COUNT(*),SUM(amount) FROM need_marble_cp_payout WHERE account_id={aid} GROUP BY status')
    record('V20 CP only DECIDED, #CASHPOINTS untouched', 'PASS' if cash == '0' and all(p[0] == '1' for p in pays) else 'FAIL',
           f'payout by status {pays}, #CASHPOINTS rows={cash}')
    inv = invariants()
    record('INV after rolls', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


def cmd_until_warp(a):
    """V23 warp W1: roll until a warp tile is the final arrival (max N rolls); dungeon choices alternate."""
    aid = AID['gm']
    n = int(a.rest[0]) if a.rest else 25
    w = wallet(aid)
    if w['dice'] < n:
        adjust(aid, dice=min(n, 30) - w['dice'], ticket=int(time.time()))
    c = login('gm')
    enter_board(c)
    warp = None
    rows = []
    for i in range(n):
        j0 = int(sql(f'SELECT COUNT(*) FROM need_marble_jump WHERE account_id={aid}')[0][0])
        r = one_roll(c, aid, i + 1)
        rows.append(r)
        log(f'   roll {i + 1}: from {r["from"]} die {r["die"]} -> {r["to"]} ev {r["event"]}')
        if r['event'] and int(r['event'][0][1]) == 11:
            time.sleep(2)
            jumps = sql(f'SELECT jump_seq,kind,roll_seq,from_pos,to_pos,dropped_steps,start_pass_credit FROM need_marble_jump WHERE account_id={aid} ORDER BY jump_seq')
            st = state(aid)
            log('   warp! jumps', jumps, 'state', st)
            # the character must stand on the destination tile now (script warp after the commit)
            m = c.mark()
            mes, texts, done = marble(c, [])
            warp = (r, jumps, st, j0)
            break
    c.logout()
    if warp is None:
        record('V23 warp W1', 'NOT RUN', f'no warp landing in {len(rows)} rolls')
        return
    r, jumps, st, j0 = warp
    tile = int(r['event'][0][0])
    dest = {7: 13, 22: 28}[tile]
    last = jumps[-1]
    ok = (len(jumps) == j0 + 1 and int(last[1]) == 1 and int(last[2]) == r['roll'] and int(last[3]) == tile and int(last[4]) == dest and
          st['pos'] == dest and st['state'] == 0 and st['jump'] == int(last[0]) and not r['pay'] and int(r['event'][0][2]) == 100 and
          r['steps'] == r['die'])
    record('V23 warp W1 (jump journal, no destination event)', 'PASS' if ok else 'FAIL',
           f'tile {tile} -> {dest}; jump {last}; state pos={st["pos"]} jump_seq={st["jump"]}; event {r["event"]}; steps {r["steps"]}=die {r["die"]}')
    ev_dest = sql(f'SELECT COUNT(*) FROM need_marble_event WHERE account_id={aid} AND roll_seq={r["roll"]}')[0][0]
    record('V23 warp no chain (one event for the roll)', 'PASS' if ev_dest == '1' else 'FAIL', f'events for roll {r["roll"]} = {ev_dest}')
    inv = invariants()
    record('INV after warp', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


def cmd_v07(a):
    """V07: roll with character 1, drop after the first committed step, continue with character 2."""
    aid = AID['gm']
    if wallet(aid)['dice'] < 3:
        adjust(aid, dice=5, ticket=int(time.time()))
    c = login('gm')
    enter_board(c)
    s0, w0 = state(aid), wallet(aid)
    if s0['state'] != 0:
        record('V07', 'FAIL', f'not idle before: {s0}')
        return
    for attempt in range(6):
        marble(c, ['주사위 굴리기'])
        s1 = wait_state(aid, lambda st: st['steps'] >= s0['steps'] + 1, 15)
        if s1['state'] == 1 and s1['left'] > 0:
            break
        s1 = wait_state(aid, lambda st: st['state'] != 1, 30)   # die 1: finished already, roll again
        s0, w0 = state(aid), wallet(aid)
    c.drop()
    wait_offline(aid)
    sd = state(aid)
    wd = wallet(aid)
    log('   after drop', sd, wd)
    c2 = login('gm', 1)
    enter_board(c2)
    marble(c2, ['이동 이어가기'])
    s2 = wait_state(aid, lambda st: st['state'] != 1, 40)
    w2 = wallet(aid)
    ev = sql(f'SELECT COUNT(*) FROM need_marble_event WHERE account_id={aid} AND roll_seq={sd["roll"]}')[0][0]
    rolls = sql(f'SELECT char_id,die FROM need_marble_roll WHERE account_id={aid} AND roll_seq={sd["roll"]}')
    c2.logout()
    ok = (sd['state'] == 1 and sd['left'] > 0 and s2['roll'] == sd['roll'] and w2['dice'] == wd['dice'] == w0['dice'] - 1 and
          s2['run'] > sd['run'] and s2['run_char'] == 150902 and s2['steps'] - s0['steps'] == sd['die'] and ev == '1' and
          rolls[0][0] == '150901')
    record('V07 char switch resumes the same roll', 'PASS' if ok else 'FAIL',
           f'dropped at steps+{sd["steps"] - s0["steps"]}/{sd["die"]} left={sd["left"]} run={sd["run"]}; resumed by 150902 run={s2["run"]} '
           f'state={s2["state"]} total steps={s2["steps"] - s0["steps"]}; dice {w0["dice"]}->{wd["dice"]}->{w2["dice"]}; events={ev}; roll char={rolls}')
    inv = invariants()
    record('INV after V07', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


def cmd_v06_roll(a):
    """V05/V06 part 1: roll, then the operator script kills the isolated map-server right away."""
    aid = AID['gm']
    if wallet(aid)['dice'] < 3:
        adjust(aid, dice=5, ticket=int(time.time()))
    c = login('gm')
    enter_board(c)
    s0, w0 = state(aid), wallet(aid)
    marble(c, ['주사위 굴리기'])
    json.dump({'s0': s0, 'w0': w0, 't': time.time()}, open(os.path.join(OUT, 'v06_before.json'), 'w'))
    log('   rolled, before', s0, w0, 'now', state(aid))
    open(os.path.join(OUT, 'v06_rolled.flag'), 'w').write('1')
    time.sleep(40)
    # keep the session open; the map-server is killed by bc_servers.ps1 -Action killmap


def cmd_v06_check(a):
    """V05/V06 part 2: after restart the roll is either fully there or not at all; continuing spends nothing."""
    aid = AID['gm']
    b = json.load(open(os.path.join(OUT, 'v06_before.json')))
    s0, w0 = b['s0'], b['w0']
    s1, w1 = state(aid), wallet(aid)
    roll_rows = int(sql(f'SELECT COUNT(*) FROM need_marble_roll WHERE account_id={aid} AND roll_seq>{s0["roll"]}')[0][0])
    spend = int(sql(f'SELECT COUNT(*) FROM need_marble_wallet_tx WHERE account_id={aid} AND kind=3 AND ref_a>{s0["roll"]}')[0][0])
    all_in = roll_rows == 1 and spend == 1 and w1['dice'] == w0['dice'] - 1 and s1['roll'] == s0['roll'] + 1
    none_in = roll_rows == 0 and spend == 0 and w1['dice'] == w0['dice'] and s1['roll'] == s0['roll']
    log('   after restart', s1, w1, 'rolls', roll_rows, 'spend', spend)
    record('V06 kill right after roll: all or nothing', 'PASS' if (all_in or none_in) else 'FAIL',
           f'{"committed" if all_in else "not committed" if none_in else "PARTIAL"}: state {s1}, dice {w0["dice"]}->{w1["dice"]}, roll rows {roll_rows}, spend rows {spend}')
    wait_offline(aid, 30)
    c = login('gm')
    enter_board(c)
    if s1['state'] in (1, 2, 3):
        marble(c, ['이동 이어가기'])
        s2 = wait_state(aid, lambda st: st['state'] != 1, 40)
        if s2['state'] == 4:
            marble(c, ['던전 칸', '안전'])
            s2 = state(aid)
    else:
        s2 = s1
    w2 = wallet(aid)
    ev = sql(f'SELECT COUNT(*) FROM need_marble_event WHERE account_id={aid} AND roll_seq={s1["roll"]}')[0][0]
    c.logout()
    ok = w2['dice'] == w1['dice'] and s2['roll'] == s1['roll'] and s2['state'] == 0 and (ev == '1' or not all_in)
    record('V05 continue after restart spends no second die', 'PASS' if ok else 'FAIL',
           f'state {s2}, dice {w1["dice"]}->{w2["dice"]}, events for roll {s1["roll"]} = {ev}')
    inv = invariants()
    record('INV after V06', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


def cmd_v05_continue(a):
    """V05 part 3: second 'continue' from the committed tile finishes the same roll, still no second die."""
    aid = AID['gm']
    s0, w0 = state(aid), wallet(aid)
    c = login('gm')
    enter_board(c)
    marble(c, ['이동 이어가기'])
    s1 = wait_state(aid, lambda st: st['state'] != 1, 40)
    if s1['state'] == 4:
        marble(c, ['던전 칸', '안전'])
        s1 = state(aid)
    w1 = wallet(aid)
    ev = sql(f'SELECT COUNT(*) FROM need_marble_event WHERE account_id={aid} AND roll_seq={s0["roll"]}')[0][0]
    c.logout()
    ok = s0['state'] in (2, 3) and s1['state'] == 0 and s1['roll'] == s0['roll'] and w1['dice'] == w0['dice'] and ev == '1' and s1['steps'] - s0['steps'] == s0['left']
    record('V05 continue after restart spends no second die', 'PASS' if ok else 'FAIL',
           f'before {s0} dice {w0["dice"]}; after {s1} dice {w1["dice"]}; events for roll {s0["roll"]} = {ev}')
    inv = invariants()
    record('INV after V05', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


def inst_clear(c, k):
    harness(c, f'@mbqainst {k}')
    time.sleep(2)
    c.walk(100, 100 + k)
    time.sleep(2)
    t = harness(c, f'@mbqaclear {k}')
    harness(c, '@mbqaleave')
    time.sleep(2)
    return t


def cmd_phase_b(a):
    gm, us = AID['gm'], AID['user']
    # V10 restart: instance ids restart at 1, boot_id keeps the run key distinct
    c = login('gm')
    before = inst_rows(gm)
    t = inst_clear(c, 1)
    after = inst_rows(gm)
    boots = sql('SELECT boot_id FROM need_marble_boot ORDER BY boot_id')
    c.logout()
    new = [r for r in after if r not in before]
    ok = (len(new) == 1 and new[0][0] != before[0][0] and new[0][2] == '10' and new[0][4] == '3' and new[0][3] == '0' and len(boots) == 2)
    record('V10 restart: instance_id reused, boot_id differs, no false ALREADY', 'PASS' if ok else 'FAIL',
           f'boots={boots}; new row {new}; harness {t}')

    # V04 cap (instance cap set to 50 for this boot): exchange does not reopen it; capped clear consumes the slot
    wait_offline(gm)
    u = login('user')
    t1 = inst_clear(u, 1)
    w1 = wallet(us)
    marble(u, ['점수 → 주사위 교환', 1, '교환'])
    w2 = wallet(us)
    t2 = inst_clear(u, 2)
    w3 = wallet(us)
    rows = inst_rows(us)
    per = sql(f'SELECT activity,earned FROM need_marble_period WHERE account_id={us} ORDER BY activity')
    daily = sql(f'SELECT dungeon_code FROM need_marble_dungeon_daily WHERE account_id={us} ORDER BY dungeon_code')
    ok = (w1['score'] == 50 and w2['score'] == 0 and w2['dice'] == 1 and w3['score'] == 0 and
          [r[3:5] for r in rows] == [['50', '1'], ['0', '2']] and per == [['0', '50'], ['2', '50']] and daily == [['7'], ['10']])
    record('V04 cap 50: exchange does not reopen it, capped clear consumes the slot (N1 a)', 'PASS' if ok else 'FAIL',
           f'score {w1} -> exchange {w2} -> 2nd dungeon {w3}; rows {rows}; period {per}; daily {daily}')

    # X-E4 request expires (TTL 20 s for this boot): confirming after expiry changes nothing
    adjust(us, score=50, ticket=int(time.time()))
    w4 = wallet(us)
    ch = menu_picker(['점수 → 주사위 교환', 1])
    def choose(kind, items, mes, base=ch):
        if kind == 'menu' and any(x == '교환' for x in items):
            time.sleep(23)
            return '교환'
        return base(kind, items, mes)
    mes, texts, done = u.dialog('@marble', choose, 60)
    w5 = wallet(us)
    r = reqs(us)
    expired_msg = any('만료' in m for m in mes)
    ok = r[-1][1] == '4' and w5['score'] == w4['score'] and w5['dice'] == w4['dice'] and w5['pack'] == 0 and expired_msg and exch_tx(us, int(r[-1][0])) == 0
    record('X-E4 expired request confirms nothing', 'PASS' if ok else 'FAIL', f'req {r[-1]}; wallet {w4} -> {w5}; message shown={expired_msg}')
    u.logout()
    inv = invariants()
    record('INV after phase B', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


def cmd_phase_b2(a):
    gm, us = AID['gm'], AID['p2']
    u = login('p2')
    # V04 cap (instance cap set to 50 for this boot): exchange does not reopen it; capped clear consumes the slot
    t1 = inst_clear(u, 1)
    w1 = wallet(us)
    marble(u, ['점수 → 주사위 교환', 1, '교환'])
    w2 = wallet(us)
    t2 = inst_clear(u, 2)
    w3 = wallet(us)
    rows = inst_rows(us)
    per = sql(f'SELECT activity,earned FROM need_marble_period WHERE account_id={us} ORDER BY activity')
    daily = sql(f'SELECT dungeon_code FROM need_marble_dungeon_daily WHERE account_id={us} ORDER BY dungeon_code')
    ok = (w1['score'] == 50 and w2['score'] == 0 and w2['dice'] == 1 and w3['score'] == 0 and
          [r[3:5] for r in rows] == [['50', '1'], ['0', '2']] and per == [['0', '50'], ['2', '50']] and daily == [['7'], ['10']])
    record('V04 cap 50: exchange does not reopen it, capped clear consumes the slot (N1 a)', 'PASS' if ok else 'FAIL',
           f'score {w1} -> exchange {w2} -> 2nd dungeon {w3}; rows {rows}; period {per}; daily {daily}')

    # X-E4 request expires (TTL 20 s for this boot): confirming after expiry changes nothing
    adjust(us, score=50, ticket=int(time.time()))
    w4 = wallet(us)
    ch = menu_picker(['점수 → 주사위 교환', 1])
    def choose(kind, items, mes, base=ch):
        if kind == 'menu' and any(x == '교환' for x in items):
            time.sleep(23)
            return '교환'
        return base(kind, items, mes)
    mes, texts, done = u.dialog('@marble', choose, 60)
    w5 = wallet(us)
    r = reqs(us)
    expired_msg = any('만료' in m for m in mes)
    ok = r[-1][1] == '4' and w5['score'] == w4['score'] and w5['dice'] == w4['dice'] and w5['pack'] == 0 and expired_msg and exch_tx(us, int(r[-1][0])) == 0
    record('X-E4 expired request confirms nothing', 'PASS' if ok else 'FAIL', f'req {r[-1]}; wallet {w4} -> {w5}; message shown={expired_msg}')
    u.logout()
    inv = invariants()
    record('INV after phase B', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')



def cmd_v12(a):
    who = a.rest[0] if a.rest else 'p2'
    """V12: a lock held by another DB session makes the exchange fail after innodb_lock_wait_timeout (10 s here):
    nothing is written, the module stays usable, and the map-server thread is blocked meanwhile."""
    import subprocess
    import threading
    us = AID[who]
    s = sec()
    w0, r0 = wallet(us), reqs(us)
    hold = 'START TRANSACTION; SELECT score FROM need_marble_wallet WHERE account_id=%d AND season_id=1 FOR UPDATE; DO SLEEP(18); ROLLBACK;' % us
    th = threading.Thread(target=lambda: bcdb.run(OUT, 'rt', hold))
    th.start()
    time.sleep(1.5)
    u = login(who)
    t0 = time.time()
    mes, texts, done = marble(u, ['점수 → 주사위 교환', 1, '교환'], timeout=40)
    dt = time.time() - t0
    th.join()
    w1, r1 = wallet(us), reqs(us)
    errs = [l for l in open(os.path.join(OUT, 'stack', 'log', 'map-msg_log.log'), encoding='cp949', errors='replace').read().splitlines()
            if 'Lock wait timeout' in l]
    fault = [l for l in open(os.path.join(OUT, 'stack', 'log', 'map-msg_log.log'), encoding='cp949', errors='replace').read().splitlines()
             if 'need_marble' in l and 'fails closed' in l]
    log('   mes', mes[-4:], 'took', round(dt, 1))
    record('V12 lock wait timeout: no partial write', 'PASS' if (w1 == w0 and len(r1) == len(r0) and errs) else 'FAIL',
           f'dialog {round(dt, 1)} s, wallet {w0} -> {w1}, requests {len(r0)} -> {len(r1)}, lock-timeout log lines {len(errs)}, fail-closed lines {len(fault)}')
    # module still usable afterwards
    mes, texts, done = marble(u, ['점수 → 주사위 교환', 1, '교환'])
    w2, r2 = wallet(us), reqs(us)
    record('V12 module usable after the timeout', 'PASS' if (r2[-1][1] == '2' and w2['dice'] == w1['dice'] + 1) else 'FAIL', f'req {r2[-1]} wallet {w2}')
    u.logout()


def cmd_sqlmodel(a):
    """G-6: V09 concurrency model with two DB sessions (same statements as need_marble.cpp) and V02 key limits."""
    import threading
    acc = AID['p4']
    sql(f"INSERT IGNORE INTO need_marble_state (account_id,season_id,updated_at) VALUES ({acc},1,NOW())")
    sql(f"INSERT IGNORE INTO need_marble_wallet (account_id,season_id,updated_at) VALUES ({acc},1,NOW())")
    adjust(acc, dice=1, ticket=int(time.time()))
    out = {}

    def sess(name, q, delay=0):
        time.sleep(delay)
        rc, o, e = bcdb.run(OUT, 'rt', q)
        out[name] = (rc, o.strip(), e.strip()[-200:])

    # (1) two sessions spend the last die at the same time: CAS "dice>=1" lets exactly one through
    spend = ("START TRANSACTION; SELECT dice FROM need_marble_wallet WHERE account_id=%d AND season_id=1 FOR UPDATE; DO SLEEP(%s); "
             "UPDATE need_marble_wallet SET dice=dice-1 WHERE account_id=%d AND season_id=1 AND dice>=1 AND locked=0; SELECT ROW_COUNT(); COMMIT;")
    t1 = threading.Thread(target=sess, args=('A', spend % (acc, 2, acc)))
    t2 = threading.Thread(target=sess, args=('B', spend % (acc, 0, acc), 0.5))
    t1.start(); t2.start(); t1.join(); t2.join()
    rows = (out['A'][1].splitlines()[-1], out['B'][1].splitlines()[-1])
    d = wallet(acc)['dice']
    record('V09 two sessions spend the last die', 'PASS' if sorted(rows) == ['0', '1'] and d == 0 else 'FAIL', f'ROW_COUNT A/B={rows}, dice now {d}')
    adjust(acc, dice=1, ticket=int(time.time()) + 1)   # put the die back through the ledger (I1 stays true)
    sql(f"INSERT INTO need_marble_wallet_tx (account_id,season_id,kind,ref_type,ref_a,ref_b,ref_c,ref_d,char_id,d_score,d_dice,period_day,created_at) "
        f"VALUES ({acc},1,5,5,{int(time.time()) + 2},0,0,0,0,0,-1,0,NOW())")   # the model spend above had no ledger row

    # (2) two sessions resume the same paused roll: run_session CAS lets exactly one through
    st = state(acc)
    run = st['run']
    resume = ("START TRANSACTION; DO SLEEP(%s); UPDATE need_marble_state SET state=1, run_session=%d WHERE account_id=%d AND run_session=%d; "
              "SELECT ROW_COUNT(); COMMIT;")
    out.clear()
    t1 = threading.Thread(target=sess, args=('A', resume % (1, run + 1, acc, run)))
    t2 = threading.Thread(target=sess, args=('B', resume % (0, run + 1, acc, run), 0.3))
    t1.start(); t2.start(); t1.join(); t2.join()
    rows = (out['A'][1].splitlines()[-1], out['B'][1].splitlines()[-1])
    st2 = state(acc)
    record('V09 two sessions resume the same run', 'PASS' if sorted(rows) == ['0', '1'] and st2['run'] == run + 1 else 'FAIL',
           f'ROW_COUNT A/B={rows}, run {run} -> {st2["run"]}')
    sql(f"UPDATE need_marble_state SET state=0 WHERE account_id={acc}")

    # (3) V02 key limits: maximum values in every numeric key column, no truncation warning
    q = ("INSERT INTO need_marble_activity (account_id,season_id,activity,key_a,key_b,key_c,char_id,period_day,requested,credited,outcome,created_at) "
         "VALUES (4294967295,4294967295,255,18446744073709551615,18446744073709551615,18446744073709551615,4294967295,99991231,0,0,2,NOW()); SHOW WARNINGS; "
         "INSERT INTO need_marble_activity (account_id,season_id,activity,key_a,key_b,key_c,char_id,period_day,requested,credited,outcome,created_at) "
         "VALUES (4294967295,4294967295,255,18446744073709551615,18446744073709551615,18446744073709551614,4294967295,99991231,0,0,2,NOW()); SHOW WARNINGS; "
         "SELECT COUNT(*) FROM need_marble_activity WHERE account_id=4294967295; "
         "DELETE FROM need_marble_activity WHERE account_id=4294967295; SELECT ROW_COUNT();")
    rc, o, e = bcdb.run(OUT, 'rt', q)
    lines = o.strip().splitlines()
    record('V02 maximum key values stored without truncation', 'PASS' if rc == 0 and lines[:2] == ['2', '2'] else 'FAIL',
           f'rc={rc} out={lines} err={e.strip()[-120:]}')
    inv = invariants()
    record('INV after SQL model', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


def hunt(aid):
    r = sql(f'SELECT period_day,kills,next_tier FROM need_marble_hunt_progress WHERE account_id={aid}')
    return [[int(x) for x in row] for row in r]


def hunt_rows(aid):
    return sql(f'SELECT key_a,key_b,credited,outcome FROM need_marble_activity WHERE account_id={aid} AND activity=1 ORDER BY key_a,key_b')


def batches(aid):
    return sql(f'SELECT collect_session,batch_no,period_day,kills,char_id FROM need_marble_hunt_batch WHERE account_id={aid} ORDER BY collect_session,batch_no')


def kill(c, radius, n, wait=6):
    c.command('@alive', settle=0.8)   # the dealer must be alive at the kill (a dead top damager credits nobody)
    c.command('@heal', settle=0.8)
    t = harness(c, f'@mbqakill {radius} {n}', needle='[MBQA] kill', wait=wait)
    c.last_counted = None
    try:
        if t and 'counted=' in t:
            c.last_counted = int(t.split('counted=')[1].split()[0])
        return int(t.split('n=')[1].split()[0]) if t else None
    except Exception:
        return None


def to_field(c):
    c.command('@warp yuno_fild02')
    time.sleep(2)
    c.command('@alive')
    c.command('@heal')
    c.command('@monsterignore')   # no deaths mixed into the result (does not refresh idletime)


def kill_total(c, total, radius=0, step=60):
    done = 0
    for _ in range(40):
        if done >= total:
            break
        k = kill(c, radius, min(step, total - done))
        done += max(k or 0, 0)
        if (k or 0) == 0:
            time.sleep(6)   # wait for respawns
    return done


def cmd_v14(a):
    """V14: A 70 kills -> logout -> same account B 40 kills: exactly one tier at the crossing, separate collect sessions."""
    who = a.rest[0] if a.rest else 'user'
    aid = AID[who]
    c = login(who)
    to_field(c)
    ka = kill_total(c, 70)
    c.logout()
    wait_offline(aid)
    h1, b1 = hunt(aid), batches(aid)
    c2 = login(who, 1)
    to_field(c2)
    kb = kill_total(c2, 40)
    c2.logout()
    wait_offline(aid)
    h2, b2, r2 = hunt(aid), batches(aid), hunt_rows(aid)
    sessions = sorted({x[0] for x in b2})
    chars = {x[0]: x[4] for x in b2}
    ok = (ka == 70 and kb == 40 and h1 and h1[0][1] == 70 and h1[0][2] == 1 and h2[0][1] == 110 and h2[0][2] == 2 and
          len(r2) == 1 and r2[0][1] == '1' and r2[0][2] == '25' and len(sessions) == 2 and
          sum(int(x[3]) for x in b2) == 110 and len(set(chars.values())) == 2)
    record('V14 collect sessions: A 70 -> B 40 = one tier', 'PASS' if ok else 'FAIL',
           f'killed A={ka} B={kb}; after A {h1}; after B {h2}; milestones {r2}; sessions {sessions} chars {chars}; batches {len(b2)}')
    inv = invariants()
    record('INV after V14', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


def cmd_v14_crash_kill(a):
    """V14 loss bound part 1: 15 kills (below the 20-kill checkpoint, within 60 s), then the map-server is killed."""
    aid = AID['gm']
    json.dump({'hunt': hunt(aid), 'batches': len(batches(aid))}, open(os.path.join(OUT, 'v14_before.json'), 'w'))
    c = login('gm')
    to_field(c)
    k = kill(c, 0, 15)
    log('   killed', k, 'pending in memory, no checkpoint yet')
    open(os.path.join(OUT, 'v14_killed.flag'), 'w').write(str(k))
    time.sleep(40)


def cmd_v14_crash_check(a):
    aid = AID['gm']
    b = json.load(open(os.path.join(OUT, 'v14_before.json')))
    k = int(open(os.path.join(OUT, 'v14_killed.flag')).read())
    h, nb = hunt(aid), len(batches(aid))
    lost = k - (h[0][1] - b['hunt'][0][1])
    ok = h[0][1] == b['hunt'][0][1] and nb == b['batches'] and 0 <= lost <= 20
    record('V14 crash before checkpoint: loss bounded, no duplicate', 'PASS' if ok else 'FAIL',
           f'killed {k}, kills {b["hunt"][0][1]} -> {h[0][1]}, batches {b["batches"]} -> {nb}, lost {lost} (bound: flush 20 kills / 60 s)')


def cmd_v03(a):
    """V03: tiers at 100/200/300/400 kills, 25 points each, nothing after the 4th; checkpoints stop after the cap."""
    who = a.rest[0] if a.rest else 'gm'
    aid = AID[who]
    c = login(who)
    to_field(c)
    h0 = hunt(aid)
    need = 400 - h0[0][1] + 5
    k = kill_total(c, need)
    time.sleep(2)
    c.logout()
    wait_offline(aid)
    h1, r1, nb1 = hunt(aid), hunt_rows(aid), len(batches(aid))
    w1 = wallet(aid)
    per = sql(f'SELECT activity,earned FROM need_marble_period WHERE account_id={aid} AND activity IN (0,1) ORDER BY activity')
    c = login(who)
    to_field(c)
    k2 = kill_total(c, 40)
    c.logout()
    wait_offline(aid)
    h2, r2, nb2 = hunt(aid), hunt_rows(aid), len(batches(aid))
    ok = (h1[0][2] == 5 and h1[0][1] >= 400 and [x[1] for x in r1] == ['1', '2', '3', '4'] and all(x[2] == '25' for x in r1) and
          per == [['0', '100'], ['1', '100']] and w1['score'] == 100 and r2 == r1 and h2 == h1 and nb2 == nb1 and k2 == 40)
    record('V03 hunt tiers 100/200/300/400 x 25, nothing after the cap', 'PASS' if ok else 'FAIL',
           f'killed {k} then {k2} more; progress {h1} -> {h2}; milestones {r1}; period {per}; score {w1["score"]}; batches {nb1} -> {nb2}')
    inv = invariants()
    record('INV after V03', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


def dist(p, q):
    return max(abs(p[1] - q[1]), abs(p[2] - q[2])) if p and q else None


def cmd_v18(a):
    """V18 (captcha excluded): party P2-active. GM deals, P2 near and active, P3 near and idle > 60 s, P4 far."""
    gm, p2, p3, p4 = AID['gm'], AID['p2'], AID['p3'], AID['p4']
    g = login('gm')
    to_field(g)
    g.command('@monsterignore')
    m2, m3, m4 = login('p2'), login('p3'), login('p4')
    t_login = time.time()
    for c in (m2, m3, m4):
        c.command('@monsterignore')   # atcommands do not refresh idletime (0x7C1F has no ATCOMMAND bit)
    for name in ('MBQA_P2', 'MBQA_P3', 'MBQA_P4'):
        g.command(f'@recall {name}')
    time.sleep(2)
    spot_a = m4.pos
    for tries in range(6):
        g.command('@warp yuno_fild02')
        time.sleep(2)
        g.command('@recall MBQA_P2')
        g.command('@recall MBQA_P3')
        time.sleep(2)
        if dist(m2.pos, spot_a) and dist(m2.pos, spot_a) >= 40:
            break
    log('   positions: P2', m2.pos, 'P3', m3.pos, 'P4', m4.pos, 'dist(P2,P4)=', dist(m2.pos, m4.pos))
    # P3 must be idle for more than 60 s; P2 stays active by walking one cell now and then
    while time.time() - t_login < 70:
        if m2.pos:
            m2.walk(m2.pos[1] + (1 if int(time.time()) % 2 else 0), m2.pos[2])
        time.sleep(15)
    m2.walk(m2.pos[1], m2.pos[2])
    time.sleep(1)
    g.command('@alive')
    g.command('@heal')
    base = {x: hunt(x) for x in (p2, p3, p4)}
    k = 0
    step = 0
    for _ in range(20):
        if k >= 8:
            break
        got = kill(g, 13, 8 - k) or 0
        k += got
        step += 1
        m2.walk(m2.pos[1] + (step % 2), m2.pos[2])   # P2 keeps acting (walk refreshes idletime)
        g.command('@heal')
        if got == 0:
            time.sleep(6)
    for c in (m2, m3, m4, g):
        c.logout()
    for x in (gm, p2, p3, p4):
        wait_offline(x)
    after = {x: hunt(x) for x in (p2, p3, p4)}
    d = lambda x: (after[x][0][1] if after[x] else 0) - (base[x][0][1] if base[x] else 0)
    far = dist(m2.pos, m4.pos)
    ok = k > 0 and d(p2) == k and d(p3) == 0 and d(p4) == 0 and far is not None and far > 24
    record('V18 party P2-active: near+active credited, idle and far not', 'PASS' if ok else 'FAIL',
           f'GM kills {k} (radius 13); P2 +{d(p2)}, P3(idle>60s) +{d(p3)}, P4(distance {far}) +{d(p4)}')
    inv = invariants()
    record('INV after V18', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


def logical_day(t=None):
    """need_marble::logical_day: 04:00 KST boundary, yyyymmdd."""
    t = time.time() if t is None else t
    return int(time.strftime('%Y%m%d', time.gmtime(t + 9 * 3600 - 4 * 3600)))


def secs_to_reset(t=None):
    t = time.time() if t is None else t
    return 86400 - int(t + 9 * 3600 - 4 * 3600) % 86400


def v22_policy(day):
    """Effective hunt policy of the day: latest row per key with effective_day <= day, else the v1.0 default."""
    pol = dict(v22_model.DEFAULT_POLICY)
    keys = ','.join(str(k) for k in v22_model.POLICY_KEYS.values())
    rows = sql(f'SELECT policy_key,value FROM need_marble_policy WHERE policy_key IN ({keys}) AND effective_day<={day} ORDER BY effective_day')
    by_key = {v: k for k, v in v22_model.POLICY_KEYS.items()}
    for k, v in rows:
        pol[by_key[int(k)]] = int(v)
    return pol


def v22_snapshot(aid, day, season=1):
    """Everything V22 compares, read in one go (see v22_model module doc)."""
    q = lambda s: [[int(x) for x in r] for r in sql(s)]
    w = wallet(aid, season) or {'score': 0, 'dice': 0}
    return {
        'day': day,
        'progress': {r[0]: (r[1], r[2]) for r in q(f'SELECT period_day,kills,next_tier FROM need_marble_hunt_progress WHERE account_id={aid} AND season_id={season}')},
        'batches': [tuple(r) for r in q(f'SELECT collect_session,batch_no,period_day,kills FROM need_marble_hunt_batch WHERE account_id={aid} AND season_id={season}')],
        'milestones': [tuple(r) for r in q(f'SELECT key_a,key_b,requested,credited,outcome FROM need_marble_activity WHERE account_id={aid} AND season_id={season} AND activity=1')],
        'earn': [tuple(r) for r in q(f'SELECT ref_b,ref_c,d_score FROM need_marble_wallet_tx WHERE account_id={aid} AND season_id={season} AND kind=1 AND ref_a=1')],
        'tx_count': q(f'SELECT COUNT(*) FROM need_marble_wallet_tx WHERE account_id={aid}')[0][0],
        'period': {(r[0], r[1]): r[2] for r in q(f'SELECT period_day,activity,earned FROM need_marble_period WHERE account_id={aid} AND season_id={season}')},
        'score': w['score'], 'dice': w['dice'],
    }


def alive(c):
    """The caller must be alive before kills are fed: a dead top damager credits nobody (R3 first run)."""
    c.command('@alive', settle=0.8)
    c.command('@heal', settle=0.8)
    t = harness(c, '@mbqastate', needle='[MBQA] state', wait=3)
    try:
        return t is not None and int(t.split('hp=')[1].split()[0]) > 0, t
    except (IndexError, ValueError):
        return False, t


def v22_feed(c, model, n, wait):
    t = harness(c, f'@mbqafeed {n}', needle='[MBQA] feed', wait=wait)
    got, counted = v22_model.parse_feed(t)
    want = model.feed(n)
    return [(f'feed {n} reply', got == n and counted == want, f'{t!r}, expected n={n} counted={want}')]


def v22_fixture(label, who, fixture, day, pol, feeds):
    """One V22 case on a dedicated account. Returns (status, checks). BLOCKED = the precondition did not hold."""
    aid = AID[who]
    s0 = v22_snapshot(aid, day)
    got = v22_model.fixture_state(s0, pol)
    if got != fixture:
        return 'BLOCKED', [(f'{label} precondition', False, f'fixture {fixture} expected, account is {got}: progress {s0["progress"].get(day)}, '
                                                                f'{len(s0["batches"])} batches')]
    checks = [(f'{label} precondition', True, f'fixture {fixture}: progress {s0["progress"].get(day)}, {len(s0["batches"])} batches, '
                                                f'hunt earned {s0["period"].get((day, 1), 0)}, score {s0["score"]}')]
    c = login(who)
    left = c.wait_event(0, 'captcha_image', 4)
    if left is not None:
        # a captcha left pending by an earlier login is shown again at login and blocks commands (to_field too)
        asks, close = captcha_solve(c, left)
        checks.append((f'{label} leftover captcha cleared before the case', close == 2, f'asks {asks}, close {close}'))
        time.sleep(1.5)
    to_field(c)
    ok, t = alive(c)
    checks.append((f'{label} caller alive', ok, repr(t)))
    if not ok:
        c.logout()
        return 'BLOCKED', checks
    m = v22_model.Login(s0, pol)
    prev = s0
    for kind, n in feeds:
        if kind == 'kill':
            k = kill_total(c, n)
            want = m.feed(k)
            checks.append((f'{label} real kills {n}', k == n, f'killed {k}, model counted {want}'))
        else:
            checks += [(f'{label} {x}', ok, d) for x, ok, d in v22_feed(c, m, n, 60 if n > 10000 else 30)]
        cur = v22_snapshot(aid, day)
        checks += v22_model.compare(s0, cur, v22_model.expected(s0, m), pol, f'{label} after {kind} {n}')
        if prev['progress'].get(day, (0, 1))[1] > pol['max_tiers']:
            checks += v22_model.unchanged(prev, cur, f'{label} after {kind} {n}')
        prev = cur
    c.logout()
    wait_offline(aid)
    m.logout()
    s_end = v22_snapshot(aid, day)
    checks += v22_model.compare(s0, s_end, v22_model.expected(s0, m), pol, f'{label} after logout')
    if prev['progress'].get(day, (0, 1))[1] > pol['max_tiers']:
        checks += v22_model.unchanged(prev, s_end, f'{label} after logout')
    return ('PASS' if all(x[1] for x in checks) else 'FAIL'), checks


def cmd_v22(a):
    """V22 (R2) max_tiers and overflow on dedicated accounts (bc_synth_v22.sql), expected rows derived from a
    snapshot taken right before each case (v22_model), never from absolute counts:
      V22-1 mbqa_v22a clean day: feed 30,000 -> 4 tiers, hunt 100, then 5,000 more and logout write nothing
      V22-2 mbqa_v22b one real kill + one batch first: the same feed, every count relative to that row
      V22-3 mbqa_v22a again (capped): relog, 10 real kills + 5,000 fed write nothing
    args: case numbers to run (default 1 2 3), kills=<real kills in V22-3> (default 10; keep it below the
    stack's captcha trigger when bc_stack_extras.py --captcha is on, or the captcha blocks the next command)
    """
    sel = [x for x in a.rest if x in ('1', '2', '3')] or ['1', '2', '3']
    real = next((int(x.split('=')[1]) for x in a.rest if x.startswith('kills=')), 10)
    day = logical_day()
    if secs_to_reset() < 900:
        record('V22 precondition: logical day boundary', 'BLOCKED', f'{secs_to_reset()} s to 04:00 KST, needs 900 s')
        return
    pol = v22_policy(day)
    log(f'   V22 day {day}, policy {pol}')
    allchecks = []
    cases = []
    if '1' in sel:
        st, ch = v22_fixture('V22-1', 'v22a', 'clean', day, pol, [('feed', 30000), ('feed', 5000)])
        cases.append(('V22-1 clean account: 30,000 fed -> 4 tiers, nothing after the cap', st, ch))
    if '2' in sel:
        # fixture 2 setup: exactly one real kill flushed at logout = one batch of one kill
        sb = v22_snapshot(AID['v22b'], day)
        if v22_model.fixture_state(sb, pol) == 'clean':
            c = login('v22b')
            to_field(c)
            ok, t = alive(c)
            k = kill(c, 0, 1) if ok else None
            c.logout()
            wait_offline(AID['v22b'])
            log(f'   V22-2 setup: alive {ok} ({t!r}), killed {k}')
        st, ch = v22_fixture('V22-2', 'v22b', 'prior1', day, pol, [('feed', 30000), ('feed', 5000)])
        cases.append(('V22-2 one prior kill and batch: same feed, counts relative to the snapshot', st, ch))
    if '3' in sel:
        st, ch = v22_fixture('V22-3', 'v22a', 'capped', day, pol, [('kill', real), ('feed', 5000)])
        cases.append((f'V22-3 capped account relog: {real} real kills and 5,000 fed write nothing', st, ch))
    if logical_day() != day:
        cases = [(n, 'BLOCKED', ch + [('logical day unchanged', False, f'{day} -> {logical_day()}')]) for n, _, ch in cases]
    for name, st, ch in cases:
        bad = [f'{x}: {d}' for x, ok, d in ch if not ok]
        record(name, st, f'{sum(1 for x in ch if x[1])}/{len(ch)} checks; ' + ('; '.join(bad[:4]) if bad else 'all checks hold'))
        allchecks += [{'case': name, 'check': x, 'ok': ok, 'detail': d} for x, ok, d in ch]
    name = 'v22_checks.json' if sel == ['1', '2', '3'] else f'v22_checks_{"".join(sel)}_{time.strftime("%H%M%S")}.json'
    json.dump(allchecks, open(os.path.join(OUT, name), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
    inv = invariants()
    record('INV after V22', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


def cmd_guard(a):
    """Guard: the same test map-server started WITHOUT NEED_MARBLE_TEST_HOOKS=1 refuses the commands."""
    aid = AID['gm']
    c = login('gm')
    to_field(c)
    h0 = hunt(aid)
    t = harness(c, '@mbqakill 0 5', needle='[MBQA] kill', wait=6)
    t2 = harness(c, '@mbqafeed 50', needle='[MBQA] feed', wait=6)
    c.logout()
    lines = [l for l in open(os.path.join(OUT, 'stack', 'log', 'map-msg_log.log'), encoding='cp949', errors='replace').read().splitlines()
             if 'need_marble_test' in l]
    ok = t and 'n=-1' in t and t2 and 'n=-1' in t2 and hunt(aid) == h0 and any('refused' in l for l in lines)
    record('Guard: no env -> test commands refused', 'PASS' if ok else 'FAIL', f'{t} / {t2}; log {lines[-2:]}')


def cmd_probe_kill(a):
    """Diagnostic: who / how many kills and whether a captcha (0x0A57 / 0x0A5B family) reached the client."""
    who = a.rest[0] if a.rest else 'gm'
    slot = int(a.rest[1]) if len(a.rest) > 1 else 0
    n = int(a.rest[2]) if len(a.rest) > 2 else 5
    aid = AID[who]
    h0 = hunt(aid)
    c = login(who, slot)
    to_field(c)
    k = kill(c, 0, n)
    time.sleep(1)
    raw = bytes(c.buf)
    cap = {hex(x): raw.count(bytes([x & 0xFF, x >> 8])) for x in (0x0A53, 0x0A55, 0x0A57, 0x0A58, 0x0A5B)}
    c.logout()
    wait_offline(aid)
    h1 = hunt(aid)
    log(f'   probe {who}{slot}: killed {k}, progress {h0} -> {h1}, captcha-like packet ids seen {cap}')


def char_level(cid):
    return [int(x) for x in sql(f'SELECT base_level,base_exp FROM `char` WHERE char_id={cid}')[0]]


def kills_of(aid):
    h = hunt(aid)
    return h[0][1] if h else 0


def cmd_r1(a):
    """F1 R1: one kill levels the dealer from 41 to 42 (hornet lv 11: difference 30 -> 31)."""
    label = a.rest[0] if a.rest else 'run'
    aid, cid = AID['cap'], 150908
    lv0, k0 = char_level(cid), kills_of(aid)
    c = login('cap')
    c.command('@warp prt_fild05')
    time.sleep(3)
    c.command('@monsterignore')
    n = kill(c, 0, 1)
    time.sleep(1)
    c.logout()
    wait_offline(aid)
    lv1, k1 = char_level(cid), kills_of(aid)
    record(f'R1 F1 dealer level-up kill ({label})', 'INFO', f'killed {n}; level {lv0} -> {lv1}; marble kills {k0} -> {k1} (+{k1 - k0})')
    return lv1, k1 - k0


def cmd_r2(a):
    """F1 R2: the dealer's kill levels the party member P2 from 41 to 42 through the even exp share."""
    label = a.rest[0] if a.rest else 'run'
    gm, p2 = AID['gm'], AID['p2']
    lv0, k0 = char_level(150905), kills_of(p2)
    g = login('gm')
    g.command('@warp prt_fild05')
    time.sleep(3)
    g.command('@monsterignore')
    m2 = login('p2')
    m2.command('@monsterignore')
    g.command('@recall MBQA_P2')
    time.sleep(2)
    if m2.pos:
        m2.walk(m2.pos[1] + 1, m2.pos[2])
    time.sleep(1.5)
    n = 0
    for _ in range(12):
        n = kill(g, 13, 1) or 0
        if n:
            break
        if m2.pos:
            m2.walk(m2.pos[1], m2.pos[2])
        time.sleep(5)
    time.sleep(1)
    for c in (m2, g):
        c.logout()
    wait_offline(gm)
    wait_offline(p2)
    lv1, k1 = char_level(150905), kills_of(p2)
    record(f'R2 F1 party member level-up kill ({label})', 'INFO', f'dealer killed {n}; P2 level {lv0} -> {lv1}; P2 marble kills {k0} -> {k1} (+{k1 - k0})')


def cmd_r3(a):
    """F2 R3: a capped account relogs; 10 real kills and 5,000 fed kills write nothing."""
    who = a.rest[0] if a.rest else 'user'
    aid = AID[who]
    h0, b0, r0 = hunt(aid), batches(aid), hunt_rows(aid)
    c = login(who)
    to_field(c)
    k = kill_total(c, 10)
    c.command('@alive', settle=0.8)
    t = harness(c, '@mbqafeed 5000', needle='[MBQA] feed', wait=30)
    c.logout()
    wait_offline(aid)
    h1, b1, r1 = hunt(aid), batches(aid), hunt_rows(aid)
    ok = h0 and h0[0][2] == 5 and k == 10 and t and 'n=5000' in t and h1 == h0 and len(b1) == len(b0) and r1 == r0
    record('R3 F2 capped account relog: no batch / kill count written', 'PASS' if ok else 'FAIL',
           f'progress {h0} -> {h1}; batches {len(b0)} -> {len(b1)}; milestones {len(r0)} -> {len(r1)}; real kills {k}; {t}')
    inv = invariants()
    record('INV after R3', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


def cmd_smoke_board(a):
    """R4 regression smoke: exchange 1 die, enter the board, roll twice (any account with >= 50 score)."""
    who = a.rest[0] if a.rest else 'user'
    aid = AID[who]
    c = login(who)
    w0 = wallet(aid)
    marble(c, ['점수 → 주사위 교환', 1, '교환'])
    w1 = wallet(aid)
    adjust(aid, dice=1, ticket=int(time.time()))
    if not c.mapname.startswith('need_marbl'):
        marble(c, ['게임판으로 이동'])
        time.sleep(3)
        c.mapname = 'need_marbl.gat'
    rows = [one_roll(c, aid, i) for i in range(2)]
    c.logout()
    ok = (w1['score'] == w0['score'] - 50 and w1['dice'] == w0['dice'] + 1 and
          all(r['event'] and len(r['event']) == 1 and r['steps'] == r['die'] for r in rows))
    record('R4 smoke: exchange + 2 rolls', 'PASS' if ok else 'FAIL', f'wallet {w0} -> {w1}; rolls {[(r["from"], r["die"], r["to"], r["event"]) for r in rows]}')
    inv = invariants()
    record('INV after smoke', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


def view_kills(c):
    """Today's marble kill count of c's account (DB + in-memory batch). An atcommand: no idletime refresh."""
    t = harness(c, '@mbqastate', needle='[MBQA] state', wait=3)
    try:
        return int(t.split('kills=')[1].split()[0])
    except (AttributeError, IndexError, ValueError):
        return None


def captcha_answers():
    """(bmp bytes, answer) pairs of the isolated stack's captcha DB (test data inside the stack copy)."""
    stack = os.path.join(OUT, 'stack')
    text = open(os.path.join(stack, 'db', 'import', 'captcha_db.yml'), encoding='utf-8', errors='replace').read()
    out = []
    for fn, ans in re.findall(r'Filename:\s*(\S+)\s*\n\s*Answer:\s*(\S+)', text):
        p = os.path.join(stack, fn)
        if os.path.exists(p):
            out.append((open(p, 'rb').read(), ans))
    return out


def captcha_answer_for(image):
    try:
        bmp = zlib.decompress(image)
    except (zlib.error, TypeError):
        return None
    hit = [a for b, a in captcha_answers() if b == bmp]
    return hit[0] if len(hit) == 1 else None


def captcha_solve(c, image, wrong_first=False):
    """ack -> (wrong answer) -> right answer. Returns (asks, close status)."""
    asks = []
    m = c.mark()
    c.captcha_ack()
    asks.append(c.wait_event(m, 'captcha_ask', 8))
    if wrong_first:
        m = c.mark()
        c.captcha_answer('ZZZZ')
        asks.append(c.wait_event(m, 'captcha_ask', 8))
    ans = captcha_answer_for(image)
    if ans is None:
        return asks, None
    m = c.mark()
    c.captcha_answer(ans)
    return asks, c.wait_event(m, 'captcha_close', 8)


def kill_exact(c, radius, n, between=None, tries=12):
    """Up to n kills by c itself, one call at a time; `between` runs after every call (keeps party members active)."""
    done = counted = 0
    for _ in range(tries):
        if done >= n:
            break
        k = kill(c, radius, n - done) or 0
        done += max(k, 0)
        counted += max(c.last_counted or 0, 0)
        if between:
            between()
        if k <= 0:
            time.sleep(5)
    return done, counted


def killas(g, name, radius, n):
    """GM-driven kills with another character as the damage source (that character may have a captcha up)."""
    g.command(f'#alive {name}', settle=0.6)   # the source must be alive (a dead top damager credits nobody)
    g.command(f'#heal {name}', settle=0.6)
    t = harness(g, f'@mbqakillas {name} {radius} {n}', needle='[MBQA] killas', wait=6)
    try:
        return int(t.split('n=')[1].split()[0]), int(t.split('counted=')[1].split()[0])
    except (AttributeError, IndexError, ValueError):
        return None, None


def killas_exact(g, name, radius, n, tries=8):
    done = counted = 0
    for _ in range(tries):
        if done >= n:
            break
        k, cnt = killas(g, name, radius, n - done)
        done += max(k or 0, 0)
        counted += max(cnt or 0, 0)
        if not k or k <= 0:
            time.sleep(5)
    return done, counted


def viewof(g, name):
    t = harness(g, f'@mbqaviewof {name}', needle='[MBQA] viewof', wait=3)
    try:
        return int(t.split('kills=')[1].split()[0])
    except (AttributeError, IndexError, ValueError):
        return None


def last_event(c, mark, kind):
    hit = [e[2] for e in c.events[mark:] if e[1] == kind]
    return hit[-1] if hit else None


def find_spot(g, need, tries=12):
    """GM warps around yuno_fild02 until at least `need` mobs are in view (about 14 cells). @monster spawns are
    no hunt target (spawn == nullptr), so the party has to go where field mobs are."""
    n = 0
    for _ in range(tries):
        g.mobs.clear()
        g.command('@warp yuno_fild02')
        time.sleep(3)
        n = len([t for t in g.mobs.values() if time.time() - t < 5])
        if n >= need:
            break
    return n


def clear_leftover_captcha(c, res, label):
    """A captcha left pending by an earlier login is shown again after login; answer it before the case."""
    left = c.wait_event(0, 'captcha_image', 5)
    if left is not None:
        asks, close = captcha_solve(c, left)
        res.append((f'{label} leftover captcha cleared first', close == 2, f'asks {asks}, close {close}'))
        time.sleep(1.5)


def cmd_v18c(a):
    """V18-C (G2-A): hunt captcha and support members. Captcha at the 5th kill (bc_stack_extras.py --captcha).
    A character with a captcha on screen cannot type commands, so MBQA_GM1 drives kills (@mbqakillas, damage
    source = that character) and reads kill counts (@mbqaviewof); the captcha itself is answered by the
    character's own client (0x0A5A ack, 0x0A5C answer).
    Part A, dealer MBQA_U1: C1 the kill that raises the captcha (observed only: policy undecided), C2 kills while
    the captcha is pending / the answer window is open, C4 after a wrong answer, C3 after the right answer.
    Part B, party: dealer MBQA_GM1 (own captcha answered first, then on its 1 h cooldown), MBQA_P2 walks then
    gets a captcha, MBQA_HEAL only heals the dealer, MBQA_P3 does nothing for more than 60 s."""
    res = []
    gm, uid = AID['gm'], AID['user']
    g = login('gm')
    clear_leftover_captcha(g, res, 'GM')   # first: a captcha on screen blocks to_field's commands
    to_field(g)
    # ------------------------------------------------------------ part A
    u = login('user')
    clear_leftover_captcha(u, res, 'U1')
    to_field(u)
    k0 = viewof(g, 'MBQA_U1')
    m0 = u.mark()
    first = [killas(g, 'MBQA_U1', 0, 1) for _ in range(4)]
    early = u.wait_event(m0, 'captcha_image', 0.5)
    k4 = viewof(g, 'MBQA_U1')
    res.append(('C1a kills 1-4 counted, no captcha yet', first == [(1, 1)] * 4 and k4 == k0 + 4 and early is None,
                f'kills {first}, view {k0} -> {k4}'))
    n5, c5 = killas(g, 'MBQA_U1', 0, 1)
    img = u.wait_event(m0, 'captcha_image', 10)
    res.append(('C1b 5th kill raises the captcha', n5 == 1 and img is not None, f'kill {n5}, captcha image {len(img) if img else None} bytes'))
    res.append(('C1c the raising kill is counted by the marble (policy undecided: observation only)', None, f'counted {c5}'))
    if img is None:
        for c in (u, g):
            c.logout()
        for x, ok, d in res:
            record(f'V18-C {x}', 'INFO' if ok is None else ('PASS' if ok else 'FAIL'), d)
        record('V18-C part A', 'BLOCKED', 'no captcha raised')
        return
    n_p, c_p = killas_exact(g, 'MBQA_U1', 0, 3)
    res.append(('C2a captcha shown, not answered: kills not counted', n_p > 0 and c_p == 0, f'killed {n_p}, counted {c_p}'))
    m = u.mark()
    u.captcha_ack()
    ask1 = u.wait_event(m, 'captcha_ask', 8)
    n_a, c_a = killas_exact(g, 'MBQA_U1', 0, 2)
    res.append(('C2b answer window open: kills not counted', ask1 is not None and n_a > 0 and c_a == 0, f'ask retry {ask1}, killed {n_a}, counted {c_a}'))
    m = u.mark()
    u.captcha_answer('ZZZZ')
    ask2 = u.wait_event(m, 'captcha_ask', 8)
    n_w, c_w = killas_exact(g, 'MBQA_U1', 0, 2)
    res.append(('C4 after a wrong answer (retry left): kills not counted', ask2 is not None and ask2 < (ask1 or 99) and n_w > 0 and c_w == 0,
                f'ask retry {ask1} -> {ask2}, killed {n_w}, counted {c_w}'))
    ans = captcha_answer_for(img)
    m = u.mark()
    if ans:
        u.captcha_answer(ans)
    close = u.wait_event(m, 'captcha_close', 8)
    time.sleep(1.5)   # macro_checker_success_immunity 1000 ms
    n_r, c_r = killas_exact(g, 'MBQA_U1', 0, 3)
    k_end = viewof(g, 'MBQA_U1')
    res.append(('C3 right answer: counting resumes', ans is not None and close == 2 and n_r > 0 and c_r == n_r,
                f'answer found {ans is not None}, close status {close}, killed {n_r}, counted {c_r}, view {k_end}'))
    u.logout()
    wait_offline(uid)
    h = hunt(uid)
    bsum = sum(int(x[3]) for x in batches(uid))
    want = k0 + 4 + (c5 or 0) + c_r
    res.append(('C-A ledger: kills = counted kills, batch sum = kills', h and h[0][1] == want == k_end and bsum == h[0][1],
                f'progress {h}, batch sum {bsum}, expected {want} (start {k0} + 4 + raising {c5} + after answer {c_r})'))
    # ------------------------------------------------------------ part B
    p2, p3, hl = AID['p2'], AID['p3'], AID['heal']
    mg = g.mark()
    kw, _ = kill_exact(g, 0, 5)
    gimg = g.wait_event(mg, 'captcha_image', 10)
    gasks, gclose = captcha_solve(g, gimg) if gimg else ([], None)
    res.append(('B0 dealer captcha answered first (then 1 h cooldown)', gimg is not None and gclose == 2, f'warm-up kills {kw}, asks {gasks}, close {gclose}'))
    time.sleep(1.5)
    m2, m3, mh = login('p2'), login('p3'), login('heal')
    t_login = time.time()
    for cl, label in ((m2, 'P2'), (m3, 'P3'), (mh, 'HEAL')):
        clear_leftover_captcha(cl, res, label)
        cl.command('@monsterignore')
    for name in ('MBQA_P3', 'MBQA_HEAL'):
        # group 0 characters cannot use @monsterignore themselves; without it they get killed and a dead member
        # is excluded (a GM command on them is server side: no idletime refresh)
        log(f'   #monsterignore {name} ->', g.command(f'#monsterignore {name}'))
    for name in ('MBQA_P2', 'MBQA_P3', 'MBQA_HEAL'):
        g.command(f'@recall {name}')
    time.sleep(2)
    step = [0]

    def keep(p2_walks=True):
        step[0] += 1
        if p2_walks and m2.pos:
            m2.walk(m2.pos[1] + (step[0] % 2), m2.pos[2])
        mh.use_skill(28, 10, gm)

    while time.time() - t_login < 70:   # P3 idle > 60 s; P2 and the healer stay active
        keep()
        time.sleep(10)
    keep()
    log('   positions: GM', g.pos, 'P2', m2.pos, 'P3', m3.pos, 'HEAL', mh.pos)
    names = {'P2': 'MBQA_P2', 'P3': 'MBQA_P3', 'HEAL': 'MBQA_HEAL'}

    def relocate(need):
        """Never while P2 has a captcha up (moving it could cancel / requeue the captcha)."""
        n = find_spot(g, need)
        for name in names.values():
            g.command(f'@recall {name}')
            g.command(f'#alive {name}', settle=0.6)   # every member alive before the phase (dead = excluded)
            g.command(f'#heal {name}', settle=0.6)
        time.sleep(2)
        keep()
        log(f'   relocated: {n} mobs in view at', g.pos)
        return n

    def views():
        return {k: viewof(g, v) for k, v in names.items()}

    def diag(mark):
        """Positions (last map move), healer HP (@mbqastate on its own client: no idletime refresh) and the
        heal results the healer saw since `mark`."""
        t = harness(mh, '@mbqastate', needle='[MBQA] state', wait=3)
        heals = [e[2] for e in mh.events[mark:] if e[1] == 'skill']
        return (f'pos GM {g.pos[1:] if g.pos else None} P2 {m2.pos[1:] if m2.pos else None} P3 {m3.pos[1:] if m3.pos else None} '
                f'HEAL {mh.pos[1:] if mh.pos else None}; healer {t}; heals {len(heals)} {heals[-2:]}')

    def delta(b, e):
        return {k: (e[k] - b[k]) if e[k] is not None and b[k] is not None else None for k in b}

    relocate(6)
    ph = mh.mark()
    b1 = views()
    k1, _ = kill_exact(g, 13, 3, between=keep)
    d1 = delta(b1, views())
    res.append(('B1 healer (heal only) and walking P2 credited, idle P3 not', k1 > 0 and d1 == {'P2': k1, 'P3': 0, 'HEAL': k1},
                f'GM kills {k1}, deltas {d1}; {diag(ph)}'))
    ph = mh.mark()
    relocate(8)
    m2c = m2.mark()
    kp, _ = killas_exact(g, 'MBQA_P2', 0, 5)
    p2img = m2.wait_event(m2c, 'captcha_image', 10)
    t_cap = time.time()
    b2 = views()
    k2, _ = kill_exact(g, 13, 3, between=lambda: keep(p2_walks=False), tries=4)
    log(f'   C2c phase took {time.time() - t_cap:.0f} s (captcha ack timeout 60 s)')
    d2 = delta(b2, views())
    res.append(('C2c party member with a captcha (active < 60 s) not credited; healer still is', p2img is not None and k2 > 0 and
                d2 == {'P2': 0, 'P3': 0, 'HEAL': k2}, f'P2 own kills {kp}, captcha {p2img is not None}, GM kills {k2}, deltas {d2}; {diag(ph)}'))
    p2img = last_event(m2, m2c, 'captcha_image')   # a timed-out captcha comes back with another image
    pasks, pclose = captcha_solve(m2, p2img) if p2img else ([], None)
    time.sleep(1.5)
    ph = mh.mark()
    relocate(5)
    b3 = views()
    k3, _ = kill_exact(g, 13, 3, between=keep)
    d3 = delta(b3, views())
    res.append(('C3b party member after the right answer credited again', pclose == 2 and k3 > 0 and d3 == {'P2': k3, 'P3': 0, 'HEAL': k3},
                f'asks {pasks}, close {pclose}, GM kills {k3}, deltas {d3}; {diag(ph)}'))
    for cl in (m2, m3, mh, g):
        cl.logout()
    for x in (gm, p2, p3, hl):
        wait_offline(x)
    bad = []
    for x in (uid, gm, p2, p3, hl):
        hh = hunt(x)
        bs = sum(int(r[3]) for r in batches(x))
        if (hh[0][1] if hh else 0) != bs:
            bad.append((x, hh, bs))
    res.append(('C-B ledger: every account kills = batch sum (I11)', not bad, f'mismatch {bad}'))
    json.dump([{'check': x, 'ok': ok, 'detail': d} for x, ok, d in res], open(os.path.join(OUT, 'v18c_checks.json'), 'w', encoding='utf-8'),
              indent=1, ensure_ascii=False)
    for x, ok, d in res:
        record(f'V18-C {x}', 'INFO' if ok is None else ('PASS' if ok else 'FAIL'), d)
    inv = invariants()
    record('INV after V18-C', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


def cmd_v11(a):
    """V11 (G2-A): n rolls (default 1,000) on the dedicated account mbqa_v11; dice from operator adjustment rows (kind 5).
    Analysis in v11_model against the policy the server ran with (source defaults + isolated DB policy rows)."""
    n = int(a.rest[0]) if a.rest else 1000
    aid = AID['v11']
    day = logical_day()
    over = {int(k): int(v) for k, v in sql(f'SELECT policy_key,value FROM need_marble_policy WHERE effective_day<={day} ORDER BY effective_day')}
    pol = v11_model.policy_from_source(v11_model.default_logic_path(), over)
    log(f'   V11 policy overrides {over}; warp {pol["warp"]}')
    c = login('v11')
    enter_board(c)
    st0 = state(aid) or {'state': 0, 'pos': 0, 'left': 0, 'die': 0, 'roll': 0, 'steps': 0, 'run': 0, 'jump': 0, 'spass': 0, 'laps': 0, 'run_char': 0}   # the state row is created by the first roll
    if st0['state'] != 0 or st0['roll'] != 0:
        record('V11 precondition', 'BLOCKED', f'account not fresh: {st0}')
        c.logout()
        return
    rows, t0 = [], time.time()
    for i in range(n):
        w = wallet(aid)
        if not w or w['dice'] < 1:
            adjust(aid, dice=25, ticket=900000 + i)
        if not c.alive:
            log('   connection lost, relogin')
            c = login('v11')
            enter_board(c)
        r = one_roll(c, aid, i)
        rows.append(r)
        if r['state'] != 0:
            log(f'   roll {i + 1} left state {r["state"]}, stop')
            break
        if (i + 1) % 50 == 0:
            json.dump(rows, open(os.path.join(OUT, 'v11_rolls.json'), 'w'), ensure_ascii=False)
            log(f'   {i + 1} rolls, {time.time() - t0:.0f} s')
    c.logout()
    wait_offline(aid)
    json.dump(rows, open(os.path.join(OUT, 'v11_rolls.json'), 'w'), ensure_ascii=False)
    st1 = state(aid)
    laps = sql(f'SELECT src_a,amount,status FROM need_marble_cp_payout WHERE account_id={aid} AND src_type=2 ORDER BY src_a')
    pays = sql(f'SELECT status,COUNT(*),SUM(amount) FROM need_marble_cp_payout WHERE account_id={aid} GROUP BY status')
    cash = int(sql("SELECT COUNT(*) FROM acc_reg_num WHERE `key`='#CASHPOINTS'")[0][0])
    res = v11_model.analyze(rows, pol, st1['spass'] - st0['spass'], st1['steps'], laps, pays, cash)
    res.append(('V11-0 roll count', len(rows) == n, f'{len(rows)} of {n} rolls in {time.time() - t0:.0f} s'))
    json.dump([{'check': x, 'ok': ok, 'detail': d} for x, ok, d in res], open(os.path.join(OUT, 'v11_checks.json'), 'w', encoding='utf-8'),
              indent=1, ensure_ascii=False)
    for x, ok, d in res:
        record(x, 'PASS' if ok else 'FAIL', d)
    inv = invariants()
    record('INV after V11', 'PASS' if not inv else 'FAIL', f'{len(inv)} violating rows: {inv[:6]}')


# ---------------------------------------------------------------------------- G2-B (V16 / V21 / V24)
# Needs: map-server built with apply_test_hooks.py --g2b, harness bc_stack_extras.py --kill-hooks --g2b,
# bc_faults_g2b.sql (root) and bc_synth_g2b.sql. Scenarios that need a map-server restart are split into
# phases; the operator restarts with bc_servers.ps1 between phases (this runner never controls a process).

G2B_TABLES = ['need_marble_account_seq', 'need_marble_wallet', 'need_marble_wallet_tx', 'need_marble_period', 'need_marble_activity',
              'need_marble_dungeon_daily', 'need_marble_hunt_progress', 'need_marble_hunt_batch', 'need_marble_exchange_req',
              'need_marble_state', 'need_marble_roll', 'need_marble_jump', 'need_marble_event', 'need_marble_cp_payout']
G2B_GLOBAL = ['need_marble_season', 'need_marble_policy', 'need_marble_boot']
RC = {'OK': 0, 'DB_ERROR': -2, 'ALREADY': -4, 'BAD_ARG': -16, 'INTERNAL': -19, 'CONFLICT': -21}


def acct_snapshot(aid):
    """Every ledger row of one account (all columns), table by table."""
    return {t: sorted(tuple(r) for r in sql(f'SELECT * FROM {t} WHERE account_id={aid}')) for t in G2B_TABLES}


def others_digest(aid):
    """Fingerprint of every ledger row NOT of this account, plus the global tables and #CASHPOINTS rows."""
    import hashlib
    h = {}
    for t in G2B_TABLES:
        rows = sorted(tuple(r) for r in sql(f'SELECT * FROM {t} WHERE account_id<>{aid}'))
        h[t] = (len(rows), hashlib.sha1(repr(rows).encode()).hexdigest()[:12])
    for t in G2B_GLOBAL:
        rows = sorted(tuple(r) for r in sql(f'SELECT * FROM {t}'))
        h[t] = (len(rows), hashlib.sha1(repr(rows).encode()).hexdigest()[:12])
    h['#CASHPOINTS'] = sql("SELECT COUNT(*) FROM acc_reg_num WHERE `key`='#CASHPOINTS'")[0][0]
    return json.loads(json.dumps(h))   # same shape as after a round trip through the phase state file


def diff_tables(a, b):
    return [t for t in a if a[t] != b.get(t)]


def open_trx():
    """Transactions open on the isolated server right now (map-server connection included)."""
    return int(bcdb.query(OUT, 'SELECT COUNT(*) FROM information_schema.innodb_trx', who='root')[0][0])


def arm(point, aid):
    sql(f"INSERT IGNORE INTO mbqa_fault (point, account_id) VALUES ('{point}', {aid})")


def disarm(aid):
    sql(f'DELETE FROM mbqa_fault WHERE account_id={aid}')


def flags(c):
    t = harness(c, '@mbqaflags', needle='[MBQA] flags', wait=3)
    out = {}
    for part in (t or '').split()[2:]:
        if '=' in part:
            k, v = part.split('=', 1)
            try:
                out[k] = int(v)
            except ValueError:
                pass
    return out


def feed(c, n, wait=8):
    t = harness(c, f'@mbqafeed {n}', needle='[MBQA] feed', wait=wait)
    return v22_model.parse_feed(t)


def clock_to(c, kst):
    """Moves the test clock so that the server now reads `kst` ('2026-11-01 03:59:50', KST). Returns the logical day."""
    import calendar
    utc = calendar.timegm(time.strptime(kst, '%Y-%m-%d %H:%M:%S')) - 9 * 3600
    t = harness(c, f'@mbqaclock {utc}', needle='[MBQA] clock', wait=3)   # absolute: the server computes the offset
    try:
        return int(t.split('day=')[1].split()[0])
    except (AttributeError, IndexError, ValueError):
        return None


def replay(c, session, batch, spec):
    t = harness(c, f'@mbqareplay {session} {batch} {spec}', needle='[MBQA] replay', wait=6)
    try:
        return int(t.split('rc=')[1].split()[0]), int(t.split('credited=')[1].split()[0])
    except (AttributeError, IndexError, ValueError):
        return None, None


def map_log_pos():
    """Sizes of the current map-server stdout (ShowWarning), stderr (ShowError / [SQL] errors) and map-msg_log.log.
    SQL errors such as the fault SIGNALs, lock wait timeouts and deadlocks never reach stdout."""
    files = []
    for ext in ('.out', '.err'):
        logs = sorted((p for p in os.listdir(os.path.join(OUT, 'logs')) if p.startswith('map-server.') and p.endswith(ext)),
                      key=lambda p: os.path.getmtime(os.path.join(OUT, 'logs', p)))
        if logs:
            files.append(os.path.join(OUT, 'logs', logs[-1]))
    msg = os.path.join(OUT, 'stack', 'log', 'map-msg_log.log')
    if os.path.exists(msg):
        files.append(msg)
    return [(p, os.path.getsize(p)) for p in files]


def map_log_since(pos):
    out = []
    for p, n in pos:
        with open(p, 'rb') as f:
            f.seek(n)
            out.append(f.read().decode('cp949', 'replace'))
    return '\n'.join(out)


def g2b_state(name, data=None):
    p = os.path.join(OUT, f'{name}_state.json')
    if data is None:
        return json.load(open(p, encoding='utf-8'))
    json.dump(data, open(p, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)


def hunt_day(aid, day):
    r = sql(f'SELECT kills,next_tier FROM need_marble_hunt_progress WHERE account_id={aid} AND period_day={day}')
    return (int(r[0][0]), int(r[0][1])) if r else (0, 1)


def batch_rows(aid):
    return [tuple(int(x) for x in r) for r in
            sql(f'SELECT collect_session,batch_no,period_day,kills FROM need_marble_hunt_batch WHERE account_id={aid} ORDER BY collect_session,batch_no,period_day')]


def ledger_check(aid, day, kills, label):
    """One day of hunting: progress = expected kills = batch sum (I11), tiers = min(kills // 100, 4) (I10),
    hunt earned = 25 x tiers = EARN rows sum = wallet score part; plus the invariant SQL."""
    k, nt = hunt_day(aid, day)
    bsum = sum(r[3] for r in batch_rows(aid) if r[2] == day)
    tiers = min(kills // 100, 4)
    ms = sql(f'SELECT COUNT(*),COALESCE(SUM(credited),0) FROM need_marble_activity WHERE account_id={aid} AND activity=1 AND key_a={day}')[0]
    earn = sql(f'SELECT COUNT(*),COALESCE(SUM(d_score),0) FROM need_marble_wallet_tx WHERE account_id={aid} AND kind=1 AND ref_a=1 AND ref_b={day}')[0]
    per = sql(f'SELECT COALESCE(SUM(earned),0) FROM need_marble_period WHERE account_id={aid} AND period_day={day} AND activity=1')[0][0]
    inv = invariants()
    ok = (k == kills == bsum and nt == tiers + 1 and ms == [str(tiers), str(25 * tiers)] and earn == [str(tiers), str(25 * tiers)] and
          int(per) == 25 * tiers and not inv)
    return (label, ok, f'progress {k} (expected {kills}), batch sum {bsum}, next_tier {nt}, tiers rows {ms}, EARN {earn}, hunt earned {per}, '
                       f'invariant rows {len(inv)}')


def g2b_record(prefix, res, state_name=None):
    for x, ok, d in res:
        record(f'{prefix} {x}', 'INFO' if ok is None else ('PASS' if ok else 'FAIL'), d)
    if state_name:
        p = os.path.join(OUT, f'{state_name}_checks.json')
        old = json.load(open(p, encoding='utf-8')) if os.path.exists(p) else []
        json.dump(old + [{'check': x, 'ok': ok, 'detail': d} for x, ok, d in res], open(p, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)


def v16_fault_case(c, aid, day, st, label, point=None, holder=None):
    """A checkpoint (20 fed kills) fails at `point` (trigger) or on a lock held by `holder`; every ledger row of the
    account must stay as it was, nothing else may change, no transaction may stay open, the batch stays frozen in
    memory. Then 20 more kills resend the frozen batch plus a new one: both applied exactly once."""
    s0, o0 = acct_snapshot(aid), others_digest(aid)
    pos = map_log_pos()
    if point:
        arm(point, aid)
    if holder:
        holder()
    t0 = time.time()
    n, counted = feed(c, 20, wait=16)
    took = time.time() - t0
    f = flags(c)
    s1 = acct_snapshot(aid)
    if point:
        disarm(aid)
    trx = open_trx()
    txt = map_log_since(pos)
    want = f'mbqa fault {point}' if point else 'Lock wait timeout'
    # The first flush of a login issues the collect session in its own transaction before the checkpoint (design 7.1):
    # need_marble_account_seq may change by exactly collect_session + 1 and nothing else; every other table is identical.
    seq0, seq1 = s0['need_marble_account_seq'], s1['need_marble_account_seq']
    seq_ok = seq1 == seq0 or (len(seq1) == 1 and len(seq0) <= 1 and
                              int(seq1[0][1]) == (int(seq0[0][1]) if seq0 else 0) + 1 and
                              seq1[0][2:] == (seq0[0][2:] if seq0 else ('0',) * len(seq1[0][2:])))
    rest = [t for t in diff_tables(s0, s1) if t != 'need_marble_account_seq']
    res = [(f'{label} failed checkpoint writes nothing', not rest and seq_ok and others_digest(aid) == o0 and n == 20,
            f'changed tables {diff_tables(s0, s1)} (account_seq {seq0} -> {seq1}); feed n={n} counted={counted}; {took:.1f} s')]
    res.append((f'{label} no open transaction, module still up, batch kept', f.get('in_tx') == 0 and f.get('fault') == 0 and f.get('frozen', 0) > 0 and
                (trx == 0 or holder is not None), f'flags {f}; open transactions {trx}; log has {want!r}: {want in txt}'))
    return res


def v16_deadlock_case(c, aid, day, label):
    """V16 deadlock attempt: the external transaction writes 2,000 pad rows, takes the next batch key, then asks for
    the progress row the checkpoint holds; InnoDB rolls back the lighter transaction (expected: the checkpoint)."""
    res = []
    f = flags(c)
    sess = bcdb.open_session(OUT)
    sess.stdin.write((f'START TRANSACTION; INSERT INTO mbqa_pad (v) SELECT 1 FROM information_schema.columns LIMIT 2000; '
                      f'INSERT INTO need_marble_hunt_batch (account_id,collect_session,batch_no,period_day,season_id,kills,char_id,created_at) '
                      f'VALUES ({aid},{f.get("session")},{f.get("batch", 0) + 1},{day},1,0,0,NOW()); SELECT \'held\';\n').encode())
    sess.stdin.flush()
    for _ in range(5):
        if b'held' in sess.stdout.readline():
            break
    s0, o0, pos = acct_snapshot(aid), others_digest(aid), map_log_pos()
    import threading
    th = threading.Thread(target=lambda: feed(c, 20, wait=16))
    th.start()
    time.sleep(2)
    sess.stdin.write(f'SELECT kills FROM need_marble_hunt_progress WHERE account_id={aid} AND period_day={day} FOR UPDATE; SELECT \'second\';\n'.encode())
    sess.stdin.flush()
    th.join(30)
    out_ext, err_ext = sess.communicate(input=b'ROLLBACK;\n', timeout=20)
    txt = map_log_since(pos)
    victim = 'checkpoint' if 'Deadlock' in txt else ('external' if b'Deadlock' in err_ext else 'none')
    s1 = acct_snapshot(aid)
    pad = sql('SELECT COUNT(*) FROM mbqa_pad')[0][0]
    if victim == 'checkpoint':
        res.append((f'{label} deadlock: checkpoint rolled back as a whole', s1 == s0 and others_digest(aid) == o0 and pad == '0' and open_trx() == 0,
                    f'changed {diff_tables(s0, s1)}; pad rows {pad}'))
    else:
        res.append((f'{label} deadlock: checkpoint path not exercised (victim ' + victim + ')', None, f'external stderr {err_ext[-200:]!r}'))
    return res


def cmd_v16a(a):
    """V16 phase a (G2-B): fault points on one account, in the real order of hunt_checkpoint / credit_in_tx.
    kills: 0 -[progress_update]-> 40 -[lock wait]-> 80 -[activity_update, tier 1]-> 120 -> 180 -[wallet_update, tier 2]-> 220
           -[deadlock attempt]-> 260 -> 280 -[period_update, tier 3]-> 320, then the COMMIT case of phase b."""
    aid = AID['v16']
    c = login('v16')
    to_field(c)
    ok, t = alive(c)
    day = flags(c).get('day')
    st = {'day': day, 'exp': 0, 'others': others_digest(aid)}
    res = [('precondition: alive, clean account', ok and acct_snapshot(aid)['need_marble_hunt_progress'] == [], repr(t))]
    # C1 progress_update without a tier
    res += v16_fault_case(c, aid, day, st, 'C1 progress_update', point='progress_update')
    feed(c, 20)
    st['exp'] = 40
    res.append(ledger_check(aid, day, st['exp'], 'C1 resend applied once'))
    # C8 lock wait timeout on the progress row (innodb_lock_wait_timeout 10 s, bc_faults_g2b.sql)
    sess = bcdb.open_session(OUT)

    def hold():
        sess.stdin.write(f'START TRANSACTION; SELECT kills FROM need_marble_hunt_progress WHERE account_id={aid} AND period_day={day} FOR UPDATE; SELECT \'held\';\n'.encode())
        sess.stdin.flush()
        for _ in range(5):
            if b'held' in sess.stdout.readline():
                break
    res += v16_fault_case(c, aid, day, st, 'C8 lock wait timeout', holder=hold)
    sess.stdin.write(b'ROLLBACK;\n')
    sess.stdin.close()
    sess.wait(10)
    feed(c, 20)
    st['exp'] = 80
    res.append(ledger_check(aid, day, st['exp'], 'C8 resend applied once'))
    # C2 activity_update at tier 1, C3 wallet_update at tier 2
    res += v16_fault_case(c, aid, day, st, 'C2 activity_update (tier 1)', point='activity_update')
    feed(c, 20)
    st['exp'] = 120
    res.append(ledger_check(aid, day, st['exp'], 'C2 resend applied once'))
    feed(c, 60)
    res += v16_fault_case(c, aid, day, st, 'C3 wallet_update (tier 2)', point='wallet_update')
    feed(c, 20)
    st['exp'] = 220
    res.append(ledger_check(aid, day, st['exp'], 'C3 resend applied once'))
    res += v16_deadlock_case(c, aid, day, 'C9')
    feed(c, 20)
    st['exp'] = 260
    res.append(ledger_check(aid, day, st['exp'], 'C9 after the deadlock attempt: applied once'))
    # C4 period_update at tier 3
    feed(c, 20)
    res += v16_fault_case(c, aid, day, st, 'C4 period_update (tier 3)', point='period_update')
    feed(c, 20)
    st['exp'] = 320
    res.append(ledger_check(aid, day, st['exp'], 'C4 resend applied once'))
    # C10 COMMIT outcome hidden, really rolled back: the module must fail closed (every entry point refused)
    s0, pos = acct_snapshot(aid), map_log_pos()
    harness(c, '@mbqacommitfault 1', needle='[MBQA] commitfault', wait=3)
    feed(c, 20)
    f = flags(c)
    n2, counted2 = feed(c, 20)
    res.append(('C10 hidden COMMIT (rolled back): module fails closed, nothing written',
                f.get('fault') == 1 and acct_snapshot(aid) == s0 and counted2 == 0 and 'COMMIT outcome hidden' in map_log_since(pos),
                f'flags {f}; feed after fault counted={counted2}'))
    g2b_state('v16', st)
    g2b_record('V16', res, 'v16')
    c.logout()
    log('   V16 phase a done: restart the map-server (bc_servers.ps1 killmap / startmap -TestHooks), then run v16b')


def cmd_v16b(a):
    """V16 phase b: after the restart the fault flag is gone and the rolled-back batch is lost (bounded, design 7.5);
    then a COMMIT that really committed is reported as failed (module fails closed again)."""
    aid = AID['v16']
    st = g2b_state('v16')
    c = login('v16')
    to_field(c)
    alive(c)
    f = flags(c)
    res = [('C10 after restart: module up, kills = before the hidden COMMIT (lost 20, no duplicate)', f.get('fault') == 0, f'flags {f}'),
           ledger_check(aid, st['day'], st['exp'], 'C10 ledger after restart')]
    # the first COMMIT of a login is the collect-session transaction (hunt_issue_session): one normal checkpoint first,
    # so the armed hidden COMMIT lands on a hunt checkpoint (attempt 1 armed it too early, kept in v16_checks.json)
    feed(c, 20)
    st['exp'] += 20
    res.append(ledger_check(aid, st['day'], st['exp'], 'C11 setup: one normal checkpoint issues the collect session'))
    pos = map_log_pos()
    harness(c, '@mbqacommitfault 2', needle='[MBQA] commitfault', wait=3)
    feed(c, 20)
    f = flags(c)
    st['exp'] += 20
    st['c11'] = {'session': f.get('session'), 'batch': f.get('frozen'), 'spec': f"{st['day']}:20"}
    res.append(('C11 hidden COMMIT (committed): module fails closed, rows written once',
                f.get('fault') == 1 and f.get('frozen', 0) > 0 and 'COMMIT outcome hidden' in map_log_since(pos), f'flags {f}'))
    res.append(ledger_check(aid, st['day'], st['exp'], 'C11 ledger right after the committed batch'))
    g2b_state('v16', st)
    g2b_record('V16', res, 'v16')
    c.logout()
    log('   V16 phase b done: restart the map-server again, then run v16c')


def cmd_v16c(a):
    """V16 phase c: the same batch resent after the restart is ALREADY; tier 4 through progress_update; the
    re-entry and out-of-transaction probes; nothing outside the account changed during the whole of V16."""
    aid = AID['v16']
    st = g2b_state('v16')
    day = st['day']
    c = login('v16')
    to_field(c)
    alive(c)
    s0 = acct_snapshot(aid)
    rc, cr = replay(c, st['c11']['session'], st['c11']['batch'], st['c11']['spec'])
    res = [('C11 same batch resent after the restart: ALREADY, nothing written', rc == RC['ALREADY'] and acct_snapshot(aid) == s0,
            f'replay {st["c11"]} -> rc {rc}, credited {cr}'),
           ledger_check(aid, day, st['exp'], 'C11 ledger after the resend')]
    # C9r / C5 on the next logical day (test clock), so fresh tiers are left: the first feed issues this login's
    # collect session (the deadlock needs the next batch key), 0 -> 20; 20 -> 40 is the deadlocked checkpoint, resent
    # by the 60 s flush timer alone; 40 -> 80; 80 -> 100 fails at progress_update after the tier 1 period counters.
    import datetime
    nd = datetime.date(day // 10000, day // 100 % 100, day % 100) + datetime.timedelta(days=1)
    day2 = clock_to(c, f'{nd.isoformat()} 12:00:00')
    st['day2'] = day2
    feed(c, 20)
    res += v16_deadlock_case(c, aid, day2, 'C9r')
    end = time.time() + 120
    while time.time() < end and hunt_day(aid, day2)[0] < 40:
        time.sleep(3)
    res.append(ledger_check(aid, day2, 40, 'C9r frozen batch resent by the flush timer: applied once'))
    feed(c, 40)
    res += v16_fault_case(c, aid, day2, st, 'C5 progress_update (tier 1 of the next day, after the period counters)', point='progress_update')
    feed(c, 20)
    res.append(ledger_check(aid, day2, 120, 'C5 resend applied once'))
    res.append(ledger_check(aid, day, st['exp'], 'first day unchanged by the next day'))
    s0, pos = acct_snapshot(aid), map_log_pos()
    t = harness(c, '@mbqareentry', needle='[MBQA] reentry', wait=3)
    t2 = harness(c, '@mbqaoutoftx', needle='[MBQA] outoftx', wait=3)
    txt = map_log_since(pos)
    f = flags(c)
    res.append(('C6 entry point inside an open transaction refused, nothing written',
                t is not None and f'rc={RC["DB_ERROR"]}' in t and 'nested transaction refused' in txt and acct_snapshot(aid) == s0 and f.get('in_tx') == 0,
                f'{t}; log nested refused: {"nested transaction refused" in txt}; flags in_tx={f.get("in_tx")}'))
    res.append(('C7 internal function outside a transaction refused, nothing written',
                t2 is not None and f'rc={RC["INTERNAL"]}' in t2 and 'called outside a transaction' in txt and acct_snapshot(aid) == s0,
                f'{t2}; log outside-tx: {"called outside a transaction" in txt}'))
    res.append(('C-all other accounts / global tables / #CASHPOINTS unchanged over V16', others_digest(aid) == st['others'],
                f'changed {diff_tables(st["others"], others_digest(aid))}'))
    res.append(('C-all no open transaction at the end', open_trx() == 0, ''))
    g2b_state('v16', st)
    g2b_record('V16', res, 'v16')
    c.logout()


def v21_days():
    """V21 runs on fixed future logical days so it never meets the V24 dates."""
    return [20261120, 20261121, 20261122, 20261123]


def kst_noon(day):
    return f'{day // 10000}-{day // 100 % 100:02d}-{day % 100:02d} 12:00:00'


def cmd_v21a(a):
    """V21 phase a (G2-B): a batch over three logical days, the next batch, exact resend, resend with other content,
    then a COMMIT reported as failed although it committed. Test clock: D0..D3 = 2026-11-20..23 noon KST."""
    aid = AID['v21']
    D0, D1, D2, D3 = v21_days()
    c = login('v21')
    to_field(c)
    ok, t = alive(c)
    st = {'others': others_digest(aid)}
    res = [('precondition: alive, clean account', ok and batch_rows(aid) == [], repr(t))]
    days = [clock_to(c, kst_noon(D0))]
    arm('progress_update', aid)
    feed(c, 20)            # batch 1 {D0: 20} fails, stays frozen
    feed(c, 5)             # pending {D0: 5}
    days.append(clock_to(c, kst_noon(D1)))
    feed(c, 5)
    days.append(clock_to(c, kst_noon(D2)))
    feed(c, 5)
    f = flags(c)
    res.append(('setup: pending spans three logical days while batch 1 is frozen', days == [D0, D1, D2] and f.get('frozen') == 1 and
                f.get('pending') == 15 and f.get('pdays') == 3, f'days {days}; flags {f}'))
    disarm(aid)
    feed(c, 5)             # resend batch 1, then batch 2 {D0: 5, D1: 5, D2: 10}
    S = f.get('session')
    feed(c, 20)            # batch 3 {D2: 20}
    want = [(S, 1, D0, 20), (S, 2, D0, 5), (S, 2, D1, 5), (S, 2, D2, 10), (S, 3, D2, 20)]
    got = batch_rows(aid)
    res.append(('V21-1 batch over three periods + following batch: one row per (session, batch, period)', got == want,
                f'rows {got}'))
    res.append(('V21-1 progress per period = batch sum (I11)', [hunt_day(aid, d)[0] for d in (D0, D1, D2)] == [25, 5, 30],
                f'progress {[hunt_day(aid, d) for d in (D0, D1, D2)]}'))
    s0 = acct_snapshot(aid)
    rc, _ = replay(c, S, 2, f'{D0}:5,{D1}:5,{D2}:10')
    res.append(('V21-2 exact resend of batch 2: ALREADY, nothing written', rc == RC['ALREADY'] and acct_snapshot(aid) == s0, f'rc {rc}'))
    pos = map_log_pos()
    rc2, _ = replay(c, S, 2, f'{D0}:6,{D1}:5,{D2}:10')
    s2 = acct_snapshot(aid)
    f2 = flags(c)
    # G2-B2: the conflict must be named (CONFLICT -21) and logged; any other answer is still a FAIL
    detected = rc2 == RC['CONFLICT'] and 'hunt batch conflict' in map_log_since(pos)
    res.append(('V21-3 same key, different kills: mismatch detected and fail-closed (request 4.3)', detected and s2 == s0,
                f'rc {rc2}; flags fault={f2.get("fault")}; rows changed {diff_tables(s0, s2)}; log {map_log_since(pos)[-160:]!r}'))
    pos = map_log_pos()
    rc3, _ = replay(c, S, 3, f'{D2}:20,{D3}:7')
    s3 = acct_snapshot(aid)
    f3 = flags(c)
    detected = rc3 == RC['CONFLICT'] and 'hunt batch conflict' in map_log_since(pos)
    res.append(('V21-4 same key with an extra period: mismatch detected and fail-closed (request 4.3)', detected and s3 == s0,
                f'rc {rc3}; flags fault={f3.get("fault")}; D3 progress {hunt_day(aid, D3)}; rows changed {diff_tables(s0, s3)}'))
    # V21-4b the new period sorts first: its row is written, then the duplicate period ends the batch as ALREADY
    # (the transaction rolls back, but the caller treats ALREADY as "applied" and would drop those kills)
    pos = map_log_pos()
    rc4, _ = replay(c, S, 3, f'{D1}:4,{D2}:20')
    s4 = acct_snapshot(aid)
    f5 = flags(c)
    detected = rc4 == RC['CONFLICT'] and 'hunt batch conflict' in map_log_since(pos)
    res.append(('V21-4b same key, new period first: mismatch detected and fail-closed (request 4.3)', detected and s4 == s0,
                f'rc {rc4}; flags fault={f5.get("fault")}; D1 progress {hunt_day(aid, D1)}; rows changed {diff_tables(s0, s4)}'))
    if s2 != s0 or s3 != s0 or s4 != s0:
        # a conflicting resend wrote ledger rows: stop here (G2-B1 instruction), keep the data for analysis
        g2b_state('v21', st)
        g2b_record('V21', res + [('STOP conflicting resend changed the ledger', False,
                                  f'changed {diff_tables(s0, s2)} / {diff_tables(s0, s3)} / {diff_tables(s0, s4)}')], 'v21')
        c.logout()
        return
    harness(c, '@mbqacommitfault 2', needle='[MBQA] commitfault', wait=3)
    feed(c, 20)            # batch 4 {D2: 20} commits, the ledger is told it failed
    f4 = flags(c)
    st.update({'S': S, 'b4': f4.get('frozen'), 'D2': D2})
    res.append(('V21-5 COMMIT reported failed although committed: module fails closed', f4.get('fault') == 1 and hunt_day(aid, D2)[0] == 50,
                f'flags {f4}; D2 progress {hunt_day(aid, D2)}'))
    g2b_state('v21', st)
    g2b_record('V21', res, 'v21')
    c.logout()
    log('   V21 phase a done: restart the map-server, then run v21b')


def cmd_v21b(a):
    """V21 phase b: resend of the committed batch after the restart, a late batch of the old collect session,
    character B (no dice ever rolled) with a new collect session, then kills left in memory for a forced stop."""
    aid = AID['v21']
    st = g2b_state('v21')
    D2 = st['D2']
    c = login('v21')
    to_field(c)
    alive(c)
    clock_to(c, kst_noon(D2))
    s0 = acct_snapshot(aid)
    rc, _ = replay(c, st['S'], st['b4'], f'{D2}:20')
    res = [('V21-5 committed batch resent after the restart: ALREADY, D2 = 50 once', rc == RC['ALREADY'] and acct_snapshot(aid) == s0 and
            hunt_day(aid, D2)[0] == 50, f'rc {rc}; D2 {hunt_day(aid, D2)}')]
    rc, _ = replay(c, st['S'], 99, f'{D2}:3')
    res.append(('V21-6 late batch of the old collect session applied (design 7.4)', rc == RC['OK'] and hunt_day(aid, D2)[0] == 53,
                f'rc {rc}; D2 {hunt_day(aid, D2)}'))
    c.logout()
    wait_offline(aid)
    seq0 = int(sql(f'SELECT collect_session FROM need_marble_account_seq WHERE account_id={aid}')[0][0])
    b = login('v21', 1)
    to_field(b)
    alive(b)
    clock_to(b, kst_noon(D2))
    feed(b, 25)
    SB = flags(b).get('session')
    b.logout()
    wait_offline(aid)
    rows_b = [r for r in batch_rows(aid) if r[0] == SB]
    res.append(('V21-7 character B: new collect session, batches 1 (20) and 2 (5 at logout)', SB is not None and SB > st['S'] and SB >= seq0 and
                rows_b == [(SB, 1, D2, 20), (SB, 2, D2, 5)] and hunt_day(aid, D2)[0] == 78, f'session {SB}; rows {rows_b}; D2 {hunt_day(aid, D2)}'))
    res.append(('V21-7 no dice were ever rolled on this account', sql(f'SELECT COUNT(*) FROM need_marble_roll WHERE account_id={aid}')[0][0] == '0', ''))
    st['before_kill'] = hunt_day(aid, D2)[0]
    g2b_state('v21', st)
    g2b_record('V21', res, 'v21')
    b = login('v21', 1)
    to_field(b)
    alive(b)
    clock_to(b, kst_noon(D2))
    # V21-7b (G2-B3) the account-wide batch key keeps its identity after a restart, relogs and a character
    # switch: character B resends A's three-day batch 2 and B's own batch 1
    D0, D1 = v21_days()[0], v21_days()[1]
    s7, pos = acct_snapshot(aid), map_log_pos()
    rc_a, _ = replay(b, st['S'], 2, f'{D0}:5,{D1}:5,{D2}:10')
    rc_b, _ = replay(b, st['S'], 2, f'{D0}:5,{D1}:6,{D2}:10')
    rc_c, _ = replay(b, SB, 1, f'{D2}:21')
    g2b_record('V21', [("V21-7b after restart, relog and character switch: A's batch 2 same content ALREADY, other content CONFLICT, "
                        "B's batch 1 other content CONFLICT, nothing written",
                        rc_a == RC['ALREADY'] and rc_b == RC['CONFLICT'] and rc_c == RC['CONFLICT'] and acct_snapshot(aid) == s7 and
                        map_log_since(pos).count('hunt batch conflict') >= 2,   # each line is in .err and map-msg_log.log
                        f'rc {rc_a} / {rc_b} / {rc_c}; rows changed {diff_tables(s7, acct_snapshot(aid))}')],
               'v21')
    feed(b, 15)
    open(os.path.join(OUT, 'v21_killme.flag'), 'w').write('15')
    log('   V21: 15 kills only in memory - stop the map-server now (bc_servers.ps1 killmap), then startmap -TestHooks and run v21c')
    time.sleep(45)


def cmd_v21c(a):
    """V21 phase c: after the forced stop the 15 kills are lost (bounded) and nothing is duplicated; retry reaches tier 1
    on D2; every period keeps progress = batch sum; nothing outside the account changed."""
    aid = AID['v21']
    st = g2b_state('v21')
    D0, D1, D2, D3 = v21_days()
    res = [('V21-8 forced stop with 15 kills in memory: lost (bounded), no duplicate', hunt_day(aid, D2)[0] == st['before_kill'] == 78,
            f'D2 {hunt_day(aid, D2)}')]
    b = login('v21', 1)
    to_field(b)
    alive(b)
    clock_to(b, kst_noon(D2))
    feed(b, 22)
    b.logout()
    wait_offline(aid)
    res.append(ledger_check(aid, D2, 100, 'V21-9 retry: D2 reaches 100 kills, tier 1 credited once'))
    for d, k in ((D0, 25), (D1, 5), (D3, 0)):
        res.append(ledger_check(aid, d, k, f'V21-9 period {d}'))
    tot = sql(f'SELECT period_day,activity,earned FROM need_marble_period WHERE account_id={aid} ORDER BY period_day,activity')
    res.append(('V21-10 daily caps (hunt 100 / total 200) never exceeded', all(int(r[2]) <= (200 if r[1] == '0' else 100) for r in tot), f'{tot}'))
    res.append(('V21-10 other accounts / global tables / #CASHPOINTS unchanged over V21', others_digest(aid) == st['others'],
                f'changed {diff_tables(st["others"], others_digest(aid))}'))
    g2b_record('V21', res, 'v21')


V24_TABLE = [
    # (KST wall clock, expected logical day) - submitted before the run (G2-B preflight document 5.3)
    ('2026-10-31 03:59:59', 20261030), ('2026-10-31 04:00:00', 20261031),
    ('2026-11-01 00:00:00', 20261031), ('2026-11-01 03:59:59', 20261031), ('2026-11-01 04:00:00', 20261101),
    ('2026-11-02 03:59:59', 20261101), ('2026-11-02 04:00:00', 20261102),
]


def cmd_v24(a):
    """V24 (G2-B): 04:00 KST logical day under the test clock (the OS clock and time zone are never touched).
    Boundaries: 2026-10-31 04:00 (Fri->Sat), calendar midnight 11-01 00:00, 11-01 04:00 (Sat->Sun, month end),
    11-02 04:00 (Sun->Mon). Policy row: exchange cost 50 -> 40 from 20261101 (isolated DB only)."""
    # v24r / v24s: reruns on fresh accounts (earlier runs kept as recorded)
    who = 'v24s' if 's' in a.rest else ('v24r' if 'r' in a.rest else 'v24')
    aid = AID[who]
    st = {'others': None}
    c = login(who)
    to_field(c)
    ok, t = alive(c)
    res = [('precondition: alive, clean account', ok and acct_snapshot(aid)['need_marble_wallet'] == [], repr(t))]
    days = [(kst, clock_to(c, kst), want) for kst, want in V24_TABLE]
    res.append(('V24-0 logical day at each wall clock instant (test clock)', all(d == w for _, d, w in days), f'{days}'))
    sql("INSERT IGNORE INTO need_marble_policy (policy_key,effective_day,value,changed_by,changed_at) VALUES (10,20261101,40,'g2b_v24',NOW())")
    st['others'] = others_digest(aid)
    # V24-1 kills on both sides of 04:00 are split by logical day in one batch
    clock_to(c, '2026-10-31 03:59:00')
    feed(c, 10)
    clock_to(c, '2026-10-31 04:00:05')
    feed(c, 9)             # 19 in memory: below the 20-kill flush, so the pending map shows both days
    f = flags(c)
    c.logout()             # the collect session is issued by this first flush (it is 0 in the flags above)
    wait_offline(aid)
    rows1 = batch_rows(aid)
    S = rows1[0][0] if rows1 else None
    res.append(('V24-1 one batch across 04:00 -> one row per logical day', f.get('pdays') == 2 and f.get('pending') == 19 and
                rows1 == [(S, 1, 20261030, 10), (S, 1, 20261031, 9)], f'flags {f}; rows {rows1}'))
    # V24-2 fill 20261031 to every cap; the logout flushes every kill before the day is fingerprinted
    c = login(who)
    to_field(c)
    alive(c)
    clock_to(c, '2026-10-31 22:00:00')
    feed(c, 391)           # 9 + 391 = 400 kills on 20261031; 11 of them are still in memory
    c.logout()             # flush them so the day is exactly 400 before the extra kills
    wait_offline(aid)
    c = login(who)
    for k in (1, 2, 3):
        inst_clear(c, k)
    to_field(c)            # the dungeon harness leaves the character in a town without field mobs
    alive(c)
    n_cap, cnt_cap = feed(c, 20)
    c.logout()
    wait_offline(aid)
    per31 = sql(f'SELECT activity,earned FROM need_marble_period WHERE account_id={aid} AND period_day=20261031 ORDER BY activity')
    res.append(('V24-2 20261031 capped: hunt 100, dungeon 100, total 200 (third dungeon 0), 20 more kills count 0',
                per31 == [['0', '200'], ['1', '100'], ['2', '100']] and hunt_day(aid, 20261031) == (400, 5) and cnt_cap == 0,
                f'period {per31}; progress {hunt_day(aid, 20261031)}; extra feed n={n_cap} counted={cnt_cap}'))
    day31 = {t_: [r for r in rows if str(20261031) in map(str, r)] for t_, rows in acct_snapshot(aid).items()}
    # V24-3 calendar midnight is not a boundary
    c = login(who)
    to_field(c)
    alive(c)
    d_mid = clock_to(c, '2026-11-01 00:00:05')
    n, cnt = feed(c, 20)
    res.append(('V24-3 calendar midnight: still 20261031, capped day adds nothing', d_mid == 20261031 and cnt == 0, f'day {d_mid}, counted {cnt}'))
    # V24-4 exchange issued before 04:00, confirmed after it with the reply lost
    w0 = wallet(aid)
    clock_to(c, '2026-11-01 03:59:50')
    ch = menu_picker(['점수 → 주사위 교환', 2])

    def drop_at_confirm(kind, items, mes, base=ch):
        if kind == 'menu' and any(x == '교환' for x in items):
            c.drop()
            raise SystemExit
        return base(kind, items, mes)
    try:
        c.dialog('@marble', drop_at_confirm, 25)
    except SystemExit:
        pass
    wait_offline(aid)
    req = sql(f'SELECT request_no,status,bundles,cost FROM need_marble_exchange_req WHERE account_id={aid} ORDER BY request_no DESC LIMIT 1')
    c = login(who, 1)
    clock_to(c, '2026-11-01 04:00:01')
    ch2 = menu_picker(['이 요청으로 교환 확정'])

    def confirm_then_drop(kind, items, mes, base=ch2):
        if kind == 'menu' and any('이 요청으로 교환 확정' in x for x in items):
            c.drop_after = True
        return base(kind, items, mes)
    c.dialog('@marble', confirm_then_drop, 25)
    wait_offline(aid)
    c = login(who)
    clock_to(c, '2026-11-01 04:00:20')
    marble(c, [])
    rq = int(req[0][0]) if req else -1
    ex = sql(f'SELECT d_score,d_dice,period_day FROM need_marble_wallet_tx WHERE account_id={aid} AND kind=2 AND ref_a={rq}')
    w1 = wallet(aid)
    pex = sql(f'SELECT period_day,exchanged FROM need_marble_period WHERE account_id={aid} AND activity=0 AND exchanged<>0')
    res.append(('V24-4 exchange across 04:00 with the reply lost: applied once at the issue-time cost',
                len(ex) == 1 and ex[0][0] == '-100' and ex[0][1] == '2' and w1['score'] == w0['score'] - 100 and w1['dice'] == w0['dice'] + 2 and w1['pack'] == 0,
                f'request {req}; EXCHANGE rows {ex}; wallet {w0} -> {w1}; period exchanged {pex}'))
    res.append(('V24-4 exchange period key (observation)', None, f'EXCHANGE row period_day {ex[0][2] if ex else None}; period rows {pex}'))
    # V24-5 first activity after 04:00 goes to the new day, 20261031 rows untouched
    to_field(c)
    alive(c)
    feed(c, 20)
    inst_clear(c, 1)
    after31 = {t_: [r for r in rows if str(20261031) in map(str, r)] for t_, rows in acct_snapshot(aid).items()}
    per01 = sql(f'SELECT activity,earned FROM need_marble_period WHERE account_id={aid} AND period_day=20261101 ORDER BY activity')
    res.append(('V24-5 after 04:00: kills and the same dungeon count on 20261101, 20261031 rows unchanged',
                after31 == day31 and hunt_day(aid, 20261101)[0] == 20 and ['2', '50'] in per01, f'period 20261101 {per01}'))
    # V24-6 new exchange uses the new policy (40), the old ledger row keeps 50 x 2
    w2 = wallet(aid)
    marble(c, ['점수 → 주사위 교환', 1, '교환'])
    w3 = wallet(aid)
    ex_all = sql(f'SELECT ref_a,d_score,d_dice FROM need_marble_wallet_tx WHERE account_id={aid} AND kind=2 ORDER BY ref_a')
    res.append(('V24-6 policy from 20261101: exchange costs 40, the earlier row still -100 / +2', w3['score'] == w2['score'] - 40 and
                ex_all[0][1:] == ['-100', '2'] and ex_all[-1][1:] == ['-40', '1'], f'{w2} -> {w3}; EXCHANGE rows {ex_all}'))
    # V24-7 an unfinished roll survives 04:00 (Sun -> Mon), score / dice / position kept
    adjust(aid, dice=3, ticket=924001)
    clock_to(c, '2026-11-02 03:59:30')
    enter_board(c)
    s0 = state(aid) or {'steps': 0, 'state': 0}
    for _ in range(6):
        marble(c, ['주사위 굴리기'])
        s1 = wait_state(aid, lambda s: s and s['steps'] >= s0['steps'] + 1, 15)
        if s1 and s1['state'] == 1 and s1['left'] > 0:
            break
        wait_state(aid, lambda s: s and s['state'] != 1, 30)
        s0 = state(aid)
    c.drop()
    wait_offline(aid)
    sd, wd = state(aid), wallet(aid)
    b = login(who, 1)
    d_b = clock_to(b, '2026-11-02 04:00:10')
    w_after = wallet(aid)
    enter_board(b)
    marble(b, ['이동 이어가기'])
    s2 = wait_state(aid, lambda s: s and s['state'] != 1, 40)
    w4 = wallet(aid)
    ev = sql(f'SELECT COUNT(*) FROM need_marble_event WHERE account_id={aid} AND roll_seq={sd["roll"]}')[0][0] if sd else None
    res.append(('V24-7 roll left mid-move before 04:00 resumes after it: same roll, one die, one event',
                sd and sd['state'] == 1 and sd['left'] > 0 and d_b == 20261102 and w_after == wd and s2['roll'] == sd['roll'] and
                w4['dice'] == wd['dice'] and ev == '1' and s2['steps'] - s0['steps'] == sd['die'],
                f'dropped {sd}; wallet across 04:00 {wd} -> {w_after}; resumed {s2}; events {ev}'))
    # V24-8 first kill after the Sun -> Mon boundary
    to_field(b)
    alive(b)
    feed(b, 20)
    b.logout()
    wait_offline(aid)
    res.append(('V24-8 after 2026-11-02 04:00 kills go to 20261102', hunt_day(aid, 20261102)[0] == 20, f'{hunt_day(aid, 20261102)}'))
    caps = sql(f'SELECT period_day,activity,earned FROM need_marble_period WHERE account_id={aid} ORDER BY period_day,activity')
    cap_of = {'0': 200, '1': 100, '2': 100, '3': 100}
    res.append(('V24-9 caps never exceeded on any day', all(int(r[2]) <= cap_of.get(r[1], 100) for r in caps), f'{caps}'))
    inv = invariants()
    res.append(('V24-9 invariants', not inv, f'{len(inv)} violating rows: {inv[:5]}'))
    # need_marble_boot gets this map-server run's boot row at the first dungeon credit (instance run key, design 3.3)
    skip = ('need_marble_policy', 'need_marble_boot')
    res.append(('V24-9 other accounts / global tables (except the policy and boot rows) / #CASHPOINTS unchanged',
                {k: v for k, v in others_digest(aid).items() if k not in skip} ==
                {k: v for k, v in st['others'].items() if k not in skip}, ''))
    g2b_record('V24' + who[3:], res, who)


# ---------------------------------------------------------------------------- G2-C battleground (v1.1 simplified)
G2C_ACC = ['bga', 'bgb', 'bgheal', 'bgafk', 'bgleave', 'bgrelog', 'bgdef', 'bgdead', 'bgc', 'bgclk']
for _i, _w in enumerate(G2C_ACC):
    ACC[_w] = ('mbqa_' + _w, 'user')
    AID[_w] = 2000921 + _i
NAMES.update({('bga', 0): 'MBQA_BGA1', ('bga', 1): 'MBQA_BGA2', ('bgb', 0): 'MBQA_BGB', ('bgheal', 0): 'MBQA_BGHEAL',
              ('bgafk', 0): 'MBQA_BGAFK', ('bgleave', 0): 'MBQA_BGLEAVE', ('bgrelog', 0): 'MBQA_BGRELOG', ('bgdef', 0): 'MBQA_BGDEF',
              ('bgdead', 0): 'MBQA_BGDEAD', ('bgc', 0): 'MBQA_BGC', ('bgclk', 0): 'MBQA_BGCLK'})
G2C_DAY_A = '2026-11-20 12:00:00'      # M1 Tierra / M2 Flavius / M3 KVM, one cycle token 101
G2C_DAY_OFF = 20261122                 # M6: BG credit switched off by a policy row (existing rewards compared)
ACT_BG = 3


def g2c_state(name, data=None):
    p = os.path.join(OUT, f'g2c_{name}.json')
    if data is None:
        return json.load(open(p, encoding='utf-8')) if os.path.exists(p) else {}
    json.dump(data, open(p, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)


def g2c_record(cid, res):
    ok = all(r[1] for r in res)
    json.dump([{'check': n, 'pass': bool(p), 'detail': d} for n, p, d in res],
              open(os.path.join(OUT, f'g2c_{cid}_checks.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
    for n, p, d in res:
        log(f'   {"PASS" if p else "FAIL"} {n} | {d}')
    record(cid, 'PASS' if ok else 'FAIL', f'{sum(1 for r in res if r[1])}/{len(res)} checks')


def bg_rows(aid):
    """BG activity rows of one account: (token, type, period_day, requested, credited, outcome, char_id)."""
    return [tuple(int(x) for x in r) for r in
            sql(f'SELECT key_b,key_c,period_day,requested,credited,outcome,char_id FROM need_marble_activity '
                f'WHERE account_id={aid} AND activity={ACT_BG} ORDER BY created_at,key_b,key_c')]


def earned(aid, day):
    """{activity: earned} of one logical day (0 = total, 1 hunt, 2 instance, 3 BG)."""
    return {int(a): int(e) for a, e in sql(f'SELECT activity,earned FROM need_marble_period WHERE account_id={aid} AND period_day={day}')}


def score(aid):
    w = wallet(aid)
    return w['score'] if w else 0


def bgcmd(g, text, needle, wait=6):
    return harness(g, text, needle=needle, wait=wait)


def kv(t):
    out = {}
    for part in (t or '').split():
        if '=' in part:
            k, v = part.split('=', 1)
            try:
                out[k] = int(v)
            except ValueError:
                out[k] = v
    return out


def items(g, name):
    return kv(bgcmd(g, f'@mbqabgitems {name}', '[MBQA] bgitems', wait=3))


def where(g, name):
    return kv(bgcmd(g, f'@mbqabgwhere {name}', '[MBQA] bgwhere', wait=3))


def delta(a, b):
    return {k: b[k] - a[k] for k in a if isinstance(a[k], int) and k in b and k not in ('name',) and b[k] != a[k]}


class Responder:
    """Answers the NPC dialog buttons (next / close, ESC on menus) of one client in the background, like a player
    who reads the reward dialog. The AFK character never gets one."""

    def __init__(self, c, start=None):
        import threading
        self.c, self.stop, self.n, self.start = c, threading.Event(), 0, start
        self.t = threading.Thread(target=self.run, daemon=True)
        self.t.start()

    def run(self):
        import struct
        pos = self.c.mark() if self.start is None else self.start
        while not self.stop.is_set() and self.c.alive:
            while pos < len(self.c.events):
                _, kind, data = self.c.events[pos]
                pos += 1
                if kind == 'next':
                    self.c.raw(struct.pack('<HI', 0x00B9, data))
                elif kind == 'close':
                    self.c.raw(struct.pack('<HI', 0x0146, data))
                    self.n += 1
                elif kind == 'menu':
                    self.c.raw(struct.pack('<HIB', 0x00B8, data[0], 0xFF))
            time.sleep(0.05)

    def close(self):
        self.stop.set()
        self.t.join(2)


def npc_claim(g, c, team):
    """The player clicks the REAL reward NPC of the current mode and reads the dialog to the end."""
    gid = kv(bgcmd(g, f'@mbqabgnpc {team}', '[MBQA] bgnpc', wait=3)).get('gid')
    mes, texts, done = c.dialog(int(gid), lambda kind, items_, m: None if kind == 'menu' else 0, timeout=20)
    log(f'   [{c.tag}] npc {gid} done={done} mes={len(mes)} texts={texts[-2:]}')
    return done


def bg_start(g, mode, token, gnames, cnames):
    t = kv(bgcmd(g, f'@mbqabgstart {mode} {token} {",".join(gnames) or "-"} {",".join(cnames) or "-"}', '[MBQA] bgstart', wait=4))
    return t


def play(clients, seconds, heal=None, once=(), skip=()):
    """Normal match activity: walking around the spawn (idletime_option WALK). heal = (healer, target aid): skill only.
    once: characters that move only at the start (defenders). skip: AFK / gone."""
    end = time.time() + seconds
    first = True
    while time.time() < end:
        for tag, (c, x, y) in clients.items():
            if tag in skip or not c.alive or (tag in once and not first):
                continue
            c.walk(x + (1 if int(time.time()) % 2 else 0), y)
        if heal:
            heal[0].use_skill(28, 10, heal[1])
        first = False
        time.sleep(2)


def cmd_g2c(a):
    """G2-C v1.1 (simplified): one normally finished battleground match = 50 marble points per account, through the REAL
    reward NPCs and auto reward functions of Tierra / Flavius / KVM. Phases: m1 m2 m3 clk off (run in this order), cancel."""
    ph = a.rest[0] if a.rest else 'm1'
    globals()['g2c_' + ph]()


def g2c_m1():
    """M1 Tierra (token 101): C-01 win/loss, C-02 healer, C-03 AFK / leaver / short relog / defender / dead, C-04 duplicates."""
    g = login('gm')
    day = clock_to(g, G2C_DAY_A)
    st = {'day': day}
    afk = login('bgafk')                      # logs in first and never sends anything but keepalives from here on
    cl = {w: login(w) for w in ('bga', 'bgb', 'bgheal', 'bgleave', 'bgrelog', 'bgdef', 'bgdead')}
    time.sleep(9)                             # the AFK character is idle longer than the 8 s activity window before the match
    before = {w: (score(AID[w]), bg_rows(AID[w])) for w in list(cl) + ['bgafk']}
    st['items0'] = {n: items(g, n) for n in ('MBQA_BGA1', 'MBQA_BGB')}
    # Croix order matters for the auto reward loop: the defender comes after the AFK character
    t = bg_start(g, 1, 101, ['MBQA_BGA1', 'MBQA_BGHEAL', 'MBQA_BGDEAD'],
                 ['MBQA_BGB', 'MBQA_BGRELOG', 'MBQA_BGLEAVE', 'MBQA_BGAFK', 'MBQA_BGDEF'])
    res = [('M1 setup: 2 teams, 8 joined on bat_a01 (random match cycle, token 101)',
            t.get('gcount') == 3 and t.get('ccount') == 5 and t.get('joined') == 8, repr(t))]
    time.sleep(2)
    pos = {'bga': (50, 374), 'bgheal': (50, 375), 'bgdef': (41, 16), 'bgdead': (51, 375),
           'bgb': (42, 16), 'bgrelog': (42, 17), 'bgleave': (43, 16)}
    clients = {w: (cl[w], x, y) for w, (x, y) in pos.items()}
    heal = (cl['bgheal'], cl['bga'].aid)
    play(clients, 6, heal=heal, once=('bgdef',))
    cl['bgleave'].logout()                   # leaves in the middle of the match and does not come back
    play(clients, 2, heal=heal, once=('bgdef',), skip=('bgleave',))
    cl['bgrelog'].drop()                     # short disconnect
    wait_offline(AID['bgrelog'])
    play(clients, 2, heal=heal, once=('bgdef',), skip=('bgleave', 'bgrelog'))
    cl['bgrelog'] = login('bgrelog')
    w_relog = where(g, 'MBQA_BGRELOG')
    clients['bgrelog'] = (cl['bgrelog'], w_relog.get('x', 42) or 42, w_relog.get('y', 17) or 17)
    play(clients, 6, heal=heal, once=('bgdef',), skip=('bgleave',))
    g.command('@kill MBQA_BGDEAD')            # dead at the end of the match (respawn wait)
    play(clients, 4, heal=heal, once=('bgdef',), skip=('bgleave', 'bgdead'))
    st['where_end'] = {n: where(g, n) for n in ('MBQA_BGRELOG', 'MBQA_BGDEAD', 'MBQA_BGAFK', 'MBQA_BGDEF')}
    bgcmd(g, '@mbqabgend 1', '[MBQA] bgend')      # Guillaume wins
    # NPC path: the winner and the loser click their reward NPC
    npc_claim(g, cl['bga'], 1)
    npc_claim(g, cl['bgb'], 2)
    st['items1'] = {n: items(g, n) for n in ('MBQA_BGA1', 'MBQA_BGB')}
    # auto path for everyone still on the map; the AFK character does not answer its dialog
    resp = [Responder(cl[w]) for w in ('bgheal', 'bgdef', 'bgdead', 'bgrelog')]
    m = g.mark()
    afk_mark = afk.mark()
    t0 = time.time()
    g.say('@mbqabgauto')
    auto = g.wait_text(m, '[MBQA] bgauto', 15)
    st['auto_wait_s'] = round(time.time() - t0, 1)
    st['afk_row_while_waiting'] = bg_rows(AID['bgafk'])
    st['def_row_while_waiting'] = bg_rows(AID['bgdef'])   # the defender comes after the AFK character in the loop
    st['auto_before_afk_answers'] = auto
    if auto is None:
        # the existing reward loop waits in close2 for the AFK character; the NPC secure timeout is 60 s
        auto = g.wait_text(m, '[MBQA] bgauto', 75)
    st['auto'] = auto
    st['auto_after_90s'] = auto
    st['gm_free_after_90s'] = where(g, 'MBQA_BGDEF')            # empty when the GM's first command is still running
    st['def_items_after_first_auto'] = items(g, 'MBQA_BGDEF')
    st['afk_items_after_first_auto'] = items(g, 'MBQA_BGAFK')
    time.sleep(1)
    after = {w: (score(AID[w]), bg_rows(AID[w])) for w in list(cl) + ['bgafk']}
    st['rows'] = {w: after[w][1] for w in after}
    st['scores'] = {w: [before[w][0], after[w][0]] for w in after}
    gained = {w: after[w][0] - before[w][0] for w in after}

    def one(w, char):
        r = after[w][1]
        return len(r) == 1 and r[0][:6] == (101, 1, day, 50, 50, 1) and r[0][6] == char
    res.append(('C-01 winner (NPC path) +50, one row (token 101, Tierra, credited)', gained['bga'] == 50 and one('bga', 150931),
                f"gained {gained['bga']} rows {after['bga'][1]}"))
    res.append(('C-01 loser (NPC path) +50, same as the winner', gained['bgb'] == 50 and one('bgb', 150933),
                f"gained {gained['bgb']} rows {after['bgb'][1]}"))
    res.append(('C-02 healer: heal only, no attack, auto path +50', gained['bgheal'] == 50 and one('bgheal', 150934),
                f"gained {gained['bgheal']} rows {after['bgheal'][1]}"))
    res.append(('C-03 AFK the whole match: 0, no row', gained['bgafk'] == 0 and after['bgafk'][1] == [],
                f"gained {gained['bgafk']} rows {after['bgafk'][1]}; auto wait {st['auto_wait_s']} s; {st.get('auto_before_afk_answers')!r}"))
    res.append(('C-03 left the match and did not come back: 0, no row', gained['bgleave'] == 0 and after['bgleave'][1] == [],
                f"gained {gained['bgleave']} rows {after['bgleave'][1]}"))
    res.append(('C-03 defender standing still after one move: +50 (not treated as AFK)', gained['bgdef'] == 50 and one('bgdef', 150938),
                f"gained {gained['bgdef']} rows {after['bgdef'][1]}"))
    res.append(('C-03 dead at the end (respawn wait): +50', gained['bgdead'] == 50 and one('bgdead', 150939),
                f"gained {gained['bgdead']} rows {after['bgdead'][1]}; where {st['where_end']['MBQA_BGDEAD']}"))
    st['relog'] = {'where_after_relog': w_relog, 'gained': gained['bgrelog'], 'rows': after['bgrelog'][1]}
    res.append(('C-03 short disconnect + relog during the match: measured (credited only when back on the map)',
                (gained['bgrelog'] == 50 and one('bgrelog', 150937)) == (w_relog.get('map') == 'bat_a01'),
                f"where after relog {w_relog}; gained {gained['bgrelog']} rows {after['bgrelog'][1]}"))
    res.append(('C-03 member after the AFK character in the auto loop: credited while the loop still waits in the AFK dialog',
                st['def_row_while_waiting'] == after['bgdef'][1] and len(after['bgdef'][1]) == 1, f"while waiting {st['def_row_while_waiting']}"))
    # existing BG behaviour, not a marble check: how the reward loop ends behind the AFK character
    record('G2C-M1-BG-AUTO', 'OBSERVED', f"auto reply after 15 s {st['auto_before_afk_answers']!r}, after 90 s {auto!r}; "
           f"GM free {bool(st['gm_free_after_90s'])}; defender BG items {st['def_items_after_first_auto']}; AFK BG items {st['afk_items_after_first_auto']}")
    # C-04 duplicates: second auto call, second end call by the account, character change, other character of the account
    s0 = {w: acct_snapshot(AID[w]) for w in ('bga', 'bgb', 'bgheal', 'bgdef', 'bgafk')}
    t2 = bgcmd(g, '@mbqabgauto', '[MBQA] bgauto', wait=12)
    r1 = kv(bgcmd(cl['bga'], '@mbqabgcredit Tierra 101', '[MBQA] bgcredit', wait=3)).get('rc')
    r2 = kv(bgcmd(cl['bgheal'], '@mbqabgcredit Tierra 101', '[MBQA] bgcredit', wait=3)).get('rc')
    cl['bga'].logout()
    wait_offline(AID['bga'])
    a2 = login('bga', 1)
    r3 = kv(bgcmd(a2, '@mbqabgcredit Tierra 101', '[MBQA] bgcredit', wait=3)).get('rc')
    s1 = {w: acct_snapshot(AID[w]) for w in ('bga', 'bgb', 'bgheal', 'bgdef', 'bgafk')}
    st['def_items_after_second_auto'] = items(g, 'MBQA_BGDEF')
    for r in resp:
        r.close()
    st['dup'] = {'auto2': t2, 'rc_bga1': r1, 'rc_heal': r2, 'rc_bga2': r3}
    res.append(('C-04 second auto reward call (duplicate end): marble ledger unchanged', t2 is not None and s1 == s0,
                f"{t2!r} (existing BG reward of members the first loop never reached: defender items {st['def_items_after_second_auto']})"))
    res.append(('C-04 second end call for the same match: ALREADY (-4), ledger unchanged',
                r1 == RC['ALREADY'] and r2 == RC['ALREADY'] and s1 == s0, f'rc {r1} / {r2}'))
    res.append(('C-04 character change (MBQA_BGA2, same account): ALREADY (-4), ledger unchanged', r3 == RC['ALREADY'] and s1 == s0, f'rc {r3}'))
    bgcmd(g, '@mbqabgreset', '[MBQA] bgreset')
    # the leaver logs back in after the match: still nothing
    lv = login('bgleave')
    time.sleep(2)
    st['leaver_back'] = {'where': where(g, 'MBQA_BGLEAVE'), 'rows': bg_rows(AID['bgleave'])}
    res.append(('C-03 leaver logging back in after the match: still 0', bg_rows(AID['bgleave']) == [], repr(st['leaver_back'])))
    res.append(('M1 ledger invariants: 0 violations', invariants() == [], repr(invariants()[:3])))
    st['items_delta_m1'] = {n: delta(st['items0'][n], st['items1'][n]) for n in st['items0']}
    g2c_state('m1', st)
    g2c_record('G2C-M1', res)
    for c in [g, afk, a2, lv] + [cl[w] for w in cl if w != 'bga']:
        if c.alive:
            c.logout()


def g2c_m2():
    """M2 Flavius (same cycle token 101): account sharing with the second character, Croix win, NPC + auto path."""
    g = login('gm')
    day = clock_to(g, G2C_DAY_A)
    a2, b, h = login('bga', 1), login('bgb'), login('bgheal')
    before = {w: score(AID[w]) for w in ('bga', 'bgb', 'bgheal')}
    t = bg_start(g, 2, 101, ['MBQA_BGA2', 'MBQA_BGHEAL'], ['MBQA_BGB'])
    res = [('M2 setup: Flavius, 3 joined on bat_b01', t.get('joined') == 3, repr(t))]
    time.sleep(2)
    play({'bga': (a2, 10, 290), 'bgb': (b, 390, 10)}, 9, heal=(h, a2.aid))
    bgcmd(g, '@mbqabgend 2', '[MBQA] bgend')      # Croix wins
    npc_claim(g, b, 2)
    resp = [Responder(a2), Responder(h)]
    auto = bgcmd(g, '@mbqabgauto', '[MBQA] bgauto', wait=10)
    for r in resp:
        r.close()
    bgcmd(g, '@mbqabgreset', '[MBQA] bgreset')
    after = {w: score(AID[w]) for w in before}
    rows = {w: bg_rows(AID[w]) for w in before}
    res.append(('M2 auto reward: 2 rewarded (BGA2, healer)', kv(auto).get('rewarded') == 2, repr(auto)))
    res.append(('C-05 account sharing: MBQA_BGA2 credits the same account, BG 50 + 50 = 100',
                after['bga'] - before['bga'] == 50 and earned(AID['bga'], day).get(3) == 100 and
                [r[:2] for r in rows['bga']] == [(101, 1), (101, 2)] and rows['bga'][1][6] == 150932, f"rows {rows['bga']}"))
    res.append(('C-06 same cycle token, other mode: a new row per mode (Flavius type 2)',
                all(len(rows[w]) == 2 and rows[w][1][:6] == (101, 2, day, 50, 50, 1) for w in before), repr(rows)))
    res.append(('M2 ledger invariants: 0 violations', invariants() == [], repr(invariants()[:3])))
    g2c_state('m2', {'day': day, 'auto': auto, 'rows': rows, 'scores': {w: [before[w], after[w]] for w in before}})
    g2c_record('G2C-M2', res)
    for c in (g, a2, b, h):
        c.logout()


def g2c_m3():
    """M3 KVM (same cycle token 101, draw): BG daily cap (third match = 0), total cap 200 with hunt + instance first."""
    g = login('gm')
    day = clock_to(g, G2C_DAY_A)
    c = login('bgc')
    to_field(c)
    alive(c)
    feed(c, 200)
    feed(c, 200)
    time.sleep(2)
    c.command('@warp prontera 156 191')
    time.sleep(2)
    i1 = inst_clear(c, 1)
    i2 = inst_clear(c, 2)
    pre_c = earned(AID['bgc'], day)
    a1, b, h = login('bga'), login('bgb'), login('bgheal')
    before = {w: (score(AID[w]), earned(AID[w], day)) for w in ('bga', 'bgb', 'bgheal', 'bgc')}
    t = bg_start(g, 3, 101, ['MBQA_BGA1', 'MBQA_BGB'], ['MBQA_BGHEAL', 'MBQA_BGC'])
    res = [('M3 setup: bgc hunt 100 + instance 100 = total 200 before the match', pre_c.get(0) == 200 and pre_c.get(1) == 100 and pre_c.get(2) == 100,
            f'earned {pre_c}; inst {i1!r} / {i2!r}'),
           ('M3 setup: KVM, 4 joined on bat_c01', t.get('joined') == 4, repr(t))]
    time.sleep(2)
    play({'bga': (a1, 52, 129), 'bgb': (b, 53, 129), 'bgc': (c, 147, 55)}, 9, heal=(h, c.aid))
    bgcmd(g, '@mbqabgend 3', '[MBQA] bgend')      # draw
    npc_claim(g, a1, 1)                           # KVM01A (Guillaume side)
    auto = bgcmd(g, '@mbqabgauto', '[MBQA] bgauto', wait=8)
    bgcmd(g, '@mbqabgreset', '[MBQA] bgreset')
    after = {w: (score(AID[w]), earned(AID[w], day)) for w in before}
    rows = {w: bg_rows(AID[w]) for w in before}
    capped = lambda w: rows[w] and rows[w][-1][:6] == (101, 3, day, 50, 0, 2)
    res.append(('M3 auto reward (F_KvM_AutoRewardRandom): 3 rewarded', kv(auto).get('rewarded') == 3, repr(auto)))
    res.append(('C-05 third match of the day: BG 50 + 50 + 0 (row CAPPED, credited 0) for bga / bgb / healer',
                all(capped(w) and after[w][0] == before[w][0] and after[w][1].get(3) == 100 for w in ('bga', 'bgb', 'bgheal')), repr(rows)))
    res.append(('C-05 total cap: bgc at 200 gets 0 from the match (BG earned 0, total stays 200)',
                capped('bgc') and after['bgc'][0] == before['bgc'][0] and after['bgc'][1].get(0) == 200 and after['bgc'][1].get(3, 0) == 0,
                f"rows {rows['bgc']} earned {after['bgc'][1]}"))
    over = sql('SELECT account_id,period_day,activity,earned FROM need_marble_period WHERE (activity=0 AND earned>200) OR (activity=3 AND earned>100)')
    res.append(('C-05 no account / day over BG 100 or total 200', over == [], repr(over)))
    res.append(('M3 ledger invariants: 0 violations', invariants() == [], repr(invariants()[:3])))
    g2c_state('m3', {'day': day, 'auto': auto, 'rows': rows, 'before': before, 'after': after})
    g2c_record('G2C-M3', res)
    for x in (g, c, a1, b, h):
        x.logout()


def g2c_clk():
    """C-06 04:00 KST: a match rewarded at 03:59:5x belongs to the old logical day, one that starts before 04:00 and
    is rewarded after it belongs to the new day (reward time decides)."""
    g = login('gm')
    k = login('bgclk')
    res = []
    t = bg_start(g, 1, 201, ['MBQA_BGCLK'], [])
    play({'k': (k, 50, 374)}, 4)
    bgcmd(g, '@mbqabgend 1', '[MBQA] bgend')
    d1 = clock_to(g, '2026-11-21 03:59:50')
    npc_claim(g, k, 1)
    r1 = bg_rows(AID['bgclk'])
    bgcmd(g, '@mbqabgreset', '[MBQA] bgreset')
    d_start = clock_to(g, '2026-11-21 03:59:52')
    t2 = bg_start(g, 2, 201, ['MBQA_BGCLK'], [])
    play({'k': (k, 10, 290)}, 9)
    bgcmd(g, '@mbqabgend 1', '[MBQA] bgend')
    d2 = kv(bgcmd(g, '@mbqaflags', '[MBQA] flags', wait=3)).get('day')
    npc_claim(g, k, 1)
    r2 = bg_rows(AID['bgclk'])
    bgcmd(g, '@mbqabgreset', '[MBQA] bgreset')
    res.append(('C-06 setup: both matches started', t.get('joined') == 1 and t2.get('joined') == 1, f'{t} / {t2}'))
    res.append(('C-06 rewarded at 03:59:5x KST -> old logical day 20261120', d1 == 20261120 and r1 == [(201, 1, 20261120, 50, 50, 1, 150941)], f'day {d1} rows {r1}'))
    res.append(('C-06 started 03:59:52, rewarded after 04:00 -> new logical day 20261121 (one row each, no duplicate)',
                d_start == 20261120 and d2 == 20261121 and r2 == r1 + [(201, 2, 20261121, 50, 50, 1, 150941)], f'start day {d_start}, reward day {d2}, rows {r2}'))
    res.append(('C-06 each day counts its own BG points', earned(AID['bgclk'], 20261120).get(3) == 50 and earned(AID['bgclk'], 20261121).get(3) == 50,
                f'{earned(AID["bgclk"], 20261120)} / {earned(AID["bgclk"], 20261121)}'))
    res.append(('C-06 ledger invariants: 0 violations', invariants() == [], repr(invariants()[:3])))
    g2c_state('clk', {'rows': r2})
    g2c_record('G2C-CLK', res)
    g.logout()
    k.logout()


def g2c_off():
    """C-07: the same Tierra rewards with BG credit switched off for the day (policy row, effective next logical day).
    The existing BG rewards must not differ from M1, and no marble row is written."""
    m1 = g2c_state('m1')
    sql(f"INSERT IGNORE INTO need_marble_policy (policy_key,effective_day,value,changed_by,changed_at) VALUES (4,{G2C_DAY_OFF},0,'g2c_off',NOW())")
    g = login('gm')
    day = clock_to(g, '2026-11-22 12:00:00')
    a1, b = login('bga'), login('bgb')
    s0 = {w: acct_snapshot(AID[w]) for w in ('bga', 'bgb')}
    i0 = {n: items(g, n) for n in ('MBQA_BGA1', 'MBQA_BGB')}
    t = bg_start(g, 1, 301, ['MBQA_BGA1'], ['MBQA_BGB'])
    play({'bga': (a1, 50, 374), 'bgb': (b, 42, 16)}, 6)
    bgcmd(g, '@mbqabgend 1', '[MBQA] bgend')
    npc_claim(g, a1, 1)
    npc_claim(g, b, 2)
    i1 = {n: items(g, n) for n in i0}
    bgcmd(g, '@mbqabgreset', '[MBQA] bgreset')
    s1 = {w: acct_snapshot(AID[w]) for w in ('bga', 'bgb')}
    d_off = {n: delta(i0[n], i1[n]) for n in i0}
    pol = kv(bgcmd(g, '@mbqaflags', '[MBQA] flags', wait=3))
    res = [('C-07 setup: logical day 20261122 with BG credit off, match started', day == G2C_DAY_OFF and t.get('joined') == 2, f'day {day}; {t}'),
           ('C-07 BG credit off: no marble row, ledger unchanged', s1 == s0, f'changed {[w for w in s0 if s0[w] != s1[w]]}'),
           ('C-07 existing BG rewards (badges, 6378, 399990, #BATTLEGROUND_POINT) identical with marble on (M1) and off',
            d_off == m1.get('items_delta_m1') and all(d_off.values()), f'off {d_off} / on {m1.get("items_delta_m1")}')]
    allrows = sql('SELECT COUNT(*) FROM need_marble_cp_payout')[0][0]
    cash = sql("SELECT COUNT(*) FROM acc_reg_num WHERE `key`='#CASHPOINTS'")[0][0]
    res.append(('C-07 #CASHPOINTS rows 0, CP payout rows 0 (no CP decided or applied)', cash == '0' and allrows == '0', f'cash {cash} payout {allrows}'))
    inv = invariants()
    res.append(('C-07 ledger invariants over the whole DB: 0 violations', inv == [], repr(inv[:3])))
    other = sql('SELECT COUNT(*) FROM need_marble_activity WHERE account_id NOT BETWEEN 2000921 AND 2000930')[0][0]
    res.append(('C-07 no marble row for any account outside the G2-C test accounts', other == '0', f'rows {other}'))
    g2c_state('off', {'day': day, 'delta_off': d_off, 'delta_on': m1.get('items_delta_m1'), 'flags': pol})
    g2c_record('G2C-OFF', res)
    for x in (g, a1, b):
        x.logout()


def g2c_cancel():
    """C-03 cancelled match: the REAL admin force stop (NEED_BG_RM_Core::OnAdminForceStop) in the middle of a Tierra match,
    then every end path is tried anyway (result, reward NPC click when still possible, auto reward). Expect 0 marble rows."""
    g = login('gm')
    day = clock_to(g, '2026-11-25 12:00:00')
    b, h = login('bgb'), login('bgheal')
    s0 = {w: acct_snapshot(AID[w]) for w in ('bgb', 'bgheal')}
    t = bg_start(g, 1, 401, ['MBQA_BGHEAL'], ['MBQA_BGB'])
    play({'bgb': (b, 42, 16)}, 6, heal=(h, h.aid))
    stop = kv(bgcmd(g, '@mbqabgstop', '[MBQA] bgstop', wait=5))
    w_after = {n: where(g, n) for n in ('MBQA_BGB', 'MBQA_BGHEAL')}
    bgcmd(g, '@mbqabgend 1', '[MBQA] bgend')           # a late result after the cancel
    clicked = []
    for c, team, n in ((h, 1, 'MBQA_BGHEAL'), (b, 2, 'MBQA_BGB')):
        if w_after[n].get('map') == 'bat_a01':          # still on the battle map: try the reward NPC anyway
            r = Responder(c)
            clicked.append((n, npc_claim(g, c, team)))
            r.close()
    auto = bgcmd(g, '@mbqabgauto', '[MBQA] bgauto', wait=8)
    rows = {w: [r for r in bg_rows(AID[w]) if r[0] == 401] for w in ('bgb', 'bgheal')}
    s1 = {w: acct_snapshot(AID[w]) for w in ('bgb', 'bgheal')}
    res = [('cancel setup: Tierra token 401, 2 joined, then the real admin force stop ends the cycle',
            t.get('joined') == 2 and stop.get('cycle') == 0, f'{t} / stop {stop}'),
           ('C-03 cancelled match: auto reward gives nothing', kv(auto).get('rewarded') == 0, repr(auto)),
           ('C-03 cancelled match: no marble row for token 401, ledger unchanged (every end path tried)',
            rows == {'bgb': [], 'bgheal': []} and s1 == s0, f'rows {rows}; where after stop {w_after}; npc clicks {clicked}'),
           ('cancel ledger invariants: 0 violations', invariants() == [], repr(invariants()[:3]))]
    g2c_state('cancel', {'day': day, 'stop': stop, 'where_after_stop': w_after, 'clicked': clicked, 'auto': auto, 'rows': rows})
    g2c_record('G2C-CANCEL', res)
    bgcmd(g, '@mbqabgreset', '[MBQA] bgreset')
    for x in (g, b, h):
        x.logout()


# ---------------------------------------------------------------------------- BG auto reward fix (BG-A1..A5)
BGA_DAY = '2026-11-27 12:00:00'        # a1 Tierra / a2 Flavius / a3 KVM, one cycle token 501
BGA_DAY_OFF = 20261128                 # a4: marble BG credit off for the day, Tierra token 601
G2C_REF = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'doc', 'need_marble', 'g2c_evidence', 'g2c_m1.json')
DIALOG = ('mes', 'next', 'close', 'menu', 'input')
BGA_NAMES = {'bgafk': 'MBQA_BGAFK', 'bga': 'MBQA_BGA1', 'bgb': 'MBQA_BGB', 'bgheal': 'MBQA_BGHEAL', 'bgdef': 'MBQA_BGDEF',
             'bgdead': 'MBQA_BGDEAD', 'bgc': 'MBQA_BGC', 'bgleave': 'MBQA_BGLEAVE', 'bgrelog': 'MBQA_BGRELOG', 'bgclk': 'MBQA_BGCLK'}


def bga_auto(g, clients, limit=5):
    """The real auto reward once, timed. Nobody answers anything; returns (reply, seconds, dialog packets per client)."""
    marks = {w: c.mark() for w, c in clients.items()}
    m = g.mark()
    t0 = time.time()
    g.say('@mbqabgauto')
    r = g.wait_text(m, '[MBQA] bgauto', limit)
    sec = round(time.time() - t0, 1)
    if r is None:
        r = g.wait_text(m, '[MBQA] bgauto', 75)    # how long the loop really took, for the report
        sec = round(time.time() - t0, 1)
    time.sleep(1)
    return r, sec, {w: [k if k != 'mes' else 'mes:' + str(d[1])[:40] for _, k, d in c.events[marks[w]:] if k in DIALOG]
                    for w, c in clients.items()}


def npc_read(g, c, team, timeout=20):
    """The player clicks the real reward NPC and reads the dialog to the end. Returns (mes lines, finished)."""
    gid = int(kv(bgcmd(g, f'@mbqabgnpc {team}', '[MBQA] bgnpc', wait=3)).get('gid'))
    mes, texts, done = c.dialog(gid, lambda kind, items_, m: None if kind == 'menu' else 0, timeout=timeout)
    log(f'   [{c.tag}] npc {gid} done={done} mes={len(mes)}')
    return mes, done


def npc_hold(g, c, team):
    """The player clicks the real reward NPC and leaves the dialog open at its close button (close2). Returns the npc id."""
    import struct
    gid = int(kv(bgcmd(g, f'@mbqabgnpc {team}', '[MBQA] bgnpc', wait=3)).get('gid'))
    m = c.mark()
    c.expect_close = True
    c.raw(struct.pack('<HIB', 0x0090, gid, 1))
    return c.wait_event(m, 'close', 10)


def npc_release(c, npc):
    import struct
    c.raw(struct.pack('<HI', 0x0146, npc))
    c.expect_close = False


def bga_rows(w, token, typ):
    return [r for r in bg_rows(AID[w]) if r[0] == token and r[1] == typ]


def bga_idle_afk():
    """The AFK character logs in first and stays idle longer than the 8 s activity window before the match."""
    return login('bgafk')


def cmd_bga(a):
    """BG auto reward fix: the auto reward pays without any dialog, so an AFK character first in the loop holds nobody back.
    Phases a1 (Tierra) a2 (Flavius) a3 (KVM) a4 (marble switch off), run in this order on a DB where G2-C ran first."""
    ph = a.rest[0] if a.rest else 'a1'
    globals()['bga_' + ph]()


def bga_a1():
    """Tierra token 501: AFK first in the auto loop with normal members behind it, NPC winner / loser, a loser holding the
    NPC dialog open while the auto reward runs, then duplicates, the reverse order (auto first, NPC after) and a character change."""
    sql("INSERT IGNORE INTO need_marble_policy (policy_key,effective_day,value,changed_by,changed_at) VALUES (4,20261126,1,'bga_on',NOW())")
    g = login('gm')
    day = clock_to(g, BGA_DAY)
    afk = bga_idle_afk()
    ws = ('bga', 'bgb', 'bgheal', 'bgdef', 'bgdead', 'bgc', 'bgleave')
    cl = {w: login(w) for w in ws}
    time.sleep(9)
    names = {w: BGA_NAMES[w] for w in ('bgafk',) + ws}
    i0 = {w: items(g, n) for w, n in names.items()}
    s0 = {w: score(AID[w]) for w in names}
    t = bg_start(g, 1, 501, ['MBQA_BGAFK', 'MBQA_BGHEAL', 'MBQA_BGDEF', 'MBQA_BGA1'], ['MBQA_BGB', 'MBQA_BGDEAD', 'MBQA_BGC', 'MBQA_BGLEAVE'])
    res = [('A1 setup: Tierra token 501, 8 joined, the AFK character first in the Guillaume list (first in the auto loop)',
            t.get('gcount') == 4 and t.get('ccount') == 4 and t.get('joined') == 8, repr(t))]
    time.sleep(2)
    pos = {'bgheal': (50, 375), 'bgdef': (51, 374), 'bga': (50, 374), 'bgb': (42, 16), 'bgdead': (43, 16), 'bgc': (42, 17), 'bgleave': (43, 17)}
    clients = {w: (cl[w], x, y) for w, (x, y) in pos.items()}
    heal = (cl['bgheal'], cl['bga'].aid)
    play(clients, 6, heal=heal)
    cl['bgleave'].logout()                    # leaves in the middle of the match and does not come back
    play(clients, 6, heal=heal, skip=('bgleave',))
    bgcmd(g, '@mbqabgend 1', '[MBQA] bgend')  # Guillaume wins
    mes_w, done_w = npc_read(g, cl['bga'], 1)
    hold = npc_hold(g, cl['bgc'], 2)           # Croix loser: dialog open, close button not pressed yet
    i_hold = items(g, 'MBQA_BGC')
    auto_cl = {w: cl[w] for w in ('bgheal', 'bgdef', 'bgb', 'bgdead')}
    auto_cl['bgafk'] = afk
    auto, sec, dlg = bga_auto(g, auto_cl)
    i1 = {w: items(g, n) for w, n in names.items()}
    w1 = {w: where(g, names[w]) for w in ('bgafk', 'bgheal', 'bgdef', 'bgb', 'bgdead', 'bgc')}
    npc_release(cl['bgc'], hold)
    time.sleep(2)
    i2c = items(g, 'MBQA_BGC')
    wc = where(g, 'MBQA_BGC')
    ref = json.load(open(G2C_REF, encoding='utf-8'))['items_delta_m1']    # G2-C M1 NPC path, before this fix
    ref_w, ref_l = ref['MBQA_BGA1'], ref['MBQA_BGB']
    d = {w: delta(i0[w], i1[w]) for w in names}
    d_c = delta(i0['bgc'], i2c)
    st = {'day': day, 'auto': auto, 'auto_s': sec, 'dialogs': dlg, 'delta_first_auto': d, 'delta_bgc_held': delta(i0['bgc'], i_hold),
          'delta_bgc_final': d_c, 'where_after_auto': w1, 'where_bgc_final': wc, 'npc_winner_mes': len(mes_w), 'ref_g2c': ref}
    res.append(('BG-A1 auto reward returned at once with nobody answering (AFK first in the loop): <= 5 s, 5 rewarded',
                sec <= 5 and kv(auto).get('rewarded') == 5, f'{auto!r} in {sec} s'))
    res.append(('BG-A1 members behind the AFK character got the existing BG reward in the FIRST auto call (winners = NPC winner)',
                d['bgheal'] == d['bga'] == d['bgdef'] and d['bga'] == ref_w, f"heal {d['bgheal']} def {d['bgdef']} npc {d['bga']}"))
    res.append(('BG-A1 losers behind the AFK character got the existing loser reward in the first auto call',
                d['bgb'] == d['bgdead'] == ref_l, f"b {d['bgb']} dead {d['bgdead']} ref {ref_l}"))
    res.append(('BG-A1 the AFK character itself gets the existing reward as before (existing BG pays everyone on the map)',
                d['bgafk'] == ref_w, f"afk {d['bgafk']}"))
    res.append(('BG-A1 auto reward sent no NPC dialog packet to any rewarded client',
                all(v == [] for v in dlg.values()), repr(dlg)))
    res.append(('BG-A1 every auto-rewarded member left the BG and is in bat_room',
                all(w1[w].get('map') == 'bat_room' and w1[w].get('bg') == 0 for w in ('bgafk', 'bgheal', 'bgdef', 'bgb', 'bgdead')), repr(w1)))
    res.append(('BG-A2 NPC winner: dialog shown and closed as before, reward identical to G2-C (before the fix)',
                done_w and len(mes_w) >= 3 and d['bga'] == ref_w, f'mes {len(mes_w)} done {done_w} delta {d["bga"]}'))
    res.append(('BG-A3 dialog held open during the auto reward: auto skips it (claim key), only the BG point set before close2 so far',
                st['delta_bgc_held'] == {'bgp': 3} and d['bgc'] == {'bgp': 3} and w1['bgc'].get('map') == 'bat_a01',
                f"held {st['delta_bgc_held']} after auto {d['bgc']} where {w1['bgc']}"))
    res.append(('BG-A2/A3 the held loser closes the dialog: exactly one loser reward (= G2-C NPC loser), then bat_room',
                d_c == ref_l and wc.get('map') == 'bat_room', f'final {d_c} where {wc}'))
    gained = {w: score(AID[w]) - s0[w] for w in names}
    rows = {w: bga_rows(w, 501, 1) for w in names}
    ok_one = lambda w: len(rows[w]) == 1 and rows[w][0][:6] == (501, 1, day, 50, 50, 1)
    res.append(('BG-A3 marble: +50 once for every non-AFK member still there (NPC, held dialog and auto paths alike)',
                all(gained[w] == 50 and ok_one(w) for w in ('bga', 'bgb', 'bgheal', 'bgdef', 'bgdead', 'bgc')), f'{gained} {rows}'))
    res.append(('BG-A3 marble: AFK 0 and leaver 0, no row', gained['bgafk'] == 0 and rows['bgafk'] == [] and gained['bgleave'] == 0 and rows['bgleave'] == [],
                f"afk {rows['bgafk']} leaver {rows['bgleave']}"))
    # duplicates: second auto call, reverse order (auto first, then the NPC), character change
    online = ('bgafk', 'bgb', 'bgheal', 'bgdef', 'bgdead', 'bgc')
    s1 = {w: acct_snapshot(AID[w]) for w in names}
    i_pre = {w: items(g, names[w]) for w in online}
    auto2, sec2, dlg2 = bga_auto(g, auto_cl)
    g.command('@warp bat_a01 46 20')
    time.sleep(1)
    g.command('@recall MBQA_BGB')
    time.sleep(1.5)
    w_back = where(g, 'MBQA_BGB')
    mes_b, done_b = npc_read(g, cl['bgb'], 2, timeout=6)
    w_b2 = where(g, 'MBQA_BGB')
    g.command('@warp prontera 156 191')
    cl['bga'].logout()
    wait_offline(AID['bga'])
    a2 = login('bga', 1)
    rc_a2 = kv(bgcmd(a2, '@mbqabgcredit Tierra 501', '[MBQA] bgcredit', wait=3)).get('rc')
    i_post = {w: items(g, names[w]) for w in online}
    s2 = {w: acct_snapshot(AID[w]) for w in names}
    st.update({'auto2': auto2, 'npc_after_auto': {'where_back': w_back, 'mes': mes_b, 'where_after': w_b2}, 'rc_bga2': rc_a2})
    res.append(('BG-A3 second auto call: 0 rewarded, existing rewards unchanged', kv(auto2).get('rewarded') == 0 and i_post == i_pre,
                f'{auto2!r}; changed {[w for w in online if i_pre[w] != i_post[w]]}'))
    res.append(('BG-A3 reverse order: auto-rewarded member back at the NPC gets nothing, no dialog, sent to bat_room',
                w_back.get('map') == 'bat_a01' and mes_b == [] and i_post['bgb'] == i_pre['bgb'] and w_b2.get('map') == 'bat_room',
                f'back {w_back} mes {mes_b} after {w_b2}'))
    res.append(('BG-A3 character change (MBQA_BGA2): marble ALREADY (-4)', rc_a2 == RC['ALREADY'], f'rc {rc_a2}'))
    res.append(('BG-A3 duplicates changed no marble row', s2 == s1, f'changed {[w for w in names if s1[w] != s2[w]]}'))
    bgcmd(g, '@mbqabgreset', '[MBQA] bgreset')
    lv = login('bgleave')
    time.sleep(2)
    d_lv = delta(i0['bgleave'], items(g, 'MBQA_BGLEAVE'))
    res.append(('BG-A4 leaver (did not come back): no existing BG reward, no marble row', d_lv == {} and bga_rows('bgleave', 501, 1) == [], f'delta {d_lv}'))
    res.append(('A1 ledger invariants: 0 violations', invariants() == [], repr(invariants()[:3])))
    st['delta_leaver'] = d_lv
    st['ref_w'], st['ref_l'] = d['bga'], d_c
    g2c_state('bga1', st)
    g2c_record('BGA-A1', res)
    for c in [g, afk, a2, lv] + [cl[w] for w in cl if w not in ('bga', 'bgleave')]:
        if c.alive:
            c.logout()


def bga_a2():
    """Flavius token 501: AFK first, NPC winner (Croix) / NPC loser (Guillaume) as the reference, auto for the rest."""
    g = login('gm')
    day = clock_to(g, BGA_DAY)
    afk = bga_idle_afk()
    ws = ('bgheal', 'bgdead', 'bgb', 'bgdef')
    cl = {w: login(w) for w in ws}
    time.sleep(9)
    names = {w: BGA_NAMES[w] for w in ('bgafk',) + ws}
    i0 = {w: items(g, n) for w, n in names.items()}
    s0 = {w: score(AID[w]) for w in names}
    t = bg_start(g, 2, 501, ['MBQA_BGAFK', 'MBQA_BGHEAL', 'MBQA_BGDEAD'], ['MBQA_BGB', 'MBQA_BGDEF'])
    res = [('A2 setup: Flavius token 501, 5 joined, AFK first', t.get('joined') == 5, repr(t))]
    time.sleep(2)
    play({'bgheal': (cl['bgheal'], 10, 291), 'bgdead': (cl['bgdead'], 11, 291), 'bgb': (cl['bgb'], 390, 11), 'bgdef': (cl['bgdef'], 389, 11)},
         9, heal=(cl['bgheal'], cl['bgdead'].aid))
    bgcmd(g, '@mbqabgend 2', '[MBQA] bgend')  # Croix wins
    mes_w, done_w = npc_read(g, cl['bgdef'], 2)
    mes_l, done_l = npc_read(g, cl['bgdead'], 1)
    auto, sec, dlg = bga_auto(g, {'bgafk': afk, 'bgheal': cl['bgheal'], 'bgb': cl['bgb']})
    i1 = {w: items(g, n) for w, n in names.items()}
    bgcmd(g, '@mbqabgreset', '[MBQA] bgreset')
    d = {w: delta(i0[w], i1[w]) for w in names}
    gained = {w: score(AID[w]) - s0[w] for w in names}
    rows = {w: bga_rows(w, 501, 2) for w in names}
    res += [('BG-A1/A5 Flavius auto reward at once with AFK first: <= 5 s, 3 rewarded', sec <= 5 and kv(auto).get('rewarded') == 3, f'{auto!r} in {sec} s'),
            ('BG-A5 Flavius winner behind the AFK character (auto) = NPC winner; badge 7829 x3 as in F_BG_Badge',
             d['bgb'] == d['bgdef'] and d['bgdef'].get('7829') == 3, f"auto {d['bgb']} npc {d['bgdef']}"),
            ('BG-A5 Flavius losers (auto: AFK, healer) = NPC loser; badge 7829 x2, BG point +3',
             d['bgheal'] == d['bgafk'] == d['bgdead'] and d['bgdead'].get('7829') == 2 and d['bgdead'].get('bgp') == 3,
             f"heal {d['bgheal']} afk {d['bgafk']} npc {d['bgdead']}"),
            ('BG-A2 Flavius NPC dialogs shown and closed as before', done_w and done_l and len(mes_w) >= 3 and len(mes_l) >= 3, f'{len(mes_w)} / {len(mes_l)}'),
            ('BG-A1 no NPC dialog packet to auto-rewarded clients', all(v == [] for v in dlg.values()), repr(dlg)),
            ('BG-A3 marble Flavius type 2: +50 once for non-AFK members, AFK 0',
             all(gained[w] == 50 and len(rows[w]) == 1 and rows[w][0][:6] == (501, 2, day, 50, 50, 1) for w in ws) and rows['bgafk'] == [] and gained['bgafk'] == 0,
             f'{gained} {rows}'),
            ('A2 ledger invariants: 0 violations', invariants() == [], repr(invariants()[:3]))]
    g2c_state('bga2', {'day': day, 'auto': auto, 'auto_s': sec, 'dialogs': dlg, 'delta': d, 'rows': rows})
    g2c_record('BGA-A2', res)
    for c in [g, afk] + list(cl.values()):
        c.logout()


def bga_a3():
    """KVM token 501: the existing KVM auto reward (no dialog, unchanged) with AFK first."""
    g = login('gm')
    day = clock_to(g, BGA_DAY)
    afk = bga_idle_afk()
    ws = ('bgrelog', 'bgc', 'bgclk')
    cl = {w: login(w) for w in ws}
    time.sleep(9)
    names = {w: BGA_NAMES[w] for w in ('bgafk',) + ws}
    i0 = {w: items(g, n) for w, n in names.items()}
    s0 = {w: score(AID[w]) for w in names}
    t = bg_start(g, 3, 501, ['MBQA_BGAFK', 'MBQA_BGRELOG', 'MBQA_BGC'], ['MBQA_BGCLK'])
    res = [('A3 setup: KVM token 501, 4 joined, AFK first', t.get('joined') == 4, repr(t))]
    time.sleep(2)
    play({'bgrelog': (cl['bgrelog'], 52, 129), 'bgc': (cl['bgc'], 53, 129), 'bgclk': (cl['bgclk'], 147, 55)}, 9)
    bgcmd(g, '@mbqabgend 1', '[MBQA] bgend')  # Guillaume wins
    mes_w, done_w = npc_read(g, cl['bgc'], 1)
    auto, sec, dlg = bga_auto(g, {'bgafk': afk, 'bgrelog': cl['bgrelog'], 'bgclk': cl['bgclk']})
    i1 = {w: items(g, n) for w, n in names.items()}
    bgcmd(g, '@mbqabgreset', '[MBQA] bgreset')
    d = {w: delta(i0[w], i1[w]) for w in names}
    gained = {w: score(AID[w]) - s0[w] for w in names}
    rows = {w: bga_rows(w, 501, 3) for w in names}
    res += [('BG-A5 KVM auto reward at once with AFK first (existing path, no change needed): <= 5 s, 3 rewarded',
             sec <= 5 and kv(auto).get('rewarded') == 3, f'{auto!r} in {sec} s'),
            ('BG-A5 KVM winners by auto (AFK, member behind it) = NPC winner', d['bgafk'] == d['bgrelog'] == d['bgc'] and d['bgc'].get('7773', 0) > 0,
             f"afk {d['bgafk']} relog {d['bgrelog']} npc {d['bgc']}"),
            ('BG-A5 KVM loser by auto: badges, 6378 x2, BG point +3', d['bgclk'].get('7773', 0) > 0 and d['bgclk'].get('6378') == 2 and d['bgclk'].get('bgp') == 3,
             repr(d['bgclk'])),
            ('BG-A5 KVM NPC dialog shown and closed as before', done_w and len(mes_w) >= 2, f'mes {len(mes_w)}'),
            ('BG-A5 KVM auto reward sent no NPC dialog packet', all(v == [] for v in dlg.values()), repr(dlg)),
            ('BG-A3 marble KVM type 3: +50 once for non-AFK members, AFK 0',
             all(gained[w] == 50 and len(rows[w]) == 1 and rows[w][0][:6] == (501, 3, day, 50, 50, 1) for w in ws) and rows['bgafk'] == [],
             f'{gained} {rows}'),
            ('A3 ledger invariants: 0 violations', invariants() == [], repr(invariants()[:3]))]
    g2c_state('bga3', {'day': day, 'auto': auto, 'auto_s': sec, 'dialogs': dlg, 'delta': d, 'rows': rows})
    g2c_record('BGA-A3', res)
    for c in [g, afk] + list(cl.values()):
        c.logout()


def bga_a4():
    """Marble BG credit switched off for the day: the existing Tierra auto reward pays exactly as with it on (a1)."""
    a1 = g2c_state('bga1')
    sql(f"INSERT IGNORE INTO need_marble_policy (policy_key,effective_day,value,changed_by,changed_at) VALUES (4,{BGA_DAY_OFF},0,'bga_off',NOW())")
    g = login('gm')
    day = clock_to(g, '2026-11-28 12:00:00')
    afk = bga_idle_afk()
    cl = {w: login(w) for w in ('bgheal', 'bgb')}
    time.sleep(9)
    names = {w: BGA_NAMES[w] for w in ('bgafk', 'bgheal', 'bgb')}
    i0 = {w: items(g, n) for w, n in names.items()}
    s0 = {w: acct_snapshot(AID[w]) for w in names}
    t = bg_start(g, 1, 601, ['MBQA_BGAFK', 'MBQA_BGHEAL'], ['MBQA_BGB'])
    play({'bgheal': (cl['bgheal'], 50, 375), 'bgb': (cl['bgb'], 42, 16)}, 6)
    bgcmd(g, '@mbqabgend 1', '[MBQA] bgend')
    auto, sec, dlg = bga_auto(g, {'bgafk': afk, 'bgheal': cl['bgheal'], 'bgb': cl['bgb']})
    i1 = {w: items(g, n) for w, n in names.items()}
    bgcmd(g, '@mbqabgreset', '[MBQA] bgreset')
    s1 = {w: acct_snapshot(AID[w]) for w in names}
    d = {w: delta(i0[w], i1[w]) for w in names}
    pol = kv(bgcmd(g, '@mbqaflags', '[MBQA] flags', wait=3))
    cash = sql("SELECT COUNT(*) FROM acc_reg_num WHERE `key`='#CASHPOINTS'")[0][0]
    payout = sql('SELECT COUNT(*) FROM need_marble_cp_payout')[0][0]
    res = [('A4 setup: logical day 20261128 with BG credit off, Tierra token 601, 3 joined', day == BGA_DAY_OFF and t.get('joined') == 3, f'day {day}; {t}'),
           ('BG-A4 marble off: auto reward still at once and pays the same existing rewards as with marble on (a1)',
            sec <= 5 and kv(auto).get('rewarded') == 3 and d['bgafk'] == d['bgheal'] == a1.get('ref_w') and d['bgb'] == a1.get('ref_l'),
            f'{auto!r} {sec} s; {d}; on {a1.get("ref_w")} / {a1.get("ref_l")}'),
           ('BG-A4 marble off: no marble row, ledger unchanged', s1 == s0, f'changed {[w for w in s0 if s0[w] != s1[w]]}'),
           ('BG-A4 no dialog packet', all(v == [] for v in dlg.values()), repr(dlg)),
           ('BG-A4 #CASHPOINTS rows 0, CP payout rows 0', cash == '0' and payout == '0', f'cash {cash} payout {payout}'),
           ('A4 ledger invariants: 0 violations', invariants() == [], repr(invariants()[:3]))]
    g2c_state('bga4', {'day': day, 'auto': auto, 'auto_s': sec, 'delta': d, 'flags': pol})
    g2c_record('BGA-A4', res)
    for c in [g, afk] + list(cl.values()):
        c.logout()


# ---------------------------------------------------------------------------- V20-MOCK (CP-1..CP-6)
# Needs: map-server built with apply_test_hooks.py --g2b --v20 (mock CP writer), harness bc_stack_extras.py --g2b --v20,
# sql-files/need_marble_v20_cp_apply.sql and bc_synth_v20.sql (root). p1, then the operator restarts the map-server
# (the mock writer stopped it in CP-5), then p2. The runner never controls a process.
ACC.update({'cpa': ('mbqa_cpa', 'user'), 'cpb': ('mbqa_cpb', 'user')})
AID.update({'cpa': 2000941, 'cpb': 2000942})
NAMES.update({('cpa', 0): 'MBQA_CPA1', ('cpa', 1): 'MBQA_CPA2', ('cpb', 0): 'MBQA_CPB'})
RC.update({'DISABLED': -1, 'NOT_FOUND': -9, 'BAD_STATE': -10, 'REJECTED': -20, 'UNCERTAIN': -22})
CP_TRANSITIONS = {(1, 3, 1), (3, 2, 2), (3, 1, 3), (3, 3, 4), (3, 4, 5), (3, 5, 5)}   # (from, to, reason) the ledger may write


def payouts(aid):
    """{payout_no: (src_type, src_a, src_b, amount, granted, status, operator_ticket)}"""
    return {int(r[0]): tuple(int(x) if x not in ('NULL', None) else 0 for x in r[1:]) for r in
            sql(f'SELECT payout_no,src_type,src_a,src_b,amount,granted,status,COALESCE(operator_ticket,0) FROM need_marble_cp_payout '
                f'WHERE account_id={aid} ORDER BY payout_no')}


def mock_ops(aid):
    ops = {}
    for no, amount, mode in sql(f'SELECT payout_no,amount,mode FROM mbqa_cp_mock_op WHERE account_id={aid} ORDER BY op_id'):
        ops.setdefault(int(no), []).append((int(amount), int(mode)))
    return ops


def mock_bal(aid):
    r = sql(f'SELECT balance FROM mbqa_cp_mock_balance WHERE account_id={aid}')
    return int(r[0][0]) if r else 0


def cp_audit(aid):
    return [tuple(int(x) for x in r) for r in
            sql(f'SELECT payout_no,from_status,to_status,reason,COALESCE(operator_ticket,0) FROM need_marble_cp_audit '
                f'WHERE account_id={aid} ORDER BY audit_id')]


def cp_rc(t):
    return kv(t).get('rc') if t else None


def cpapply(g, name, no, wait=6):
    return cp_rc(harness(g, f'@mbqacpapply {name} {no}', needle='[MBQA] cpapply', wait=wait))


def cpmode(g, mode):
    return kv(harness(g, f'@mbqacpmode {mode}', needle='[MBQA] cpmode', wait=3)).get('mode')


def cpresolve(g, aid, no, to, ticket):
    return cp_rc(harness(g, f'@mbqacpresolve {aid} {no} {to} {ticket}', needle='[MBQA] cpresolve', wait=4))


def v20_inv(aid):
    """Payout ledger vs mock balance: at most one balance change per decision, APPLIED / MANUAL_APPLIED exactly one,
    DECIDED / MANUAL_VOID none, balance = sum of the changes, audit trail ends at the current status."""
    bad = []
    pays, ops, aud = payouts(aid), mock_ops(aid), cp_audit(aid)
    last = {}
    for no, f, t, reason, _ in aud:
        if (f, t, reason) not in CP_TRANSITIONS:
            bad.append(('transition', no, f, t, reason))
        if no in last and last[no] != f:
            bad.append(('audit chain', no, last[no], f))
        last[no] = t
    for no, (_, _, _, amount, granted, status, _) in pays.items():
        o = ops.get(no, [])
        if len(o) > 1:
            bad.append(('applied twice', no, o))
        if status in (2, 4) and (len(o) != 1 or granted != amount or o[0][0] != amount):
            bad.append(('applied without one change', no, status, granted, o))
        if status in (1, 5) and (o or granted != 0):
            bad.append(('change without apply', no, status, granted, o))
        if status == 3 and granted != 0:
            bad.append(('uncertain with granted', no, granted))
        if last.get(no, 1) != status:
            bad.append(('audit end', no, last.get(no), status))
    for no in ops:
        if no not in pays:
            bad.append(('change for no decision', no))
    if mock_bal(aid) != sum(a for o in ops.values() for a, _ in o):
        bad.append(('balance', mock_bal(aid), ops))
    return bad


def map_alive():
    import socket
    try:
        socket.create_connection(('127.0.0.1', 25121), 2).close()
        return True
    except OSError:
        return False


def cmd_v20(a):
    """V20-MOCK: DECIDED -> UNCERTAIN (before the writer) -> APPLIED / back to DECIDED (confirmed no effect) / stays
    UNCERTAIN, with the mock writer of the test build only. Phases p1 (ends with the map-server stopped by the mock), p2."""
    globals()['v20_' + (a.rest[0] if a.rest else 'p1')]()


def v20_p1():
    aid, bid = AID['cpa'], AID['cpb']
    g = login('gm')
    d1 = clock_to(g, '2026-12-01 12:00:00')
    c = login('cpa')
    enter_board(c)
    res = []
    # real decisions from the board (current draft policy): roll until one roll decides a tile CP AND the 33 step bonus
    pair, rolls, bad = None, [], []
    for i in range(80):
        w = wallet(aid)
        if not w or w['dice'] < 1:
            adjust(aid, dice=25, ticket=920000 + i)
        before = set(payouts(aid))
        r = one_roll(c, aid, i)
        new = {k: v for k, v in payouts(aid).items() if k not in before}
        rolls.append({'roll': r['roll'], 'die': r['die'], 'to': r['to'], 'event': r['event'], 'new_payouts': new})
        tile = [k for k, v in new.items() if v[0] == 1]
        lap = [k for k, v in new.items() if v[0] == 2]
        ev_cp = int(r['event'][0][3]) if r['event'] else 0
        if (ev_cp > 0) != bool(tile) or (tile and new[tile[0]][3] != ev_cp) or (tile and new[tile[0]][1] != r['roll']):
            bad.append(('tile', r['roll'], ev_cp, new))
        if any(new[k][3] != 10000 for k in lap):
            bad.append(('lap', r['roll'], new))
        if tile and lap and pair is None:
            pair = (tile[0], lap[0], r['roll'])
        if r['state'] != 0:
            bad.append(('state', r))
            break
        if pair and sum(1 for v in payouts(aid).values() if v[5] == 1) >= 9:
            break
    c.logout()
    wait_offline(aid)
    p0 = payouts(aid)
    st = {'day1': d1, 'rolls': rolls, 'pair': pair, 'payouts_after_rolls': p0}
    res.append(('V20 decisions from real rolls: tile CP = arrival event CP of that roll, 33 step bonus 10000, all DECIDED',
                not bad and pair is not None and all(v[5] == 1 and v[4] == 0 for v in p0.values()),
                f'{len(rolls)} rolls, {len(p0)} payouts, pair {pair}, bad {bad[:3]}'))
    if pair is None:
        g2c_state('v20p1', st)
        g2c_record('V20-P1', res)
        return
    tile, lap, roll = pair
    free = [k for k, v in p0.items() if v[5] == 1 and k not in (tile, lap)]
    X, Y, Z, W, V, U = free[:6]
    st['roles'] = {'tile': tile, 'lap': lap, 'X_double': X, 'Y_no_effect': Y, 'Z_lost_after_change': Z, 'W_lost_before_change': W,
                   'V_left_decided': V, 'U_crash': U}
    c = login('cpa')
    # CP-6 switch off (policy key 5 = 0, the default): DECIDED stays, nothing changes
    r_off = cpapply(g, 'MBQA_CPA1', tile)
    res.append(('CP-6 switch off (key 5 default 0): DISABLED, row DECIDED, mock balance 0, no audit',
                r_off == RC['DISABLED'] and payouts(aid)[tile][5] == 1 and mock_bal(aid) == 0 and cp_audit(aid) == [], f'rc {r_off}'))
    sql("INSERT IGNORE INTO need_marble_policy (policy_key,effective_day,value,changed_by,changed_at) VALUES (5,20261202,1,'v20_mock_on',NOW())")
    d2 = clock_to(g, '2026-12-02 12:00:00')
    # CP-2 tile CP and 33 step bonus decided in the same roll: each applied once under its own key
    r_t, r_l = cpapply(g, 'MBQA_CPA1', tile), cpapply(g, 'MBQA_CPA1', lap)
    p, o = payouts(aid), mock_ops(aid)
    res.append(('CP-2 same roll: tile CP (src 1) and 33 step bonus (src 2) each APPLIED once, balance = sum',
                r_t == RC['OK'] and r_l == RC['OK'] and p[tile][5] == 2 and p[lap][5] == 2 and len(o.get(tile, [])) == 1 and
                len(o.get(lap, [])) == 1 and mock_bal(aid) == p[tile][3] + p[lap][3] and p[tile][1] == roll,
                f'rc {r_t}/{r_l}; tile {p[tile]} lap {p[lap]}; balance {mock_bal(aid)}'))
    # CP-1 the same decision again, the same request delivered twice, the other character of the account
    bal1, n1 = mock_bal(aid), len(p)
    r_again = cpapply(g, 'MBQA_CPA1', tile)
    m = g.mark()
    g.say(f'@mbqacpapply MBQA_CPA1 {X}')
    g.say(f'@mbqacpapply MBQA_CPA1 {X}')
    end = time.time() + 8
    while time.time() < end and sum(1 for t in g.texts_since(m) if '[MBQA] cpapply' in t) < 2:
        time.sleep(0.1)
    dbl = sorted(cp_rc(t) for t in g.texts_since(m) if '[MBQA] cpapply' in t)
    c.logout()
    wait_offline(aid)
    c2 = login('cpa', 1)
    r_c2t, r_c2x = cpapply(g, 'MBQA_CPA2', tile), cpapply(g, 'MBQA_CPA2', X)
    p, o = payouts(aid), mock_ops(aid)
    res.append(('CP-1 same decision again: ALREADY (-4), no second change', r_again == RC['ALREADY'], f'rc {r_again}'))
    res.append(('CP-1 same request delivered twice: one OK, one ALREADY, one change', dbl == [RC['ALREADY'], RC['OK']] and len(o.get(X, [])) == 1,
                f'replies {dbl}; changes {o.get(X)}'))
    res.append(('CP-1 other character of the account (MBQA_CPA2): ALREADY for both, no change, no new decision, no re-draw',
                r_c2t == RC['ALREADY'] and r_c2x == RC['ALREADY'] and mock_bal(aid) == bal1 + p[X][3] and len(p) == n1 and
                all(p[k][:4] == p0[k][:4] for k in p0), f'rc {r_c2t}/{r_c2x}; balance {bal1} -> {mock_bal(aid)}; payouts {n1} -> {len(p)}'))
    # CP-3 refused before any change: back to DECIDED, retry allowed
    bal2 = mock_bal(aid)
    cpmode(g, 1)
    r_y1 = cpapply(g, 'MBQA_CPA2', Y)
    py1, oy1, bal_y1 = payouts(aid)[Y], mock_ops(aid).get(Y, []), mock_bal(aid)
    r_y2 = cpapply(g, 'MBQA_CPA2', Y)
    res.append(('CP-3 writer refused before any change: REJECTED, row back to DECIDED, balance unchanged',
                r_y1 == RC['REJECTED'] and py1[5] == 1 and oy1 == [] and bal_y1 == bal2, f'rc {r_y1}; row {py1}; changes {oy1}'))
    res.append(('CP-3 retry after a confirmed no-effect: APPLIED once', r_y2 == RC['OK'] and payouts(aid)[Y][5] == 2 and len(mock_ops(aid).get(Y, [])) == 1,
                f'rc {r_y2}; row {payouts(aid)[Y]}'))
    # CP-4 balance changed, answer lost -> UNCERTAIN, blocked; answer lost before any change -> UNCERTAIN as well
    cpmode(g, 2)
    r_z1 = cpapply(g, 'MBQA_CPA2', Z)
    bal_z = mock_bal(aid)
    r_z2 = cpapply(g, 'MBQA_CPA2', Z)
    cpmode(g, 4)
    r_w1 = cpapply(g, 'MBQA_CPA2', W)
    p, o = payouts(aid), mock_ops(aid)
    res.append(('CP-4 balance changed but the answer lost: UNCERTAIN (-22), row 3, one change',
                r_z1 == RC['UNCERTAIN'] and p[Z][5] == 3 and len(o.get(Z, [])) == 1, f'rc {r_z1}; row {p[Z]}; changes {o.get(Z)}'))
    res.append(('CP-4 UNCERTAIN asked again: UNCERTAIN, never handed to the writer again (no second change)',
                r_z2 == RC['UNCERTAIN'] and len(o.get(Z, [])) == 1 and mock_bal(aid) == bal_z, f'rc {r_z2}; balance {bal_z} -> {mock_bal(aid)}'))
    res.append(('CP-4 answer lost before any change: UNCERTAIN too (the ledger cannot tell), no change',
                r_w1 == RC['UNCERTAIN'] and p[W][5] == 3 and o.get(W) is None, f'rc {r_w1}; row {p[W]}'))
    # CP-6 another account
    b = login('cpb')
    r_b = cpapply(g, 'MBQA_CPB', V)
    res.append(('CP-6 other account (MBQA_CPB) with a payout_no of mbqa_cpa: NOT_FOUND, nothing changes for either account',
                r_b == RC['NOT_FOUND'] and payouts(aid)[V][5] == 1 and mock_bal(bid) == 0 and payouts(bid) == {} and cp_audit(bid) == [], f'rc {r_b}'))
    inv = v20_inv(aid)
    res.append(('CP invariants before the stop: 0 violations', inv == [], repr(inv[:4])))
    st.update({'day2': d2, 'payouts_before_stop': payouts(aid), 'ops_before_stop': mock_ops(aid), 'balance_before_stop': mock_bal(aid),
               'audit_before_stop': cp_audit(aid)})
    # CP-5 the map-server stops right after the mock changed the balance (before the final record)
    cpmode(g, 3)
    r_u = cpapply(g, 'MBQA_CPA2', U, wait=8)
    time.sleep(3)
    alive = map_alive()
    pu, ou = payouts(aid)[U], mock_ops(aid).get(U, [])
    res.append(('CP-5 map-server stopped between the balance change and the final record: no reply, row left UNCERTAIN, one change',
                r_u is None and not alive and pu[5] == 3 and len(ou) == 1, f'reply {r_u}; map alive {alive}; row {pu}; changes {ou}'))
    st['crash'] = {'reply': r_u, 'map_alive': alive, 'row': pu, 'changes': ou}
    g2c_state('v20p1', st)
    g2c_record('V20-P1', res)
    for x in (g, c2, b):
        x.drop()


def v20_p2():
    aid, bid = AID['cpa'], AID['cpb']
    st = g2c_state('v20p1')
    rl = st['roles']
    tile, Z, W, V, U = rl['tile'], rl['Z_lost_after_change'], rl['W_lost_before_change'], rl['V_left_decided'], rl['U_crash']
    g = login('gm')
    d3 = clock_to(g, '2026-12-03 12:00:00')
    c = login('cpa')   # relog after the restart
    p0, o0, bal0 = payouts(aid), mock_ops(aid), mock_bal(aid)
    res = [('CP-5 after the restart: crashed row still UNCERTAIN with its one change, claim audit only (1 -> 3)',
            p0[U][5] == 3 and len(o0.get(U, [])) == 1 and [x for x in cp_audit(aid) if x[0] == U] == [(U, 1, 3, 1, 0)],
            f'row {p0[U]} changes {o0.get(U)} audit {[x for x in cp_audit(aid) if x[0] == U]}')]
    r_u, r_z, r_t = cpapply(g, 'MBQA_CPA1', U), cpapply(g, 'MBQA_CPA1', Z), cpapply(g, 'MBQA_CPA1', tile)
    res.append(('CP-5 relog + restart + the same requests again: UNCERTAIN stays blocked (-22), APPLIED stays ALREADY (-4), no change',
                r_u == RC['UNCERTAIN'] and r_z == RC['UNCERTAIN'] and r_t == RC['ALREADY'] and mock_ops(aid) == o0 and mock_bal(aid) == bal0,
                f'rc {r_u}/{r_z}/{r_t}; balance {bal0} -> {mock_bal(aid)}'))
    # operator reconciliation: the mock op rows play the role of the evidence an operator checks
    r_bad0 = cpresolve(g, aid, Z, 4, 0)
    r_bad1 = cpresolve(g, aid, Z, 1, 7000)
    r_bad2 = cpresolve(g, aid, tile, 4, 7003)
    r_res_u = cpresolve(g, aid, U, 4, 7001)      # change found -> applied after all
    r_res_w = cpresolve(g, aid, W, 5, 7002)      # no change found -> void
    r_res_z = cpresolve(g, aid, Z, 4, 7004)
    r_after_u, r_after_w = cpapply(g, 'MBQA_CPA1', U), cpapply(g, 'MBQA_CPA1', W)
    p = payouts(aid)
    res.append(('CP-5 reconciliation input checked: ticket 0 / back to DECIDED refused (BAD_ARG), APPLIED row not resolvable (BAD_STATE)',
                r_bad0 == RC['BAD_ARG'] and r_bad1 == RC['BAD_ARG'] and r_bad2 == RC['BAD_STATE'], f'rc {r_bad0}/{r_bad1}/{r_bad2}'))
    res.append(('CP-5 operator: crashed and answer-lost rows with a change -> MANUAL_APPLIED (4), granted = amount, ticket kept',
                r_res_u == RC['OK'] and r_res_z == RC['OK'] and p[U][5] == 4 and p[Z][5] == 4 and p[U][4] == p[U][3] and p[U][6] == 7001,
                f'rc {r_res_u}/{r_res_z}; U {p[U]} Z {p[Z]}'))
    res.append(('CP-5 operator: answer lost with no change -> MANUAL_VOID (5), granted 0', r_res_w == RC['OK'] and p[W][5] == 5 and p[W][4] == 0,
                f'rc {r_res_w}; W {p[W]}'))
    res.append(('CP-5 after reconciliation: MANUAL_APPLIED -> ALREADY, MANUAL_VOID -> BAD_STATE, still no change',
                r_after_u == RC['ALREADY'] and r_after_w == RC['BAD_STATE'] and mock_ops(aid) == o0 and mock_bal(aid) == bal0,
                f'rc {r_after_u}/{r_after_w}'))
    # CP-6 switch off again
    sql("INSERT IGNORE INTO need_marble_policy (policy_key,effective_day,value,changed_by,changed_at) VALUES (5,20261204,0,'v20_mock_off',NOW())")
    d4 = clock_to(g, '2026-12-04 12:00:00')
    r_v = cpapply(g, 'MBQA_CPA1', V)
    res.append(('CP-6 switch off again (key 5 = 0): DISABLED, the DECIDED row stays, balance unchanged',
                r_v == RC['DISABLED'] and payouts(aid)[V][5] == 1 and mock_bal(aid) == bal0, f'rc {r_v}'))
    pf = payouts(aid)
    res.append(('CP-1/CP-6 no new decision and no re-draw over the whole run (same keys and amounts as after the rolls)',
                sorted(pf) == sorted(int(k) for k in st['payouts_after_rolls']) and
                all(pf[int(k)][:4] == tuple(v[:4]) for k, v in st['payouts_after_rolls'].items()), f'{len(pf)} payouts'))
    inv = v20_inv(aid)
    res.append(('CP invariants at the end: 0 violations (one change per applied decision, balance = changes, audit chain)', inv == [], repr(inv[:4])))
    cash = sql("SELECT COUNT(*) FROM acc_reg_num WHERE `key`='#CASHPOINTS'")[0][0]
    cplog = sql('SELECT COUNT(*) FROM cashlog')[0][0]
    res.append(('CP-6 real #CASHPOINTS rows 0, cashlog rows 0, other account untouched', cash == '0' and cplog == '0' and mock_bal(bid) == 0 and payouts(bid) == {},
                f'#CASHPOINTS {cash} cashlog {cplog}'))
    li = invariants()
    res.append(('marble ledger invariants (sql-files) over the whole DB: 0 violations', li == [], repr(li[:3])))
    g2c_state('v20p2', {'day3': d3, 'day4': d4, 'payouts': pf, 'ops': mock_ops(aid), 'balance': mock_bal(aid), 'audit': cp_audit(aid)})
    g2c_record('V20-P2', res)
    for x in (g, c):
        x.logout()


def main():
    global OUT
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('cmd')
    ap.add_argument('rest', nargs='*')
    a = ap.parse_args()
    OUT = os.path.abspath(a.out)
    sys.stdout.reconfigure(encoding='utf-8', errors='backslashreplace')
    globals()['cmd_' + a.cmd](a)


if __name__ == '__main__':
    main()

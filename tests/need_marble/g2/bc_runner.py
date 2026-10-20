"""G2 scenario runner for the isolated stack only (login 26901 / char 26121 / map 25121, DB 127.0.0.1:3327).
Never starts, stops or kills a process (bc_servers.ps1 does that) and writes only to <out>.

usage: python -u -I bc_runner.py --out <scratch>/mbqa_bc <cmd> [args]
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bcclient  # noqa: E402
import bcdb  # noqa: E402

OUT = None
LOG = []
ACC = {'gm': ('mbqa_gm', 'gm'), 'user': ('mbqa_user', 'user'), 'p2': ('mbqa_p2', 'user'), 'p3': ('mbqa_p3', 'user'),
       'p4': ('mbqa_p4', 'user'), 'cap': ('mbqa_cap', 'user')}
NAMES = {('gm', 0): 'MBQA_GM1', ('gm', 1): 'MBQA_GM2', ('user', 0): 'MBQA_U1', ('user', 1): 'MBQA_U2',
         ('p2', 0): 'MBQA_P2', ('p3', 0): 'MBQA_P3', ('p4', 0): 'MBQA_P4', ('cap', 0): 'MBQA_CAP'}
AID = {'gm': 2000901, 'user': 2000902, 'p2': 2000903, 'p3': 2000904, 'p4': 2000905, 'cap': 2000906}


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
    s0 = state(aid)
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

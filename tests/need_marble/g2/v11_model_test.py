"""Offline check of the V11 analysis (v11_model). No DB, no server.

usage: python -I v11_model_test.py
A simulated 1,000-roll run built from the policy in need_marble_logic.hpp must pass every check, and each
injected defect must be caught by the check that is meant to catch it.
"""
import copy
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import v11_model as vm  # noqa: E402

PASS = FAIL = 0


def check(name, cond, detail=''):
    global PASS, FAIL
    if cond:
        PASS += 1
    else:
        FAIL += 1
        print(f'FAIL {name} {detail}')


def simulate(pol, n, seed):
    rnd = random.Random(seed)
    rows, pos, steps, spass = [], 0, 0, 0
    for i in range(n):
        die = rnd.randint(1, 6)
        landing = (pos + die) % 33
        spass += 1 if pos + die >= 33 else 0
        steps += die
        kind = vm.BOARD[landing]
        r = {'from': pos, 'die': die, 'roll': i + 1, 'steps': die, 'dice_used': 1, 'label': '주사위 굴리기', 'state': 0, 'pay': []}
        if kind == vm.WARP:
            r['to'] = pol['warp'][landing]
            r['event'] = [[landing, kind, 100, 0, 0, 0]]
        elif kind == vm.DUNGEON:
            r['to'] = landing
            if i % 2:
                rng = rnd.randint(1, 10000)
                win = rng <= pol['chal_rate']
                cp = pol['chal_cp'] if win else 0
                r['event'] = [[landing, kind, 2 if win else 3, cp, 2, rng]]
            else:
                cp = pol['safe']
                r['event'] = [[landing, kind, 1, cp, 1, 0]]
        else:
            r['to'] = landing
            rng = rnd.randint(1, 10000)
            s, cp = vm.pick(pol, kind, rng)
            r['event'] = [[landing, kind, s + 1, cp, 0, rng]]
        cp = int(r['event'][0][3])
        if cp > 0:
            r['pay'] = [[str(cp), '1']]
        rows.append(r)
        pos = r['to']
    laps = [[str(k + 1), str(pol['lap_cp']), '1'] for k in range(steps // pol['lap_steps'])]
    return rows, spass, steps, laps


pol = vm.policy_from_source(vm.default_logic_path())
check('policy parsed', pol['rates'][1] == [5000, 4000, 1000, 0] and pol['warp'] == {7: 13, 22: 28} and pol['lap_steps'] == 33, pol)
check('policy override', vm.policy_from_source(vm.default_logic_path(), {50: 14, 111: 4100})['warp'][7] == 14)
rows, spass, steps, laps = simulate(pol, 1000, 7)
res = vm.analyze(rows, pol, spass, steps, laps, [['1', '300', '1']], 0)
check('clean run passes', all(ok for _, ok, _ in res), [(c, d) for c, ok, d in res if not ok])
check('ten checks', len(res) == 10, len(res))


def caught(mutate, cid, extra=None):
    rr = copy.deepcopy(rows)
    mutate(rr)
    args = extra or (spass, steps, laps, [['1', '300', '1']], 0)
    r = vm.analyze(rr, pol, *args)
    return [c for c, ok, _ in r if not ok and c.startswith(cid)] != []


table = next(i for i, r in enumerate(rows) if int(r['event'][0][1]) in vm.TABLE_KINDS)
warp = next(i for i, r in enumerate(rows) if int(r['event'][0][1]) == vm.WARP)
check('second event caught', caught(lambda rr: rr[3]['event'].append(rr[3]['event'][0]), 'V11-1'))
check('two dice for one roll caught', caught(lambda rr: rr[4].__setitem__('dice_used', 2), 'V11-1'))
check('wrong landing caught', caught(lambda rr: rr[5]['event'][0].__setitem__(0, (rr[5]['event'][0][0] + 1) % 33), 'V11-1'))
check('wrong slot caught', caught(lambda rr: rr[table]['event'][0].__setitem__(2, rr[table]['event'][0][2] % 4 + 1), 'V11-4'))
check('warp to a wrong tile caught', caught(lambda rr: rr[warp].__setitem__('to', 0), 'V11-5'))
check('warp with CP caught', caught(lambda rr: rr[warp]['pay'].append(['500', '1']), 'V11-5'))
check('CP not DECIDED caught', caught(lambda rr: rr[table].__setitem__('pay', [[str(rr[table]['event'][0][3]), '2']]) if int(rr[table]['event'][0][3]) else rr[table].__setitem__('pay', [['1', '1']]), 'V11-7'))
check('START pass mismatch caught', caught(lambda rr: None, 'V11-8', (spass + 1, steps, laps, [['1', '300', '1']], 0)))
check('lap rows mismatch caught', caught(lambda rr: None, 'V11-9', (spass, steps, laps[:-1], [['1', '300', '1']], 0)))
check('#CASHPOINTS caught', caught(lambda rr: None, 'V11-10', (spass, steps, laps, [['1', '300', '1']], 1)))
biased = copy.deepcopy(rows)
for r in biased[:400]:
    r['die'] = 6
    r['steps'] = 6
check('biased die caught', [c for c, ok, _ in vm.analyze(biased, pol) if not ok and c.startswith('V11-2')] != [])
skew = copy.deepcopy(rows)
for r in skew:
    e = r['event'][0]
    if int(e[1]) in vm.TABLE_KINDS:
        e[5] = (int(e[5]) - 1) % 3000 + 1
        s, cp = vm.pick(pol, int(e[1]), e[5])
        e[2], e[3] = s + 1, cp
        r['pay'] = [[str(cp), '1']] if cp else []
check('skewed draws caught', [c for c, ok, _ in vm.analyze(skew, pol) if not ok and c.startswith('V11-3')] != [])
print(f'v11_model offline checks: {PASS} passed, {FAIL} failed')
sys.exit(1 if FAIL else 0)

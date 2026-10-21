"""Offline fixtures for the V22 comparator (v22_model). No DB, no server.

usage: python -I v22_model_test.py

Fixtures (expected DB differences are written next to each case):
  1 clean      account and day empty
  2 prior1     one kill and one batch row before the feed (the state that broke the first V22 runner)
  3 capped     next_tier already past max_tiers, more kills arrive after a relog
  4 total cap  (extra) 150 of the 200 daily total already earned elsewhere
Each fixture also runs mutated "DB results" that the comparator has to reject.
"""
import copy
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import v22_model as vm  # noqa: E402

DAY = 20261021
POL = dict(vm.DEFAULT_POLICY)
PASS = FAIL = 0


def check(name, cond, detail=''):
    global PASS, FAIL
    if cond:
        PASS += 1
    else:
        FAIL += 1
        print(f'FAIL {name} {detail}')


def empty():
    return {'day': DAY, 'progress': {}, 'batches': [], 'milestones': [], 'earn': [], 'tx_count': 0,
            'period': {}, 'score': 0, 'dice': 0}


def db_after(before, login, session):
    """What a correct ledger holds after `login` (batch numbers 1.. in a new collect session)."""
    exp = vm.expected(before, login)
    snap = {k: copy.deepcopy(v) for k, v in exp.items() if k != 'new_batches'}
    snap['batches'] = list(before['batches']) + [(session, i + 1, DAY, k) for i, k in enumerate(exp['new_batches'])]
    return snap


def all_ok(results):
    return all(ok for _, ok, _ in results)


def failed(results):
    return [cid for cid, ok, _ in results if not ok]


def old_rule(after):
    """The first V22 runner: absolute kills 400 and 20 batches."""
    p = after['progress'].get(DAY)
    return p is not None and p == (400, 5) and len(after['batches']) == 20


# ---------------------------------------------------------------- fixture 1: clean account and day
def fixture_clean():
    s0 = empty()
    check('F1 fixture_state', vm.fixture_state(s0, POL) == 'clean')
    m = vm.Login(s0, POL)
    counted = m.feed(30000)
    # expected DB difference: progress (400, 5), 20 batch rows of 20 kills, tier rows 1..4 credited 25 each,
    # 4 EARN rows, period hunt / total 100, score +100; counted (DB + memory) = 400
    check('F1 counted', counted == 400, counted)
    check('F1 progress', (m.kills, m.next_tier) == (400, 5), (m.kills, m.next_tier))
    check('F1 batches', m.batches == [20] * 20, m.batches)
    check('F1 tiers', [t[:3] for t in m.milestones] == [(1, 25, 25), (2, 25, 25), (3, 25, 25), (4, 25, 25)], m.milestones)
    s1 = db_after(s0, m, 1)
    r = vm.compare(s0, s1, vm.expected(s0, m), POL, 'F1')
    check('F1 compare correct DB', all_ok(r), failed(r))
    check('F1 period', s1['period'] == {(DAY, 0): 100, (DAY, 1): 100} and s1['score'] == 100 and s1['tx_count'] == 4, s1['period'])
    # after the cap: 5,000 more and the logout flush write nothing
    check('F1 +5000 counted 0', m.feed(5000) == 0)
    m.logout()
    s2 = db_after(s0, m, 1)
    check('F1 +5000 unchanged', all_ok(vm.unchanged(s1, s2, 'F1')))
    check('F1 old rule happened to hold on a clean account', old_rule(s1))
    # mutations the comparator has to reject
    bad = copy.deepcopy(s1)
    bad['milestones'].append((DAY, 5, 25, 0, 2))
    check('F1 reject a 5th tier row', not all_ok(vm.compare(s0, bad, vm.expected(s0, m), POL, 'F1')))
    bad = copy.deepcopy(s1)
    bad['batches'].append((1, 21, DAY, 20))
    bad['progress'][DAY] = (420, 5)
    check('F1 reject a batch after the cap', not all_ok(vm.compare(s0, bad, vm.expected(s0, m), POL, 'F1')))
    check('F1 reject a batch after the cap (unchanged)', not all_ok(vm.unchanged(s1, bad, 'F1')))
    bad = copy.deepcopy(s1)
    bad['batches'].append((2, 1, DAY, 20))   # I11 break: batch row without the kill count
    check('F1 reject kills != batch sum', 'F1 a4 I11 kills = sum of batches' in failed(vm.compare(s0, bad, vm.expected(s0, m), POL, 'F1')))
    bad = copy.deepcopy(s1)
    bad['period'][(DAY, 1)] = 125
    bad['period'][(DAY, 0)] = 125
    check('F1 reject hunt earned over 100', 'F1 b1 daily hunt cap' in failed(vm.compare(s0, bad, vm.expected(s0, m), POL, 'F1')))
    bad = copy.deepcopy(s1)
    bad['score'] += 25
    check('F1 reject wallet drift', 'F1 d1 wallet' in failed(vm.compare(s0, bad, vm.expected(s0, m), POL, 'F1')))
    bad = copy.deepcopy(s1)
    bad['earn'].append((DAY, 4, 25))
    bad['tx_count'] += 1
    check('F1 reject a duplicate EARN row', 'F1 c1 EARN rows match model' in failed(vm.compare(s0, bad, vm.expected(s0, m), POL, 'F1')))
    return s1


# ---------------------------------------------------------------- fixture 2: one kill and one batch first
def fixture_prior1():
    s0 = empty()
    s0['progress'][DAY] = (1, 1)
    s0['batches'] = [(1, 1, DAY, 1)]
    check('F2 fixture_state', vm.fixture_state(s0, POL) == 'prior1')
    m = vm.Login(s0, POL)
    counted = m.feed(30000)
    # expected DB difference: 20 new batch rows of 20 (the 20th crosses 400 at 401), progress (401, 5),
    # tiers 1..4 x 25, hunt 100, score +100; the old batch row stays as it was
    check('F2 counted', counted == 400, counted)
    check('F2 progress', (m.kills, m.next_tier) == (401, 5), (m.kills, m.next_tier))
    check('F2 batches', m.batches == [20] * 20, m.batches)
    s1 = db_after(s0, m, 2)
    r = vm.compare(s0, s1, vm.expected(s0, m), POL, 'F2')
    check('F2 compare correct DB', all_ok(r), failed(r))
    # the F1/F2 retest observation (kills 401, 21 batches, tier 4, next_tier 5, nothing after 5,000 more)
    obs = {'progress': s1['progress'][DAY], 'batches': len(s1['batches']), 'tiers': len([x for x in s1['milestones'] if x[0] == DAY])}
    check('F2 matches the observed run', obs == {'progress': (401, 5), 'batches': 21, 'tiers': 4}, obs)
    check('F2 old absolute rule fails on the same correct DB (the defect)', not old_rule(s1))
    check('F2 +5000 counted 0', m.feed(5000) == 0)
    m.logout()
    check('F2 logout unchanged', all_ok(vm.unchanged(s1, db_after(s0, m, 2), 'F2')))
    bad = copy.deepcopy(s1)
    bad['batches'][0] = (1, 1, DAY, 2)        # the prior row may not change
    check('F2 reject a changed old batch row', 'F2 a3 old batch rows unchanged' in failed(vm.compare(s0, bad, vm.expected(s0, m), POL, 'F2')))
    bad = copy.deepcopy(s1)
    bad['progress'][DAY] = (400, 5)           # an absolute 400 is wrong here
    check('F2 reject kills 400 (absolute)', not all_ok(vm.compare(s0, bad, vm.expected(s0, m), POL, 'F2')))


# ---------------------------------------------------------------- fixture 3: capped account, more kills
def fixture_capped(capped):
    s0 = copy.deepcopy(capped)
    check('F3 fixture_state', vm.fixture_state(s0, POL) == 'capped')
    m = vm.Login(s0, POL)
    # expected DB difference: none (F2 layer 1 stops every kill of this login; layer 2 would refuse a batch)
    check('F3 real kills counted 0', m.feed(10) == 0)
    check('F3 feed counted 0', m.feed(5000) == 0)
    m.logout()
    s1 = db_after(s0, m, 3)
    check('F3 no new batch', m.batches == [] and m.milestones == [])
    check('F3 unchanged', all_ok(vm.unchanged(s0, s1, 'F3')))
    r = vm.compare(s0, s1, vm.expected(s0, m), POL, 'F3')
    check('F3 compare', all_ok(r), failed(r))
    # layer 2 alone (layer 1 lookup failed and returned false): the checkpoint still writes nothing
    m2 = vm.Login(s0, POL)
    m2.done = False
    m2.feed(25)
    check('F3 layer 2 alone writes nothing', m2.batches == [] and m2.kills == s0['progress'][DAY][0])
    bad = copy.deepcopy(s0)
    bad['batches'].append((3, 1, DAY, 20))
    bad['progress'][DAY] = (s0['progress'][DAY][0] + 20, 5)
    check('F3 reject a batch on a capped day', not all_ok(vm.unchanged(s0, bad, 'F3')) and
          not all_ok(vm.compare(s0, bad, vm.expected(s0, m), POL, 'F3')))


# ---------------------------------------------------------------- fixture 4: total cap partly used
def fixture_total_cap():
    s0 = empty()
    s0['period'] = {(DAY, 0): 150, (DAY, 2): 100}   # 100 from dungeons and 50 from a battleground
    s0['period'][(DAY, 3)] = 50
    s0['score'] = 150
    m = vm.Login(s0, POL)
    m.feed(30000)
    # expected: tiers 1..4 rows, credited 25 / 25 / 0 / 0 (outcome 2 = capped), hunt 50, total 200
    check('F4 tiers', [t[2:] for t in m.milestones] == [(25, 1), (25, 1), (0, 2), (0, 2)], m.milestones)
    s1 = db_after(s0, m, 4)
    r = vm.compare(s0, s1, vm.expected(s0, m), POL, 'F4')
    check('F4 compare', all_ok(r), failed(r))
    check('F4 totals', s1['period'][(DAY, 1)] == 50 and s1['period'][(DAY, 0)] == 200 and s1['score'] == 200 and s1['tx_count'] == 2)


def misc():
    check('parse feed', vm.parse_feed('[MBQA] feed n=30000 counted=400') == (30000, 400))
    check('parse dead', vm.parse_feed('[MBQA] feed n=-2 counted=-1') == (-2, -1))
    check('parse old reply', vm.parse_feed('[MBQA] feed n=5000') == (5000, None))
    check('parse none', vm.parse_feed(None) == (None, None))
    s = empty()
    s['progress'][DAY] = (37, 1)
    s['batches'] = [(1, 1, DAY, 20), (1, 2, DAY, 17)]
    check('contaminated fixture -> None', vm.fixture_state(s, POL) is None)
    other = empty()
    other['progress'][DAY - 1] = (400, 5)          # yesterday capped, today clean
    other['batches'] = [(1, 1, DAY - 1, 400)]
    check('yesterday does not count', vm.fixture_state(other, POL) == 'clean')
    m = vm.Login(other, POL)
    check('yesterday capped, today counts', m.feed(40) == 40 and m.batches == [20, 20])
    check('credit_amount mirror', vm.credit_amount(25, 90, 100, 0, 200) == 10 and vm.credit_amount(25, 0, 100, 200, 200) == 0)
    check('hunt_next_tier mirror', vm.hunt_next_tier(399, 1, 100, 4) == 4 and vm.hunt_next_tier(30000, 1, 100, 4) == 5)


capped = fixture_clean()
fixture_prior1()
fixture_capped(capped)
fixture_total_cap()
misc()
print(f'v22_model offline fixtures: {PASS} passed, {FAIL} failed')
sys.exit(1 if FAIL else 0)

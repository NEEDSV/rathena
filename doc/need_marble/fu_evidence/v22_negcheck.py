"""Negative checks of the new S1 rules against older commits + a dry run of the runner's V22 helpers on a fake DB."""
import re
import subprocess
import sys
import types

WT = r'E:\tools\Need\need-marble-phase-bc-v1'
show = lambda rev, p: subprocess.run(['git', '-C', WT, 'show', f'{rev}:{p}'], capture_output=True).stdout.decode('utf-8', 'replace')

old = show('HEAD', 'tests/need_marble/g2/bc_runner.py')
v22 = old[old.index('def cmd_v22('):]
v22 = v22[:v22.index('\ndef ', 1)]
print('old runner absolute expectation detected:', bool(re.search(r'==\s*400\b|\bb1\s*==\s*20\b|== \[\[\'0\', \'100\'\]', v22)))

mob = show('e6f808d3d', 'src/map/mob.cpp').splitlines()
dead_at = next(i for i, l in enumerate(mob) if l.startswith('int32 mob_dead('))
dead_end = next(i for i in range(dead_at + 1, len(mob)) if mob[i] == '}')
first = lambda pat: next((i for i in range(dead_at, dead_end) if pat in mob[i]), None)
order = [first(p) for p in ('need_marble_hunt_on_kill(', 'if(!(type&2) &&', 'pc_gainexp(', 'rebirth =', 'pc_need_macro_hunt_on_mob_kill(',
                            'need_summer_hunt_on_kill(', 'need_chuseok_hunt_on_kill(')]
print('pre-F1 mob.cpp order', order, 'sorted:', order == sorted(order))

# dry run: fake bcclient / bcdb, canned rows
sys.path.insert(0, WT + r'\tests\need_marble\g2')
sys.modules['bcclient'] = types.ModuleType('bcclient')
fake = types.ModuleType('bcdb')
ROWS = {
    'need_marble_policy': [['22', '4']],
    'need_marble_hunt_progress': [['20261021', '401', '5']],
    'need_marble_hunt_batch': [['1', '1', '20261021', '1']] + [['2', str(i), '20261021', '20'] for i in range(1, 21)],
    'need_marble_activity': [['20261021', str(t), '25', '25', '1'] for t in range(1, 5)],
    'kind=1': [['20261021', str(t), '25'] for t in range(1, 5)],
    'COUNT(*)': [['4']],
    'need_marble_period': [['20261021', '0', '100'], ['20261021', '1', '100']],
    'need_marble_wallet ': [['100', '0', '0']],
}


def query(out, q, who='rt'):
    for k, v in ROWS.items():
        if k in q:
            return v
    raise AssertionError(q)


fake.query = query
sys.modules['bcdb'] = fake
import bc_runner as r  # noqa: E402
import v22_model as vm  # noqa: E402
print('logical_day now', r.logical_day(), 'secs_to_reset', r.secs_to_reset())
print('logical_day 2026-10-21 18:59:59 UTC (03:59:59 KST)', r.logical_day(1792609199), '-> 19:00:00', r.logical_day(1792609200))
pol = r.v22_policy(20261021)
s = r.v22_snapshot(2000908, 20261021)
print('policy', pol)
print('snapshot progress', s['progress'], 'batches', len(s['batches']), 'tiers', len(s['milestones']), 'period', s['period'], 'score', s['score'])
print('fixture_state', vm.fixture_state(s, pol))
before = dict(s, progress={20261021: (1, 1)}, batches=s['batches'][:1], milestones=[], earn=[], tx_count=0, period={}, score=0)
m = vm.Login(before, pol)
m.feed(30000)
res = vm.compare(before, s, vm.expected(before, m), pol, 'dry')
print('dry compare on the observed 401/21 rows:', all(x[1] for x in res), [x[0] for x in res if not x[1]])

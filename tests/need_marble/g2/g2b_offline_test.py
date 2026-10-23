"""G2-B offline checks (no DB, no server): the runner's clock arithmetic, the V24 expected table, the V21 day list
and the harness reply parsers.

usage: python -I g2b_offline_test.py
"""
import calendar
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bc_runner as r  # noqa: E402

PASS = FAIL = 0


def check(name, cond, detail=''):
    global PASS, FAIL
    if cond:
        PASS += 1
    else:
        FAIL += 1
        print(f'FAIL {name} {detail}')


def kst_epoch(kst):
    """Same arithmetic as bc_runner.clock_to (KST wall clock -> unix seconds)."""
    return calendar.timegm(time.strptime(kst, '%Y-%m-%d %H:%M:%S')) - 9 * 3600


# V24 expected table = the runner's logical_day (same formula as need_marble::logical_day) at those instants
for kst, want in r.V24_TABLE:
    check(f'V24 table {kst}', r.logical_day(kst_epoch(kst)) == want, r.logical_day(kst_epoch(kst)))
# the same instants as the C++ unit test (tests/need_marble/logic_test.cpp test_v24_boundaries)
for kst, unix in (('2026-10-31 03:59:59', 1793386799), ('2026-11-01 04:00:00', 1793473200), ('2027-01-01 04:00:00', 1798743600),
                  ('2028-03-01 03:59:59', 1835463599)):
    check(f'unix {kst}', kst_epoch(kst) == unix, kst_epoch(kst))
# sub-second instants: time() is whole seconds, so .999 belongs to the second before 04:00
check('03:59:59.999 floors to the previous day', r.logical_day(int(kst_epoch('2026-11-01 03:59:59') + 0.999)) == 20261031)
check('04:00:00.001 is the new day', r.logical_day(int(kst_epoch('2026-11-01 04:00:00') + 0.001)) == 20261101)
# weekday / month / year labels used in the preflight document
check('2026-11-01 is Sunday', time.strftime('%a', time.gmtime(kst_epoch('2026-11-01 12:00:00') + 9 * 3600)) == 'Sun')
check('2026-11-02 is Monday', time.strftime('%a', time.gmtime(kst_epoch('2026-11-02 12:00:00') + 9 * 3600)) == 'Mon')
# V21 days: consecutive, noon KST maps to the same logical day, and never the V24 dates
days = r.v21_days()
check('V21 four consecutive days', [r.logical_day(kst_epoch(r.kst_noon(d))) for d in days] == days, days)
check('V21 days apart from V24 dates', not set(days) & {w for _, w in r.V24_TABLE})
# harness reply parsing
check('feed reply', r.v22_model.parse_feed('[MBQA] feed n=20 counted=20') == (20, 20))
print(f'g2b offline checks: {PASS} passed, {FAIL} failed')
sys.exit(1 if FAIL else 0)

# -*- coding: utf-8 -*-
"""Self test of the NEED enchant tooling (parity extractor, numbers, registry, generator gates).

    python tools/need_enchant/selftest.py

Needs the official client data.grf (see generate.py --client-grf) and lupa (lua51).
"""
import io
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import generate   # noqa: E402
import parity     # noqa: E402

REPO = generate.REPO
results = []


def check(name, ok, detail=''):
    results.append((name, bool(ok)))
    print('%-70s %s %s' % (name, 'PASS' if ok else 'FAIL', detail))


def counts(path, line, lo, hi):
    d = parity.table(os.path.join(REPO, path), line, lo, hi)
    total = sum(d.values())
    special = {0, 9, None}
    return total, sum(n for v, n in d.items() if v not in special), d.get(0, 0), d.get(9, 0)


def main():
    # 1. parity extractor against the Phase 0 hand analysis
    check('parity: Charleston type1 slot3 rand(101,300) = 180/15/5 of 200',
          counts('npc/re/merchants/enchan_verus.txt', 214, 101, 300) == (200, 180, 15, 5))
    check('parity: Charleston type3 slot3 rand(101,310) = 180/25/5 of 210',
          counts('npc/re/merchants/enchan_verus.txt', 326, 101, 310) == (210, 180, 25, 5))
    check('parity: Mora Attack slot2 rand(451,750) = 119 valid / 100 fail / 81 destroy',
          counts('npc/re/merchants/enchan_mora.txt', 906, 451, 750) == (300, 119, 100, 81))
    d = parity.table(os.path.join(REPO, 'npc/re/merchants/enchan_upg.txt'), 137, 1, 1300)
    check('parity: Upg physical rand(1,1300) fail 59, Expert_Archer4 200 (two ranges merged)',
          d.get(0) == 59 and d.get(4835) == 200 and d.get(4836) == 50)

    sarah = parity.read_lines(os.path.join(REPO, 'npc/NEED/instances/SarahAndFenrir.txt'))
    left = parity.loop_table(sarah, 537, 538, 457, 420, 421)
    right = parity.loop_table(sarah, 537, 538, 457, 426, 427)
    check('parity: Sarah left loop rand(100) = 21/20/20/20 + overflow 19',
          list(left.values()) == [21, 20, 20, 20, 19] and left.get('OVERFLOW') == 19)
    check('parity: Sarah right loop = 11 + 10x5 + overflow 39',
          list(right.values()) == [11, 10, 10, 10, 10, 10, 39] and right.get('OVERFLOW') == 39)
    mora = parity.read_lines(os.path.join(REPO, 'npc/re/merchants/enchan_mora.txt'))
    check('parity: Mora rand window lines 898/900 = (1,525)/(451,750)',
          parity.rand_window(mora, 898) == (1, 525) and parity.rand_window(mora, 900) == (451, 750))

    # 2. numbers
    for weights in ([1, 1, 1], [10001] * 6 + [2000] * 6 + [400] * 6, [180, 25, 5], [1, 99999], [3, 1]):
        n = generate.normalize(weights)
        check('normalize %s... sums to 100000, positives >= 1' % weights[:3], sum(n) == 100000 and all(x >= 1 for x in n))
    check('pct 180/210 = 85.7143%', generate.pct(180, 210) == '85.7143%')

    # 3. generator end to end on the fixture, registry stability
    tmp = tempfile.mkdtemp(prefix='need_enchant_selftest_')
    try:
        reg = os.path.join(tmp, 'registry.yml')
        master = os.path.join(HERE, 'fixtures', 'master_fixture.yml')
        rc = generate.main(['--master', master, '--registry', reg, '--out', os.path.join(tmp, 'out1'), '--commit-registry'])
        check('generate fixture: all gates PASS', rc == 0)
        ids1 = io.open(reg, encoding='utf-8').read()
        check('registry: first ids start at 10001', 'FIXTURE_CHARLESTON_PLATE_ACCEL: 10001' in ids1)
        rc = generate.main(['--master', master, '--registry', reg, '--out', os.path.join(tmp, 'out2'), '--commit-registry'])
        check('registry: second run keeps every id', rc == 0 and io.open(reg, encoding='utf-8').read() == ids1)
        a = io.open(os.path.join(tmp, 'out1', 'server', 'item_enchant.yml'), 'rb').read()
        b = io.open(os.path.join(tmp, 'out2', 'server', 'item_enchant.yml'), 'rb').read()
        check('generate: output is deterministic', a == b)
        rules = io.open(os.path.join(tmp, 'out1', 'server', 'enchant_rules.yml'), encoding='utf-8').read()
        check('generate: Charleston 3rd slot failures 25/5 in rules', 'Weight: 25' in rules and 'Weight: 5' in rules)
        # a variant removed from the master while its id is still active must be refused
        trimmed = os.path.join(tmp, 'trimmed.yml')
        text = io.open(master, encoding='utf-8').read()
        cut = text.index('  - Family: FIXTURE_COVERAGE')
        io.open(trimmed, 'w', encoding='utf-8').write(text[:cut])
        rc = generate.main(['--master', trimmed, '--registry', reg, '--out', os.path.join(tmp, 'out3')])
        check('registry: removing a registered variant without Retired is refused', rc == 1)
        # a broken master must stop before writing anything
        broken = os.path.join(tmp, 'broken.yml')
        io.open(broken, 'w', encoding='utf-8').write(text.replace('Weight: 25', 'Weight: 0', 1))
        rc = generate.main(['--master', broken, '--registry', os.path.join(tmp, 'r2.yml'), '--out', os.path.join(tmp, 'out4')])
        check('master: weight 0 refused', rc == 1 and not os.path.exists(os.path.join(tmp, 'out4')))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    failed = [n for n, ok in results if not ok]
    print('\nRESULT: %s (%d checks, %d failed)' % ('PASS' if not failed else 'FAIL', len(results), len(failed)))
    return 0 if not failed else 1


if __name__ == '__main__':
    sys.exit(main())

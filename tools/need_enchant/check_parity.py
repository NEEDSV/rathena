# -*- coding: utf-8 -*-
"""Re-derive every `Parity` block of the master from the LIVE scripts and compare exactly.

    python tools/need_enchant/check_parity.py [--master db/need/enchant_master.yml]

Slot:  Parity: { Chain, Window: [lo, hi], Map: { value: RESULT | REROLL }, Empty: RESULT }
       -> option weights (enchant item -> count) and failure weights (result -> count) must equal
          the master's Options and Failures, integer for integer.
Reset: Parity: { SuccessLine, RewardChain, RewardWindow }
       -> SUCCESS : rest must equal the inline `rand(a,b) < N` split (cross-multiplied),
          the reward table must equal the getitem chain counts.
Exit 1 on any mismatch; a variant without Parity is reported as NOT CHECKED.
"""
import argparse
import os
import sys
from collections import Counter

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import generate   # noqa: E402
import parity     # noqa: E402

REPO = generate.REPO


def slot_expected(script, spec, items_by_id):
    lo, hi = spec['Window']
    dist = parity.table(os.path.join(REPO, script), int(spec['Chain']), int(lo), int(hi))
    vmap = {int(k): v for k, v in (spec.get('Map') or {}).items()}
    opts, fails = Counter(), Counter()
    for value, n in dist.items():
        if value is None:
            if not spec.get('Empty'):
                raise ValueError('unassigned values in window and no Empty result')
            fails[spec['Empty']] += n
        elif value in vmap:
            if vmap[value] != 'REROLL':
                fails[vmap[value]] += n
        else:
            opts[items_by_id[value].aegis] += n
    return opts, fails


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--master', default=os.path.join(REPO, 'db', 'need', 'enchant_master.yml'))
    args = ap.parse_args(argv)
    _items, items_by_id = generate.load_items(REPO)
    doc = yaml.safe_load(open(args.master, encoding='utf-8'))
    checked, failed, unchecked = 0, 0, 0
    for fam in doc.get('Body') or []:
        for v in fam.get('Variants') or []:
            script = (v.get('Source') or {}).get('Script')
            for s in v.get('Slots') or []:
                spec = s.get('Parity')
                where = '%s slot %s' % (v['Key'], s['Slot'])
                if spec is None or script is None:
                    unchecked += 1
                    print('%-48s NOT CHECKED' % where)
                    continue
                opts, fails = slot_expected(script, spec, items_by_id)
                m_opts = Counter()
                for o in s.get('Options') or []:
                    m_opts[o['Enchant']] += int(o['Weight'])
                m_fails = Counter()
                for f in s.get('Failures') or []:
                    m_fails[f['Result']] += int(f['Weight'])
                ok = opts == m_opts and fails == m_fails
                checked += 1
                failed += not ok
                total = sum(opts.values()) + sum(fails.values())
                print('%-48s %s  script %s:%s window %s  options %d/%d  failures %s' % (
                    where, 'PASS' if ok else 'FAIL', script, spec['Chain'], spec['Window'],
                    sum(opts.values()), total, dict(fails) or '-'))
                if not ok:
                    print('    script options-master: %s' % dict(opts - m_opts))
                    print('    master options-script: %s' % dict(m_opts - opts))
                    print('    failures script %s / master %s' % (dict(fails), dict(m_fails)))
            reset = v.get('Reset') or {}
            spec = reset.get('Parity')
            if spec and script:
                lines = parity.read_lines(os.path.join(REPO, script))
                hits, total = parity.inline_rand_lt(lines, int(spec['SuccessLine']))
                outcomes = reset.get('Outcomes') or []
                ok_w = sum(int(o['Weight']) for o in outcomes if o['Result'] == 'SUCCESS')
                all_w = sum(int(o['Weight']) for o in outcomes)
                split_ok = ok_w * total == hits * all_w
                lo, hi = spec['RewardWindow']
                chain = parity.table(os.path.join(REPO, script), int(spec['RewardChain']), int(lo), int(hi))
                expected = Counter({(items_by_id[int(k.split(':')[0])].aegis, int(k.split(':')[1])): n for k, n in chain.items()})
                got = Counter()
                for o in outcomes:
                    for r in o.get('Rewards') or []:
                        got[(r['Item'], int(r.get('Amount', 1)))] += int(r.get('Weight', 1))
                ok = split_ok and expected == got
                checked += 1
                failed += not ok
                print('%-48s %s  success %d/%d (master %d/%d), rewards %s' % (
                    v['Key'] + ' reset', 'PASS' if ok else 'FAIL', hits, total, ok_w, all_w,
                    'match' if expected == got else 'MISMATCH %s vs %s' % (dict(expected), dict(got))))
    print('\nparity: %d checked, %d failed, %d not checked -> %s' % (checked, failed, unchecked, 'PASS' if failed == 0 else 'FAIL'))
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())

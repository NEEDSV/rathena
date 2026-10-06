# -*- coding: utf-8 -*-
"""Re-derive every `Parity` block of the master from the LIVE scripts and compare exactly.

    python tools/need_enchant/check_parity.py [--master db/need/enchant_master.yml]

Slot:  Parity: { Chain, Window: [lo, hi], Map: { value: RESULT | REROLL }, Empty: RESULT }
       -> option weights (enchant item -> count) and failure weights (result -> count) must equal
          the master's Options and Failures, integer for integer.
       Parity: { WindowLine, ... } additionally requires `rand(lo, hi)` on that line to equal Window.
Slot:  Parity: { Rand, Loop, Array, Size, Chance, Overflow: RESULT }   (Sarah-style counted pick loop)
       -> array value counts are option weights, the loop running past Size is the Overflow failure.
Slot:  Parity: { Strings, Index, Field, Pick }   (uniform pick from an explode()d string setarray)
Reset: Parity: { SuccessLine, Vars: { .@var: line }, RewardChain, RewardWindow }  (Vars / rewards optional;
       the inline comparison may be < <= > >= against a literal or an .@var)
       -> SUCCESS : rest must equal the inline `rand(a,b) < N` split (cross-multiplied),
          the reward table must equal the getitem chain counts.
Variant Source: { Callsub: [lines], Type, Bonus } (Mora): every listed `callsub L_Socket,type,bonus,allowed`
       line must name a target with this type (or bonus type), the Order must follow `allowed`, and
       no callsub line of the script with this type/allowed may be missing from the variant.
Exit 1 on any mismatch; a variant without Parity is reported as NOT CHECKED.
"""
import argparse
import os
import re
import sys
from collections import Counter

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import generate   # noqa: E402
import parity     # noqa: E402

REPO = generate.REPO


def slot_expected(script, spec, items_by_id):
    if 'Loop' in spec:
        lines = parity.read_lines(os.path.join(REPO, script))
        dist = parity.loop_table(lines, int(spec['Rand']), int(spec['Loop']), int(spec['Array']),
                                 int(spec['Size']), int(spec['Chance']))
        opts, fails = Counter(), Counter()
        for value, n in dist.items():
            if value == 'OVERFLOW':
                fails[spec['Overflow']] += n
            else:
                opts[items_by_id[value].aegis] += n
        return opts, fails
    if 'Array' in spec or 'FRand' in spec:
        lines = parity.read_lines(os.path.join(REPO, script))
        dist = (parity.array_pick(lines, int(spec['Array']), int(spec['Pick'])) if 'Array' in spec
                else parity.frand(lines, int(spec['FRand'])))
        opts, fails = Counter(), Counter()
        if 'Chances' in spec:
            hits, total = chance_fail(lines, spec)
            for v, n in dist.items():
                opts[items_by_id[v].aegis] += n * (total - hits)
            fails['DESTROY'] += hits * sum(dist.values())
        elif 'DestroyLine' in spec:
            # each pick survives an inline `rand(n) < k` destroy check: options x survivors, destroy x picks
            hits, total = parity.inline_rand_cmp(lines, int(spec['DestroyLine']))
            for v, n in dist.items():
                opts[items_by_id[v].aegis] += n * (total - hits)
            fails['DESTROY'] += hits * sum(dist.values())
        else:
            for v, n in dist.items():
                opts[items_by_id[v].aegis] += n
        return opts, fails
    if 'Eq' in spec:
        lines = parity.read_lines(os.path.join(REPO, script))
        lo, hi = parity.rand_window(lines, int(spec['WindowLine']))
        dist = parity.eq_table(lines, int(spec['Eq']), lo, hi)
        # Replace: { script id: master id } = an operator decision recorded in the master, applied before comparing
        repl = {int(k): v for k, v in (spec.get('Replace') or {}).items()}
        opts = Counter()
        for v, n in dist.items():
            opts[repl[v] if v in repl else items_by_id[v].aegis] += n
        return opts, Counter()
    if 'Strings' in spec:
        lines = parity.read_lines(os.path.join(REPO, script))
        dist = parity.explode_pool(lines, int(spec['Strings']), int(spec['Index']), int(spec['Field']), int(spec['Pick']))
        return Counter({items_by_id[v].aegis: n for v, n in dist.items()}), Counter()
    lo, hi = spec['Window']
    if 'WindowLine' in spec:
        got = parity.rand_window(parity.read_lines(os.path.join(REPO, script)), int(spec['WindowLine']))
        if list(got) != [int(lo), int(hi)]:
            raise ValueError('WindowLine %s has rand%s, master Window %s' % (spec['WindowLine'], got, spec['Window']))
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


def callsub_check(v, items_by_id):
    """Mora targets: -> (ok, detail)."""
    import import_legacy
    src = v['Source']
    lines = parity.read_lines(os.path.join(REPO, src['Script']))
    subs = {ln: (iid, t, b, a) for ln, iid, t, b, a in import_legacy.mora_callsubs(lines)}
    etype, bonus = int(src['Type']), bool(src['Bonus'])
    listed = [int(x) for x in src['Callsub']]
    problems = []
    allowed = set()
    for ln in listed:
        if ln not in subs:
            problems.append('line %d is not a callsub' % ln)
            continue
        iid, t, b, a = subs[ln]
        if (b if bonus else t) != etype:
            problems.append('line %d has type %d/%d' % (ln, t, b))
        allowed.add(a)
    if len(allowed) == 1:
        a = allowed.pop()
        if [int(x) for x in v['Order']] != import_legacy.mora_order(a):
            problems.append('Order %s does not follow allowed %d' % (v['Order'], a))
        missing = [ln for ln, (iid, t, b, aa) in subs.items() if (b if bonus else t) == etype and aa == a and ln not in listed]
        if missing:
            problems.append('callsub lines missing from the variant: %s' % missing)
    else:
        problems.append('mixed allowed slots %s' % sorted(allowed))
    want = sorted(items_by_id[subs[ln][0]].aegis for ln in listed if ln in subs)
    if sorted(v['TargetItems']) != want:
        problems.append('TargetItems %s != script %s' % (sorted(v['TargetItems']), want))
    if int(v.get('MinimumRefine', 0)) != (9 if bonus else 0):
        problems.append('MinimumRefine %s (script: %d)' % (v.get('MinimumRefine', 0), 9 if bonus else 0))
    return not problems, '; '.join(problems) or '%d targets, order %s' % (len(listed), v['Order'])


def chance_fail(lines, spec):
    """`if (.@chances[.@num] < rand(a,b))` fails: -> (fail count, total) for chances[ChanceIndex]."""
    ch = parity.setarray_values(lines, int(spec['Chances']))[1][int(spec['ChanceIndex'])]
    m = re.search(r'\.@\w+\[\.@\w+\]\s*<\s*rand\s*\(\s*(\d+)\s*,\s*(\d+)\s*\)', parity._strip(lines[int(spec['ChanceLine']) - 1]))
    if not m:
        raise ValueError('ChanceLine %s is not `.@chances[.@n] < rand(a,b)`' % spec['ChanceLine'])
    lo, hi = int(m.group(1)), int(m.group(2))
    return sum(1 for r in range(lo, hi + 1) if ch < r), hi - lo + 1


def perfect_check(script, s, spec, items_by_id):
    """Perfect / Upgrades / MaxSame of a slot against the script. -> (ok, detail)."""
    lines = parity.read_lines(os.path.join(REPO, script))
    aeg = lambda i: items_by_id[i].aegis  # noqa: E731
    got_perfect = {p['Enchant']: p.get('Cost') or {} for p in s.get('Perfect') or []}
    problems = []
    if 'Blueprints' in spec:
        _v, vals = parity.setarray_values(lines, int(spec['Blueprints']))
        rows = [vals[i:i + 5] for i in range(0, len(vals), 5)]
        col, first = int(spec['Column']), bool(spec['FirstSlot'])
        want = {aeg(r[1]): {'Materials': [{'Item': aeg(r[0]), 'Amount': 1}]} for r in rows if r[col] > 0 and (first or r[4] == 0)}
        cap = {aeg(r[1]): r[col] for r in rows if r[col] > 0}
        got_cap = {c['Enchant']: int(c['Max']) for c in s.get('MaxSame') or []}
        if got_perfect != want:
            problems.append('Perfect differs')
        if got_cap != cap:
            problems.append('MaxSame differs')
        return not problems, '; '.join(problems) or '%d perfect, %d caps' % (len(want), len(cap))
    ladder = [parity.setarray_values(lines, int(x))[1] for x in spec['Ladder']]
    costs = [parity.setarray_values(lines, int(x))[1] for x in spec['Costs']]
    mats = [aeg(6608), aeg(6755)][:len(costs)]

    def cost_of(n):
        c = {'Materials': [{'Item': m, 'Amount': costs[i][n]} for i, m in enumerate(mats)]}
        if spec.get('Zeny'):
            c = dict(c, Zeny=int(spec['Zeny']))
        return c
    want = {aeg(e): cost_of(0) for e in ladder[0]}
    if got_perfect != want:
        problems.append('Perfect differs')
    chances = parity.setarray_values(lines, int(spec['Chances']))[1] if spec.get('Chances') else None
    want_up = {}
    for n in (1, 2, 3):
        for t in range(len(ladder[0])):
            u = {'To': aeg(ladder[n][t]), 'Cost': cost_of(n)}
            if chances:
                fail, total = chance_fail(lines, dict(spec, ChanceIndex=n))
                u['SuccessWeight'] = total - fail
                u['Failures'] = [{'Result': 'DESTROY', 'Weight': fail}]
            want_up[aeg(ladder[n - 1][t])] = u
    got_up = {}
    for u in s.get('Upgrades') or []:
        g = {'To': u['To'], 'Cost': u.get('Cost') or {}}
        if 'SuccessWeight' in u:
            g['SuccessWeight'] = int(u['SuccessWeight'])
            g['Failures'] = [{'Result': f['Result'], 'Weight': int(f['Weight'])} for f in u.get('Failures') or []]
        got_up[u['Enchant']] = g
    if got_up != want_up:
        problems.append('Upgrades differ')
    return not problems, '; '.join(problems) or '%d perfect, %d upgrades' % (len(want), len(want_up))


def callsub_args_check(v, items_by_id):
    import import_legacy
    src = v['Source']
    lines = parity.read_lines(os.path.join(REPO, src['Script']))
    a, b = [int(x) for x in src['CallsubArgs']]
    want = sorted(items_by_id[iid].aegis for _n, iid, m, lim in import_legacy.mal_callsubs(lines) if m == a and lim == b)
    ok = sorted(v['TargetItems']) == want
    return ok, '%d callsub targets%s' % (len(want), '' if ok else ' (master %d)' % len(v['TargetItems']))


def target_lines_check(v, items_by_id):
    """Source: { TargetLines: [lines], TargetExact: bool } -> every target id must appear on those script lines
    (comments stripped); with TargetExact the ids on the lines must be exactly the targets."""
    src = v['Source']
    lines = parity.read_lines(os.path.join(REPO, src['Script']))
    found = set()
    if src.get('TargetRange'):
        # `if (.@id < A || .@id > B)` refusal = targets A..B inclusive
        m = re.search(r'<\s*(\d+)\s*\|\|\s*\.@\w+\s*>\s*(\d+)', parity._strip(lines[int(src['TargetRange']) - 1]))
        if not m:
            return False, 'TargetRange line %s is not `< A || > B`' % src['TargetRange']
        found |= set(range(int(m.group(1)), int(m.group(2)) + 1))
    for ln in src.get('TargetLines') or []:
        found |= set(int(x) for x in re.findall(r'(?<![\w.@])(\d{4,6})(?![\w])', parity._strip(lines[int(ln) - 1])))
    by_aegis = {it.aegis: i for i, it in items_by_id.items()}
    want = set(by_aegis[t] for t in v['TargetItems'])
    ok = want <= found and (not src.get('TargetExact') or want == found)
    return ok, '%d targets on %d line(s)%s' % (len(want), len(src.get('TargetLines') or [src.get('TargetRange')]),
                                               '' if ok else ': missing %s extra %s' % (sorted(want - found), sorted(found - want)))


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
            if (v.get('Source') or {}).get('Callsub'):
                ok, detail = callsub_check(v, items_by_id)
                checked += 1
                failed += not ok
                print('%-48s %s  %s' % (v['Key'] + ' targets', 'PASS' if ok else 'FAIL', detail))
            if (v.get('Source') or {}).get('TargetLines') or (v.get('Source') or {}).get('TargetRange'):
                ok, detail = target_lines_check(v, items_by_id)
                checked += 1
                failed += not ok
                print('%-48s %s  %s' % (v['Key'] + ' targets', 'PASS' if ok else 'FAIL', detail))
            if (v.get('Source') or {}).get('CallsubArgs'):
                ok, detail = callsub_args_check(v, items_by_id)
                checked += 1
                failed += not ok
                print('%-48s %s  %s' % (v['Key'] + ' targets', 'PASS' if ok else 'FAIL', detail))
            for s in v.get('Slots') or []:
                spec = s.get('Parity')
                if spec and ('Blueprints' in spec or 'Ladder' in spec):
                    ok, detail = perfect_check(script, s, spec, items_by_id)
                    checked += 1
                    failed += not ok
                    print('%-48s %s  %s' % ('%s slot %s' % (v['Key'], s['Slot']), 'PASS' if ok else 'FAIL', detail))
                    continue
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
                    where, 'PASS' if ok else 'FAIL', script, spec.get('Chain', spec.get('Loop', spec.get('Strings'))), spec.get('Window', 'loop' if 'Loop' in spec else 'pool'),
                    sum(opts.values()), total, dict(fails) or '-'))
                if not ok:
                    print('    script options-master: %s' % dict(opts - m_opts))
                    print('    master options-script: %s' % dict(m_opts - opts))
                    print('    failures script %s / master %s' % (dict(fails), dict(m_fails)))
            reset = v.get('Reset') or {}
            spec = reset.get('Parity')
            if spec and script:
                lines = parity.read_lines(os.path.join(REPO, script))
                hits, total = parity.inline_rand_cmp(lines, int(spec['SuccessLine']), spec.get('Vars'))
                outcomes = reset.get('Outcomes') or []
                ok_w = sum(int(o['Weight']) for o in outcomes if o['Result'] == 'SUCCESS')
                all_w = sum(int(o['Weight']) for o in outcomes)
                split_ok = ok_w * total == hits * all_w
                expected = Counter()
                if spec.get('RewardChain'):
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

# -*- coding: utf-8 -*-
"""Automated test cases of the NEED Unified Enchant V2 engine, with the master as the oracle.

Offline (no client; the harness map-server rolls the real engine functions on the installed data):
    python tools/need_enchant/tc.py offline-plan  --out <plan>
    map-server-enchanttest --map-config <conf> --need-enchant-plan <item_enchant.yml> <enchant_rules.yml> <plan> <result>
    python tools/need_enchant/tc.py offline-verify --plan <plan> --result <result>

In game (a GM 99 test character; every request goes through the real packet handler):
    python tools/need_enchant/tc.py ingame-plan --out <plan>
    @v2try <plan> <result>                       (in game; items and zeny of the character are replaced)
    python tools/need_enchant/tc.py ingame-verify --result <result>

The verifier recomputes, from the master, what every request must do in the logged state: refused (and free) or
paid exactly its cost, which results are possible, which item fields must stay unchanged, and (offline) the
probability of every result, checked with a chi-square test.
"""
import argparse
import io
import json
import math
import os
import sys
from collections import OrderedDict, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import generate   # noqa: E402

REPO = generate.REPO
CHANCE_BASE = generate.CHANCE_BASE
MSG_SUCCESS, MSG_FAILED, MSG_REJECT, MSG_BREAK = 3857, 3858, 3860, 3705
NO_POOL = 'no normal pool'   # answered with msg 829 + a plain failure (3858), nothing paid
TEST_CARD = 4001          # Poring Card, put in a real card slot to prove it is kept
TEST_OPTION = (1, 100, 0)  # random option id 1 (VAR_MAXHPAMOUNT) +100
N_NORMAL, N_UPGRADE, N_RESET = 100000, 50000, 100000
Z_LIMIT = 4.75            # one-sided p ~ 1e-6 per distribution (about a thousand distributions)


# ----------------------------------------------------------------------------- model

class Model:
    def __init__(self, master=None, registry=None):
        self.items, self.by_id = generate.load_items(REPO)
        _f, self.variants = generate.load_master(master or os.path.join(REPO, 'db', 'need', 'enchant_master.yml'), self.items)
        ids, _r = generate.load_registry(registry or os.path.join(REPO, 'db', 'need', 'enchant_clientid_registry.yml'))
        for v in self.variants:
            v['id'] = ids[v['key']]
        self.by_group = {v['id']: v for v in self.variants}

    def iid(self, aegis):
        return self.items[aegis].id

    def slots_of(self, item_id):
        return self.by_id[item_id].slots


def caps(s, m):
    return {m.iid(c['Enchant']): int(c['Max']) for c in s['max_same']}


def requirements(s, m):
    """[(slot, [enchant ids in master order])]"""
    return [(int(rq['Slot']), [m.iid(e) for e in rq['Enchants']]) for rq in s['require']]


def count_enchant(cards, ts, e):
    return sum(1 for i in range(ts, 4) if cards[i] == e)


def option_weights(v, sl, cards, ts, m):
    s = v['slots'][sl]
    cp = caps(s, m)
    out = OrderedDict()
    for a, w in s['options'].items():
        e = m.iid(a)
        out[e] = 0 if e in cp and count_enchant(cards, ts, e) >= cp[e] else w
    return out


def next_slot(v, cards):
    for sl in v['order']:
        if cards[sl] == 0:
            return sl
    return None


def candidates(v, sl, m):
    s = v['slots'][sl]
    return [m.iid(a) for a in s['options']] or [m.iid(a) for a in s['perfect']]


def fill(v, upto, m, prefer=None):
    """cards with the order slots before `upto` (or all, upto=None) filled so that every later Require holds"""
    cards = [0, 0, 0, 0]
    order = v['order']
    k = len(order) if upto is None else order.index(upto)
    for i, ps in enumerate(order[:k]):
        cand = candidates(v, ps, m)
        allowed = list(cand)
        for later in order[i + 1:]:
            for rslot, need in requirements(v['slots'][later], m):
                if rslot == ps:
                    allowed = [e for e in allowed if e in need]
        if prefer and ps in prefer and prefer[ps] in cand:
            allowed = [prefer[ps]] + allowed
        if not allowed:
            return None
        cards[ps] = allowed[0]
    # slots another group fills first (Ring of Hero chain, RWC risk slots): the first enchant the Require accepts
    for sl in order:
        for rslot, need in requirements(v['slots'][sl], m):
            if rslot not in order and cards[rslot] == 0:
                cards[rslot] = need[0]
    return cards


def outcome_key(o, m):
    k = 'F' + o['Result']
    if o['Result'] == 'DOWNGRADE':
        k += ':%d' % m.iid(o['To'])
    return k


def cost_ids(c, m):
    return c['zeny'], OrderedDict((m.iid(a), n) for a, n in c['mats'].items())


# ----------------------------------------------------------------------------- expected distributions

def dist_normal(v, sl, cards, refine, ts, m):
    """main: {key: p}, aux: {key: p} (reward picks, refine-down results)"""
    s = v['slots'][sl]
    w = option_weights(v, sl, cards, ts, m)
    main, aux = OrderedDict(), defaultdict(float)
    if s['failures']:
        total = sum(w.values()) + sum(int(f['Weight']) for f in s['failures'])
        for e, x in w.items():
            main['E%d' % e] = main.get('E%d' % e, 0) + x / total
        for f in s['failures']:
            p = int(f['Weight']) / total
            main[outcome_key(f, m)] = main.get(outcome_key(f, m), 0) + p
            aux_outcome(f, p, refine, aux, m)
    else:
        c = s['chance'] / CHANCE_BASE
        sw = sum(w.values())
        for e, x in w.items():
            main['E%d' % e] = main.get('E%d' % e, 0) + (c * x / sw if sw else 0)
        if c < 1:
            main['STOCK_FAIL'] = 1 - c
    return main, dict(aux)


def aux_outcome(o, p, refine, aux, m):
    rws = o.get('Rewards') or []
    if rws:
        tw = sum(int(r.get('Weight', 1)) for r in rws)
        for r in rws:
            aux['R%dx%d' % (m.iid(r['Item']), int(r.get('Amount', 1)))] += p * int(r.get('Weight', 1)) / tw
    if o['Result'] == 'REFINE_DOWN':
        if o.get('Random'):
            for k in range(refine + 1):
                aux['RD%d' % (refine - k)] += p / (refine + 1)
        else:
            aux['RD%d' % max(0, refine - int(o.get('Amount', 1)))] += p


def dist_upgrade(u, refine, m):
    main, aux = OrderedDict(), defaultdict(float)
    if not u['failures']:
        main['SUCCESS'] = 1.0
        return main, {}
    total = u['success'] + sum(int(f['Weight']) for f in u['failures'])
    main['SUCCESS'] = u['success'] / total
    for f in u['failures']:
        p = int(f['Weight']) / total
        main[outcome_key(f, m)] = main.get(outcome_key(f, m), 0) + p
        aux_outcome(f, p, refine, aux, m)
    return main, dict(aux)


def dist_reset(r, refine, m):
    main, aux = OrderedDict(), defaultdict(float)
    if r['outcomes']:
        total = sum(int(o['Weight']) for o in r['outcomes'])
        for o in r['outcomes']:
            p = int(o['Weight']) / total
            k = 'SUCCESS' if o['Result'] == 'SUCCESS' else outcome_key(o, m)
            main[k] = main.get(k, 0) + p
            aux_outcome(o, p, refine, aux, m)
    else:
        c = r['chance'] / CHANCE_BASE
        main['SUCCESS'] = c
        if c < 1:
            main['FAIL'] = 1 - c
    return main, dict(aux)


def slot_refine(v, sl):
    r = max(v['min_refine'], v['slots'][sl]['min_refine'] if sl is not None else 0)
    return r


def all_refine(v):
    return max([v['min_refine']] + [s['min_refine'] for s in v['slots'].values()])


def test_refine(v, minimum):
    """a refine above 0 where allowed, so refine-down, refine reset and refine preservation are visible:
    the group maximum if it has one, else +7 (never below the minimum)"""
    if v['max_refine']:
        return max(minimum, v['max_refine']) if minimum <= v['max_refine'] else minimum
    return max(minimum, 7)


# ----------------------------------------------------------------------------- offline plan / verify

def offline_states(m):
    """(tag, item line, roll line, expected) for every distribution to sample"""
    out = []
    for v in m.variants:
        g = v['id']
        tid = m.iid(v['targets'][0])
        ts = m.slots_of(tid)

        def add(tag, cards, refine, op, arg, n, exp, slot):
            out.append((tag, 'item %d %d 0 %s' % (tid, refine, ' '.join(map(str, cards))),
                        'roll %s %s %d %d %d' % (tag, op, g, arg, n), exp, slot))

        for sl in v['order']:
            cards = fill(v, sl, m)
            if cards is None:
                continue
            r = test_refine(v, slot_refine(v, sl))
            if v['slots'][sl]['options']:
                add('%d:n:%d' % (g, sl), cards, r, 'n', 0, N_NORMAL, dist_normal(v, sl, cards, r, ts, m), sl)
                # a state where a MaxSame cap is reached on the earlier slots
                for e, mx in caps(v['slots'][sl], m).items():
                    prior = [ps for ps in v['order'][:v['order'].index(sl)] if e in candidates(v, ps, m)]
                    if len(prior) >= mx:
                        pref = {ps: e for ps in prior[:mx]}
                        cc = fill(v, sl, m, pref)
                        if cc and count_enchant(cc, ts, e) >= mx and any(x > 0 for x in option_weights(v, sl, cc, ts, m).values()):
                            add('%d:ncap:%d:%d' % (g, sl, e), cc, r, 'n', 0, N_NORMAL, dist_normal(v, sl, cc, r, ts, m), sl)
                        break
        for sl in v['order']:
            for a, u in v['slots'][sl]['upgrades'].items():
                cards = fill(v, None, m)
                if cards is None:
                    continue
                cards[sl] = m.iid(a)
                r = test_refine(v, all_refine(v))
                add('%d:u:%d:%d' % (g, sl, m.iid(a)), cards, r, 'u', sl, N_UPGRADE, dist_upgrade(u, r, m), sl)
        if v['reset']:
            cards = fill(v, None, m)
            if cards is not None:
                r = test_refine(v, all_refine(v))
                add('%d:r' % g, cards, r, 'r', 0, N_RESET, dist_reset(v['reset'], r, m), 0)
    return out


def chisq_z(observed, expected_p, n):
    """Wilson-Hilferty z of a chi-square over the bins with an expected count >= 5 (rest pooled)"""
    chisq, bins, pool_o, pool_e = 0.0, 0, 0.0, 0.0
    for k, p in expected_p.items():
        e = n * p
        o = observed.get(k, 0)
        if e >= 5:
            chisq += (o - e) ** 2 / e
            bins += 1
        else:
            pool_o += o
            pool_e += e
    if pool_e >= 5:
        chisq += (pool_o - pool_e) ** 2 / pool_e
        bins += 1
    if bins < 2:
        return 0.0
    df = bins - 1
    h = 2.0 / (9.0 * df)
    return ((chisq / df) ** (1.0 / 3) - (1 - h)) / math.sqrt(h)


def offline_plan(args):
    m = Model()
    states = offline_states(m)
    with io.open(args.out, 'w', encoding='ascii', newline='\n') as f:
        f.write('# NEED Enchant V2 offline distribution plan, generated by tools/need_enchant/tc.py\n')
        for tag, item, roll, _e, _s in states:
            f.write(item + '\n' + roll + '\n')
    print('offline plan: %d distributions -> %s' % (len(states), args.out))


def offline_verify(args):
    m = Model()
    states = {s[0]: s for s in offline_states(m)}
    seen, fails, worst = set(), [], (0, None)
    for line in io.open(args.result, encoding='utf-8'):
        r = json.loads(line)
        if 'error' in r:
            fails.append('runner error: %s' % r['error'])
            continue
        tag = r['tag']
        seen.add(tag)
        st = states.get(tag)
        if st is None:
            fails.append('%s: not in the plan' % tag)
            continue
        (main, aux), slot = st[3], st[4]
        if r['check'] != 1:
            fails.append('%s: the engine refused a request the master allows' % tag)
            continue
        if r['slot'] != slot:
            fails.append('%s: engine slot %d, master slot %d' % (tag, r['slot'], slot))
        n, c = r['n'], r['counts']
        mainobs = {k: x for k, x in c.items() if not k.startswith('R')}   # R<item>x<n> rewards, RD<refine> refine down
        auxobs = {k: x for k, x in c.items() if k.startswith('R')}
        for k, x in mainobs.items():
            if main.get(k, 0) == 0:
                fails.append('%s: result %s observed %d times, impossible by the master' % (tag, k, x))
        for k, x in auxobs.items():
            if aux.get(k, 0) == 0:
                fails.append('%s: %s observed %d times, impossible by the master' % (tag, k, x))
        z = chisq_z(mainobs, main, n)
        if aux:
            ap = dict(aux)
            ap['_none'] = max(0.0, 1 - sum(aux.values()))
            ao = dict(auxobs)
            ao['_none'] = n - sum(auxobs.values())
            z = max(z, chisq_z(ao, ap, n))
        if z > worst[0]:
            worst = (z, tag)
        if z > Z_LIMIT:
            detail = ', '.join('%s %.4f%%/%.4f%%' % (k, 100.0 * mainobs.get(k, 0) / n, 100 * p) for k, p in main.items())
            fails.append('%s: distribution off (z=%.2f): %s' % (tag, z, detail))
    missing = [t for t in states if t not in seen]
    for t in missing:
        fails.append('%s: no result' % t)
    print('offline verify: %d distributions, %d results, worst z=%.2f (%s), %d failures'
          % (len(states), len(seen), worst[0], worst[1], len(fails)))
    for f_ in fails[:60]:
        print('  FAIL ' + f_)
    return 1 if fails else 0


# ----------------------------------------------------------------------------- in-game plan

class Plan:
    def __init__(self):
        self.lines = []

    def add(self, s):
        self.lines.append(s)

    def item(self, cmd, tid, refine, bound, cards, opt=(0, 0, 0)):
        self.add('%s %d %d %d 0 %s %d %d %d' % ((cmd, tid, refine, bound, ' '.join(map(str, cards))) + tuple(opt)))

    def pay(self, c, m, short=None):
        """set zeny / materials to exactly the cost (short='zeny' or a material id: one less)"""
        z, mats = cost_ids(c, m)
        self.add('zeny %d' % (z - 1 if short == 'zeny' else z))
        for i, n in mats.items():
            self.add('have %d %d' % (i, n - 1 if short == i else n))

    def tryop(self, tag, op, g, tid, arg=0):
        self.add('try %s %s %d %d %d' % (tag, op, g, tid, arg))


def ingame_plan(args):
    m = Model()
    p = Plan()
    p.add('# NEED Enchant V2 in-game plan, generated by tools/need_enchant/tc.py - test character only')
    p.add('note start')
    for v in m.variants:
        g = v['id']
        tid = m.iid(v['targets'][0])
        ts = m.slots_of(tid)
        base = fill(v, v['order'][0], m) or [0, 0, 0, 0]
        for i in range(ts):
            base[i] = TEST_CARD
        opt = TEST_OPTION if v['allow_random'] else (0, 0, 0)
        r_all = test_refine(v, all_refine(v))
        mats_used = set()
        for c in [s['cost'] for s in v['slots'].values()] + [x for s in v['slots'].values() for x in s['perfect'].values()] + \
                 [u['cost'] for s in v['slots'].values() for u in s['upgrades'].values()] + ([v['reset']['cost']] if v['reset'] else []):
            mats_used.update(cost_ids(c, m)[1])
        p.add('note group %d %s' % (g, v['key']))
        p.add('clear %d' % tid)

        # walk: every slot in order, bound item with a real card and a random option (all must survive)
        first = v['order'][0]
        walk_cost = {'zeny': max(s['cost']['zeny'] for s in v['slots'].values()), 'mats': OrderedDict()}
        for s in v['slots'].values():
            for a, n in s['cost']['mats'].items():
                walk_cost['mats'][a] = max(n, walk_cost['mats'].get(a, 0))
        rounds = 3 if v['slots'][first]['options'] else 1
        for rnd in range(rounds):
            p.add('clear %d' % tid)
            p.item('give', tid, r_all, 1, base, opt)
            for i in range(len(v['order']) if rounds > 1 else 1):
                p.item('ensure', tid, r_all, 1, base, opt)
                p.pay(walk_cost, m)
                p.tryop('%d:walk:%d:%d' % (g, rnd, i), 'n', g, tid)
        p.add('clear %d' % tid)

        # slots that can lower the refine: 60 more requests on a fresh +r item each, so the rare failure shows up
        for sl in v['order']:
            s = v['slots'][sl]
            if any(f['Result'] == 'REFINE_DOWN' for f in s['failures']) and s['options']:
                cards = fill(v, sl, m)
                if cards is None:
                    continue
                for i in range(ts):
                    cards[i] = TEST_CARD
                for k in range(60):
                    p.add('clear %d' % tid)
                    p.item('give', tid, r_all, 1, cards, opt)
                    p.pay(s['cost'], m)
                    p.tryop('%d:refdown:%d:%d' % (g, sl, k), 'n', g, tid)
                p.add('clear %d' % tid)

        # refusals of the first slot (free): short zeny, short material, refine out of range, option not allowed
        c0 = v['slots'][first]['cost']
        z0, m0 = cost_ids(c0, m)
        r0 = slot_refine(v, first)
        shorts = (['zeny'] if z0 > 0 else []) + list(m0)[:1]
        for sh in shorts:
            p.item('give', tid, r0, 0, base)
            p.pay(c0, m, short=sh)
            p.tryop('%d:short:%s' % (g, sh), 'n', g, tid)
            p.add('clear %d' % tid)
        if r0 > 0:
            p.item('give', tid, r0 - 1, 0, base)
            p.pay(c0, m)
            p.tryop('%d:lowrefine' % g, 'n', g, tid)
            p.add('clear %d' % tid)
        if v['max_refine'] and v['max_refine'] < 20:
            p.item('give', tid, v['max_refine'] + 1, 0, base)
            p.pay(c0, m)
            p.tryop('%d:highrefine' % g, 'n', g, tid)
            p.add('clear %d' % tid)
        if not v['allow_random']:
            p.item('give', tid, r0, 0, base, TEST_OPTION)
            p.pay(c0, m)
            p.tryop('%d:option' % g, 'n', g, tid)
            p.add('clear %d' % tid)

        # Require broken: an earlier slot holds an enchant the later slot does not accept
        for sl in v['order']:
            for rslot, need in requirements(v['slots'][sl], m):
                pool = candidates(v, rslot, m) if rslot in v['slots'] else [e for x in v['order'] for e in candidates(v, x, m)]
                bad = [e for e in pool if e not in need]
                cards = fill(v, sl, m)
                if bad and cards is not None:
                    cards[rslot] = bad[0]
                    for i in range(ts):
                        cards[i] = TEST_CARD
                    p.item('give', tid, slot_refine(v, sl), 0, cards)
                    p.pay(v['slots'][sl]['cost'], m)
                    p.tryop('%d:require:%d' % (g, sl), 'n', g, tid)
                    p.add('clear %d' % tid)
                break

        # every perfect enchant once
        for sl in v['order']:
            for a in v['slots'][sl]['perfect']:
                cards = fill(v, sl, m)
                if cards is None:
                    continue
                for i in range(ts):
                    cards[i] = TEST_CARD
                p.item('give', tid, test_refine(v, slot_refine(v, sl)), 1, cards, opt)
                p.pay(v['slots'][sl]['perfect'][a], m)
                p.tryop('%d:perfect:%d:%d' % (g, sl, m.iid(a)), 'p', g, tid, m.iid(a))
                p.add('clear %d' % tid)

        # every upgrade entry, two requests each (a fresh item each time)
        for sl in v['order']:
            for a, u in v['slots'][sl]['upgrades'].items():
                cards = fill(v, None, m)
                if cards is None:
                    continue
                cards[sl] = m.iid(a)
                for i in range(ts):
                    cards[i] = TEST_CARD
                for k in range(2):
                    p.add('clear %d' % tid)
                    p.item('give', tid, r_all, 1, cards, opt)
                    p.pay(u['cost'], m)
                    p.tryop('%d:upgrade:%d:%d:%d' % (g, sl, m.iid(a), k), 'u', g, tid, sl)
                p.add('clear %d' % tid)

        # reset: three requests on a full item, then the refusals (not all filled / empty scope / no reset)
        full = fill(v, None, m)
        if full is not None:
            for i in range(ts):
                full[i] = TEST_CARD
            rc = v['reset']['cost'] if v['reset'] else {'zeny': 0, 'mats': OrderedDict()}
            for k in range(3 if v['reset'] else 1):
                p.add('clear %d' % tid)
                p.item('give', tid, r_all, 1, full, opt)
                p.pay(rc, m)
                p.tryop('%d:reset:%d' % (g, k), 'r', g, tid)
            p.add('clear %d' % tid)
            if v['reset'] and len(v['order']) > 1:
                part = list(full)
                part[v['order'][-1]] = 0
                p.item('give', tid, r_all, 0, part)
                p.pay(rc, m)
                p.tryop('%d:resetpart' % g, 'r', g, tid)
                p.add('clear %d' % tid)
            if v['reset'] and v['reset']['scope']:
                part = list(full)
                for sc in v['reset']['scope']:
                    part[sc] = 0
                if any(part[i] for i in range(ts, 4)):
                    p.item('give', tid, r_all, 0, part)
                    p.pay(rc, m)
                    p.tryop('%d:resetscope' % g, 'r', g, tid)
                    p.add('clear %d' % tid)
        for i in sorted(mats_used):
            p.add('clear %d' % i)
        rewards = set()
        for s in v['slots'].values():
            for o in s['failures'] + [f for u in s['upgrades'].values() for f in u['failures']]:
                rewards.update(m.iid(r['Item']) for r in o.get('Rewards') or [])
        if v['reset']:
            for o in v['reset']['outcomes']:
                rewards.update(m.iid(r['Item']) for r in o.get('Rewards') or [])
        for i in sorted(rewards):
            p.add('clear %d' % i)
    p.add('note end')
    with io.open(args.out, 'w', encoding='ascii', newline='\n') as f:
        f.write('\n'.join(p.lines) + '\n')
    tries = sum(1 for x in p.lines if x.startswith('try '))
    print('in-game plan: %d groups, %d requests, %d lines -> %s' % (len(m.variants), tries, len(p.lines), args.out))


# ----------------------------------------------------------------------------- in-game verify

def expect(m, rec):
    """-> (refuse_reason or None, cost (zeny, {id: n}), [allowed (result, after-predicate, rewards)])"""
    v = m.by_group.get(rec['group'])
    b = rec['before']
    if v is None:
        return 'not a NEED group', None, []
    tid, cards, refine = b['id'], list(b['cards']), b['refine']
    ts = m.slots_of(tid)
    have = {int(k): x for k, x in rec['have'].items()}
    zeny = rec['zeny'][0]
    op = rec['op']
    targets = {m.iid(t) for t in v['targets']}
    opts = any(o[0] for o in b['opts'])

    def afford(c):
        z, mats = cost_ids(c, m)
        if zeny < z:
            return 'zeny short'
        for i, n in mats.items():
            if have.get(i, 0) < n:
                return 'material %d short' % i
        return None

    if b['equip'] or b['attr'] or tid not in targets:
        return 'base check', None, []
    if op in 'npu':
        if refine < v['min_refine']:
            return 'refine below the group minimum', None, []
        if opts and not v['allow_random']:
            return 'random options not allowed', None, []
    if v['max_refine'] and refine > v['max_refine']:
        return 'refine above the maximum', None, []

    if op in 'np':
        sl = next_slot(v, cards)
        if sl is None:
            return 'no empty slot', None, []
        if sl < ts:
            return 'slot is a real card slot', None, []
        s = v['slots'][sl]
        if refine < s['min_refine']:
            return 'refine below the slot minimum', None, []
        for rslot, need in requirements(s, m):
            if cards[rslot] not in need:
                return 'Require', None, []
        if op == 'p':
            perf = {m.iid(a): c for a, c in s['perfect'].items()}
            e = rec['arg']
            if e not in perf:
                return 'no such perfect enchant', None, []
            cp = caps(s, m)
            if e in cp and count_enchant(cards, ts, e) >= cp[e]:
                return 'MaxSame', None, []
            why = afford(perf[e])
            if why:
                return why, None, []
            after = list(cards)
            after[sl] = e
            return None, cost_ids(perf[e], m), [('PERFECT', {'cards': after}, None)]
        if not s['options']:
            return NO_POOL, None, []      # the slot has only perfect enchants: no normal pool for the grade
        w = option_weights(v, sl, cards, ts, m)
        if sum(w.values()) == 0:
            return 'no option can be rolled (MaxSame)', None, []
        why = afford(s['cost'])
        if why:
            return why, None, []
        allowed = []
        for e, x in w.items():
            if x:
                if s['success_reset']:
                    after = [0, 0, 0, 0]
                    after[sl] = e
                    allowed.append(('SUCCESS', {'cards': after, 'refine': 0}, None))
                else:
                    after = list(cards)
                    after[sl] = e
                    allowed.append(('SUCCESS', {'cards': after}, None))
        if s['failures']:
            for f in s['failures']:
                allowed += failure_states(f, cards, refine, ts, sl, m)
        elif s['chance'] < CHANCE_BASE:
            allowed.append(('STOCK_FAIL', {}, None))
        return None, cost_ids(s['cost'], m), allowed

    if op == 'u':
        sl = rec['arg']
        if sl >= 4 or sl < ts or cards[sl] == 0 or sl not in v['slots']:
            return 'nothing to upgrade', None, []
        ups = {m.iid(a): u for a, u in v['slots'][sl]['upgrades'].items()}
        u = ups.get(cards[sl])
        if u is None:
            return 'no upgrade for this enchant', None, []
        why = afford(u['cost'])
        if why:
            return why, None, []
        after = list(cards)
        after[sl] = m.iid(u['to'])
        allowed = [('SUCCESS', {'cards': after}, None)]
        for f in u['failures']:
            allowed += failure_states(f, cards, refine, ts, sl, m)
        return None, cost_ids(u['cost'], m), allowed

    if op == 'r':
        r = v['reset']
        if r is None:
            return 'no reset', None, []
        if refine < v['min_refine']:
            return 'refine below the group minimum', None, []
        if not any(cards[i] for i in range(ts, 4)):
            return 'nothing enchanted', None, []
        if r['require_all'] and any(cards[sl] == 0 for sl in v['order'] if sl >= ts):
            return 'RequireAllFilled', None, []
        if r['scope'] and not any(cards[sc] for sc in r['scope'] if ts <= sc < 4):
            return 'scope empty', None, []
        why = afford(r['cost'])
        if why:
            return why, None, []
        cleared = list(cards)
        for i in range(ts, 4):
            if not r['scope'] or i in r['scope']:
                cleared[i] = 0
        allowed = []
        if r['outcomes']:
            for o in r['outcomes']:
                if o['Result'] == 'SUCCESS':
                    allowed.append(('SUCCESS', {'cards': cleared}, None))
                else:
                    allowed += failure_states(o, cards, refine, ts, 0, m)
        else:
            allowed.append(('SUCCESS', {'cards': cleared}, None))
            if r['chance'] < CHANCE_BASE:
                allowed.append(('STOCK_FAIL', {}, None))
        return None, cost_ids(r['cost'], m), allowed
    return 'unknown op', None, []


def failure_states(f, cards, refine, ts, sl, m):
    res = f['Result']
    rws = [(m.iid(r['Item']), int(r.get('Amount', 1))) for r in f.get('Rewards') or []] or None
    if res == 'FAIL_KEEP':
        return [(res, {}, None)]
    if res == 'CLEAR_SLOTS':
        after = list(cards)
        for c in f.get('Slots') or []:
            if int(c) >= ts:
                after[int(c)] = 0
        return [(res, {'cards': after}, None)]
    if res == 'CLEAR_ENCHANTS':
        after = [cards[i] if i < ts else 0 for i in range(4)]
        return [(res, {'cards': after}, None)]
    if res == 'REFINE_DOWN':
        if f.get('Random'):
            return [(res, {'refine': refine - k}, None) for k in range(refine + 1)]
        return [(res, {'refine': max(0, refine - int(f.get('Amount', 1)))}, None)]
    if res == 'DOWNGRADE':
        after = list(cards)
        after[sl] = m.iid(f['To'])
        return [(res, {'cards': after}, None)]
    if res == 'DESTROY':
        return [(res, None, None)]
    if res == 'REWARD':
        return [(res, {}, rws)]
    if res == 'DESTROY_WITH_REWARD':
        return [(res, None, rws)]
    return []


def message_ok(result, msg):
    if result in ('SUCCESS', 'PERFECT'):
        return msg == MSG_SUCCESS
    if result in ('DESTROY', 'DESTROY_WITH_REWARD'):
        return msg == MSG_BREAK
    return msg == MSG_FAILED


def check_try(m, rec):
    """-> (result label, [problems])"""
    why, cost, allowed = expect(m, rec)
    b, a = rec['before'], rec['after']
    inv = {int(k): x for k, x in rec['inv'].items()}
    dz = rec['zeny'][1] - rec['zeny'][0]
    if why:
        probs = []
        if rec['msg'] != (MSG_FAILED if why == NO_POOL else MSG_REJECT):
            probs.append('expected a refusal (%s), got msg %d' % (why, rec['msg']))
        if dz or inv or a != b:
            probs.append('refused request (%s) changed something: zeny %+d inv %s' % (why, dz, inv))
        return 'REFUSED(%s)' % why, probs
    z, mats = cost
    matches = []
    for res, fields, rws in allowed:
        if fields is None:
            if a.get('exists'):
                continue
        else:
            if not a.get('exists'):
                continue
            want = dict(b)
            want.update(fields)
            if any(a.get(k) != want.get(k) for k in ('id', 'refine', 'grade', 'bound', 'attr', 'cards', 'opts', 'uid')):
                continue
        exp_inv = {i: -n for i, n in mats.items()}
        if fields is None:
            exp_inv[b['id']] = exp_inv.get(b['id'], 0) - 1
        options = [exp_inv]
        if rws:
            options = []
            for rid, n in rws:
                e = dict(exp_inv)
                e[rid] = e.get(rid, 0) + n
                options.append(e)
        options = [{k: x for k, x in e.items() if x} for e in options]
        if dz != -z or inv not in options:
            continue
        if not message_ok(res, rec['msg']):
            continue
        matches.append(res)
    if matches:
        return matches[0], []
    return 'MISMATCH', ['no allowed result matches: msg %d, zeny %+d (cost %d), inv %s, before %s, after %s, allowed %s'
                        % (rec['msg'], dz, z, inv, b['cards'], a.get('cards'), sorted({x[0] for x in allowed}))]


def ingame_verify(args):
    m = Model()
    total, fails, labels, errors, done = 0, [], defaultdict(lambda: defaultdict(int)), [], False
    for line in io.open(args.result, encoding='utf-8'):
        rec = json.loads(line)
        if 'error' in rec:
            errors.append(rec)
            continue
        if rec.get('done'):
            done = True
            continue
        if 'tag' not in rec:
            continue
        total += 1
        label, probs = check_try(m, rec)
        kind = rec['tag'].split(':')[1]
        labels[kind][label.split('(')[0]] += 1
        for pr in probs:
            fails.append('%s (line %d): %s' % (rec['tag'], rec['line'], pr))
    print('in-game verify: %d requests, %d failures, %d runner errors, finished=%s' % (total, len(fails), len(errors), done))
    for kind in sorted(labels):
        print('  %-11s %s' % (kind, ', '.join('%s %d' % kv for kv in sorted(labels[kind].items()))))
    for e in errors[:20]:
        print('  RUNNER ' + json.dumps(e))
    for f_ in fails[:60]:
        print('  FAIL ' + f_)
    return 1 if fails or errors or not done else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    a = sub.add_parser('offline-plan')
    a.add_argument('--out', required=True)
    a = sub.add_parser('offline-verify')
    a.add_argument('--plan')
    a.add_argument('--result', required=True)
    a = sub.add_parser('ingame-plan')
    a.add_argument('--out', required=True)
    a = sub.add_parser('ingame-verify')
    a.add_argument('--result', required=True)
    args = ap.parse_args(argv)
    return {'offline-plan': offline_plan, 'offline-verify': offline_verify,
            'ingame-plan': ingame_plan, 'ingame-verify': ingame_verify}[args.cmd](args) or 0


if __name__ == '__main__':
    sys.exit(main())

# -*- coding: utf-8 -*-
"""Import LIVE legacy enchant NPCs into the master (one-time importer, kept for audit).

    python tools/need_enchant/import_legacy.py --out db/need/enchant_master.yml

Every slot gets a `Parity` block (script chain line, rand window, value -> result map) so
check_parity.py can re-derive the exact integer table from the script at any time and compare it
with the master, whoever edits the master later.

Migration rules (operator decision 2026-10-06): rates, costs and outcomes = current script code;
only data-loss behaviour is not reproduced (the engine keeps refine/cards/options/bound/unique id).
"""
import argparse
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import generate   # noqa: E402
import parity     # noqa: E402

REPO = generate.REPO
VERUS = 'npc/re/merchants/enchan_verus.txt'
Q161 = 'npc/re/quests/quests_16_1.txt'


def chain_slot(items_by_id, script, chain, lo, hi, card, value_map, cost_line, empty=None, indent=10):
    """One slot from a threshold chain. value_map: special value -> result ('REROLL' drops the value)."""
    dist = parity.table(os.path.join(REPO, script), chain, lo, hi)
    pad = ' ' * indent
    opts, fails = [], {}
    for value, n in dist.items():
        key = value if value is not None else 'EMPTY'
        if value is None:
            if empty is None:
                raise SystemExit('%s:%d window %d..%d leaves values unassigned' % (script, chain, lo, hi))
            fails[empty] = fails.get(empty, 0) + n
        elif value in value_map:
            if value_map[value] != 'REROLL':
                fails[value_map[value]] = fails.get(value_map[value], 0) + n
        else:
            opts.append((items_by_id[value].aegis, n))
        del key
    mapping = ', '.join('%d: %s' % (k, v) for k, v in sorted(value_map.items()))
    L = ['%s- Slot: %d' % (pad, card)]
    if cost_line:
        L.append('%s  %s' % (pad, cost_line))
    L.append('%s  Parity: { Chain: %d, Window: [ %d, %d ], Map: { %s }%s }' % (
        pad, chain, lo, hi, mapping, (', Empty: %s' % empty) if empty else ''))
    L.append('%s  Options:' % pad)
    L += ['%s    - { Enchant: %s, Weight: %d }' % (pad, a, n) for a, n in opts]
    if fails:
        L.append('%s  Failures:' % pad)
        L += ['%s    - { Result: %s, Weight: %d }' % (pad, r, n) for r, n in fails.items()]
    return L


# ----------------------------------------------------------------------------- Charleston (enchan_verus.txt, Mass Charleston)

# en_type -> rand chain line (`.@r = rand( .@en_brk1, .@en_brk2 )` + 1) and windows per card slot (L194-622)
CHARLESTON_TYPES = {
    1: (214, {3: (1, 100), 2: (101, 200), 1: (101, 300)}),
    2: (270, {3: (1, 100), 2: (101, 200), 1: (101, 310)}),
    3: (326, {3: (1, 100), 2: (101, 200), 1: (101, 310)}),
    4: (380, {3: (1, 100), 2: (101, 200), 1: (101, 310)}),
    5: (443, {3: (1, 100), 2: (101, 200), 1: (101, 310)}),
    6: (509, {3: (1, 100), 2: (101, 200), 1: (101, 310)}),
    7: (563, {3: (1, 100), 2: (101, 200), 1: (101, 310)}),
    8: (623, {3: (1, 100), 2: (1, 111)}),
}
CHARLESTON_VARIANTS = [
    # key, display KR, display EN, targets, en_type, MinimumRefine
    ('CHARLESTON_ARMOR_ACCEL', '갑옷 - 가속 강화', 'Armor - Acceleration', ['Upgrade_Part_Plate', 'Supplement_Part_Str'], 3, 0),
    ('CHARLESTON_PLATE_ATTACK', '플레이트 - 공격 강화 (+9)', 'Plate - Attack (+9)', ['Upgrade_Part_Plate'], 1, 9),
    ('CHARLESTON_STR_DEFENSE', '서플먼트 STR - 방어 강화 (+9)', 'Supplement STR - Defense (+9)', ['Supplement_Part_Str'], 2, 9),
    ('CHARLESTON_GARMENT', '걸칠것 강화', 'Garment', ['Upgrade_Part_Engine', 'Supplement_Part_Con'], 4, 0),
    ('CHARLESTON_GARMENT_ADV', '고급 걸칠것 강화 (+9)', 'Advanced Garment (+9)', ['Upgrade_Part_Engine', 'Supplement_Part_Con'], 5, 9),
    ('CHARLESTON_SHOES', '신발 강화', 'Shoes', ['Upgrade_Part_Booster', 'Supplement_Part_Agi'], 6, 0),
    ('CHARLESTON_SHOES_ADV', '고급 신발 강화 (+9)', 'Advanced Shoes (+9)', ['Upgrade_Part_Booster', 'Supplement_Part_Agi'], 7, 9),
    ('CHARLESTON_ACCESSORY', '액세서리 강화', 'Accessory', ['Upgrade_Part_Gun_Barrel', 'Supplement_Part_Dex'], 8, 0),
]
CHARLESTON_COST = 'Cost: { Zeny: 100000, Materials: [ { Item: Charleston_Parts, Amount: 1 } ] }'


def charleston(items_by_id):
    L = ['  - Family: CHARLESTON',
         '    Display: { KR: "위기의 찰스턴 파츠", EN: "Charleston Crisis Parts" }',
         '    Variants:']
    for key, kr, en, targets, en_type, min_refine in CHARLESTON_VARIANTS:
        chain, windows = CHARLESTON_TYPES[en_type]
        L += ['      - Key: %s' % key,
              '        Display: { KR: "%s", EN: "%s" }' % (kr, en),
              '        Source: { Script: %s, Npc: "Mass Charleston", EnType: %d }' % (VERUS, en_type),
              '        TargetItems: [ %s ]' % ', '.join(targets),
              '        MinimumRefine: %d' % min_refine,
              '        Order: [ %s ]' % ', '.join(str(c) for c in windows),
              '        Cost: { Zeny: 100000, Materials: [ { Item: Charleston_Parts, Amount: 1 } ] }',
              '        Reset: { Chance: 100000, Cost: { Zeny: 100000, Materials: [ { Item: Charleston_Parts, Amount: 1 } ] } }',
              '        Slots:']
        for card, (lo, hi) in windows.items():
            L += chain_slot(items_by_id, VERUS, chain, lo, hi, card, {0: 'FAIL_KEEP', 9: 'DESTROY'}, None)
    return L


# ----------------------------------------------------------------------------- EP16.1 Dylan (quests_16_1.txt)

def ep161(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, Q161))
    hits, total = parity.inline_rand_lt(lines, 16838)
    rewards = parity.table(os.path.join(REPO, Q161), 16846, 1, 100)
    L = ['  - Family: EP161_HONOR',
         '    Display: { KR: "명예의 증표 장비 (EP16.1)", EN: "Token of Honor Gear (EP16.1)" }',
         '    Variants:',
         '      - Key: EP161_ROBE',
         '        Display: { KR: "아첨/독설의 로브", EN: "Robe of Flattery / Vituperation" }',
         '        Source: { Script: %s, Npc: "Dylan#pa0829" }' % Q161,
         '        TargetItems: [ Robe_Of_Flattery, Robe_Of_Vituperation ]',
         '        Order: [ 3, 2 ]',
         '        Cost: { Materials: [ { Item: TokenOfHonor, Amount: 20 } ] }',
         '        Reset:',
         '          Chance: 100000',
         '          RequireAllFilled: true',
         '          Cost: { Materials: [ { Item: TokenOfHonor, Amount: 10 } ] }',
         '        Slots:']
    for card in (3, 2):
        L += chain_slot(items_by_id, Q161, 16687, 1, 961, card, {0: 'FAIL_KEEP'}, None)
    L += ['      - Key: EP161_BADGE',
          '        Display: { KR: "프론테라 배지", EN: "Prontera Badge" }',
          '        Source: { Script: %s, Npc: "Dylan#pa0829" }' % Q161,
          '        TargetItems: [ BadgeOfProntera_ ]',
          '        Order: [ 3, 2 ]',
          '        Cost: { Materials: [ { Item: TokenOfHonor, Amount: 5 } ] }',
          '        Reset:',
          '          RequireAllFilled: true',
          '          Cost: { Materials: [ { Item: TokenOfHonor, Amount: 10 } ] }',
          '          Parity: { SuccessLine: 16838, RewardChain: 16846, RewardWindow: [ 1, 100 ] }',
          '          Outcomes:',
          '            - { Result: SUCCESS, Weight: %d }' % hits,
          '            - Result: DESTROY_WITH_REWARD',
          '              Weight: %d' % (total - hits),
          '              Rewards:']
    for value, n in rewards.items():
        iid, amount = value.split(':')
        L.append('                - { Item: %s, Amount: %s, Weight: %d }' % (items_by_id[int(iid)].aegis, amount, n))
    L.append('        Slots:')
    for card in (3, 2):
        L += chain_slot(items_by_id, Q161, 16740, 1, 22231, card, {0: 'FAIL_KEEP'}, None)
    return L


# ----------------------------------------------------------------------------- Mora (enchan_mora.txt, "유물 연구원#new")

MORA = 'npc/re/merchants/enchan_mora.txt'
# enchant type -> first of the three `rand` window lines (slot 4, 3, 2 = card 3, 2, 1); the chain starts 8 lines below
MORA_TYPE_RAND = {1: 898, 2: 925, 3: 952, 4: 979, 5: 1006, 6: 1033, 8: 1087, 9: 1114, 10: 1141, 11: 1168}
MORA_TYPE_NAME = {   # L857 .@enchant_type$[] (type 7 is never referenced by a target)
    1: ('ATK', '공격 유형', 'ATK Type'), 2: ('CRITICAL', '크리티컬 유형', 'Critical Type'),
    3: ('EVASION', '회피 유형', 'Evasion Type'), 4: ('HEALER', '힐러 유형', 'Healer Type'),
    5: ('SPELL1', '마법 능력 1', 'Spell Ability 1'), 6: ('ASSIST1', '보조 능력 1', 'Assist Ability 1'),
    8: ('STRENGTH', '강도', 'Toughness Type'), 9: ('RANGED', '원거리 유형', 'Ranged Type'),
    10: ('PHYSICAL', '물리 유형', 'Physical Type'), 11: ('SPELL2', '마법 능력 2', 'Spell Ability 2')}
MORA_COST = '{ Zeny: 100000, Materials: [ { Item: Mora_Coin, Amount: 1 } ] }'
RE_CALLSUB = re.compile(r'^\s*case\s+(\d+)\s*:\s*callsub\s+L_Socket\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*;')


def mora_callsubs(lines):
    """-> [(line, item id, type, bonus type, allowed slot)] of every `case <id>: callsub L_Socket,...`."""
    out = []
    for n, raw in enumerate(lines, 1):
        m = RE_CALLSUB.match(parity._strip(raw))
        if m:
            out.append((n,) + tuple(int(x) for x in m.groups()))
    return out


def mora_order(allowed):
    # L869-888: card[3] if allowed < 4, card[2] if allowed < 3, card[1] if allowed < 2
    return [c for c, slot in ((3, 4), (2, 3), (1, 2)) if allowed < slot]


def mora_variants(lines):
    """Base variant per (type, order) and +9 bonus variant per (bonus type, order), as the script offers them."""
    groups = {}
    for line, iid, etype, bonus, allowed in mora_callsubs(lines):
        groups.setdefault((etype, False, allowed), []).append((line, iid))
        if bonus > 0:
            groups.setdefault((bonus, True, allowed), []).append((line, iid))
    allowed_set = sorted({k[2] for k in groups})
    out = []
    for (etype, plus9, allowed), members in sorted(groups.items()):
        code = MORA_TYPE_NAME[etype][0]
        key = 'MORA_%s%s%s' % ('P9_' if plus9 else '', code, '_ACC' if allowed != allowed_set[0] else '')
        out.append((key, etype, plus9, allowed, members))
    return out


def mora(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, MORA))
    L = ['  - Family: MORA_ARTIFACT',
         '    Display: { KR: "모라 유물", EN: "Mora Artifacts" }',
         '    Variants:']
    for key, etype, plus9, allowed, members in mora_variants(lines):
        code, kr, en = MORA_TYPE_NAME[etype]
        order = mora_order(allowed)
        acc = len(order) == 2
        L += ['      - Key: %s' % key,
              '        Display: { KR: "%s%s%s", EN: "%s%s%s" }' % (
                  kr, ' (액세서리)' if acc else '', ' (+9)' if plus9 else '',
                  en, ' (Accessory)' if acc else '', ' (+9)' if plus9 else ''),
              '        Source: { Script: %s, Npc: "유물 연구원#new", Type: %d, Bonus: %s, Callsub: [ %s ] }' % (
                  MORA, etype, 'true' if plus9 else 'false', ', '.join(str(ln) for ln, _ in members)),
              '        TargetItems: [ %s ]' % ', '.join(items_by_id[iid].aegis for _, iid in members),
              '        MinimumRefine: %d' % (9 if plus9 else 0),
              '        Order: [ %s ]' % ', '.join(str(c) for c in order),
              '        Cost: %s' % MORA_COST,
              '        Reset: { Chance: 100000, Cost: %s }' % MORA_COST,
              '        Slots:']
        rand_line = MORA_TYPE_RAND[etype]
        for i, card in enumerate((3, 2, 1)):
            if card not in order:
                continue
            lo, hi = parity.rand_window(lines, rand_line + i)
            # 0 and 9 jump back to L_enchant (L1201) before the fee is taken: a re-roll, never a result
            sl = chain_slot(items_by_id, MORA, rand_line + 8, lo, hi, card, {0: 'REROLL', 9: 'REROLL'}, None)
            sl[1] = sl[1].replace('Parity: { Chain', 'Parity: { WindowLine: %d, Chain' % (rand_line + i))
            L += sl
    return L


# ----------------------------------------------------------------------------- Sarah's earrings (SarahAndFenrir.txt)

SARAH = 'npc/NEED/instances/SarahAndFenrir.txt'
SARAH_SIDES = [   # key part, item, size line, chance line (L417-428)
    ('L', 'Earring_Of_Sarah_L', '왼쪽', 'Left', 420, 421),
    ('R', 'Earring_Of_Sarah_R', '오른쪽', 'Right', 426, 427),
]
SARAH_FAMILIES = [   # select() order at L455 -> setarray line
    ('CRI', 457, 'CRI 또는 크리티컬', 'Fatal or Critical'),
    ('ARCHER', 468, '명궁 또는 회피', 'Expert Archer or Parrying'),
    ('THRIFT', 479, '절약 또는 MATK', 'Saving or MATK'),
    ('DELAY', 490, '공격 후딜 또는 스킬 후딜', 'After Attack Delay or After Skill Delay'),
]
SARAH_RAND, SARAH_LOOP = 537, 538


def loop_slot(items_by_id, lines, card, array_line, size_line, chance_line, overflow, indent=10):
    dist = parity.loop_table(lines, SARAH_RAND, SARAH_LOOP, array_line, size_line, chance_line)
    pad = ' ' * indent
    L = ['%s- Slot: %d' % (pad, card),
         '%s  Parity: { Rand: %d, Loop: %d, Array: %d, Size: %d, Chance: %d, Overflow: %s }' % (
             pad, SARAH_RAND, SARAH_LOOP, array_line, size_line, chance_line, overflow),
         '%s  Options:' % pad]
    L += ['%s    - { Enchant: %s, Weight: %d }' % (pad, items_by_id[v].aegis, n) for v, n in dist.items() if v != 'OVERFLOW']
    if dist.get('OVERFLOW'):
        L += ['%s  Failures:' % pad, '%s    - { Result: %s, Weight: %d }' % (pad, overflow, dist['OVERFLOW'])]
    return L


def sarah(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, SARAH))
    L = ['  - Family: SARAH_EARRING',
         '    Display: { KR: "사라의 귀걸이", EN: "Sarah\'s Earrings" }',
         '    Variants:']
    for side, item, side_kr, side_en, size_line, chance_line in SARAH_SIDES:
        for fam, array_line, kr, en in SARAH_FAMILIES:
            L += ['      - Key: SARAH_%s_%s' % (side, fam),
                  '        Display: { KR: "%s - %s", EN: "%s - %s" }' % (side_kr, kr, side_en, en),
                  '        Source: { Script: %s, Npc: "수석 조교#a1" }' % SARAH,
                  '        TargetItems: [ %s ]' % item,
                  # L436-450: card[3] first, then card[2]; refused once card[2] is set
                  '        Order: [ 3, 2 ]',
                  '        Cost: { Materials: [ { Item: Piece_Of_Gigantes, Amount: 4 } ] }',
                  # L555-635: 1 fragment, needs card[3] (an enchanted earring), always succeeds
                  '        Reset: { Chance: 100000, Cost: { Materials: [ { Item: Piece_Of_Gigantes, Amount: 1 } ] } }',
                  '        Slots:']
            for card in (3, 2):
                # L539: .@i == .@bonus_size -> the earring (already delequip'ed at L536) is gone
                L += loop_slot(items_by_id, lines, card, array_line, size_line, chance_line, 'DESTROY')
    return L


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    args = ap.parse_args(argv)
    _items, items_by_id = generate.load_items(REPO)
    L = ['# NEED Unified Enchant V2 master (source of truth).',
         '# First imported from the LIVE scripts by tools/need_enchant/import_legacy.py on 2026-10-06.',
         '# Every Parity block is re-checked against the scripts by tools/need_enchant/check_parity.py.',
         'Header:', '  Type: NEED_ENCHANT_MASTER', '  Version: 1', 'Body:']
    L += charleston(items_by_id)
    L += ep161(items_by_id)
    L += mora(items_by_id)
    L += sarah(items_by_id)
    io.open(args.out, 'w', encoding='utf-8', newline='\n').write('\n'.join(L) + '\n')
    print('wrote', args.out)


if __name__ == '__main__':
    main()

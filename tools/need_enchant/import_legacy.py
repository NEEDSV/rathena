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
    io.open(args.out, 'w', encoding='utf-8', newline='\n').write('\n'.join(L) + '\n')
    print('wrote', args.out)


if __name__ == '__main__':
    main()

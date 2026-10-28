# -*- coding: utf-8 -*-
"""Build the generator test fixture master from the LIVE scripts (via parity.py).

Real systems: Charleston Upgrade_Part_Plate (en_type 3 normal / en_type 1 at +9), Prontera badge.
Synthetic:    FIXTURE_COVERAGE exercises MaximumRefine, Require, MaxSame, Perfect, Upgrades + DOWNGRADE.
The fixture is a test input only; it is never installed.
"""
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import parity      # noqa: E402
import generate    # noqa: E402

REPO = generate.REPO
VERUS = os.path.join(REPO, 'npc', 're', 'merchants', 'enchan_verus.txt')
Q161 = os.path.join(REPO, 'npc', 're', 'quests', 'quests_16_1.txt')
SPECIAL = {0: 'FAIL_KEEP', 9: 'DESTROY', None: 'FAIL_KEEP'}


def slot_yaml(items_by_id, dist, card, indent, cost_lines):
    pad = ' ' * indent
    L = ['%s- Slot: %d' % (pad, card)] + cost_lines
    opts, fails = [], {}
    for value, n in dist.items():
        if value in SPECIAL:
            fails[SPECIAL[value]] = fails.get(SPECIAL[value], 0) + n
        else:
            opts.append((items_by_id[value].aegis, n))
    L.append('%s  Options:' % pad)
    L += ['%s    - { Enchant: %s, Weight: %d }' % (pad, a, n) for a, n in opts]
    if fails:
        L.append('%s  Failures:' % pad)
        L += ['%s    - { Result: %s, Weight: %d }' % (pad, r, n) for r, n in fails.items()]
    return L


def charleston(items_by_id, key, display_kr, display_en, chain, windows, min_refine):
    cost = ['            Cost: { Zeny: 100000, Materials: [ { Item: Charleston_Parts, Amount: 1 } ] }']
    L = ['      - Key: %s' % key,
         '        Display: { KR: "%s", EN: "%s" }' % (display_kr, display_en),
         '        Source: { Script: npc/re/merchants/enchan_verus.txt, Chain: %d }' % chain,
         '        TargetItems: [ Upgrade_Part_Plate ]',
         '        MinimumRefine: %d' % min_refine,
         '        Order: [ 3, 2, 1 ]',
         '        Reset: { Chance: 100000, Cost: { Zeny: 100000, Materials: [ { Item: Charleston_Parts, Amount: 1 } ] } }',
         '        Slots:']
    for card, (lo, hi) in zip((3, 2, 1), windows):
        L += slot_yaml(items_by_id, parity.table(VERUS, chain, lo, hi), card, 10, cost)
    return L


def main():
    _items, items_by_id = generate.load_items(REPO)
    L = ['# GENERATED test fixture (tools/need_enchant/fixtures/build_fixture.py) - not installed anywhere.',
         'Header:', '  Type: NEED_ENCHANT_MASTER', '  Version: 1', 'Body:',
         '  - Family: FIXTURE_CHARLESTON_PLATE',
         '    Display: { KR: "찰스턴 파츠 - 플레이트", EN: "Charleston Parts - Plate" }',
         '    Variants:']
    L += charleston(items_by_id, 'FIXTURE_CHARLESTON_PLATE_ACCEL', '가속 강화', 'Acceleration', 326,
                    [(1, 100), (101, 200), (101, 310)], 0)
    L += charleston(items_by_id, 'FIXTURE_CHARLESTON_PLATE_ATTACK', '공격 강화 (+9)', 'Attack (+9)', 214,
                    [(1, 100), (101, 200), (101, 300)], 9)
    badge = parity.table(Q161, 16740, 1, 22231)
    cost = ['            Cost: { Materials: [ { Item: TokenOfHonor, Amount: 5 } ] }']
    L += ['  - Family: FIXTURE_PRONTERA_BADGE',
          '    Display: { KR: "프론테라 배지", EN: "Prontera Badge" }',
          '    Variants:',
          '      - Key: FIXTURE_PRONTERA_BADGE',
          '        Source: { Script: npc/re/quests/quests_16_1.txt, Chain: 16740 }',
          '        TargetItems: [ BadgeOfProntera_ ]',
          '        Order: [ 3, 2 ]',
          '        Reset:',
          '          Cost: { Materials: [ { Item: TokenOfHonor, Amount: 10 } ] }',
          '          RequireAllFilled: true',
          '          Outcomes:',
          '            - { Result: SUCCESS, Weight: 79 }',
          '            - Result: DESTROY_WITH_REWARD',
          '              Weight: 21',
          '              Rewards:']
    for amount, weight in ((9, 60), (10, 20), (11, 6), (12, 6), (13, 5), (14, 2), (15, 1)):
        L.append('                - { Item: RuneMagicPowder, Amount: %d, Weight: %d }' % (amount, weight))
    L.append('        Slots:')
    for card in (3, 2):
        L += slot_yaml(items_by_id, badge, card, 10, cost)
    L += ['  - Family: FIXTURE_COVERAGE',
          '    Variants:',
          '      - Key: FIXTURE_COVERAGE',
          '        TargetItems: [ Upgrade_Part_Booster ]',
          '        MinimumRefine: 0',
          '        MaximumRefine: 8',
          '        Order: [ 3, 2 ]',
          '        Cost: { Zeny: 1000 }',
          '        Slots:',
          '          - Slot: 3',
          '            Options: [ { Enchant: Agility1, Weight: 3 }, { Enchant: Agility2, Weight: 1 } ]',
          '            MaxSame: [ { Enchant: Agility2, Max: 1 } ]',
          '            Upgrades:',
          '              - { Enchant: Agility1, To: Agility2, Cost: { Zeny: 500 }, SuccessWeight: 80,',
          '                  Failures: [ { Result: DOWNGRADE, To: Agility1, Weight: 20 } ] }',
          '          - Slot: 2',
          '            MinimumRefine: 7',
          '            Require: [ { Slot: 3, Enchants: [ Agility2 ] } ]',
          '            Options: [ { Enchant: Agility1, Weight: 1 } ]',
          '            Failures: [ { Result: REFINE_DOWN, Random: true, Weight: 1 } ]',
          '            Perfect: [ { Enchant: Agility2, Cost: { Zeny: 2000 } } ]']
    out = os.path.join(HERE, 'master_fixture.yml')
    io.open(out, 'w', encoding='utf-8', newline='\n').write('\n'.join(L) + '\n')
    print('wrote', out)


if __name__ == '__main__':
    main()

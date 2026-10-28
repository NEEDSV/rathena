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
         '        Source: { Script: %s, Npc: "Dylan#pa0829", Access: "quest:12369:2" }' % Q161,
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
          '        Source: { Script: %s, Npc: "Dylan#pa0829", Access: "quest:12369:2" }' % Q161,
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
                  '        Source: { Script: %s, Npc: "수석 조교#a1", Access: "var:sarah_fenrir" }' % SARAH,
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


# ----------------------------------------------------------------------------- Horror Toy Factory (검은 수염 죠#pa0829)

HORROR = 'npc/re/merchants/HorrorToyFactory_merchants.txt'
HORROR_COST = '{ Materials: [ { Item: Bloody_Coin, Amount: 15 } ] }'   # L298-299, every system
# key, display KR/EN, targets (switch L59-86), [(card, window line, chain line)], failure map
HORROR_VARIANTS = [
    ('HORROR_ACC_STR', '액세서리 - STR/AGI/DEX', 'Accessory - STR/AGI/DEX',
     ['Red_Lantern', 'Hurt_Mind', 'KindHeart', 'Evilspirit_Gloves'], [(3, 169, 182), (2, 171, 182), (1, 173, 182)], {0: 'DESTROY'}, [60, 61, 62, 63]),
    ('HORROR_ACC_INT', '액세서리 - INT/VIT/DEX', 'Accessory - INT/VIT/DEX',
     ['Red_Lantern', 'Hurt_Mind', 'KindHeart', 'Evilspirit_Gloves'], [(3, 169, 199), (2, 171, 199), (1, 173, 199)], {0: 'DESTROY'}, [60, 61, 62, 63]),
    ('HORROR_WEAPON', '무기', 'Weapon', ['Old_Parasol'], [(3, 215, 216)], {}, [71]),
    ('HORROR_HELM_SHOES', '투구 / 신발', 'Helm / Shoes', ['Lush_Rose', 'Celines_Ribbon', 'Shadow_Walk_'],
     [(3, 239, 240), (2, 260, 261)], {}, [66, 67, 68]),
]


def horror(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, HORROR))
    L = ['  - Family: HORROR_TOY_FACTORY',
         '    Display: { KR: "호러 장난감 공장", EN: "Horror Toy Factory" }',
         '    Variants:']
    for key, kr, en, targets, slots, vmap, tlines in HORROR_VARIANTS:
        L += ['      - Key: %s' % key,
              '        Display: { KR: "%s", EN: "%s" }' % (kr, en),
              '        Source: { Script: %s, Npc: "검은 수염 죠#pa0829", TargetLines: [ %s ], TargetExact: true }' % (
                  HORROR, ', '.join(str(x) for x in tlines)),
              '        TargetItems: [ %s ]' % ', '.join(targets),
              '        Order: [ %s ]' % ', '.join(str(c) for c, _, _ in slots),
              '        Cost: %s' % HORROR_COST,
              '        Slots:']
        for card, wline, chain in slots:
            lo, hi = parity.rand_window(lines, wline)
            sl = chain_slot(items_by_id, HORROR, chain, lo, hi, card, vmap, None)
            sl[1] = sl[1].replace('Parity: { Chain', 'Parity: { WindowLine: %d, Chain' % wline)
            L += sl
    return L


# ----------------------------------------------------------------------------- Infinite Space (유물 강화사#pa0829_01)

INFINITE = 'npc/re/merchants/InfiniteSpace_merchants.txt'
INFINITE_PARTS = [   # key part, display, targets (L235 by part), pool setarray line (switch L89-109)
    ('WEAPON', '무기', 'Weapon', ['Whip_Of_Infinite', 'Viollin_Of_Infinite', 'Huuma_Of_Infinite', 'Gun_Of_Infinite',
                                 'Dagger_Of_Infinite', 'D_Staff_Of_Infinite', 'Mace_Of_Infinite', 'D_Sword_Of_Infinite',
                                 'Axe_Of_Infinite', 'Bow_Of_Infinite'], 91),
    ('ARMOR', '갑옷 / 신발', 'Armor / Shoes', ['Armor_Of_Goddess', 'Shoes_Of_Cracks'], 98),
    ('GARMENT', '걸칠것 / 투구', 'Garment / Helm', ['ManteauOfCracks', 'Accessories_Of_Goddess'], 105),
]
INFINITE_TYPES = [('PHYSICAL', '물리', 'Physical'), ('MAGICAL', '마법', 'Magical'), ('RANGED', '원거리', 'Ranged')]  # L116
INFINITE_PICK = 141


def explode_slot(items_by_id, lines, card, setarray_line, index, field, indent=10):
    dist = parity.explode_pool(lines, setarray_line, index, field, INFINITE_PICK)
    pad = ' ' * indent
    L = ['%s- Slot: %d' % (pad, card),
         '%s  Parity: { Strings: %d, Index: %d, Field: %d, Pick: %d }' % (pad, setarray_line, index, field, INFINITE_PICK),
         '%s  Options:' % pad]
    L += ['%s    - { Enchant: %s, Weight: %d }' % (pad, items_by_id[v].aegis, n) for v, n in dist.items()]
    return L


def infinite(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, INFINITE))
    hits, total = parity.inline_rand_cmp(lines, 216, {'.@break_chance': 156})
    L = ['  - Family: INFINITE_SPACE',
         '    Display: { KR: "무한의 공간 유물", EN: "Infinite Space Artifacts" }',
         '    Variants:']
    for part, pkr, pen, targets, sline in INFINITE_PARTS:
        for field, (tkey, tkr, ten) in enumerate(INFINITE_TYPES):
            L += ['      - Key: INFINITE_%s_%s' % (part, tkey),
                  '        Display: { KR: "%s - %s", EN: "%s - %s" }' % (pkr, tkr, pen, ten),
                  '        Source: { Script: %s, Npc: "유물 강화사#pa0829_01", TargetLines: [ 235 ] }' % INFINITE,
                  '        TargetItems: [ %s ]' % ', '.join(targets),
                  # L84/L110-111: card[3] with pool 0, then card[2] with pool 1; refused once card[2] is set
                  '        Order: [ 3, 2 ]',
                  '        Cost: { Materials: [ { Item: Shattered_Rune, Amount: 20 } ] }',
                  # L153-225: 30 runes, needs an enchant, rand(1,100) > 30 keeps the item, else it is destroyed
                  '        Reset:',
                  '          Cost: { Materials: [ { Item: Shattered_Rune, Amount: 30 } ] }',
                  '          Parity: { SuccessLine: 216, Vars: { .@break_chance: 156 } }',
                  '          Outcomes:',
                  '            - { Result: SUCCESS, Weight: %d }' % hits,
                  '            - { Result: DESTROY, Weight: %d }' % (total - hits),
                  '        Slots:']
            for card, index in ((3, 0), (2, 1)):
                L += explode_slot(items_by_id, lines, card, sline, index, field)
    return L


# ----------------------------------------------------------------------------- uniform picks (array / F_Rand)

def uniform_slot(items_by_id, lines, card, parity_text, dist, cost_line=None, extra=None, destroy=None, indent=10):
    """dist: value -> count of a uniform pick. destroy: (hits, total) of an inline `rand(n) < k` destroy check."""
    pad = ' ' * indent
    L = ['%s- Slot: %d' % (pad, card)]
    if cost_line:
        L.append('%s  %s' % (pad, cost_line))
    L += ['%s  %s' % (pad, x) for x in (extra or [])]
    L.append('%s  Parity: { %s }' % (pad, parity_text))
    L.append('%s  Options:' % pad)
    keep = (destroy[1] - destroy[0]) if destroy else 1
    L += ['%s    - { Enchant: %s, Weight: %d }' % (pad, items_by_id[v].aegis, n * keep) for v, n in dist.items()]
    if destroy:
        L += ['%s  Failures:' % pad, '%s    - { Result: DESTROY, Weight: %d }' % (pad, destroy[0] * sum(dist.values()))]
    return L


def names(items_by_id, ids):
    return ', '.join(items_by_id[i].aegis for i in ids)


# ----------------------------------------------------------------------------- Old helmet basic (nightmare_biolab.txt)

BIOLAB = 'npc/re/merchants/nightmare_biolab.txt'


def old_helm(items_by_id):
    # line numbers: nightmare_biolab.txt after the ops merge of 2026-10-08 (+85 lines: bully bulk exchange)
    lines = parity.read_lines(os.path.join(REPO, BIOLAB))
    targets = list(range(18971, 18985))   # L523: 18971 .. 18984
    L = ['  - Family: OLD_HELM',
         '    Display: { KR: "낡은 투구 (전사자의 모자)", EN: "Old Headgear (Fallen Warrior Hat)" }',
         '    Variants:',
         '      - Key: OLD_HELM_BASIC',
         '        Display: { KR: "기본 인챈트", EN: "Basic Enchant" }',
         '        Source: { Script: %s, Npc: "nightmare_biolab L501", TargetRange: 523, TargetExact: true }' % BIOLAB,
         '        TargetItems: [ %s ]' % names(items_by_id, targets),
         '        Order: [ 3, 2, 1 ]',                       # L451-462
         '        Caution:',
         '          KR: "성공률: 1번째 100% / 2번째 100% / 3번째 100%\\n특수 인챈트 강화 80~20%, 실패 시 1단계 하락(Lv1 유지)\\n초기화: 100%"',
         '          EN: "Success: 1st 100% / 2nd 100% / 3rd 100%\\nSpecial upgrade 80-20%, failure drops a level (Lv1 kept)\\nReset: 100%"',
         '        Cost: { Materials: [ { Item: %s, Amount: 10 } ] }' % items_by_id[23016].aegis,   # L446/L537
         # L647-708: 10 Pieces_Of_Sentiment, needs card[3], clears card[1..3]
         '        Reset: { Chance: 100000, Cost: { Materials: [ { Item: %s, Amount: 10 } ] } }' % items_by_id[22687].aegis,
         '        Slots:']
    for card, aline, pline in ((3, 627, 658), (2, 627, 658), (1, 661, 680)):
        L += uniform_slot(items_by_id, lines, card, 'Array: %d, Pick: %d' % (aline, pline), parity.array_pick(lines, aline, pline))
    # Phase 2.4: the special upgrade Lv1-10 of the card[1] enchant (L467-619)
    L += old_helm_upgrades(items_by_id, lines)
    return L


# ----------------------------------------------------------------------------- Brosnan (cashmall.txt 피어싱 브로스넌#pa0829)

CASHMALL = 'npc/re/merchants/cashmall.txt'


def brosnan(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, CASHMALL))
    _var, targets = parity.setarray_values(lines, 1488)
    return ['  - Family: BROSNAN',
            '    Display: { KR: "피어싱 브로스넌", EN: "Piercing Brosnan" }',
            '    Variants:',
            '      - Key: BROSNAN_MID',
            '        Display: { KR: "중단 머리 장비", EN: "Middle Headgear" }',
            '        Source: { Script: %s, Npc: "피어싱 브로스넌#pa0829", TargetLines: [ 1488 ], TargetExact: true }' % CASHMALL,
            '        TargetItems: [ %s ]' % names(items_by_id, targets),
            '        Order: [ 3 ]',
            # L1516 charges 1,000,000z (the dialogue says 100,000: the code is canonical)
            '        Cost: { Zeny: 1000000 }',
            # L1541-1567: requires 6909 (Silvervine 6417 is taken first when owned; V2 takes the required 6909)
            '        Reset: { Chance: 100000, Cost: { Materials: [ { Item: %s, Amount: 1 } ] } }' % items_by_id[6909].aegis,
            '        Slots:'] + uniform_slot(items_by_id, lines, 3, 'FRand: 1512', parity.frand(lines, 1512))


# ----------------------------------------------------------------------------- Hero ring (moro_cav_exchange.txt 인챈터 번즈)

MORO = 'npc/re/merchants/moro_cav_exchange.txt'
HERO_SLOTS = [   # card, [(key, KR, EN, setarray line)] (L51-63 slot by first empty; families L64-126)
    (3, [('DEF', 'DEF', 'DEF', 79), ('MDEF', 'MDEF', 'MDEF', 82)]),
    (2, [('MHP', 'MaxHP', 'MaxHP', 89), ('MSP', 'MaxSP', 'MaxSP', 92)]),
    (1, [('ATK', 'ATK', 'ATK', 99), ('MATK', 'MATK', 'MATK', 102)]),
    (0, [('STR', 'STR', 'STR', 109), ('INT', 'INT', 'INT', 112), ('AGI', 'AGI', 'AGI', 115),
         ('VIT', 'VIT', 'VIT', 118), ('DEX', 'DEX', 'DEX', 121), ('LUK', 'LUK', 'LUK', 124)]),
]
HERO_PICK = 135


def hero_ring(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, MORO))
    L = ['  - Family: HERO_RING',
         '    Display: { KR: "용사의 반지", EN: "Ring of Hero" }',
         '    Variants:']
    prev = None
    for card, fams in HERO_SLOTS:
        for key, kr, en, aline in fams:
            L += ['      - Key: HERO_RING_%s' % key,
                  '        Display: { KR: "%d번 슬롯 - %s", EN: "Slot %d - %s" }' % (4 - card, kr, 4 - card, en),
                  '        Source: { Script: %s, Npc: "인챈터 번즈", TargetLines: [ 31 ], TargetExact: true }' % MORO,
                  '        TargetItems: [ RingOfHero ]',
                  '        Order: [ %d ]' % card,
                  '        OrdinalOffset: %d' % (3 - card),
                  # L146-197: 3 x 6684, needs card[3]; the script hands out a new ring (V2 clears the enchants)
                  '        Reset: { Chance: 100000, Cost: { Materials: [ { Item: %s, Amount: 3 } ] } }' % items_by_id[6684].aegis,
                  '        Slots:']
            extra = None
            if prev:
                pool = []
                for _k, _kr, _en, pl in prev[1]:
                    pool += [v for v in parity.setarray_values(lines, pl)[1] if v not in pool]
                extra = ['Require: [ { Slot: %d, Enchants: [ %s ] } ]' % (prev[0], names(items_by_id, pool))]
            L += uniform_slot(items_by_id, lines, card, 'Array: %d, Pick: %d' % (aline, HERO_PICK),
                              parity.array_pick(lines, aline, HERO_PICK), extra=extra)
        prev = (card, fams)
    return L


# ----------------------------------------------------------------------------- RWC 2012 (cashmall.txt 골드버그#new10)

RWC_KINDS = [   # key, KR, EN, plain item, socketed item, safe families [(key, KR, EN, F_Rand line)], risky families
    ('RING', '반지', 'Ring', 'RWC_2012_Ring', 'RWC_2012_Ring_',
     [('FS', '투지', 'Fighting Spirit', 1829), ('ATK', '어택', 'Attack', 1832), ('MHP', 'MHP', 'MHP', 1835), ('HP', 'HP', 'HP', 1838)],
     [('STR', 'STR', 'STR', 1767), ('AGI', 'AGI', 'AGI', 1770), ('VIT', 'VIT', 'VIT', 1773), ('INT', 'INT', 'INT', 1776),
      ('DEX', 'DEX', 'DEX', 1779), ('LUK', 'LUK', 'LUK', 1782), ('SP', 'SP', 'SP', 1785)]),
    ('PENDANT', '펜던트', 'Pendant', 'RWC_2012_Pendant', 'RWC_2012_Pendant_',
     [('SPELL', '마력', 'Spell', 1844), ('MATK', '마공', 'Magic Attack', 1847), ('SP', 'SP', 'SP', 1850)],
     [('STR', 'STR', 'STR', 1791), ('AGI', 'AGI', 'AGI', 1794), ('VIT', 'VIT', 'VIT', 1797), ('INT', 'INT', 'INT', 1800),
      ('DEX', 'DEX', 'DEX', 1803), ('LUK', 'LUK', 'LUK', 1806), ('MHP', 'MHP', 'MHP', 1809), ('HP', 'HP', 'HP', 1812)]),
]
RWC_DESTROY = 1816   # `if (rand(4) < 1)` destroys, only on the card[1] / card[0] branch (L1754)


def rwc(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, CASHMALL))
    destroy = parity.inline_rand_cmp(lines, RWC_DESTROY)
    reset = '        Reset: { Chance: 100000, Cost: { Materials: [ { Item: %s, Amount: 1 } ] } }' % items_by_id[6665].aegis
    L = ['  - Family: RWC_2012',
         '    Display: { KR: "RWC 2012 반지/펜던트", EN: "RWC 2012 Ring / Pendant" }',
         '    Variants:']
    for kind, kkr, ken, plain, sock, safe, risky in RWC_KINDS:
        safe_pool = []
        for _k, _kr, _en, fl in safe:
            safe_pool += [v for v in parity.frand(lines, fl) if v not in safe_pool]
        for key, kr, en, fl in safe:
            L += ['      - Key: RWC_%s_%s' % (kind, key),
                  '        Display: { KR: "%s 1~2번 - %s", EN: "%s 1st-2nd - %s" }' % (kkr, kr, ken, en),
                  '        Source: { Script: %s, Npc: "골드버그#new10", TargetLines: [ 1724 ] }' % CASHMALL,
                  '        TargetItems: [ %s, %s ]' % (plain, sock),
                  '        Order: [ 3, 2 ]',
                  reset,
                  '        Slots:']
            for card in (3, 2):
                L += uniform_slot(items_by_id, lines, card, 'FRand: %d' % fl, parity.frand(lines, fl))
        # risky slots: plain kind card[1] then card[0]; socketed kind card[1] only (card[0] holds the real card)
        for suffix, skr, sen, target, order in (('', '', '', plain, (1, 0)), ('_SOCKET', ' 소켓', ' Socketed', sock, (1,))):
            for key, kr, en, fl in risky:
                L += ['      - Key: RWC_%s%s_%s' % (kind, suffix, key),
                      '        Display: { KR: "%s%s 위험 슬롯 - %s", EN: "%s%s risky slots - %s" }' % (kkr, skr, kr, ken, sen, en),
                      '        Source: { Script: %s, Npc: "골드버그#new10", TargetLines: [ 1724 ] }' % CASHMALL,
                      '        TargetItems: [ %s ]' % target,
                      '        Order: [ %s ]' % ', '.join(str(c) for c in order),
                      '        OrdinalOffset: 2',
                      reset,
                      '        Slots:']
                for card in order:
                    extra = ['Require: [ { Slot: 3, Enchants: [ %s ] }, { Slot: 2, Enchants: [ %s ] } ]' % (
                        names(items_by_id, safe_pool), names(items_by_id, safe_pool))]
                    L += uniform_slot(items_by_id, lines, card, 'FRand: %d, DestroyLine: %d' % (fl, RWC_DESTROY),
                                      parity.frand(lines, fl), extra=extra, destroy=destroy)
    return L


# ----------------------------------------------------------------------------- Fallen Angel Wings (cashmall.txt 다크 루히르)

FA_FAMILIES = [   # key, KR, EN (menu L503), first `== k` line for card[3] (A1), card[2] (A2), card[1] (A3)
    ('FS', '투지', 'Fighting Spirit', 510, 705, 914), ('SPELL', '마력', 'Spell', 523, 719, 928),
    ('ARCHER', '명궁', 'Archery', 536, 733, 942), ('FATAL', '치명', 'Critical', 549, 747, 956),
    ('MHP', 'MHP', 'MHP', 562, 761, 970), ('MSP', 'MSP', 'MSP', 575, 775, 984),
    ('ASPD', '공격속도', 'Attack Speed', 588, 789, 998), ('STR', 'STR', 'STR', 601, 803, 1012),
    ('AGI', 'AGI', 'AGI', 614, 817, 1026), ('VIT', 'VIT', 'VIT', 627, 831, 1040),
    ('INT', 'INT', 'INT', 640, 845, 1054), ('DEX', 'DEX', 'DEX', 653, 859, 1068), ('LUK', 'LUK', 'LUK', 666, 873, 1082),
]
FA_WINDOWS = {3: 492, 2: 690, 1: 897}
# Operator decision 2026-10-06 ("의도대로 모두 수정"): STR card[1] rolls 5-6 give S_Str (script: Luck4 4753),
# DEX Lv6 in card[2] may continue, card[1] charges the 100,000z the script only checks (L904).
FA_REPLACE = {('STR', 1): {4753: 'S_Str'}}
FA_REQUIRE_EXTRA = {('DEX', 1): ['Dexterity6']}


def fallen_angel(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, CASHMALL))
    L = ['  - Family: FALLEN_ANGEL_WING',
         '    Display: { KR: "타락천사의 날개", EN: "Fallen Angel Wing" }',
         '    Variants:']
    for key, kr, en, a1, a2, a3 in FA_FAMILIES:
        L += ['      - Key: FALLEN_ANGEL_%s' % key,
              '        Display: { KR: "%s", EN: "%s" }' % (kr, en),
              '        Source: { Script: %s, Npc: "다크 루히르", TargetLines: [ 467 ], TargetExact: true }' % CASHMALL,
              '        TargetItems: [ Fallen_Angel_Wing, K_Fallen_Angel_Wing ]',
              '        Order: [ 3, 2, 1 ]',
              '        Slots:']
        for card, first in ((3, a1), (2, a2), (1, a3)):
            lo, hi = parity.rand_window(lines, FA_WINDOWS[card])
            dist = parity.eq_table(lines, first, lo, hi)
            repl = FA_REPLACE.get((key, card), {})
            extra = []
            if card == 2:
                extra.append('MinimumRefine: 7')                      # L685
            if card == 1:
                extra.append('MinimumRefine: 9')                      # L892
            if card < 3:
                need = names(items_by_id, parity.case_labels(lines, first)).split(', ')
                need += [x for x in FA_REQUIRE_EXTRA.get((key, card), []) if x not in need]
                extra.append('Require: [ { Slot: %d, Enchants: [ %s ] } ]' % (card + 1, ', '.join(need)))
            ptext = 'WindowLine: %d, Eq: %d' % (FA_WINDOWS[card], first)
            if repl:
                ptext += ', Replace: { %s }, Decision: "operator 2026-10-06"' % ', '.join('%d: %s' % kv for kv in repl.items())
            pad = ' ' * 10
            S = ['%s- Slot: %d' % (pad, card)]
            if card == 1:
                S.append('%s  Cost: { Zeny: 100000 }' % pad)
            S += ['%s  %s' % (pad, x) for x in extra]
            S += ['%s  Parity: { %s }' % (pad, ptext), '%s  Options:' % pad]
            S += ['%s    - { Enchant: %s, Weight: %d }' % (pad, repl.get(v) or items_by_id[v].aegis, n) for v, n in dist.items()]
            L += S
    return L


# ----------------------------------------------------------------------------- Upg weapons (enchan_upg.txt 마성의강화사#prq)

UPG = 'npc/re/merchants/enchan_upg.txt'


def upg(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, UPG))
    targets = [int(x) for x in re.findall(r'\d+', parity._strip(lines[91]))]   # L92 "|1292|1394|...|"
    L = ['  - Family: UPG_WEAPON',
         '    Display: { KR: "강화 무기 (Upg)", EN: "Upg Weapons" }',
         '    Variants:']
    for key, kr, en, wline, chain in (('PHYSICAL', '물리 계열', 'Physical Series', 136, 137),
                                      ('MAGICAL', '마법 계열', 'Magical Series', 175, 176)):
        lo, hi = parity.rand_window(lines, wline)
        sl = chain_slot(items_by_id, UPG, chain, lo, hi, 3, {0: 'REFINE_DOWN'}, None)
        sl[1] = sl[1].replace('Parity: { Chain', 'Parity: { WindowLine: %d, Chain' % wline)
        # L225-229: the weapon stays, refine drops by rand(0, refine) = REFINE_DOWN Random
        sl = [x.replace('Result: REFINE_DOWN,', 'Result: REFINE_DOWN, Random: true,') for x in sl]
        L += ['      - Key: UPG_%s' % key,
              '        Display: { KR: "%s", EN: "%s" }' % (kr, en),
              '        Source: { Script: %s, Npc: "마성의강화사#prq", TargetLines: [ 92 ], TargetExact: true }' % UPG,
              '        TargetItems: [ %s ]' % names(items_by_id, targets),
              '        Order: [ 3 ]',
              '        Cost: { Materials: [ { Item: %s, Amount: 1 } ] }' % items_by_id[6484].aegis,   # L234, every outcome
              '        Reset: { Chance: 100000, Cost: { Zeny: 100000 } }',                              # L248-273
              '        Slots:'] + sl
    return L


# ----------------------------------------------------------------------------- Excellion (enchan_verus.txt MARS_01#pa0829)

EXCEL_TABLE = 1041   # setarray .@list: <blueprint>, <enchant>, <suit max>, <wing max>, <first slot only>


def excel_rows(lines):
    _v, vals = parity.setarray_values(lines, EXCEL_TABLE)
    return [vals[i:i + 5] for i in range(0, len(vals), 5)]


def excellion(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, VERUS))
    rows = excel_rows(lines)
    L = ['  - Family: EXCELION',
         '    Display: { KR: "엑셀리온", EN: "Excelion" }',
         '    Variants:']
    for key, kr, en, target, col in (('SUIT', '엑셀리온 슈트', 'Excelion Suit', 'Excelion_Suit', 2),
                                     ('WING', '엑셀리온 윙', 'Excelion Wing', 'Excelion_Wing', 3)):
        L += ['      - Key: EXCELION_%s' % key,
              '        Display: { KR: "%s", EN: "%s" }' % (kr, en),
              '        Source: { Script: %s, Npc: "MARS_01#pa0829", TargetLines: [ 1166, 1167 ], Access: "quest:12368:2" }' % VERUS,
              '        TargetItems: [ %s ]' % target,
              '        Order: [ 3, 2, 1 ]',                         # L1191-1202
              '        Caution:',
              '          KR: "설계도 1개로 원하는 효과를 확정 부여합니다\\n같은 효과는 장비별 최대 개수까지만 가능합니다\\n초기화 불가"',
              '          EN: "Each blueprint grants its effect for certain\\nEach effect has a per-item maximum\\nReset: not available"',
              '        Slots:']
        for card in (3, 2, 1):
            use = [r for r in rows if r[col] > 0 and (card == 3 or r[4] == 0)]   # L1137 / L1142
            L += ['          - Slot: %d' % card,
                  '            Parity: { Blueprints: %d, Column: %d, FirstSlot: %s }' % (EXCEL_TABLE, col, 'true' if card == 3 else 'false'),
                  '            Perfect:']
            L += ['              - { Enchant: %s, Cost: { Materials: [ { Item: %s, Amount: 1 } ] } }' % (
                items_by_id[r[1]].aegis, items_by_id[r[0]].aegis) for r in use]
            L += ['            MaxSame:']
            L += ['              - { Enchant: %s, Max: %d }' % (items_by_id[r[1]].aegis, r[col]) for r in rows if r[col] > 0]
    return L


# ----------------------------------------------------------------------------- Time boots (OldGlastHeim_merchants.txt)

OGH = 'npc/re/merchants/OldGlastHeim_merchants.txt'


def time_boots(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, OGH))
    coag, poll = items_by_id[6608].aegis, items_by_id[6755].aegis
    L = ['  - Family: TIME_BOOTS',
         '    Display: { KR: "시간의 부츠", EN: "Temporal Boots" }',
         '    Variants:']
    for key, kr, en, npc, tlines, ladder_lines, extra in (
            ('BASIC', '일반', 'Unsocketed', '휴긴의 마법사#pa0829', [70, 71, 72, 73, 74, 75, 77, 78, 79, 80, 81, 82], (63, 64, 65, 66), None),
            ('SOCKET', '소켓', 'Socketed', '어둠의 마법 전문가#pa082', list(range(305, 319)), (329, 330, 331, 332), True)):
        ladder = [parity.setarray_values(lines, ln)[1] for ln in ladder_lines]
        if extra:
            coag_cost = parity.setarray_values(lines, 335)[1]
            poll_cost = parity.setarray_values(lines, 336)[1]
            chances = parity.setarray_values(lines, 339)[1]

            def step_cost(n):
                return '{ Zeny: 100000, Materials: [ { Item: %s, Amount: %d }, { Item: %s, Amount: %d } ] }' % (
                    coag, coag_cost[n], poll, poll_cost[n])
            caution = ('"1단계 선택 100%, 강화(2~4단계) 70%\\n보너스(4단계 후) 70%\\n실패 시 장비 파괴 30% / 초기화 불가"',
                       '"Stage 1 pick 100%, upgrades (stage 2-4) 70%\\nBonus (after stage 4) 70%\\nOn failure: destroyed 30% / no reset"')
        else:
            costs = parity.setarray_values(lines, 67)[1]

            def step_cost(n):
                return '{ Materials: [ { Item: %s, Amount: %d } ] }' % (coag, costs[n])
            caution = ('"1단계 선택 후 같은 계열로 4단계까지 강화 (100%)\\n4단계 후 보너스 랜덤 1종 (100%)\\n초기화 불가"',
                       '"Pick a stage 1 type, upgrade it to stage 4 (100%)\\nThen a random bonus (100%)\\nReset: not available"')
        found = set()
        for ln in tlines:
            found |= set(int(x) for x in re.findall(r'case\s+(\d+)', parity._strip(lines[ln - 1])))
        L += ['      - Key: TIME_BOOTS_%s' % key,
              '        Display: { KR: "%s", EN: "%s" }' % (kr, en),
              '        Source: { Script: %s, Npc: "%s", TargetLines: [ %s ], TargetExact: true }' % (OGH, npc, ', '.join(str(x) for x in tlines)),
              '        TargetItems: [ %s ]' % names(items_by_id, sorted(found)),
              '        Order: [ 3, 2 ]',
              '        Caution: { KR: %s, EN: %s }' % caution,
              '        Slots:',
              '          - Slot: 3',
              '            Parity: { Ladder: [ %s ]%s }' % (', '.join(str(x) for x in ladder_lines),
                                                         ', Costs: [ 335, 336 ], Zeny: 100000, Chances: 339, ChanceLine: 432' if extra else ', Costs: [ 67 ]'),
              '            Perfect:']
        L += ['              - { Enchant: %s, Cost: %s }' % (items_by_id[e].aegis, step_cost(0)) for e in ladder[0]]
        L += ['            Upgrades:']
        for n in (1, 2, 3):
            for t in range(6):
                u = '              - { Enchant: %s, To: %s, Cost: %s' % (items_by_id[ladder[n - 1][t]].aegis, items_by_id[ladder[n][t]].aegis, step_cost(n))
                if extra:
                    u += ', SuccessWeight: %d, Failures: [ { Result: DESTROY, Weight: %d } ]' % (chances[n], 100 - chances[n])
                L.append(u + ' }')
        bline = 391 if extra else 128
        bonus = parity.frand(lines, bline)
        destroy = None
        ptext = 'FRand: %d' % bline
        if extra:
            destroy = (100 - chances[4], 100)
            ptext += ', Chances: 339, ChanceIndex: 4, ChanceLine: 432'
        L += uniform_slot(items_by_id, lines, 2, ptext, bonus, cost_line='Cost: %s' % step_cost(4),
                          extra=['Require: [ { Slot: 3, Enchants: [ %s ] } ]' % names(items_by_id, ladder[3])], destroy=destroy)
    return L


# ----------------------------------------------------------------------------- Malangdo weapons (enchan_mal.txt 마요마요#mal)

MAL = 'npc/re/merchants/enchan_mal.txt'
RE_MAL_CALLSUB = re.compile(r'case\s+(\d+)\s*:\s*callsub\s+L_Socket\s*,\s*(\d+)\s*,\s*(\d+)\s*;')
MAL_POOLS = [   # key, coin id, KR, EN, (rand line, first if line)   (L455-614)
    ('E', 6422, '', '', 457, 458), ('D', 6421, '', '', 473, 474), ('C', 6420, '', '', 489, 490), ('B', 6419, '', '', 505, 506),
    ('A_CASTER', 6418, '캐스팅', 'Caster', 521, 522), ('A_RANGED', 6418, '원거리', 'Ranged', 537, 538),
    ('A_MELEE', 6418, '근접', 'Melee', 553, 554), ('SEAGOD_CASTER', 6423, '캐스팅', 'Caster', 569, 570),
    ('SEAGOD_RANGED', 6423, '원거리', 'Ranged', 585, 586), ('SEAGOD_MELEE', 6423, '근접', 'Melee', 601, 602),
]
MAL_GROUPS = [(1, 2, 'X1'), (1, 3, 'X1_ONE'), (2, 2, 'X2'), (4, 2, 'X4')]   # (cost multiplier, enclimit, key)


def mal_callsubs(lines):
    return [(n, int(a), int(b), int(c)) for n, raw in enumerate(lines, 1)
            for a, b, c in [m.groups() for m in [RE_MAL_CALLSUB.search(parity._strip(raw))] if m]]


def malangdo(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, MAL))
    subs = mal_callsubs(lines)
    _v, coins = parity.setarray_values(lines, 379)
    _v, base = parity.setarray_values(lines, 380)
    unit = dict(zip(coins, base))
    L = ['  - Family: MALANGDO_WEAPON',
         '    Display: { KR: "말랑도 무기", EN: "Malangdo Weapons" }',
         '    Variants:']
    for mult, limit, gkey in MAL_GROUPS:
        targets = [iid for _n, iid, m, lim in subs if m == mult and lim == limit]
        order = [3] if limit == 3 else [3, 2]           # L436-446: enclimit 2 -> card[3], card[2]; 3 -> card[3]
        for pkey, coin, tkr, ten, rline, chain in MAL_POOLS:
            cname = items_by_id[coin]
            lo, hi = parity.rand_window(lines, rline)
            L += ['      - Key: MALANGDO_%s_%s' % (gkey, pkey),
                  '        Display: { KR: "%s%s (x%d%s)", EN: "%s%s (x%d%s)" }' % (
                      cname.name_kr, (' - ' + tkr) if tkr else '', mult, ', 1슬롯' if limit == 3 else '',
                      cname.name_en, (' - ' + ten) if ten else '', mult, ', 1 slot' if limit == 3 else ''),
                  '        Source: { Script: %s, Npc: "마요마요#mal", CallsubArgs: [ %d, %d ] }' % (MAL, mult, limit),
                  '        TargetItems: [ %s ]' % names(items_by_id, targets),
                  '        Order: [ %s ]' % ', '.join(str(c) for c in order),
                  '        Cost: { Materials: [ { Item: %s, Amount: %d } ] }' % (cname.aegis, unit[coin] * mult),   # L386 / L651
                  # L658-699: 1 Nyangvine_Fruit, needs card[3], clears every enchant
                  '        Reset: { Chance: 100000, Cost: { Materials: [ { Item: %s, Amount: 1 } ] } }' % items_by_id[6909].aegis,
                  '        Slots:']
            for card in order:
                sl = chain_slot(items_by_id, MAL, chain, lo, hi, card, {}, None)
                sl[1] = sl[1].replace('Parity: { Chain', 'Parity: { WindowLine: %d, Chain' % rline)
                L += sl
    return L


# ----------------------------------------------------------------------------- Artisan Tene (enchan_ko.txt 장인 테네#ko)

KO = 'npc/re/merchants/enchan_ko.txt'
KO_BANDS = [   # key, min, max (L296-300), card[3] first if, card[2] first if
    ('R0_4', 0, 4, 343, 430), ('R5_7', 5, 7, 354, 441), ('R8_9', 8, 9, 371, 458),
    ('R10_12', 10, 12, 386, 473), ('R13', 13, 0, 406, 493),
]


def tene(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, KO))
    found = []
    for ln in (94, 124):
        found += [int(x) for x in re.findall(r'getitemname\((\d+)\)', parity._strip(lines[ln - 1]))]
    L = ['  - Family: TENE',
         '    Display: { KR: "장인 테네 (카게로우/오보로)", EN: "Artisan Tene (Kagerou/Oboro)" }',
         '    Variants:']
    for key, lo_r, hi_r, c3, c2 in KO_BANDS:
        band = ('%d~%d' % (lo_r, hi_r)) if hi_r else ('%d+' % lo_r)
        L += ['      - Key: TENE_%s' % key,
              '        Display: { KR: "제련 %s - 전체 초기화", EN: "Refine %s - full reset" }' % (band, band),
              '        Source: { Script: %s, Npc: "장인 테네#ko", TargetLines: [ 94, 124 ], TargetExact: true }' % KO,
              '        TargetItems: [ %s ]' % names(items_by_id, found),
              '        MinimumRefine: %d' % lo_r]
        if hi_r:
            L.append('        MaximumRefine: %d' % hi_r)
        L += ['        Order: [ 3, 2 ]',
              '        Cost: { Zeny: 100000 }',                                   # L244 / L524
              '        Reset: { Chance: 100000, Cost: { Zeny: 100000 } }',         # K2b L271-280: both slots
              '        Slots:']
        for card, rline, chain in ((3, 340, c3), (2, 427, c2)):
            lo, hi = parity.rand_window(lines, rline)
            sl = chain_slot(items_by_id, KO, chain, lo, hi, card, {}, None)
            sl[1] = sl[1].replace('Parity: { Chain', 'Parity: { WindowLine: %d, Chain' % rline)
            L += sl
    return L


# ----------------------------------------------------------------------------- Phase 2.4: hidden armor (enchan_arm.txt 수습 세공사)

ARM = 'npc/merchants/enchan_arm.txt'
ARM_GROUPS = [   # key, KR, EN, setarray line, `set .@j,N` window line (L38-50)
    ('NONSLOT', '슬롯 없는 갑옷', 'Non-slotted Armor', 40, 41),
    ('SLOT', '슬롯 갑옷', 'Slotted Armor', 44, 45),
    ('HIGH', '고급 갑옷', 'High Grade Armor', 49, 50),
]
ARM_SWITCH = 103   # switch (rand(1, .@failrate)) { case N: set .@addpart, ID; ... default: destroyed }


def hidden_armor(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, ARM))
    L = ['  - Family: HIDDEN_ARMOR',
         '    Display: { KR: "히든 갑옷 인챈트", EN: "Hidden Armor Enchant" }',
         '    Variants:']
    for key, kr, en, aline, wline in ARM_GROUPS:
        _v, targets = parity.setarray_values(lines, aline)
        hi = parity._int_assign(lines, wline, '.@j')
        dist = parity.switch_table(lines, ARM_SWITCH, 1, hi)
        pad = ' ' * 10
        L += ['      - Key: HIDDEN_ARMOR_%s' % key,
              '        Display: { KR: "%s", EN: "%s" }' % (kr, en),
              '        Source: { Script: %s, Npc: "수습 세공사", TargetLines: [ %d ], TargetExact: true }' % (ARM, aline),
              '        TargetItems: [ %s ]' % names(items_by_id, targets),
              '        Order: [ 3 ]',
              '        Cost: { Zeny: 400000 }',                  # L101 (the script also takes the armor and hands out a new one)
              # operator decision 2026-10-07: the script re-enchanted an enchanted armor (overwrite); V2 only fills
              # empty slots, so a free 100% reset replaces it (not in the script)
              '        Reset: { Chance: 100000 }',
              '        Slots:',
              '%s- Slot: 3' % pad,
              # operator decision 2026-10-06: keep the current behaviour - a success hands back refine 0 / no cards (L147)
              '%s  SuccessReset: true' % pad,
              '%s  Parity: { Switch: %d, Window: [ 1, %d ], WindowVarLine: %d }' % (pad, ARM_SWITCH, hi, wline),
              '%s  Options:' % pad]
        L += ['%s    - { Enchant: %s, Weight: %d }' % (pad, items_by_id[v].aegis, n) for v, n in dist.items() if v != 'DEFAULT']
        L += ['%s  Failures:' % pad, '%s    - { Result: DESTROY, Weight: %d }' % (pad, dist['DEFAULT'])]
    return L


# ----------------------------------------------------------------------------- Phase 2.4: Bio4 sorcerer (bio4_reward.txt 소서러#Bio4Reward)

BIO4 = 'npc/re/merchants/bio4_reward.txt'
BIO4_GROUPS = [   # key, KR, EN, chain first if (L640/676/712/748), window var line, target lines, exact
    ('MELEE', '근거리', 'Melee', 640, 468, [469, 470, 471], False, [13069, 1291, 1392, 1393, 1435, 13070, 16017]),
    ('MELEE_LANCE', '근거리 (자이언트 랜스)', 'Melee (Giant Lance)', 640, 474, [473], True, [1490]),
    ('RANGED', '원거리', 'Ranged', 676, 468, [475], True, [18109, 18110, 18111]),
    ('MAGIC', '마법', 'Magic', 712, 468, [477], True, [1584, 1659]),
    ('ARMOR', '방어구', 'Armor', 748, 468, [479, 480, 481], False, [2160, 2161, 2162, 2892, 15044]),
    ('ARMOR_WIDE', '방어구 (구조 망토 / 고대 금장식)', 'Armor (Salvage Cape / Ancient Gold Deco)', 748, 484, [483], True, [2582, 18570]),
]
BIO4_MATERIALS = [   # key, KR, EN, card, material id (L518-548; write L798 card[socket_type-1])
    ('WILL', '전사의 의지', 'Will of Warrior', 3, 6469),
    ('BLOOD', '피의 갈망', 'Thirst for Blood', 2, 6470),
]


def bio4(items_by_id):
    lines = parity.read_lines(os.path.join(REPO, BIO4))
    L = ['  - Family: BIO4_SORCERER',
         '    Display: { KR: "생체 연구소 4층 장비 (소서러)", EN: "Bio Lab 4F Gear (Sorcerer)" }',
         '    Variants:']
    for gkey, gkr, gen, chain, wline, tlines, exact, targets in BIO4_GROUPS:
        hi = parity._int_assign(lines, wline, '.@lhz_max_num')
        for mkey, mkr, men, card, mat in BIO4_MATERIALS:
            sl = chain_slot(items_by_id, BIO4, chain, 1, hi, card, {0: 'DESTROY'}, None)
            sl[1] = sl[1].replace('Parity: { Chain', 'Parity: { WindowVarLine: %d, Chain' % wline)
            L += ['      - Key: BIO4_%s_%s' % (gkey, mkey),
                  '        Display: { KR: "%s - %s", EN: "%s - %s" }' % (gkr, mkr, gen, men),
                  '        Source: { Script: %s, Npc: "소서러#Bio4Reward", TargetLines: [ %s ]%s }' % (
                      BIO4, ', '.join(str(x) for x in tlines), ', TargetExact: true' if exact else ''),
                  '        TargetItems: [ %s ]' % names(items_by_id, targets),
                  '        Order: [ %d ]' % card,
                  '        OrdinalOffset: %d' % (3 - card),
                  '        Cost: { Materials: [ { Item: %s, Amount: 10 } ] }' % items_by_id[mat].aegis,     # L790/792
                  # L575-607: 10 Goast_Chill clears only this material's slot (refused while it is empty)
                  '        Reset: { Chance: 100000, Scope: [ %d ], Cost: { Materials: [ { Item: %s, Amount: 10 } ] } }' % (
                      card, items_by_id[6471].aegis),
                  '        Slots:'] + sl
    return L


# ----------------------------------------------------------------------------- Phase 2.4: Tene slot-3-only reset (K2a)

def tene_slot3(items_by_id):
    """Same enchant groups as tene(), reset limited to card[2] (L264-269: refused unless card[2] is an enchant)."""
    L = tene(items_by_id)[3:]
    out = []
    for x in L:
        if x.startswith('      - Key: TENE_'):
            x += '_S3'
        x = x.replace(' - 전체 초기화"', ' - 3번째 칸만 초기화"').replace(' - full reset"', ' - 3rd slot reset only"')
        x = x.replace('        Reset: { Chance: 100000, Cost: { Zeny: 100000 } }',
                      '        Reset: { Chance: 100000, Scope: [ 2 ], Cost: { Zeny: 100000 } }')
        out.append(x)
    return out


# ----------------------------------------------------------------------------- Phase 2.4: old helmet special upgrade (C2)

OLD_HELM_SPECIAL_BASES = [29061, 29071, 29081, 29091, 29101, 29111]   # L469-470: Mettle, MagicEssence, Acute, MasterArcher, Adamantine, Affection


def old_helm_upgrades(items_by_id, lines, indent=12):
    _v, req = parity.setarray_values(lines, 558)      # setarray .@req_table[1], ... (index = current level)
    _v, rate = parity.setarray_values(lines, 559)
    hits_per_level = [sum(1 for r in range(100) if rt > r) for rt in rate]   # L684 `.@enchant_rate > rand(100)`
    pad = ' ' * indent
    L = ['%sUpgradeParity: { Bases: [ %s ], Costs: 558, Rates: 559, RollLine: 684 }' % (pad, ', '.join(str(b) for b in OLD_HELM_SPECIAL_BASES)),
         '%sUpgrades:' % pad]
    for base in OLD_HELM_SPECIAL_BASES:
        for lv in range(1, 10):
            ok = hits_per_level[lv - 1]
            fail = ('{ Result: FAIL_KEEP, Weight: %d }' % (100 - ok)) if lv == 1 else (
                '{ Result: DOWNGRADE, To: %s, Weight: %d }' % (items_by_id[base + lv - 2].aegis, 100 - ok))
            L.append('%s  - { Enchant: %s, To: %s, Cost: { Materials: [ { Item: %s, Amount: %d } ] }, SuccessWeight: %d, Failures: [ %s ] }' % (
                pad, items_by_id[base + lv - 1].aegis, items_by_id[base + lv].aegis, items_by_id[23016].aegis, req[lv - 1], ok, fail))
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
    L += horror(items_by_id)
    L += infinite(items_by_id)
    L += old_helm(items_by_id)
    L += brosnan(items_by_id)
    L += hero_ring(items_by_id)
    L += rwc(items_by_id)
    L += fallen_angel(items_by_id)
    L += upg(items_by_id)
    L += excellion(items_by_id)
    L += time_boots(items_by_id)
    L += malangdo(items_by_id)
    L += tene(items_by_id)
    L += hidden_armor(items_by_id)
    L += bio4(items_by_id)
    L += tene_slot3(items_by_id)
    io.open(args.out, 'w', encoding='utf-8', newline='\n').write('\n'.join(L) + '\n')
    print('wrote', args.out)


if __name__ == '__main__':
    main()

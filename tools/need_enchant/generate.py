# -*- coding: utf-8 -*-
"""NEED Unified Enchant V2 generator.

    python tools/need_enchant/generate.py --master db/need/enchant_master.yml --out <dir> [--commit-registry]

Reads the master (one source of truth) and writes, under <dir>:
  server/item_enchant.yml          NEED groups in stock rAthena format    (install: db/import/item_enchant.yml)
  server/enchant_rules.yml         NEED rules (failures, conditions)     (install: db/need/enchant_rules.yml)
  client_kr/data/luafiles514/lua files/enchant/enchantlist.lub   official KR + NEED groups (Lua text)
  client_en/data/luafiles514/lua files/enchant/enchantlist.lub   EN base + NEED groups (Lua text)
  client_common/data/luafiles514/lua files/itemdbnametbl.lub     official keys + missing keys (shared KR/EN)
  variants.tsv                     Family/variant/ClientId list for the unified NPC
  report.md                        what was generated and every gate result

Gates (any failure -> exit 1, nothing is installed by this tool):
  master validation, ClientId registry stability, option weight limits,
  decompile equivalence of the official KR/EN parts, client CheckFile/LoadAllData replay (KR and EN)
  with zero MessageBox.
"""
import argparse
import hashlib
import io
import json
import os
import re
import sys
from collections import OrderedDict

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import nelib            # noqa: E402
import clientcheck      # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
CHANCE_BASE = 100000
OPTION_WEIGHT_MAX = 100000   # item_enchant.yml Items[].Chance is parsed with a 100000 cap
CLIENT_ID_MIN = 10001
GRADE_BONUS_GRADES = (1, 2, 3)   # official tables declare these (all 0)
LUA_DIR = r'data\luafiles514\lua files'
EN_REPO = r'E:\tools\Need\NEEDResoruce_EN'
EN_BASE = os.path.join(EN_REPO, 'en_build', 'enchant_v2', 'base', 'enchantlist_phase418.lub')
EN_BASE_STAMP = os.path.join(EN_REPO, 'en_build', 'equipment_attribute', 'phase418_equipment_attribute_english.stamp.json')
RESULTS = ('FAIL_KEEP', 'CLEAR_SLOTS', 'CLEAR_ENCHANTS', 'REFINE_DOWN', 'DESTROY', 'REWARD', 'DESTROY_WITH_REWARD')
RESET_RESULTS = ('SUCCESS', 'FAIL_KEEP', 'DESTROY', 'REWARD', 'DESTROY_WITH_REWARD')
UPGRADE_RESULTS = RESULTS + ('DOWNGRADE',)


class GenError(Exception):
    pass


# ----------------------------------------------------------------------------- item database

class Item(object):
    __slots__ = ('id', 'aegis', 'name_kr', 'name_en', 'slots', 'type', 'subtype')


def load_items(repo):
    items_by_aegis, items_by_id = {}, {}
    for rel in ('db/re/item_db_equip.yml', 'db/re/item_db_etc.yml', 'db/re/item_db_usable.yml', 'db/import/item_db.yml'):
        raw = io.open(os.path.join(repo, rel), 'rb').read()
        try:
            text = raw.decode('utf-8')
        except UnicodeDecodeError:
            text = raw.decode('cp949', 'replace')
        for m in re.finditer(r'^  - Id: (\d+)\s*\n((?:    .*\n)+)', text, re.M):
            body = m.group(2)

            def field(name, default=None):
                f = re.search(r'^    %s: (.*)$' % name, body, re.M)
                return f.group(1).strip() if f else default

            it = Item()
            it.id = int(m.group(1))
            it.aegis = field('AegisName')
            it.name_kr = field('Name', it.aegis)
            it.name_en = None
            it.slots = int(field('Slots', '0'))
            it.type = field('Type', 'Etc')
            it.subtype = field('SubType', '')
            items_by_aegis[it.aegis] = it
            items_by_id[it.id] = it
    en = yaml.safe_load(io.open(os.path.join(repo, 'db/need/item_name_en.yml'), encoding='utf-8')) or {}
    for e in en.get('Body') or []:
        it = items_by_id.get(e.get('Id'))
        if it is not None and e.get('NameEN'):
            it.name_en = str(e['NameEN'])
    return items_by_aegis, items_by_id


# ----------------------------------------------------------------------------- master

def cost(node, items, where):
    node = node or {}
    zeny = int(node.get('Zeny', 0))
    mats = OrderedDict()
    for m in node.get('Materials') or []:
        name = m['Item']
        if name not in items:
            raise GenError('%s: unknown material %s' % (where, name))
        mats[name] = int(m.get('Amount', 1))
        if not 1 <= mats[name] <= 30000:
            raise GenError('%s: material amount out of range' % where)
    if zeny < 0:
        raise GenError('%s: negative zeny' % where)
    return {'zeny': zeny, 'mats': mats}


def check_outcomes(lst, allowed, items, where):
    out = []
    for o in lst or []:
        r = o.get('Result')
        if r not in allowed:
            raise GenError('%s: result %r not allowed here' % (where, r))
        w = int(o.get('Weight', 0))
        if w <= 0 or w > 0xFFFFFFFF:
            raise GenError('%s: weight must be 1..4294967295' % where)
        for rw in o.get('Rewards') or []:
            if rw['Item'] not in items:
                raise GenError('%s: unknown reward %s' % (where, rw['Item']))
        if r in ('REWARD', 'DESTROY_WITH_REWARD') and not o.get('Rewards'):
            raise GenError('%s: %s needs Rewards' % (where, r))
        if r == 'DOWNGRADE' and o.get('To') not in items:
            raise GenError('%s: DOWNGRADE needs a known To item' % where)
        out.append(o)
    return out


def load_master(path, items):
    doc = yaml.safe_load(io.open(path, encoding='utf-8'))
    hdr = doc.get('Header') or {}
    if hdr.get('Type') != 'NEED_ENCHANT_MASTER' or hdr.get('Version') != 1:
        raise GenError('master header must be Type NEED_ENCHANT_MASTER, Version 1')
    variants, families, keys = [], [], set()
    for fam in doc.get('Body') or []:
        fkey = fam['Family']
        fam_display = fam.get('Display') or {}
        families.append(fkey)
        for v in fam.get('Variants') or []:
            key = v['Key']
            where = '%s/%s' % (fkey, key)
            if key in keys:
                raise GenError('duplicate variant key %s' % key)
            keys.add(key)
            if not re.match(r'^[A-Z0-9_]+$', key):
                raise GenError('%s: key must be [A-Z0-9_]' % where)
            targets = v.get('TargetItems') or []
            if not targets:
                raise GenError('%s: TargetItems empty' % where)
            for t in targets:
                if t not in items:
                    raise GenError('%s: unknown target %s' % (where, t))
            order = [int(x) for x in v.get('Order') or []]
            if not order or len(set(order)) != len(order) or any(not 0 <= x <= 3 for x in order):
                raise GenError('%s: Order must be distinct card indices 0..3' % where)
            min_slots_ok = min(order) >= max(items[t].slots for t in targets)
            if not min_slots_ok:
                raise GenError('%s: an Order slot overlaps a target item real card slot' % where)
            default_cost = v.get('Cost')
            slots = OrderedDict()
            for s in v.get('Slots') or []:
                sl = int(s['Slot'])
                sw = '%s slot %d' % (where, sl)
                if sl not in order:
                    raise GenError('%s: slot not in Order' % sw)
                if sl in slots:
                    raise GenError('%s: duplicate slot' % sw)
                opts = OrderedDict()
                for o in s.get('Options') or []:
                    if o['Enchant'] not in items:
                        raise GenError('%s: unknown enchant %s' % (sw, o['Enchant']))
                    w = int(o['Weight'])
                    if not 1 <= w <= OPTION_WEIGHT_MAX:
                        raise GenError('%s: option weight must be 1..%d (%s=%d)' % (sw, OPTION_WEIGHT_MAX, o['Enchant'], w))
                    if o['Enchant'] in opts:
                        raise GenError('%s: duplicate option %s (merge the weights)' % (sw, o['Enchant']))
                    opts[o['Enchant']] = w
                perfect = OrderedDict()
                for p in s.get('Perfect') or []:
                    if p['Enchant'] not in items:
                        raise GenError('%s: unknown perfect %s' % (sw, p['Enchant']))
                    perfect[p['Enchant']] = cost(p.get('Cost'), items, sw)
                upgrades = OrderedDict()
                for u in s.get('Upgrades') or []:
                    if u['Enchant'] not in items or u['To'] not in items:
                        raise GenError('%s: unknown upgrade item' % sw)
                    upgrades[u['Enchant']] = {
                        'to': u['To'], 'cost': cost(u.get('Cost'), items, sw),
                        'success': int(u.get('SuccessWeight', 0)),
                        'failures': check_outcomes(u.get('Failures'), UPGRADE_RESULTS, items, sw)}
                    if upgrades[u['Enchant']]['failures'] and upgrades[u['Enchant']]['success'] <= 0:
                        raise GenError('%s: upgrade with Failures needs SuccessWeight' % sw)
                failures = check_outcomes(s.get('Failures'), RESULTS, items, sw)
                chance = int(s.get('SuccessChance', CHANCE_BASE))
                if failures and chance != CHANCE_BASE:
                    raise GenError('%s: use either Failures or SuccessChance, not both' % sw)
                if not opts and not perfect:
                    raise GenError('%s: slot has neither Options nor Perfect' % sw)
                slots[sl] = {
                    'cost': cost(s.get('Cost', default_cost), items, sw), 'options': opts, 'failures': failures,
                    'chance': chance, 'perfect': perfect, 'upgrades': upgrades,
                    'min_refine': int(s.get('MinimumRefine', 0)), 'require': s.get('Require') or [],
                    'max_same': s.get('MaxSame') or []}
            missing = [x for x in order if x not in slots]
            if missing:
                raise GenError('%s: Order slots without definition: %s' % (where, missing))
            reset = v.get('Reset')
            if reset is not None:
                outcomes = check_outcomes(reset.get('Outcomes'), RESET_RESULTS, items, where + ' reset')
                reset = {'cost': cost(reset.get('Cost'), items, where + ' reset'),
                         'chance': CHANCE_BASE if outcomes else int(reset.get('Chance', CHANCE_BASE)),
                         'outcomes': outcomes, 'require_all': bool(reset.get('RequireAllFilled', False)),
                         'scope': [int(x) for x in reset.get('Scope') or []]}
                if not 0 < reset['chance'] <= CHANCE_BASE:
                    raise GenError('%s: reset Chance must be 1..%d' % (where, CHANCE_BASE))
            caution = v.get('Caution', 'AUTO')
            variants.append({
                'family': fkey, 'family_display': fam_display, 'key': key, 'display': v.get('Display') or {},
                'targets': targets, 'min_refine': int(v.get('MinimumRefine', 0)), 'max_refine': int(v.get('MaximumRefine', 0)),
                'allow_random': bool(v.get('AllowRandomOptions', True)), 'order': order, 'slots': slots,
                'reset': reset, 'caution': caution, 'source': v.get('Source')})
    return families, variants


# ----------------------------------------------------------------------------- registry

def load_registry(path):
    if not os.path.isfile(path):
        return OrderedDict(), []
    doc = yaml.safe_load(io.open(path, encoding='utf-8')) or {}
    ids = OrderedDict((str(k), int(v)) for k, v in (doc.get('Ids') or {}).items())
    retired = [int(x) for x in doc.get('Retired') or []]
    return ids, retired


def assign_ids(variants, registry, retired, forbidden):
    used = set(registry.values()) | set(retired)
    if len(used) != len(registry) + len(retired):
        raise GenError('registry has duplicate ids')
    for k, v in registry.items():
        if v < CLIENT_ID_MIN:
            raise GenError('registry id %d for %s is below %d' % (v, k, CLIENT_ID_MIN))
        if v in forbidden:
            raise GenError('registry id %d for %s collides with an official group' % (v, k))
    nxt = max([CLIENT_ID_MIN - 1] + list(used)) + 1
    new = []
    for v in variants:
        if v['key'] not in registry:
            while nxt in forbidden or nxt in used:
                nxt += 1
            registry[v['key']] = nxt
            used.add(nxt)
            new.append(v['key'])
        v['id'] = registry[v['key']]
    active = {v['key'] for v in variants}
    for k in list(registry):
        if k not in active:
            raise GenError('registry key %s has no variant any more: move its id to Retired' % k)
    return new


def write_registry(path, registry, retired):
    lines = ['# NEED Unified Enchant V2 ClientId registry - APPEND ONLY.',
             '# A variant key keeps its id forever; a removed variant moves its id to Retired (never reused).',
             'Header:', '  Type: NEED_ENCHANT_CLIENTID_REGISTRY', '  Version: 1', 'Ids:']
    lines += ['  %s: %d' % (k, v) for k, v in registry.items()]
    lines.append('Retired: [%s]' % ', '.join(str(x) for x in retired))
    io.open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')


# ----------------------------------------------------------------------------- numbers

def normalize(weights, total=CHANCE_BASE):
    """Largest remainder: integer shares summing exactly to total, every positive weight >= 1."""
    s = sum(weights)
    raw = [w * total / s for w in weights]
    out = [int(x) for x in raw]
    rest = total - sum(out)
    for i in sorted(range(len(raw)), key=lambda i: raw[i] - out[i], reverse=True)[:rest]:
        out[i] += 1
    for i, w in enumerate(out):
        if w == 0 and weights[i] > 0:
            j = max(range(len(out)), key=lambda j: out[j])
            out[j] -= 1
            out[i] = 1
    return out


def pct(n, d):
    v = 100.0 * n / d
    s = ('%.4f' % v).rstrip('0').rstrip('.')
    return s + '%'


# ----------------------------------------------------------------------------- caution text

ORD_KR = {1: '1번째', 2: '2번째', 3: '3번째', 4: '4번째'}
ORD_EN = {1: '1st', 2: '2nd', 3: '3rd', 4: '4th'}
RESULT_KR = {'FAIL_KEEP': '유지', 'CLEAR_SLOTS': '일부 소멸', 'CLEAR_ENCHANTS': '인챈트 소멸',
             'REFINE_DOWN': '제련 하락', 'DESTROY': '파괴', 'REWARD': '보상',
             'DESTROY_WITH_REWARD': '파괴(보상)', 'DOWNGRADE': '하락'}
RESULT_EN = {'FAIL_KEEP': 'kept', 'CLEAR_SLOTS': 'some lost', 'CLEAR_ENCHANTS': 'all lost',
             'REFINE_DOWN': 'refine down', 'DESTROY': 'destroyed', 'REWARD': 'reward',
             'DESTROY_WITH_REWARD': 'destroyed (reward)', 'DOWNGRADE': 'downgraded'}
# The client's caution box shows 3 lines; the official data never uses more, nor more than 93 bytes a line
CAUTION_MAX_LINES = 3
CAUTION_MAX_BYTES = 93
NEWLINE = chr(10)


def pct_short(n, d):
    """Up to 2 decimals; more only when 2 decimals would round a non-exact value to 0% or 100%."""
    v = 100.0 * n / d
    for digits in (2, 3, 4):
        s = ('%.*f' % (digits, v)).rstrip('0').rstrip('.')
        if (s not in ('0', '100')) or n in (0, d):
            return s + '%'
    return s + '%'


def caution_text(v, items, lang):
    kr = lang == 'KR'
    if v['caution'] != 'AUTO':
        text = (v['caution'] or {}).get(lang)
        if not text:
            raise GenError('%s: Caution.%s missing' % (v['key'], lang))
    else:
        lines = []
        cond = ''
        if v['min_refine'] and v['max_refine']:
            cond = ('[%d~%d제련] ' if kr else '[Refine +%d to +%d] ') % (v['min_refine'], v['max_refine'])
        elif v['min_refine']:
            cond = ('[%d제련 이상] ' if kr else '[Refine +%d or higher] ') % v['min_refine']
        elif v['max_refine']:
            cond = ('[%d제련 이하] ' if kr else '[Refine +%d or lower] ') % v['max_refine']
        rates, fails = [], []
        for n, sl in enumerate(v['order'], 1):
            s = v['slots'][sl]
            if not s['options']:
                continue
            ordn = ORD_KR[n] if kr else ORD_EN[n]
            opt = sum(s['options'].values())
            if s['failures']:
                total = opt + sum(int(f['Weight']) for f in s['failures'])
                rates.append('%s %s' % (ordn, pct_short(opt, total)))
                labels = RESULT_KR if kr else RESULT_EN
                fails.append('%s %s' % (ordn, ('·' if kr else ', ').join(
                    '%s %s' % (labels[f['Result']], pct_short(int(f['Weight']), total)) for f in s['failures'])))
            else:
                rates.append('%s %s' % (ordn, pct_short(s['chance'], CHANCE_BASE)))
                if s['chance'] < CHANCE_BASE:
                    fails.append('%s %s %s' % (ordn, '유지' if kr else 'kept', pct_short(CHANCE_BASE - s['chance'], CHANCE_BASE)))
        if rates:
            lines.append(cond + ('성공률: ' if kr else 'Success: ') + ' / '.join(rates))
        elif cond:
            lines.append(cond.strip())
        if fails:
            lines.append(('실패 시: ' if kr else 'On failure: ') + ' / '.join(fails))
        r = v['reset']
        if r is None:
            lines.append('초기화 불가' if kr else 'Reset: not available')
        else:
            head = ('초기화(전 슬롯 인챈트 시): ' if kr else 'Reset (all slots filled): ') if r['require_all'] else ('초기화: ' if kr else 'Reset: ')
            if r['outcomes']:
                total = sum(int(o['Weight']) for o in r['outcomes'])
                labels = RESULT_KR if kr else RESULT_EN
                parts = ['%s %s' % (('성공' if kr else 'success') if o['Result'] == 'SUCCESS' else labels[o['Result']],
                                    pct_short(int(o['Weight']), total)) for o in r['outcomes']]
                lines.append(head + ', '.join(parts))
            else:
                lines.append(head + pct_short(r['chance'], CHANCE_BASE))
        text = NEWLINE.join(lines)
    if lang == 'EN' and any(ord(ch) > 127 for ch in text):
        raise GenError('%s: EN caution is not ASCII: %r' % (v['key'], text))
    lines = text.split(NEWLINE)
    if len(lines) > CAUTION_MAX_LINES:
        raise GenError('%s: %s caution has %d lines (client shows %d)' % (v['key'], lang, len(lines), CAUTION_MAX_LINES))
    for line in lines:
        width = len(line.encode('cp949'))
        if width > CAUTION_MAX_BYTES:
            raise GenError('%s: %s caution line is %d bytes (max %d): %s' % (v['key'], lang, width, CAUTION_MAX_BYTES, line))
    return text


# ----------------------------------------------------------------------------- outputs

def yml_cost(lines, indent, c, price_key='Price'):
    pad = ' ' * indent
    if c['zeny']:
        lines.append('%s%s: %d' % (pad, price_key, c['zeny']))
    if c['mats']:
        lines.append('%sMaterials:' % pad)
        for name, n in c['mats'].items():
            lines.append('%s  - Material: %s' % (pad, name))
            lines.append('%s    Amount: %d' % (pad, n))


def server_item_enchant(variants):
    L = ['# GENERATED by tools/need_enchant/generate.py from db/need/enchant_master.yml - do not edit.',
         'Header:', '  Type: ITEM_ENCHANT_DB', '  Version: 1', '', 'Body:']
    for v in variants:
        L.append('  - Id: %d' % v['id'])
        L.append('    TargetItems:')
        L += ['      %s: true' % t for t in v['targets']]
        if v['min_refine']:
            L.append('    MinimumRefine: %d' % v['min_refine'])
        if not v['allow_random']:
            L.append('    AllowRandomOptions: false')
        r = v['reset']
        if r is not None:
            L.append('    Reset:')
            L.append('      Chance: %d' % r['chance'])
            yml_cost(L, 6, r['cost'])
        L.append('    Order:')
        L += ['      - Slot: %d' % s for s in v['order']]
        L.append('    Slots:')
        for sl in v['order']:
            s = v['slots'][sl]
            L.append('      - Slot: %d' % sl)
            if s['options']:
                yml_cost(L, 8, s['cost'])
                L.append('        Chance: %d' % (CHANCE_BASE if s['failures'] else s['chance']))
                L.append('        Enchants:')
                L.append('          - Enchantgrade: 0')
                L.append('            Items:')
                for name, w in s['options'].items():
                    L.append('              - Item: %s' % name)
                    L.append('                Chance: %d' % w)
            if s['perfect']:
                L.append('        PerfectEnchants:')
                for name, c in s['perfect'].items():
                    L.append('          - Item: %s' % name)
                    yml_cost(L, 12, c)
            if s['upgrades']:
                L.append('        Upgrades:')
                for name, u in s['upgrades'].items():
                    L.append('          - Enchant: %s' % name)
                    L.append('            Upgrade: %s' % u['to'])
                    yml_cost(L, 12, u['cost'])
    return '\n'.join(L) + '\n'


def yml_outcomes(L, indent, key, outcomes):
    pad = ' ' * indent
    L.append('%s%s:' % (pad, key))
    for o in outcomes:
        L.append('%s  - Result: %s' % (pad, o['Result']))
        L.append('%s    Weight: %d' % (pad, int(o['Weight'])))
        if 'Message' in o:
            L.append('%s    Message: %d' % (pad, int(o['Message'])))
        if o['Result'] == 'CLEAR_SLOTS':
            L.append('%s    Slots:' % pad)
            L += ['%s      - Slot: %d' % (pad, int(x)) for x in o['Slots']]
        if o['Result'] == 'REFINE_DOWN':
            if o.get('Random'):
                L.append('%s    Random: true' % pad)
            else:
                L.append('%s    Amount: %d' % (pad, int(o['Amount'])))
        if o['Result'] == 'DOWNGRADE':
            L.append('%s    To: %s' % (pad, o['To']))
        if o.get('Rewards'):
            L.append('%s    Rewards:' % pad)
            for rw in o['Rewards']:
                L.append('%s      - Item: %s' % (pad, rw['Item']))
                L.append('%s        Amount: %d' % (pad, int(rw.get('Amount', 1))))
                L.append('%s        Weight: %d' % (pad, int(rw.get('Weight', 1))))


def server_rules(variants):
    L = ['# GENERATED by tools/need_enchant/generate.py from db/need/enchant_master.yml - do not edit.',
         'Header:', '  Type: NEED_ENCHANT_RULES_DB', '  Version: 1', '', 'Body:']
    n = 0
    for v in variants:
        body = []
        if v['max_refine']:
            body.append('    MaximumRefine: %d' % v['max_refine'])
        slot_lines = []
        for sl in v['order']:
            s = v['slots'][sl]
            sL = []
            if s['min_refine']:
                sL.append('        MinimumRefine: %d' % s['min_refine'])
            if s['require']:
                sL.append('        Require:')
                for rq in s['require']:
                    sL.append('          - Slot: %d' % int(rq['Slot']))
                    sL.append('            Enchants:')
                    sL += ['              - %s' % e for e in rq['Enchants']]
            if s['max_same']:
                sL.append('        MaxSame:')
                for c in s['max_same']:
                    sL.append('          - Enchant: %s' % c['Enchant'])
                    sL.append('            Max: %d' % int(c['Max']))
            if s['failures']:
                yml_outcomes(sL, 8, 'Failures', s['failures'])
            ups = [(k, u) for k, u in s['upgrades'].items() if u['failures']]
            if ups:
                sL.append('        Upgrades:')
                for k, u in ups:
                    sL.append('          - Enchant: %s' % k)
                    sL.append('            SuccessWeight: %d' % u['success'])
                    yml_outcomes(sL, 12, 'Failures', u['failures'])
            if sL:
                slot_lines.append('      - Slot: %d' % sl)
                slot_lines += sL
        if slot_lines:
            body.append('    Slots:')
            body += slot_lines
        r = v['reset']
        if r is not None and (r['outcomes'] or r['require_all'] or r['scope']):
            body.append('    Reset:')
            if r['require_all']:
                body.append('      RequireAllFilled: true')
            if r['scope']:
                body.append('      Scope:')
                body += ['        - Slot: %d' % x for x in r['scope']]
            if r['outcomes']:
                yml_outcomes(body, 6, 'Outcomes', r['outcomes'])
        if body:
            L.append('  - Id: %d' % v['id'])
            L += body
            n += 1
    return '\n'.join(L) + '\n', n


def client_table(v, keyof, caution):
    N = str(v['id']).encode('ascii')
    t = b'Table[' + N + b']'
    L = [t + b' = CreateEnchantInfo()',
         t + b':SetSlotOrder(' + b', '.join(str(x).encode() for x in v['order']) + b')']
    L += [t + b':AddTargetItem_Duplicate(' + nelib.lua_value(keyof(name)) + b')' for name in v['targets']]
    L.append(t + b':SetCondition(' + str(v['min_refine']).encode() + b', 0)')
    L.append(t + b':ApproveRandomOption(' + (b'true' if v['allow_random'] else b'false') + b')')
    r = v['reset']
    if r is None:
        L.append(t + b':SetReset(false, 0, 0)')
    else:
        rate = r['chance']
        if r['outcomes']:
            total = sum(int(o['Weight']) for o in r['outcomes'])
            ok = sum(int(o['Weight']) for o in r['outcomes'] if o['Result'] == 'SUCCESS')
            rate = round(ok * CHANCE_BASE / total)
        args = [b'true', str(rate).encode(), str(r['cost']['zeny']).encode()]
        args += [nelib.lua_value([keyof(m), n]) for m, n in r['cost']['mats'].items()]
        L.append(t + b':SetReset(' + b', '.join(args) + b')')
    L.append(t + b':SetCaution(' + nelib.lua_quote(caution) + b')')
    for sl in v['order']:
        s = v['slots'][sl]
        st = t + b'.Slot[' + str(sl).encode() + b']'
        if s['options']:
            req = [str(s['cost']['zeny']).encode()] + [nelib.lua_value([keyof(m), n]) for m, n in s['cost']['mats'].items()]
            L.append(st + b':SetRequire(' + b', '.join(req) + b')')
            opt = sum(s['options'].values())
            if s['failures']:
                rate = round(opt * CHANCE_BASE / (opt + sum(int(f['Weight']) for f in s['failures'])))
            else:
                rate = s['chance']
            L.append(st + b':SetSuccessRate(' + str(rate).encode() + b')')
            L += [st + b':SetGradeBonus(' + str(g).encode() + b', 0)' for g in GRADE_BONUS_GRADES]
            names = list(s['options'])
            for name, share in zip(names, normalize([s['options'][n] for n in names])):
                L.append(st + b':SetEnchant(0, ' + nelib.lua_value(keyof(name)) + b', ' + str(share).encode() + b')')
        for name, c in s['perfect'].items():
            args = [nelib.lua_value(keyof(name)), str(c['zeny']).encode()] + [nelib.lua_value([keyof(m), n]) for m, n in c['mats'].items()]
            L.append(st + b':AddPerfectEnchant(' + b', '.join(args) + b')')
        for name, u in s['upgrades'].items():
            args = [nelib.lua_value(keyof(name)), nelib.lua_value(keyof(u['to'])), str(u['cost']['zeny']).encode()]
            args += [nelib.lua_value([keyof(m), n]) for m, n in u['cost']['mats'].items()]
            L.append(st + b':AddUpgradeEnchant(' + b', '.join(args) + b')')
    return b'\r\n'.join(L) + b'\r\n'


# ----------------------------------------------------------------------------- main

def display_path(path):
    try:
        return os.path.relpath(path, REPO)
    except ValueError:      # another drive
        return path


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with io.open(path, 'wb') as f:
        f.write(data if isinstance(data, bytes) else data.encode('utf-8'))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--master', default=os.path.join(REPO, 'db', 'need', 'enchant_master.yml'))
    ap.add_argument('--registry', default=os.path.join(REPO, 'db', 'need', 'enchant_clientid_registry.yml'))
    ap.add_argument('--client-grf', default=r'E:\ttt\NEED_LIVE_0821\data.grf', help='official data.grf (KR base, EnchantList_f, ItemDBNameTbl)')
    # The EN base is the Phase 4.18 file pinned in the EN repo, never the repo's live enchantlist.lub:
    # that one is this generator's own output since Phase 2.1b and would feed NEED groups back in.
    ap.add_argument('--en-enchantlist', default=EN_BASE)
    ap.add_argument('--en-stamp', default=EN_BASE_STAMP, help='Phase 4.18 stamp whose enchantlist.lub generated_hash the EN base must match')
    ap.add_argument('--out', required=True)
    ap.add_argument('--commit-registry', action='store_true', help='write newly assigned ids back to the registry')
    args = ap.parse_args(argv)

    report = ['# NEED enchant generator report', '']
    try:
        items, items_by_id = load_items(REPO)
        families, variants = load_master(args.master, items)
        report.append('- master: `%s` - %d families, %d variants' % (display_path(args.master), len(families), len(variants)))

        el_kr = nelib.grf_read(args.client_grf, LUA_DIR + r'\enchant\enchantlist.lub')
        el_f = nelib.grf_read(args.client_grf, LUA_DIR + r'\enchant\enchantlist_f.lub')
        idb = nelib.grf_read(args.client_grf, LUA_DIR + r'\itemdbnametbl.lub')
        el_en = io.open(args.en_enchantlist, 'rb').read()
        en_stamp = json.load(io.open(args.en_stamp, encoding='utf-8')).get('enchantlist.lub') or {}
        if en_stamp.get('status') != 'PASS' or hashlib.sha256(el_en).hexdigest() != en_stamp.get('generated_hash'):
            raise GenError('EN base %s is not the Phase 4.18 stamped EnchantList (status %s, sha256 %s, stamped %s)' % (
                display_path(args.en_enchantlist), en_stamp.get('status'), hashlib.sha256(el_en).hexdigest()[:12],
                str(en_stamp.get('generated_hash'))[:12]))
        official_ids = set(int(x) for x in re.findall(rb'^Table\[(\d+)\] = CreateEnchantInfo', nelib.enchantlist_decompile(el_kr), re.M))
        server_ids = set(int(x) for x in re.findall(r'^  - Id: (\d+)', io.open(os.path.join(REPO, 'db/re/item_enchant.yml'), encoding='utf-8').read(), re.M))
        en_text = nelib.enchantlist_decompile(el_en) if el_en[:4] == b'\x1bLua' else el_en
        en_ids = set(int(x) for x in re.findall(rb'^Table\[(\d+)\] = CreateEnchantInfo', en_text, re.M))
        own = sorted(i for i in official_ids | en_ids if i >= CLIENT_ID_MIN)
        if own:
            raise GenError('official EnchantList input already holds NEED-range ids %s (generated output fed back?)' % own[:10])
        forbidden = official_ids | server_ids

        registry, retired = load_registry(args.registry)
        new_keys = assign_ids(variants, registry, retired, forbidden)
        report.append('- ClientId: %s (new: %s)' % (', '.join('%s=%d' % (v['key'], v['id']) for v in variants), ', '.join(new_keys) or 'none'))
        if new_keys and not args.commit_registry:
            report.append('  - new ids are NOT saved (run with --commit-registry to make them permanent)')

        # resource keys: reuse the official key of an id, else add the AegisName
        pairs, seen = [], set()
        for k, iid in nelib.itemdbnametbl_pairs(idb):
            if k not in seen:
                seen.add(k)
                pairs.append((k, iid))
        key_by_id, id_by_key = {}, {}
        for k, iid in pairs:
            key_by_id.setdefault(iid, k)
            id_by_key[k] = iid
        added = []

        def keyof(name):
            iid = items[name].id
            if iid in key_by_id:
                return key_by_id[iid]
            k = name.encode('ascii')
            if k in id_by_key and id_by_key[k] != iid:
                raise GenError('ItemDBNameTbl key %s already maps to %d (needed for %d)' % (name, id_by_key[k], iid))
            key_by_id[iid] = k
            id_by_key[k] = iid
            pairs.append((k, iid))
            added.append((name, iid))
            return k

        tables_kr, tables_en, variant_rows = [], [], []
        for v in variants:
            kr = caution_text(v, items, 'KR').encode('cp949')
            en = caution_text(v, items, 'EN').encode('ascii')
            tables_kr.append(client_table(v, keyof, kr))
            tables_en.append(client_table(v, keyof, en))
            variant_rows.append('\t'.join([v['family'], v['key'], str(v['id']),
                                           (v['display'] or {}).get('KR', ''), (v['display'] or {}).get('EN', '')]))

        idb_src = nelib.itemdbnametbl_source(pairs)
        base_kr = nelib.enchantlist_decompile(el_kr)
        base_en = nelib.enchantlist_decompile(el_en)
        banner = b'-- NEED Unified Enchant V2: groups ' + str(CLIENT_ID_MIN).encode() + b'+ generated by tools/need_enchant/generate.py\r\n'
        out_kr = base_kr + banner + b''.join(tables_kr)
        out_en = base_en + banner + b''.join(tables_en)
        rules_text, rule_groups = server_rules(variants)

        # gates
        slot_count = lambda iid: items_by_id[iid].slots if iid in items_by_id else 0  # noqa: E731
        eq_kr, _, _ = clientcheck.equivalent(idb_src, el_f, el_kr, base_kr, slot_count)
        eq_en, _, _ = clientcheck.equivalent(idb_src, el_f, el_en, base_en, slot_count)
        run_kr = clientcheck.run(idb_src, el_f, out_kr, slot_count)
        run_en = clientcheck.run(idb_src, el_f, out_en, slot_count)
        gates = [
            ('official KR part decompile equivalence', eq_kr),
            ('official EN part decompile equivalence', eq_en),
            ('KR client replay (CheckFile, LoadAllData, MessageBox 0)', run_kr.ok),
            ('EN client replay (CheckFile, LoadAllData, MessageBox 0)', run_en.ok),
            ('KR tables = official + variants', run_kr.tables == len(official_ids) + len(variants)),
            ('EN tables = official + variants', run_en.tables == len(official_ids) + len(variants)),
        ]
        report += ['', '## Gates', '']
        report += ['- %s: %s' % (name, 'PASS' if ok else 'FAIL') for name, ok in gates]
        for label, run in (('KR', run_kr), ('EN', run_en)):
            for m in run.messages[:10]:
                report.append('  - %s MessageBox: %s' % (label, m))
        report += ['', '## Outputs', '',
                   '- server/item_enchant.yml: %d groups' % len(variants),
                   '- server/enchant_rules.yml: %d groups with rules' % rule_groups,
                   '- ItemDBNameTbl: %d official keys + %d added' % (len(pairs) - len(added), len(added))]
        report += ['  - %s = %d' % a for a in added]

        write(os.path.join(args.out, 'server', 'item_enchant.yml'), server_item_enchant(variants))
        write(os.path.join(args.out, 'server', 'enchant_rules.yml'), rules_text)
        write(os.path.join(args.out, 'client_kr', LUA_DIR, 'enchant', 'enchantlist.lub'), out_kr)
        write(os.path.join(args.out, 'client_en', LUA_DIR, 'enchant', 'enchantlist.lub'), out_en)
        write(os.path.join(args.out, 'client_common', LUA_DIR, 'itemdbnametbl.lub'), idb_src)
        write(os.path.join(args.out, 'variants.tsv'), 'Family\tKey\tClientId\tDisplayKR\tDisplayEN\n' + '\n'.join(variant_rows) + '\n')
        ok = all(g[1] for g in gates)
        report += ['', '**RESULT: %s**' % ('PASS' if ok else 'FAIL')]
        write(os.path.join(args.out, 'report.md'), '\n'.join(report) + '\n')
        if ok and args.commit_registry:
            write_registry(args.registry, registry, retired)
        print('\n'.join(report))
        return 0 if ok else 1
    except GenError as e:
        print('GENERATOR ERROR: %s' % e)
        return 1


if __name__ == '__main__':
    sys.exit(main())

# -*- coding: utf-8 -*-
"""Add the "new enchant window" choice to the legacy enchant NPCs (Phase 3, operator decision 2026-10-07:
keep every legacy dialogue, add a first choice that opens F_NeedEnchantV2 for that NPC's family).

    python tools/need_enchant/wire_legacy.py           apply (idempotent: an NPC already wired is left alone)
    python tools/need_enchant/wire_legacy.py --check   only report, exit 1 if an NPC is not wired
    python tools/need_enchant/wire_legacy.py --remove  take every hook out again (byte-exact inverse)

The hook is appended to the END of an existing line - the NPC header, or the last line of the NPC's own
access gate when it has one (Sarah's progress variable, Dylan's and MARS_01's quest), so a player who could
not reach the old enchant menu does not get a shortcut around it. Appending keeps every line number of the
script, which the parity checker and the importer refer to. Files stay CP949 + CRLF; only those lines change.
"""
import argparse
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
MARK = '// NEED Enchant V2 hook'

# family, script, text in the header's NPC name field, anchors after the header (regexes, matched in order;
# the hook goes after the line of the last one; none = right after the header)
HOOKS = [
    ('CHARLESTON', 'npc/re/merchants/enchan_verus.txt', '양산형 찰스턴#2', []),
    ('EP161_HONOR', 'npc/re/quests/quests_16_1.txt', '딜런#pa0829', [r'completequest 12369;', r'^\s*}\s*$']),
    ('MORA_ARTIFACT', 'npc/re/merchants/enchan_mora.txt', '유물 연구원#new', []),
    ('SARAH_EARRING', 'npc/NEED/instances/SarahAndFenrir.txt', '수석 조교#a1', [r'if \(sarah_fenrir == 0\)', r'^\s*end;\s*$']),
    ('HORROR_TOY_FACTORY', 'npc/re/merchants/HorrorToyFactory_merchants.txt', '검은 수염 죠#pa0829', []),
    ('INFINITE_SPACE', 'npc/re/merchants/InfiniteSpace_merchants.txt', '유물 강화사#pa0829_01', []),
    ('OLD_HELM', 'npc/re/merchants/nightmare_biolab.txt', '헤매는 사념#JCv2', []),
    ('BROSNAN', 'npc/re/merchants/cashmall.txt', '피어싱 브로스넌#pa0829', []),
    ('HERO_RING', 'npc/re/merchants/moro_cav_exchange.txt', '인챈터 번즈', []),
    ('RWC_2012', 'npc/re/merchants/cashmall.txt', '골드버그#new10', []),
    ('FALLEN_ANGEL_WING', 'npc/re/merchants/cashmall.txt', '다크 루히르', []),
    ('UPG_WEAPON', 'npc/re/merchants/enchan_upg.txt', '마성의강화사#prq', []),
    ('EXCELION', 'npc/re/merchants/enchan_verus.txt', 'MARS_01#pa0829', [r'switch\( isbegin_quest\(12368\) \)', r'^\s*case 2:', r'^\s*}\s*$']),
    ('TIME_BOOTS', 'npc/re/merchants/OldGlastHeim_merchants.txt', '휴긴의 마법사#pa0829', []),
    ('TIME_BOOTS', 'npc/re/merchants/OldGlastHeim_merchants.txt', '어둠의 마법 전문가#pa082', []),
    ('MALANGDO_WEAPON', 'npc/re/merchants/enchan_mal.txt', '마요마요#mal', []),
    ('TENE', 'npc/re/merchants/enchan_ko.txt', '장인 테네#ko', []),
    ('HIDDEN_ARMOR', 'npc/merchants/enchan_arm.txt', '수습 세공사', []),
    ('BIO4_SORCERER', 'npc/re/merchants/bio4_reward.txt', '소서러#Bio4Reward', []),
]


def hook_suffix(family):
    return (' if (select(needtr("새 인챈트 창으로 진행", "Use the new enchant window"), needtr("기존 방식으로 대화", "Continue as before")) == 1)'
            ' { callfunc "F_NeedEnchantV2", "%s"; end; } %s (%s)' % (family, MARK, family))


def find_header(lines, name):
    hits = [i for i, l in enumerate(lines) if '\tscript\t' in l and len(l.split('\t')) > 2 and name in l.split('\t')[2]]
    if len(hits) != 1:
        raise SystemExit('header "%s": %d matches (need exactly 1)' % (name, len(hits)))
    return hits[0]


HOOK_START = ' if (select(needtr("새 인챈트 창으로 진행"'


def unhooked(line):
    i = line.find(HOOK_START)
    return line if i < 0 else line[:i]


def place(lines, header, anchors):
    """-> index of the line the hook is appended to"""
    i = header
    for rx in anchors:
        j = i + 1
        while j < len(lines) and not re.search(rx, unhooked(lines[j])):
            if '	script	' in lines[j]:
                raise SystemExit('anchor %r not found before the next NPC (after line %d)' % (rx, header + 1))
            j += 1
        if j >= len(lines):
            raise SystemExit('anchor %r not found after line %d' % (rx, header + 1))
        i = j
    if '//' in lines[i] and MARK not in lines[i]:
        raise SystemExit('line %d already ends with a comment; cannot append the hook' % (i + 1))
    return i


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--remove', action='store_true')
    args = ap.parse_args(argv)
    files = {}
    for fam, path, name, anchors in HOOKS:
        files.setdefault(path, []).append((fam, name, anchors))
    missing, changed = [], []
    for path, entries in files.items():
        full = os.path.join(REPO, path)
        raw = io.open(full, 'rb').read()
        text = raw.decode('cp949')
        lines = text.split('\r\n')
        for fam, name, anchors in entries:
            if args.remove:
                lines = [l.replace(hook_suffix(fam), '') for l in lines]
                continue
            h = find_header(lines, name)
            at = place(lines, h, anchors)
            if MARK in lines[at]:
                continue
            if args.check:
                missing.append('%s (%s)' % (name, path))
                continue
            lines[at] += hook_suffix(fam)
        new = '\r\n'.join(lines).encode('cp949')
        if new != raw and not args.check:
            io.open(full, 'wb').write(new)
            changed.append(path)
    if args.check:
        print('hooks missing: %d%s' % (len(missing), ''.join('\n  ' + m for m in missing)))
        return 1 if missing else 0
    print('%s %d file(s): %s' % ('removed from' if args.remove else 'wired', len(changed), ', '.join(changed) or '-'))
    return 0


if __name__ == '__main__':
    sys.exit(main())

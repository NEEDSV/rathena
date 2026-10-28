"""NEED daily quest: static check of the battleground hooks (phase 4.5, decision D2).

usage: python -I -X utf8 check_bg_hooks.py <rathena root>

The self test cannot run a battle, so this checks where the script calls are:
- needdq_bg_start once per battle, at its start;
- needdq_bg_finish at every official end and nowhere else
    Tierra: both food storages (win) + 30-minute time-out
    Flavius: both crystals, final round (win) + time-out
    KVM: the 300-second end (win, loss and draw all end there)
  each before any bg_warp / mapwarp of the same label, so players are still on the battle map;
- the reward paths (F_BG_Badge, F_KvM_GrantReward, reward NPCs) never judge: no needdq_bg_end left anywhere.
Exit code 0 = PASS.
"""
import os
import re
import sys

root = sys.argv[1]
BG = os.path.join(root, 'npc', 'battleground')

# file -> {(npc name, label): count}
EXPECTED_FINISH = {
    'tierra/tierra01.txt': {('기욤 식량창고#bat_a01', 'OnMyMobDead'): 1, ('크루아 식량창고#bat_a01', 'OnMyMobDead'): 1,
                            ('티에라 카운트다운#bat_a01', 'OnTimer1830000'): 1},
    'flavius/flavius01.txt': {('OBJ#bat_b01_a', 'OnMyMobDead'): 1, ('OBJ#bat_b01_b', 'OnMyMobDead'): 1,
                              ('countdown#bat_b01', 'OnTimer1830000'): 1},
    'kvm/kvm01.txt': {('KvM01_BG', 'OnTimer330000'): 1},
}
HEADER = re.compile(r'^\S+\t(?:script|duplicate\([^)]*\))\t([^\t]+)\t')
FUNC = re.compile(r'^function\tscript\t([^\t]+)\t')
LABEL = re.compile(r'^(On\w+):')

fails = []


def scan(rel):
    lines = open(os.path.join(BG, rel), 'rb').read().decode('cp949').replace('\r\n', '\n').split('\n')
    npc = label = None
    out = []   # (line no, npc, label, kind, text)
    for i, l in enumerate(lines, 1):
        h = HEADER.match(l) or FUNC.match(l)
        if h:
            npc, label = h.group(1), None
            continue
        m = LABEL.match(l)
        if m:
            label = m.group(1)
            continue
        code = l.split('//')[0]
        for kind in ('needdq_bg_start', 'needdq_bg_finish', 'needdq_bg_end', 'bg_warp', 'mapwarp'):
            if re.search(r'\b%s\b' % kind, code):
                out.append((i, npc, label, kind, l.strip()))
    return out


for rel, expected in EXPECTED_FINISH.items():
    calls = scan(rel)
    starts = [c for c in calls if c[3] == 'needdq_bg_start']
    if len(starts) != 1:
        fails.append('%s: %d needdq_bg_start (expected 1)' % (rel, len(starts)))
    found = {}
    for c in calls:
        if c[3] == 'needdq_bg_finish':
            found[(c[1], c[2])] = found.get((c[1], c[2]), 0) + 1
            # finish before any warp of the same npc + label
            warps = [w for w in calls if w[3] in ('bg_warp', 'mapwarp') and (w[1], w[2]) == (c[1], c[2])]
            if warps and min(w[0] for w in warps) < c[0]:
                fails.append('%s:%d finish after a warp in %s::%s' % (rel, c[0], c[1], c[2]))
    # a win is the branch that sets the victory: the finish must sit right under it (Flavius round 1 is not an end)
    text = open(os.path.join(BG, rel), 'rb').read().decode('cp949').replace('\r\n', '\n').split('\n')
    for c in calls:
        if c[3] == 'needdq_bg_finish' and c[2] == 'OnMyMobDead':
            prev = [l for l in text[:c[0] - 1] if l.strip() and not l.strip().startswith('//')][-1]
            if '_Victory =' not in prev:
                fails.append('%s:%d finish not right under the victory assignment' % (rel, c[0]))
    if found != expected:
        fails.append('%s: finish calls %s, expected %s' % (rel, found, expected))
    print('%-24s start %d, finish %s' % (rel, len(starts), sorted(found.items())))

for dirpath, _, files in os.walk(os.path.join(root, 'npc')):
    for f in files:
        if not f.endswith('.txt'):
            continue
        p = os.path.join(dirpath, f)
        text = open(p, 'rb').read().decode('cp949', 'replace')
        if re.search(r'\bneeddq_bg_end\b', text):
            fails.append('%s: needdq_bg_end is still called (reward-time judging)' % p)
        if 'needdq_bg_finish' in text and not p.replace('\\', '/').endswith(tuple(EXPECTED_FINISH)):
            fails.append('%s: needdq_bg_finish outside the three battle files' % p)

common = open(os.path.join(BG, 'bg_common.txt'), 'rb').read().decode('cp949')
if 'needdq_' in common:
    fails.append('bg_common.txt: F_BG_Badge must not call the daily quest')

print('FAIL' if fails else 'PASS', len(fails))
for f in fails:
    print('  -', f)
sys.exit(1 if fails else 0)

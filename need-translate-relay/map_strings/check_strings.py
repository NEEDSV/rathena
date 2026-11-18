#!/usr/bin/env python3
"""Round-trip check of need_chat_translate_strings.inc against gen_strings.STRINGS."""
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import gen_strings  # noqa: E402

root = os.path.abspath(os.path.join(here, '..', '..'))
src = open(os.path.join(root, 'src', 'map', 'need_chat_translate_strings.inc'), 'rb').read().decode('ascii')


def dec(s):
    b = bytearray()
    i = 0
    while i < len(s):
        if s[i] == '\\':
            if s[i + 1] in '01234567':
                b.append(int(s[i + 1:i + 4], 8))
                i += 4
            else:
                b.append(ord(s[i + 1]))
                i += 2
        else:
            b.append(ord(s[i]))
            i += 1
    return bytes(b)


found = {}
for m in re.finditer(r'NCT_S_(\w+)\[2\] = \{ (.*) \};', src):
    lits = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2))
    found[m.group(1)] = (dec(lits[0]).decode('cp949'), dec(lits[1]).decode('ascii'))
bad = 0
for ident, kr, en in gen_strings.STRINGS:
    if found.get(ident) != (kr, en):
        print('MISMATCH', ident, found.get(ident))
        bad += 1
print('strings', len(gen_strings.STRINGS), 'mismatch', bad)
sys.exit(1 if bad else 0)

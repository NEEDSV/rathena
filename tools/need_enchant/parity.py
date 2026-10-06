# -*- coding: utf-8 -*-
"""Exact outcome tables from rAthena NPC script threshold chains (parity source of truth).

A legacy enchant NPC typically does

    .@r = rand( lo, hi );            // or: set .@i, rand(lo, hi);
    if (.@r < 11) .@en_name = 4700;
    else if (.@r < 21) .@en_name = 4720;
    ...
    else .@en_name = 0;

extract_chain() reads the if / else-if / else lines starting at a given line and
distribution() walks every value of the rand window through them, so the result is the exact
integer count per assigned value (no percentages, no rounding).

    python tools/need_enchant/parity.py <script> <chain_line> <lo> <hi>
"""
import io
import re
import sys
from collections import OrderedDict

_COND = r'\(\s*(\.@\w+)\s*(<=|<|>=|>|==)\s*(-?\d+)\s*\)'
_ASSIGN = r'(?:set\s+(\.@\w+)\s*,\s*(-?\d+)|(\.@\w+)\s*=\s*(-?\d+)|(getitem)\s+(\d+)\s*,\s*(\d+))\s*;'
_RE_IF = re.compile(r'^\s*(?:else\s+)?if\s*' + _COND + r'\s*' + _ASSIGN)
_RE_ELSE = re.compile(r'^\s*else\s+' + _ASSIGN)


def read_lines(path):
    raw = io.open(path, 'rb').read()
    try:
        text = raw.decode('utf-8')
    except UnicodeDecodeError:
        text = raw.decode('cp949', 'replace')
    return text.splitlines()


def _strip(line):
    i = line.find('//')
    return line if i < 0 else line[:i]


def extract_chain(lines, start):
    """start: 1-based line of the first `if`. -> (variable, assigned variable, [(op, n, value)], else_value)"""
    chain, else_value, var, target = [], None, None, None
    i = start - 1
    while i < len(lines):
        s = _strip(lines[i]).strip()
        if not s:
            i += 1
            continue
        m = _RE_IF.match(s)
        if m and (var is None or m.group(1) == var):
            var = m.group(1)
            avar, aval = _assigned(m, 4)
            if target is not None and avar != target:
                break
            target = avar
            if chain and not s.startswith('else'):
                break
            chain.append((m.group(2), int(m.group(3)), aval))
            i += 1
            continue
        m = _RE_ELSE.match(s)
        if m and chain:
            avar, aval = _assigned(m, 1)
            if avar == target:
                else_value = aval
        break
    if not chain:
        raise ValueError('no threshold chain at line %d' % start)
    return var, target, chain, else_value


def _assigned(m, first):
    """-> (assigned variable, value). `getitem id,n` counts as the pseudo variable 'getitem' with value 'id:n'."""
    g = [m.group(first + i) for i in range(7)]
    if g[0]:
        return g[0], int(g[1])
    if g[2]:
        return g[2], int(g[3])
    return 'getitem', '%s:%s' % (g[5], g[6])


def inline_rand_lt(lines, line):
    """`if (rand(a,b) < N)` on a line -> (hits, total) with exact counts."""
    m = re.search(r'rand\s*\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\)\s*(<=|<)\s*(-?\d+)', _strip(lines[line - 1]))
    if not m:
        raise ValueError('no inline rand comparison at line %d' % line)
    lo, hi, op, n = int(m.group(1)), int(m.group(2)), m.group(3), int(m.group(4))
    hits = sum(1 for r in range(lo, hi + 1) if (r < n if op == '<' else r <= n))
    return hits, hi - lo + 1


def _hit(op, n, r):
    return {'<': r < n, '<=': r <= n, '>': r > n, '>=': r >= n, '==': r == n}[op]


def distribution(chain, else_value, lo, hi):
    """-> OrderedDict value -> count over r in [lo, hi] (None = no assignment)."""
    out = OrderedDict()
    for r in range(lo, hi + 1):
        value = else_value
        for op, n, v in chain:
            if _hit(op, n, r):
                value = v
                break
        out[value] = out.get(value, 0) + 1
    return out


def table(path, start, lo, hi):
    _var, _target, chain, else_value = extract_chain(read_lines(path), start)
    return distribution(chain, else_value, lo, hi)


if __name__ == '__main__':
    path, start, lo, hi = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
    dist = table(path, start, lo, hi)
    total = sum(dist.values())
    for v, n in dist.items():
        print('%10s  %6d / %d  %8.4f%%' % (v, n, total, 100.0 * n / total))

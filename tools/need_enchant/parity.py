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


def rand_window(lines, line):
    """`rand(lo, hi)` on a line -> (lo, hi); `rand(n)` -> (0, n - 1)."""
    s = _strip(lines[line - 1])
    m = re.search(r'rand\s*\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\)', s)
    if m:
        return int(m.group(1)), int(m.group(2))
    m = re.search(r'rand\s*\(\s*(\d+)\s*\)', s)
    if m:
        return 0, int(m.group(1)) - 1
    # window prepared for a later `rand(.@range[0], .@range[1])`: setarray .@range[0], lo, hi;
    m = re.match(r'^\s*setarray\s+\.@\w+\[0\]\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*;', s)
    if m:
        return int(m.group(1)), int(m.group(2))
    raise ValueError('no rand() or window setarray at line %d' % line)


def string_setarray(lines, line):
    """`setarray .@x$, "a", "b", ...;` over several lines -> [strings]."""
    text, i = '', line - 1
    while i < len(lines):
        text += ' ' + _strip(lines[i])
        if re.search(r'"\s*;', text):
            break
        i += 1
    if not re.match(r'^\s*setarray\s+\.@\w+\$', text):
        raise ValueError('no string setarray at line %d' % line)
    return re.findall(r'"([^"]*)"', text)


def explode_pool(lines, setarray_line, index, field, pick_line):
    """Infinite-Space style uniform pick:
         setarray .@enchant$, "a,b:c,d:...", ...;   explode by ':' (field), then ',' ;
         .@enchant = atoi(.@TT$[rand(getarraysize(.@TT$))]);
    -> OrderedDict value -> 1 per occurrence (uniform)."""
    if not re.search(r'\[\s*rand\s*\(\s*getarraysize\s*\(', _strip(lines[pick_line - 1])):
        raise ValueError('line %d is not a uniform array pick' % pick_line)
    pool = string_setarray(lines, setarray_line)[index].split(':')[field].split(',')
    out = OrderedDict()
    for v in pool:
        v = int(v)
        out[v] = out.get(v, 0) + 1
    return out


def array_pick(lines, array_line, pick_line):
    """Uniform pick from an int setarray: `.@x = .@arr[ rand( getarraysize(.@arr) ) ];` or `.@arr[rand(.@size)]`
    (Hero ring: .@size = getarraysize(.@arr)). -> OrderedDict value -> occurrences."""
    var, values = setarray_values(lines, array_line)
    s = _strip(lines[pick_line - 1])
    m = re.search(re.escape(var) + r'\s*\[\s*rand\s*\(\s*(getarraysize\s*\(\s*' + re.escape(var) + r'\s*\)|\.@\w+|\d+)\s*\)\s*\]', s)
    if not m:
        raise ValueError('line %d does not pick %s[rand(...)]' % (pick_line, var))
    arg = m.group(1)
    if arg.isdigit() and int(arg) != len(values):
        raise ValueError('line %d picks rand(%s) from %d values' % (pick_line, arg, len(values)))
    out = OrderedDict()
    for v in values:
        out[v] = out.get(v, 0) + 1
    return out


_RE_EQ = re.compile(r'^if\s*\(\s*(\.@\w+)\s*==\s*(-?\d+)\s*\)\s*set\s+\.@\w+(?:\[\d+\])?\s*,\s*(\d+)\s*;$')


def eq_table(lines, first_line, lo, hi):
    """Fallen-Angel style: consecutive `if (.@r == k) set .@x[i], id;` lines (no else) -> value -> count over
    r in [lo, hi]. Every r of the window must be covered exactly once."""
    mapping, var, i = {}, None, first_line - 1
    while i < len(lines):
        m = _RE_EQ.match(_strip(lines[i]).strip())
        if not m or (var is not None and m.group(1) != var):
            break
        var = m.group(1)
        k = int(m.group(2))
        if k in mapping:
            raise ValueError('line %d repeats == %d' % (i + 1, k))
        mapping[k] = int(m.group(3))
        i += 1
    if not mapping:
        raise ValueError('no == chain at line %d' % first_line)
    out = OrderedDict()
    for r in range(lo, hi + 1):
        if r not in mapping:
            raise ValueError('== chain at line %d does not cover roll %d' % (first_line, r))
        out[mapping[r]] = out.get(mapping[r], 0) + 1
    return out


def switch_table(lines, switch_line, lo, hi):
    """enchan_arm style: `switch (rand(lo, .@x)) { case 1: case 2: set .@v, ID; break; ... default: ... }`
    -> OrderedDict ID -> count over r in [lo, hi]; rolls without a case go to 'DEFAULT'."""
    if not re.search(r'switch\s*\(\s*rand\s*\(', _strip(lines[switch_line - 1])):
        raise ValueError('line %d is not switch (rand(...))' % switch_line)
    mapping, pending, i = {}, [], switch_line
    while i < len(lines):
        s = _strip(lines[i]).strip()
        i += 1
        if s.startswith('default:') or s == '}':
            break
        for m in re.finditer(r'case\s+(\d+)\s*:', s):
            pending.append(int(m.group(1)))
        m = re.search(r'set\s+\.@\w+\s*,\s*(\d+)\s*;|\.@\w+\s*=\s*(\d+)\s*;', s)
        if m:
            value = int(m.group(1) or m.group(2))
            for k in pending:
                if k in mapping:
                    raise ValueError('case %d repeated near line %d' % (k, i))
                mapping[k] = value
            pending = []
    out = OrderedDict()
    for r in range(lo, hi + 1):
        v = mapping.get(r, 'DEFAULT')
        out[v] = out.get(v, 0) + 1
    return out


def case_labels(lines, first_if_line):
    """`case N:` lines directly above a line -> [N]."""
    out, i = [], first_if_line - 2
    while i >= 0:
        m = re.match(r'^\s*case\s+(\d+)\s*:\s*$', _strip(lines[i]))
        if not m:
            break
        out.insert(0, int(m.group(1)))
        i -= 1
    return out


def frand(lines, line):
    """`callfunc("F_Rand", a, b, ...)` (Global_Functions: getarg(rand(getargcount()))) -> uniform."""
    m = re.search(r'callfunc\s*\(?\s*"F_Rand"\s*,\s*([-\d\s,]+)\)', _strip(lines[line - 1]))
    if not m:
        raise ValueError('no callfunc("F_Rand", ...) at line %d' % line)
    out = OrderedDict()
    for v in m.group(1).split(','):
        v = int(v)
        out[v] = out.get(v, 0) + 1
    return out


def inline_rand_cmp(lines, line, var_lines=None):
    """`rand(a,b) OP N` or `rand(a,b) OP .@var` on a line (OP < <= > >=); .@var is read from its
    integer assignment line in var_lines {name: line}. -> (hits, total) where hits = rolls making the
    comparison true."""
    s = _strip(lines[line - 1])
    m = re.search(r'rand\s*\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\)\s*(<=|<|>=|>)\s*(-?\d+|\.@\w+)', s)
    if m:
        lo, hi, op, rhs = int(m.group(1)), int(m.group(2)), m.group(3), m.group(4)
    else:
        m = re.search(r'rand\s*\(\s*(\d+)\s*\)\s*(<=|<|>=|>)\s*(-?\d+|\.@\w+)', s)   # rand(n) = 0..n-1
        if not m:
            raise ValueError('no inline rand comparison at line %d' % line)
        lo, hi, op, rhs = 0, int(m.group(1)) - 1, m.group(2), m.group(3)
    if rhs.startswith('.@'):
        if not var_lines or rhs not in var_lines:
            raise ValueError('line %d compares with %s; give its assignment line' % (line, rhs))
        n = _int_assign(lines, int(var_lines[rhs]), rhs)
    else:
        n = int(rhs)
    hits = sum(1 for r in range(lo, hi + 1) if _hit(op, n, r))
    return hits, hi - lo + 1


_RE_LOOP = re.compile(r'^for\s*\(\s*(\.@\w+)\s*=\s*0\s*;\s*\1\s*<\s*(\.@\w+)\s*&&\s*\(\s*(\.@\w+)\s*\*\s*\(\s*\1\s*\+\s*1\s*\)\s*\)'
                      r'\s*<\s*(\.@\w+)\s*;\s*\1\s*\+\+\s*\)\s*;$')
_RE_INT_ASSIGN = re.compile(r'^(?:set\s+(\.@\w+)\s*,\s*(-?\d+)|(\.@\w+)\s*=\s*(-?\d+))\s*;$')


def _int_assign(lines, line, var):
    m = _RE_INT_ASSIGN.match(_strip(lines[line - 1]).strip())
    if not m or (m.group(1) or m.group(3)) != var:
        raise ValueError('line %d does not assign %s an integer' % (line, var))
    return int(m.group(2) if m.group(1) else m.group(4))


def setarray_values(lines, line):
    """`setarray .@x[0], a, b, ...;` possibly over several lines -> (variable, [values])."""
    text, i = '', line - 1
    while i < len(lines):
        text += ' ' + _strip(lines[i])
        if ';' in text:
            break
        i += 1
    m = re.match(r'^\s*setarray\s+(\.@\w+)\[\d+\]\s*,(.*?);', text)
    if not m:
        raise ValueError('no setarray at line %d' % line)
    return m.group(1), [int(x) for x in m.group(2).split(',')]


def loop_table(lines, rand_line, loop_line, array_line, size_line, chance_line):
    """Sarah-style pick:  .@r = rand(N);  for (.@i = 0; .@i < .@size && (.@chance * (.@i+1)) < .@r; .@i++);
    -> OrderedDict array value -> count, plus 'OVERFLOW' for .@i == .@size (the script's failure branch)."""
    m = _RE_LOOP.match(_strip(lines[loop_line - 1]).strip())
    if not m:
        raise ValueError('line %d is not the counted pick loop' % loop_line)
    _idx, size_var, chance_var, r_var = m.groups()
    rm = re.match(r'^(\.@\w+)\s*=\s*rand\s*\(', _strip(lines[rand_line - 1]).strip()) or         re.match(r'^set\s+(\.@\w+)\s*,\s*rand\s*\(', _strip(lines[rand_line - 1]).strip())
    if not rm or rm.group(1) != r_var:
        raise ValueError('line %d does not assign %s = rand(...)' % (rand_line, r_var))
    lo, hi = rand_window(lines, rand_line)
    size = _int_assign(lines, size_line, size_var)
    chance = _int_assign(lines, chance_line, chance_var)
    _arr, values = setarray_values(lines, array_line)
    if size > len(values):
        raise ValueError('size %d exceeds the %d array values at line %d' % (size, len(values), array_line))
    out = OrderedDict()
    for r in range(lo, hi + 1):
        i = 0
        while i < size and chance * (i + 1) < r:
            i += 1
        value = 'OVERFLOW' if i == size else values[i]
        out[value] = out.get(value, 0) + 1
    return out


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

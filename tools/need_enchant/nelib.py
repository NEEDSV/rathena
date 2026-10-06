# -*- coding: utf-8 -*-
"""NEED Unified Enchant V2 tooling: shared helpers (stdlib only).

- GRF (0x200) read-only access
- Lua 5.1 bytecode undump/redump (client chunks are 32-bit: size_t 4)
- EnchantList bytecode -> equivalent Lua source text (byte-exact strings)
- ItemDBNameTbl bytecode/text -> (key, id) pairs
"""
import io
import re
import struct
import zlib

# ----------------------------------------------------------------------------- GRF

_grf_cache = {}


def grf_table(path):
    if path in _grf_cache:
        return _grf_cache[path]
    with io.open(path, 'rb') as f:
        hdr = f.read(46)
        if hdr[0:15] != b'Master of Magic':
            raise ValueError('not a GRF: %s' % path)
        table_off, _seed, _fcount, _ver = struct.unpack('<IIII', hdr[30:46])
        f.seek(46 + table_off)
        comp_size, uncomp_size = struct.unpack('<II', f.read(8))
        raw = zlib.decompress(f.read(comp_size))
    if len(raw) != uncomp_size:
        raise ValueError('GRF table size mismatch: %s' % path)
    out, pos = {}, 0
    while pos < len(raw):
        end = raw.index(b'\x00', pos)
        name = raw[pos:end].decode('cp949', 'replace').lower().replace('/', '\\')
        pos = end + 1
        comp_sz, comp_al, uncomp_sz, flags, offset = struct.unpack('<IIIBI', raw[pos:pos + 17])
        pos += 17
        out[name] = (comp_sz, comp_al, uncomp_sz, flags, offset)
    _grf_cache[path] = out
    return out


def grf_read(path, name):
    """-> bytes, or raises KeyError / ValueError."""
    entry = grf_table(path).get(name.lower().replace('/', '\\'))
    if entry is None:
        raise KeyError('%s not in %s' % (name, path))
    comp_sz, comp_al, uncomp_sz, flags, offset = entry
    if not flags & 1 or flags & 0x06:
        raise ValueError('%s: unsupported entry flags 0x%02x' % (name, flags))
    with io.open(path, 'rb') as f:
        f.seek(46 + offset)
        blob = f.read(comp_al)
    for data, wbits in ((blob[:comp_sz], 15), (blob, 15), (blob[:comp_sz], -15)):
        try:
            out = zlib.decompressobj(wbits).decompress(data)
        except zlib.error:
            continue
        if len(out) == uncomp_sz:
            return out
    raise ValueError('%s: undecodable (encrypted?)' % name)


# ----------------------------------------------------------------------------- Lua 5.1 chunk

OPNAMES = ('MOVE LOADK LOADBOOL LOADNIL GETUPVAL GETGLOBAL GETTABLE SETGLOBAL SETUPVAL SETTABLE '
           'NEWTABLE SELF ADD SUB MUL DIV MOD POW UNM NOT LEN CONCAT JMP EQ LT LE TEST TESTSET '
           'CALL TAILCALL RETURN FORLOOP FORPREP TFORLOOP SETLIST CLOSE CLOSURE VARARG').split()
BITRK = 1 << 8


class Chunk(object):
    pass


class _Reader(object):
    def __init__(self, b):
        self.b, self.p = b, 0

    def take(self, n):
        v = self.b[self.p:self.p + n]
        if len(v) != n:
            raise ValueError('truncated chunk at %d' % self.p)
        self.p += n
        return v


def lua_parse(blob):
    r = _Reader(blob)
    head = r.take(12)
    if head[:4] != b'\x1bLua' or head[4] != 0x51:
        raise ValueError('not a Lua 5.1 chunk')
    c = Chunk()
    c.header = head
    c.size_int, c.size_t, c.size_ins, c.size_num = head[7], head[8], head[9], head[10]
    e = '<' if head[6] == 1 else '>'
    c.IFMT = e + {4: 'i', 8: 'q'}[c.size_int]
    c.TFMT = e + {4: 'I', 8: 'Q'}[c.size_t]
    c.NFMT = e + {4: 'f', 8: 'd'}[c.size_num]
    c.func = _parse_func(r, c)
    c.tail = r.b[r.p:]
    return c


def _pint(r, c):
    return struct.unpack(c.IFMT, r.take(c.size_int))[0]


def _pstr(r, c):
    n = struct.unpack(c.TFMT, r.take(c.size_t))[0]
    return None if n == 0 else r.take(n)[:-1]


def _parse_func(r, c):
    f = {'source': _pstr(r, c), 'linedefined': _pint(r, c), 'lastlinedefined': _pint(r, c)}
    f['nups'], f['numparams'], f['is_vararg'], f['maxstacksize'] = r.take(4)
    f['code'] = [struct.unpack(c.IFMT[0] + 'I', r.take(c.size_ins))[0] for _ in range(_pint(r, c))]
    ks = []
    for _ in range(_pint(r, c)):
        t = r.take(1)[0]
        if t == 0:
            ks.append(('nil', None))
        elif t == 1:
            ks.append(('bool', r.take(1)[0]))
        elif t == 3:
            ks.append(('num', struct.unpack(c.NFMT, r.take(c.size_num))[0]))
        elif t == 4:
            ks.append(('str', _pstr(r, c)))
        else:
            raise ValueError('constant type %d' % t)
    f['consts'] = ks
    f['protos'] = [_parse_func(r, c) for _ in range(_pint(r, c))]
    f['lineinfo'] = [_pint(r, c) for _ in range(_pint(r, c))]
    f['locvars'] = [(_pstr(r, c), _pint(r, c), _pint(r, c)) for _ in range(_pint(r, c))]
    f['upvalues'] = [_pstr(r, c) for _ in range(_pint(r, c))]
    return f


def lua_dump(c):
    out = bytearray(c.header)
    _dump_func(out, c, c.func)
    out += c.tail
    return bytes(out)


def _dint(out, c, v):
    out += struct.pack(c.IFMT, v)


def _dstr(out, c, s):
    if s is None:
        out += struct.pack(c.TFMT, 0)
    else:
        out += struct.pack(c.TFMT, len(s) + 1) + s + b'\x00'


def _dump_func(out, c, f):
    _dstr(out, c, f['source'])
    _dint(out, c, f['linedefined'])
    _dint(out, c, f['lastlinedefined'])
    out += bytes([f['nups'], f['numparams'], f['is_vararg'], f['maxstacksize']])
    _dint(out, c, len(f['code']))
    for i in f['code']:
        out += struct.pack(c.IFMT[0] + 'I', i)
    _dint(out, c, len(f['consts']))
    for t, v in f['consts']:
        if t == 'nil':
            out += b'\x00'
        elif t == 'bool':
            out += bytes([1, v])
        elif t == 'num':
            out += b'\x03' + struct.pack(c.NFMT, v)
        else:
            out += b'\x04'
            _dstr(out, c, v)
    _dint(out, c, len(f['protos']))
    for p in f['protos']:
        _dump_func(out, c, p)
    _dint(out, c, len(f['lineinfo']))
    for v in f['lineinfo']:
        _dint(out, c, v)
    _dint(out, c, len(f['locvars']))
    for s, a, b in f['locvars']:
        _dstr(out, c, s)
        _dint(out, c, a)
        _dint(out, c, b)
    _dint(out, c, len(f['upvalues']))
    for s in f['upvalues']:
        _dstr(out, c, s)


def lua_to_host(blob, size_t=8):
    """Re-dump a client chunk (size_t 4) for a host Lua 5.1 built with another size_t."""
    c = lua_parse(blob)
    if c.size_t == size_t:
        return blob
    c.size_t = size_t
    c.TFMT = c.TFMT[0] + {4: 'I', 8: 'Q'}[size_t]
    c.header = c.header[:8] + bytes([size_t]) + c.header[9:]
    return lua_dump(c)


# ----------------------------------------------------------------------------- Lua source helpers

def lua_quote(b):
    """bytes -> Lua 5.1 string literal; high bytes are kept raw (cp949 text stays byte-exact)."""
    out = bytearray(b'"')
    for ch in b:
        if ch == 0x5C:
            out += b'\\\\'
        elif ch == 0x22:
            out += b'\\"'
        elif ch == 0x0A:
            out += b'\\n'
        elif ch == 0x0D:
            out += b'\\r'
        elif ch == 0x09:
            out += b'\\t'
        elif ch < 0x20 or ch == 0x7F:
            out += ('\\%03d' % ch).encode('ascii')
        else:
            out.append(ch)
    out += b'"'
    return bytes(out)


def lua_number(v):
    if isinstance(v, bool):
        raise TypeError('bool')
    f = float(v)
    if f.is_integer():
        return str(int(f)).encode('ascii')
    return repr(f).encode('ascii')


def lua_value(v):
    """Python value -> Lua source bytes (str/bytes, int/float, bool, list)."""
    if isinstance(v, bool):
        return b'true' if v else b'false'
    if isinstance(v, (int, float)):
        return lua_number(v)
    if isinstance(v, bytes):
        return lua_quote(v)
    if isinstance(v, str):
        return lua_quote(v.encode('cp949'))
    if isinstance(v, (list, tuple)):
        return b'{' + b', '.join(lua_value(x) for x in v) + b'}'
    raise TypeError(type(v))


# ----------------------------------------------------------------------------- EnchantList decompile

def enchantlist_decompile(blob):
    """EnchantList bytecode (a flat list of Table[N] method calls) -> Lua source bytes.
    The result is checked for exact equivalence by clientcheck.equivalent()."""
    c = lua_parse(blob)
    f = c.func
    K, code = f['consts'], f['code']
    if f['protos']:
        raise ValueError('unexpected nested functions in EnchantList')
    regs = {}
    lines = []

    def k(i):
        t, v = K[i]
        if t == 'str':
            return ('s', v)
        if t == 'num':
            return ('n', v)
        if t == 'bool':
            return ('b', bool(v))
        return ('nil', None)

    def rk(x):
        return k(x & 0xFF) if x & BITRK else regs.get(x)

    def src(v):
        if v is None:
            raise ValueError('unknown register value')
        t = v[0]
        if t == 's':
            return lua_quote(v[1])
        if t == 'n':
            return lua_number(v[1])
        if t == 'b':
            return b'true' if v[1] else b'false'
        if t == 'nil':
            return b'nil'
        if t == 'tbl':
            return b'{' + b', '.join(src(x) for x in v[1]) + b'}'
        if t == 'ref':
            return v[1]
        raise ValueError('cannot render %r' % (t,))

    pc = 0
    while pc < len(code):
        ins = code[pc]
        op = OPNAMES[ins & 0x3F]
        a = (ins >> 6) & 0xFF
        cc = (ins >> 14) & 0x1FF
        b = (ins >> 23) & 0x1FF
        bx = (ins >> 14) & 0x3FFFF
        if op == 'LOADK':
            regs[a] = k(bx)
        elif op == 'LOADBOOL':
            regs[a] = ('b', bool(b))
            if cc:
                raise ValueError('LOADBOOL skip not supported')
        elif op == 'LOADNIL':
            for rr in range(a, b + 1):
                regs[rr] = ('nil', None)
        elif op == 'GETGLOBAL':
            regs[a] = ('ref', K[bx][1])
        elif op == 'GETTABLE':
            o, key = regs.get(b), rk(cc)
            if o is None or o[0] != 'ref':
                raise ValueError('GETTABLE on %r' % (o,))
            if key[0] == 'n':
                regs[a] = ('ref', o[1] + b'[' + lua_number(key[1]) + b']')
            elif key[0] == 's':
                regs[a] = ('ref', o[1] + b'.' + key[1])
            else:
                raise ValueError('GETTABLE key %r' % (key,))
        elif op == 'SETTABLE':
            o, key, val = regs.get(a), rk(b), rk(cc)
            lines.append(o[1] + b'[' + src(key) + b'] = ' + src(val))
        elif op == 'NEWTABLE':
            regs[a] = ('tbl', [])
        elif op == 'SETLIST':
            if cc == 0:
                pc += 1
            t = regs.get(a)
            for i in range(1, b + 1):
                t[1].append(regs.get(a + i))
        elif op == 'MOVE':
            regs[a] = regs.get(b)
        elif op == 'SELF':
            o, key = regs.get(b), rk(cc)
            regs[a] = ('meth', o, key[1])
            regs[a + 1] = o
        elif op == 'CALL':
            fn = regs.get(a)
            nargs = b - 1
            if nargs < 0:
                raise ValueError('variable argument call not supported')
            args = [regs.get(a + 1 + i) for i in range(nargs)]
            if fn[0] == 'meth':
                lines.append(fn[1][1] + b':' + fn[2] + b'(' + b', '.join(src(x) for x in args[1:]) + b')')
                regs[a] = ('ret', None)
            elif fn[0] == 'ref':
                regs[a] = ('ref', fn[1] + b'(' + b', '.join(src(x) for x in args) + b')')
                if cc != 2:   # result discarded -> statement
                    lines.append(regs[a][1])
            else:
                raise ValueError('CALL on %r' % (fn,))
        elif op == 'SETGLOBAL':
            lines.append(K[bx][1] + b' = ' + src(regs.get(a)))
        elif op == 'RETURN':
            pass
        else:
            raise ValueError('unsupported opcode %s at %d' % (op, pc))
        pc += 1
    return b'\r\n'.join(lines) + b'\r\n'


# ----------------------------------------------------------------------------- ItemDBNameTbl

def itemdbnametbl_pairs(blob):
    """-> list of (key bytes, id int) in definition order (duplicates kept)."""
    if blob[:4] == b'\x1bLua':
        f = lua_parse(blob).func
        K, regs, pairs = f['consts'], {}, []
        for ins in f['code']:
            op = OPNAMES[ins & 0x3F]
            a = (ins >> 6) & 0xFF
            cc = (ins >> 14) & 0x1FF
            b = (ins >> 23) & 0x1FF
            bx = (ins >> 14) & 0x3FFFF
            if op == 'LOADK':
                regs[a] = K[bx]
            elif op == 'SETTABLE':
                tk = K[b & 0xFF] if b & BITRK else regs.get(b)
                vk = K[cc & 0xFF] if cc & BITRK else regs.get(cc)
                if tk and vk and tk[0] == 'str' and vk[0] == 'num':
                    pairs.append((tk[1], int(vk[1])))
        return pairs
    # GRF Editor decompiler text (bare or ["quoted"] keys)
    body = blob.split(b'ItemDBNameTbl = {', 1)[1].split(b'\n}', 1)[0]
    pairs = []
    for line in body.splitlines():
        s = line.strip()
        if not s:
            continue
        m = re.match(rb'^(?:\["(.*)"\]|([^\s=]+))\s*=\s*(\d+),?$', s)
        if not m:
            raise ValueError('unparsed ItemDBNameTbl line: %r' % s)
        pairs.append((m.group(1) if m.group(1) is not None else m.group(2), int(m.group(3))))
    return pairs


ITEMDB_TO_ITEMID = (
    b'ItemDB_To_ItemID = function(in_ItemDB)\r\n'
    b'\tif ItemDBNameTbl[in_ItemDB] == nil then\r\n'
    b'\t\tMessageBox("[ " .. in_ItemDB .. " ] : ItemDBNameTbl' + '에서 찾을 수 없습니다.'.encode('cp949') + b'")\r\n'
    b'\t\treturn 0\r\n'
    b'\tend\r\n'
    b'\treturn ItemDBNameTbl[in_ItemDB]\r\n'
    b'end\r\n')


def itemdbnametbl_source(pairs):
    """(key bytes, id) pairs -> ItemDBNameTbl source with always-valid ["key"] syntax."""
    out = [b'ItemDBNameTbl = {']
    for key, iid in pairs:
        out.append(b'\t[' + lua_quote(key) + b'] = ' + str(iid).encode('ascii') + b',')
    out.append(b'}')
    return b'\r\n'.join(out) + b'\r\n\r\n' + ITEMDB_TO_ITEMID

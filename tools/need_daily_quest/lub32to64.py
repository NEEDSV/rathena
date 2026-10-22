"""Lua 5.1 bytecode: rewrite a chunk compiled with sizeof(size_t)=4 into sizeof(size_t)=8."""
import struct

def convert(data: bytes) -> bytes:
    assert data[:4] == b'\x1bLua' and data[4] == 0x51, 'not Lua 5.1'
    hdr = bytearray(data[:12])
    endian, s_int, s_sizet, s_instr, s_num = data[6], data[7], data[8], data[9], data[10]
    assert endian == 1 and s_int == 4 and s_instr == 4 and s_num == 8
    if s_sizet == 8:
        return data
    assert s_sizet == 4
    hdr[8] = 8
    out = bytearray(hdr)
    pos = 12

    def rd(n):
        nonlocal pos
        b = data[pos:pos + n]; pos += n; return b

    def rint():
        return struct.unpack('<i', rd(4))[0]

    def string():
        n = struct.unpack('<I', rd(4))[0]
        out.extend(struct.pack('<Q', n)); out.extend(rd(n))

    def copy_int():
        b = rd(4); out.extend(b); return struct.unpack('<i', b)[0]

    def function():
        string()                      # source
        copy_int(); copy_int()        # linedefined, lastlinedefined
        out.extend(rd(4))             # nups, numparams, is_vararg, maxstacksize
        n = copy_int(); out.extend(rd(4 * n))          # code
        n = copy_int()                                  # constants
        for _ in range(n):
            t = rd(1)[0]; out.append(t)
            if t == 0: pass
            elif t == 1: out.extend(rd(1))
            elif t == 3: out.extend(rd(8))
            elif t == 4: string()
            else: raise ValueError('bad const type %d' % t)
        n = copy_int()                                  # protos
        for _ in range(n): function()
        n = copy_int(); out.extend(rd(4 * n))          # lineinfo
        n = copy_int()                                  # locvars
        for _ in range(n): string(); copy_int(); copy_int()
        n = copy_int()                                  # upvalues
        for _ in range(n): string()

    function()
    assert pos == len(data), (pos, len(data))
    return bytes(out)

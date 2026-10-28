# -*- coding: utf-8 -*-
"""Build the rune tooltip client patches (.gpf) from the committed client repos.

    python tools/need_rune_tooltip/make_patch.py --kr ../NEEDResoruce --en ../NEEDResoruce_EN --out <dir> [--tag 261019]

  <tag>_rune_tooltip.gpf     shared list (merged into need_data.grf): efstids / stateiconimginfo / stateiconinfo_f,
                             KR stateiconinfo, need_rune_<set>.tga x10
  <tag>_en_rune_tooltip.gpf  EN list (merged into need_english.grf): EN stateiconinfo only
Ship BOTH in the same round: an EN client reads stateiconinfo from need_english.grf, so without the EN gpf it
shows the icons with no text.

Uses build_english.build_grf (classic 0x200 writer of the EN pipeline), re-reads both files and checks every
entry byte for byte against the repos, then runs the generator --check so a patch is never built from drift.
"""
import argparse
import glob
import hashlib
import json
import os
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
STATEICON = 'data\\luafiles514\\lua files\\stateicon\\'
EFFECT = 'data\\texture\\effect\\'


def read_grf(path):
    d = open(path, 'rb').read()
    toff, seed, fc, ver = struct.unpack('<IIII', d[30:46])
    assert d[:15] == b'Master of Magic' and ver == 0x200
    ft = 46 + toff
    csz, usz = struct.unpack('<II', d[ft:ft + 8])
    tbl = zlib.decompress(d[ft + 8:ft + 8 + csz])
    out, pos = {}, 0
    while pos < len(tbl):
        e = tbl.index(b'\0', pos)
        name = tbl[pos:e]
        pos = e + 1
        c, ca, u, fl, off = struct.unpack('<IIIBI', tbl[pos:pos + 17])
        pos += 17
        raw = d[46 + off:46 + off + c]
        out[name] = zlib.decompress(raw) if fl & 1 else raw
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kr', required=True)
    ap.add_argument('--en', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--tag', default='261019')
    a = ap.parse_args()
    gen = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'generate.py'), '--kr', a.kr, '--en', a.en, '--check'],
                         capture_output=True, text=True)
    if gen.returncode != 0:
        raise SystemExit('generator --check failed, refusing to build a patch:\n' + gen.stdout)
    sys.path.insert(0, os.path.join(a.en, 'en_build'))
    import build_english as BE   # noqa: E402

    def src(root, grfname):
        return open(os.path.join(root, grfname.replace('\\', os.sep)), 'rb').read()

    kr_names = [STATEICON + n for n in ('efstids.lub', 'stateiconinfo.lub', 'stateiconimginfo.lub', 'stateiconinfo_f.lub')]
    kr_names += [EFFECT + os.path.basename(p) for p in sorted(glob.glob(os.path.join(a.kr, 'data', 'texture', 'effect', 'need_rune_*.tga')))]
    en_names = [STATEICON + 'stateiconinfo.lub']
    if len(kr_names) != 14:
        raise SystemExit('expected 4 lub + 10 tga, got %d' % len(kr_names))
    os.makedirs(a.out, exist_ok=True)
    rep = {}
    for fname, root, names in (('%s_rune_tooltip.gpf' % a.tag, a.kr, kr_names), ('%s_en_rune_tooltip.gpf' % a.tag, a.en, en_names)):
        files = [(n.encode('cp949'), src(root, n)) for n in names]
        path = os.path.join(a.out, fname)
        BE.build_grf(path, files)
        BE.verify_grf(path)
        back = read_grf(path)
        bad = [n for n, d in files if back.get(n) != d] + [n for n in back if n not in dict(files)]
        if bad:
            raise SystemExit('%s: entries differ after re-read: %r' % (fname, bad))
        data = open(path, 'rb').read()
        rep[fname] = {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
                      'entries': {n.decode('cp949'): hashlib.sha256(d).hexdigest()[:16] for n, d in files}}
    # common entries must be identical in the EN repo (EN clients read them from need_data.grf)
    for n in kr_names:
        if 'stateiconinfo.lub' not in n and src(a.kr, n) != src(a.en, n):
            raise SystemExit('KR/EN differ: %s' % n)
    with open(os.path.join(a.out, 'patch_report.json'), 'w', encoding='utf-8') as f:
        json.dump(rep, f, ensure_ascii=False, indent=1)
    for k, v in rep.items():
        print('%s  %d bytes  %d entries  sha256 %s' % (k, v['bytes'], len(v['entries']), v['sha256'][:16]))
    return 0


if __name__ == '__main__':
    sys.exit(main())

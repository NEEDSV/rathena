# -*- coding: utf-8 -*-
"""Verify the rune tooltip client files by running them in Lua (lupa), KR and EN.

    python tools/need_rune_tooltip/verify_client.py --kr <KR repo or client data root> --en <EN ...> [--base-kr <dir> --base-en <dir>]

Checks, per language:
  1. the 4 stateicon lub files load in Lua in client order (efstids, stateiconinfo, stateiconimginfo, stateiconinfo_f)
  2. every generated EFST: GetStateIconDescript lines == generator text, no timer, HaveEfstImg true,
     GetEFSTImgFileName == effect\\need_rune_<set>.tga, the tga exists (32x32, 32 bpp, uncompressed)
  3. every EFST of the base files (generated block stripped, or --base-*) renders exactly as before
  4. KR and EN share efstids / stateiconimginfo / stateiconinfo_f / tga byte for byte,
     and EN tooltip lines are ASCII with the same line count and colours as KR
Exit 1 on any failure.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import generate as gen   # noqa: E402
import yaml               # noqa: E402

FILES = ['efstids.lub', 'stateiconinfo.lub', 'stateiconimginfo.lub', 'stateiconinfo_f.lub']


def lua_with(blobs):
    from lupa import LuaRuntime
    L = LuaRuntime(encoding=None, unpack_returned_tuples=True)
    for n in FILES:
        L.execute(blobs[n])
    return L


def render(L, efst):
    g = L.globals()
    rows = []
    for i in range(1, 40):
        r = g.GetStateIconDescript(efst, i)
        if not r[0]:
            break
        rows.append((r[0], r[1], r[2], r[3]))
    return rows, tuple(g.GetTimeLimitInfo(efst)), bool(g.HaveEfstImg(efst)), g.GetEFSTImgFileName(efst, 0)


def base_of(n, data):
    if n == 'efstids.lub':
        i = data.find(b'\t' + gen.BEGIN.encode())
        if i < 0:
            return data
        j = data.find(gen.END.encode(), i)
        return data[:i] + data[j + len(gen.END) + 2:]
    return gen.strip_tail_block(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kr', required=True)
    ap.add_argument('--en', required=True)
    ap.add_argument('--icons', default=os.path.join(gen.REPO, '..', 'NEEDResoruce'), help='KR repo with the rune UI bmps')
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')
    cfg = yaml.safe_load(open(os.path.join(HERE, 'config.yml'), encoding='utf-8'))
    _, sets_states = gen.generate(cfg, a.kr, a.en, a.icons)
    fails = []
    blobs = {}
    for lang, root in (('Kr', a.kr), ('En', a.en)):
        cur = {n: open(os.path.join(root, gen.STATEICON, n), 'rb').read() for n in FILES}
        base = {n: base_of(n, d) for n, d in cur.items()}
        blobs[lang] = cur
        L, B = lua_with(cur), lua_with(base)
        ids = [v for _, v in B.globals().EFST_IDs.items() if isinstance(v, int)]
        changed = [e for e in ids if render(B, e) != render(L, e)]
        if changed:
            fails.append('%s: %d existing EFSTs render differently: %s' % (lang, len(changed), changed[:10]))
        n_ok = 0
        for s, states in sets_states:
            sc = cfg['Sets'][s['id']]
            for st in states:
                want = gen.tooltip_lines(cfg, sc, st, lang)
                rows, tl, img, fn = render(L, st['efst'])
                texts = [r[0].decode('cp949') for r in rows]
                if texts != want:
                    fails.append('%s EFST %d: lines %r != %r' % (lang, st['efst'], texts, want))
                elif tl != (0, 0) or not img or fn != b'effect\\' + gen.icon_name(sc).encode():
                    fails.append('%s EFST %d: timer %r img %r file %r' % (lang, st['efst'], tl, img, fn))
                elif [r[1:] for r in rows] != [(155, 202, 155)] + [(255, 255, 255)] * (len(rows) - 1):
                    fails.append('%s EFST %d: colours %r' % (lang, st['efst'], [r[1:] for r in rows]))
                else:
                    n_ok += 1
                if lang == 'En' and any(ord(c) > 127 for t in texts for c in t):
                    fails.append('EN EFST %d: non-ASCII text' % st['efst'])
                if L.globals().EFST_IDs[gen.efst_symbol(sc, st['efst']).encode()] != st['efst']:
                    fails.append('%s: efstids symbol %s missing' % (lang, gen.efst_symbol(sc, st['efst'])))
        print('%s: existing EFSTs %d unchanged %d, rune states OK %d' % (lang, len(ids), len(ids) - len(changed), n_ok))
    for n in ('efstids.lub', 'stateiconimginfo.lub', 'stateiconinfo_f.lub'):
        if blobs['Kr'][n] != blobs['En'][n]:
            fails.append('KR/EN %s differ' % n)
    for s, _ in sets_states:
        sc = cfg['Sets'][s['id']]
        tg = []
        for root in (a.kr, a.en):
            p = os.path.join(root, gen.EFFECT, gen.icon_name(sc))
            d = open(p, 'rb').read() if os.path.isfile(p) else b''
            tg.append(d)
            if len(d) != 18 + 32 * 32 * 4 + 26 or d[2] != 2 or d[12:18] != bytes([32, 0, 32, 0, 32, 8]):
                fails.append('bad tga %s' % p)
        if tg[0] != tg[1]:
            fails.append('KR/EN %s differ' % gen.icon_name(sc))
    for f in fails:
        print('FAIL', f)
    print('RESULT', 'FAIL' if fails else 'PASS')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())

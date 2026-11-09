"""Enchant V2 client patches as .gpf (classic 0x200 GRF, plain zlib - the writer of need_english.grf, and the
same kind of entries that live patches such as 260926_egg.gpf already merged into need_data.grf).

  KR: enchantlist (KR) + itemdbnametbl  -> shared list patch_main.txt -> need_data.grf
  EN: enchantlist (EN)                  -> EN list patch_en.txt       -> need_english.grf

usage: make_gpf.py --generated <generate.py --out dir> --out <patch dir> --name <prefix>
       -> <prefix>.gpf (KR) and <prefix with _en after the date>.gpf (EN), gpf_report.json, files.txt and the
          unpacked entries under KR\\ and EN\\ for review.
Each patch is read back and must hold exactly the given bytes. Never pack need_data.grf / need_english.grf.
"""
import argparse
import hashlib
import json
import os
import sys

EN_BUILD = r'E:\tools\Need\NEEDResoruce_EN\en_build'
sys.path.insert(0, EN_BUILD)
sys.path.insert(0, os.path.join(EN_BUILD, 'kr_split_429'))
import build_english as BE  # noqa: E402
import make_en_gpf as MG  # noqa: E402

BS = chr(92)
EL = BS.join(['data', 'luafiles514', 'lua files', 'enchant', 'enchantlist.lub'])
IT = BS.join(['data', 'luafiles514', 'lua files', 'itemdbnametbl.lub'])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--generated', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--name', required=True, help='KR patch name without .gpf, e.g. 261109_enchant_v2_ep162')
    args = ap.parse_args(argv)

    def src(*p):
        return open(os.path.join(args.generated, *p), 'rb').read()

    date, _, rest = args.name.partition('_')
    patches = {
        args.name + '.gpf': ('KR', [(EL, src('client_kr', *EL.split(BS))), (IT, src('client_common', *IT.split(BS)))]),
        '%s_en_%s.gpf' % (date, rest): ('EN', [(EL, src('client_en', *EL.split(BS)))]),
    }
    os.makedirs(args.out, exist_ok=True)
    report, files = {}, []
    for name, (side, entries) in patches.items():
        path = os.path.join(args.out, name)
        BE.build_grf(path, [(n.encode('cp949'), d) for n, d in entries])
        BE.verify_grf(path)
        back = MG.entries(path)
        if set(back) != {n.encode('cp949') for n, _ in entries} or any(back[n.encode('cp949')] != d for n, d in entries):
            raise SystemExit('%s: re-read mismatch' % name)
        for n, d in entries:
            dst = os.path.join(args.out, side, n)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, 'wb').write(d)
            files.append('%s  %s  %d bytes  sha256 %s' % (side, n, len(d), hashlib.sha256(d).hexdigest()))
        report[name] = {'bytes': os.path.getsize(path), 'sha256': hashlib.sha256(open(path, 'rb').read()).hexdigest(),
                        'entries': {n: {'bytes': len(d), 'sha256': hashlib.sha256(d).hexdigest()} for n, d in entries}}
    open(os.path.join(args.out, 'gpf_report.json'), 'w', encoding='utf-8').write(json.dumps(report, indent=1, ensure_ascii=False))
    open(os.path.join(args.out, 'files.txt'), 'w', encoding='utf-8').write('\n'.join(files) + '\n')
    print(json.dumps(report, indent=1, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(main())

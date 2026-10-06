# -*- coding: utf-8 -*-
"""Install generator output (operator step).

    python tools/need_enchant/install.py --from <generate --out dir> --server
    python tools/need_enchant/install.py --from <dir> --client-kr <KR client folder> --client-en <EN client folder>

--server      copies server/item_enchant.yml -> db/import/item_enchant.yml and
              server/enchant_rules.yml -> db/need/enchant_rules.yml. Refused while any rAthena server
              process runs from this machine (the db folder is shared with it), unless --force.
--client-*    copies the client Lua files as loose files under <client>/data/... for a test client.
              Packing them into need_data.grf (KR, EnchantList + ItemDBNameTbl) and need_english.grf
              (EN EnchantList) stays an operator step.
The generator report must say RESULT: PASS.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
LUA = os.path.join('data', 'luafiles514', 'lua files')
SERVER_PROCESSES = ('map-server', 'char-server', 'login-server', 'web-server')


def running_servers():
    try:
        out = subprocess.run(['tasklist'], capture_output=True, text=True, errors='replace').stdout.lower()
    except OSError:
        return []
    return sorted({line.split()[0] for line in out.splitlines() if any(p in line for p in SERVER_PROCESSES)})


def copy(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    print('  %s -> %s' % (src, dst))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--from', dest='src', required=True)
    ap.add_argument('--server', action='store_true')
    ap.add_argument('--client-kr')
    ap.add_argument('--client-en')
    ap.add_argument('--force', action='store_true')
    args = ap.parse_args(argv)

    report = os.path.join(args.src, 'report.md')
    if not os.path.isfile(report) or '**RESULT: PASS**' not in open(report, encoding='utf-8').read():
        print('refused: %s does not say RESULT: PASS' % report)
        return 1

    if args.server:
        procs = running_servers()
        if procs and not args.force:
            print('refused: rAthena servers are running (%s); stop them first (db/ is shared)' % ', '.join(procs))
            return 1
        print('server:')
        copy(os.path.join(args.src, 'server', 'item_enchant.yml'), os.path.join(REPO, 'db', 'import', 'item_enchant.yml'))
        copy(os.path.join(args.src, 'server', 'enchant_rules.yml'), os.path.join(REPO, 'db', 'need', 'enchant_rules.yml'))

    if args.client_kr:
        print('client KR:')
        copy(os.path.join(args.src, 'client_kr', LUA, 'enchant', 'enchantlist.lub'), os.path.join(args.client_kr, LUA, 'enchant', 'enchantlist.lub'))
        copy(os.path.join(args.src, 'client_common', LUA, 'itemdbnametbl.lub'), os.path.join(args.client_kr, LUA, 'itemdbnametbl.lub'))
    if args.client_en:
        print('client EN:')
        copy(os.path.join(args.src, 'client_en', LUA, 'enchant', 'enchantlist.lub'), os.path.join(args.client_en, LUA, 'enchant', 'enchantlist.lub'))
        copy(os.path.join(args.src, 'client_common', LUA, 'itemdbnametbl.lub'), os.path.join(args.client_en, LUA, 'itemdbnametbl.lub'))
    return 0


if __name__ == '__main__':
    sys.exit(main())

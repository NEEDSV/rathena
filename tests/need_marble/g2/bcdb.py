"""G2 isolated DB helper. Only 127.0.0.1:3327 / ragnarok_mbqa. Secrets come from <out>/secrets.json and are
passed through MYSQL_PWD, never on the command line, never printed.

usage: python -I bcdb.py --out <scratch>/mbqa_bc file <ddl|rt|root> <sql file> [key=value ...]
       python -I bcdb.py --out <scratch>/mbqa_bc sql  <ddl|rt|root> "<query>"
{{srv_user}} {{srv}} {{gm}} {{user}} {{instance_token}} and key=value pairs are substituted in files.
"""
import argparse
import json
import os
import subprocess
import sys

MYSQL = r'E:\MariaDB 12.2\bin\mysql.exe'
PORT = 3327
DB = 'ragnarok_mbqa'
USERS = {'root': 'root', 'ddl': 'mbqa_ddl', 'rt': 'mbqa_rt'}


def secrets(out):
    a = json.load(open(os.path.join(out, 'approved.json')))
    assert a['db_port'] == PORT and a['db_name'] == DB and a['db_host'] == '127.0.0.1'
    return json.load(open(os.path.join(out, 'secrets.json')))


def mask(text, s):
    for k, v in s.items():
        if isinstance(v, str) and len(v) >= 8:
            text = text.replace(v, '***')
    return text


def run(out, who, sql, table=False):
    s = secrets(out)
    pw = s[who]
    args = [MYSQL, '--host=127.0.0.1', f'--port={PORT}', f'--user={USERS[who]}', '--default-character-set=utf8mb4']
    if not table:
        args += ['-N', '-B']
    if who != 'root':
        args.append(f'--database={DB}')
    r = subprocess.run(args, input=sql.encode('utf-8'), capture_output=True, env=dict(os.environ, MYSQL_PWD=pw))
    return r.returncode, mask(r.stdout.decode('utf-8', 'replace'), s), mask(r.stderr.decode('utf-8', 'replace'), s)


def query(out, sql, who='rt'):
    rc, o, e = run(out, who, sql)
    if rc:
        raise RuntimeError(e.strip()[-400:])
    return [l.split('\t') for l in o.splitlines() if l]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('cmd', choices=['file', 'sql'])
    ap.add_argument('who', choices=list(USERS))
    ap.add_argument('arg')
    ap.add_argument('kv', nargs='*')
    a = ap.parse_args()
    if a.cmd == 'file':
        sql = open(a.arg, encoding='utf-8').read()
        s = secrets(a.out)
        q = lambda x: x.replace("'", "''")
        for k in ('srv_user', 'srv', 'gm', 'user', 'instance_token'):
            sql = sql.replace('{{' + k + '}}', q(s[k]))
        for kv in a.kv:
            k, v = kv.split('=', 1)
            sql = sql.replace('{{' + k + '}}', q(v))
    else:
        sql = a.arg
    rc, o, e = run(a.out, a.who, sql, table=(a.cmd == 'sql'))
    print(o, end='')
    if e.strip():
        print(e.strip()[-600:], file=sys.stderr)
    sys.exit(rc)


if __name__ == '__main__':
    main()

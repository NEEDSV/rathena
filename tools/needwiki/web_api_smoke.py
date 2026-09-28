"""NEED Wiki web-server smoke test: POST /api/wiki/report, GET /api/wiki/itemgroups and the
existing wiki routes, against a LOCAL web-server and a LOCAL test database only.

    python tools/needwiki/web_api_smoke.py --base http://127.0.0.1:18888 \
        --server-dir <dir the web-server runs in> --log <web-server stdout file> \
        --mariadb "<path to mariadb.exe>" --db test_needwiki_w4b --user ragnarok --password ...

Safety: the base URL must be a loopback address and the database name must start with
"test_" - the script ages, renames and inspects rows in that database.
"""
import argparse
import hashlib
import http.client
import ipaddress
import json
import os
import subprocess
import sys
import time
import urllib.parse

sys.stdout.reconfigure(encoding='utf-8')

LEGACY = {
    '정보 오류': 'incorrect_info',
    '내용 추가': 'missing_content',
    '오타자': 'typo',
    '오탈자': 'typo',
    '기타': 'other',
}
STABLE = ['incorrect_info', 'missing_content', 'typo', 'other']


class Smoke:
    def __init__(self, args):
        self.args = args
        url = urllib.parse.urlparse(args.base)
        host = url.hostname or ''
        if not (host == 'localhost' or ipaddress.ip_address(host).is_loopback):
            raise SystemExit(f'refusing non-loopback base URL {args.base}')
        if not args.db.startswith('test_'):
            raise SystemExit(f'refusing database {args.db!r}: the name must start with test_')
        self.host, self.port = host, url.port or 80
        self.results = []
        self.seq = 0

    # ---- plumbing -------------------------------------------------------------------
    def http(self, method, path, body=None, headers=None):
        conn = http.client.HTTPConnection(self.host, self.port, timeout=10)
        try:
            conn.request(method, path, body=body, headers=headers or {})
            r = conn.getresponse()
            return r.status, r.getheader('Content-Type'), r.read()
        finally:
            conn.close()

    def post_report(self, payload, raw=None, headers=None):
        body = raw if raw is not None else json.dumps(payload, ensure_ascii=False).encode('utf-8')
        h = {'Content-Type': 'application/json; charset=utf-8'}
        h.update(headers or {})
        status, ctype, data = self.http('POST', '/api/wiki/report', body, h)
        try:
            doc = json.loads(data.decode('utf-8'))
        except Exception:
            doc = None
        return status, ctype, doc, data

    def sql(self, query):
        cmd = [self.args.mariadb, '-h127.0.0.1', f'-u{self.args.user}', f'-p{self.args.password}',
               '--default-character-set=utf8mb4', '-N', '-B', self.args.db, '-e', query]
        out = subprocess.run(cmd, capture_output=True, check=True)
        return [line.split('\t') for line in out.stdout.decode('utf-8').splitlines() if line]

    def check(self, name, ok, detail=''):
        self.results.append((name, bool(ok), detail))
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f'  [{detail}]' if detail else ''))

    def age_rows(self):
        """Moves every stored report out of the 10-minute window (keeps the rows)."""
        self.sql('UPDATE needwiki_reports SET created_at = created_at - 3600')

    def unique(self, text):
        self.seq += 1
        return f'{text} #{self.seq} {time.time_ns()}'

    def expect(self, name, payload, status, code, raw=None, headers=None):
        st, ctype, doc, data = self.post_report(payload, raw, headers)
        got = doc.get('status') if isinstance(doc, dict) else data[:60]
        ok = st == status and got == code and (ctype or '').startswith('application/json')
        self.check(name, ok, f'{st} {got}')
        return doc if isinstance(doc, dict) else {}

    def stored(self, name, payload, want_type, want_title=None, want_message=None, want_raw=None):
        doc = self.expect(name, payload, 200, 'OK')
        rid = doc.get('id')
        if not rid:
            self.check(name + ' / row id', False, 'no id')
            return None
        rows = self.sql('SELECT report_type, HEX(report_type_raw), HEX(article_title), HEX(message), '
                        f'message_hash, remote_ip, article_id, status FROM needwiki_reports WHERE id={int(rid)}')
        if len(rows) != 1:
            self.check(name + ' / row', False, f'{len(rows)} rows')
            return rid
        rtype, raw_hex, title_hex, msg_hex, mhash, ip, article, status = rows[0]
        message = want_message if want_message is not None else payload.get('message', '').strip()
        title = want_title if want_title is not None else payload.get('article_title', payload.get('title', ''))
        raw = want_raw if want_raw is not None else (payload.get('report_type') or '')
        ok = (rtype == want_type
              and bytes.fromhex(msg_hex).decode('utf-8') == message
              and bytes.fromhex(title_hex).decode('utf-8') == title
              and bytes.fromhex(raw_hex).decode('utf-8') == raw
              and mhash == hashlib.sha256(message.encode('utf-8')).hexdigest()
              and ip == '127.0.0.1' and article == payload['article_id'] and status == '0')
        self.check(name + ' / DB row', ok, f'id={rid} type={rtype}')
        return rid

    # ---- report ----------------------------------------------------------------------
    def report_cases(self):
        self.sql('DELETE FROM needwiki_reports')
        base = {'article_id': 'home', 'article_title': 'NEED Wiki 대문'}

        n = 0
        for code in STABLE:
            self.stored(f'stable report_type {code}', dict(base, report_type=code,
                        message=self.unique('stable code 제보 테스트')), code)
            n += 1
        self.age_rows()
        for legacy, code in LEGACY.items():
            self.stored(f'legacy report_type {legacy} -> {code}', dict(base, report_type=legacy,
                        message=self.unique('레거시 유형 테스트')), code)
            n += 1
            if n % 4 == 0:
                self.age_rows()
        self.age_rows()

        # report_type missing / null -> other; the web viewer body {article_id, title, message}
        self.stored('report_type missing -> other', dict(base, message=self.unique('유형 없음')), 'other')
        self.stored('report_type null -> other', dict(base, report_type=None, message=self.unique('유형 null')),
                    'other', want_raw='')
        self.stored('web viewer body (title, no report_type)',
                    {'article_id': 'content_battleground', 'title': '전장', 'message': self.unique('web viewer report')},
                    'other', want_title='전장')
        self.stored('uppercase article id', {'article_id': 'egg_2011_RWC_SCROLL_KR', 'report_type': 'typo',
                    'message': self.unique('대문자 문서 id')}, 'typo', want_title='')
        self.age_rows()

        # unknown report_type
        self.expect('unknown report_type', dict(base, report_type='bug', message='unknown type test'), 400, 'INVALID_REPORT_TYPE')
        self.expect('numeric report_type', dict(base, report_type=3, message='numeric type test'), 400, 'INVALID_REPORT_TYPE')

        # JSON / UTF-8
        self.expect('broken JSON', None, 400, 'INVALID_JSON', raw=b'{"article_id": "home", "message": ')
        self.expect('JSON array', None, 400, 'INVALID_JSON', raw=b'[]')
        self.expect('invalid UTF-8 in a string', None, 400, 'INVALID_JSON',
                    raw=b'{"article_id":"home","report_type":"typo","message":"\xff\xfe broken utf8"}')
        self.expect('CP949 bytes in a string', None, 400, 'INVALID_JSON',
                    raw=b'{"article_id":"home","report_type":"typo","message":"' + '오타가 있어요'.encode('cp949') + b'"}')

        # message length (UTF-16 units after trimming, like the DLL)
        self.expect('message missing', dict(base, report_type='typo'), 400, 'MESSAGE_REQUIRED')
        self.expect('message empty (spaces)', dict(base, report_type='typo', message='   　 '), 400, 'MESSAGE_REQUIRED')
        self.expect('message 4 chars', dict(base, report_type='typo', message='abcd'), 400, 'MESSAGE_TOO_SHORT')
        self.expect('message 4 chars + spaces', dict(base, report_type='typo', message='  가나다라  '), 400, 'MESSAGE_TOO_SHORT')
        self.stored('message 5 chars', dict(base, report_type='typo', message='가나다라마'), 'typo')
        self.stored('message 1000 Hangul (3000 bytes)', dict(base, report_type='typo', message='가' * 999 + str(self.seq % 10)), 'typo')
        self.stored('message multi-line', dict(base, report_type='other', message='첫 줄\r\n둘째 줄\n\t셋째'), 'other')
        self.age_rows()
        self.expect('message 1001 chars', dict(base, report_type='typo', message='a' * 1001), 400, 'MESSAGE_TOO_LONG')
        self.expect('message control char', dict(base, report_type='typo', message='hello\u0001world'), 400, 'INVALID_MESSAGE')

        # article_id
        for label, value in [('missing', None), ('empty', ''), ('hyphen', 'a-b'), ('path', '../etc'),
                             ('129 chars', 'a' * 129), ('number', 12), ('Hangul', '대문')]:
            payload = dict(base, report_type='typo', message='article id test')
            if value is None:
                payload.pop('article_id')
            else:
                payload['article_id'] = value
            self.expect(f'article_id {label}', payload, 400, 'INVALID_ARTICLE_ID')
        self.stored('article_id 128 chars', dict(base, article_id='a' * 128, report_type='typo',
                    message=self.unique('128')), 'typo')
        self.expect('article_title 256 chars', dict(base, article_title='제' * 256, report_type='typo',
                    message='title length test'), 400, 'INVALID_ARTICLE_TITLE')
        self.expect('article_title number', dict(base, article_title=5, report_type='typo',
                    message='title type test'), 400, 'INVALID_ARTICLE_TITLE')
        self.age_rows()

        # payload size, before the body is read
        st, _, data = self.http('POST', '/api/wiki/report', b'{"a":"' + b'x' * 20000 + b'"}',
                                {'Content-Type': 'application/json'})
        self.check('body > 16 KiB -> 413', st == 413 and b'PAYLOAD_TOO_LARGE' in data, f'{st} {data[:40]}')
        conn = http.client.HTTPConnection(self.host, self.port, timeout=10)
        conn.putrequest('POST', '/api/wiki/report')
        conn.putheader('Content-Type', 'application/json')
        conn.putheader('Transfer-Encoding', 'chunked')
        conn.endheaders()
        chunk = b'{"article_id":"home","message":"chunked body"}'
        conn.send(b'%x\r\n%s\r\n0\r\n\r\n' % (len(chunk), chunk))
        r = conn.getresponse()
        data = r.read()
        conn.close()
        self.check('chunked body -> 411', r.status == 411 and b'LENGTH_REQUIRED' in data, f'{r.status} {data[:40]}')

        # duplicate: same ip + article + type + message within 10 minutes -> 200 DUPLICATE, no new row
        self.age_rows()
        dup = dict(base, report_type='typo', message=self.unique('중복 제보'))
        first = self.stored('duplicate: first send', dup, 'typo')
        before = int(self.sql('SELECT COUNT(*) FROM needwiki_reports')[0][0])
        doc = self.expect('duplicate: second send -> DUPLICATE', dup, 200, 'DUPLICATE')
        after = int(self.sql('SELECT COUNT(*) FROM needwiki_reports')[0][0])
        self.check('duplicate: same id, no new row', doc.get('id') == first and before == after, f'{doc.get("id")} {before}->{after}')
        self.expect('duplicate: other type is not a duplicate', dict(dup, report_type='other'), 200, 'OK')

        # rate limit: 5 per 10 minutes per IP
        self.age_rows()
        for i in range(5):
            self.expect(f'rate limit: report {i + 1}/5', dict(base, report_type='other', message=self.unique('rate')), 200, 'OK')
        self.expect('rate limit: 6th -> 429', dict(base, report_type='other', message=self.unique('rate')), 429, 'RATE_LIMITED')
        self.age_rows()
        self.expect('rate limit: window passed -> OK', dict(base, report_type='other', message=self.unique('after window')), 200, 'OK')

        # DB failure -> 500
        self.age_rows()
        self.sql('RENAME TABLE needwiki_reports TO needwiki_reports_hidden')
        try:
            self.expect('DB failure -> 500 DB_ERROR', dict(base, report_type='other', message=self.unique('db down')), 500, 'DB_ERROR')
        finally:
            self.sql('RENAME TABLE needwiki_reports_hidden TO needwiki_reports')

        st, _, _ = self.http('GET', '/api/wiki/report')
        self.check('GET /api/wiki/report is not a route', st == 404, str(st))

    # ---- itemgroups ------------------------------------------------------------------
    def itemgroups(self):
        yml = os.path.join(self.args.server_dir, 'db', 'import', 'wiki_item_group.yml')
        original = open(yml, 'rb').read()
        expected = {}
        cur = None
        for line in original.decode('cp949').split('\n'):
            if line.startswith('  - Id: '):
                cur = line[8:].strip()
            elif line.startswith('    Name: ') and cur:
                expected[cur] = line[10:].strip()

        st, ctype, data = self.http('GET', '/api/wiki/itemgroups')
        try:
            doc = json.loads(data.decode('utf-8'))
        except Exception as e:
            doc = None
        self.check('itemgroups 200 JSON', st == 200 and (ctype or '').startswith('application/json'), f'{st} {ctype}')
        self.check('itemgroups 21 groups, names == CP949 source decoded',
                   isinstance(doc, dict) and len(doc) == 21 and doc == expected,
                   f'{len(doc) if isinstance(doc, dict) else doc}')
        self.check('itemgroups schema {id: string}', isinstance(doc, dict) and all(isinstance(v, str) for v in doc.values()))
        st2, _, data2 = self.http('GET', '/api/wiki/itemgroups?group_id=Lhz_boss_card')
        self.check('itemgroups cached body identical', st2 == 200 and data2 == data)

        def swap(content, label, want_status, want_name=None):
            open(yml, 'wb').write(content)
            t = os.path.getmtime(yml) + 5
            os.utime(yml, (t, t))
            st, _, body = self.http('GET', '/api/wiki/itemgroups')
            ok = st == want_status
            if ok and want_name:
                ok = json.loads(body.decode('utf-8')).get('Lhz_boss_card') == want_name
            self.check(label, ok, f'{st}')

        try:
            changed = original.replace('생던 진보 카드'.encode('cp949'), '생던 진보 카드 변경'.encode('cp949'))
            swap(changed, 'itemgroups reloads when the file changes', 200, '생던 진보 카드 변경')
            swap(original.decode('cp949').encode('utf-8'), 'itemgroups UTF-8 re-saved file -> 500', 500)
            swap(b'\xef\xbb\xbf' + original, 'itemgroups BOM file -> 500', 500)
            with_en = original.replace(b'    Name: ' + '생던 진보 카드'.encode('cp949'),
                                       b'    Name: ' + '생던 진보 카드'.encode('cp949') + b'\n    NameEN: Test Boss Cards', 1)
            swap(with_en, 'itemgroups with NameEN keeps the KR schema', 200, '생던 진보 카드')
        finally:
            open(yml, 'wb').write(original)
            t = time.time() + 10
            os.utime(yml, (t, t))
        st, _, data3 = self.http('GET', '/api/wiki/itemgroups')
        self.check('itemgroups restored', st == 200 and data3 == data)

    # ---- existing routes ---------------------------------------------------------------
    def regression(self):
        cases = [
            ('GET', '/api/wiki/auth', None, {}, 400, b'INVALID_TOKEN'),
            ('POST', '/api/wiki/bootstrap/start', b'', {}, 401, b'INVALID_TOKEN'),
            ('GET', '/api/wiki/showgroup?group_id=Lhz_boss_card', None, {}, 401, b'INVALID_TOKEN'),
            ('GET', '/api/wiki/showgroup?group_id=bad%20id', None, {}, 400, b'Invalid group_id'),
            ('GET', '/api/wiki/showitem?item_id=501', None, {}, 401, b'INVALID_TOKEN'),
            ('GET', '/api/wiki/navi?map=prontera&x=1&y=2&name=test', None, {}, 401, b'INVALID_TOKEN'),
            ('GET', '/api/wiki/test?msg=hello', None, {}, 401, b'INVALID_TOKEN'),
        ]
        for method, path, body, headers, want, marker in cases:
            st, _, data = self.http(method, path, body, headers)
            self.check(f'{method} {path.split("?")[0]} -> {want}', st == want and marker in data, f'{st} {data[:30]}')

        token = hashlib.sha256(b'w4b-smoke-token-%d' % time.time_ns()).hexdigest()
        auth = {'Authorization': 'Bearer ' + token}
        st, _, data = self.http('GET', '/api/wiki/auth', None, auth)
        self.check('auth unknown token -> 401 NOT_BOUND', st == 401 and b'NOT_BOUND' in data, f'{st} {data[:40]}')
        st, _, data = self.http('POST', '/api/wiki/bootstrap/start', b'', auth)
        doc = json.loads(data) if st == 200 else {}
        self.check('bootstrap/start -> 200 WAITING + 8-digit code',
                   st == 200 and doc.get('status') == 'WAITING' and len(doc.get('code', '')) == 8, f'{st}')
        st, _, data = self.http('GET', '/api/wiki/auth', None, auth)
        self.check('auth after bootstrap -> WAITING', st == 200 and b'WAITING' in data, f'{st} {data[:40]}')
        st, _, data = self.http('GET', '/api/wiki/showgroup?group_id=Lhz_boss_card', None, auth)
        self.check('showgroup with token, no map-server -> 503', st == 503 and b'SERVER_UNAVAILABLE' in data, f'{st} {data[:40]}')
        self.sql("DELETE FROM needwiki_sessions WHERE token_hash=SHA2('%s',256)" % token)

    def log_check(self):
        if not self.args.log:
            return
        text = open(self.args.log, 'rb').read().decode('cp949', 'replace')
        stored = text.count('[NeedWiki] report stored id=')
        leaked = any(marker in text for marker in ('stable code', 'web viewer report', '가나다라마', 'NEED Wiki 대문'))
        self.check('log: one line per stored report, no title/message text', stored > 0 and not leaked, f'stored lines={stored}')

    def run(self):
        if self.args.log_only:
            # The web-server's stdout is block-buffered when redirected to a file, so the
            # log is checked after the server has been stopped.
            self.log_check()
        else:
            self.itemgroups()
            self.report_cases()
            self.regression()
        failed = [r for r in self.results if not r[1]]
        print(f'\n{len(self.results) - len(failed)}/{len(self.results)} passed')
        return 1 if failed else 0


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--base', default='http://127.0.0.1:18888')
    p.add_argument('--server-dir', required=True)
    p.add_argument('--log', help='web-server stdout file (used with --log-only)')
    p.add_argument('--log-only', action='store_true', help='only check the log, after stopping the server')
    p.add_argument('--mariadb', required=True)
    p.add_argument('--db', required=True)
    p.add_argument('--user', required=True)
    p.add_argument('--password', required=True)
    return Smoke(p.parse_args()).run()


if __name__ == '__main__':
    sys.exit(main())

"""NEED Wiki server data policy tests (no server, no database).

    python tools/needwiki/test_needwiki_server.py [-v]

Covers the item group DB source policy (CP949, no BOM, LF), the KR group names against the
wiki labels (when the NeedWiki checkout is next to this repo), the wiki msg ids in the KR and
EN tables (presence, placeholders, no id collision) and the call sites that must not return.
"""
import collections
import json
import glob
import os
import re
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
YML = os.path.join(ROOT, 'db', 'import', 'wiki_item_group.yml')
NEEDWIKI = os.environ.get('NEEDWIKI_ROOT', os.path.join(ROOT, '..', 'NeedWiki'))

# KR table load order: map_msg_need_summer.conf imports map_msg.conf first (which imports
# import/map_msg_eng_conf.txt at its end), then its own rows override.
KR_TABLES = ['conf/msg_conf/map_msg.conf', 'conf/msg_conf/import/map_msg_eng_conf.txt',
             'conf/msg_conf/map_msg_need_summer.conf']
EN_TABLE = 'conf/msg_conf/map_msg_need_en.conf'
WIKI_MSG_IDS = [1541, 1542, 1543, 1544, 1545, 1598, 1604, 1605, 1606, 1607, 1608, 1609, 1610, 1611]


def read_bytes(path):
    with open(path, 'rb') as f:
        return f.read()


def read_text(path, encoding='utf-8'):
    with open(path, encoding=encoding) as f:
        return f.read()


PRINTF = re.compile(r'%(?:%|[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|z|j|t|L|I64|I32)?[diouxXeEfgGaAcspn])')


def read_msg_table(path):
    rows = {}
    for raw in read_bytes(os.path.join(ROOT, path)).split(b'\n'):
        m = re.match(rb'^(\d+):\s?(.*?)\r?$', raw)
        if m:
            rows.setdefault(int(m.group(1)), []).append(m.group(2))
    return rows


def parse_groups(text):
    groups = collections.OrderedDict()
    cur = None
    for line in text.split('\n'):
        m = re.match(r'^  - Id: (.*)$', line)
        if m:
            cur = m.group(1).strip()
            groups[cur] = {}
            continue
        m = re.match(r'^    (Name|NameEN): ?(.*)$', line)
        if m and cur is not None:
            groups[cur][m.group(1)] = m.group(2)
    return groups


class ItemGroupSourcePolicy(unittest.TestCase):
    raw = read_bytes(YML)

    def test_no_bom(self):
        self.assertFalse(self.raw.startswith(b'\xef\xbb\xbf'))

    def test_cp949_decodable(self):
        self.raw.decode('cp949')

    def test_not_saved_as_utf8(self):
        self.assertTrue(any(b >= 0x80 for b in self.raw), 'expected Korean content')
        with self.assertRaises(UnicodeDecodeError):
            self.raw.decode('utf-8')

    def test_lf_line_endings(self):
        self.assertNotIn(b'\r', self.raw)

    def test_header(self):
        text = self.raw.decode('cp949')
        self.assertIn('Type: NEED_WIKI_ITEM_GROUP_DB', text)
        self.assertIn('Version: 1', text)

    def test_groups(self):
        groups = parse_groups(self.raw.decode('cp949'))
        self.assertEqual(len(groups), 21)
        for gid, fields in groups.items():
            with self.subTest(group=gid):
                self.assertRegex(gid, r'^[A-Za-z0-9_-]{1,64}$')
                name = fields.get('Name', '')
                self.assertTrue(name)
                self.assertFalse(re.search(r'[\x00-\x1f\x7f]', name), f'control character in Name {name!r}')
                self.assertEqual(name, name.strip())
                if 'NameEN' in fields and fields['NameEN'] not in ('', '~'):
                    self.assertRegex(fields['NameEN'], r'^[\x20-\x7e]+$')

    def test_known_defects_are_gone(self):
        text = self.raw.decode('cp949')
        self.assertNotIn('펠\t록', text)
        self.assertNotIn('국오아', text)


@unittest.skipUnless(os.path.isdir(os.path.join(NEEDWIKI, 'articles')), 'NeedWiki checkout not found')
class ItemGroupNamesMatchWikiLabels(unittest.TestCase):
    def test_labels(self):
        groups = parse_groups(read_bytes(YML).decode('cp949'))
        labels = collections.defaultdict(set)
        unlabelled = []
        for path in glob.glob(os.path.join(NEEDWIKI, 'articles', '*.json')):
            text = read_text(path, 'utf-8-sig')
            for gid, label in re.findall(r'\[ITEMGROUP:([^|\]]+)\|([^\]]*)\]', text):
                labels[gid].add(label.strip())
            unlabelled += re.findall(r'\[ITEMGROUP:([^|\]]+)\]', text)
        self.assertEqual(unlabelled, [])
        self.assertEqual(sorted(labels), sorted(groups))          # every group is used, no unknown group
        for gid, used in labels.items():
            with self.subTest(group=gid):
                # one label per group (a group used in several articles reads the same everywhere) == yml Name
                self.assertEqual(sorted(used), [groups[gid]['Name']])
        names = [g['Name'] for g in groups.values()]
        self.assertEqual(len(names), len(set(names)), 'two groups share a display name')   # e.g. Time_boots vs Time_boots_


class ItemGroupNameEN(unittest.TestCase):
    """W5: every group has an English display name for EN sessions (the map-server's ITEMGROUP chat)."""
    groups = parse_groups(read_bytes(YML).decode('cp949'))

    def test_nameen_present_ascii_unique(self):
        self.assertEqual(len(self.groups), 21)
        names = []
        for gid, fields in self.groups.items():
            with self.subTest(group=gid):
                en = fields.get('NameEN', '')
                self.assertTrue(en, 'NameEN missing or empty')
                self.assertRegex(en, r'^[\x20-\x7e]+$')
                self.assertEqual(en, en.strip())
                names.append(en)
        self.assertEqual(len(names), len(set(names)), 'two groups share an English name')
        self.assertNotEqual(self.groups['Time_boots']['NameEN'], self.groups['Time_boots_']['NameEN'])

    @unittest.skipUnless(os.path.isfile(os.path.join(NEEDWIKI, 'i18n', 'en', 'glossary.json')), 'NeedWiki glossary not found')
    def test_nameen_equals_wiki_glossary(self):
        # the wiki's EN ITEMGROUP labels must equal NameEN (NeedWiki validator); both come from its glossary
        glossary = json.loads(read_text(os.path.join(NEEDWIKI, 'i18n', 'en', 'glossary.json')))['itemgroups']
        self.assertEqual(sorted(glossary), sorted(self.groups))
        for gid, fields in self.groups.items():
            with self.subTest(group=gid):
                self.assertEqual(fields['NameEN'], glossary[gid]['en'])
                self.assertEqual(fields['Name'], glossary[gid]['kr'])


class WikiMessages(unittest.TestCase):
    kr_parts = [read_msg_table(p) for p in KR_TABLES]
    en = read_msg_table(EN_TABLE)

    def kr(self, msg_id):
        value = None
        for part in self.kr_parts:
            if msg_id in part:
                value = part[msg_id][-1]
        return value

    def test_ids_present_with_same_placeholders(self):
        for msg_id in WIKI_MSG_IDS:
            with self.subTest(id=msg_id):
                kr = self.kr(msg_id)
                self.assertIsNotNone(kr, 'no KR row')
                self.assertIn(msg_id, self.en, 'no EN row')
                en = self.en[msg_id][-1]
                self.assertEqual(PRINTF.findall(en.decode('ascii')), PRINTF.findall(kr.decode('cp949')))

    def test_en_rows_are_ascii(self):
        for msg_id in WIKI_MSG_IDS:
            with self.subTest(id=msg_id):
                self.en[msg_id][-1].decode('ascii')

    def test_approved_en_wording(self):
        want = {
            1541: 'Too many Wiki verification attempts. Please try again later.',
            1542: 'That Wiki verification code does not exist or has expired.',
            1543: 'An error occurred while processing Wiki verification.',
            1544: 'That Wiki verification code has already been used.',
            1545: 'Wiki verification complete.',
            1598: 'Usage: @wikicode <code>',
            1607: '[NEED Wiki] That item does not exist.',
            1609: '[NEED Wiki] %s.',
            1610: '[NEED Wiki] %s',
        }
        for msg_id, text in want.items():
            with self.subTest(id=msg_id):
                self.assertEqual(self.en[msg_id][-1].decode('ascii'), text)

    def test_usage_id_1598_is_defined_once(self):
        self.assertEqual(sum(len(part.get(1598, [])) for part in self.kr_parts), 1)
        self.assertEqual(len(self.en.get(1598, [])), 1)
        self.assertEqual(self.kr(1598).decode('cp949'), '사용법: @wikicode <code>')

    def test_new_en_ids_not_duplicated(self):
        for msg_id in (1541, 1542, 1543, 1544, 1545, 1598, 1607, 1609, 1610):
            with self.subTest(id=msg_id):
                self.assertEqual(len(self.en[msg_id]), 1)


class CallSites(unittest.TestCase):
    def src(self, rel):
        return read_bytes(os.path.join(ROOT, rel)).decode('utf-8', 'replace')

    def test_wikicode_usage_uses_msg_table(self):
        text = self.src('src/map/atcommand.cpp')
        body = text[text.index('ACMD_FUNC(wikicode)'):]
        body = body[:body.index('\n}')]
        self.assertIn('msg_txt(sd, 1598)', body)
        self.assertNotIn('clif_displaymessage(fd, "Usage: @wikicode', body)

    def test_group_chat_has_no_utf8_round_trip(self):
        text = self.src('src/map/needwiki.cpp')
        self.assertNotRegex(text, r'needwiki_utf8_to_cp949\(\s*group')
        self.assertIn('needwiki_item_group::display_name(group.source, sd->need_lang == NEED_LANG_EN)', text)

    def test_item_group_path_uses_db_path(self):
        for rel in ('src/map/needwiki.cpp', 'src/web/needwiki_controller.cpp'):
            with self.subTest(file=rel):
                text = self.src(rel)
                self.assertNotIn('"db/import/wiki_item_group.yml"', text)
                self.assertIn('needwiki_item_group::db_file(db_path)', text)

    def test_report_route_registered(self):
        self.assertIn('http_server->Post("/api/wiki/report", needwiki_report);', self.src('src/web/web.cpp'))


class ReportSchema(unittest.TestCase):
    def test_main_and_upgrade_define_the_same_table(self):
        main = read_text(os.path.join(ROOT, 'sql-files', 'main.sql'))
        upgrade = read_text(os.path.join(ROOT, 'sql-files', 'upgrades', 'upgrade_20260926.sql'))

        def table(text):
            m = re.search(r'CREATE TABLE IF NOT EXISTS `needwiki_reports` \(.*?\) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;', text, re.S)
            self.assertIsNotNone(m)
            return m.group(0).replace('\r\n', '\n')

        self.assertEqual(table(main), table(upgrade))
        for key in ('KEY `created_at`', 'KEY `remote_ip_created_at` (`remote_ip`,`created_at`)',
                    'KEY `article_id`', 'KEY `report_type`'):
            self.assertIn(key, table(main))


if __name__ == '__main__':
    unittest.main()

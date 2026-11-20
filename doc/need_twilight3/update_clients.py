"""Surgically patch skill 498, validate in Lua 5.1, and make KR/EN GPFs.

Uses the local KR main worktree and the established EN working tree. Does not
replace installed GRFs or publish patch lists. --check verifies existing output.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'doc/need_twilight3/client'
REL = Path('data/luafiles514/lua files/skillinfoz/skilldescript.lub')
GRF_NAME = str(REL).replace('/', '\\').encode('ascii')
KR = ROOT / 'generated/twilight3/client-kr'
EN = Path('F:/need/need_client/NEEDResource')
sys.path.insert(0, str(ROOT / 'generated/twilight3/python'))
from lupa.lua51 import LuaRuntime

spec = importlib.util.spec_from_file_location('english_builder', EN / 'en_build/build_english.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
sys.path.insert(0, str(ROOT / 'tools/need_enchant'))
from nelib import grf_read, grf_table

BLOCK = re.compile(rb'\[SKID\.AM_TWILIGHT3\]\s*=\s*\{.*?\}', re.S)
LITERAL = re.compile(rb'"(?:\\.|[^"\\])*"')
sha = lambda b: hashlib.sha256(b).hexdigest()


def vm_entries(data):
    lua = LuaRuntime(encoding=None)
    lua.execute(b'SKID=setmetatable({}, {__index=function(t,k) return k end})\n' + data)
    return {k: dict(v.items()) for k, v in lua.globals().SKILL_DESCRIPT.items()}


def change(data, lang):
    m = BLOCK.search(data)
    assert m, 'skill 498 block missing'
    old = m[0]
    lines = LITERAL.findall(old)
    if lang == 'KR':
        replacements = {
            0: '트와이라이트 파머시 III',
            1: '^777777습득조건 : 파머시 10, 영혼상태, 태권계열 파티원^000000',
            4: 'SP200을 소모해서 염산병 50개와 화염병 50개의 제조를 시도합니다.',
            5: '완제품 알코올 50개, 빈병 100개, 죽지않는심장 50개, 투명한천조각 50개와',
            6: '약사발 200개와 각 제조법의 제조책이 필요합니다. 기존 성공률, 시전 및 사용 조건은 동일합니다.^000000',
        }
        encoding = 'cp949'
    else:
        replacements = {
            0: 'Twilight Pharmacy III',
            1: '^777777Requirement : Pharmacy 10, Soul Link state, Taekwon-class party member^000000',
            4: 'Consumes 200 SP to attempt to craft 50 Acid Bottles and 50 Bottle Grenades.',
            5: 'Requires 50 pre-made Alcohol, 100 Empty Bottles, 50 Immortal Hearts and 50 Fabric.',
            6: 'Also requires 200 Medicine Bowls and both creation guides. Success rates, casting and usage requirements are unchanged.^000000',
        }
        encoding = 'utf-8'
    assert len(lines) == 10, (lang, len(lines))
    n = 0

    def replace(match):
        nonlocal n
        idx = n
        n += 1
        return ('"' + replacements[idx] + '"').encode(encoding) if idx in replacements else match[0]

    new = LITERAL.sub(replace, old)
    result = data[:m.start()] + new + data[m.end():]
    # Every byte outside the target entry, and every non-string byte inside, is preserved.
    assert LITERAL.sub(b'""', old) == LITERAL.sub(b'""', new)
    assert data.count(b'\n') == result.count(b'\n')
    before, after = vm_entries(data), vm_entries(result)
    assert before.keys() == after.keys()
    assert [k for k in before if before[k] != after[k]] in ([b'AM_TWILIGHT3'], [])
    assert before[b'AM_TWILIGHT3'][9] == after[b'AM_TWILIGHT3'][9]
    assert before[b'AM_TWILIGHT3'][10] == after[b'AM_TWILIGHT3'][10]
    assert b'100 Alcohol' not in new and '알코올 100'.encode('cp949') not in new
    return result, old, new


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for lang, tree in [('KR', KR), ('EN', EN)]:
        path = tree / REL
        current = path.read_bytes()
        backup = OUT / lang / 'skilldescript.before.lub'
        if not backup.exists():
            assert not args.check
            backup.parent.mkdir(parents=True, exist_ok=True)
            backup.write_bytes(current)
        baseline = backup.read_bytes()
        desired, old, new = change(baseline, lang)
        if args.check:
            assert current == desired, f'{lang} working source drift'
        else:
            assert current in (baseline, desired), f'{lang} unrelated edits; refusing overwrite'
            path.write_bytes(desired)
        destination = OUT / lang / REL
        patch = OUT / f'261120_twilight3_{lang.lower()}.gpf'
        if args.check:
            assert destination.read_bytes() == desired
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(desired)
            builder.build_grf(str(patch), [(GRF_NAME, desired)])
        assert builder.verify_grf(str(patch)) == 1
        assert len(grf_table(str(patch))) == 1
        assert grf_read(str(patch), GRF_NAME.decode('ascii')) == desired
        encoding = 'cp949' if lang == 'KR' else 'utf-8'
        (OUT / lang / 'entry.before.txt').write_text(
            old.decode(encoding).replace('\r\n', '\n'), 'utf-8', newline='\n')
        (OUT / lang / 'entry.after.txt').write_text(
            new.decode(encoding).replace('\r\n', '\n'), 'utf-8', newline='\n')
        manifest[lang] = {
            'source': str(path), 'before_sha256': sha(baseline), 'after_sha256': sha(desired),
            'encoding': encoding, 'changed_skids': ['AM_TWILIGHT3'],
            'lua51_load': 'PASS', 'other_entries_byte_identical': True,
            'patch': str(patch), 'patch_sha256': sha(patch.read_bytes()),
            'gpf_roundtrip': 'PASS', 'runtime_tooltip': 'NOT_RUN',
        }
    # The pre-existing EN working source does not match the historical builder's
    # hash chain. Report the mismatch without rewriting stamps or bypassing it.
    errors = []
    chain_ok = builder.skillinfoz_stamp_ok(str(REL), errors)
    manifest['EN']['legacy_builder_stamp_chain'] = 'PASS' if chain_ok else 'FAIL'
    manifest['EN']['legacy_builder_errors'] = errors
    stamp = json.loads(Path(builder.SKILLINFOZ_STAMP).read_text('utf-8'))
    expected = stamp['generated']['skilldescript.lub']['sha256']
    for path in builder.SKILLINFOZ_SUPERSESSIONS:
        record = json.loads(Path(path).read_text('utf-8'))
        item = record.get('files', {}).get('skilldescript.lub', {})
        if record.get('result') == 'PASS' and item.get('previous_sha256') == expected:
            expected = item['sha256']
    manifest['EN']['legacy_expected_sha256'] = expected
    manifest['EN']['legacy_builder_before_change'] = (
        'PASS' if expected == manifest['EN']['before_sha256'] else 'FAIL')
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2), 'utf-8')
    print('PASS KR/EN Lua 5.1, only skill 498 changed, 3-second casting lines retained, GPF roundtrips')
    print('Legacy EN builder stamp chain:', 'PASS' if chain_ok else 'FAIL (pre-existing source/hash drift; no stamp bypass)')
    print('Client source files updated; installed GRFs and patch lists untouched.')


if __name__ == '__main__':
    main()

"""Compile every Charleston script body using the existing isolated rAthena parser.

No server startup, SQL initialization, sockets, or NPC events. The probe must
have been prepared with doc/need_doram/prepare_parser_probe.py and built with
its companion .cmd; verify its parser source still matches this checkout.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs/reports/need-charleston-20261120"
PROBE = ROOT / "docs/reports/need-doram-release-gate-20261118/parser"
SCRIPT = ROOT / "npc/NEED/instances/CharlestonCrisis.txt"


def blocks(raw):
    token = re.compile(rb'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/|[{}]')
    header = rb'^(?:function\s+script\s+[^\r\n]+|(?:[\w@]+,\d+,\d+,\d+(?:,\d+)?|-)\tscript\t[^\r\n]+)\{'
    for match in re.finditer(header, raw, re.M):
        begin = match.end() - 1
        depth = 0
        for brace in token.finditer(raw, begin):
            if brace[0] == b'{':
                depth += 1
            elif brace[0] == b'}':
                depth -= 1
                if depth == 0:
                    yield raw[match.start():begin], raw[:begin].count(b'\n') + 1, raw[begin:brace.end()]
                    break
        else:
            raise AssertionError(f"Unclosed body at {begin}")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    current = (ROOT / "src/map/script.cpp").read_bytes()
    assert current.count(b'\tmapreg_init();') == 1
    expected = current.replace(b'\tmapreg_init();', b'\t// Isolated parser only: do not initialize SQL-backed mapreg.')
    assert (PROBE / "script_probe.cpp").read_bytes() == expected, "Rebuild isolated parser for current source"
    exe = PROBE / "parser_probe.exe"
    assert exe.stat().st_mtime >= (PROBE / "script_probe.cpp").stat().st_mtime
    raw = SCRIPT.read_bytes()
    raw.decode('cp949')  # Strict encoding and newline preservation checks.
    assert b'\n' not in raw.replace(b'\r\n', b'')
    records = []
    for i, (header, line, body) in enumerate(blocks(raw)):
        path = OUT / f"block_{i:03}.txt"
        path.write_bytes(body)
        records.append(dict(header=header.decode('cp949'), line=line, body=str(path), sha256=hashlib.sha256(body).hexdigest()))
    assert len(records) == len(re.findall(rb'^(?:function[ \t]+script[ \t]+|[^/\r\n]*\tscript\t)', raw, re.M)), "Missed NPC/function header"
    manifest = OUT / "manifest.txt"
    manifest.write_text('\n'.join(f'{r["line"]}|{r["body"]}' for r in records), encoding='utf-8')
    result = subprocess.run([str(exe), str(manifest)], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / "parser-run.log").write_bytes(result.stdout)
    print(result.stdout.decode('utf-8', errors='replace').strip())
    assert result.returncode == 0, f"Parser exited {result.returncode}"
    # A malformed-expression control establishes that the real parser rejects errors.
    bad = OUT / "negative-control.txt"
    bad.write_bytes(b'{ .@value = ; }')
    neg_manifest = OUT / "negative-manifest.txt"
    neg_manifest.write_text('1|' + str(bad), encoding='utf-8')
    negative = subprocess.run([str(exe), str(neg_manifest)], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / "parser-negative.log").write_bytes(negative.stdout)
    assert negative.returncode == 2, f"Unexpected negative-control exit {negative.returncode}"
    print('PASS: malformed-expression control rejected (exit 2)')
    (OUT / "parser-evidence.json").write_text(json.dumps(dict(
        script_sha256=hashlib.sha256(raw).hexdigest(),
        parser_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
        source_matches=True, blocks=records, exit=result.returncode, negative_exit=negative.returncode,
        scope='Actual parse_script compiler; no NPC headers/events/runtime, SQL or sockets'
    ), ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()

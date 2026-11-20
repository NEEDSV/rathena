"""Audit the active NEED encounter and compile its blocks with the real parser.

This does not start a server or claim to test combat, timers, or client behavior.
The parser executable is the existing isolated, SQL-free parser probe.
"""
from pathlib import Path
import argparse
import difflib
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = "npc/NEED/instances/SealedShrine.txt"
OUT = ROOT / "docs/reports/need-sealed-shrine-20261120"


def blocks(raw):
    token = re.compile(rb'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/|[{}]')
    for match in re.finditer(rb'^[^\r\n]*\tscript\t[^\r\n]*\{', raw, re.M):
        begin = match.end() - 1
        depth = 0
        for brace in token.finditer(raw, begin):
            if brace[0] == b'{':
                depth += 1
            elif brace[0] == b'}':
                depth -= 1
                if not depth:
                    yield raw[:begin].count(b'\n') + 1, raw[begin:brace.end()]
                    break
        else:
            raise ValueError("Unclosed script block")


def audit(parser):
    OUT.mkdir(parents=True, exist_ok=True)
    raw = (ROOT / SCRIPT).read_bytes()
    text = raw.decode("cp949")
    original = subprocess.check_output(["git", "show", "HEAD:" + SCRIPT], cwd=ROOT)
    original = original.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
    before = original.decode("cp949")
    control = text.split("\tscript\tcontrol_baphomet\t", 1)[1].split("\n}", 1)[0]
    enable = control.split("OnEnable:", 1)[1].split("OnTimer3000:", 1)[0]
    timer = control.split("OnTimer3000:", 1)[1].split("OnMyMobDead:", 1)[0]
    assert 'initnpctimer;' in enable and '"NPC_INVINCIBLE"' not in enable
    assert 'stopnpctimer;' in timer and 'mobcount(' in timer
    assert '79,64,150,1929,"NPC_INVINCIBLE",1,0,0,ET_HELP,0;' in timer
    assert 'stopnpctimer;' in control.split("OnDisable:", 1)[1].split("OnEnable:", 1)[0]
    # Keep every other encounter block, including seal costs and repeat timers, byte-identical.
    control_start = raw.index(b'2@cata,3,3,0\tscript\tcontrol_baphomet\t')
    control_end = raw.index(b'2@cata,2,2,0\tscript\tins_2f_hero_broad2\t', control_start)
    old_start = original.index(b'2@cata,3,3,0\tscript\tcontrol_baphomet\t')
    old_end = original.index(b'2@cata,2,2,0\tscript\tins_2f_hero_broad2\t', old_start)
    assert raw[:control_start] == original[:old_start]
    assert raw[control_end:] == original[old_end:]
    assert raw.count(b'\n') == raw.count(b'\r\n')

    # The loader uses re first, then import. Negative IDs are global skills.
    rows = []
    for filename in ("db/re/mob_skill_db.txt", "db/import/mob_skill_db.txt"):
        for line, content in enumerate((ROOT / filename).read_bytes().splitlines(), 1):
            content = content.split(b'//', 1)[0].strip()
            fields = content.split(b',')
            if len(fields) != 19 or fields[0] not in (b'1929', b'-1', b'-2', b'-3'):
                continue
            rows.append({"file": filename, "line": line, "row": content.decode("ascii")})
    invincible = [r for r in rows if r["row"].split(',')[3] == '685']
    assert not invincible, invincible

    records = []
    for i, (line, body) in enumerate(blocks(raw)):
        target = OUT / f"block_{i:03d}.txt"
        target.write_bytes(body)
        records.append({"file": SCRIPT, "line": line, "body": str(target),
                        "sha256": hashlib.sha256(body).hexdigest()})
    assert len(records) >= 20
    manifest = OUT / "manifest.txt"
    manifest.write_text("\n".join(f'{r["line"]}|{r["body"]}' for r in records), encoding="utf-8")
    parser_result = "NOT RUN"
    if parser:
        parser = parser.resolve()
        # Confirm that the reused parser source differs only by disabling SQL mapreg init.
        probe = parser.parent / "script_probe.cpp"
        expected = (ROOT / "src/map/script.cpp").read_bytes().replace(
            b'\tmapreg_init();', b'\t// Isolated parser only: do not initialize SQL-backed mapreg.')
        assert probe.read_bytes() == expected, "Parser probe source is stale"
        result = subprocess.run([str(parser), str(manifest)], cwd=ROOT, capture_output=True)
        (OUT / "parser.log").write_bytes(result.stdout + result.stderr)
        output = (result.stdout + result.stderr).decode("utf-8", errors="replace")
        assert result.returncode == 0, output
        assert f"REAL_PARSER_RESULT blocks={len(records)} failures=0" in output, output
        assert "[Error]" not in output, output
        parser_result = f"PASS: {len(records)} actual script blocks; SQL and sockets not initialized"
        print(parser_result)
    diff = ''.join(difflib.unified_diff(before.splitlines(True), text.splitlines(True),
                                      fromfile="a/" + SCRIPT, tofile="b/" + SCRIPT))
    (OUT / "minimal.diff").write_text(diff, encoding="utf-8", newline="")
    summary = {"script": SCRIPT, "sha256": hashlib.sha256(raw).hexdigest(),
               "encoding": "CP949 / CRLF preserved", "parser": parser_result,
               "relevant_active_skill_rows": rows, "active_invincible_rows": invincible,
               "change_scope": "control_baphomet only; all other script blocks byte-identical",
               "live_combat_timestamps": "NOT TESTED: no isolated running map server or client session",
               "quest_3041": "Existing NEED seal block has no setquest/checkquest cooldown gate",
               "seal_cost": "Existing SP -50%, Eff_Stone 20000ms retained"}
    (OUT / "audit.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    (OUT / "blocks.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
    print("PASS: DB audit, encoding, timer placement and unchanged encounter blocks")
    print(f"Artifacts: {OUT}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--parser", type=Path)
    audit(ap.parse_args().parser)

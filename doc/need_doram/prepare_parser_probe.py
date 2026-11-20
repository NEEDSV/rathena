"""Prepare an isolated real-script-parser executable, never the server main loop.

Only generated copies are adapted: skip mapreg_init (which connects to SQL),
and replace the executable entrypoint. Production sources are not modified.
Link against the freshly built map-server objects; do not run map-server itself.
"""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs/reports/need-doram-release-gate-20261118/parser"
OUT.mkdir(parents=True, exist_ok=True)
source = (ROOT / "src/map/script.cpp").read_bytes()
assert source.count(b"\tmapreg_init();") == 1
(OUT / "script_probe.cpp").write_bytes(source.replace(b"\tmapreg_init();", b"\t// Isolated parser only: do not initialize SQL-backed mapreg."))
source = (ROOT / "src/map/map.cpp").read_bytes()
needle = b"int32 main( int32 argc, char *argv[] ){"
start = source.index(needle)
assert b"main_core<MapServer>" in source[start:]
driver = r'''
#include <fstream>
#include <iterator>
#include <sstream>
#include <cstdlib>
int32 main(int32 argc, char* argv[]) {
    // Intentionally no Core::start / MapServer::initialize / Sql_Init /
    // map_sql_init / socket_init / mapreg_init / NPC events or server loop.
    MapServer identity;
    global_core = &identity;
    malloc_init();
    db_init();
    timer_init();
    do_init_script();
    int count = 0, failures = 0;
    if (argc != 2) std::_Exit(10);
    std::ifstream manifest(argv[1]);
    if (!manifest) std::_Exit(11);
    std::string row;
    while (std::getline(manifest, row)) {
        auto split = row.find('|');
        int line = std::stoi(row.substr(0, split));
        std::string path = row.substr(split + 1);
        std::ifstream stream(path, std::ios::binary);
        std::string body((std::istreambuf_iterator<char>(stream)), {});
        auto* code = parse_script(body.c_str(), path.c_str(), line, SCRIPT_RETURN_EMPTY_SCRIPT);
        if (!code) {
            ++failures;
            printf("FAIL actual parse_script: %s\n", path.c_str());
        } else {
            ++count;
            script_free_code(code);
        }
    }
    printf("REAL_PARSER_RESULT blocks=%d failures=%d sql_initialized=0 sockets_initialized=0 server_started=0\n", count, failures);
    fflush(nullptr);
    std::_Exit(failures ? 2 : 0);
}
'''
(OUT / "map_probe.cpp").write_bytes(source[:start] + driver.encode("ascii"))

# Extract complete function/NPC script blocks, retaining original bytes/line numbers.
files = ["npc/NEED/need_doram_exchange.txt", "npc/NEED/shop.txt", "npc/NEED/need_beginner.txt"]
records = []
for filename in files:
    raw = (ROOT / filename).read_bytes()
    token = re.compile(rb'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/|[{}]')
    for match in re.finditer(rb'^(?:function\s+script\s+[^\r\n]+|(?:\w+,\d+,\d+,\d+|-)\tscript\t[^\r\n]+)\{', raw, re.M):
        begin = match.end() - 1
        depth = 0
        end = None
        for brace in token.finditer(raw, begin):
            if brace[0] == b'{': depth += 1
            elif brace[0] == b'}':
                depth -= 1
                if not depth:
                    end = brace.end()
                    break
        assert end, filename
        line = raw[:begin].count(b'\n') + 1
        body = raw[begin:end]
        target = OUT / f"block_{len(records):03d}.txt"
        target.write_bytes(body)
        records.append({"file": filename, "line": line, "body": str(target), "sha256": hashlib.sha256(body).hexdigest()})
assert len(records) >= 10, len(records)
(OUT / "manifest.txt").write_text("\n".join(str(r["line"]) + "|" + r["body"] for r in records), encoding="utf-8", newline="\n")
(OUT / "blocks.json").write_text(json.dumps(records, indent=2), encoding="utf-8")

objs = sorted((ROOT / ".vs/build/map-server/Win32/Debug").glob("*.obj"))
assert len(objs) >= 60
libs = ["ws2_32.lib", "advapi32.lib", "user32.lib", "shell32.lib", str(ROOT/".vs/build/ryml.lib"), str(ROOT/".vs/build/common.lib"), str(ROOT/".vs/build/libconfig.lib"),
        str(ROOT/"3rdparty/zlib/lib/Win32/zlib.lib"), str(ROOT/"3rdparty/pcre/lib/Win32/pcre8.lib"),
        str(ROOT/"3rdparty/mysql/lib/Win32/libmysql.lib")]
args = ["/NOLOGO", "/DEBUG", "/SUBSYSTEM:CONSOLE", "/MACHINE:X86", "/INCREMENTAL:NO", "/OUT:"+str(OUT/"parser_probe.exe"),
        str(OUT/"map_probe.obj"), str(OUT/"script_probe.obj")]
args += [str(p) for p in objs if p.name.lower() not in ("map.obj", "script.obj")]
args += libs
(OUT / "link.rsp").write_text("\n".join('"'+a+'"' for a in args), encoding="utf-8")
print(f"Prepared {len(records)} actual script blocks; generated-only driver. No production source edits.")

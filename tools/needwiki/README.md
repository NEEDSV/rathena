# NEED Wiki server tests

| file | what | needs |
|---|---|---|
| `run_tests.py` | runs the two tests below | a built solution (`.vs/build/ryml.lib`) |
| `test_needwiki_server.py` | item group DB source policy (CP949, no BOM, LF), KR group names vs wiki labels, wiki msg ids (KR/EN rows, placeholders, id 1598 defined once), call sites | nothing (the label check runs when `../NeedWiki` or `NEEDWIKI_ROOT` exists) |
| `needwiki_item_group_test.cpp` | `src/common/needwiki_item_group.hpp`: CP949 -> UTF-8 for the web API, unchanged CP949 bytes for the map chat, `NameEN` / KR fallback, UTF-8 / BOM file rejected | MSVC (`run_tests.py` compiles it) |
| `web_api_smoke.py` | `POST /api/wiki/report`, `GET /api/wiki/itemgroups` and the existing wiki routes against a running web-server, rows checked in the database | a LOCAL web-server and a database whose name starts with `test_` (the script refuses anything else) |

```
python tools/needwiki/run_tests.py
python tools/needwiki/web_api_smoke.py --server-dir <web-server dir> --mariadb <mariadb.exe> --db test_... --user ... --password ...
```

`wiki_item_group.yml` stays CP949 without a BOM. The web-server converts `Name` to UTF-8 when it builds
JSON; the map-server sends `Name` / `NameEN` to the client unchanged. `NameEN` (optional, printable ASCII)
is shown to English sessions; without it they see `Name`.

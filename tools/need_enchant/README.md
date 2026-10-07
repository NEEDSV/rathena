# NEED Unified Enchant V2 tooling

One master file describes every NEED enchant variant. The generator writes the server and client
data from it, and refuses to write anything when a gate fails.

| File | Role |
|---|---|
| `generate.py` | master + ClientId registry -> server yml, KR/EN EnchantList, ItemDBNameTbl, `variants.tsv`, `report.md` |
| `parity.py` | exact outcome table of a legacy NPC `rand()` threshold chain (the parity source of truth) |
| `check_parity.py` | re-derives every master `Parity` block from the scripts: threshold chains (with `WindowLine`), Sarah-style counted pick loops (`Loop`), reset splits/rewards, Mora target lists (`Source.Callsub`) |
| `import_legacy.py` | one-time importer of the LIVE NPCs into the master (Charleston, EP16.1, Mora, Sarah) |
| `install.py` | installs generator output (`--server` refused while servers run; `--client-kr/-en` loose files) |
| `clientcheck.py` | replays the client's `EnchantList_f` load in real Lua 5.1 (lupa.lua51) |
| `nelib.py` | GRF reader, Lua 5.1 chunk undump/redump, EnchantList decompiler, ItemDBNameTbl helpers |
| `selftest.py` | tooling self test (parity numbers, normalisation, registry, generator gates) |
| `fixtures/` | `build_fixture.py` builds `master_fixture.yml` from the LIVE scripts (test input only) |

Requirements: Python 3, PyYAML, lupa (with `lupa.lua51`), the official client `data.grf`.

## Files it owns

| Path | Content |
|---|---|
| `db/need/enchant_master.yml` | the master (Phase 2 onward) |
| `db/need/enchant_clientid_registry.yml` | **append-only** key -> ClientId (10001+). A removed variant moves its id to `Retired`, never reused |
| `db/import/item_enchant.yml` | generated NEED groups (stock rAthena format) |
| `db/need/enchant_rules.yml` | generated NEED rules (failures, conditions, reset outcomes), see `src/map/need_enchant.hpp` |
| client `data\luafiles514\lua files\enchant\enchantlist.lub` | official groups (decompiled, proven equivalent) + NEED groups, Lua text, KR and EN |
| client `data\luafiles514\lua files\itemdbnametbl.lub` | official keys + keys the NEED groups need; shared by KR and EN (ship once, in need_data.grf) |

## Master schema (Version 1)

```yaml
Header:
  Type: NEED_ENCHANT_MASTER
  Version: 1
Body:
  - Family: CHARLESTON_PLATE                 # one entry of the unified NPC menu
    Display: { KR: "...", EN: "..." }
    Variants:                                # one variant = one ClientId = one client Table
      - Key: CHARLESTON_PLATE_ACCEL          # [A-Z0-9_], permanent (registry)
        Display: { KR: "...", EN: "..." }
        Source: { Script: npc/..., Chain: 326 }   # informational / parity input
        TargetItems: [ Upgrade_Part_Plate ]  # AegisNames
        MinimumRefine: 0                     # client SetCondition + server
        MaximumRefine: 8                     # server rule only (0/absent = none)
        AllowRandomOptions: true
        Order: [ 3, 2, 1 ]                   # card[] indices
        Cost: { Zeny: 100000, Materials: [ { Item: Charleston_Parts, Amount: 1 } ] }   # default per slot
        Slots:
          - Slot: 3
            Cost: ...                        # optional override
            Options: [ { Enchant: Agility1, Weight: 10 }, ... ]   # integer weights, 1..100000 each
            Failures: [ { Result: FAIL_KEEP, Weight: 25 }, { Result: DESTROY, Weight: 5 } ]  # same denominator
            SuccessChance: 100000            # only without Failures (stock chance, 100000 base)
            MinimumRefine / Require / MaxSame
            Perfect: [ { Enchant: X, Cost: {...} } ]
            Upgrades: [ { Enchant: A, To: B, Cost: {...}, SuccessWeight: 80, Failures: [...] } ]
        Reset:                               # absent = no reset
          Cost: {...}
          Chance: 100000                     # stock chance (without Outcomes)
          RequireAllFilled: true
          Scope: [ 3, 2 ]
          Outcomes: [ { Result: SUCCESS, Weight: 79 }, { Result: DESTROY_WITH_REWARD, Weight: 21, Rewards: [...] } ]
        Caution: AUTO                        # or { KR: "...", EN: "..." } (EN must be ASCII)
```

Results: `FAIL_KEEP`, `CLEAR_SLOTS` (Slots), `CLEAR_ENCHANTS`, `REFINE_DOWN` (Amount | Random: true),
`DOWNGRADE` (upgrades, To), `DESTROY`, `REWARD` / `DESTROY_WITH_REWARD` (Rewards: Item, Amount, Weight),
`SUCCESS` (reset only). Optional `Message: <msgstringtable id>`.

## Commands

```
python tools/need_enchant/selftest.py
python tools/need_enchant/parity.py npc/re/merchants/enchan_verus.txt 326 101 310
python tools/need_enchant/generate.py --out <dir>                      # dry run, registry untouched
python tools/need_enchant/generate.py --out <dir> --commit-registry    # keep new ids
```

Server side check of generated files, without touching `db/` (harness build, `NEED_ENCHANT_TEST`):

```
map-server-enchanttest.exe --map-config <conf with another map_port> --need-enchant-check <item_enchant.yml> <enchant_rules.yml>
```

## Gates

`generate.py` exits 1 and writes nothing when any of these fails:

- master validation: names, weights, order and slots, the real card slot overlap, and results allowed per request type
- ClientId registry: append-only, ids 10001+, no clash with official client or server groups
- the decompiled official KR/EN EnchantList builds exactly the same `Table` as the original bytecode
- KR and EN client replay: `CheckFile` and `LoadAllData` true, zero MessageBox (one bad entry would break the whole client list)
- table count = official + NEED variants

Installing the output into `db/` and packing the client files into the GRFs are operator steps.

## Resource repos (Phase 2.1b)

- KR: `install.py --from <out> --repo-kr E:\tools\Need\NEEDResoruce` copies EnchantList + ItemDBNameTbl into the
  need_data.grf source tree (the operator packs need_data.grf).
- EN: `install.py --from <out> --repo-en E:\tools\Need\NEEDResoruce_EN` copies the EN EnchantList; then, in the EN repo,
  `python en_build\enchant_v2\verify_enchant_v2_enchantlist.py --write --tag <phase> --generated <out>` writes the
  supersession record that lets `build_english.py` accept the new hash. The Phase 4.18 validator and stamp are never
  run or edited.
- The generator's EN base is the pinned Phase 4.18 file `en_build\enchant_v2\base\enchantlist_phase418.lub` (its sha256
  must equal the 4.18 stamp's generated_hash), never the EN repo's live enchantlist.lub. Official inputs holding ids
  >= 10001 are refused, so generated output can never be fed back in.
- After an official client sync that changes data.grf's EnchantList, regenerate: the KR override in need_data.grf
  would otherwise hide the new official groups.

## Unified menu (Phase 3)

`python tools/need_enchant/menu_script.py` regenerates `npc/NEED/need_enchant_v2.txt` (CP949, CRLF) from the master
and the registry: data NPC `NEED_EnchantV2_Data` + function `F_NeedEnchantV2` ("my gear" inventory list and the full
catalogue; picking a group opens the 2025 Enchant UI; `callfunc "F_NeedEnchantV2", "<FAMILY>"` limits it to one family).
Regenerate after every master / registry change. The file is not in any conf until the Phase 3 LIVE gate.

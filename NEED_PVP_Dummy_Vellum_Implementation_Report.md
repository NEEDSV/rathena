# NEED PvP Dummy / Vellum 구현 보고서

- 작업일: 2026-11-15 (Asia/Seoul)
- 브랜치: `codex/need-pvp-dummy-vellum`
- 구현·계산 비교 기준: `d7fa1a369` 및 당시 존재하던 로컬 변경.
- 커밋 준비 시 HEAD: `064d16a08`. 기존 Laphine 변경은 이번 커밋에 포함하지 않는다.
- 결과: **로컬 구현 및 제한된 오프라인 검증 완료. 실제 서버·클라이언트 통합 검증 미실시.**
- 전체 회귀 테스트, 본서버 반영, push, merge, DB 마이그레이션, 서버/SQL 접속은 수행하지 않았다. 기존 서버 실행 파일도 교체하지 않았다.

## 1. 기존 시스템과 계산 경로 조사

| 근거 | 확인 사항 |
|---|---|
| `npc/NEED/need_zone_warp.txt:31` | 훈련장 이동은 `hero_tra,70,150`이다. |
| `npc/re/scripts_monsters.conf:11` | 구형 `tra_fild.txt`는 비활성, `tra_fild_123.txt`는 활성이다. |
| `npc/re/mobs/tra_fild_123.txt:11` | 현재 훈련장은 크기·속성·종족별 21064~21086을 고정 배치한다. 크기는 x40/50/60, y120/130/140, 다른 종류는 x65~95 부근이다. |
| `npc/re/mobs/tra_fild_123.txt:21` | HUMANP라고 적힌 주석과 달리 실제 인간형 소환 ID는 21070이다. |
| `db/re/mob_db.yml:107436` | 21070은 Demihuman, 중형/무속성1, 레벨100, HP20억, VIT0, DEF 기본0, Boss이다. |
| `db/re/mob_db.yml:107828` | 21087/21088의 Player_Human/Player_Doram 허수아비 DB가 이미 있으나 현재 훈련장 스폰에는 쓰이지 않는다. |

기존 스폰 파일에 전용 DPS 집계·HP 리셋·사망 이벤트는 없다. 일반 대미지 표시와 몬스터 사망/리스폰을 사용한다. 기존 스폰·수치·DB는 유지했다. 기존 플레이어 종족 DB를 그대로 소환하면 다른 종족 의존 효과까지 넓어지므로, 이번에는 별도 ID와 좁은 계산 분기를 사용했다.

- `src/map/map.hpp:323`의 종족 enum에는 `RC_DEMIHUMAN`, `RC_PLAYER_HUMAN`, `RC_PLAYER_DORAM`이 별도 인덱스로 존재한다. 실제 아이템 스크립트 표기는 `RC_Player_Human`/`RC_Player_Doram`이며 구형 `RC_Player`를 추가하지 않았다.
- `src/map/script_constants.hpp:3530`에서 해당 상수를 노출한다.
- `src/map/status.cpp:4076`: 일반 PC 종족은 Player_Human, 소환사는 `summoner_race` 설정을 따른다. 현재 `conf/battle/player.conf:297`의 값은 11이다.
- `src/map/pc.cpp:4549`, `pc_bonus2`: `bAddRace`가 오른손·왼손·화살의 종족별 `addrace` 배열에 누적된다.
- `src/map/pc.cpp:4980`, `pc_bonus2`: `bIgnoreDefRaceRate`가 `indexed_bonus.ignore_def_by_race`에 누적된다.
- `src/map/battle.cpp:771`, `battle_calc_cardfix`: 플레이어 공격자의 무기 공격 보너스를 대상 종족 인덱스로 조회한다. 이 추가 대미지 자체에 대상 BL_PC 조건은 없다. 기존 Demihuman 허수아비에서는 Player_Human 슬롯이 선택되지 않는다.
- `src/map/battle.cpp:4728`, `battle_calc_defense_reduction`: 종족·All·클래스 DEF 무시율 합산, 기존 100% 상한과 정수 연산을 적용한다. 이 옵션도 대상 BL_PC 조건보다 종족 선택이 핵심이다.
- `src/map/battle.cpp:5520`: Renewal에서는 공격자 cardfix를 무기/장비 ATK 성분에 적용한다. 전체 최종 대미지에 일괄 배율을 추가하는 방식이 아니다.
- `src/map/battle.cpp:2114`, `battle_calc_bg_damage`: BG 맵에서 공격 종류별 별도 감소를 적용한다. GvG와 스킬별 보정도 별도 경로이며 이번에 수정하지 않았다.

### 실제 Vellum 17종

AegisName은 `Velum_*`이며 17종의 스크립트·라인을 증빙 JSON에 보존했다. 이 스크립트들에 별도 PvP/WoE/BG 맵 검사 조건은 없었다. 각 옵션의 엔진상 조건과 맵 보정은 별개다.

| ID / 이름 | 주요 옵션 및 이번 범위 |
|---|---|
| 1396 Guillotine | 물리 +40%, DEF 무시30%, +9에 물리 +60% 추가. 해당 옵션 지원. |
| 1436 Spear | 물리 +60%, +9에 +30% 추가. 해당 옵션 지원. |
| 1492 Glaive | 물리 +80%. 해당 옵션 지원. |
| 1587 Encyclopedia | 물리 +(80+제련도)%, DEF 무시30%. 해당 옵션 지원. 확률 autobonus 검증 제외. |
| 1832 Claw | 물리 +100%, +8부터 +20% 추가. 해당 옵션 지원. |
| 18114 CrossBow | 물리 +(30+제련도)%, DEF 무시30%. 해당 옵션 지원. |
| 21001 Claymore | 물리 +80%, DEF 무시30%, +6부터 물리 +40% 추가. 최소 비교 대상. `db/re/item_db_equip.yml:115204`. |
| 1293 / 13072 / 16020 | Jamadhar / Damascus / Stunner: 플레이어 DEF·MDEF 설정 등 대상 효과는 범위 밖. |
| 1294 / 1395 / 18113 / 21002 | Scare / Buster / Arbalest / Katzbalger: HP/SP 소실은 범위 밖. |
| 2016 Arc Wand | 플레이어 마법 추가 대미지·MDEF 무시는 범위 밖. |
| 1586 / 16021 | Bible / Flail: 회복·쿨다운·회복불가 등은 이번 검증 범위 밖. |

장비의 스킬별 증가·ASPD 등 기존 일반 효과를 제거하지 않았지만, 무기 전체의 모든 효과를 지원·검증했다는 뜻은 아니다. 아이템 DB는 수정하지 않았다.

## 2. 변경 파일과 구현

| 파일 | 변경 내용 |
|---|---|
| `src/map/need_pvp_dummy.hpp` (신규) | ID31900 + hero_tra + 비소유·비BG 몬스터 + 비PvP/BG/GvG 맵을 모두 검사하는 공통 가드. |
| `src/map/battle.cpp` | BF_WEAPON의 bAddRace 조회 및 비율형 DEF 무시 조회에만 Player_Human 인덱스를 사용. 오른손·왼손·화살·양손 통합 설정 재사용. |
| `src/map/mob.cpp` | mob_dead 진입부에서 전용 대상 HP 회복·피해 기여 로그 초기화 후 사망 취소. |
| `db/import/mob_db.yml:2477` | ID31900 NEED_PVP_TEST_DUMMY 추가. HP20억/레벨100/중형/무속성1/DEF100, Boss, 이동·공격 불가, 넉백·텔레포트 차단, EXP0/드롭 없음. 실제 종족은 Formless. |
| `db/import/mob_avail.yml:127` | 기존 S_DUMMY_100_MEDIUM 외형 재사용. 신규 클라이언트 리소스 불필요. |
| `npc/NEED/need_pvp_dummy.txt` (신규) | hero_tra 95,144에 1개 소환, 95,147에 KR/EN 안내·HP 회복 NPC. 임시 GID로 단일 파일 재로드 시 중복 소환 방지. |
| `npc/scripts_need.conf:158` | 신규 파일 로드만 추가. 기존의 별도 shadow_recycle 변경은 이번 커밋에서 제외. |

ID31900은 소스의 몬스터 허용 범위 20020~31999 및 int16 범위 안에 있고 기존 몬스터 ID와 충돌하지 않는다. 두 배치 좌표는 실제 로컬 map_cache의 보행 가능 셀임을 확인했다.

`block_list.type`, 실제 status.race, PC 캐스팅, 종족 상수는 바꾸지 않는다. 두 조회 지점만 Player_Human 인덱스를 선택하며 Demihuman과 더하지 않으므로 중복 합산하지 않는다. All/크기/속성/클래스 보너스는 기존 경로를 따른다.

공유 DEF 함수는 Grand Cross 같은 복합 마법에서도 호출되므로 인덱스 대체에 `wd->flag & BF_WEAPON` 조건을 추가했다. 마법/MDEF·소실·상태이상·종족별 크리티컬 등은 새로 활성화하지 않는다. `mapdata_flag_vs2`로 PvP/BG/GvG/길드 던전/공성 성을 제외한다. 일반 몬스터·기존 허수아비·실제 PC·다른 맵 동일 ID·소유 몬스터·BG 목표물은 특별 판정 대상이 아니다.

### 사망과 보상

EXP0/빈 드롭만으로 NEED 월드 드롭·일일퀘스트·전역 킬 이벤트까지 막을 수 없어, mob_dead 시작에서 사망을 취소한다. 그 뒤의 EXP/아이템/퀘스트/업적, NEED 월드 드롭·이벤트·일일퀘스트(`mob.cpp:4115` 부근), NPCE_KILLNPC(`mob.cpp:4148`)에 진입하지 않는다. 실제 PC 사망/PK 판정으로 전환하지 않는다.

기존 전투 숫자를 표시하고 치명 피해가 누적되면 HP를 회복한다. 안내 NPC에서도 HP만 회복할 수 있다. 새로운 DPS·보상·랭킹 시스템은 만들지 않았다.

## 3. 테스트 결과

| 검증 | 결과 / 한계 |
|---|---|
| 변경 C++ 컴파일 | MSVC x86/C++17로 battle.cpp, mob.cpp 성공(exit0). 출력은 tools/need_pvp_dummy/obj에만 생성. 전체 map-server 링크·교체 미실시. |
| 컴파일 경고 | 헤더 인코딩 C4819, signed/unsigned C4018 등 출력. 오류 없음. 로그 보존. |
| 네이티브 계산 스모크 | **36 통과 / 0 실패**, exit0. |
| 데이터·범위 정적 점검 | **17 통과 / 0 실패**. 기존 파일 유지, ID/EXP/드롭/외형/로더/명령 등록/괄호/좌표 등. |
| diff 공백 검사 | 변경한 추적 파일 git diff --check 통과. |
| 서버·클라이언트 통합 | 미실시. 정적 점검은 실제 YAML 로더·NPC 파서·클라이언트 렌더링 검증이 아니다. |

### 대미지 수치

**실제 장비를 장착한 캐릭터의 실측값이 아니다.** 실제 cardfix·DEF 계산 함수 본문과 실제 헤더/보너스 배열을 사용하는 C++ 하네스다. 월드 조회·상태이상·패시브는 고정 스텁이며 실제 21001 DB +0/+6 분기를 확인한 뒤 옵션 값을 배열에 주입했다. 아이템 스크립트 실행·장착 재계산·무기 ATK260·제련 ATK 증가까지 검증한 결과로 해석하면 안 된다.

입력: 카드 보정 전 무기/장비 ATK 성분10,000, eDEF100, sDEF50, 중형/무속성1/Boss. 랜덤·크리티컬·버프·기타 카드 없음. 상태/숙련 ATK·명중·스킬 배율·맵 보정 제외.

| 동일 방어 fixture | 기존 인간형 | 신규 PvP |
|---|---:|---:|
| 일반 옵션 / 벨룸 옵션 없음 | 8,150 | 8,150 |
| 벨룸 +0 옵션(+80%, DEF 무시30%) | 8,150 | 15,552 |
| 벨룸 +6 옵션(+120%, DEF 무시30%) | 8,150 | 19,016 |
| 벨룸 옵션 해제 | 8,150 | 8,150 |

추가 대미지만 적용한 신규 값은14,710, DEF 무시30%까지 적용하면15,552다. Demihuman 보너스를 함께 넣어도 신규 대상은 중복 증가하지 않았다. 인간형 옵션만 넣으면 기존 대상만 증가했다.

기존 DB 그대로 DEF0/sDEF50의 일반 입력은9,950이며 신규 DEF100/sDEF50은8,150이다. 동일 방어 비교는 하네스 fixture에서만 수행했고 기존 DB를 바꾸지 않았다. 게임에서는 각 대상의 장비 변경 전후를 비교해야 하며 두 대상의 절대값을 직접 비교하면 안 된다. NPC에도 안내했다.

### 동작별 확인 범위

- 기존 허수아비는 벨룸 전용 옵션 미적용 및 수정 전/후 계산 함수 결과 동일.
- 신규 일반/벨룸/+6/해제, 인간형 중복 방지, 원거리·왼손·양손 통합 설정 확인. 왼손 +20%는10,000→12,000.
- BF_NORMAL과 BF_SKILL/SM_BASH ID로 두 함수 경로 확인. 실제 Bash의 명중·배율·SP·애니메이션과 전체 skill_attack은 미검증.
- 플레이어 마법 cardfix 미확장 및 BF_MAGIC의 공유 DEF 호출 제외 확인.
- 다른 맵·일반 몬스터·소유 몬스터·BG 목표물 제외 확인.
- 실제 PC의 PvP/BG/GvG 맵 조건에서 수정 전/후 cardfix·DEF 동일. 실제 전장 세션·최종 맵 보정 통합 실측은 미실시.
- 실제 mob_dead 가드 본문을 추출해 사망 요청3회에 HP 복구·피해 로그 초기화·보상 sentinel 미진입 확인. 뒤쪽 보상 본문은 sentinel로 대체했으며 실제 SQL·순위·인벤토리는 미검증. 일반 몬스터는 downstream에 도달함.

## 4. 남은 제약 및 추가 설계

1. 실제 PvP/BG 대미지 100% 재현 기능이 아니다. 플레이어 방어장비·카드·상태·피해감소·맵/스킬별 보정·BG 팀/목표물을 모사하지 않으며 Boss 분류와 몬스터 sDEF도 유지한다.
2. Player_Human 대상 bAddRace와 **비율형** bIgnoreDefRaceRate만 지원한다. Doram 선택, 완전 무시 bIgnoreDefRace, MDEF·소실·DEF 설정·종족별 proc 확장은 별도 조사와 설계가 필요하다.
3. 실제 종족은 Formless라 범위 밖 종족 의존 효과는 몬스터 종족을 볼 수 있다. Formless 전용 물리 bAddRace는 Player_Human 인덱스로 대체된다. 완전한 플레이어 상태 모형이라고 표현하지 않는다.
4. HP 회복은 디버프 전체·DPS 집계 초기화가 아니다. 공유 대상이므로 통제 비교는 단독 진행이 필요하다.
5. 지정 대상의 unitkill/killmonster 등 HP 기반 강제 처치도 사망 취소된다. 일반 몬스터는 영향 없다. 전체 NPC 재로드의 직접 unit_free 정리는 영향 없지만 운영 중 단일 대상 제거는 보상 없는 명시적 despawn 설계를 별도로 검토해야 한다.
6. 다른 맵의 동일 ID나 PvP/BG/GvG로 변경한 hero_tra에서는 가드가 비활성화된다. 해당 구성으로 운영하면 안 된다.
7. 배포 전 격리 서버에서 실제 DB 로드/NPC 파싱, KR/EN·외형·HP 회복·재로드 후1개 유지, 동일 캐릭터의 일반공격/Bash, +0/+6 실제 장착, 인간형 카드 병행, 치명 피해 후 EXP/드롭/일일퀘스트/킬 이벤트 불증가를 확인해야 한다. 이번 작업에서 실행하지 않았다.

## 5. 증빙 및 재현

증빙 폴더: `docs/reports/need-pvp-dummy-vellum-20261115/`

- build_changed.log / build_smoke.log / native_smoke.log: 컴파일 및36개 스모크 로그.
- data_scope_checks.json: 17개 점검, 실제 좌표 셀, Vellum17종 원문·라인.
- generate_smoke.py / validate_scope.py / build_changed.cmd / build_smoke.cmd: 재현 도구. 계산 비교 baseline은 불변 커밋 d7fa1a369로 고정했다.
- implementation-review.patch: 이번7개 실행 파일의 검토용 diff. 기존 사용자 변경 제외. 신규 NPC 원본은 CP949, 검토 패치 한글은 UTF-8이므로 배포용 자동 적용 패치로 사용하지 않는다.

```powershell
python docs/reports/need-pvp-dummy-vellum-20261115/generate_smoke.py
cmd /d /c tools\need_pvp_dummy\build_changed.cmd
cmd /d /c tools\need_pvp_dummy\build_smoke.cmd
& tools/need_pvp_dummy/native_smoke.exe
python docs/reports/need-pvp-dummy-vellum-20261115/validate_scope.py
```

빌드 명령은 현재 F:\rathena 및 설치된 MSVC 환경 기준이다. 도구 출력은 gitignore 대상 tools/need_pvp_dummy에 생성한다. 서버 시작·생산 DB 접속 명령은 포함하지 않았다. 커밋 준비 시 누락된 보고서를 보존된 증빙과 구현 결과를 기준으로 복구했다.

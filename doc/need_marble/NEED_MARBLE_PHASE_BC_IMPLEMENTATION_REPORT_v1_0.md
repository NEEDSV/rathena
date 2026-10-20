# NEED 라그 마블 — Phase B~C 구현 보고서 v1.0

- 작성일: 2026-10-20
- 요청서: `NEED_MARBLE_Claude_Phase_BC_Implementation_Separate_Branch_Request_v1_0.md`
- 브랜치: `feature/need-marble-phase-bc-v1` (로컬 전용, upstream 없음, **푸시 안 함**)
- worktree: `E:\tools\Need\need-marble-phase-bc-v1` (원본 `E:\tools\Need\rathena` 바깥)
- 기준 커밋: `feature/need-marble` **`373dcfe27`** (요청서 기준과 일치, 차이 없음)
- 상태: **G1(정적·오프라인·worktree 빌드)까지 완료.** G2(격리 DB/서버 실행 시험) 전부 NOT RUN. 원본 DB 적용·운영 배포·NPC 운영 로드·CP 실지급 **미승인·미실행**.

## 1. 커밋

| 커밋 | 단계 | 내용 |
|---|---|---|
| `a89e6586b` | M1 | C++ 원장 모듈(`need_marble.cpp/hpp`), 순수 규칙(`need_marble_logic.hpp`), 신규 스키마 초안·되돌리기 SQL, 기존 소스 훅, 스크립트 명령 8개 |
| `65ed985e8` | M2·M3 | 일반 유저 보드 NPC v2(`@marble`), 인던·전장 훅 각 1줄 |
| `9fff4fb49` | M4 | 단위 시험(113개)·정적 검사(64개) |
| (이 문서 커밋) | M5 | 보고서 6종, 불변식 점검 SQL(읽기 전용, 미실행) |

기준 대비 코드 변경: 19개 파일, 추가 3,253줄, 삭제 0줄(문서 커밋 제외). 기존 파일 수정은 아래 9곳뿐이다.

| 기존 파일 | 변경 |
|---|---|
| `src/map/mob.cpp` | include 1줄 + 사냥 훅 2줄(추석 훅 바로 뒤, 같은 조건) |
| `src/map/map.cpp` | include 1줄, `map_quit` 에서 사냥 배치 flush 1줄, init/final 각 1줄 |
| `src/map/script.cpp` | include 1줄 |
| `src/custom/script.inc`, `script_def.inc` | 마블 스크립트 명령 8개(본문·정의) |
| `src/map/map-server.vcxproj`, `map-server-generator.vcxproj`, `.filters` | 신규 소스 등록 |
| `npc/NEED/need_md_clear_bonus.txt` (CP949 유지) | `need_marble_instance_clear(.@key$, .@map$);` 1줄 + 주석 |
| `npc/battleground/bg_common.txt` (CP949 유지) | `need_marble_bg_match(.@map$, .@bg_type$, $@BG_RM_TOKEN);` 1줄 + 주석 |

GM v1.2 이동 코어(`need_marble_motion_v1_2.txt`)는 **수정하지 않았다.** `npc/scripts_need.conf` 도 수정하지 않았다(새 NPC는 로드되지 않음).

## 2. 기준 자료 확인

| 문서 | 위치 | 확인 |
|---|---|---|
| INITIAL_BALANCE_V1_0, 그 리포트 | 원본 `md/need_marble_20261019/` | 있음 (읽기만) |
| OPEN_DECISIONS_v0_3 | 같음 | 있음 |
| DICE_LEDGER_DESIGN_v0_5 | 같음 | 있음 — 구현 기준 |
| LEDGER_VALIDATION_PLAN_v0_3 | 같음 | 있음 — V01~V24 |
| TEST_REPORT, M07_M16_EXECUTION_REPORT, D1_ACCOUNT_SCOPE_IMPACT_v0_1, PHASE_B_FLOW_SPEC_v0_1 | 같음 | 있음 |

정책 충돌 처리: 요청서·v1.0 > 설계 v0.5 미결 권장안 > 과거 문서. 이번에 요청서가 정한 것: 완주 = **L1(실제 33걸음마다)**, 워프 목적지 **임시 7→13, 22→28**, 던전 미선택 **60초 자동 안전은 구현하되 기본 OFF**, 교환 일일 상한 없음.

## 3. 구현 내용

### 3.1 M1 — 원장·스키마

| 요구(요청서 7절) | 구현 | 위치 |
|---|---|---|
| 계정 공용 상태 | 지갑·보드 상태·롤·이벤트·완주 모두 `account_id` 키, `char_id` 는 감사 열 | 스키마 전체 |
| 단일 원자 연산 | 진입점 1개 = 트랜잭션 1개. `ledger_tx` 만 START/COMMIT/ROLLBACK, 실패 시 소멸자가 ROLLBACK | `need_marble.cpp` `ledger_tx` |
| 중첩 START 금지 | `ledger_tx::begin()` 이 열린 트랜잭션이 있으면 거부, `*_in_tx` 7개는 `MARBLE_REQUIRE_TX()` 로 트랜잭션 밖 호출 거부 | 같음 |
| `query_sql` 한계 | 원장은 전부 C++(`mmysql_handle`), `Sql_NumRowsAffected` 로 CAS 판정. NPC 는 원장 SQL 을 직접 쓰지 않음 | 같음 |
| COMMIT 불명확 | COMMIT 실패 = 결과 불명 → 모듈 전체 fail-closed(재기동·운영자 대사 전까지 모든 진입점 거부) | `fault()` |
| 사냥 배치 키 | 수집 세션(DB 발급, 보드 `run_session` 과 무관) + PK `(account, collect_session, batch_no, period_day)`, 보낸 배치는 확인될 때까지 같은 내용으로만 재전송 | `hunt_buf`, `hunt_checkpoint` |
| 구간 무한 생성 금지 | `next_tier` SMALLINT, `max_tiers`(4) 이후 처리 중지, 그날 구간 완료 시 메모리에서 더 세지 않음 | `hunt_next_tier`, `done_day` |
| 인던 두 키 | 실행 키 `(boot_id, instance_id, dungeon_code)` = 활동 행 PK, 일일 자격 키 `(account, season, day, dungeon)` = `need_marble_dungeon_daily` PK | `instance_credit` |
| 일일 상한 | `need_marble_period.earned`(EARN 인정분)만으로 판정 → 교환 차감으로 상한이 다시 열리지 않음 | `credit_in_tx` |
| 04:00 KST | `logical_day()` 가 UTC+9 고정 오프셋으로 계산, OS 시간대 무관 (이 PC OS 는 UTC 로 실측) | `need_marble_logic.hpp` |
| 설정 분리 | `need_marble_policy (key, effective_day, value)`, 하루 단위 재조회, 값 검증 실패 시 모듈 비활성 | `policy_refresh` |
| 비활성·롤백 | 마스터 스위치(키 1) 기본 0, 스키마 없으면 비활성, 되돌리기 SQL 별도 | `need_marble_phase_bc_v1_revert.sql` |
| 스키마 | 신규 테이블 17개, 기존 테이블 변경 0, 숫자 PK, InnoDB. **파일만 작성, 미적용** | `sql-files/need_marble_phase_bc_v1.sql` |

### 3.2 M2 — Phase B 유저 기능

| 요구(5절) | 구현 |
|---|---|
| 입장 NPC | 이번 범위는 `@marble` 메뉴의 "게임판으로 이동"(직전 위치 저장 후 확정 칸으로 이동). **페이욘 NPC 좌표는 미정이라 배치하지 않음** |
| 전역 메뉴 | `@marble` (모든 유저, `#marble` 형태 불가). GM `@mbroll/@mbdice/@mbstatus/@mbreset` 과 명령·저장소 모두 분리 |
| 메뉴 항목 | 규칙, 계정 공용 점수·주사위(보유/한도), 오늘 획득(활동별/전체 상한), 교환, 굴리기/이어가기, 던전 선택, 입장, 위치 복구, 퇴장, CP 지급 대기 합계 |
| NPC 대화 중 이동 금지 | 굴리기·복구·퇴장은 `close2` 로 대화창을 닫은 뒤 실행 |
| 서버 난수만 | 눈은 C++ `rnd_value(1,6)`. 강제 눈은 GM 99 + TEST 시즌(≥900000) 에서만 C++ 가 받아들임(유저 메뉴에는 없음) |
| 이동·복구 | MOVING/PAUSED/RECOVERY 이면 새 주사위를 쓰지 않고 같은 롤 재개(새 `run_session`). 다른 캐릭터·재접속·늦은 타이머의 옛 `run_session` 은 CAS 에서 거부 |
| 이탈·재입장 | 입장 직전 맵·좌표를 캐릭터 변수에 저장, 퇴장 시 복귀(없으면 세이브 포인트). 전장·PvP·GvG·인스턴스·nowarp 맵에서는 입장 거부. 이동 중 사망·맵 이탈·수동 이동·정체·시간 초과 → 일시정지 |
| 교환 | 요청 ID 영속, 확정 전 재접속해도 같은 요청 표시(계속/취소), 결과 확인(ack) 전 새 교환 차단, 메뉴 진입 시 읽기 전용 peek 으로 미확인 요청 먼저 표시 |
| 활동 계측 | 사냥: 기존 필드 필터 재사용, ±30, P2-active(몹 주변 화면 범위·생존·최근 60초 활동·레벨 조건, 캡차 진행 중 제외 설정). 인던: 등급 A 10곳만, 보스 맵 체류 + 최근 60초 활동(설정). 전장: 참여 표본(30초 간격, 최소 10회, 활동 비율 50%) + 종료 시점 비자리비움 — **정책 기본 OFF** |
| KO/EN | 모든 유저 문구 `needtr("한국어","English")`, NPC 파일 CP949 |

### 3.3 M3 — Phase C 칸 이벤트

| 요구(6절) | 구현 |
|---|---|
| 확률·효과 설정 분리 | 칸 종류별 확률(1/10000)·CP 를 정책 키 `100+종류×10+슬롯` / `200+…` 로 분리, 확률 합 10000 아니면 모듈 비활성 |
| 한 번만 결정 | 이벤트 PK `(account, roll_seq, hop)`, 추첨값 `rng_value` 를 결정 시 기록, 재접속·반복 메시지는 재추첨 없음 |
| 워프 W1 | 도착 트랜잭션에서 이벤트(WARP) + jump 저널 + 위치=목적지 + IDLE. 목적지는 도착 처리 안 함 → 연쇄 없음. 캐릭터 실제 워프 실패 시 다음 굴리기에서 확정 칸으로 이동 |
| 던전 선택 | 도착 시 상태 4(선택 대기), 선택 트랜잭션에서 한 번만 추첨·확정. 자동 안전은 정책 ON + 60초 경과 시에만(타이머 유실 대비 다음 진입 시에도 판정) — **기본 OFF** |
| 33걸음 보너스(L1) | 걸음 확정 트랜잭션에서 `step_seq` 가 33의 배수가 되면 `cp_payout(LAP, lap_no)` — 도착 칸 키(TILE, roll_seq)와 다른 키, 1회만 |
| CP | `need_marble_cp_payout` 에 **DECIDED(지급 대기) 행만** 생성. `#CASHPOINTS`·계정 레지스트리를 바꾸는 코드 없음(정적 검사로 확인), 정책 키 5(CP 적용)를 0 이 아닌 값으로 두면 정책 검증 실패로 모듈이 꺼짐 |

## 4. 미완료·차단

| 항목 | 상태 | 이유 / 필요한 것 |
|---|---|---|
| G2 격리 실행 시험 V01~V24 | **NOT RUN** | 운영자 G2 승인 필요 |
| 게임판 맵 등록 | **BLOCKED (G2 전제)** | `need_marbl` 의 `maps_athena.conf`·`map_index.txt`·`map_cache.dat` 등록은 원본 작업 트리에만 미커밋 상태로 있고 이 브랜치에는 없음. 원본 캐시·리소스 변경 금지라 가져오지 않음. G2 때 격리 복사본에만 넣을지 결정 필요 |
| NPC 스크립트 구문 실측 | NOT RUN | map-server 로 파싱해야 확인 가능(G2). 정적 검사는 명령 이름·인자·보드 표 일치까지만 |
| map-server-generator 빌드 | FAIL (**기존 결함**) | 기준 커밋의 generator 프로젝트에 `need_jf_pattern.cpp`·`need_storage_shop.cpp` 가 없어 링크 오류 22건. 마블 기호 누락 0건. 요청 범위 밖이라 고치지 않음 |
| CP 실지급 경로 | 미구현(의도) | 별도 설계·승인 필요(설계 v0.5 16절의 표지 방식 등) |
| 시즌 정산 | 미구현(의도) | v1.0: 14일은 재조정 주기, 시즌 초기화 없음. 시즌 행 상태 확인(OPEN)만 구현 |
| 페이욘 입장 NPC | 미배치 | 좌표 운영자 미확정 |
| 전장 참여 판정·던전 자동 안전·캡차 제외·워프 목적지 | 구현했으나 **운영자 확정 전** | 설정표 참조 |

## 5. 남은 위험

1. **동기 DB 호출:** 사냥 체크포인트(20킬 또는 60초마다), 도착·걸음 확정이 map-server 주 스레드에서 DB 를 기다린다(추석 원장과 같은 방식). 동시 접속 부하는 미측정.
2. **COMMIT 실패 = 전체 정지:** 안전하지만, 일시적 DB 장애 한 번으로 재기동 전까지 마블 전체가 멈춘다.
3. **사냥 손실:** 강제 종료 시 마지막 체크포인트 이후 처치(최대 약 20마리 또는 60초분)는 사라진다. 중복은 없다.
4. **알림 유실:** 적립 알림은 NPC 이벤트로 보내므로 다른 NPC 대화 중이면 메시지가 보이지 않을 수 있다(적립 자체는 유지).
5. **일자 경계 차이:** 마블은 고정 KST, 기존 NEED 이벤트 사냥은 서버 OS 시간대를 따른다. 이 PC OS 가 UTC 이므로, 운영 서버 시간대가 KST 가 아니면 두 기능의 하루 경계가 다르다.
6. **런타임 검증 필요:** MariaDB 의 트랜잭션 중 START TRANSACTION 암묵 커밋 동작(가드로 막지만 실측 전), `INSERT IGNORE` 영향 행 수, `LOCK IN SHARE MODE` 대기, 끊긴 연결의 롤백 시점.
7. **배포 결합:** NPC 훅 2줄은 새 스크립트 명령을 부르므로 **이 브랜치의 map-server 와 함께만** 배포할 수 있다(옛 바이너리 + 새 NPC = 스크립트 오류).

## 6. 다음 단계 (각각 별도 승인)

G2 격리 시험 승인 → 맵 등록 방식 결정 → V01~V24 실행 → 원본 DB DDL → 바이너리·NPC 로드 → 마스터 스위치 ON → CP 지급 설계·승인. 상세: `NEED_MARBLE_DEPLOYMENT_AND_CP_GATE_v1_0.md`.

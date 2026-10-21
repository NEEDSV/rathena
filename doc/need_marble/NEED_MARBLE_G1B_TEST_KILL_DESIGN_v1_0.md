# NEED 라그 마블 — G-1(b) 시험 빌드 전용 처치 주입 설계 v1.0

- 작성일: 2026-10-21
- 상태: **설계만. 코드는 쓰지 않았다.** 실행 승인 전 구현·빌드·기동 없음.
- 목적: G2 에서 BLOCKED 된 사냥 시나리오(V03·V14·V18·V22)를 격리 스택에서 실행할 수단. 더미 클라이언트는 공격 패킷(0x0437)으로 몹을 잡지 못했다(G2 F1).

## 1. 근거 (읽기 전용 확인)

| 사실 | 소스 |
|---|---|
| `status_damage(src, target, hp, …)` 는 몹 대상일 때 `mob_damage` → `mob_log_damage(md, src, …)` 로 데미지 기록을 남기고, HP 0 이면 `mob_dead(md, src, 0)` | `src/map/status.cpp:1508, 1710`, `src/map/mob.cpp:2655, 2757` |
| 마블 사냥 훅은 `mob_dead` 안에서 데미지 기록으로 뽑은 최다 피해자에게 걸린다(새 훅 불필요) | `src/map/mob.cpp:3369~3373, 3993~3994` |
| map-server vcxproj 4개 구성 모두 `PreprocessorDefinitions` 에 `$(DefineConstants)` 포함 → 프로젝트 파일 수정 없이 `/p:DefineConstants=…` 로 정의를 넣을 수 있음 | `src/map/map-server.vcxproj:110` 외 3곳 |
| 운영 Linux 빌드(`Makefile.in`)와 CMake 는 `src/map` 의 `*.cpp` 를 **전부 자동으로** 컴파일 | `src/map/Makefile.in:28`, `src/map/CMakeLists.txt:14` |

마지막 사실 때문에, 시험 코드를 `src/map` 에 두면 운영 빌드에도 (빈) 파일로 들어간다. 그래서 저장소 소스 트리 밖에 두는 안을 추천한다.

## 2. 넣을 코드 (예정)

### 2.1 두 가지 주입

| 명령 (스크립트) | 동작 | 타는 경로 | 쓰는 시나리오 |
|---|---|---|---|
| `need_marble_test_kill(<반경>,<최대 수>)` | 호출한 GM 주변 반경 안의 몹 중 **마블과 같은 필터**(`need_event_hunt_normal_field_target`, 레벨 ±정책값)를 통과한 영구 스폰 필드 몹을 골라 `status_damage(&sd->bl, &md->bl, md->status.hp, 0, 0, 0, 0, 0)` 로 처치. 처치 수 반환 | **실제 경로**: 데미지 기록 → `mob_dead` → 최다 피해자 → `need_marble_hunt_on_kill` → 파티 P2-active → 체크포인트 | V03, V14, V18 |
| `need_marble_test_hunt_feed(<횟수>)` | 주변 살아 있는 필드 몹 1마리를 기준으로 `need_marble_hunt_on_kill(sd, md, 0)` 를 n번 호출(몹은 죽지 않음) | **원장 계층만**: `mob_dead`·데미지 기록·파티 판정은 건너뜀 | V22(30,000회 등 대량), 체크포인트 부하 측정 |

- 공통 가드(시험 빌드 안에서도 적용): ① map-server 기동 시 환경 변수 `NEED_MARBLE_TEST_HOOKS=1` ② 호출자 그룹 레벨 99 ③ `SELECT DATABASE()` 가 `ragnarok_mbqa` 일 때만 동작. 하나라도 아니면 0 반환과 함께 `ShowError` 를 남긴다.
- `test_kill` 은 몹 HP 만큼 한 번에 주는 데미지라 데미지 공식·스킬·장비는 검증하지 않는다. 원장 입장에서는 실제 처치와 같다(같은 `mob_dead` 경로, 같은 귀속).
- 캡차 상태(V18 의 캡차 제외 항목)는 이번 주입에 넣지 않는다. 보안 상태를 바꾸는 시험 코드는 범위를 넘으므로, 기존 캡차 발동 경로로 만들 수 있는지 따로 확인한다.

### 2.2 파일 위치 — 두 안

| 안 | 위치 | 운영 영향 | 추천 |
|---|---|---|---|
| **B 소스 밖 패치 (추천)** | `tests/need_marble/g2/test_hooks/` 에 `need_marble_test.cpp`·`.hpp` 와 패치 파일(`script.inc`·`script_def.inc`·`map-server.vcxproj` 에 등록 3곳)을 둔다. 시험 때만 **worktree 를 스크래치로 복사한 소스**(`mbqa_testsrc`)에 패치를 적용해 빌드 | 저장소 `src/` 에 시험 코드가 전혀 없다. Linux·CMake 운영 빌드에 들어갈 길이 없음 | ✔ |
| A 저장소 안 `#ifdef` | `src/map/need_marble_test.cpp` 전체와 `script.inc`·`script_def.inc` 시험 블록을 `#ifdef NEED_MARBLE_TEST_HOOKS` 로 감쌈 | 운영 빌드에는 빈 번역 단위로 들어간다(기능 없음). 정의가 실수로 들어가면 운영에 노출될 여지 | |

어느 안이든 코드 자체는 `#ifdef NEED_MARBLE_TEST_HOOKS` 로 감싸고 2.1 의 실행 가드를 둔다(이중 방어).

### 2.3 시험 하네스 (스택 복사본만)

기존 `_mbqa_harness.txt` 에 GM 99 전용 `@mbqakill <최대 수>`, `@mbqafeed <횟수>` 두 명령을 추가한다. 하네스는 지금처럼 스택 복사본에서만 로드된다.

## 3. 운영 빌드에서 빠지는 방식

| 겹 | 방식 | 확인 방법 |
|---|---|---|
| 1 소스 | (B) 저장소 `src/` 에 시험 코드가 없다. 패치는 스크래치 소스 복사본에만 적용 | 정적 검사: `src/` 아래 `need_marble_test` 문자열 0건 |
| 2 컴파일 정의 | `NEED_MARBLE_TEST_HOOKS` 는 시험 빌드 명령줄 `/p:DefineConstants=NEED_MARBLE_TEST_HOOKS` 로만 준다. vcxproj·CMakeLists·configure·Makefile 어디에도 쓰지 않음 | 정적 검사: 빌드 파일 전체에 정의 0건 |
| 3 산출물 분리 | 시험 빌드는 `SolutionDir` = 스크래치 소스 복사본, `/p:OutDir=<스크래치>\mbqa_testbuild\`, `/p:IntDir=…\obj\` → worktree 루트 `map-server.exe`·`.vs\build` 와 섞이지 않음 | 빌드 전후 worktree exe 해시 동일 |
| 4 바이너리 | 운영용(worktree) `map-server.exe` 에는 명령 이름 문자열이 없어야 함 | `need_marble_test_` 문자열 검색: worktree exe 0건 / 시험 exe 있음 |
| 5 실행 가드 | 시험 exe 라도 환경 변수 + GM 99 + 격리 DB 이름 3조건 | 격리 시험 첫 단계에서 조건 하나씩 빼고 거부되는지 확인 |

## 4. 이걸로 풀리는 시나리오

| ID | 방법 | 남는 한계 |
|---|---|---|
| V03 사냥 누계 | `@mbqakill` 로 실제 처치 100·200·400마리 → 구간 25점 ×4, 5구간째 0 | 리스폰 속도에 따라 시간 소요(필드별 몹 수 측정 필요) |
| V14 수집 세션·배치 | 캐릭터 A 40마리 → 로그아웃(배치 flush) → 같은 계정 B 10마리 → 구간 1회 / 배치 재전송은 같은 키 ALREADY / A 체크포인트 전 map-server 강제 종료 시 손실 상한 측정 | "늦게 도착한 옛 세션 배치"는 DB 지연 주입이 필요해 부분만 |
| V18 파티 귀속 | 합성 파티(격리 DB 에 `party` 행 + `char.party_id`)를 만들고, GM 이 처치하는 동안 파티원이 범위 안·밖·방치 상태로 있음 → P2-active 대상만 적립 | 캡차 제외 항목은 제외(2.1) |
| V22 구간 상한·오버플로 | `@mbqafeed 30000` → 구간 행 ≤ 4, next_tier ≤ 5, 상한 뒤 체크포인트 생략 | `mob_dead` 를 건너뛰므로 처치 경로 자체는 V03 이 담당 |
| V21 배치에 기간 3개 | **여전히 불가** — 체크포인트 실패를 며칠 이어 가야 함(시각 오프셋 G-4(b) + DB 고장 주입 G-5(a) 필요) | 별도 승인 |

## 5. 승인이 필요한 것 (각각)

| ID | 내용 |
|---|---|
| T1 | 시험 코드·패치 작성과 이 브랜치 `tests/need_marble/g2/test_hooks/` 커밋 (안 B 기준, `src/` 무변경) |
| T2 | worktree 를 스크래치로 복사해 패치 적용·시험 빌드 (`/p:DefineConstants`, 별도 OutDir/IntDir) |
| T3 | 격리 스택 재생성과 기동 — 지난 G2 와 같은 포트(3327 / 26901 / 26121 / 25121 / 26906, 127.0.0.1)·경로 규칙, 사전 점검 PASS 후 |
| T4 | 합성 데이터 확장: 파티 1개(격리 DB `party`·`char.party_id`), 사냥 필드 위치로 캐릭터 배치 |
| T5 | 실행 범위 V03 · V14 · V18(캡차 제외) · V22, 소유 프로세스 강제 종료(V14 손실 측정) |
| T6 | 종료 후 정리(증거 이동 → 프로세스 종료 → 스크래치 소스·빌드·DB 삭제) |

원본 3306·dqtest·원본 서버·원본 작업 트리 변경, CP 실지급, 푸시·병합은 이 승인에 포함되지 않는다.

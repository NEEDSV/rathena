# NEED 라그 마블 — V22 테스트 러너 수정 보고서 v1.0

- 작성일: 2026-10-21 (작업 09:13~09:45 UTC)
- 요청: `NEED_MARBLE_F1_F2_FOLLOWUP_VALIDATION_Claude_Request_v1_0.md` 2절(P0)
- 위치: 브랜치 `feature/need-marble-phase-bc-v1`, worktree `E:\tools\Need\need-marble-phase-bc-v1`
- 범위: 테스트 러너·시험 훅·픽스처만 수정. **제품 코드(`src/`)는 바꾸지 않았다.** 격리 DB·서버는 띄우지 않았다.
- **격리 서버에서 V22 를 다시 돌린 결과는 없다 → V22 런타임 결과는 `NOT RUN`.** 직전 F1·F2 재시험의 러너 판정 FAIL 은 그대로 둔다.

## 1. 결함 원인

| 항목 | 내용 |
|---|---|
| 증상 | F1·F2 재시험 R4 에서 V22 러너가 FAIL. 관측: 누계 401, 배치 21, 구간 4, next_tier 5, 추가 5,000회 뒤 행 증가 0 |
| 원인 | 러너가 `mbqa_cap` 계정을 썼고, 같은 계정의 R1 처치 1건(배치 1행)이 남아 있었다. 러너는 누계 `== 400`, 배치 `== 20`, 기간 `[[0,100],[1,100]]` 같은 **절대값**을 기대했다 |
| 원장 동작 | 정상. 시작 누계 1에서 20마리씩 체크포인트가 돌아 20번째 배치에서 401 이 되며 4구간을 넘고, 그 뒤로는 F2 두 겹 방어로 아무것도 쓰지 않았다 |
| 근거 | 새 비교 모듈에 같은 관측값(401/21/4/5)을 넣으면 모든 검사가 통과하고, 옛 절대값 규칙은 같은 DB 에서 FAIL 한다(`v22_model_test.py` F2 묶음, `fu_evidence/s1_negative_and_dryrun.txt`) |

## 2. 수정 내용

### 2.1 기대값을 절대값에서 "시작 스냅샷 + 정책 + 코드 경로" 로

`tests/need_marble/g2/v22_model.py`(신규, 순수 Python)가 기대 행을 계산한다. 숫자를 400→401 로 바꾼 것이 아니다.

| 단계 | 기대값을 어떻게 정하나 | 왜 그 값이어야 하나 |
|---|---|---|
| 시작 | 시험 직전 스냅샷: 그날 누계·next_tier, 배치 행, 구간 실적 행, EARN 원장 행, 원장 행 수, 기간 earned(전체·사냥), 잔액 | 시나리오 간 잔여 데이터를 기준값으로 흡수 |
| 주입 | 로그인 첫 처치에서 그날 상한 여부를 한 번 조회(F2 1층) → 처치마다 메모리 +1 → `flush_kills`(20)마다 배치 1행 → 체크포인트가 누계에 더하고 넘긴 구간마다 적립 → `next_tier > max_tiers` 가 되면 그 로그인은 더 세지 않음 | `need_marble_hunt_on_kill` → `hunt_flush` → `hunt_checkpoint` 와 같은 순서 |
| 상한 날 체크포인트 | 배치 행·누계를 쓰지 않음(F2 2층) | `hunt_checkpoint` 의 `FOR UPDATE` 뒤 `hunt_day_maxed` |
| 구간 적립 | 구간마다 25점을 사냥 일일 상한 100·전체 일일 상한 200 으로 깎음(`credit_amount` 와 같은 식). 0점이어도 구간 행은 생기고 outcome 2 | `credit_in_tx` |
| 로그아웃 | 20 미만으로 남은 메모리 처치를 마지막 배치로 | `need_marble_on_logout` |
| 정책값 | 격리 DB `need_marble_policy` 의 그날 유효값, 없으면 v1.0 기본값 | 정책을 바꿔도 같은 러너로 판정 |

정책 v1.0 에서 시작 누계 K0(< 400)일 때 기대값은 **새 배치 ⌈(400−K0)/20⌉ 행(각 20마리), 누계 K0 + 20·⌈(400−K0)/20⌉, next_tier 5, 구간 행 4, 사냥 earned 100(전체 상한에 걸리면 그만큼 적음)**. K0 = 0 이면 400·20행, K0 = 1 이면 401·20행(합계 21행)이다.

### 2.2 검사 항목 (`v22_model.compare` / `unchanged`)

| 요청서 항목 | 검사 ID | 내용 |
|---|---|---|
| (a) 유효 처치 총량·단계 | a1, a2, a5, a6 | 누계·next_tier = 모형, 새 배치 행 = 모형(그날 기간만), 구간 행 = 1..next_tier−1 이고 ≤ max_tiers, next_tier ≤ max_tiers+1 |
| (b) 일일 사냥 100점 상한 | b1, b2, b3 | 사냥 earned ≤ 100, 전체 ≤ 200, 모형과 같음, 구간 적립 합 = 사냥 earned |
| (c) 상한 뒤 추가 5,000회·로그아웃 기록 0 | c1, c2 | 상한 뒤 스냅샷 전체(누계·배치·구간·EARN·원장 행 수·기간·잔액)가 직전과 같음 |
| (d) 기존 잔액·단계 정합 | a3, d1 | 옛 배치 행 불변, 잔액 증가 = 새 적립 합, 주사위 불변 |
| (e) 원장 불변식 | INV | 기존 `need_marble_phase_bc_v1_invariants.sql` 실행, 위반 0 |
| I11 | a4 | 그날 누계 = 그날 배치 합 |
| 주입 응답 | feed reply | `n=` 주입 수, `counted=` 가 모형의 증가량과 같음 |

### 2.3 V22 전용 합성 계정 (`bc_synth_v22.sql`, 신규)

| 계정 | 캐릭터 | 쓰임 |
|---|---|---|
| `mbqa_v22a` (2000907, 그룹 99) | `MBQA_V22A` (150909, 99레벨) | V22-1 깨끗한 계정 → V22-3 상한 계정 재접속 |
| `mbqa_v22b` (2000908, 그룹 99) | `MBQA_V22B` (150910, 99레벨) | V22-2 실제 처치 1건 + 배치 1행 선행 |

- 다른 시나리오는 이 두 계정에 로그인하지 않는다(정적 검사 "V22 dedicated accounts used by V22 only").
- 비밀번호는 `{{user}}` 자리표시자, 파일에 비밀값 없음.
- 독립 논리 일자는 OS·서버 시계를 건드려야 해서 쓰지 않았다. 대신 시작 시 04:00 KST 까지 900초 미만이면 BLOCKED 로 멈추고, 끝날 때 논리 일자가 바뀌었으면 전 케이스를 BLOCKED 로 바꾼다.
- 계정 상태가 픽스처와 다르면(`fixture_state` 가 clean / prior1 / capped 가 아니면) 그 케이스는 **FAIL 이 아니라 BLOCKED**(전제 오염)로 기록한다.

### 2.4 생존 상태 검증 (몬스터 사망 판정 코드는 손대지 않음)

| 파일 | 변경 |
|---|---|
| `test_hooks/need_marble_test.cpp` (시험 빌드 전용, `src` 밖) | `need_marble_test_kill`·`need_marble_test_hunt_feed` 가 호출자가 죽어 있으면 `-2` 를 돌려주고 경고 로그. 주입은 시작·끝의 `@MB_HUNT_KILLS`(DB+메모리) 차이를 `@MBQA_COUNTED` 로 남김 |
| `bc_stack_extras.py` (하네스 NPC) | `[MBQA] feed n=<수> counted=<증가량>`, 새 `@mbqastate` → `[MBQA] state hp=… lv=… map=…` |
| `bc_runner.py` | 각 케이스 시작 시 `@alive`·`@heal` 뒤 `@mbqastate` 로 HP > 0 확인, 실패하면 BLOCKED |

R3 1회차처럼 "죽어서 안 셈"과 "상한이라 안 셈"이 섞이면, 이제 주입이 `n=-2` 로 거부되거나 `counted` 가 모형과 달라 바로 드러난다.

### 2.5 시나리오 (`cmd_v22`)

| 케이스 | 계정·전제 | 조작 | 기대 (v1.0 정책) |
|---|---|---|---|
| V22-1 | `mbqa_v22a`, clean | 주입 30,000 → 5,000 → 로그아웃 | 30,000: 누계 400, 배치 20행, 구간 4, 사냥 100, 잔액 +100, counted 400. 5,000·로그아웃: 전부 불변, counted 0 |
| V22-2 | `mbqa_v22b`, prior1(러너가 실제 처치 1건 → 로그아웃으로 만듦) | 주입 30,000 → 5,000 → 로그아웃 | 누계 401, 새 배치 20행(합 21), 옛 배치 불변, 구간 4, 사냥 100, counted 400. 이후 불변 |
| V22-3 | `mbqa_v22a`, capped | 재접속 → 실제 처치 10 → 주입 5,000 → 로그아웃 | 처치 10 성공, counted 0, 모든 행 불변 |

## 3. 오프라인 픽스처 (요청서 2.5)

`tests/need_marble/g2/v22_model_test.py` (Python, 48개) 와 `tests/need_marble/logic_test.cpp` 의 `test_hunt_v22_fixtures`(C++, 실제 `hunt_next_tier`·`hunt_day_maxed`·`credit_amount` 사용, 7개).

| 픽스처 | 예상 DB 차이 | 상한 뒤 불변식 | 같이 넣은 거부 사례(비교기가 FAIL 해야 함) |
|---|---|---|---|
| ① 깨끗한 계정·일자 | 누계 0→400, next_tier 1→5, 배치 +20(각 20), 구간 행 +4(25×4), EARN +4, 기간 사냥·전체 +100, 잔액 +100 | 5,000·로그아웃 후 전부 동일, I11(400 = 배치 합), I10(구간 4 = next_tier−1) | 5번째 구간 행 / 상한 뒤 배치 1행 / 누계 없이 배치 행 / 사냥 125 / 잔액 +25 어긋남 / EARN 중복 → 모두 거부 확인 |
| ② 처치 1·배치 1 선행 | 누계 1→401, 배치 +20(합 21), 구간 +4, 사냥 +100, 옛 배치 행 불변 | 동일. 지난 실측값(401/21/4/5)과 일치, **옛 절대값 규칙은 이 정상 DB 를 FAIL** | 옛 배치 행 변경 / 누계 400(절대값) → 거부 |
| ③ 상한 도달 후 추가 처치 | 처치 10 + 5,010 주입 + 로그아웃: 차이 0. 1층 조회가 실패한 경우(2층만)도 차이 0 | 전부 동일 | 상한 날 배치 1행 → 거부 |
| (추가) ④ 전체 상한 150 사용 | 구간 4행: 25·25·0(outcome 2)·0, 사냥 50, 전체 200, EARN 2행 | - | - |
| 기타 | 전날 상한·오늘 새 날 → 오늘은 셈 / 오염된 계정 → `None`(BLOCKED) / 응답 파싱(`n=-2` 포함) / 거울 함수 경계값 | | |

## 4. 결과

| ID | 내용 | 결과 | 재현 명령 | 로그 |
|---|---|---|---|---|
| U1 | C++ 순수 규칙 단위 시험 | **PASS 126 / 0** (기존 119 + V22 픽스처 7) | `cl /nologo /std:c++17 /EHsc /W4 /I src tests\need_marble\logic_test.cpp /Fe:<스크래치>\marble_logic_test.exe` 후 실행 | `fu_evidence/u1_logic_test_*.txt` (컴파일 경고 0) |
| U2 | V22 비교기 오프라인 픽스처 | **PASS 48 / 0** | `python -I tests/need_marble/g2/v22_model_test.py` | `fu_evidence/u2_v22_model_test.txt` |
| S1 | 정적 검사 | **PASS 79 / 0** (기존 72 + 7) | `python -I tests/need_marble/static_check.py <worktree>` | `fu_evidence/s1_static_check.txt` |
| S1-neg | 새 검사가 실제로 결함을 잡는지 | **PASS**: 수정 전 러너(`19d6984e1`)의 V22 에서 절대값 기대를 검출, F1 이전 `mob.cpp`(`e6f808d3d`)는 호출 순서 검사 불통과 | `python -I fu_evidence/v22_negcheck.py` | `fu_evidence/s1_negative_and_dryrun.txt` |
| D1 | 러너 V22 도우미 건조 실행(가짜 DB 응답) | **PASS**: 스냅샷 파싱, 정책 읽기, 논리 일자 경계(03:59:59 → 20261021, 04:00:00 → 20261022), 지난 관측값 비교 통과 | 위와 같음 | 위와 같음 |
| B1 | worktree 운영용 map-server 빌드 | **PASS**: 소스 변경 없음 → 최신 상태, 해시 `fc2718c2…` 그대로, 시험 문자열 0 | MSBuild `src\map\map-server.vcxproj` `/p:SolutionDir=<worktree>\` | `fu_evidence/b1_worktree_map-server_build.txt`, `exe_hashes.txt` |
| B1-T | 시험 빌드(훅 포함, 스크래치 복사본) | **PASS**: `need_marble_test.cpp` 경고 0·오류 0, exe `81f95e68…`. 스크래치 복사본에 `db/` 를 넣지 않아 빌드 뒤 복사 단계 MSB3030 경고 84건(산출물 무관) | `apply_test_hooks.py --src <스크래치>` → `build_test.ps1` | `fu_evidence/b1_testbuild_map-server_build.txt` |
| V22 | 격리 서버 재실행 | **NOT RUN** (G2 실행 승인 전) | `bc_runner.py --out <스크래치>/mbqa_bc v22` | - |

첫 시험 빌드 시도는 스크래치 복사 때 `x64` 폴더를 통째로 빼서 `3rdparty\zlib\lib\x64\zlib.lib` 링크 오류(LNK1181)로 실패했다. 컴파일은 통과한 상태였고, 라이브러리 폴더 3개를 채워 다시 빌드해 통과했다. 스크래치 소스·빌드는 정리 대상이다(감사 보고서 5절).

## 5. 수정 전후 비교 (요약)

| | 수정 전 (`19d6984e1`) | 수정 후 |
|---|---|---|
| 계정 | `mbqa_cap` (R1 과 공유) | 전용 `mbqa_v22a`·`mbqa_v22b` |
| 기대값 | 누계 400, 배치 20, 기간 `[[0,100],[1,100]]` 고정 | 시작 스냅샷 + 정책 + 코드 경로로 계산 |
| 생존 확인 | 없음 | `@mbqastate` HP > 0, 죽으면 주입 `n=-2` 거부 |
| 상한 뒤 확인 | 누계·구간·배치 수 | 누계·배치 전 행·구간·EARN·원장 행 수·기간·잔액 전체 |
| 전제 오염 | FAIL 로 섞임 | BLOCKED 로 분리 |
| 결과 파일 | results.json 1줄 | 케이스별 1줄 + `v22_checks.json` 검사별 상세 |

전체 diff: `fu_evidence/diff_bc_runner.txt`, `fu_evidence/diff_test_hooks_harness.txt`.

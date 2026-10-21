# NEED 라그 마블 — F1·F2 후속 작업 보고서 v1.0

- 작성일: 2026-10-21 (작업 09:13~09:35 UTC)
- 요청: `NEED_MARBLE_F1_F2_FOLLOWUP_VALIDATION_Claude_Request_v1_0.md`
- 범위(G1): 독립 worktree 안 테스트 러너·픽스처 수정, 정적 검사, 오프라인 단위 시험, worktree 전용 빌드, 로컬 커밋. **격리 DB·서버 기동과 런타임 시험은 하지 않았다.**

## 1. 위치와 상태

| 항목 | 값 |
|---|---|
| 브랜치 | `feature/need-marble-phase-bc-v1` (로컬만, 원격 없음) |
| worktree 절대 경로 | `E:\tools\Need\need-marble-phase-bc-v1` |
| 작업 전 HEAD | `19d6984e1` (F1·F2 시험 커밋, 그 앞이 보고 커밋 `f6e620f3a`) |
| 이번 로컬 커밋 | `4848d91db` 러너·픽스처·증거, 이어서 보고서 커밋 1개(`git log -1` 로 확인) |
| 원본 저장소 | `E:\tools\Need\rathena` `need-server` `9f55324d2`, status 7줄 — 작업 전후 같음 |
| 원본 DB·dqtest | 접속 0건. 원본 3306 PID 8680 불변. dqtest·mhtest DB 는 이 작업과 무관하게 바뀜(감사 보고서 2절) |

G0 근거: `NEED_MARBLE_BRANCH_ISOLATION_AUDIT_FOLLOWUP_v1_0.md`.

## 2. F1·F2 보존 확인

| 항목 | 결과 |
|---|---|
| `src/` 변경 | 없음(이번 커밋 19개 파일 모두 `tests/`·`doc/`) |
| F1 마블 훅 위치·식 | 정적 검사 PASS(경험치 분배 앞, `rebirth` 와 같은 최종 처치 식) |
| F1 이후 `mob_dead` 호출 순서 | **새 정적 검사 PASS**: 마블 → 경험치 분배(`pc_gainexp`) → `rebirth` 판정 → NEED 매크로 사냥 → 여름 → 추석. F1 이전 커밋 `e6f808d3d` 에서는 같은 검사가 불통과(마블이 매크로 뒤)해 검사가 실제로 순서를 본다는 것을 확인 |
| F2 두 겹 방어 | 정적 검사 PASS(체크포인트 `FOR UPDATE` 뒤 상한 판정, 로그인·일자당 1회 조회) + 오프라인 픽스처 ③ |
| 운영용 빌드의 시험 명령·고장 주입 | 운영용 exe 에 `need_marble_test`·`MBQA`·`mbqafeed` 0건, `src`·빌드 파일에 `NEED_MARBLE_TEST_HOOKS` 0건 |

## 3. V22 결함과 수정 (요약)

- **원인**: 러너가 R1 과 같은 계정을 쓰면서 누계 400·배치 20을 절대값으로 기대. 원장 동작은 정상(누계 1에서 시작해 401·배치 21).
- **수정**: 기대값을 시작 스냅샷 + 정책 + 코드 경로로 계산하는 `v22_model.py`, V22 전용 합성 계정 2개, 케이스 3개(깨끗한 계정 / 처치 1·배치 1 선행 / 상한 재접속), 생존 확인(`@mbqastate`, 죽은 호출자 주입 거부 `n=-2`), 상한 뒤 전체 스냅샷 불변 검사, 전제 오염은 BLOCKED 로 분리.
- **우회 아님**: 400→401 처럼 숫자만 바꾸지 않았다. 같은 비교기가 5번째 구간 행, 상한 뒤 배치, 누계·배치 불일치, 사냥 125점, 잔액 어긋남, EARN 중복, 옛 배치 변경, 절대값 400을 모두 거부하는 것을 픽스처로 확인했다.
- 상세: `NEED_MARBLE_V22_RUNNER_FIX_REPORT_v1_0.md`.

## 4. 변경 파일 (커밋 `4848d91db`, 19개 파일 +1,211 / −20)

| 파일 | 변경 |
|---|---|
| `tests/need_marble/g2/v22_model.py` | 신규. 기대 행 계산·비교·픽스처 판별·응답 파싱 (순수 Python) |
| `tests/need_marble/g2/v22_model_test.py` | 신규. 오프라인 픽스처 48개 |
| `tests/need_marble/g2/bc_runner.py` | `cmd_v22` 재작성, 스냅샷·정책·논리 일자·생존 확인 도우미, 전용 계정 등록 |
| `tests/need_marble/g2/bc_synth_v22.sql` | 신규. 합성 계정 2000907·2000908 (격리 DB 전용, 비밀값 없음) |
| `tests/need_marble/g2/test_hooks/need_marble_test.cpp` | 시험 빌드 전용 훅: 죽은 호출자 거부, `@MBQA_COUNTED` |
| `tests/need_marble/g2/bc_stack_extras.py` | 하네스 NPC: `counted=` 출력, `@mbqastate` |
| `tests/need_marble/logic_test.cpp` | V22 픽스처 7개(실제 로직 함수 사용) |
| `tests/need_marble/static_check.py` | 호출 순서·V22 러너·전용 계정·훅·합성 SQL 검사 7개 |
| `doc/need_marble/fu_evidence/` | 시험·빌드 로그, diff, exe 해시, 음성 확인 스크립트와 출력 |

이어지는 보고서 커밋: 이 문서, `NEED_MARBLE_V22_RUNNER_FIX_REPORT_v1_0.md`, `NEED_MARBLE_PENDING_RUNTIME_GATE_v1_0.md`, `NEED_MARBLE_BRANCH_ISOLATION_AUDIT_FOLLOWUP_v1_0.md`, `NEED_MARBLE_TEST_REPORT_Phase_BC_v1_0.md`(V18·V22 줄 정정).

## 5. 시험 상태표 (이번 작업에서 실제로 관측한 것만)

| ID | 내용 | 상태 |
|---|---|---|
| G0 | 독립 worktree·브랜치·HEAD·링크·원본 기준선 | **PASS** |
| U1 | C++ 단위 시험 | **PASS** 126 / 0 |
| U2 | V22 비교기 오프라인 픽스처 | **PASS** 48 / 0 |
| S1 | 정적 검사 | **PASS** 79 / 0 |
| S1-neg | 새 검사의 음성 확인(수정 전 러너, F1 이전 mob.cpp) | **PASS** |
| D1 | 러너 V22 도우미 건조 실행(가짜 DB) | **PASS** |
| B1 | worktree 운영용 빌드 | **PASS** (해시 `fc2718c2…` 불변) |
| B1-T | 시험 빌드(스크래치) | **PASS** (첫 시도는 스크래치 복사 실수로 링크 실패 → 보충 후 PASS) |
| V22 | 격리 재실행 | **NOT RUN** |
| V18 | 캡차·지원 스킬 | **PARTIAL** (기존 P2-active 만 PASS), 캡차·지원 **NOT RUN** |
| V11·V16·V21·V24·전장 | | **NOT RUN** |
| V20 | CP 반영 단계 | **BLOCKED** (제품 코드에 반영 단계 없음). DECIDED 단계는 이전 G2 PASS |
| V01·V17 | 시즌 정산 | **BLOCKED** (미구현) |

U1 컴파일 로그의 `'vswhere.exe' is not recognized` 는 vcvars 스크립트가 PATH 에서 vswhere 를 못 찾을 때 내는 안내로, 컴파일·실행(exit 0)에는 영향이 없다.

## 6. 완료·보류

| 구분 | 내용 |
|---|---|
| 완료 | G0, V22 러너 수정, 오프라인 픽스처 3종(+1), U1·U2·S1·B1, 보고서 4종, 로컬 커밋 |
| 보류(승인 필요) | 격리 V22 재실행과 V18-C·V11 (G2-A), V16·V21·V24 (G2-B), 전장 (G2-C) — `NEED_MARBLE_PENDING_RUNTIME_GATE_v1_0.md` |
| 운영 결정 필요 | 캡차를 **유발한** 처치를 마블에서 뺄지(현재 코드는 인정), 전장 최소 경기 시간·표본, 워프 목적지(N10, 정책값 7→13·22→28 은 초안), 마블과 여름·추석의 레벨 판정 시점 차이 공지 |
| 하지 않음 | 원본 DB·서버·작업 트리 변경, 격리 스택 기동, CP 실지급, 운영 배포, 푸시·병합 |

기능 로직 수정이 필요한 실패는 이번 범위에서 발견되지 않았다.

# NEED 라그 마블 — Phase B~C 시험 보고서 v1.0

- 작성일: 2026-10-20
- 대상: `feature/need-marble-phase-bc-v1` (커밋 `a89e6586b`, `65ed985e8`, `9fff4fb49`)
- 표기: **PASS**(실제로 실행해 확인) / **FAIL** / **NOT RUN**(실행하지 않음) / **BLOCKED**(전제 미충족)
- 과거 GM v1.2 이동 코어의 M01~M16 PASS 는 캐릭터 단위 GM 코어의 결과로만 보존한다. **이번 계정 공용 원장 시험에 상속하지 않는다.**

## 1. G1 — 정적·오프라인·worktree 빌드 (이번 요청 범위)

| ID | 항목 | 방법 | 결과 | 근거 |
|---|---|---|---|---|
| G1-1 | map-server Release x64 빌드 | MSBuild `src\map\map-server.vcxproj`, `SolutionDir` = 새 worktree. 의존 lib(ryml·libconfig·yaml-cpp·common)도 worktree `.vs\build` 에 새로 빌드 | **PASS** | exit 0, 산출물 `need-marble-phase-bc-v1\map-server.exe` (6,511,616 바이트). 원본 `rathena\map-server.exe` 는 2026-10-19 08:50 그대로 |
| G1-2 | 마블 소스 컴파일 경고 | 빌드 로그에서 `need_marble` 경고 검색 | **PASS** (최종 0건) | 첫 빌드에서 C4789(정책 배열 256 < 칸 키 303, **실제 버퍼 초과 결함**)를 발견해 512 로 고치고 `static_assert` 추가 |
| G1-3 | map-server-generator 빌드 | 같은 방법 | **FAIL (기존 결함)** | 링크 오류 22건 모두 `need_jf_pattern_*`·`need_storage_shop_*`. 기준 커밋 `373dcfe27` 의 generator 프로젝트에 해당 .cpp 가 없음(직접 확인). 마블 기호 누락 0건 |
| G1-4 | 순수 규칙 단위 시험 | `tests/need_marble/logic_test.cpp` 를 cl 로 단독 컴파일·실행(출력은 세션 스크래치) | **PASS 113 / FAIL 0** | 아래 1.1 |
| G1-5 | 정적 검사 | `python -I tests/need_marble/static_check.py <worktree>` | **PASS 64 / FAIL 0** | 아래 1.2. 첫 실행 FAIL 2건은 검사기 오탐(헤더 주석의 단어, 기존 v1.2 로드 줄)으로 확인 후 검사 조건을 정확히 함 |
| G1-6 | GM v1.2 코어 훼손 여부 | `git diff 373dcfe27..HEAD -- npc/NEED/need_marble_motion_v1_2.txt npc/scripts_need.conf` | **PASS** | 변경 0줄 |
| G1-7 | 하드코딩 비밀값·호스트·절대경로 | 정적 검사 8절 | **PASS** | 신규 파일 8개 0건 |
| G1-8 | 불변식 점검 SQL | `sql-files/need_marble_phase_bc_v1_invariants.sql` | **NOT RUN** | DB 실행 금지. 문법도 DB 로 확인하지 않음 |
| G1-9 | NPC 스크립트 구문 | map-server 파싱 | **NOT RUN** | 서버 기동 금지(G2) |

### 1.1 단위 시험 내용 (113개)

| 묶음 | 확인한 것 |
|---|---|
| 정책 기본값 | 사냥 100(25×4, 400마리)·인던 100(50×2)·전장 100(50×2)·총 200, 하루 이론 주사위 4개, 교환 50, 보유 30, 교환 일일 상한 0, ±30, 33걸음·10,000 CP, 마스터·CP·전장·자동 안전 스위치 OFF |
| 정책 거부 | CP 스위치 ON, 확률 합 9999, 워프 목적지가 워프/START/판 밖, 교환 비용 0, 구간 0, 도전 확률 10001 → 모두 무효 판정 |
| 보드 | 33칸, 도시 14·이벤트 4·보물 3·몬스터 3·워프 2·START·포링·BONUS·세금·보스·던전·감옥 각 1, 워프 7→13·22→28 이 도시 칸 |
| 확률 | 칸 종류마다 합 10000, 도시 경계(5000/5001/9000/9001/10000), 보스·몬스터·보물·이벤트 경계, 범위 밖 추첨값 거부, 던전 안전/도전 경계, 균등 착지 기대값 안전 1,067.4 / 도전 1,085.6 CP(v1.0 3절 1,086 과 일치) |
| 논리 일자 | 2026-10-20 03:59:59 KST → 20261019, 04:00:00 → 20261020, 연말·윤일 경계 |
| 상한 | 활동·전체 상한, 음수 없음, 반복 적립해도 활동 상한 100 에서 멈춤 |
| 구간 | 99/100/399/400마리 경계, 40억 마리에서도 next_tier = 5 (오버플로 없음) |
| 33걸음 | 32/33/34/66걸음, 0 입력 |

### 1.2 정적 검사 내용 (64개)

NPC 가 CP949 로 읽힘 / NPC·v1.2 좌표·칸 이름 일치 / NPC 칸 종류 = C++ `BOARD` / NPC 가 쓰는 정책 키 번호 = 열거값 / 스키마 테이블 = C++ 설치 확인 목록 / 전부 InnoDB / 17개 테이블 숫자 PK / 되돌리기 SQL = 생성 테이블 / 스키마에 ALTER·DROP 없음 / 사냥 배치 키에 기간 포함 / 던전 일일 자격 키 / `next_tier` SMALLINT / 마스터 스위치 OFF 시드 / 마블 코드에 `#CASHPOINTS`·`pc_getcash`·`acc_reg_num` 없음 / CP 행은 DECIDED 로만 INSERT, UPDATE 없음 / CP 스위치 검증 / START·COMMIT·ROLLBACK 문자열이 `ledger_tx` 에만 1번씩 / `*_in_tx` 7개가 트랜잭션을 요구하고 열지 않음 / 스크립트 명령 8개 본문=정의 / NPC 는 정의된 명령만 호출 / 기본 로드 목록에 v2 없음 / v2 는 GM 명령을 바인딩하지 않고 `@marble` 하나만 / 신규 파일에 호스트·비밀번호·절대경로 없음.

## 2. G2 — 격리 DB/서버 실행 시험 (별도 승인 전 실행 금지)

V01~V24 전부 **NOT RUN**. 아래 "구현 대응"은 시험할 코드가 어디 있는지를 적은 것이지 통과했다는 뜻이 아니다.

| ID | 시나리오 (검증 계획 v0.3) | 구현 대응 | 결과 |
|---|---|---|---|
| V01 | 시즌 정산 | 미구현(v1.0 시즌 초기화 없음) | BLOCKED |
| V02 | 키 길이·충돌 | 스키마 숫자 PK | NOT RUN |
| V03 | 사냥 누계 기본 | `hunt_checkpoint` | NOT RUN |
| V04 | 상한 우회 | `credit_in_tx`(EARN 기준) | NOT RUN |
| V05 | COMMIT 응답 유실(롤) | `ledger_tx::commit` fail-closed, `run_session` CAS | NOT RUN |
| V06 | 롤 중 강제 종료 | `need_marble_roll` 한 트랜잭션 | NOT RUN |
| V07 | 캐릭터 전환 재개 | 재개 시 새 `run_session`, 옛 세션 step 거부 | NOT RUN |
| V08 | 도착 이벤트 1회 | 이벤트 PK, 도착 CAS | NOT RUN |
| V09 | 동시 접속 | `FOR UPDATE` + CAS | NOT RUN |
| V10 | 인던·전장 실행 키 | `instance_credit`, `bg_credit` | NOT RUN |
| V11 | 난수·워프·START | `rnd_value`, W1, `start_pass` | NOT RUN |
| V12 | 오류·롤백 | `fault()` | NOT RUN |
| V13 | 같은 던전 다른 인스턴스 | `need_marble_dungeon_daily` | NOT RUN |
| V14 | 수집 세션·배치(A40→B10) | `hunt_buf`, 수집 세션 | NOT RUN |
| V15 | 교환 재접속 이중 방지 | 요청 행·ack·peek | NOT RUN |
| V16 | 체크포인트 중간 오류·중첩 | `ledger_tx` 가드 | NOT RUN (고장 주입 빌드 없음) |
| V17 | 시즌 종료 동시 연산 | 시즌 행 공유 잠금만 | BLOCKED (정산 미구현) |
| V18 | 파티 지원·AFK·캡차 | `hunt_member_ok`, `idle_for` | NOT RUN |
| V19 | 워프·리셋 후 불변식 | jump 저널(WARP), GM 리셋은 v2 에 없음 | NOT RUN |
| V20 | CP 2단계·UNCERTAIN | DECIDED 기록만, 적용 경로 없음 | BLOCKED (적용 경로 미구현·미승인) |
| V21 | 배치에 기간 3개 이상 | PK 에 `period_day` | NOT RUN |
| V22 | max_tiers·오버플로 | `hunt_next_tier`, `done_day` | NOT RUN (규칙 자체는 G1-4 PASS) |
| V23 | 워프·던전 칸 | `need_marble_arrive`, `need_marble_choice` | NOT RUN |
| V24 | 04:00 KST·정책 적용일 | `logical_day`, `policy_refresh` | NOT RUN (계산 자체는 G1-4 PASS) |

G2 전제: 운영자 실행 승인, 격리 MariaDB·서버 포트, 합성 계정, 격리 복사본의 `need_marbl` 맵 등록, `npc/NEED/marble/scripts_marble.conf` import, 격리 DB 에서만 마스터 스위치 ON. 절차는 `NEED_MARBLE_DEPLOYMENT_AND_CP_GATE_v1_0.md` 4절.

## 3. QA 사용자 가이드 (G2 이후 사용)

| 순서 | 조작 | 기대 |
|---|---|---|
| 1 | 마을에서 `@marble` | 점수 0, 주사위 0, 오늘 획득 0/200, 위치 0 출발 |
| 2 | 필드에서 ±30 레벨 몹 100마리 | 체크포인트 후 "사냥 활동 점수 +25" (최대 4회/일) |
| 3 | 등급 A 인던 최종 보스 처치(보스 맵 체류) | "인던 클리어 활동 점수 +50", 같은 던전 같은 날 두 번째는 0 |
| 4 | `@marble` → 교환 → 1개 | 점수 −50, 주사위 +1, 결과 확인 후 메뉴 복귀 |
| 5 | "게임판으로 이동" → "주사위 굴리기" | 대화창이 닫힌 뒤 한 칸씩 이동, 도착 칸 메시지, CP 는 "지급 대기" |
| 6 | 이동 중 접속 종료 → 같은 계정 다른 캐릭터로 `@marble` → 이동 이어가기 | 주사위 차감 없이 남은 칸만 이동 |
| 7 | 7번 칸 도착 | 13번 칸으로 워프, 13번 칸 보상 없음 |
| 8 | 17번 칸 도착 | `@marble` 에 던전 선택 메뉴, 한 번 선택 후 재선택 불가 |
| 9 | 누적 33걸음 | "33걸음 누적 보너스 #1: 10,000 CP (지급 대기)" |
| 10 | 주사위 30개 상태에서 교환 | 거부, 점수 그대로 |

실제 `#CASHPOINTS` 는 어떤 경우에도 바뀌지 않아야 한다(바뀌면 FAIL).

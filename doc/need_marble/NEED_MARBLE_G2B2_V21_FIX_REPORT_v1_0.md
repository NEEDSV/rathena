# NEED 라그 마블 — G2-B2 V21 배치 충돌 최소 수정 보고서 v1.0

- 작성: 2026-10-23 (14:11 UTC 재개)
- 승인 (채팅): G2-B2. V21 "동일 키·상이한 내용 재전송" 결함 최소 수정, 단위·정적 검사, 독립 빌드, 오프라인 회귀, 문서화. 격리 재시험(G2-B3)은 별도 승인.
- 기준: `NEED_MARBLE_V21_BATCH_CONFLICT_FIX_PROPOSAL_v1_0.md`, G2-B1 커밋 `d0f6b3d7f`
- **격리 DB·서버는 띄우지 않았다.** 런타임 재시험 결과는 없음(**NOT RUN**).

## 1. G0

| 항목 | 값 |
|---|---|
| worktree / 브랜치 | `E:\tools\Need\need-marble-phase-bc-v1` / `feature/need-marble-phase-bc-v1` |
| 시작 HEAD | `d0f6b3d7f`, 미커밋 변경 없음 |
| 원본 기준선 | `need-server` `57ee6d79c`, status 7줄, 3306 PID 8908 — 작업 전후 가드 동일 |
| 격리 포트 | 비어 있음(기동 없음) |

## 2. 제품 코드 변경 (최소, 결함 지점만)

| 파일 | 변경 |
|---|---|
| `src/map/need_marble.hpp` | 반환 코드 `NEED_MARBLE_CONFLICT = -21` 추가. 기존 `-20`(`REJECTED`)은 NPC 가 쓰므로 그대로 |
| `src/map/need_marble_logic.hpp` | 순수 함수 `hunt_resend_check(stored, batch, capped_days)`: 저장 행 없음 → FRESH. 저장 행이 전부 배치와 같고, 행이 없는 배치 기간이 전부 "지금 상한인 날"이면 → ALREADY. 그 밖 → CONFLICT |
| `src/map/need_marble.cpp` `hunt_checkpoint` | 기간별 쓰기 **전에** 그 배치 키(계정·수집 세션·배치 번호)의 기존 행을 읽는다(일반 SELECT, 간격 잠금 없음). 행이 있으면 재전송으로 보고 판정해 ALREADY 또는 **CONFLICT(오류 로그 1줄, 아무것도 쓰지 않음)**. 기존 "INSERT IGNORE 0행 → ALREADY"는 **CONFLICT** 로 바꿈(읽은 뒤 끼어든 키를 "반영됨"으로 보지 않음) |
| 〃 `hunt_flush` | CONFLICT 를 받으면 그 로그인의 사냥 적립을 멈춘다(`hunt_buf.conflict`). 충돌 배치는 보내지 않고 메모리에 둔다. 이후 flush 는 바로 반환 |
| 〃 `need_marble_hunt_on_kill` | `conflict` 인 로그인은 처치를 세지 않음 |

`git diff --stat -- src`: 3개 파일. 다른 진입점·트랜잭션 경계·시간 계산은 바꾸지 않았다.

### 2.1 판정 규칙이 여러 논리 일자 배치에서도 일관되는 이유 (승인 조건 6)

- 판정은 **배치 전체**(모든 기간)를 한 번에 본다. 처음 겹친 기간에서 멈추지 않는다.
- 저장 행을 **이번 트랜잭션이 아무것도 쓰기 전에** 읽는다. 그래서 새 기간이 앞쪽에 정렬되는 경우(V21-4b)에도 자기가 방금 넣은 행과 비교하는 일이 없다.
- 체크포인트는 상한인 날의 배치 행을 쓰지 않는다(F2 2층). 따라서 "저장 행이 없는 기간"은 **지금 상한인 날일 때만** 같은 배치로 인정한다. 상한은 같은 날 다시 열리지 않으므로 처음 보낼 때 건너뛴 날은 재전송 때도 상한이다.
- 기간 하나라도 처치 수가 다르거나, 기간이 더 있거나, 빠지면 → CONFLICT.

## 3. 동작 표 (수정 후, 설계상)

| 경우 | 수정 전 | 수정 후 | 원장 변화 |
|---|---|---|---|
| 새 키 | 정상 반영 | 같음 | 반영 |
| 같은 키·같은 내용 (응답 유실 후 재전송, V21-2·V21-5·V16 C11) | ALREADY | **ALREADY** | 0 |
| 같은 키·처치 수 다름 (V21-3) | ALREADY (차이 소실) | **CONFLICT(-21)** + 오류 로그 | 0 |
| 같은 키·기간 추가 뒤쪽 (V21-4) | ALREADY | **CONFLICT** | 0 |
| 같은 키·기간 추가 앞쪽 (V21-4b) | ALREADY (트랜잭션 안에서 앞 기간을 쓰고 롤백) | **CONFLICT**, 쓰기 전 판정 | 0 |
| 같은 키·저장 기간 누락 | ALREADY | **CONFLICT** | 0 |
| 같은 키·상한이라 건너뛴 기간 포함 | 첫 기간이 상한이면 건너뛰고 다음 기간에서 ALREADY 또는 OK | **ALREADY** | 0 |
| 읽은 뒤 다른 연결이 같은 키를 넣음 | ALREADY | **CONFLICT** (롤백) | 0 |

- V16(장애 주입·원자성)과 V24(04:00)는 정상 경로·장애 경로에서 이 판정에 닿지 않는다. V16 C11 재전송은 같은 내용이라 ALREADY 그대로다. 트랜잭션 경계, 잠금 순서(진행 행 `FOR UPDATE` → 배치 INSERT), 시간 계산은 그대로다.
- 추가 비용: 체크포인트마다 배치 키 조회 SELECT 1회(기본키 앞부분 범위, 행 수 = 배치 기간 수)가 늘어난다. 재전송일 때만 기간별 `next_tier` SELECT 가 더해진다.

## 4. 시험 (오프라인, 실제 실행)

| ID | 내용 | 결과 |
|---|---|---|
| U1 | `logic_test.cpp` (재전송 판정 10개 추가: 새 키, 같은 내용, 처치 수 다름, 뒤쪽 기간 추가, 앞쪽 기간 추가, 기간 누락, 빈 재전송, 상한으로 건너뛴 기간 인정·불인정, 상한 날의 저장 행 불일치) | **PASS 148 / 0** |
| U2~U4 | V22 비교기 48/0, V11 분석 16/0, G2-B 오프라인 18/0 | **PASS** |
| S1 | 정적 검사 (V21 수정 5개 추가: 쓰기 전 판정, 0행은 CONFLICT, 반환 코드 −21, flush 의 충돌 처리, 순수 함수 사용) | **PASS 100 / 0** |
| S1-neg | 새 검사 5개를 수정 전 코드(`d0f6b3d7f`)에 돌림 | **5개 모두 FAIL**(수정 후에는 5개 모두 PASS). 첫 시도에서는 새 검사가 문자열을 못 찾으면 예외로 멈춰 FAIL 이 출력되지 않았다. `find()` 기반으로 바꿔 FAIL 로 보고되게 했다. `g2b2_evidence/s1_negative.txt` |
| B1 | worktree 운영용 map-server 빌드 | **PASS**: `need_marble.cpp` 경고 0·오류 0, exe `f406d0e05feaa7a1…`(이전 `fc2718c2…`), 시험 문자열 0. 원본 `rathena\map-server.exe` 불변(2026-10-19 08:50) |
| B-T | 스크래치 시험 빌드(`apply_test_hooks.py --g2b`) | **PASS**: 고정 문자열 3곳이 수정된 코드에 1회씩 일치, 경고·오류 0, exe `7b933f8ddd8bb0da…` |
| 설계 보정 | 첫 구현은 재전송 조회에 `FOR UPDATE` 를 붙였다. 키가 없을 때 InnoDB 간격 잠금이 생겨 잠금 범위가 넓어지므로 일반 SELECT 로 바꿨다(읽은 뒤 끼어든 키는 INSERT 0행 → CONFLICT 로 닫힘). 두 빌드는 최종 코드로 다시 했다 | |
| 러너 | V21-3·4·4b 판정을 **더 엄격하게**: 반환 코드가 정확히 `CONFLICT(-21)` 이고 map 로그에 "hunt batch conflict" 가 있어야 PASS(이전 기준은 "OK·ALREADY 가 아니거나 fault"). 원장 스냅샷 불변 조건은 그대로 | 런타임 미실행 |

## 5. 원장 불변식 검증 계획 (G2-B3 에서)

| 확인 | 방법 |
|---|---|
| CONFLICT 3사례 무변경 | 계정 원장 14개 테이블 스냅샷 전후 동일, 다른 계정·전역 테이블·`#CASHPOINTS` 지문 동일 |
| 같은 내용 재전송 무변경 | V21-2·V21-5·V16 C11 에서 ALREADY + 스냅샷 동일 |
| I11 | 기간마다 `hunt_progress.kills` = Σ`hunt_batch.kills` |
| I10 | `next_tier − 1` = 그 기간 구간 실적 행 수 |
| I1·I2·I9 | 지갑 잔액 = 원장 합, 기간 earned = EARN 합, 활동 credited 합 = EARN 합 |
| 모듈 상태 | CONFLICT 는 fail-closed 플래그(`g_fault`)를 켜지 않는다(그 로그인의 사냥만 멈춤). 다른 계정·다른 로그인 정상 |
| 회귀 | V16 a·b·c, V21 a·b·c, V24 최종, V22 전부 재실행. 불변식 SQL 매 단계 위반 0 |

## 6. 남은 것

- G2-B3 격리 재시험: `NEED_MARBLE_G2B3_RETEST_APPROVAL_REQUEST_v1_0.md` 로 승인 요청
- 충돌 시 계정 RECON 잠금은 하지 않았다(제안서 6절 결정 사항, 최소 수정 범위 밖). 필요하면 별도 결정
- 원본·운영 반영, CP, push·merge: 하지 않음

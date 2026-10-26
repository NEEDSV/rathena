- `V20-REAL: PASS` — 격리 `#CASHPOINTS`(127.0.0.1:3327 ragnarok_mbqa의 acc_reg_num)에 지급이 정확히 1회 반영됐고, 저장·재접속·서버 재기동 뒤에도 같은 값임을 확인했습니다(최종 깨끗한 실행에서 R1~R8 모두 PASS).
- `정합성: 중복 지급 0 / 온라인 덮어쓰기 0 / UNCERTAIN 자동 재시도 0` (실측 범위: 합성 계정 MBQA_CPR·MBQA_CPS, 지급 행 18건, 장애 주입 7종).
- `운영 영향: 원본/타 환경 변경 0 · push/merge/배포 0 · 운영 CP 지급 0`

# NEED 라그 마블 — V20-REAL 실제 CP 지급 연결·격리 검증 보고서 v1.0

- 작성: 2026-10-26 (UTC). 요청서: `NEED_MARBLE_V20_REAL_CP_WRITER_Claude_Request_v1_0.md`
- 브랜치 `feature/need-marble-phase-bc-v1`, worktree `E:\tools\Need\need-marble-phase-bc-v1`
- 커밋: `015922a0a`(코드·SQL·시험), 이 보고서 + 증거(다음 커밋). 직전 커밋 `b259dd936`(V20-MOCK)
- 증거: `doc/need_marble/evidence/v20_real/` (최종 실행 결과·러너 로그·서버 로그·DB 요약·이전 시도 기록, 비밀값 마스킹)
- **이번 결과는 격리 시험 환경의 실측입니다.** 운영 DB 마이그레이션, writer가 포함된 운영 빌드, 정책 키 5 ON은 하지 않았습니다(§7).

## 1. 방식 선택 근거

### 1.1 왜 char-server 처리기인가 (기존 보고서 ②안)

- `#CASHPOINTS`의 영속값은 char DB `acc_reg_num`에 있고, 그 테이블에 쓰는 프로세스는 char-server 하나입니다. char-server는 단일 스레드라서, 같은 계정의 일반 레지스트리 저장(0x3004)과 지급 요청이 섞여 실행되지 않습니다.
- map 쪽 ①안(`pc_getcash` + 저장 강제)은 `cashlog`와 메모리를 먼저 바꾸고, 영속 반영은 응답 없는 0x3004에 맡깁니다. 그래서 "멱등 키 기록과 잔액 갱신을 같은 트랜잭션에서"라는 조건 1을 만족할 수 없습니다.
- map이 `acc_reg_num`을 직접 UPDATE하는 방식은 금지 사항이고, 실제로도 온라인 세션의 뒤늦은 절대값 저장이 덮어씁니다(R6 음성 대조로 재현, §3).
- **전제 조건:** rAthena 기본 스키마의 `acc_reg_num`은 **MyISAM**이라 트랜잭션에 묶이지 않습니다. 그래서 InnoDB 전환 마이그레이션이 필요하고, char-server는 두 테이블이 InnoDB가 아니면 모든 지급을 거부합니다(R3c 실측).

### 1.2 세 조건을 어떻게 만족했나

| 조건 | 메커니즘 | 코드 |
|---|---|---|
| 1. 영속 멱등성 | char-server가 지급 요청 하나를 InnoDB 트랜잭션 하나로 처리합니다. (1) `need_marble_cp_grant`에서 (계정, payout_no)를 `FOR UPDATE`로 읽습니다. (2) `acc_reg_num` `#CASHPOINTS`를 `FOR UPDATE`로 읽습니다. (3) 새 잔액을 `REPLACE`합니다. (4) 멱등 행을 `INSERT`합니다. (5) `COMMIT`합니다. 같은 키·같은 금액이면 `ALREADY`, 다른 금액이면 `CONFLICT`이고 어느 쪽도 쓰지 않습니다. | `src/char/inter.cpp` `need_cp_grant_apply` |
| 1-보강. 무음 재접속 | rAthena SQL 계층은 끊긴 연결을 자동으로 다시 잇습니다(`MYSQL_OPT_RECONNECT`). 재접속된 세션의 문장은 트랜잭션 밖에서 autocommit됩니다. 그래서 두 쓰기는 `WHERE @need_cp_tx='<req_id>'` 조건이 붙어야만 실행되고(새 세션에는 이 변수가 없어 0행), `COMMIT` 뒤에는 이 요청의 멱등 행을 다시 읽어야 `APPLIED`를 답합니다. | 같은 함수 |
| 2. 온라인 동기화 | map→char 패킷은 한 TCP 연결로 순서대로 처리됩니다. 지급 요청을 보낸 뒤 응답이 오기 전까지, 그 **로그인**(char_id + login_id1/2)의 `#CASHPOINTS`는 0x3004의 절대값으로 저장하지 않습니다. 대신 조건부 저장 0x300B(map 값 = 지급 미포함)로 보냅니다. char-server는 **이 요청(req_id)의 멱등 행이 있을 때만** 금액을 더해 저장합니다. 그래서 요청 전에 보낸 저장은 지급보다 먼저 처리되고, 요청 뒤에 보낸 저장은 지급을 지우지 못합니다. 응답(APPLIED)이 오면 그 로그인의 메모리에 금액을 한 번 더하고 펜스를 풉니다. 이후 평범한 저장은 지급을 포함한 값입니다. | `src/map/intif.cpp` `intif_saveregistry` 펜스, `need_marble_cp_char_result` |
| 3. 완료 증명 | `APPLIED`는 char-server가 커밋하고 행을 다시 읽어 답한 경우에만 기록합니다. 응답 유실·map 종료·char 종료가 나면 원장은 `UNCERTAIN`에 머물고 자동 재요청은 없습니다. ACK가 덮어쓰기 부재의 증거가 아니라는 점은 펜스(조건 2)가 따로 보장하고, R6에서 실측했습니다. | `need_marble_cp_apply` / `cp_settle` |

### 1.3 프로토콜 (`src/common/need_marble_cp_grant.hpp`)

| 패킷 | 방향 | 내용 | 길이 |
|---|---|---|---|
| 0x300A | map→char | account_id, char_id, payout_no, req_id(boot_id<<32 \| 순번), amount | 30 |
| 0x300B | map→char | account_id, char_id, payout_no, req_id, base(#CASHPOINTS, 지급 미포함) | 34 |
| 0x380A | char→map | account_id, payout_no, req_id, amount, result, balance_after | 35 |

세 번호 모두 기존 길이 테이블에서 0(미사용)이었습니다(정적 검사로 확인). 결과 코드는 다음과 같습니다.

- `APPLIED`(0)
- 효과 없음 확정: `DISABLED`(1), `BAD_ARG`(2), `CAP`(3) → 원장 DECIDED 복귀, 재시도 허용
- 효과 불명: `ALREADY`(4), `CONFLICT`(5), `DB_ERROR`(6) → UNCERTAIN 유지

한 번에 지급할 수 있는 최대액은 100,000 CP(`GRANT_MAX`)이고, 잔액 상한은 `MAX_CASHPOINT`(INT_MAX)와 같습니다.

### 1.4 상태 전이·감사

기존 `DECIDED(1) → UNCERTAIN(3) → APPLIED(2)` / `→ DECIDED`(효과 없음 확정) / 운영자 `MANUAL_APPLIED(4)·MANUAL_VOID(5)`를 그대로 씁니다. char writer는 비동기라서 쓰기 함수가 `CPW_PENDING`을 돌려주고, 감사 사유 2개를 추가했습니다.

- 감사 사유 6: 요청 송신(3→3)
- 감사 사유 7: char 응답이 이 요청의 반영을 증명하지 못함(3→3, ALREADY/CONFLICT/DB_ERROR)

나머지 동작은 다음과 같습니다.

- **운영자 대사:** `need_marble_cp_grant`가 보이는 환경에서는 char 행이 판정합니다. MANUAL_APPLIED는 같은 금액의 행이 있어야, MANUAL_VOID는 행이 없어야 통과하고, 그 외에는 `CONFLICT`입니다. 운영자도 증거를 거스를 수 없습니다.
- **응답 대기 상한:** 응답을 기다리는 요청은 60초(`CP_ANSWER_WAIT_SEC`) 동안 `BUSY`입니다. char-server는 밀리초 단위로 응답하므로, 그보다 길게 조용하면 응답을 잃은 것으로 봅니다.
- **연결 끊김:** char-server 연결이 끊기면(`chrif_on_disconnect` → `need_marble_cp_char_lost`) 그 연결로 보낸 요청의 응답 대기만 비웁니다. char는 요청이 온 연결로만 응답하기 때문입니다. 저장 펜스는 유지합니다.

### 1.5 활성화 관문 (네 가지가 모두 있어야 지급)

1. **빌드:** `NEED_MARBLE_CP_CHAR_WRITER=1`로 빌드한 map-server. 기본 빌드에는 writer 함수가 컴파일되지 않습니다(§5에서 exe 문자열로 확인).
2. **char 설정:** `inter_athena.conf`의 `need_marble_cp_grant: yes`(기본 off, 설정 파일은 변경하지 않음).
3. **DB:** `need_marble_cp_grant`·`acc_reg_num`이 모두 InnoDB(`sql-files/need_marble_v20_real_cp_grant.sql`). char-server가 기동할 때 확인합니다.
4. **정책:** 키 5 = 1(기본 0). writer가 없는 기본 빌드에서 1이면 정책 검증이 실패해 모듈이 꺼집니다(V20-MOCK 때와 같음).

자동 지급은 다음과 같이 동작합니다.

- **시점:** 결정 직후(도착·33걸음·던전 선택), 보드 조회, 지급 응답 직후에 그 계정의 가장 오래된 DECIDED 1건을 보냅니다.
- **한도:** 로그인당 동시에 1건이고, 다음 건은 앞 건의 응답 뒤에 나갑니다.
- **제외:** UNCERTAIN과 10만 CP 초과 행은 고르지 않습니다.

## 2. 변경 파일·커밋·기준선

| 구분 | 파일 | 내용 |
|---|---|---|
| 공용 | `src/common/need_marble_cp_grant.hpp` (신규) | 패킷 번호·길이, 결과 코드, 검증·잔액·조건부 저장 계산(헤더 전용, 단위 시험 대상) |
| char | `src/char/inter.cpp` | 설정 키, 기동 시 테이블 검사, 0x300A 지급 트랜잭션, 0x300B 조건부 저장, 0x380A 응답 |
| map | `src/map/intif.cpp` / `.hpp` | 0x300A/0x300B 송신, 0x380A 수신, `intif_saveregistry` 펜스 |
| map | `src/map/chrif.cpp` | 연결 끊김 시 `need_marble_cp_char_lost()` 1줄 |
| map | `src/map/need_marble.cpp` / `.hpp` / `need_marble_logic.hpp` | char writer(매크로 빌드 전용), 응답 처리, `cp_settle`, 자동 지급, 증거 기반 운영자 대사, 응답 대기 상한 |
| SQL | `sql-files/need_marble_v20_real_cp_grant.sql` (신규, 미적용 초안) | `acc_reg_num` InnoDB 전환 + `need_marble_cp_grant`, 롤백 절차 |
| 시험 | `tests/need_marble/logic_test.cpp`, `static_check.py` | 단위 +32(201), 정적: V20-REAL 13개 추가 + 의도된 변경에 맞춘 기존 5개 갱신(137) |
| 시험 | `tests/need_marble/g2/*` | 러너 `v20r p0~p4`, 합성 계정, 시험 훅(map·char), 하네스, `build_test.ps1 -Real`, 서버 제어 `-Cp` |

- **worktree:** HEAD `015922a0a`(이 보고서 커밋 전). 미커밋 변경은 이 보고서와 증거뿐입니다.
- **원본 `E:\tools\Need\rathena`:** 이 작업은 원본을 한 번도 수정하지 않았습니다. 편집 도구 기록 59건 모두 worktree 또는 스크래치 대상이고, 원본 대상은 0건입니다. 원본 HEAD 변경은 다른 세션의 작업으로, 2026-10-26 사용자 정책에 따라 WARNING으로만 기록했습니다.
  - 시작: `929a83834`
  - `074c10c23`: 의상 도감
  - `b60d458eb`: 공중요새 점수
  - `a9c0a42ed`: 행알 판매 준비
  - 미커밋 `src/map/skill.cpp`: 다른 세션의 램페이지 블래스터 수정
  - 위 변경 모두 마블 경로와 겹치지 않습니다(`guard_warnings.log`).
- **병합 등:** 원본을 마블 브랜치에 병합·리베이스·cherry-pick하지 않았습니다.

## 3. R1~R8 결과 (최종 깨끗한 실행 2026-10-26 13:41~13:57 UTC)

격리 MariaDB를 새로 설치한 뒤, 같은 시험 exe로 V20-MOCK 회귀 → V20-REAL p0~p4를 이어서 실행했습니다.

시험 계정은 다음과 같습니다.
- MBQA_CPR: 시작 잔액 1000, 캐릭터 2개
- MBQA_CPS: `#CASHPOINTS` 행 없음
- MBQA_CPT: 777, 접속하지 않는 방관자

| ID | 결과 | 실측(최종 실행) |
|---|---|---|
| **R1** 보드 칸 CP 1건 | **PASS** | 자동 지급(키 5 = 1): 롤 19의 칸 CP 1,000이 도착 직후 스스로 지급됐습니다. DB 38,380 → 39,380, 온라인 38,380 → 39,380, 감사 1→3(1), 3→3(6), 3→2(2). 보드 조회 시 이전 DECIDED 5건이 차례로 각 1회 지급됐습니다. 행이 없던 MBQA_CPS는 첫 지급이 `balance_before 0`으로 행을 만들었습니다(DB = 온라인 = 1,500). |
| **R2** 같은 롤 칸+33걸음 / 같은 키 / 다른 캐릭터 | **PASS** | 같은 롤의 칸 1,500 + 33걸음 10,000이 각자 키로 1회씩 지급됐습니다(2,500 → 14,000). 같은 키 재요청은 원장에서 ALREADY(-4)입니다. char에 같은 키를 직접 다시 보내면 같은 금액은 `ALREADY`, 다른 금액은 `CONFLICT`이고 잔액·행 모두 변화가 없습니다. MBQA_CPR2로 바꿔 접속하면 14,000을 읽고, 자기 지급 1회 후 14,500입니다. |
| **R3** 차단 | **PASS** | (a) 키 5 = 0: 롤 18회 결정 17건이 모두 DECIDED, `DISABLED`, 감사 0. (b) char-server 중지(map이 연결 끊김을 인지한 뒤): `REJECTED`(-20), 감사 1→3, 3→1, 행 0, 잔액 1000. char 복귀 후 재시도는 APPLIED 1회(1000 → 2500). (c) DB 미준비(MyISAM, 테이블 없음): 요청은 갔지만 char가 `DISABLED`로 답해 DECIDED로 복귀, 잔액 불변. |
| **R4** ACK 유실·map 종료 | **PASS** | (a) char 커밋 후 응답만 버림: 원장 UNCERTAIN, 행 1, DB P+z, 온라인 P. 그 상태에서 20 차감 + 저장하자 DB가 P−20+z로, 지급이 살아남았습니다. 같은 로그인 재요청은 BUSY(-3), 새 로그인 재요청은 UNCERTAIN(-22)이고 재전송은 0입니다. (b) 요청을 내보낸 직후 map 종료: 응답 없음, char는 1회 반영(행이 증명), 재기동·재접속 시 온라인 = DB. (c) 요청을 내보내기 전 map 종료: 행 0, 잔액 불변. |
| **R5** char 저장 전/후 강제 종료 | **PASS** | (a) 두 쓰기 뒤 COMMIT 전 종료: InnoDB가 롤백해 행 0, DB 불변. 재기동 후 조건부 저장도 금액을 더하지 않았습니다(19,390 → 19,385). (b) COMMIT 후 응답 전 종료: 행 1, DB Q+1,500, 온라인 Q. 재기동 후 5 차감 + 저장 → DB Q−5+1,500(두 번 더하지 않음), 재접속 시 같은 값. 두 경우 모두 2회 지급 0. |
| **R6** 일반 캐시 경합 | **PASS** | 응답을 보류한 상태에서 다음을 실측했습니다(모두 `pc_paycash`/`pc_getcash` 제품 경로 + 저장). ① 100 차감 + 저장, 50 획득 + 저장: DB가 B−100+h1, B−50+h1로 지급이 유지됐고, 응답 후 온라인 = DB = B−50+h1. ② 지급 중 로그아웃: 저장하지 않은 −30이 로그아웃 저장에 실렸고, 다른 캐릭터로 접속하면 M−30+h2. **음성 대조**(시험 훅으로 펜스 끔): 같은 경합에서 평범한 저장이 DB 지급분 500을 지웠습니다(16,420 → 15,910). 펜스가 막는 경합이 실재함을 보였고, map이 계속 떠 있어서 응답 후 재저장으로만 회복됐습니다. |
| **R7** 운영자 대사 | **PASS** | 응답 대기 60초 안에는 BUSY(-3)입니다. 60초가 지나면 char 행이 판정합니다. 행이 있으면 VOID는 `CONFLICT`(-21), MANUAL_APPLIED는 OK(티켓 기록). 행이 없으면 MANUAL_APPLIED는 `CONFLICT`, VOID는 OK. 대사 후 지급 재요청은 ALREADY/BAD_STATE이고, 원래 결정의 자동 재호출은 0입니다. |
| **R8** 회귀·오염 | **PASS** | MOCK p1 14/14, p2 11/11(같은 exe의 모의 모드). v21a 9/9. 주사위·33걸음은 실제 롤로 결정 17건이 재추첨 없이 유지됐습니다. 원장 불변식(sql-files, CP3 포함) 0건 위반. 다른 계정: MBQA_CPT 777 그대로, 지급 행은 시험 두 계정에만, VOID/DECIDED 행에 지급 행 0. 키 5 기본값 0(코드). **BG 점수 재실측은 NOT RUN**입니다. 이번 커밋은 BG 코드를 바꾸지 않았고(정적 검사 BG 항목 통과), 직전 BG-A 실측(7eeb2eb9e)과 같은 코드입니다. |

단계별 검사 수:
- MOCK p1 14/14, p2 11/11
- REAL p0 4/4, p1 21/21, p2 5/5, p3 5/5, p4 8/8
- v21a 9/9

p4의 마지막 항목 "정책 키 5 행 기록"은 기록용이고 판정 항목이 아닙니다.

### 3.1 이전 시도의 실패와 처리 (증거 `earlier_attempts/`)

| 시도 | 결과 | 원인 | 처리 |
|---|---|---|---|
| 1 | MOCK p1 2/14 | 시험 map exe에 훅이 없었습니다. `build_test.ps1`이 정의 2개를 `%3B`로 넘겼고 MSBuild가 **둘 다** 버렸지만 빌드는 경고 0으로 성공했습니다. | 추가 정의를 `CL` 환경 변수 + Rebuild로 넘기고 exe 문자열로 확인. DB를 새로 만들고 다시 실행 |
| 2 | REAL p1 17/20 | R3b: map이 재접속 시도마다 최대 10초 멈춰(rAthena `make_connection`) 명령이 char 복귀 뒤에 처리됨. R4a: 같은 로그인 재요청은 "로그인당 1건" 규칙으로 BUSY인데 기대값을 -22로 잘못 잡음 | R3b는 연결 끊김을 TCP로 확인한 뒤 명령을 보내도록 수정. R4a 기대값은 BUSY로 고치고, 재접속 후 -22를 따로 검사하는 단계를 추가(검사를 늘렸고 완화하지 않음) |
| 2 중간 | 러너 멈춤 | 파이썬에서 서버 기동 스크립트를 파이프로 받으면, 서버가 핸들을 상속해 호출이 돌아오지 않음 | 출력을 파일로 받음 |
| 3·4 | 준비 단계 실패 / 세션 종료로 중단 | 스크립트 안 부트스트랩 타이밍, PowerShell 인자 전달 문제, 세션 종료 | 검증된 bash 절차로 DB를 구성하고 처음부터 다시 실행 |
| 5 | REAL p1 20/21 (R3b만) | DB 증거상 R3b 동작은 정상이었습니다(1→3→1, 행 0, 잔액 불변). 다만 연속 재접속 시도가 응답 패킷 송출도 늦춰 30초 대기를 넘겼고, 대상 행이 p0에서 쓴 행이라 감사 비교도 섞였습니다. | 대기 75초, p0에서 쓰지 않은 행 선택. 최종 실행 21/21 |

중복 지급·허위 확정·원장 위반은 어느 시도에서도 나오지 않았습니다. 실패는 모두 시험 빌드·시험 순서 문제였고, 매번 DB를 새로 만들어 처음부터 다시 실행했습니다.

## 4. 원장·회계 (최종 실행 끝 상태, `db_summary.txt`)

- **MBQA_CPR:** 결정 18건 = APPLIED 13 + MANUAL_APPLIED 3 + MANUAL_VOID 2, UNCERTAIN 0.
  - 지급 행 16건 = APPLIED·MANUAL_APPLIED 16건과 1:1 대응이고 금액이 모두 같습니다. VOID 2건과 DECIDED 행에는 지급 행이 없습니다.
  - 잔액 39,380 = 시작 1,000 + 지급 행 합 38,500 + 일반 변경 −120. DB와 온라인이 같습니다.
- **MBQA_CPS:** 1건 1,500, DB = 온라인.
- **지급 행 산술:** 모든 행에서 `balance_after − balance_before = amount`입니다.
- **중복·허위·오염:** 중복 지급 0(키 하나당 행 하나는 PK가 보장하고, ALREADY/CONFLICT 경로도 실측했습니다). 허위 APPLIED 0. 다른 계정 오염 0.
- **cashlog로 검증되는 범위:** 지급 기록은 그 로그인이 APPLIED 응답을 받았을 때만 남습니다(`log_cash`는 sd가 필요). 응답이 로그인에 닿지 못한 4건(로그아웃 1,000, R4a·R4b·R5b 각 1,500, 합 5,500)은 지급됐지만 cashlog에 없습니다. **cashlog는 보조 기록이고, 완료 판정과 대사는 `need_marble_cp_grant`가 기준입니다.**

## 5. 회귀·빌드

| 항목 | 결과 |
|---|---|
| 단위 `logic_test` | 201/0 (V20-REAL +32: 인자 검증·잔액 상한·조건부 저장·결과 매핑·패킷 길이 static_assert) |
| 정적 `static_check` | 137/0 (V20-REAL 13개: 패킷 표 일치, 트랜잭션·가드 쓰기, 펜스 위치, map의 `acc_reg_num` 직접 쓰기 0, 로그인 단위 펜스, 자동 지급 대상, 증거 기반 대사, 마이그레이션, 키 5 기본값, 시험 훅 격리). 의도된 변경에 맞춰 기존 5개 갱신(예: 캐시 코드 0 → `need_marble_cp_char_result`의 APPLIED 반영 1곳만 허용) |
| 운영 기본 빌드(worktree map·char) | 경고 0. map exe에 `CP char writer`·`need_marble_test`·모의 지급기 문자열이 없습니다. char exe에는 지급 처리기가 있지만(설정 off면 `DISABLED`) 시험 훅은 없습니다. |
| 시험 빌드(스크래치 복사본) | map FE88289C…(훅 + writer), char 53CB8064…(장애 주입 훅). 시험 동안 가드가 경로·해시를 감시했습니다. |

## 6. 격리·정리

- **사용 범위:** 127.0.0.1:3327 `ragnarok_mbqa`(이 작업이 새로 설치), login 26901 / char 26121 / map 25121 / wiki 26906. 강제 종료와 재기동은 이 스택의 exe만 대상으로 했습니다(`bc_servers.ps1`이 PID 경로 확인).
- **가드:** BLOCK 조건(worktree 브랜치·HEAD·시험 소스·exe 해시, DB 설정, 3306/3309 소유자, 격리 포트 소유)은 시험 내내 한 번도 걸리지 않았습니다. 원본 HEAD 변경은 WARNING 3건으로 기록했습니다.
- **원본 DB 3306(PID 8908)·mhtest 3309(PID 17360):** 처음부터 끝까지 같은 PID였고 접속·쓰기를 하지 않았습니다. dqtest(3307)도 건드리지 않았습니다.
- **정리:** 격리 서버 3종과 MariaDB 3327을 정상 종료했습니다(격리 포트 비어 있음). 스크래치 1.27 GB(스택·DB·시험 빌드)를 삭제했습니다.
- **배포 등:** push·merge·배포 0.

## 7. 운영 적용 전 별도 승인이 필요한 항목 (이번 요청의 승인 범위 밖)

1. **DB 마이그레이션:** `sql-files/need_marble_v20_real_cp_grant.sql`. `acc_reg_num` InnoDB 전환은 테이블 복사와 잠금을 동반하므로 점검 시간에 백업 후 실행해야 합니다.
2. **writer 포함 운영 빌드:** map-server `NEED_MARBLE_CP_CHAR_WRITER=1`, char-server는 이번 코드.
3. **char 설정:** `need_marble_cp_grant: yes`.
4. **정책 키 5 = 1과 실제 지급 개시:** 켜는 순간 기존 DECIDED 행(V20 초안 보상액)도 자동 지급 대상이 됩니다. 켜기 전에 DECIDED 잔량과 금액 정책을 확정해야 합니다.
5. **운영자 대사 절차:** UNCERTAIN 조회는 `need_marble_cp_payout`(status 3)과 `need_marble_cp_grant`를 (계정, payout_no)로 맞춰 봅니다. 대사 명령은 지금은 시험 하네스로만 호출되므로, 운영용 GM 명령이나 도구를 정해야 합니다.
6. **MANUAL_VOID 후 재지급(제안만, 미구현):** 원래 payout을 되살리지 않습니다. 별도 식별자(예: src_type 9 = 보정, src_a = 원래 payout_no, src_b = 티켓)로 새 DECIDED 행을 만들고, 운영자 승인 감사 기록을 남긴 뒤 정상 경로로 지급합니다.

## 8. 남은 위험·한계

- **응답 유실 시:** 응답을 잃은 로그인은 재접속 전까지 다음 지급을 보내지 않고, 화면 잔액이 그 금액만큼 적게 보입니다. DB 잔액은 조건부 저장 덕분에 정확하고, 재접속하면 맞춰집니다.
- **늦은 응답:** char-server가 연결은 살아 있는데 60초 넘게 늦게 응답하고, 그 사이 운영자가 VOID를 하면 원장과 잔액이 어긋날 수 있습니다. 늦은 응답은 기록하지 않고 경고만 남깁니다. 실측에서 응답 시간은 밀리초 단위였습니다.
- **조건부 저장 조회 실패:** char DB 오류로 조건부 저장의 조회가 실패하면 아무것도 쓰지 않습니다. 지급은 보존되지만, 그 저장에 실린 일반 변경은 다음 `#CASHPOINTS` 저장 때까지 DB에 반영되지 않습니다.
- **외부 직접 쓰기:** 웹·외부 도구가 `acc_reg_num`에 직접 쓰는 기존 위험은 이번 범위 밖입니다. 펜스는 map 저장만 다룹니다.
- **char 중단 중 map 정지:** char-server가 꺼져 있는 동안 map은 재접속 시도마다 최대 10초 멈춥니다. rAthena 기존 동작이고, 이번 변경과는 무관합니다.
- **BG 점수:** 이번에 재실측하지 않았습니다(§3 R8).

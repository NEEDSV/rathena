- `V20-SAFETY: PASS` — 늦게 도착한 지급 요청·응답이 운영자 VOID와 부딪히지 않습니다. VOID는 char-server가 그 지급 키에 취소 표지(tombstone)를 커밋한 뒤에만 확정되고, 이후 그 키의 요청은 늦게 와도 지급되지 않습니다(격리 L1~L4 PASS, L5 회귀 PASS).
- `GM 대사: PASS` — `@mbcp info|resolve`를 GM 레벨 99만 쓸 수 있습니다. 조회는 읽기 전용이고 대사는 증거가 있을 때만 됩니다. 일반 유저, 레벨 0 그룹 1(all_commands 보유), 잘못된 입력은 모두 거부되며 상태·잔액 변화가 없습니다.
- `실클라이언트: NOT RUN` — 이 세션에는 화면 조작 수단이 없어 실제 클라이언트를 실행하지 않았습니다. 준비 상태와 절차는 체크리스트에 있습니다.
- `운영 영향: 원본/운영 DB/타 세션 변경 0 · 실계정 CP 지급 0 · push/merge/배포 0`

# NEED 라그 마블 — V20 지급 안전장치 + GM 대사 + 실클라 준비 보고서 v1.0

- 작성: 2026-10-26 (UTC). 요청서: `NEED_MARBLE_V20_FINAL_SAFETY_AND_CLIENT_PLAYTEST_Claude_Request_v1_0.md`
- 브랜치 `feature/need-marble-phase-bc-v1`, worktree `E:\tools\Need\need-marble-phase-bc-v1`
- 커밋: 시작 HEAD `5f66289c6` → 코드·시험 `9100bc5c8` → 이 보고서·체크리스트·증거·이전 보고서 수치 정정(다음 커밋)
- 체크리스트: `doc/need_marble/NEED_MARBLE_REAL_CLIENT_PLAYTEST_CHECKLIST_v1_0.md`
- 증거: `doc/need_marble/evidence/v20_safety/` (검사 결과 JSON, 러너·서버 로그, DB 요약, 원장 불변식, 클라 맵 검사, 가드 기록. 비밀값 마스킹)

## 1. 늦은 응답 vs 운영자 VOID — 어떻게 막았나 (A)

### 1.1 문제

V20-REAL에서는 응답이 60초 넘게 없고 char 지급 행도 보이지 않으면 운영자가 MANUAL_VOID를 할 수 있었습니다. 하지만 지연된 요청이 그 뒤에 char에 도착해 커밋하면 "원장은 VOID인데 잔액에는 지급됨"이 됩니다. 조회 한 번에서 행이 없다는 것은 앞으로도 지급되지 않는다는 증거가 아닙니다.

### 1.2 구현 (권장안: char-server 원자적 취소 표지)

| 경로 | 동작 |
|---|---|
| VOID 요청(map) | 원장이 UNCERTAIN이고 char 지급 행이 보이지 않으면, map은 원장을 바꾸지 않고 0x300C를 char에 보냅니다(감사 사유 10 "VOID 송신"). char 미연결이면 `NO_CHAR(-23)`로 끝나고 아무것도 기록하거나 보내지 않습니다. 지급 행(state 1)이 보이면 `CONFLICT`로 거부합니다(감사 사유 8). |
| VOID 처리(char) | 지급과 같은 키 잠금 안에서 처리합니다(`need_marble_cp_grant` PK, `FOR UPDATE`, 한 트랜잭션). 지급 행이 있으면 `APPLIED_EXISTS`로 거부하고, 없으면 취소 표지(state 2, operator_aid·ticket)를 `@need_cp_tx` 가드로 커밋한 뒤 다시 읽어 확인합니다. 이미 같은 금액의 표지가 있으면 그대로 VOIDED(멱등)입니다. |
| VOID 확정(map) | char가 `VR_VOIDED`로 답하고 원장이 여전히 UNCERTAIN·같은 금액일 때만 MANUAL_VOID로 바꿉니다(ticket·GM 기록). 그 로그인의 저장 펜스는 이때 풉니다. 표지가 키를 막으므로 더 필요 없습니다. 그 외 응답은 원장을 그대로 두고 감사를 남긴 뒤 GM에게 `PENDING / NEEDS_RECONCILIATION`으로 안내합니다. |
| 지급 요청(char) | 표지가 있는 키는 `GR_VOIDED(7)`를 답하고 아무것도 쓰지 않습니다. map은 이를 "불명"으로 봐서 원장을 확정하지 않습니다. |
| MANUAL_APPLIED | 같은 계정·payout·금액의 지급 행(state 1)이 커밋돼 있을 때만 허용합니다. 상태만 바꾸고 CP를 더하지 않습니다. |
| 늦은 응답 | 원장이 이미 닫힌 행에 대한 char 응답은 감사(사유 9, s→s)만 남기고 원장·잔액을 바꾸지 않습니다. 실제로 지급된 응답이면 그 로그인 메모리에만 1회 반영됩니다(DB에는 이미 있음). |
| 증거를 못 읽는 환경 | char writer 빌드인데 map이 `need_marble_cp_grant`를 읽지 못하면 대사 자체를 거부합니다(DISABLED). |
| 부분 실패 | char 표지 커밋 뒤 map 원장 기록이 실패하면 UNCERTAIN으로 남고, 같은 VOID를 다시 하면 멱등으로 끝납니다. 자동 재지급이나 임의 확정은 없습니다. 연결이 끊기면 VOID 대기도 비우며, 다시 요청해도 안전합니다. |

### 1.3 L1~L5 결과 (최종 실행 2026-10-26 18:33~18:56 UTC, 격리 127.0.0.1:3327)

| ID | 결과 | 이벤트 순서·주입 지점 / 실측 |
|---|---|---|
| **L1** | **PASS** | ① 시험 훅으로 지급 요청 A(1,000 CP)를 map 안에 붙잡아 char에 늦게 도착하게 함 → ② 60초 안의 GM VOID는 `BUSY(-3)`, 원장 UNCERTAIN, char 행 없음 → ③ 60초 뒤 VOID → char가 취소 표지 커밋(state 2, GM 2000901, ticket 8102) → 원장 MANUAL_VOID. 감사 1→3(1), 3→3(6), 3→3(10), 3→5(5). DB·온라인 잔액 22,890 그대로 |
| **L2a** | **PASS** | ④ 붙잡아 둔 요청 A를 VOID 뒤에 char로 보냄 → char `GR_VOIDED(7)`, 쓰기 0 → map은 늦은 응답 감사(5→5, 사유 9)만 남김. 잔액 22,890 그대로, 펜스 해제 상태 |
| **L2b** | **PASS** | 지급 D(1,000)를 char가 커밋한 뒤 응답을 붙잡아 둠(DB 23,890, 온라인 22,890) → 60초 뒤 GM APPLIED로 MANUAL_APPLIED → 그 뒤 늦은 0x380A 응답을 전달 → 로그인 메모리에 1회 반영(온라인 = DB = 23,890), 원장 MANUAL_APPLIED 유지, 감사 4→4(9), 저장 후에도 중복 0 |
| **L3** | **PASS** | 같은 D: 60초 안 GM 대사는 `BUSY`. VOID는 `CONFLICT(-21)`(지급 행 있음), APPLIED는 OK(상태만 변경, DB +1,000 한 번). APPLIED 반복은 `BAD_STATE(-10)`이고 잔액 변화 없음 |
| **L4** | **PASS** | 요청을 내보내기 전 map 종료 → UNCERTAIN, 행 없음. char 중지 상태에서 GM VOID는 `NO_CHAR(-23)`로 아무것도 기록하지 않음 → char 복귀 후 VOID → 표지 커밋 → MANUAL_VOID(ticket 8302). 같은 키로 지급을 다시 요청하면 `GR_VOIDED` → char·map 재기동 후 다시 요청해도 `GR_VOIDED`. 잔액 23,890 그대로, 이후 APPLIED 대사는 `BAD_STATE` |
| **L5** | **PASS** | 새 코드로 V20-REAL p0 4/4, p1 21/21(R3b·R2·R4a·R6·R7 핵심 회귀). V20-MOCK p1 14/14, p2 11/11(모의 지급기의 기존 대사 경로). |

**최종 상태**(`db_summary.txt`, `g2c_leftover_payout9.json`):
- MBQA_CPR 잔액 23,890 = 시작 1,000 + 지급 행(state 1) 합 + 일반 변경. DB와 온라인이 같습니다.
- 취소 표지 4건은 모두 MANUAL_VOID 행에만 있습니다.
- 표지 aware 불변식 0건 위반, sql-files 원장 불변식 0건, UNCERTAIN 잔여 0, MBQA_CPT 777 그대로입니다.
- 중간에 끊긴 실행이 남긴 UNCERTAIN 1건(payout 9)은 `@mbcp`로 대사했습니다(표지 후 MANUAL_VOID, ticket 8901).

## 2. GM `@mbcp` (B)

### 2.1 명령·권한

- `@mbcp info <account_id> <payout_no>` — 읽기 전용입니다. 다음을 보여 줍니다.
  - 원장 상태·금액·결정 시각·티켓
  - char 행(없음 / APPLIED / VOID 표지, 금액 일치 여부)
  - 응답 대기(60초 안/밖), VOID 대기
  - 최근 감사 5건(티켓·GM)
  - 다음에 가능한 조치
- `@mbcp resolve <account_id> <payout_no> applied|void <ticket>` — 티켓은 0이 아닌 숫자여야 합니다. 서버가 같은 계정·결정·금액의 증거를 다시 확인합니다. VOID는 §1의 char 경로를 거칩니다(결과는 char 응답 후 GM에게 따로 표시).
- **권한:** 커스텀 atcommand로 등록했고, 명령 안에서 **GM 레벨 99를 다시 확인**합니다. 이유는 NEED `conf/groups.yml`에서 **그룹 1 "Super Player"(Level 0)가 `all_commands: true`**이고 그룹 2도 이를 상속하기 때문입니다. 그룹 권한만으로는 레벨 0 계정도 이 명령을 쓸 수 있습니다(§6 관찰 사항).
- **기록:** 대사·거부·VOID 송신은 `need_marble_cp_audit`(operator_aid, ticket)에, 조회·대사 호출은 map 로그(ShowNotice)에 남습니다. 개인정보·DB 접속 정보는 출력하지 않습니다.
- **writer와 무관:** 명령이 있다고 writer가 켜지거나 지급이 시작되지 않습니다. 기본 빌드에도 명령은 있지만 writer는 없습니다(exe 문자열로 확인).

### 2.2 실제 출력 예시 (격리 실행 로그)

```
@mbcp info 2000943 9
[mbcp] aid 2000943 payout 9: UNCERTAIN(3) amount 1000 granted 0 char 150954 src 1/10 decided 2026-10-26 18:36 ticket 0
[mbcp] char row: none
[mbcp] pending: grant answer none, VOID answer none
[mbcp] audit (newest first): 1>3 r1;
[mbcp] next: resolve void <ticket> (the char-server commits a tombstone first)

@mbcp resolve 2000943 12 void 8301      (char-server 중지 중)
[mbcp] resolve void aid 2000943 payout 12: rc -23 - refused: char-server not connected - nothing changed, PENDING

@mbcp resolve 2000943 9 void 8901
[mbcp] resolve void aid 2000943 payout 9: rc 3 - VOID sent to the char-server - the result follows (the ledger changes only after its tombstone)
[mbcp] aid 2000943 payout 9: MANUAL_VOID done (char-server tombstone committed, the key never pays).
```

### 2.3 검증 (s1 10/10, s2 6/6)

- **권한:** GM 99는 조회됩니다. 일반 유저(그룹 0)는 응답이 없습니다(명령 없음). 그룹 1(레벨 0, all_commands)은 `GM level 99 only.`로 조회·대사 모두 거부되고 변화가 없습니다.
- **입력 검사:** 오타·잘못된 대상·ticket 0·문자 ticket은 사용법 안내만 나옵니다. 없는 지급 번호·다른 계정은 `NOT_FOUND`, 이미 닫힌 행은 `BAD_STATE`입니다. 전후 원장·지급 행·잔액이 모두 같았습니다.
- **증거 판정:** APPLIED는 지급 행이 있을 때만 됩니다. 반복 호출해도 재지급은 0입니다. 응답 대기 중이면 BUSY, char가 없으면 NO_CHAR, 금액이 충돌하면 CONFLICT로 모두 실패 측(fail-closed)입니다.
- **감사:** GM 명령 이후의 운영자 감사 12건 모두 GM 계정과 티켓이 남았습니다.

## 3. 실클라이언트 준비 (C) — `NOT RUN`

- **실행하지 않은 이유:** 이 세션에는 데스크톱 화면 조작 도구가 없습니다. 실클라 PASS로 적지 않았고 스크린샷도 없습니다.
- **준비한 것:** 체크리스트(접속·계정·15단계 조작표·UX 관찰 항목)와, 기존 클라를 건드리지 않고 격리 서버 전용 시험 클라 폴더를 만드는 `tests/need_marble/client/make_mbqa_client.ps1`.
- **파일 기준 점검 결과:**
  - 클라 `need_marbl.gat`와 서버 맵 캐시가 같습니다(셀 차이 0). 33칸과 칸 사이 경로가 모두 보행 가능합니다.
  - 칸 29→30만 8셀 간격입니다.
  - KR 리소스에 맵·칸 텍스처는 있지만 `need_marbl` **미니맵이 없습니다**.
  - EN 리소스에는 `need_marbl`이 **없습니다**(EN 클라 진입 불가 가능성).
  - 서버 맵 등록(3개 파일)은 원본의 **미커밋 변경**입니다.

## 4. 이전 보고서 수치 정정 ("지급 행 18건")

- V20-REAL 증거 `evidence/v20_real/db_summary.txt` 기준:
  - 지급 행(`need_marble_cp_grant`) = **17건**(CPR 16 + CPS 1)
  - MBQA_CPR 결정(payout) 행 = 18건(CPS 포함 19건)
- 보고서 첫머리의 "지급 행 18건"은 결정 행 수를 지급 행으로 잘못 적은 것입니다. 이전 보고서 해당 줄을 **17건으로 고치고 정정 표기**를 남겼습니다(이번 커밋).

## 5. 단위·정적·빌드

| 항목 | 결과 |
|---|---|
| 단위 `logic_test` | 210/0 (+9: VOID 패킷 레이아웃, `GR_VOIDED` → UNCERTAIN 유지, VOID 인자 검증) |
| 정적 `static_check` | 147/0 (+10: VOID 패킷 표, 표지가 지급보다 먼저 판정, VOID 트랜잭션·가드·재확인, 조건부 저장 state 1, map VOID 무원장 변경, VR_VOIDED만 확정, 늦은 응답 감사, GM 레벨 확인 순서, info 읽기 전용, 마이그레이션 열). 의도된 변경에 맞춰 4개 갱신(UPDATE 경로 3 → 4 등) |
| 운영 기본 빌드 | map `B4EF01D9…`, char `6B06ED2F…`, 경고 0. map에는 `@mbcp`가 있고 writer·시험 코드는 없습니다. char에는 VOID 처리기가 있고 시험 코드는 없습니다. |
| 시험 빌드 | map `F6736696…`(훅·writer·요청 보류), char `6EC5A9B6…`(장애 주입). 시험 동안 가드가 경로·해시를 감시했습니다. |

중간 실패와 처리:
1. 처음 시험 빌드는 훅 적용 앵커가 2곳에서 일치해(`cp_gm_info`가 같은 줄을 가짐) 훅 없이 빌드됐습니다. 변수명을 바꾸고, 적용이 실패하면 빌드를 멈추도록 했습니다.
2. s1 1차 실행은 시험 클라이언트가 0x008E(`clif_displaymessage`)를 읽지 못해 `@mbcp` 출력 6건을 판정하지 못했습니다(실제 동작은 정상이었고 DB에 표지·VOID 기록이 남음). 시험 클라를 고쳐 다시 실행했고, 1차 결과는 `earlier_attempts/s1_no_0x008e`에 있습니다.

## 6. 남은 위험·관찰

- **그룹 권한(관찰, 변경 안 함):** `conf/groups.yml`에서 레벨 0 그룹 1(그룹 2도 상속)이 `all_commands: true`입니다. 다른 커스텀·기본 명령도 이 그룹에 열려 있을 수 있으므로 운영 권한 점검을 권합니다.
- **VOID 대기 중 연결 끊김:** char가 표지를 커밋했는지 map이 모르면 원장은 UNCERTAIN으로 남습니다. 같은 VOID를 다시 하면 멱등으로 끝납니다.
- **원장과 표지 불일치:** 운영자가 VOID한 뒤 char 표지만 남고 map 원장 기록이 실패한 상태는 `@mbcp info`에서 "tombstone already committed"로 보입니다. 다시 대사하면 됩니다.
- **GM 출력 문구:** 지금은 영문입니다. 한글은 msg_conf 정리와 함께 할 것을 권합니다.
- **보정 지급:** MANUAL_VOID 이후의 재지급은 이번 범위에서 제외했습니다(후속 결정).

## 7. 격리 정리·원본 무변경

- **사용 범위:** 127.0.0.1:3327 `ragnarok_mbqa`(새로 설치), login 26901 / char 26121 / map 25121 / wiki 26906. 끝난 뒤 서버 3종·MariaDB를 정상 종료했고(격리 포트 비어 있음) 스크래치 1.27 GB를 삭제했습니다.
- **다른 환경:** 3306(PID 8908)·mhtest 3309(PID 17360)는 그대로였고, dqtest 3307은 이번 작업 동안 리슨하지 않았으며 접속하지 않았습니다. `E:\ttt`의 다른 시험 클라는 읽기만 했습니다.
- **원본:** 수정 0. 원본 HEAD 변경(`074c10c23` → `b60d458eb` → `a9c0a42ed`)과 다른 세션의 미커밋 `skill.cpp`는 가드 WARNING으로만 기록했습니다(참고 사항). 마블 브랜치로의 병합·cherry-pick은 없습니다.
- **배포 등:** push·merge·운영 배포 0.

## 8. 다음 단계 (이번에 실행하지 않음)

1. 운영자가 체크리스트대로 실클라 한 판을 플레이하고 피드백합니다(말판 크기·칸 간격·연출·보상 체감).
2. 운영 전 별도 승인:
   - `acc_reg_num` InnoDB 마이그레이션 + `need_marble_cp_grant`·감사 테이블 새 열
   - char writer 설정, writer 포함 map 빌드
   - DECIDED 잔량·금액 확정
   - 키 5 ON
3. 최신 원본(일일 퀘스트·전장 수정 포함)과의 통합 충돌 점검, 서버 맵 등록 3개 파일 커밋, 미니맵·EN 리소스 보완.

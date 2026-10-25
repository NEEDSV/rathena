# NEED 라그 마블 — 전장 자동 보상 수정 + V20 CP 지급 준비 보고서 v1.0

- **BG 자동 보상: PASS** — AFK 한 명이 자동 지급 루프를 세우던 결함을 고쳤다. 뒤 참가자의 기존 전장 보상이 첫 자동 지급에서 0.1초 안에 지급된다(티에라·플라비우스·KVM 실측).
- **V20: MOCK PASS** — 결정 원장 → 모의 지급기 → 불확실 상태 차단·운영자 대사까지 격리 실측했다. **실제 char-server `#CASHPOINTS` 지급 구현·검증은 NOT RUN(별도 승인 필요)**.
- **운영 영향**: 원본 트리·3306·dqtest·mhtest 변경 0, 실제 CP 지급 0, push·merge·배포 0.

---

- 실행: 2026-10-25 11:32~12:12 UTC
- 요청서: `NEED_MARBLE_BG_AUTO_REWARD_FIX_AND_V20_CP_PREP_Claude_Request_v1_0.md` (구현·격리 시험·로컬 커밋 일괄 승인)
- 브랜치: `feature/need-marble-phase-bc-v1`, worktree `E:\tools\Need\need-marble-phase-bc-v1`
- 시작 HEAD: `cb29eb4a1` (G2-C 보고서). 미커밋 변경 없음, 다른 세션 작업 없음
- 로컬 커밋 2개(각각 되돌릴 수 있게 분리):
  - `7eeb2eb9e` 작업 A: 전장 자동 보상
  - 작업 B: 이 보고서와 함께 커밋 = 종료 HEAD
- 증거: `doc/need_marble/evidence/bg_auto_reward/`, `doc/need_marble/evidence/v20_mock/`. 비밀값 검사 0건, login-server 로그 제외
- 원본 기준선: `929a83834`, status 7줄, 3306 PID 8908. 가드 스크립트로 단계마다 확인했고, 마지막 확인은 12:11:56 UTC에 통과했다

## 1. 변경 요약

### 1.1 작업 A — 전장 자동 보상 (`7eeb2eb9e`)

| 함수 | 변경 전 | 변경 후 |
|---|---|---|
| `F_BG_RM_AutoRewardBadge` (bg_common.txt) | 참가자마다 `F_BG_Badge` 호출 → 그 안의 `close2`가 응답을 기다림. AFK 한 명에서 루프 정지 | `F_BG_Badge`를 4번째 인자 `1`로 호출 → 대화창 없이 지급만 하고 다음 참가자로 진행 |
| `F_BG_Badge` (bg_common.txt) | 승패 분기 안에 지급량 계산·패배 BG 포인트 +3·대화문이 섞여 있고 `close2` 뒤에 getitem | 지급량·패배 BG 포인트를 먼저 정하고, 대화문+`close2`는 `if (!getarg(3,0))` 블록으로 분리. getitem 3개는 그 뒤 그대로 |
| 보상 NPC (tierra01/flavius01, 16곳) | 3인자 호출 | **변경 없음**: 대화 UX 그대로 |
| KVM (`F_KvM_AutoRewardRandom` → `F_KvM_GrantReward`) | 원래 대화창 없음 | **변경 없음**: 실측으로 정체가 없음을 확인 |

- 이 방식을 고른 이유는 변경 범위가 가장 작기 때문이다. 공통 지급 함수를 새로 만드는 대신, 대화문만 인자 하나로 건너뛴다. 품목·수량·승패 규칙 코드는 한 줄도 바꾸지 않았다.
- 중복 방지는 그대로다. 기존 `F_BG_RM_TryClaimReward` 키(`토큰×10+모드`, 캐릭터 임시변수)를 쓴다.
- 마블 50점은 G2-C 사전 적립 루프를 유지했다. `F_BG_Badge` 안에서 다시 호출되면 `ALREADY`로 끝난다.
- 파일 형식(CP949·CRLF)을 보존했다.

### 1.2 작업 B — V20 준비 (이 보고서 커밋)

| 파일 | 변경 |
|---|---|
| `src/map/need_marble_logic.hpp` | `validate_policy(..., cp_writer)`: 키 5 = 1은 **지급기가 등록된 경우에만** 유효하다. `e_cp_status` 1~5, `e_cp_write`(APPLIED / NO_EFFECT / UNKNOWN), `cp_status_after_write`, `cp_resolvable` 추가 |
| `src/map/need_marble.cpp` / `.hpp` | 아래 상태 전이를 구현한 `need_marble_cp_set_writer` / `need_marble_cp_apply` / `need_marble_cp_resolve`. 반환값 `NEED_MARBLE_UNCERTAIN = -22` 추가 |
| `sql-files/need_marble_v20_cp_apply.sql` (신규) | 감사 테이블 `need_marble_cp_audit`만 추가. 기존 테이블은 변경 없음(`need_marble_cp_payout`에는 상태 1~5·granted·applied_at·operator_ticket 칸이 이미 있었다). 격리 DB에만 적용했다 |
| `sql-files/need_marble_phase_bc_v1_invariants.sql` | CP3 규칙을 V20 정의로 교체했다(4.3절) |
| `tests/.../test_hooks/need_marble_test_v20.inc` (신규), `apply_test_hooks.py --v20` | **시험 빌드 전용** 모의 지급기와 시험 명령 |
| `tests/.../bc_stack_extras.py --v20`, `bc_runner.py v20 p1/p2`, `bc_synth_v20.sql` | 하네스, 일반 계정 2개, 모의 잔액 테이블 2개 |
| `tests/need_marble/logic_test.cpp`, `static_check.py` | 단위 시험 13개 추가(169/0), 정적 검사 V20 항목 추가(122/0) |

**상태 전이와 감사 기록**

```
DECIDED(1) --claim, 첫 트랜잭션 COMMIT--> UNCERTAIN(3) --지급기 호출-->
   APPLIED 응답   → 두 번째 트랜잭션: 3 -> APPLIED(2), granted = amount, applied_at
   NO_EFFECT 응답 → 두 번째 트랜잭션: 3 -> DECIDED(1)  (부작용 0 이 확인된 경우만 재시도 가능)
   UNKNOWN / 그 사이 중단 → 3 유지 (자동 재지급 금지)
UNCERTAIN(3) --운영자 대사(티켓 필수)--> MANUAL_APPLIED(4) 또는 MANUAL_VOID(5)
```

- 지급기를 부르기 **전에** 행을 3으로 커밋한다. 그래서 지급기와 최종 기록 사이에 무엇이 끊겨도 행은 3에 남고, 3은 다시 지급기로 넘어가지 않는다. 이렇게 해서 실행 전에 중복을 차단한다.
- 모든 상태 변경은 `need_marble_cp_audit`에 한 줄씩 남는다. 사유 코드: 1 claim, 2 지급 완료, 3 무효과, 4 응답 불명, 5 운영자.
- 운영 빌드의 안전장치:
  - 지급기를 등록하는 코드가 저장소 소스에 없다(정적 검사).
  - 그래서 `need_marble_cp_apply`는 항상 DISABLED다.
  - 정책 표에서 키 5를 1로 바꾸면, 운영 빌드에서는 검증 실패로 **마블 모듈 전체가 꺼진다**(기존과 같은 fail-closed). 설정만 바꿔서 지급이 시작되는 경로는 없다.
  - 감사 테이블이 없는 DB에서도 마블은 그대로 동작한다. 감사 테이블은 원장 테이블 목록에 넣지 않았기 때문이다. 다만 apply는 DISABLED가 된다.
- 모의 지급기의 동작:
  - `NEED_MARBLE_TEST_HOOKS=1` 환경의 시험 빌드에서만 등록된다.
  - `ragnarok_mbqa`가 아닌 DB에서는 거부한다.
  - `mbqa_cp_mock_balance` / `mbqa_cp_mock_op`만 쓴다.
  - 실제 `#CASHPOINTS`처럼 멱등 키가 없다. 그래서 두 번 호출되면 증거에 두 줄로 드러난다.
  - 고장 주입은 1회성이다: 1 변경 전 거부, 2 변경 후 응답 유실, 3 변경 후 map-server 즉시 종료(`std::_Exit`), 4 변경 없이 응답 유실.

## 2. BG-A1~A5 결과 (작업 A)

**최종 판정: BG FIX PASS** (2차 실측). G2-C 회귀 6단계도 모두 PASS했다: M1 15/15, M2 5/5, M3 7/7, CLK 5/5, CANCEL 4/4, OFF 6/6.

| 케이스 | 결과 | 핵심 근거 |
|---|---|---|
| BG-A1 AFK가 루프 맨 앞, 뒤에 정상 참가자 | **PASS** (A1 18/18) | 티에라 자동 지급이 응답 없이 **0.1초**에 끝났다(5명 지급). AFK 뒤 4명이 **첫 자동 지급에서** 기존 보상을 받았고, 자동 지급 클라이언트에 간 NPC 대화 패킷은 0개였다. 수정 전 G2-C M1은 90초 뒤에도 응답이 없었다 |
| BG-A2 승패, NPC 수령 vs 자동 지급 | **PASS** | 수정 전 G2-C NPC 경로 기준값과 같다(아래 표). NPC 대화창은 4줄 표시 후 닫힘으로 기존과 같다 |
| BG-A3 중복·순서·캐릭터 전환 | **PASS** | 아래 3.1 |
| BG-A4 취소·미복귀 이탈·마블 OFF | **PASS** (A4 6/6, CANCEL 4/4) | 취소 시 자동 지급 0명·원장 0행이다. 이탈 후 미복귀는 기존 보상 0·마블 0이다. 마블 OFF 날에도 자동 지급이 0.1초에 같은 기존 보상을 줬고, 마블 원장 변화는 0이다 |
| BG-A5 티에라·플라비우스·KVM | **PASS** (A2 8/8, A3 8/8) | 플라비우스 0.1초에 3명, KVM 0.1초에 3명 지급(KVM은 무변경 경로). 자동 지급 = NPC 수령 |

### 2.1 실제 기존 보상 수량 (1인당 증가분, 실측)

| 모드 | 승리 | 패배 |
|---|---|---|
| 티에라 (`7828` 휘장) | 7828×2, 6378×1, 399990×30 | 7828×1, 6378×1, 399990×26, BG 포인트 +3 |
| 플라비우스 (`7829`) | 7829×3, 6378×1, 399990×45 | 7829×2, 6378×1, 399990×38, BG 포인트 +3 |
| KVM (`7773`, 기존 무변경) | 7773×3, 6378×2, 399990×45, BG 포인트 +3 | 7773×2, 6378×2, 399990×38, BG 포인트 +3 |

- 티에라 값은 수정 전 G2-C M1 NPC 경로 기준(`g2c_evidence/g2c_m1.json`)과 같다.
- 각 모드에서 AFK·자동 지급자·NPC 수령자의 증가분이 서로 같았다.
- AFK도 기존 보상을 받는다. 기존 BG 정책이 "맵 위 전원 지급"이고, 이번 작업에서 바꾸지 않았다. AFK 제외는 마블 점수에만 적용된다.

### 2.2 중복 처리 (BG-A3)

- **대화창을 연 채 자동 지급이 돈 경우**: 크루아 패배자가 NPC를 눌러 닫기 버튼에서 멈춘 상태였다.
  - 자동 지급은 이 사람을 건너뛰었다(수령 키 때문).
  - 그 시점 증가분은 BG 포인트 +3뿐이었다. 기존 코드가 `close2` 전에 포인트를 주기 때문이다.
  - 닫기를 누른 뒤에는 패배 보상이 정확히 1회 지급되고 대기실로 이동했다.
- **자동 지급을 한 번 더 호출한 경우**: 0명 지급, 기존 보상 변화 0이었다.
- **순서를 바꾼 경우(자동 지급 뒤 NPC)**: 자동 지급을 받은 참가자를 전장 맵 NPC 앞으로 되돌려 클릭시켰다. 지급 0, 대화창 0으로 대기실로 보내졌다.
- **캐릭터 전환(MBQA_BGA2)**: 마블은 `ALREADY(-4)`였고, 마블 원장 변화는 0이었다.
- **마블 점수**: 같은 경기·같은 계정에 +50이 1회 들어갔다. NPC·대화창 보류·자동 경로 모두 같았다. AFK 0, 이탈자 0으로 G2-C 규칙이 유지됐다.

### 2.3 1차 실측 FAIL과 원인 (숨기지 않음)

- **현상**: 1차 실측에서 BG-A1·A2·A3·A4와 G2-C M1의 C-04 일부가 FAIL이었다. 원인은 모두 같았다.
  - 자동 지급 뒤 대기실(bat_room 154,150)로 이동하면 **'전장 이용 동의' OnTouch 안내**(mes 6줄 + next)가 시험 계정에 열렸다.
  - 이 안내가 열린 동안 하네스 명령이 막혔다(rc None).
  - 보상 대화창이 아니다. 같은 실행에서 수량·즉시 완료·중복 항목은 모두 PASS였다.
- **처리**:
  - 실제 랜덤 매치는 `@전장매칭`·대기 신청·참가 3곳 모두 `#NEED_BG_RULE_AGREE`가 없으면 거부한다(`bg_random_match_addon_full_cycle.txt`). 따라서 실제 참가자는 모두 동의한 상태다.
  - 이 전제 조건을 `bc_synth_bga.sql`로 넣었다. G2-C 계정의 마블 행과 시험 정책 행을 지운 뒤 처음부터 다시 실측했다.
  - "대화 패킷 0개" 기준은 완화하지 않았다.
- **영향**: 수정 전 G2-C 실측에서는 루프 정체로 90초가 지나 있었고, 그 사이 이 안내가 타임아웃으로 닫혀 드러나지 않았던 것으로 보인다.
- **증거**: `evidence/bg_auto_reward/run1_without_rule_agree/`.

## 3. CP-1~CP-6 결과 (작업 B, V20-MOCK)

**최종 판정: V20-MOCK PASS.**
- p1 14/14 PASS.
- p2는 10/11이었다. 실패 1건은 V20 이전 전제를 적은 CP3 불변식이었고, 갱신한 뒤 같은 DB에서 원장 불변식 위반 0을 확인했다(4.3절).
- **실제 V20(char-server `#CASHPOINTS` 반영)은 NOT RUN**이다.

결정 데이터는 현재 코드의 **초안 정책** 그대로다. 일반 계정 `mbqa_cpa`가 실제 보드에서 21롤을 굴려 CP 결정 16건을 만들었다.
- 칸 CP 500~3000, 33걸음 보너스 10000이다.
- 칸 CP는 그 롤 도착 이벤트 CP와 같았고, 33걸음은 10000이었다.
- 이 금액들은 **시험값이며 승인된 운영 지급액이 아니다**.

| 케이스 | 결과 | 원장 키·상태·모의 잔액 |
|---|---|---|
| CP-1 같은 결정 2회, 같은 요청 이중 전달, 다른 캐릭터 | **PASS** | payout 16 재호출 → `ALREADY(-4)`. payout 1을 연달아 2번 보냄 → 응답 [0, -4], 변경 1줄. MBQA_CPA2로 두 건 재호출 → 둘 다 -4. 잔액은 13000→13500(payout 1의 500만 증가). 결정 16건의 키·금액 불변, 재추첨 0 |
| CP-2 같은 롤의 칸 CP + 33걸음 보너스 | **PASS** | 롤 21: payout 16(src 1, 3000), payout 15(src 2, 10000)가 각각 APPLIED 1회. 잔액 = 13000 = 합계 |
| CP-3 변경 전 거부 | **PASS** | payout 2 → `REJECTED(-20)`, 3→1로 복귀, 변경 0, 잔액 불변. 재시도 → APPLIED 1회 |
| CP-4 잔액 변경 후 응답 유실 | **PASS** | payout 3 → `UNCERTAIN(-22)`, 행 3, 변경 1줄. 재요청 → -22, 지급기 재호출 0, 잔액 불변. 변경 없이 응답만 유실된 payout 4도 3이 된다(원장으로는 구별할 수 없다) |
| CP-5 재접속·서버 재시작·재전달 | **PASS** | payout 6: 모의 잔액 변경 직후 map-server가 실제로 종료됐다. 재기동 후 행 3·변경 1줄·감사는 claim(1→3)만 남았다. 재접속 후 재요청: 3 → -22, APPLIED → -4, 변경 0. 운영자 대사: 변경이 확인된 6·3 → MANUAL_APPLIED(4, 티켓 7001·7004), 변경이 없는 4 → MANUAL_VOID(5, 7002). 잘못된 입력(티켓 0, 1로 되돌리기, APPLIED 행)은 거부 |
| CP-6 스위치 OFF·다른 계정·실제 캐시 | **PASS** | 키 5 기본 0 → `DISABLED(-1)`, 행 1 유지, 잔액 0. 다시 0으로 바꾼 날도 -1. `mbqa_cpb`가 cpa의 payout_no 호출 → `NOT_FOUND(-9)`, 양쪽 변화 0. 실제 `#CASHPOINTS` 행 0, `cashlog` 0 |

- 최종 상태: DECIDED 9, APPLIED 4, MANUAL_APPLIED 2, MANUAL_VOID 1, UNCERTAIN 0이다.
- 모의 잔액 17000 = 변경 6줄의 합 = APPLIED·MANUAL_APPLIED 금액의 합이다.
- 감사 기록은 18줄이고, 허용된 전이만 있다.
- CP 불변식(`v20_inv`) 위반은 p1·p2 모두 0이었다. 검사 내용: 결정당 변경 1회 이하, 적용 상태는 정확히 1회, DECIDED·VOID는 0회, 잔액 = 변경 합, 감사 체인 끝 = 현재 상태.

## 4. 단위·정적·빌드·회귀·불변식

### 4.1 시험 결과

| 항목 | 결과 |
|---|---|
| 단위 시험 `logic_test.cpp` | **169/0** (V20 13개 추가). 작업 A 커밋 상태는 기존 156/0 |
| 정적 검사 `static_check.py` | **122/0**. 작업 A 커밋 상태를 재현한 트리에서는 113/0 |
| 운영용 빌드 (worktree, 시험 훅 없음) | 경고 0. exe `40D23112…23BE`, 시험 문자열 0건 |
| 시험 빌드 | A: `CD8A84A5…EC0D`, V20: `03674C18…6049`. 둘 다 경고 0 |
| 핵심 회귀 | G2-C 6단계 PASS. V11 성격의 결정 검사 PASS(칸 CP = 이벤트 CP, 33걸음 10000). G2-B V21 빠른 회귀 `v21a` 9/9. G2-B 전체는 반복하지 않았다 |
| 마블 원장 불변식(sql-files) | BG-A 각 단계 0건, V20 최종 0건(CP3 갱신 후) |
| 실제 CP 지급·기능 플래그 | `#CASHPOINTS` 0, `cashlog` 0. 운영 기본값 키 5 = 0 유지(시험 DB에만 정책 행) |

### 4.2 시험 빌드에서만 등록되는 모의 지급기

- 시험 빌드는 저장소가 아닌 스크래치 복사본에서만 만들었다.
- 운영용 exe에 `need_marble_test|mbqa|NEED_MARBLE_TEST` 문자열은 0건이다.

### 4.3 CP3 불변식 갱신 (시험 기준 완화가 아님)

- 기존 CP3는 "이 빌드에서는 모든 CP 행이 DECIDED"를 적은 규칙이었다(`status <> 1`이면 위반). 지급 단계가 생겨 정의가 바뀌었으므로 상태별 정합성 규칙으로 교체했다.
- 새 CP3의 위반 조건:
  - 상태가 1~5가 아님
  - 2·4인데 granted ≠ amount이거나 applied_at이 없음
  - 1·3·5인데 granted ≠ 0이거나 applied_at이 있음
  - 4·5인데 티켓이 없음
- 지급기가 없는 운영 상태에서는 모든 행이 1이라 기존과 같은 결과를 낸다.
- 검증: 임시 테이블로 원본을 가린 음성 시험을 했다. 정상 5행은 통과, 위반 5행은 5건 모두 검출됐다(`evidence/v20_mock/cp3_negative.txt`).

## 5. 환경·정리

- **원본**: `E:\tools\Need\rathena` HEAD `929a83834`, status 7줄, 3306 PID 8908로 변화 없다. 원본 브랜치에 쓰기·merge·rebase·cherry-pick은 없었다.
- **다른 세션 환경**: mhtest(3309 PID 17360, 프로세스 3개)와 dqtest는 접속·재시작·종료하지 않았다.
- **격리 스택(이 작업 소유)**: MariaDB 127.0.0.1:3327 `ragnarok_mbqa`(PID 44976), login 26901, char 26121, map 25121, wiki 26906이다.
  - 모두 127.0.0.1 바인드다.
  - 종료: `stopall`, DB `SHUTDOWN`. 5개 포트 모두 비어 있음을 확인했다.
- **스크래치 삭제**: 스택, 시험 소스·빌드 2쌍, A 상태 재현 트리, 임시 증거 폴더(약 2.2 GB).
- **커밋 제외**: `tests/need_marble/g2/__pycache__/`는 커밋하지 않았다.

## 6. 남은 결정 사항

### 6.1 실지급 승인 판단 자료

`pc.cpp`·`script.cpp`·`intif.cpp`·`char/inter.cpp`·`log.cpp`를 읽기 전용으로 분석했다.

| 항목 | 사실 (확인 가능) | 한계·위험 |
|---|---|---|
| **실제 writer 후보와 UNCERTAIN이 되는 순서** | ① map `pc_getcash(sd, cash)`: 메모리의 `#CASHPOINTS`와 `sd->cashPoints`를 즉시 바꾸고 **`cashlog`에 즉시 INSERT**(log DB, autocommit)한다. DB 저장은 **스크립트 종료 시 또는 캐릭터 저장 시** `intif_saveregistry` → 0x3004 → char-server `inter_savereg`가 `acc_reg_num`에 `REPLACE`(키별 autocommit)한다. ② char-server 쪽 writer: 새 inter 패킷과 응답이 필요하다(현재 없음). ③ map에서 `acc_reg_num` 직접 UPDATE | ①은 **0x3004에 응답이 없다**. map이 지급 후 저장 전에 죽거나, char가 내려가 있거나, 두 번째 트랜잭션 커밋이 실패하면 → UNCERTAIN이다. ③은 온라인 세션의 다음 레지스트리 저장이 **절대값 REPLACE**로 덮어 지급이 사라질 수 있다(사용 금지). 마블 원장(map `mmysql_handle`), `cashlog`(log 핸들), `acc_reg_num`(char-server)은 **세 개의 독립 커밋**이라 한 트랜잭션으로 묶을 수 없다 |
| **대사 근거의 신뢰 한계** | `cashlog`: 호출마다 1행(time, char_id, type, cash_type, amount, map). `acc_reg_num`: 계정의 현재 `#CASHPOINTS` 절대값 1행. `#NMB_CPNO` 후보 표지(마지막 적용 payout_no를 계정 변수로 저장): 같은 0x3004 패킷으로 저장된다 | `cashlog`는 **영속 저장 전에** 쓰이고 payout 키가 없다. 행이 있어도 DB 반영을 뜻하지 않는다. `acc_reg_num`은 최종값뿐이고 다른 캐시 출처(상점·이벤트)와 섞인다. 표지 변수도 `REPLACE`가 키별 autocommit이라 캐시와 원자적이지 않다. **표지 하나로 지급 확정을 단정할 수 없다** |
| **장애 시 확인 방법·재지급 금지 기준** | 원장 DB 장애: 첫 트랜잭션 실패면 행 1(지급 0회), 이후면 3. map 종료: 3(CP-5 실측). char 종료: `intif_saveregistry`는 char 연결이 없으면 보내기 전에 반환한다. 그래서 다음 저장 기회에 다시 보낼 수 있는 것으로 보이지만(미실측), 그 전에 map도 죽으면 유실된다. 모든 3은 `need_marble_cp_audit`의 claim 시각을 기준으로 `cashlog`(char_id·금액·시간대), `acc_reg_num` 값 대비 기대 합계, map/char 로그를 대조한다 | **3은 어떤 경우에도 자동 재지급하지 않는다**(코드로 강제, CP-4·CP-5 실측). 운영자가 티켓으로 4 또는 5를 결정한다. 5로 닫은 뒤 다시 지급해야 하는 경우의 절차는 미정(6.2) |
| **별도 승인 항목** | (가) 실제 writer 방식 선택(①+저장 강제 또는 ②)과 구현 (나) 격리 DB에서 실제 `#CASHPOINTS`·char-server 종료 주입 실측 (다) 운영 DB에 `need_marble_v20_cp_apply.sql` 적용 (라) writer를 등록하는 운영 빌드 배포 (마) **운영 정책 키 5 ON** | 지금까지는 모의 지급기만 검증했다. mock PASS는 실제 원장과 실제 계정 캐시의 정합성을 증명하지 않는다 |

### 6.2 미결 3가지

1. **운영자가 결정할 것**
   - 기존 미결 정책은 이번에 확정하지 않았다: 수동 BG 포함 여부, 워프 목적지 N10, 캡차 유발 처치 인정.
   - 새로 생긴 것: MANUAL_VOID(5)로 닫은 건을 다시 지급해야 할 때의 절차. 현재 `resolve`는 1(재시도)로 되돌리기를 허용하지 않는다.
   - CP 금액(칸 500~3000, 33걸음 10000)은 초안 그대로다.
2. **실제 CP writer 시험에 필요한 승인**: 6.1의 (가)·(나).
   - writer 방식 선택이 필요하다. ①은 `pc_getcash` 직후 레지스트리 저장 강제와 저장 확인 방법이 없고, ②는 char-server 응답 패킷 신설이 필요하다.
   - char/map 종료 주입을 포함한 격리 실측이 필요하다.
3. **실지급 ON 전에 반드시 확인할 것**
   - 운영 DB에 감사 테이블 적용, writer가 든 빌드, 키 5 ON 순서를 지킨다.
   - UNCERTAIN 건을 운영자가 확인할 화면 또는 명령이 필요하다. 지금 `need_marble_cp_resolve`는 시험 명령으로만 노출돼 있다.
   - `log_cash: yes` 유지와 `cashlog` 보존 기간을 확인한다.
   - 기존 BG의 수동 NPC 경로에서 `close2` 전에 BG 포인트가 먼저 들어가는 순서(2.2)는 이번 범위 밖이라 그대로 뒀다.

# NEED 라그 마블 — F1·F2 제품 코드 개선안 v1.0

- 작성일: 2026-10-21
- 상태: 2026-10-21 승인·구현 완료(커밋 `f6e620f3a`, 결과 `NEED_MARBLE_F1_F2_EXECUTION_REPORT_v1_0.md`). 아래는 승인 당시 개선안 원문.
- 원래 상태: **개선안만. 코드는 바꾸지 않았다.** 대상 브랜치 `feature/need-marble-phase-bc-v1` (현재 `57cfda18b`)
- 출처: `NEED_MARBLE_G1B_EXECUTION_REPORT_v1_0.md` 4절 F1·F2

## F1 — 레벨 판정 시점 (그 처치로 레벨이 오르면 그 처치가 빠짐)

### 현재 동작 (소스)

`mob_dead()` 안의 순서:

| 줄 (`src/map/mob.cpp`) | 내용 |
|---|---|
| 3369~3373 | 데미지 기록에서 최다 피해자 `need_summer_highest_damage_sd` 계산 |
| 3404~3554 | 경험치 분배 — `pc_gainexp`(3545), 파티 분배 `party_exp_share`(3554). **여기서 레벨이 오른다** |
| 3899 | `rebirth = (SC_KAIZEL ‖ SC_ULTIMATE_S ‖ (SC_REBIRTH && !md->state.rebirth))` |
| 3900~3986 | 최종 처치일 때 월드 드롭, 매크로 사냥 탐지(`pc_need_macro_hunt_on_mob_kill`, 3985) |
| 3987~3994 | 여름·추석·**마블** 사냥 훅 (같은 조건, 이 순서) |

마블 훅의 레벨 조건(`need_event_hunt_normal_field_target`, ±정책값)은 **경험치가 들어간 뒤의 레벨**로 판정된다. 최다 피해자와 파티원 모두 같다(파티원은 `party_exp_share` 로 오름).

### 선택지

| 안 | 변경 | 판정 레벨 | 영향 범위 | 추천 |
|---|---|---|---|---|
| **B 마블 훅만 경험치 분배 앞으로** | `mob.cpp` 3373 직후(경험치 블록 3404 앞)에 마블 훅 호출을 옮긴다. 최종 처치 조건은 3899 와 **같은 식**으로 그 자리에서 계산(몹 상태 이상만 보므로 같은 값). 3993~3994 의 기존 호출은 삭제 | 처치 **직전** (최다 피해자·파티원 모두) | 마블만. 여름·추석 훅은 그대로 | ✔ |
| C 최다 피해자 레벨만 미리 저장 | 3373 직후 `base_level` 을 지역 변수에 담아 훅에 넘김 | 최다 피해자만 직전, 파티원은 직후 | 마블 | 파티원이 여전히 F1 을 겪음 |
| 그대로 | 변경 없음 | 직후 | 없음 | 기존 이벤트 사냥과 같은 규칙으로 맞출 때 |

B안의 의사 코드(실행 금지):

```cpp
// mob.cpp, 3373 직후 (경험치 분배 앞)
const bool need_marble_final_kill = !( md->sc.getSCE(SC_KAIZEL) || md->sc.getSCE(SC_ULTIMATE_S) ||
                                       (md->sc.getSCE(SC_REBIRTH) && !md->state.rebirth) );   // 3899 와 같은 식
if (need_marble_final_kill && need_summer_highest_damage_sd != nullptr && src != nullptr && !md->state.npc_killmonster)
    need_marble_hunt_on_kill(need_summer_highest_damage_sd, md, type);   // 레벨·파티 판정을 경험치 분배 전에
// ... 3993~3994 의 마블 호출은 삭제
```

### B안의 부수 효과 (검토 결과)

| 항목 | 지금 | B안 후 | 판단 |
|---|---|---|---|
| 레벨 경계 처치 | 빠짐 | 들어감 | 의도한 변경 |
| 매크로 사냥 탐지와의 순서 | 탐지(3985)가 먼저 → **이 처치로 캡차가 뜨면 이 처치도 제외** | 마블이 먼저 → 이 처치는 인정, 다음 처치부터 제외 | 캡차는 이 처치 *뒤에* 시작되므로 더 정확. V18 캡차 항목 시험 때 확인 |
| `rebirth` 판정 | 3899 | 같은 식을 3373 직후에도 계산 | 경험치·드롭 처리가 몹 상태 이상을 바꾸지 않아 값이 같다(정적 검사로 두 식이 같은지 고정) |
| 체크포인트 DB 트랜잭션 시점 | 드롭·경험치 뒤 | 경험치 앞 | 원장 쪽 실패는 처치·경험치에 영향을 주지 않는 구조 그대로(훅은 반환값 없음) |
| 여름·추석 | 직후 레벨 | 변경 없음 | 이벤트 간 규칙이 달라진다 → 운영자 확인 필요 |

## F2 — 상한 뒤 재접속하면 배치 행이 다시 쌓임

### 현재 동작 (소스, `src/map/need_marble.cpp`)

| 줄 | 내용 |
|---|---|
| 106~116 | `hunt_buf`(로그인 세션 메모리). `done_day` = 오늘 구간을 다 채웠다는 표시 |
| 482~537 | `hunt_checkpoint`: 배치 행 INSERT(497) → 누계 `SELECT … FOR UPDATE`(505) → 구간 처리 → 누계 UPDATE. `next_tier > max_tiers` 이면 `maxed_day` 반환(530) |
| 577 | 체크포인트 결과로 `done_day` 설정 |
| 749~790 | `need_marble_hunt_on_kill`: `done_day == day` 면 건너뜀(781) |

`done_day` 는 체크포인트가 한 번 돌아야 세팅되고 로그아웃 때 버퍼와 함께 사라진다. 그래서 상한 뒤 재접속하면 20마리마다 배치 행과 누계 UPDATE 트랜잭션이 다시 돈다. 구간·점수는 늘지 않는다(G1-b 실측: 구간 4·100점 유지, 배치 20→23행).

### 개선 (두 겹)

| # | 위치 | 변경 |
|---|---|---|
| 1 | `hunt_buf` | `uint32 checked_day = 0;` 추가 |
| 1 | `need_marble_hunt_on_kill` 781 앞 | 계정별 버퍼에서 `checked_day != day` 이면 한 번만 `SELECT next_tier FROM need_marble_hunt_progress WHERE account·season·period_day` (트랜잭션 밖) → `next_tier > max_tiers` 면 `done_day = day`. 그 뒤 `checked_day = day`. **조회 실패 시에는 표시하지 않음**(지금처럼 계속 세고, 체크포인트가 판단) |
| 2 | `hunt_checkpoint` 497~505 | 순서를 바꿔 **누계 `SELECT … FOR UPDATE` 를 먼저** 하고, 이미 `next_tier > max_tiers` 인 기간이면 그 기간은 배치 INSERT·누계 UPDATE 를 하지 않고 `maxed_day` 만 돌려준다(1번이 놓친 경우의 방어). 상한 전이면 지금과 같이 배치 INSERT(중복이면 ALREADY) → 구간 처리 |
| 3 | `need_marble_logic.hpp` | 순수 함수 `hunt_day_maxed(next_tier, max_tiers)` 를 두고 1·2 가 같이 사용, 단위 시험 추가 |

### 영향

| 항목 | 변경 후 |
|---|---|
| 정합성 | 구간·점수·원장 변화 없음. I11(누계 = 배치 합)은 둘 다 건너뛰므로 유지 |
| 통계 | 상한 뒤 처치 수는 누계에 남지 않는다(누계는 상한 도달 시점 값에서 멈춤). 설계 v0.5 7.6 "상한 이후 체크포인트 생략"과 일치 |
| DB 부하 | 상한 뒤: 로그인·논리 일자당 계정별 SELECT 1회. 파티원도 각자 1회 |
| 날짜 경계 | `checked_day`·`done_day` 가 날짜 단위라 04:00 KST 뒤 첫 처치에서 새 날짜를 다시 조회 |
| 정책 변경 | `max_tiers` 를 다음 날부터 올리면 그날 첫 조회에서 새 값으로 판단(같은 날 변경은 하루 한 번 정책 재조회 구조라 적용되지 않음 — 지금과 같음) |

## 시험 방법

| 단계 | 내용 | 기대 |
|---|---|---|
| U1 단위 | `logic_test.cpp` 에 `hunt_day_maxed` 경계(4/4 → 아님, 5/4 → 상한) | PASS |
| S1 정적 | `static_check.py` 에 ① 마블 훅이 `mob.cpp` 경험치 블록(`if(!(type&2)`)보다 앞 ② 마블 최종 처치 식이 3899 식과 같음 ③ 여름·추석 훅 줄 불변 ④ `hunt_checkpoint` 에서 누계 SELECT 가 배치 INSERT 보다 앞 | PASS |
| B1 빌드 | worktree 빌드(운영용) + G-1(b) 시험 빌드 | 경고 0 |
| R1 F1 재현→해결 (G-1(b) 격리) | 캐릭터 41레벨·다음 레벨 직전 경험치(합성 데이터)로 11레벨 몹(차이 30)을 1마리 처치 → 레벨 42(차이 31) | 변경 전 빌드: 반영 0(진단 로그 "not counted") / 변경 후: 반영 1 |
| R2 F1 파티원 | 파티 분배 균등(`party.exp=1`), 파티원도 다음 레벨 직전 → GM 처치로 파티원 레벨 상승 | 변경 후 파티원도 반영 1 |
| R3 F2 | 상한 채운 계정 로그아웃 → 재로그인 → `@mbqakill` 실제 처치 10 + `@mbqafeed 5000` | 누계·배치 행·트랜잭션 변화 0, 맵 로그 오류 0 |
| R4 회귀 | V14 · V03 · V18 · V22 · 교환·이동 일부, 불변식 SQL | 지난 결과와 같음 |
| 미포함 | 날짜 경계(V24 류), 캡차 순서 변화(V18 캡차 항목) | 시각 오프셋·캡차 시험 수단 결정 후 |

## 승인이 필요한 것

| ID | 내용 |
|---|---|
| P1 | 제품 코드 변경: `src/map/mob.cpp`(마블 훅 이동, 약 6줄), `src/map/need_marble.cpp`(약 30줄), `src/map/need_marble_logic.hpp`(함수 1개) — 이 브랜치 로컬 커밋 |
| P2 | F1 규칙 결정: 마블만 "처치 직전 레벨"(B안)로 할지, 기존 이벤트 사냥과 같게 둘지 |
| P3 | worktree 빌드와 G-1(b) 시험 빌드, 격리 스택 실행(지난번과 같은 포트·경로, 합성 데이터 조정 포함), 종료 후 정리 |

원본 DB·원본 서버·원본 작업 트리, CP 실지급, 푸시·병합은 포함하지 않는다.

# 검수 케이스 — 주간 랭킹 / 랭킹 보상 / 로그 추적

need-server 기준. 폐기 가능한 테스트 계정 A/B/C로 진행. 각 케이스: **절차 → 기대결과 → 검증방법**.
운영 데이터 오염 방지를 위해 테스트 계정/테스트 주차 override를 사용한다.

## 0. 사전 준비 / 테스트 도구
- SQL 적용: `need_summer_fishing.sql`(weekly_best/weekly_result/rank_reward_outbox), `need_summer_reward_log.sql`.
- config: `need_summer_fishing_enable: on`. 랭킹 보상 검수 시 `need_summer_fishing_rank_reward_enable: on` (**battle config → 재시작/@reloadbattleconf 필요**).
- 상태 확인: `@summergm → [한여름 낚시 관리] / [운영/보상 상태]` (런타임 값·outbox 카운트).
- 낚시 테스트 NPC(`need_fishing_test`, GM60): 
  - 주차 override `@nff_rank_test_week`(1~4, $NSF_DEBUG+GM60) → **실제 주가 안 지나도 특정 주차로 강제**.
  - 스냅샷/예약/TOP10/스윕 수동 실행, 어획 시뮬레이션.
  - 보상 outbox 현황/목록/상세/REVIEW→RETRY 재큐.
- 상수(참고): 지정 어종 주차별 fish_id = **1주 4 / 2주 5 / 3주 8 / 4주 10**. 보상 = 1위 50 / 2위 30 / 3위 20 / 4~10위 10 (코코넛 토큰 **399925**). 이벤트 202608.

---

## A. 주간 낚시 랭킹

### A-1. 지정 어종 어획 시 weekly_best 등록
- 절차: `@nff_rank_test_week=1`(1주차, 지정 fish 4) 설정 → 계정 A로 지정 어종(4번) 낚시 성공.
- 기대: `need_summer_fishing_weekly_best`에 (event=202608, week=1, fish_id=4, account=A) 1행 생성.
- 검증: `SELECT * FROM need_summer_fishing_weekly_best WHERE event_id=202608 AND week_no=1 AND account_id={A};`

### A-2. 최고기록 갱신 규칙 (길이 우선, 무게 보조)
- 절차: A가 같은 지정 어종을 여러 번 낚아 (짧은→긴→같은길이 더무거운) 순서로 잡음.
- 기대: `length_mm`가 더 클 때만 갱신, 길이 같으면 `weight_g` 더 클 때만 갱신. 더 작은 기록은 무시.
- 검증: 매 어획 후 위 SELECT의 length_mm/weight_g가 규칙대로만 증가하는지.
- 근거: `F_NeedFishWeeklyBestUpsert`(core:588) `len > ol || (len==ol && wt>ow)`.

### A-3. 랭킹 정렬 / 타이브레이크
- 절차: 계정 A/B/C가 같은 주차 지정 어종을 서로 다른 길이로 잡음(동점 케이스 포함).
- 기대: TOP10이 **length_mm DESC → weight_g DESC → caught_at ASC → account_id ASC** 순.
- 검증: 테스트 NPC "TOP10 조회" 또는
  `SELECT account_id,length_mm,weight_g,caught_at FROM need_summer_fishing_weekly_best WHERE event_id=202608 AND week_no=1 AND fish_id=4 ORDER BY length_mm DESC,weight_g DESC,caught_at ASC,account_id ASC LIMIT 10;`

### A-4. 내 순위 / 남은 시간 표시
- 절차: 랭킹 안내판(`#nsf_board → 주간 낚시 랭킹`) 열기.
- 기대: 현재 주차, 지정 어종, TOP10, **내 순위(미등록 시 "기록 없음")**, 주차 종료까지 남은 시간 표시.
- 검증: UI 육안 + 내 계정 weekly_best 값과 일치.

### A-5. 비지정 어종은 랭킹 미반영
- 절차: 1주차(지정 4)에 다른 어종(예 7번)을 낚음.
- 기대: weekly_best에 **행 생성 안 됨**(지정 어종만 업서트).
- 검증: `SELECT COUNT(*) ... WHERE week_no=1 AND fish_id<>4 AND account_id={A};` → 0.
- 근거: core:123 `if (.@rweek>=1 && .@fish == 지정어종) upsert`.

### A-6. 주차 경계(04:00 논리일자)
- 절차: `@nff_rank_test_week` 해제 후 실제 시간 기준 확인(또는 override로 각 주차).
- 기대: 논리일자 = `now-4h`. 20260815~0821=1주, 0822~0828=2주, 0829~0904=3주, 0905~0911=4주, 0912+=기간 외(0).
- 검증: 서버 시각을 04:00 전후로 두고 `@summergm`/안내판의 "현재 주차"가 정확히 바뀌는지. (운영 시간 임의 변경 금지 — override로 대체)

### A-7. 계정 기준 집계 (한 계정 여러 캐릭)
- 절차: 계정 A의 캐릭1로 기록 후 캐릭2로 더 큰 기록.
- 기대: weekly_best는 **account_id 단위 1행**, 캐릭2 기록으로 갱신(char_id/char_name 교체).
- 검증: `SELECT char_id,char_name,length_mm ... WHERE account_id={A}` → 1행, 최신 캐릭.

### A-8. 스냅샷 생성 (weekly_result)
- 절차: 1주차 기록들 만든 뒤 테스트 NPC "스냅샷(1주차)" 실행(또는 스윕).
- 기대: `need_summer_fishing_weekly_result`에 rank_no 1..N(≤10) 확정 순위 기록.
- 검증: `SELECT rank_no,account_id,char_name,length_mm,weight_g FROM need_summer_fishing_weekly_result WHERE event_id=202608 AND week_no=1 ORDER BY rank_no;`

### A-9. ★스냅샷/스윕 멱등성 (중복 실행·리로드·재시작)
- 절차: 스냅샷을 **2회 이상 반복** 실행 → `@reloadscript` → map-server **재시작** 후 다시 스윕.
- 기대: weekly_result **행 수·내용 불변**(중복 없음). 반복 실행해도 rank가 뒤섞이거나 중복되지 않음.
- 검증: 각 단계 후 `SELECT COUNT(*) FROM need_summer_fishing_weekly_result WHERE week_no=1;` 동일. 
- 근거: PK(event,week,rank) + ON DUPLICATE KEY UPDATE + have>=exp 가드. **이 케이스가 랭킹 검수의 핵심.**

---

## B. 랭킹 보상 (outbox → 시스템 우편)

### B-1. 예약(reserve): weekly_result → outbox
- 절차: A-8 스냅샷 후 테스트 NPC "보상 예약(1주차)" 실행(또는 스윕).
- 기대: `need_summer_fishing_rank_reward_outbox`에 확정 순위별 행 생성, status=0(PENDING), reward_item_id=399925, reward_amount=순위별(50/30/20/10).
- 검증: `SELECT week_no,rank_no,account_id,reward_item_id,reward_amount,status FROM need_summer_fishing_rank_reward_outbox WHERE event_id=202608 AND week_no=1 ORDER BY rank_no;`

### B-2. ★예약 멱등성 (중복 예약 방지)
- 절차: 예약을 **2회 이상 반복** + 스윕 여러 번 + `@reloadscript`/재시작.
- 기대: outbox 행 **중복 생성 안 됨**(주차별 rank/account 각 1행).
- 검증: `SELECT week_no,rank_no,COUNT(*) FROM ...outbox WHERE event_id=202608 GROUP BY week_no,rank_no HAVING COUNT(*)>1;` → **0행**.
- 근거: `UNIQUE(event,week,rank)` + `UNIQUE(event,week,account)` + INSERT IGNORE.

### B-3. consumer OFF 시 미발송
- 절차: `need_summer_fishing_rank_reward_enable: off` 상태에서 outbox PENDING 존재.
- 기대: 우편 **미발송**, status PENDING **유지**(소멸/오류 없음).
- 검증: 5분 대기 후 `SELECT status,COUNT(*) ... GROUP BY status;` 여전히 PENDING. `@summergm → 운영/보상 상태`도 동일.

### B-4. ★consumer ON 시 발송 (PENDING→DELIVERED)
- 절차: `rank_reward_enable: on` + 재시작. 5초 타이머가 배치(20건) 처리.
- 기대: PENDING → DELIVERED, 수취 계정에 **시스템 우편 도착**(코코넛 토큰 첨부).
- 검증: `SELECT status,COUNT(*) ... GROUP BY status;`(DELIVERED 증가), 수취 캐릭 우편함 확인, `SELECT * FROM mail WHERE dest_id={수취char};` + `mail_attachments`(nameid=399925, amount=순위값).

### B-5. 보상 수량 정확성
- 기대: 1위 50 / 2위 30 / 3위 20 / 4~10위 10 (399925).
- 검증: `SELECT rank_no,reward_amount FROM ...outbox WHERE week_no=1 ORDER BY rank_no;` 와 첨부 수량 일치.

### B-6. 우편 발신자/제목/본문
- 기대: 시스템 우편 형식(발신자/제목/본문 msg 1985/1986/1987), zeny 0.
- 검증: 우편 UI 육안 + `SELECT send_name,title,message,zeny,type FROM mail WHERE id={mail_id};`

### B-7. 재시도 (RETRY / 백오프 / 최대 5회)
- 절차: 발송 실패 상황 유도(예: 대상 캐릭 임시 삭제 등으로 실패 코드 발생 — 테스트 계정만).
- 기대: status=RETRY(3), attempts 증가, `next_attempt_at` 약 60초 뒤. 5회 초과 시 REVIEW(4)로 이동.
- 검증: `SELECT status,attempts,next_attempt_at,last_error_code FROM ...outbox WHERE outbox_id={id};` 시간에 따라 변화.

### B-8. ★REVIEW 처리 + 재큐
- 절차: 수취인 없음/이름 없음(INVALID_CHARACTER) 유도 → status=REVIEW. 이후 대상 복구 후 테스트 NPC "REVIEW→RETRY 재큐".
- 기대: REVIEW 행이 재큐되어 재발송, **이미 우편 발송된 행은 재큐 안 됨**(mail_id IS NULL 가드).
- 검증: 재큐 SQL은 `status=4 AND mail_id IS NULL`만 대상. 재큐 후 DELIVERED 되는지, 중복 우편 없는지.

### B-9. ★크래시 안전성 (PROCESSING 중단)
- 절차: (가능 시) 발송 도중 map-server 강제 종료 → 재기동.
- 기대: 중단된 행은 **롤백되어 PENDING 복귀**, 중복 우편/보상 손실 **없음**. 재기동 후 정상 발송.
- 검증: 재기동 후 해당 수취인 우편이 **정확히 1통**인지, outbox status 정상 진행. 
- 근거: PROCESSING은 단독 커밋 안 됨, mail+attachment+DELIVERED 단일 트랜잭션 + SELECT FOR UPDATE.

### B-10. 낚시 OFF + 보상 ON 독립 동작
- 절차: `need_summer_fishing_enable: off` + `rank_reward_enable: on`.
- 기대: 낚시 플레이는 막혀도 **랭킹 보상 우편은 정상 발송**(이벤트 종료 직후 시나리오). 오류로 취급 안 함.
- 검증: outbox PENDING이 DELIVERED로 진행. `@summergm`에서 "낚시 OFF / 랭킹보상 ON" 정상 표시.

### B-11. outbox ↔ weekly_result 정합성
- 기대: 확정 순위(weekly_result)와 outbox 대상이 1:1(누락/초과 없음).
- 검증: `SELECT r.week_no,r.rank_no FROM need_summer_fishing_weekly_result r LEFT JOIN need_summer_fishing_rank_reward_outbox o ON o.event_id=r.event_id AND o.week_no=r.week_no AND o.rank_no=r.rank_no WHERE o.outbox_id IS NULL;` → **0행**.

---

## C. 로그 추적 (Origin Log + 자산 이동 + Monitor 웹)

### C-1. Origin Log 생성 (4종)
- 절차: 계정 A로 399929(제니주머니)/399930(캐시권)/399927(황금수박)/399931(쉐도우상자) 각각 사용.
- 기대: `need_summer_reward_log`에 각 1행. reward_type: 399929=1(ZENY), 399930=2(CASH), 399927=실제 당첨(1 or 2), 399931=3(ITEM, reward_item_id+unique_id).
- 검증: `SELECT source_item_id,reward_type,reward_item_id,reward_amount,unique_id,ip FROM need_summer_reward_log WHERE char_id={A_char} ORDER BY id DESC;`

### C-2. Shadow unique_id 추적 (A→B 거래)
- 절차: A가 399931로 쉐도우 장비 획득 → B에게 거래(또는 드롭/습득).
- 기대: reward_log.unique_id = picklog.unique_id 동일, A(-1)/B(+1) type T(또는 P) 기록.
- 검증: `SELECT p.time,p.type,p.char_id,c.account_id,p.amount FROM picklog p JOIN \`char\` c ON c.char_id=p.char_id WHERE p.unique_id={uid} ORDER BY p.time;`

### C-3. Zeny 이동 추적
- 절차: A가 399929로 +1억 → B에게 7천만 거래.
- 기대: zenylog A(-70M, src=B)/B(+70M, src=A) type T.
- 검증: `SELECT time,type,char_id,src_id,amount FROM zenylog WHERE (char_id={A} AND src_id={B}) OR (char_id={B} AND src_id={A}) ORDER BY time;`

### C-4. ★Cash→금화(399990) 소액 추적 (fishing17 수정 검증)
- 절차: A가 399930으로 Cash → **100개 미만**(예 ×5) 환전.
- 기대: **100개 미만이어도** picklog에 nameid=399990, type=N, amount=+5 기록(항상 로그).
- 검증: `SELECT time,type,amount FROM picklog WHERE nameid=399990 AND char_id={A} ORDER BY id DESC;`

### C-5. 금화 이동 타입별 (T/P/G/R/E)
- 절차: A→B 금화 직접거래(T), 드롭/습득(P), 길드창고 입출고(G), 개인창고 A1↔A2(R), 우편(E) 각각 소량.
- 기대: picklog에 각 type으로 양쪽 기록(unique_id=0, amount 부호 반대). R은 동일 account.
- 검증: `SELECT type,char_id,amount,time,map FROM picklog WHERE nameid=399990 AND char_id IN(...) ORDER BY time;` 타입/부호/페어 확인. (참고 문서: `doc/need_summer_gold_coin_movement_types.md`)

### C-6. Monitor 웹 조회
- 절차: `tools/need_summer_monitor` 기동, 로그인 → Origin 목록/Account 타임라인/Shadow 추적/Zeny·Gold 이동/수령 계정 집중.
- 기대: 위 C-1~C-5의 실데이터가 웹에 정확히 표시, 배지(정확/높음/중간/후보).
- 검증: 각 화면이 DB 값과 일치, Shadow unique_id 체인·집중 계정 유입이 보이는지.

### C-7. 계정 간 집중 패턴
- 절차: A/B/C(서로 다른 IP)가 이벤트 보상을 특정 수령계정 M으로 이동.
- 기대: Monitor "수령 계정 집중"에서 M으로의 유입 계정 수·서로 다른 IP 수·유형별(Shadow/Zeny/Gold) 표시.
- 검증: 웹 "집중 상세" ↔ picklog/zenylog/reward_log 대조.

---

## D. 우선순위 (시간 없으면 ★부터)
1. **A-9 스냅샷/스윕 멱등성**, **B-2 예약 멱등성** — 중복 보상 방지(가장 치명적).
2. **B-4 발송 / B-9 크래시 안전성** — 보상 손실/중복.
3. **B-8 REVIEW 재큐**, **B-7 재시도** — 실패 복구.
4. **C-4 금화 소액 로그**, **C-2 Shadow 추적** — 추적 정확도.
5. 나머지 정상 경로.

## E. 회귀 주의
- 스냅샷/예약은 OnInit·OnTimer(5분)·랭킹UI 열 때마다 자동 실행됨 → **반복 실행 안전성(A-9/B-2)**이 반드시 통과해야 함.
- 랭킹 보상은 battle config라 **on 변경 시 재시작/@reloadbattleconf 필요**(스크립트 리로드로는 반영 안 됨).

# NEED 2026 여름 교환소 운영

## 위치와 활성화

- NPC: `바캉스 상인 코코#summer2026`
- 위치: `comodo,187,160`
- 대화 NPC가 숨김 Barter `need_summer_fragment_exchange`와
  `need_summer_token_shop`을 `callshop`으로 연다.
- `npc/NEED/need_summer_shop.txt`의 `$NS_SHOP_ON`, `$NS_EXCHANGE_ON`,
  `$NS_TOKEN_SHOP_ON` 값은 모두
  기본 0이다. SQL 적용과 점검을 마친 뒤 필요한 값을 1로 바꾸고 안전한
  재시작으로 반영한다.
- 긴급 종료는 세 값 중 전체 값을 0으로 바꾸고 재시작한다. 이미 열린 UI의
  구매도 C++ 구매 훅이 다시 확인하므로 재료와 횟수를 소비하지 않는다.

## 기간과 논리 일자

- 이벤트 ID: `202608`
- 콘텐츠 기간: 2026-08-15 04:00 ~ 2026-09-12 03:59
- 교환·상점 기간: 2026-08-15 04:00 ~ 2026-09-19 03:59
- 오전 4시를 기준으로 날짜를 이동해 계산한다.
- 1주차 8/15~8/21, 2주차 8/22~8/28, 3주차 8/29~9/4,
  4주차 9/5~9/18이다. 판매 유예 기간에도 4주차 잔여 횟수만 쓰며 5주차는
  생성하지 않는다. 지난 주 미사용 횟수는 이월되지 않는다.

## 상품과 제한

| 유형 | 교환 | 계정 제한 | IP 제한 |
|---|---|---|---|
| FRAGMENT_EXCHANGE | 수박 조각 20 → 토큰 10 | 일 3 | 일 3 |
| 의문의 알 12610 | 토큰 10 | 없음 | 없음 |
| ZENY_POUCH 399929 | 토큰 100 | 주 2, 시즌 8 | 주 2, 시즌 8 |
| CASH_TICKET 399930 | 토큰 250 | 시즌 2 | 시즌 2 |
| SHADOW_BOX 399931 | 토큰 100 | 주 2, 시즌 8 | 주 2, 시즌 8 |
| ARIA_BOX 399935 | 토큰 300 | 시즌 1 | 시즌 1 |

Barter 수량 1은 표의 한 묶음이다. 조각 교환만 `OutputAmount: 10`을 사용해
한 묶음에 토큰 10개를 지급한다. 코코넛 토큰은 인벤토리에서만 차감하며 개인
창고의 토큰이나 수박 조각은 자동 사용하지 않는다. 399931 사용 Script와
후원 코인 추가 지급은 이 작업 범위가 아니다. 후원 코인으로 얻는 토큰도 같은
계정·IP 제한을 우회할 수 없다.

## 원자 제한과 가족 예외

구매 전 모든 인벤토리·무게 검사가 끝난 뒤 InnoDB 트랜잭션을 시작한다.
계정 행과 IP 행을 고유 키로 생성하고 잠근 다음 일일·주간·시즌 횟수를 함께
예약한다. 어느 한 제한이라도 넘으면 롤백하므로 재료도 횟수도 소비하지 않는다.
빠른 중복 요청은 같은 키의 행 잠금으로 직렬화된다.

가족 예외는 기존 출석의 활성 가족 그룹·구성원 테이블을 조회한다. 같은 IP
한도를 이미 사용한 계정과 현재 계정이 같은 활성 그룹일 때만 IP 증가를
면제한다. 계정 제한은 항상 예약하므로 가족 예외로 완화되지 않는다. 그룹과
무관한 제3계정은 계속 거절된다.

기본 Barter는 SQL과 인벤토리를 하나의 원자 트랜잭션으로 묶지 않는다. 이
구현은 제한 횟수 SQL 트랜잭션을 아이템 차감·지급 구간 동안 유지하고 지급
성공 뒤 커밋한다. 사전 무게·슬롯 검사 후의 비정상 아이템 지급 실패에서는
카운터를 롤백하지만, 이미 차감된 인벤토리까지 SQL로 되돌릴 수는 없다.
여름 상품은 모두 단순 스택형이며 이 위험은 서버 오류 상황에 한정된다.
SQL 커밋 결과가 불확실하면 재시작 전까지 여름 상점만 fail-closed한다.

## 로그와 운영 조회

성공한 재료 차감과 상품 지급은 `LOG_TYPE_BARTER` picklog에 남는다.
제한 상품은 `need_summer_shop_log`에도 성공·거절·전달 실패를 ASCII 결과와
실패 코드로 남긴다. 의문의 알은 무제한이므로 별도 카운터와 이벤트 로그 없이
기존 picklog만 사용한다.

```sql
-- 현재 모든 카운터
SELECT event_id,purchase_type,scope_type,account_id,
       INET6_NTOA(ip) AS ip,period_type,period_key,used_count,
       family_group_id,first_account_id,last_account_id,updated_at
FROM need_summer_shop_counter
WHERE event_id=202608
ORDER BY purchase_type,period_type,period_key,scope_type,account_id;

-- 계정 구매 이력
SELECT *,INET6_NTOA(ip) AS ip_text
FROM need_summer_shop_log
WHERE event_id=202608 AND account_id=?
ORDER BY log_id DESC;

-- IP 구매 이력
SELECT *,INET6_NTOA(ip) AS ip_text
FROM need_summer_shop_log
WHERE event_id=202608 AND ip=INET6_ATON(?)
ORDER BY log_id DESC;

-- 거절·실패 내역
SELECT log_id,logical_date,account_id,INET6_NTOA(ip) AS ip,
       purchase_type,result,failure_code,created_at
FROM need_summer_shop_log
WHERE event_id=202608 AND result<>'DELIVERED'
ORDER BY log_id DESC;

-- 1~4주차 및 시즌 카운터 검증
SELECT purchase_type,scope_type,period_type,period_key,
       COUNT(*) AS rows_count,SUM(used_count) AS total_used,
       MAX(used_count) AS max_used
FROM need_summer_shop_counter
WHERE event_id=202608
GROUP BY purchase_type,scope_type,period_type,period_key
ORDER BY purchase_type,scope_type,period_type,period_key;
```

테이블 미설치·비 InnoDB·SQL 오류·구성 불일치는 여름 상점에만 fail-closed로
적용된다. 일반 rAthena Barter Shop에는 제한 훅이 적용되지 않는다.

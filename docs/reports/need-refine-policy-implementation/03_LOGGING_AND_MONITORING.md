# 03. 기록과 4주 관측

## 1. 이번 구현으로 기록되는 것

천장 NPC의 모든 구매 성공은 `npclog`에 남는다. `logmes` 형식은 기존 06 쿼리와 호환되며, 공유 상품에만 `skey=`가 끝에 붙는다. 시각·account_id·char_id는 npclog가 자동 기록한다.

```
[LuckyCeil] pid=100 code=BS_BLESSING   reward=6635x1 bound=0 point=200  egg=0x0     zeny=10000000  left=99
[LuckyCeil] pid=102 code=REFINE9       reward=6232x1 bound=0 point=2000 egg=0x0     zeny=100000000 left=0
[LuckyCeil] pid=103 code=BS_TO_REFINE7 reward=6230x1 bound=0 point=0    egg=6635x1  zeny=0         left=-1
[LuckyCeil] pid=104 code=BS_TO_REFINE9 reward=6228x1 bound=0 point=0    egg=6635x15 zeny=0         left=0 skey=102
```

| 지표 | 원천 | 정확도 |
|---|---|---|
| 대축 천장 교환(1천만) | `code=BS_BLESSING` | 정확 |
| +9 포인트 교환(2,000P + 1억) | `code=REFINE9` | 정확 |
| **대축 → +7 교환과 대축 소모량** | `code=BS_TO_REFINE7`, `egg=6635x1` | 정확 |
| **대축 → +9 교환과 대축 소모량** | `code=BS_TO_REFINE9`, `egg=6635x15` | 정확 |
| +9 공용 재고 차감 | `left=`(차감 후 DB 재고), `skey=102` | 정확 |
| 재고 거부·DB 오류 | `TAKE REJECTED`, `SQL TAKE FAILED`, `SQL VERIFY FAILED` | 정확 |
| 주기 리셋 | `CYCLE RESET pid=… stock=…` | 정확 |

**중복 집계 방지:** 구매 1건 = `[LuckyCeil] pid=` 행 1개. 리셋·거부·관리자 행은 접두어가 달라 06 Q1의 `LIKE '[LuckyCeil] pid=%'` 조건에 걸리지 않는다.

## 2. 기록되지 않는 것

| 항목 | 이유 | 제안 (미적용) |
|---|---|---|
| **+7 바터 200P 교환** | 바터 구매는 C++(`npc_barter_purchase`)에서 처리되어 `logmes`를 남길 수 없다. picklog는 Etc라 필터(`log_filter: 4080`) 대상이고, 제니가 0이라 zenylog에도 남지 않는다 | **운영자 결정(2026-10-21): §2.1 패치 미적용, §2.2 보유 스냅숏으로 대리 관측** |
| 대축 전체 발행·소비(행알 직접, 전장, 환생, 추석 외, 제련 소모) | Etc 아이템 미기록 | 2차 보고서 03 §1의 전용 요약 로그 설계 |
| 대축 시세 | 노점·구매노점 거래 미기록 | `vending_items`·`buyingstore_items` 스냅숏(2차 07 G7) |

### 2.1 +7 바터 계측 최소 패치 제안 (**운영자 결정으로 적용하지 않음**, 기록용으로만 보존)

`src/map/npc.cpp` `npc_barter_purchase`의 구매 루프 끝(아이템 지급 성공 뒤)에 상점 이름이 일치할 때만 npclog 1행을 남긴다.

```cpp
// NEED: 행운 포인트 바터(+7 보장권 등) 구매 계측 - npclog 1행
if( barter->name == "need_lucky_mileage_shop" ){
	char buf[128];
	safesnprintf( buf, sizeof( buf ), "[LuckyMileage] item=%u amount=%u zeny=%u",
		purchase.item->nameid, output_amount, purchase.item->price * purchase.amount );
	log_npc( &sd, buf );
}
```

- 범위: 상점 1개만 대상이고 로그량은 구매 수와 같다. 맵서버 재빌드가 필요하다.
- 위험: 구매 루프 안이라 실패 경로(`summer_failure`) 이전에만 기록되도록 위치를 정해야 한다.

### 2.2 +7 교환량 대리 관측 (운영자 수용 방식)

배포 직전 1회(기준선)와 이후 매주 같은 요일·시각에 보유 스냅숏을 뜬다. 쿼리는 2차 보고서 07 Q5와 같은 SELECT 전용이며, 운영자가 실행한다.

```sql
SELECT nameid, SUM(qty) AS qty FROM (
  SELECT nameid, amount AS qty FROM inventory      WHERE nameid IN (6230, 6234, 22911, 22926)
  UNION ALL SELECT nameid, amount FROM cart_inventory WHERE nameid IN (6230, 6234, 22911, 22926)
  UNION ALL SELECT nameid, amount FROM storage        WHERE nameid IN (6230, 6234, 22911, 22926)
  UNION ALL SELECT nameid, amount FROM storage2       WHERE nameid IN (6230, 6234, 22911, 22926)
  UNION ALL SELECT nameid, amount FROM guild_storage  WHERE nameid IN (6230, 6234, 22911, 22926)
  UNION ALL SELECT nameid, amount FROM mail_attachments WHERE nameid IN (6230, 6234, 22911, 22926)
) x GROUP BY nameid;
```

함께 보는 값:
- 행운 포인트(399905) 보유 분포(2차 07 G1) — 1,000P 이상 구간이 줄면 포인트 소모가 늘었다는 신호
- npclog `code=BS_TO_REFINE7` 건수(대축 → +7, 정확값)

**해석식 (추정, [ESTIMATE]):**

```
+7 보장권·상품권 순증(주) = 바터 200P 교환 + 행알 직접 당첨 + 대축→+7(npclog 정확값)
                          − 홀그렌 +7 제련권 사용 − (기타 소멸)
```

- 대축 → +7은 npclog로 정확히 빼낼 수 있다.
- 바터 교환량만 따로 떼어낼 수는 없다. 행알 직접 당첨(0.9%)과 제련권 사용이 같은 순증에 섞이기 때문이다. 따라서 이 지표는 **추세 신호**로만 쓴다.
  - 예: 배포 후 순증이 기준선보다 크게 늘면, 200P 인하로 바터 교환이 늘었다고 해석한다.
- 포장(보장권 → 상품권)은 6230/6234가 줄고 22911/22926이 같은 수만큼 늘 뿐이라 합계에는 영향이 없다. 그래서 네 ID를 **합산해서** 본다.
- 한계: 거래로 다른 계정에 넘어간 상품권은 합계에 그대로 남는다. 노점에 올린 물건은 카트에 포함된다.

## 3. 집계 SQL (운영자가 배포 후 실행, 이번에 실행하지 않음)

```sql
SELECT
  DATE_SUB(DATE(DATE_SUB(npc_date, INTERVAL 22 HOUR)),
           INTERVAL (DAYOFWEEK(DATE_SUB(npc_date, INTERVAL 22 HOUR)) % 7) DAY) AS wk,
  SUBSTRING_INDEX(SUBSTRING_INDEX(mes, ' code=', -1), ' ', 1) AS code,
  COUNT(*) AS n, COUNT(DISTINCT account_id) AS accounts,
  SUM(CAST(SUBSTRING_INDEX(SUBSTRING_INDEX(mes, ' point=', -1), ' ', 1) AS UNSIGNED)) AS points,
  SUM(CAST(SUBSTRING_INDEX(SUBSTRING_INDEX(mes, ' zeny=', -1), ' ', 1) AS UNSIGNED)) AS zeny,
  SUM(CASE WHEN mes LIKE '% egg=6635x%'
           THEN CAST(SUBSTRING_INDEX(SUBSTRING_INDEX(mes, ' egg=6635x', -1), ' ', 1) AS UNSIGNED) ELSE 0 END) AS blessings_spent
FROM npclog
WHERE npc_date >= '<배포 시각>' AND mes LIKE '[LuckyCeil] pid=%'
  AND mes REGEXP 'code=(BS_BLESSING|REFINE9|BS_TO_REFINE7|BS_TO_REFINE9) '
GROUP BY wk, code ORDER BY wk, code;
```

- 미지급 의심 점검: 같은 시각대의 `TAKE` 뒤 `[LuckyCeil] pid=` 행이 없는 경우를 재고 차이(base − 현재 − 구매 행 수)로 대조한다.
- npclog는 MyISAM에 `npc_date` 인덱스가 없어 전체 스캔이다. 기간을 짧게 잡고, 가능하면 복제본에서 실행한다.

## 4. 4주 관측 지표 (배포 승인 후 시작)

| # | 지표 | 원천 | 비고 |
|---|---|---|---|
| 1 | 대축 천장 주간 교환량 / 고유 계정 / 최다 계정 비중 / 주 100 소진 여부 | npclog BS_BLESSING, `need_lucky_ceiling_stock` | 계정 한도 미구현 → 편중 집중 감시. 최다 계정 비중 30% 이상이면 한도 도입 검토 |
| 2 | 대축 → +7 / +9 교환 건수, 대축 소모량, +9 월 재고 소진 시점 | BS_TO_REFINE7/9, `egg=6635x`, PID 102 `updated_at` | |
| 3 | +7 200P 교환량, +7 보장권·상품권 유통 | 직접 기록 없음. **§2.2 보유 스냅숏으로 대리 관측(운영자 수용)** | 추세 신호로만 사용. 정확한 교환 건수는 얻을 수 없음 |
| 4 | 대축 서버 보유량, 관측 가능한 발행·소비 | 보유 스냅숏(2차 07 G2), 위 npclog | **관측 범위: 천장·추석 발행, 천장 소모만**. 행알·전장·환생·제련 소모는 미관측 |
| 5 | +9 포인트 교환 vs 대축 15개 교환, 전장 공급과 구분 | REFINE9 / BS_TO_REFINE9. 전장 바터는 기록 없음 | |

- 4주 관측 후 대축 → +9 가격을 15 → 13 → 12로 인하할지 **운영자가 별도 결정**한다. 코드에 자동 인하는 없다.

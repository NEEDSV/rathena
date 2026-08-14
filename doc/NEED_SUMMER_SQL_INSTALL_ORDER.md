# NEED 여름 이벤트 SQL 설치 순서 / 전수 점검 (fishing15 기준)

작업트리: `E:\tools\Need\need-summer-fishing`. 모든 SQL은 **운영 DB 수동 적용**(자동 실행 없음).

---

## 1. 전체 SQL 파일 목록

### A. Cumulative(신규 서버 설치용, 각 테이블 `CREATE TABLE IF NOT EXISTS`)

| # | 파일 | 생성 테이블 | 비고 |
|---|---|---|---|
| 1 | `sql-files/need_summer_attendance.sql` | attendance_account / _online / _family_group / _family_member / _family_audit / _ip_daily / _claim / **_reward_outbox** | outbox에 `bonus_item_id/bonus_amount/last_error_code` 등 **최종 스키마 포함** |
| 2 | `sql-files/need_summer_hunt.sql` | hunt_golden_claim / _golden_ip_daily / _golden_log | 황금 수박 획득(사냥) |
| 3 | `sql-files/need_summer_shop.sql` | shop_counter / shop_log | 코코넛 토큰 상점 |
| 4 | `sql-files/need_summer_fishing.sql` | fishing_reward / _catch / _collection / _weekly_best / _weekly_result / **_rank_reward_outbox** | 낚시 전체(1~4차) |
| 5 | `sql-files/need_summer_reward_log.sql` | **reward_log** | (fishing15 신규) 보상 Origin Log |

### B. 선택/조건부

| 파일 | 내용 | 언제 |
|---|---|---|
| `sql-files/need_summer_fishing_fish_id_migration.sql` | catch fish_id `UPDATE`(8→12종 재배치) | **기존 테스트 catch 데이터를 보존**할 때만. 신규 서버는 불필요 |

### C. Upgrades(이미 구버전이 적용된 서버용 incremental, `sql-files/upgrades/`)

| 파일 | 작업 | cumulative 대응 |
|---|---|---|
| `upgrade_20260806.sql` | attendance 8 테이블 CREATE | = need_summer_attendance.sql (구 초기본) |
| `upgrade_20260806_2.sql` | `ALTER attendance_reward_outbox ADD last_error_code` | attendance.sql 최종본에 이미 포함 |
| `upgrade_20260806_3.sql` | hunt 3 테이블 CREATE | = need_summer_hunt.sql |
| `upgrade_20260811.sql` | fishing **rank_reward_outbox** CREATE | = need_summer_fishing.sql 의 일부(fishing13) |
| `upgrade_20260812.sql` | `ALTER attendance_reward_outbox ADD bonus_item_id, bonus_amount` | attendance.sql 최종본에 이미 포함 |

---

## 2. 신규 서버 설치 순서 (권장)

> **Cumulative(A) 파일만 적용하고, `upgrades/` 폴더는 적용하지 마세요.** 각 파일은 `CREATE TABLE IF NOT EXISTS`라 재실행 안전.

```
01. sql-files/need_summer_attendance.sql
02. sql-files/need_summer_hunt.sql
03. sql-files/need_summer_shop.sql
04. sql-files/need_summer_fishing.sql
05. sql-files/need_summer_reward_log.sql
```

- 파일 간 FK 의존은 없음(rAthena 관례). 위 순서는 도메인 정리용이며 순서를 바꿔도 생성은 성공.
- `need_summer_fishing_fish_id_migration.sql`은 신규 서버에서 **적용하지 않음**(기존 catch 데이터가 없으므로).
- 시스템 우편 지급(attendance outbox / fishing rank outbox)은 char/mail/mail_attachments 테이블을 사용하며, 이는 rAthena 기본 스키마이므로 별도 생성 불필요.

각 SQL 메타:

| 파일 | 선행 필요 | 재실행 안전 | 실서버 적용 |
|---|---|---|---|
| need_summer_attendance.sql | 없음 | O (IF NOT EXISTS) | 필요 |
| need_summer_hunt.sql | 없음 | O | 필요 |
| need_summer_shop.sql | 없음 | O | 필요 |
| need_summer_fishing.sql | 없음 | O | 필요 |
| need_summer_reward_log.sql | 없음 | O | 필요(fishing15) |
| need_summer_fishing_fish_id_migration.sql | fishing.sql(catch) | UPDATE(멱등, 단 1회 의미) | 조건부(데이터 보존 시) |

---

## 3. 기존(이미 적용된) 서버 업그레이드 경로

이미 여름 이벤트 초기본을 적용한 서버는 **누락된 upgrades만** 순서대로 적용:

```
upgrade_20260806.sql      (attendance 최초 적용분 - 이미 있으면 건너뜀)
upgrade_20260806_2.sql    (attendance outbox last_error_code 추가)
upgrade_20260806_3.sql    (hunt 테이블)
upgrade_20260812.sql      (attendance outbox bonus_item_id/bonus_amount 추가)
upgrade_20260811.sql      (fishing rank_reward_outbox)
그리고 need_summer_reward_log.sql  (fishing15 reward_log)
```

> 어떤 upgrade가 이미 적용됐는지 모르면, 해당 테이블/컬럼 존재 여부를 먼저 확인 후 미적용분만 실행.

---

## 4. 중복 / 충돌 / 폐기 의심 점검 (§14)

### ⚠️ 핵심 충돌 — ALTER upgrade vs 신규 cumulative
- `need_summer_attendance.sql`의 outbox CREATE에는 이미 `last_error_code`(`:115`), `bonus_item_id`(`:109`), `bonus_amount`(`:110`)가 **포함**됨.
- `upgrade_20260806_2.sql`(ADD last_error_code), `upgrade_20260812.sql`(ADD bonus_item_id/bonus_amount)에는 **`IF NOT EXISTS` 방어가 없음**.
- 따라서 **신규 서버(attendance.sql 적용)에서 이 두 ALTER를 실행하면 `Duplicate column` 오류**가 발생.
- 조치: **신규 서버는 upgrades/ 폴더를 적용하지 않음**(위 2번). ALTER upgrade는 오직 "구버전 outbox(컬럼 없음)"가 이미 있는 서버에만.

### 중복 CREATE(모두 `IF NOT EXISTS`라 무해, 재실행 안전)
- attendance 8종: `need_summer_attendance.sql` ↔ `upgrade_20260806.sql`
- hunt 3종: `need_summer_hunt.sql` ↔ `upgrade_20260806_3.sql`
- fishing rank_reward_outbox: `need_summer_fishing.sql` ↔ `upgrade_20260811.sql`
- → 동일 테이블을 두 파일이 생성하지만 IF NOT EXISTS로 2번째는 no-op. **버그 아님**. 단 "cumulative + 해당 upgrade를 둘 다 신규 서버에 돌리는" 혼용만 피하면 됨(위 2번은 cumulative만).

### 인덱스/제약 중복
- 각 CREATE 내부에서만 인덱스 정의(IF NOT EXISTS 테이블). 별도 `CREATE INDEX` 패치는 없음 → 중복 생성 위험 없음.

### 테스트/폐기 SQL
- `need_summer_fishing_fish_id_migration.sql`은 **선택적 마이그레이션**(테스트 catch 보존용). 실서버 무차별 실행 금지 — 신규 서버 목록에서 제외.
- `upgrade_20260806_2.sql` 내에도 fishing12 단계에서 만든 테스트용 데이터 삭제 SQL은 없음(확인함). 테스트 데이터 정리는 GM NPC/수동 SQL로만.
- 폐기된 SQL 파일은 발견되지 않음.

---

## 5. 앞으로의 SQL 관리 권장 (§15, 제안만)

현재 프로젝트 상태(cumulative + 날짜별 upgrades 혼재, ALTER-vs-fresh 충돌 리스크)에서 가장 안전한 방식:

- **권장: A(설치 순서 문서) + 이 문서 유지**를 1차로.
  - 이유: 기존 파일 구조를 건드리지 않아 이미 적용된 운영 서버에 영향 0. 신규 서버는 "cumulative만" 규칙으로 충돌 회피.
- **선택: B(신규 서버용 통합 SQL) 병행 고려** — `need_summer_2026_full.sql`을 새로 만들어 위 2번의 cumulative 5개를 한 파일로 묶어 신규 서버 배포 실수를 줄임. 기존 개별/upgrade 파일은 이미 적용된 서버용으로 그대로 유지.
  - 단 이번 Phase에서는 **생성하지 않음**(§15: 임의 병합/삭제 금지). 승인 시 후속으로 생성 가능.
- **C(001_/002_ migration 번호 체계)**: 이미 날짜(upgrade_YYYYMMDD) 관례가 자리 잡았고 파일이 많아 전면 재번호는 오히려 혼선 → **권장하지 않음**.

즉 **A(이 문서) 채택 + B는 승인 후 통합본 생성** 을 권장합니다. 이번 Phase에서 기존 SQL 파일을 합치거나 삭제하지 않았습니다.

---

## 6. 로그 설정 검증 (§11)

`conf/log_athena.conf` 현재 값:
- `enable_logs: 0xFFFFFFFF` (`:44`) — 모든 로그 type 활성.
- `log_filter: 4080` (`:74`) = `0xFF0` = WEAPON(0x10)+ARMOR(0x20)+CARD+PETITEM+PRICE+AMOUNT+REFINE+CHANCE.
- `log_zeny: 100000` (`:93`) — **절대값 10만 미만 Zeny 이동은 zenylog 미기록.**
- `log_cash: yes` (`:96`) — cashlog 활성(금액 필터 없음).

**Shadow 6종 picklog 기록 여부**: `should_log_item`(`log.cpp:149-175`)은 (a) nameid 399925~399935 항상 로그, (b) 그 외는 log_filter+타입. 6종 쉐도우(S_Spiritual_Weapon=IT_WEAPON, S_Malicious_Armor/Shield/Shoes·S_Spiritual_Earring/Pendent=IT_ARMOR)는 **log_filter 4080에 WEAPON(0x10)·ARMOR(0x20) 비트가 모두 켜져 있어 전부 picklog에 기록됨.** → **로그 필터 조정 불필요.**

- (참고) 만약 향후 log_filter에서 weapon/armor를 끄면 6종이 누락됨. 그 경우엔 log_filter를 넓히기보다 `should_log_item`에 6종(또는 shadow 범위)만 항상-로그로 추가하는 편이 로그량 증가가 적어 안전. 현재 설정에서는 조치 불필요.

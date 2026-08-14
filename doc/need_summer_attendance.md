# NEED 2026 summer attendance provider

## Scope and event rules

The stock attendance UI and packets remain the client-facing layer. While
`feature.need_summer_attendance` is on, progress, eligibility, daily limits,
and claim reservations are owned by the summer SQL provider;
`#AttendanceDate`, `#AttendanceCounter`, `AttendanceDatabase`, and stock
attendance mail delivery are not authorization inputs. When that feature is
off, the unmodified stock attendance database, account variables, and mail
delivery path remain available.

The configured participation window is 28 logical dates, 2026-08-15 through
2026-09-11 inclusive. A logical date changes at 04:00 in the map-server host's
local timezone. Each account can claim on any 20 of those dates.

The provider counts normal online sessions only. It flushes account-wide time
to SQL every 30 seconds, at 1,800 seconds, on logout, before entering
autotrade, and before changing map-server. Reconnects and character changes
therefore continue from the same `(event_id, logical_date, account_id)` row.
Autotrade sessions never add time.

## Installation and activation

1. Keep `feature.need_summer_attendance` off while installing or upgrading.
2. 새 설치는 `sql-files/upgrades/upgrade_20260806.sql` 또는 독립 스키마
   `sql-files/need_summer_attendance.sql`을 적용한다. 기존 여름 스키마가 이미
   설치된 DB에는 추가로 `sql-files/upgrades/upgrade_20260806_2.sql`을 적용해
   ASCII `last_error_code` 컬럼을 만든다.
3. Verify the host timezone and that 04:00 local time is the intended reset.
4. Start map-server and verify that all eight `need_summer_attendance_*` tables
   are accessible to the configured SQL user.
5. Enable both `feature.attendance` and
   `feature.need_summer_attendance`, then restart map-server.

Do not use `@reloadattendancedb` for this provider. The stock attendance YAML
is intentionally empty and the summer provider is initialized with map-server.

## SQL ownership and transaction boundary

- `need_summer_attendance_online`: account-wide seconds for one logical date.
- `need_summer_attendance_account`: total successful reservations, capped at 20.
- `need_summer_attendance_ip_daily`: the one IP slot for a logical date.
- `need_summer_attendance_claim`: immutable account/day claim ledger.
- `need_summer_attendance_reward_outbox`: durable reward-delivery request.
- `need_summer_attendance_family_*`: approved family membership and audit trail.

The claim transaction locks in this order: account total, account/day claim,
account/day online time, active family membership, IP/day slot. Only after all
checks pass does it insert the claim and reward outbox rows and increment the
account total. A request below 1,800 seconds rolls back before the IP row is
inserted, so it cannot consume the shared IP allowance.

The IP row records the family group of its first claimant. Another account may
reuse that IP only when both accounts resolve to the same active, pre-approved
family group. Account/day uniqueness is never waived.

### 보상 outbox 전달 경계

수령 트랜잭션은 `399925` 10개와 `399928` 1개를 하나의 claim 및 outbox
행으로 예약한다. map-server의 5초 타이머가 한 번에 최대 20건을 소비하며,
두 기능 플래그와 SQL 스키마 검사가 모두 통과한 경우에만 동작한다.
`feature.need_summer_attendance: off` 상태에서는 outbox를 조회하거나 우편을
생성하지 않는다.

일반 `intif_Mail_send` 경로는 map-server 요청 후 char-server가 비동기로
응답하므로 outbox 상태와 동일 SQL 트랜잭션으로 묶을 수 없다. 이 consumer는
char-server가 사용하는 정규 `mail` 및 `mail_attachments` 테이블에 시스템
우편을 기록하고, outbox를 `DELIVERED`로 바꾸는 작업을 하나의 InnoDB
트랜잭션으로 실행한다. `inter_athena.conf`의 `char_db`, `mail_db`,
`mail_attachment_db` 사용자 지정 테이블 이름도 따른다.

outbox 상태는 `0=PENDING`, `1=PROCESSING`, `2=DELIVERED`, `3=RETRY`,
`4=REVIEW`이다. 대상 행은 `FOR UPDATE`로 잠그며 `claim_id`와 `mail_id`에
각각 고유 제약이 있다. 우편 본문, 두 첨부 행, outbox 완료, claim 완료 중
하나라도 실패하면 전부 롤백한다. 일시 실패는 60초 뒤 재시도하며 5번째
실패부터 `REVIEW`로 남긴다. 데이터 불일치나 존재하지 않는 캐릭터는 즉시
`REVIEW`가 된다. 완료 여부와 관계없이 outbox 행은 삭제하지 않는다.

`last_error_code`는 `ascii` 문자셋의 기계 판독용 코드다. `last_error`도
인코딩과 무관하게 같은 ASCII 코드를 저장하고, 사람이 읽는 한글 상세 원인은
map-server 로그에만 남긴다. 따라서 한글 상세 오류의 변환 문제로 RETRY 또는
REVIEW 갱신이 다시 실패하지 않는다. 이 ASCII 상태 갱신 자체가 실패하거나
대상 행을 정확히 한 건 갱신하지 못하면 consumer는 메모리에서 fail-closed
상태가 된다. 서버를 재시작할 때까지 추가 outbox를 소비하지 않고 신규 출석
수령도 거절하여, 전달할 수 없는 보상이 더 예약되지 않게 한다.

운영 활성화 전 아이템 DB에 `399925`와 `399928`이 실제로 등록되어 있어야
한다. 현재 스키마나 우편 테이블이 없고, 접근할 수 없거나, 필요한 테이블이
InnoDB가 아니면 provider와 consumer 모두 fail-closed 처리된다.

## Family approval procedure

Family exceptions must be registered before either account claims on the
shared IP. Use a named operator and a ticket/reason, and write the audit row in
the same administrative transaction. Example:

```sql
START TRANSACTION;
INSERT INTO `need_summer_attendance_family_group`
  (`event_id`,`group_name`,`approved_by`,`reason`)
VALUES (202608,'family-ticket-1234','operator-name','ticket 1234 approved');
SET @family_group_id = LAST_INSERT_ID();

INSERT INTO `need_summer_attendance_family_member`
  (`event_id`,`account_id`,`family_group_id`,`approved_by`,`reason`)
VALUES
  (202608,100001,@family_group_id,'operator-name','ticket 1234 approved'),
  (202608,100002,@family_group_id,'operator-name','ticket 1234 approved');

INSERT INTO `need_summer_attendance_family_audit`
  (`event_id`,`family_group_id`,`account_id`,`action`,`operator`,`reason`)
VALUES
  (202608,@family_group_id,100001,'ADD','operator-name','ticket 1234 approved'),
  (202608,@family_group_id,100002,'ADD','operator-name','ticket 1234 approved');
COMMIT;
```

For removal, set the member `active` value to 0 and insert a `REMOVE` audit
row in one transaction. Do not move an account between groups while it has an
in-flight claim request. Prior claims and IP rows remain immutable evidence.

## 운영 조회 SQL

상태별 전체 현황과 마지막 오류를 조회한다.

```sql
SELECT
  `outbox_id`, `claim_id`, `account_id`, `char_id`,
  CASE `status`
    WHEN 0 THEN 'PENDING'
    WHEN 1 THEN 'PROCESSING'
    WHEN 2 THEN 'DELIVERED'
    WHEN 3 THEN 'RETRY'
    WHEN 4 THEN 'REVIEW'
    ELSE CONCAT('UNKNOWN(', `status`, ')')
  END AS `delivery_status`,
  `attempts`, `next_attempt_at`, `last_attempt_at`, `mail_id`,
  `last_error_code`, `last_error`, `created_at`, `delivered_at`
FROM `need_summer_attendance_reward_outbox`
WHERE `event_id` = 202608
ORDER BY `outbox_id`;
```

아직 완료되지 않은 건만 조회한다. 커밋된 `PROCESSING` 행은 정상 구조에서는
생기지 않으므로, 장시간 상태 1인 행은 운영 검토 대상으로 취급한다.

```sql
SELECT `outbox_id`, `claim_id`, `account_id`, `char_id`, `status`,
       `attempts`, `next_attempt_at`, `last_attempt_at`,
       `last_error_code`, `last_error`
FROM `need_summer_attendance_reward_outbox`
WHERE `event_id` = 202608 AND `status` IN (0, 1, 3, 4)
ORDER BY `status`, `next_attempt_at`, `outbox_id`;
```

전달 완료 우편과 첨부 보상을 확인한다.

```sql
SELECT o.`outbox_id`, o.`claim_id`, o.`account_id`, o.`char_id`,
       o.`mail_id`, o.`delivered_at`,
       GROUP_CONCAT(CONCAT(a.`nameid`, ' x', a.`amount`)
                    ORDER BY a.`index` SEPARATOR ', ') AS `attachments`
FROM `need_summer_attendance_reward_outbox` AS o
JOIN `mail` AS m ON m.`id` = o.`mail_id` AND m.`dest_id` = o.`char_id`
JOIN `mail_attachments` AS a ON a.`id` = m.`id`
WHERE o.`event_id` = 202608 AND o.`status` = 2
GROUP BY o.`outbox_id`, o.`claim_id`, o.`account_id`, o.`char_id`,
         o.`mail_id`, o.`delivered_at`
ORDER BY o.`outbox_id`;
```

다음 검증 SQL은 `399925 x10`과 `399928 x1`이 정확히 함께 들어가지 않은
완료 건만 반환한다. 정상 결과는 0행이다.

```sql
SELECT o.`outbox_id`, o.`claim_id`, o.`mail_id`, COUNT(*) AS `attachment_rows`,
       SUM(a.`nameid` = 399925 AND a.`amount` = 10) AS `token_rows`,
       SUM(a.`nameid` = 399928 AND a.`amount` = 1) AS `box_rows`
FROM `need_summer_attendance_reward_outbox` AS o
JOIN `mail_attachments` AS a ON a.`id` = o.`mail_id`
WHERE o.`event_id` = 202608 AND o.`status` = 2
GROUP BY o.`outbox_id`, o.`claim_id`, o.`mail_id`
HAVING `attachment_rows` <> 2 OR `token_rows` <> 1 OR `box_rows` <> 1;
```

운영자가 원인을 해결한 `REVIEW` 건을 재시도 큐로 되돌릴 때는 먼저
`mail_id IS NULL`을 확인하고 단일 행만 갱신한다. consumer가 동작 중인
환경에서는 반드시 유지보수 절차에 따라 실행한다.

```sql
UPDATE `need_summer_attendance_reward_outbox`
SET `status` = 3,
    `next_attempt_at` = NOW(),
    `last_error_code` = 'OPERATOR_RETRY',
    `last_error` = 'OPERATOR_RETRY',
    `updated_at` = NOW()
WHERE `event_id` = 202608
  AND `outbox_id` = :outbox_id
  AND `status` = 4
  AND `mail_id` IS NULL;
```

## Verification checklist

- Before 30 minutes, manually open the UI and verify that claim displays the
  exact remaining minutes/seconds. Confirm no claim or IP/day row was created.
- Accumulate time across reconnect and an account character change; confirm
  the SQL total reaches 1,800 without counting offline gaps twice.
- Enter autotrade, wait, return normally, and confirm the autotrade interval
  did not increase `online_seconds`.
- At 1,800 seconds, verify the ready message and automatic UI opening.
- Claim once and verify a claim row, IP row, incremented account total, and one
  outbox row were committed together. Confirm packet `0x0AF0` updates the UI.
- Repeat on the same account/date and verify rejection with no new rows.
- Repeat from another account on the same IP and verify rejection.
- Register two test accounts in one approved family group, then verify both may
  claim on the same IP while each still remains limited to one claim that day.
- Verify an unrelated third account on that IP remains rejected.
- Test across 03:59/04:00 and verify both time and account/IP daily limits use
  the new logical date after 04:00.
- Seed or exercise 20 claims and verify login no longer auto-opens the UI and a
  21st claim is rejected. Manual UI viewing remains available during the event.
- Restart map-server during partial accumulation and confirm at most the
  unflushed 30-second buffer is lost and no seconds are duplicated.

Do not delete legacy `#AttendanceDate` or `#AttendanceCounter` values. They are
ignored by this provider and remain available for rollback or audit.

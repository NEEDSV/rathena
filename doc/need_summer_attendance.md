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
2. Import `sql-files/upgrades/upgrade_20260806.sql` into the main map-server
   database. `sql-files/need_summer_attendance.sql` is the standalone schema
   for a new installation.
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

### Reward delivery boundary

The current implementation atomically reserves the daily reward payload
(`399925` x10 and `399928` x1) in the outbox and returns the attendance UI
success packet. It does not yet add items to inventory or send mail. The event
must not be opened for production rewards until an idempotent outbox consumer
has been implemented and tested; this avoids pretending that an inventory
write and a MySQL commit are one atomic operation.

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

# NEED attendance UI 28-day PoC

This PoC verifies the MAIN `PACKETVER 20250604` attendance UI and packet path.
It is not the summer event reward implementation. The compiled PoC path updates
the two stock account variables only to drive client UI state and never creates
an item or mail.

## Active test period

- Start: 2026-08-01
- End: 2026-08-28
- Days: 28
- Placeholder: item 512 (`Apple`), one per day
- Delivery: suppressed at compile time by `NEED_ATTENDANCE_UI_POC`
- Day boundary: stock midnight behavior; the production 04:00 boundary is out of scope

Do not use `@reloadattendancedb` on a live server. Restart the map-server in the
isolated test environment after changing the YAML or account state.

## Test-account state setup

Do not delete production account variables. Run the following only against an
offline, dedicated test account after replacing `2000000` with its account ID.
No statement below was executed as part of this PoC.

Fresh state:

```sql
SET @poc_account_id := 2000000;
INSERT INTO `global_acc_reg_num` (`account_id`, `key`, `index`, `value`) VALUES
  (@poc_account_id, '#AttendanceDate', 0, 0),
  (@poc_account_id, '#AttendanceCounter', 0, 0)
ON DUPLICATE KEY UPDATE `value` = VALUES(`value`);
```

Make day 21 claimable and verify auto-open after 20 completed days:

```sql
SET @poc_account_id := 2000000;
INSERT INTO `global_acc_reg_num` (`account_id`, `key`, `index`, `value`) VALUES
  (@poc_account_id, '#AttendanceDate', 0,
    CAST(DATE_FORMAT(DATE_SUB(CURDATE(), INTERVAL 1 DAY), '%Y%m%d') AS UNSIGNED)),
  (@poc_account_id, '#AttendanceCounter', 0, 20)
ON DUPLICATE KEY UPDATE `value` = VALUES(`value`);
```

Make day 28 claimable:

```sql
SET @poc_account_id := 2000000;
INSERT INTO `global_acc_reg_num` (`account_id`, `key`, `index`, `value`) VALUES
  (@poc_account_id, '#AttendanceDate', 0,
    CAST(DATE_FORMAT(DATE_SUB(CURDATE(), INTERVAL 1 DAY), '%Y%m%d') AS UNSIGNED)),
  (@poc_account_id, '#AttendanceCounter', 0, 27)
ON DUPLICATE KEY UPDATE `value` = VALUES(`value`);
```

Show day 28 as received today:

```sql
SET @poc_account_id := 2000000;
INSERT INTO `global_acc_reg_num` (`account_id`, `key`, `index`, `value`) VALUES
  (@poc_account_id, '#AttendanceDate', 0,
    CAST(DATE_FORMAT(CURDATE(), '%Y%m%d') AS UNSIGNED)),
  (@poc_account_id, '#AttendanceCounter', 0, 28)
ON DUPLICATE KEY UPDATE `value` = VALUES(`value`);
```

The test account must be offline while changing these rows. Restart the test
map-server, or reconnect only after ensuring no map/char-server cache can write
the previous values back over the test state.

## Expected packet path

1. Login auto-open or client button sends/initiates UI open.
2. Client button request: `0x0A68`, UI type 5.
3. Server opens attendance UI: `0x0AE2`, UI type 7, encoded progress.
4. Client claim request: `0x0AEF`.
5. Server logs the suppressed claim, updates temporary UI state, and sends
   `0x0AF0` with the claimed day.

## Live-client checklist

- [ ] Attendance UI opens without a client error.
- [ ] Days 1 through 20 are visible and correctly aligned.
- [ ] Days 21 through 28 are visible.
- [ ] A scroll area, page change, or additional area exists for days 21-28.
- [ ] All 28 reward icons and claimed/unclaimed states render.
- [ ] Login opens the UI automatically when today's reward is unclaimed.
- [ ] The client attendance button reopens the UI manually.
- [ ] The UI still auto-opens and manually opens with counter 20.
- [ ] Day 21 sends `0x0AEF` and receives `0x0AF0`.
- [ ] Day 28 sends `0x0AEF` and receives `0x0AF0`.
- [ ] Claiming updates the corresponding day to claimed.
- [ ] Reconnecting preserves the displayed progress.
- [ ] Closing the UI or client produces no map-server error.
- [ ] No mail, inventory item, zeny, cash, or summer-event item is delivered.

Capture the client screen, client packet trace, and map-server PoC log for each
failure. Distinguish a 20-day layout limit from missing client attendance data
and from a rendering or packet-processing failure.

## Keep versus remove

If the client passes, keep the period-length-based auto-open calculation and the
existing attendance packet route. Remove the PoC define, placeholder YAML,
messages, account-variable test procedure, and suppressed-delivery branch when
the SQL-backed summer attendance provider replaces the stock state and reward
path.

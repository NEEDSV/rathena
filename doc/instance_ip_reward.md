# Instance IP daily reward limit

Apply `sql-files/upgrades/upgrade_20260714.sql`, then enable only the intended instance:

```yml
  - Id: 16
    Name: Horror Toy Factory
    IpDailyRewardLimit: 3
    IpRewardMonsters:
      2996: true # Celine Kimi: all regular and MVP item drops are gated
```

For a personal NPC reward, call the command immediately before granting items or
updating the reward-complete quest variable:

```c
.@result = instance_ip_complete(IP_REWARD_PERSONAL);
if (.@result == 0) {
	mes "동일 IP에서 오늘 받을 수 있는 해당 던전의 보상을 모두 수령했습니다.";
	close;
}
if (.@result < 0) {
	mes "보상 처리 중 오류가 발생했습니다.";
	close;
}
getitem 512, 1;
```

To show the remaining count after a successful personal reward without hardcoding
the configured limit:

```c
.@remaining = instance_ip_reward_remaining();
.@limit = instance_ip_reward_limit();
if (.@remaining >= 0 && .@limit > 0) {
	dispbottom "해당 던전의 오늘 보상 횟수가 사용되었습니다.";
	dispbottom "남은 보상 횟수: " + .@remaining + "회 / " + .@limit + "회";
}
```

`instance_ip_reward_remaining()` is read-only and always queries SQL. It returns
the remaining count, `-1` on player/instance/SQL errors, or `-2` when the instance
does not enable the IP reward limit. `instance_ip_reward_limit()` returns the
configured maximum, with the same negative error/disabled values.

Entering or re-entering a configured instance from an outside map automatically
shows the current remaining count. Movement between maps belonging to the same
runtime instance does not show it again and never consumes a count.

## Querying before entry from an outside NPC

The optional argument is the `Id` from `instance_db.yml`, not a runtime instance
ID. For example, Horror Toy Factory has database ID 16:

```c
.@instance_db_id = 16;
.@remaining = instance_ip_reward_remaining(.@instance_db_id);
.@limit = instance_ip_reward_limit(.@instance_db_id);

if (.@remaining == -1 || .@limit == -1) {
	mes "보상 이용 횟수를 확인하지 못했습니다.";
	mes "잠시 후 다시 시도해 주세요.";
	close;
}

// -2 means that the existing instance has no IP reward limit configured.
if (.@remaining != -2 && .@limit > 0 && .@remaining <= 0) {
	mes "오늘 해당 던전의 보상 이용 횟수를 모두 사용했습니다.";
	mes "남은 보상 횟수: 0회 / " + .@limit + "회";
	mes "초기화 시간: 매일 오전 4시";
	close;
}

// Continue with the existing instance creation or entry logic.
```

Both commands remain backward compatible without an argument: they resolve the
runtime instance ID from the current map and then its database ID. With an argument,
they validate and directly use the `instance_db.yml` ID, so they also work on normal
outside maps where `map_data::instance_id` is zero.

Return values:

- `instance_ip_reward_remaining`: `0+` remaining, `-1` invalid player/DB ID or SQL
  error, `-2` existing instance with the limit disabled.
- `instance_ip_reward_limit`: `1+` configured maximum, `-1` invalid player/DB ID,
  `-2` existing instance with the limit disabled (retained for compatibility).

These entry queries only execute `SELECT`; they do not create an instance, update
the counter, write a deduplication event, or change character/account cooldowns.
The result is only advisory: always call `instance_ip_complete(IP_REWARD_PERSONAL)`
again immediately before granting the actual reward, because another character on
the same IP may consume the last count after this pre-entry query.

## Per-IP family/shared-network overrides

Apply `sql-files/upgrades/upgrade_20260812.sql`. Every query and reward completion
resolves the live effective limit in this order:

1. The IP plus the current `instance_db_id`.
2. The IP plus `instance_db_id = 0` (all limited instances).
3. The instance's `IpDailyRewardLimit` value.

Disabled rows, expired rows, and rows with `daily_limit = 0` are ignored. Overrides
are queried from SQL on every limit check, so changes take effect without a server
restart or reload. They only change the comparison limit; existing daily counter
rows are never reset.

Register a six-use override for every IP-limited instance:

```sql
INSERT INTO `instance_ip_reward_override`
  (`ip`, `instance_db_id`, `daily_limit`, `memo`)
VALUES
  (INET6_ATON('123.123.123.123'), 0, 6, 'Family/shared network');
```

Use a nonzero instance DB ID for a dungeon-specific override. It has priority over
the global row:

```sql
INSERT INTO `instance_ip_reward_override`
  (`ip`, `instance_db_id`, `daily_limit`, `memo`, `expires_at`)
VALUES
  (INET6_ATON('123.123.123.123'), 15, 4, 'Geffen family exception', '2026-09-01 04:00:00');
```

Update, disable, or delete the global override:

```sql
UPDATE `instance_ip_reward_override`
SET `daily_limit` = 6
WHERE `ip` = INET6_ATON('123.123.123.123') AND `instance_db_id` = 0;

UPDATE `instance_ip_reward_override`
SET `enabled` = 0
WHERE `ip` = INET6_ATON('123.123.123.123') AND `instance_db_id` = 0;

DELETE FROM `instance_ip_reward_override`
WHERE `ip` = INET6_ATON('123.123.123.123') AND `instance_db_id` = 0;
```

List overrides in an operator-friendly format:

```sql
SELECT
  INET6_NTOA(`ip`) AS `ip`,
  `instance_db_id`,
  `daily_limit`,
  `enabled`,
  `memo`,
  `expires_at`,
  `updated_at`
FROM `instance_ip_reward_override`
ORDER BY `ip`, `instance_db_id`;
```

Pre-entry checks remain read-only and `instance_ip_complete(IP_REWARD_PERSONAL)`
must still run immediately before the reward. Counter increments keep using the
same conditional UPSERT, with the resolved effective limit substituted for the
YAML default.

The script must keep its existing character/quest completion guard. The IP command
is concurrency-safe across map servers, but a personal reward has no universal event
identifier with which the core could infer that two script calls are the same claim.

The reward date is calculated as local server time minus four hours. The schema uses
`VARBINARY(16)` plus `INET6_ATON`, so it can store IPv4 and IPv6. This rAthena socket
build currently exposes player addresses as IPv4 `uint32`; IPv6 begins working at the
storage layer when the socket/session layer is upgraded to expose an IPv6 address.

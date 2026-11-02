-- =====================================================================================
-- NEED Duo Survival 2v2 - Phase 3 (weekly points, character ranking, cash reward tickets,
-- desertion sanctions, review / audit). MariaDB / MySQL, InnoDB.
--
--   DO NOT RUN ON THE PRODUCTION DATABASE WITHOUT A SEPARATE OPERATOR APPROVAL.
--   Development: applied only to the isolated test DB `ragnarok_duotest` (127.0.0.1:3310).
--
-- Apply  : run this file once on the game DB (the map-server's main SQL connection).
--          Every statement is CREATE TABLE IF NOT EXISTS: running it twice changes nothing.
-- Check  : SHOW CREATE TABLE need_bg2_reward;  -- the unique keys below must be present
-- Rollback (only after the feature is switched off and nothing is pending):
--          the tables hold the reward / payment ledger - never DROP them while a ticket is
--          RESERVED (status 1) or unclaimed tickets are owed. To remove an unused install:
--          DROP TABLE need_bg2_audit, need_bg2_review, need_bg2_desertion, need_bg2_week,
--                     need_bg2_reward, need_bg2_match_player, need_bg2_match;
-- Nothing else (existing tables, #CASHPOINTS, cashlog) is changed by this file.
-- Free-text columns (note / reason / detail) are VARBINARY: the script writes CP949 bytes and the
-- map-server SQL connection has no fixed codepage, so the bytes are stored and read back unchanged.
-- Times are unix seconds (UTC). Weeks are computed by the script in Asia/Seoul (UTC+9):
-- week_start = unix time of Monday 04:00 KST that opens the week, [week_start, week_start+604800).
-- mode: 2 = 2v2 (3 = 3v3 reserved, not active).
-- =====================================================================================

-- One row per finished match (queue and GM test matches alike; only source='queue' can score).
CREATE TABLE IF NOT EXISTS `need_bg2_match` (
  `match_pk` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `match_key` VARCHAR(48) NOT NULL,                  -- "<external id>-<server tick at launch>": unique per real match
  `ext_match_id` INT UNSIGNED NOT NULL,              -- the match id in the [NEED_BG2] logs
  `mode` TINYINT UNSIGNED NOT NULL DEFAULT 2,
  `source` VARCHAR(8) NOT NULL,                      -- 'queue' (public queue) / 'gm' (GM test, never scores)
  `started_at` INT UNSIGNED NOT NULL,
  `ended_at` INT UNSIGNED NOT NULL,
  `week_start` INT UNSIGNED NOT NULL,                -- week of ended_at
  `result` TINYINT UNSIGNED NOT NULL,                -- 0 draw, 1 blue, 2 red, 3 void, 4 cancelled
  `end_reason` VARCHAR(32) NOT NULL,
  `engaged` TINYINT UNSIGNED NOT NULL DEFAULT 0,     -- at least one real hostile interaction between the teams
  `hostile_total` INT UNSIGNED NOT NULL DEFAULT 0,
  `rounds` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `active_ms` INT UNSIGNED NOT NULL DEFAULT 0,       -- sum of round active time
  `win_b` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `win_r` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `valid` TINYINT UNSIGNED NOT NULL DEFAULT 0,       -- 1 = at least one participant scores
  `account_key` VARCHAR(64) NOT NULL DEFAULT '',     -- sorted account ids (repeat-pairing signal)
  `review` TINYINT UNSIGNED NOT NULL DEFAULT 0,      -- 0 none (or hold released), 1 HOLD, 2 approved, 3 voided by GM
  `signals` VARCHAR(255) NOT NULL DEFAULT '',             -- ASCII signal codes
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`match_pk`),
  UNIQUE KEY `uk_match_key` (`match_key`),
  KEY `k_week` (`mode`, `week_start`, `review`),
  KEY `k_accounts` (`account_key`, `ended_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Exactly 4 rows per match: the individual settlement.
CREATE TABLE IF NOT EXISTS `need_bg2_match_player` (
  `match_pk` BIGINT UNSIGNED NOT NULL,
  `account_id` INT UNSIGNED NOT NULL,
  `char_id` INT UNSIGNED NOT NULL,
  `team` TINYINT UNSIGNED NOT NULL,                  -- 1 blue, 2 red
  `class` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `outcome` TINYINT UNSIGNED NOT NULL DEFAULT 0,     -- 0 none, 1 win, 2 loss, 3 draw
  `deserter` TINYINT UNSIGNED NOT NULL DEFAULT 0,    -- 1 = confirmed voluntary departure after the reconnect grace
  `points` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `valid` TINYINT UNSIGNED NOT NULL DEFAULT 0,       -- counts as a valid match for this player
  `win` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `hostile` INT UNSIGNED NOT NULL DEFAULT 0,         -- damage / debuffs dealt to the other team
  `hit` INT UNSIGNED NOT NULL DEFAULT 0,             -- taken from the other team
  `support` INT UNSIGNED NOT NULL DEFAULT 0,         -- skills on a team mate
  PRIMARY KEY (`match_pk`, `char_id`),
  UNIQUE KEY `uk_match_account` (`match_pk`, `account_id`),
  KEY `k_account` (`account_id`),
  KEY `k_char` (`char_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Cash reward tickets (tier 10/30/60 and weekly rank 1..3), claimed at the reward NPC.
-- status: 0 PENDING (claimable), 1 RESERVED (paid in this session, waiting for the receipt
-- check), 2 CLAIMED, 8 REVOKED (never paid, GM decision), 9 MANUAL (paid but later voided:
-- operator settles by hand, never negative-charged automatically).
CREATE TABLE IF NOT EXISTS `need_bg2_reward` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `mode` TINYINT UNSIGNED NOT NULL DEFAULT 2,
  `week_start` INT UNSIGNED NOT NULL,
  `account_id` INT UNSIGNED NOT NULL,
  `reward_kind` VARCHAR(8) NOT NULL,                 -- 'TIER' / 'RANK'
  `reward_key` VARCHAR(8) NOT NULL,                  -- '10','30','60' / '1','2','3'
  `rank_slot` TINYINT UNSIGNED NULL DEFAULT NULL,    -- RANK only: one account per rank per week
  `char_id` INT UNSIGNED NOT NULL DEFAULT 0,         -- RANK: the ranked character
  `amount` INT UNSIGNED NOT NULL,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` INT UNSIGNED NOT NULL,
  `claim_seq` INT UNSIGNED NOT NULL DEFAULT 0,       -- per-account payment sequence (receipt #NEED_BG2_PAIDSEQ)
  `claim_char` INT UNSIGNED NOT NULL DEFAULT 0,
  `claimed_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `note` VARBINARY(160) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_ticket` (`mode`, `week_start`, `account_id`, `reward_kind`, `reward_key`),
  UNIQUE KEY `uk_rank_slot` (`mode`, `week_start`, `rank_slot`),
  KEY `k_account_status` (`account_id`, `status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Weekly ranking state per mode and week.
CREATE TABLE IF NOT EXISTS `need_bg2_week` (
  `mode` TINYINT UNSIGNED NOT NULL,
  `week_start` INT UNSIGNED NOT NULL,
  `state` VARCHAR(10) NOT NULL DEFAULT 'OPEN',       -- OPEN / HOLD / FINALIZED / NO_AWARD (fewer than 6 accounts)
  `accounts` INT UNSIGNED NOT NULL DEFAULT 0,
  `detail` VARBINARY(255) NOT NULL DEFAULT '',
  `attempts` INT UNSIGNED NOT NULL DEFAULT 0,
  `decided_at` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`mode`, `week_start`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Confirmed desertions (after the 15 s reconnect grace). Sanctions use the last 24 h.
CREATE TABLE IF NOT EXISTS `need_bg2_desertion` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id` INT UNSIGNED NOT NULL,
  `char_id` INT UNSIGNED NOT NULL,
  `match_pk` BIGINT UNSIGNED NOT NULL,
  `at` INT UNSIGNED NOT NULL,
  `kind` VARCHAR(16) NOT NULL,                       -- forfeit / both_left / no_combat
  `exempt` TINYINT UNSIGNED NOT NULL DEFAULT 0,      -- 1 = cancelled by a GM (server fault evidence)
  `exempt_by` INT UNSIGNED NOT NULL DEFAULT 0,
  `exempt_reason` VARBINARY(160) NOT NULL DEFAULT '',
  `exempt_at` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_match_account` (`match_pk`, `account_id`),
  KEY `k_account_at` (`account_id`, `at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Reviews of high-risk matches (several independent signals) and their GM decisions.
CREATE TABLE IF NOT EXISTS `need_bg2_review` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `match_pk` BIGINT UNSIGNED NOT NULL,
  `signals` VARCHAR(255) NOT NULL,
  `state` VARCHAR(10) NOT NULL DEFAULT 'HOLD',       -- HOLD / APPROVED / RELEASED / VOIDED
  `gm_aid` INT UNSIGNED NOT NULL DEFAULT 0,
  `reason` VARBINARY(160) NOT NULL DEFAULT '',
  `created_at` INT UNSIGNED NOT NULL,
  `decided_at` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_match` (`match_pk`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Every GM action and every automatic decision that changes the ledger.
CREATE TABLE IF NOT EXISTS `need_bg2_audit` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `at` INT UNSIGNED NOT NULL,
  `actor_aid` INT UNSIGNED NOT NULL DEFAULT 0,       -- 0 = server
  `action` VARCHAR(24) NOT NULL,
  `target` VARCHAR(64) NOT NULL DEFAULT '',
  `detail` VARBINARY(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  KEY `k_at` (`at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

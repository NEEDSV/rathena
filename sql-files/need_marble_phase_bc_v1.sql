-- NEED marble board Phase B/C ledger - schema v1 (DRAFT, NOT APPLIED)
--
-- Branch  : feature/need-marble-phase-bc-v1
-- Design  : NEED_MARBLE_DICE_LEDGER_DESIGN_v0_5 (+ INITIAL_BALANCE_V1_0)
-- Status  : migration file only. Do not import into the live `ragnarok` database or any
--           shared database before the operator approves it (G2 isolated test first).
-- Engine  : InnoDB only. Every key is numeric (the default collation is case/accent
--           insensitive, so text keys are avoided on purpose).
-- Rollback: need_marble_phase_bc_v1_revert.sql (drops only the tables created here).
--
-- Every table is new. No existing NEED / rAthena table is altered.

CREATE TABLE IF NOT EXISTS `need_marble_season` (
  `season_id` INT UNSIGNED NOT NULL,
  `status` TINYINT UNSIGNED NOT NULL,              -- 1 OPEN 2 SETTLING 3 CLOSED
  `starts_at` DATETIME NOT NULL,
  `note` VARCHAR(160) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  PRIMARY KEY (`season_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_policy` (
  `policy_key` SMALLINT UNSIGNED NOT NULL,
  `effective_day` INT UNSIGNED NOT NULL,           -- first logical day (YYYYMMDD, 04:00 KST) it applies to
  `value` INT NOT NULL,
  `changed_by` VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `changed_at` DATETIME NOT NULL,
  PRIMARY KEY (`policy_key`, `effective_day`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_boot` (
  `boot_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `started_at` DATETIME NOT NULL,
  PRIMARY KEY (`boot_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_account_seq` (
  `account_id` INT UNSIGNED NOT NULL,
  `collect_session` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `exchange_req` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `cp_payout` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_wallet` (
  `account_id` INT UNSIGNED NOT NULL,
  `season_id` INT UNSIGNED NOT NULL,
  `score` INT UNSIGNED NOT NULL DEFAULT 0,
  `dice` INT UNSIGNED NOT NULL DEFAULT 0,
  `pending_ack` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `locked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `lock_reason` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` DATETIME NOT NULL,
  PRIMARY KEY (`account_id`, `season_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_wallet_tx` (
  `account_id` INT UNSIGNED NOT NULL,
  `season_id` INT UNSIGNED NOT NULL,
  `kind` TINYINT UNSIGNED NOT NULL,                -- 1 EARN 2 EXCHANGE 3 SPEND 4 REFUND 5 ADJUST
  `ref_type` TINYINT UNSIGNED NOT NULL,            -- 1 ACTIVITY 2 EXCHANGE_REQ 3 ROLL 5 OPERATOR
  `ref_a` BIGINT UNSIGNED NOT NULL,
  `ref_b` BIGINT UNSIGNED NOT NULL,
  `ref_c` BIGINT UNSIGNED NOT NULL,
  `ref_d` BIGINT UNSIGNED NOT NULL,
  `char_id` INT UNSIGNED NOT NULL,
  `d_score` INT NOT NULL,
  `d_dice` INT NOT NULL,
  `period_day` INT UNSIGNED NOT NULL,
  `created_at` DATETIME NOT NULL,
  PRIMARY KEY (`account_id`, `season_id`, `kind`, `ref_type`, `ref_a`, `ref_b`, `ref_c`, `ref_d`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_period` (
  `account_id` INT UNSIGNED NOT NULL,
  `season_id` INT UNSIGNED NOT NULL,
  `period_day` INT UNSIGNED NOT NULL,
  `activity` TINYINT UNSIGNED NOT NULL,            -- 0 total 1 HUNT 2 INSTANCE 3 BG
  `earned` INT UNSIGNED NOT NULL DEFAULT 0,        -- EARN credited only (caps never reopen)
  `exchanged` INT UNSIGNED NOT NULL DEFAULT 0,     -- dice bought that day (activity 0 row only)
  PRIMARY KEY (`account_id`, `season_id`, `period_day`, `activity`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_activity` (
  `account_id` INT UNSIGNED NOT NULL,
  `season_id` INT UNSIGNED NOT NULL,
  `activity` TINYINT UNSIGNED NOT NULL,            -- 1 HUNT_MILESTONE 2 INSTANCE_CLEAR 3 BG_MATCH
  `key_a` BIGINT UNSIGNED NOT NULL,                -- HUNT period_day / INSTANCE boot_id / BG boot_id
  `key_b` BIGINT UNSIGNED NOT NULL,                -- HUNT tier      / INSTANCE instance_id / BG match token
  `key_c` BIGINT UNSIGNED NOT NULL,                -- HUNT 0         / INSTANCE dungeon_code / BG type code
  `char_id` INT UNSIGNED NOT NULL,
  `period_day` INT UNSIGNED NOT NULL,
  `requested` INT UNSIGNED NOT NULL,
  `credited` INT UNSIGNED NOT NULL,
  `outcome` TINYINT UNSIGNED NOT NULL,             -- 0 pending 1 CREDITED 2 CAPPED 3 DAILY_DUNGEON_USED
  `created_at` DATETIME NOT NULL,
  PRIMARY KEY (`account_id`, `season_id`, `activity`, `key_a`, `key_b`, `key_c`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_dungeon_daily` (
  `account_id` INT UNSIGNED NOT NULL,
  `season_id` INT UNSIGNED NOT NULL,
  `period_day` INT UNSIGNED NOT NULL,
  `dungeon_code` SMALLINT UNSIGNED NOT NULL,
  `boot_id` INT UNSIGNED NOT NULL,
  `instance_id` INT UNSIGNED NOT NULL,
  `created_at` DATETIME NOT NULL,
  PRIMARY KEY (`account_id`, `season_id`, `period_day`, `dungeon_code`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_hunt_progress` (
  `account_id` INT UNSIGNED NOT NULL,
  `season_id` INT UNSIGNED NOT NULL,
  `period_day` INT UNSIGNED NOT NULL,
  `kills` INT UNSIGNED NOT NULL DEFAULT 0,
  `next_tier` SMALLINT UNSIGNED NOT NULL DEFAULT 1, -- 1..max_tiers+1, never past it
  `updated_at` DATETIME NOT NULL,
  PRIMARY KEY (`account_id`, `season_id`, `period_day`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_hunt_batch` (
  `account_id` INT UNSIGNED NOT NULL,
  `collect_session` BIGINT UNSIGNED NOT NULL,      -- issued per login, independent from the board run_session
  `batch_no` INT UNSIGNED NOT NULL,
  `period_day` INT UNSIGNED NOT NULL,              -- part of the key: one batch may span a day boundary
  `season_id` INT UNSIGNED NOT NULL,
  `kills` INT UNSIGNED NOT NULL,
  `char_id` INT UNSIGNED NOT NULL,
  `created_at` DATETIME NOT NULL,
  PRIMARY KEY (`account_id`, `collect_session`, `batch_no`, `period_day`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_exchange_req` (
  `account_id` INT UNSIGNED NOT NULL,
  `request_no` BIGINT UNSIGNED NOT NULL,
  `season_id` INT UNSIGNED NOT NULL,
  `bundles` INT UNSIGNED NOT NULL,
  `cost` INT UNSIGNED NOT NULL,
  `status` TINYINT UNSIGNED NOT NULL,              -- 1 ISSUED 2 COMMITTED 3 REJECTED 4 EXPIRED/CANCELLED
  `reject_code` TINYINT UNSIGNED NOT NULL DEFAULT 0, -- 1 score 2 hold limit 3 locked 5 daily exchange limit
  `acked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `char_id` INT UNSIGNED NOT NULL,
  `issued_at` DATETIME NOT NULL,
  `expires_at` DATETIME NOT NULL,
  `resolved_at` DATETIME NULL,
  PRIMARY KEY (`account_id`, `request_no`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_state` (
  `account_id` INT UNSIGNED NOT NULL,
  `season_id` INT UNSIGNED NOT NULL,
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 0,     -- 0 IDLE 1 MOVING 2 PAUSED 3 RECOVERY 4 CHOICE_PENDING
  `pos` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `left_steps` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `die` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `fail_reason` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `roll_seq` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `step_seq` BIGINT UNSIGNED NOT NULL DEFAULT 0,   -- walked steps only (warps excluded)
  `run_session` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `run_char_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `jump_seq` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `start_pass` INT UNSIGNED NOT NULL DEFAULT 0,
  `laps` INT UNSIGNED NOT NULL DEFAULT 0,          -- 33-step bonuses decided (L1)
  `updated_at` DATETIME NOT NULL,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_roll` (
  `account_id` INT UNSIGNED NOT NULL,
  `roll_seq` BIGINT UNSIGNED NOT NULL,
  `season_id` INT UNSIGNED NOT NULL,
  `char_id` INT UNSIGNED NOT NULL,
  `run_session` BIGINT UNSIGNED NOT NULL,
  `die` TINYINT UNSIGNED NOT NULL,
  `start_pos` TINYINT UNSIGNED NOT NULL,
  `forced` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` DATETIME NOT NULL,
  PRIMARY KEY (`account_id`, `roll_seq`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_jump` (
  `account_id` INT UNSIGNED NOT NULL,
  `jump_seq` BIGINT UNSIGNED NOT NULL,
  `kind` TINYINT UNSIGNED NOT NULL,                -- 1 WARP
  `roll_seq` BIGINT UNSIGNED NOT NULL,
  `from_pos` TINYINT UNSIGNED NOT NULL,
  `to_pos` TINYINT UNSIGNED NOT NULL,
  `dropped_steps` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `start_pass_credit` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` DATETIME NOT NULL,
  PRIMARY KEY (`account_id`, `jump_seq`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_event` (
  `account_id` INT UNSIGNED NOT NULL,
  `roll_seq` BIGINT UNSIGNED NOT NULL,
  `hop` TINYINT UNSIGNED NOT NULL DEFAULT 0,       -- W1: always 0
  `season_id` INT UNSIGNED NOT NULL,
  `tile` TINYINT UNSIGNED NOT NULL,
  `tile_kind` TINYINT UNSIGNED NOT NULL,
  `char_id` INT UNSIGNED NOT NULL,
  `rng_value` INT UNSIGNED NOT NULL DEFAULT 0,     -- 1..10000, drawn once
  `choice` TINYINT UNSIGNED NOT NULL DEFAULT 0,    -- dungeon: 0 pending 1 safe 2 challenge
  `auto_choice` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `result_code` SMALLINT UNSIGNED NOT NULL,        -- 0 PENDING_CHOICE, else outcome slot + 1 (warp 100)
  `cp_amount` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` DATETIME NOT NULL,
  `resolved_at` DATETIME NULL,
  PRIMARY KEY (`account_id`, `roll_seq`, `hop`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_cp_payout` (
  `account_id` INT UNSIGNED NOT NULL,
  `src_type` TINYINT UNSIGNED NOT NULL,            -- 1 TILE 2 LAP
  `src_a` BIGINT UNSIGNED NOT NULL,                -- TILE roll_seq / LAP lap_no
  `src_b` BIGINT UNSIGNED NOT NULL,                -- TILE hop / LAP 0
  `payout_no` BIGINT UNSIGNED NOT NULL,
  `amount` INT UNSIGNED NOT NULL,
  `granted` INT UNSIGNED NOT NULL DEFAULT 0,
  `status` TINYINT UNSIGNED NOT NULL,              -- 1 DECIDED (pending) 2 APPLIED 3 UNCERTAIN 4 MANUAL_APPLIED 5 MANUAL_VOID
  `char_id` INT UNSIGNED NOT NULL,
  `decided_at` DATETIME NOT NULL,
  `applied_at` DATETIME NULL,
  `operator_ticket` BIGINT UNSIGNED NULL,
  PRIMARY KEY (`account_id`, `src_type`, `src_a`, `src_b`),
  UNIQUE KEY `uk_payout_no` (`account_id`, `payout_no`)
) ENGINE=InnoDB;

-- Operating season. The 14 day review is not a season end (INITIAL_BALANCE_V1_0 sec.4).
INSERT IGNORE INTO `need_marble_season` (`season_id`, `status`, `starts_at`, `note`)
VALUES (1, 1, NOW(), 'v1 operating season');

-- Master switch stays OFF. Turning it on is a separate operator decision:
--   INSERT INTO need_marble_policy (policy_key, effective_day, value, changed_by, changed_at)
--   VALUES (1, <next logical day YYYYMMDD>, 1, '<operator>', NOW());
INSERT IGNORE INTO `need_marble_policy` (`policy_key`, `effective_day`, `value`, `changed_by`, `changed_at`)
VALUES (1, 0, 0, 'migration', NOW());

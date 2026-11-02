-- NEED 2:2 Duo Survival - Phase 3 reward refactor (2026-10-29)
-- NOT APPLIED TO ANY LIVE DATABASE. Run on the isolated test schema only until the operator approves.
-- Needs: NEED_Duo_Survival_Phase3_DB_Migration.sql (need_bg2_reward etc.) already applied.
-- Every statement is CREATE TABLE IF NOT EXISTS: running it twice changes nothing. No existing table or row is altered.
--
-- need_bg2_reward is reused as is: new reward_kind values
--   'MCASH' match direct cash (reward_key = match_pk), 'MITEM' match items (reward_key = match_pk),
--   'TIERI' weekly tier items (reward_key '10'/'30'/'60'), 'RANKI' weekly rank items (reward_key '1'/'2'/'3', rank_slot set).
-- new status value 7 = HELD (match under review: not claimable until the GM approves; void -> 8).

-- Item list snapshot of an item reward ticket (the list at issue time; later rule changes do not touch it).
CREATE TABLE IF NOT EXISTS `need_bg2_reward_item` (
  `reward_id` BIGINT UNSIGNED NOT NULL,          -- need_bg2_reward.id
  `seq` TINYINT UNSIGNED NOT NULL,               -- 0..7
  `item_id` INT UNSIGNED NOT NULL,
  `amount` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`reward_id`, `seq`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Match direct cash granted per launch IP and KST day (04:00 KST .. next 04:00, day_key = (ended_at + 18000) DIV 86400).
CREATE TABLE IF NOT EXISTS `need_bg2_ipcash_day` (
  `ip` VARCHAR(45) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `day_key` INT UNSIGNED NOT NULL,
  `used` INT UNSIGNED NOT NULL DEFAULT 0,        -- sum of active + held allocations (released ones are given back)
  `updated_at` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`ip`, `day_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- One row per match and account: what was asked and what the IP cap allowed (idempotent per match).
CREATE TABLE IF NOT EXISTS `need_bg2_ipcash_alloc` (
  `match_pk` BIGINT UNSIGNED NOT NULL,
  `account_id` INT UNSIGNED NOT NULL,
  `char_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `ip` VARCHAR(45) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL DEFAULT '',
  `day_key` INT UNSIGNED NOT NULL,
  `requested` INT UNSIGNED NOT NULL,
  `allocated` INT UNSIGNED NOT NULL,
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 0,  -- 0 granted, 1 held (review), 3 released (void / revoke), 4 no launch IP (cash withheld)
  `created_at` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`match_pk`, `account_id`),
  KEY `k_ip_day` (`ip`, `day_key`),
  KEY `k_account_day` (`account_id`, `day_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Completion mark written as the LAST statement of every settlement transaction (ISSUE-01 follow-up for reward matches):
-- if a script stops inside the transaction and a later START TRANSACTION commits what was there, the mark is missing,
-- so a retry of a match with match rewards is reported as P3_PARTIAL rewards_incomplete instead of a clean duplicate.
CREATE TABLE IF NOT EXISTS `need_bg2_match_done` (
  `match_pk` BIGINT UNSIGNED NOT NULL,
  `done_at` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`match_pk`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Check
-- SHOW CREATE TABLE need_bg2_reward_item; SHOW CREATE TABLE need_bg2_ipcash_day; SHOW CREATE TABLE need_bg2_ipcash_alloc; SHOW CREATE TABLE need_bg2_match_done;
-- Roll back (only while no MCASH / MITEM / TIERI / RANKI ticket exists):
-- DROP TABLE need_bg2_match_done, need_bg2_ipcash_alloc, need_bg2_ipcash_day, need_bg2_reward_item;

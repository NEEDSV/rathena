-- NEED 2026 halloween event - Jack Rush (60 second ranking mode) tables, event_id 202610.
--
-- Import into the main map-server database BEFORE the jack rush scripts are loaded.
-- The scripts check that these three tables exist at start-up (information_schema) and keep
-- Jack Rush closed when one is missing, because query_sql on a missing table ends the script.
-- CREATE TABLE IF NOT EXISTS only: safe to run more than once, never drops or empties anything.
--
-- week_key = YYYYMMDD of the Wednesday 04:00 that opens the ranking week
--            (logical date = local time - 4 h, the same 04:00 rule as the daily jack ledger).

-- Weekly best per ACCOUNT (one row per week and account). The character that set the best is
-- kept for display. Order: score DESC, achieved_at ASC (first to reach it), account_id ASC.
CREATE TABLE IF NOT EXISTS `need_halloween_jack_rush_rank` (
  `event_id` int unsigned NOT NULL,
  `week_key` int unsigned NOT NULL,
  `account_id` int unsigned NOT NULL,
  `char_id` int unsigned NOT NULL,
  `char_name` varchar(30) NOT NULL DEFAULT '',
  `score` int unsigned NOT NULL,
  `normal_count` int unsigned NOT NULL DEFAULT 0,
  `gold_count` int unsigned NOT NULL DEFAULT 0,
  `achieved_at` datetime(3) NOT NULL,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`event_id`, `week_key`, `account_id`),
  KEY `week_rank` (`event_id`, `week_key`, `score`, `achieved_at`)
) ENGINE=InnoDB;

-- Ranking rewards made by the weekly settlement (TOP 3 = 100 / 70 / 50 coins), claimed in the
-- Event Hub. claimed_at NULL = pending. claim_token marks the rows one claim took (atomic claim).
-- UNIQUE (week, account) and (week, rank) make a repeated settlement a no-op.
CREATE TABLE IF NOT EXISTS `need_halloween_jack_rush_reward` (
  `event_id` int unsigned NOT NULL,
  `week_key` int unsigned NOT NULL,
  `account_id` int unsigned NOT NULL,
  `rank_no` tinyint unsigned NOT NULL,
  `char_id` int unsigned NOT NULL,
  `char_name` varchar(30) NOT NULL DEFAULT '',
  `score` int unsigned NOT NULL,
  `reward_item_id` int unsigned NOT NULL DEFAULT 6658,
  `reward_coin` int unsigned NOT NULL,
  `settled_at` datetime NOT NULL,
  `claimed_at` datetime DEFAULT NULL,
  `claim_char_id` int unsigned NOT NULL DEFAULT 0,
  `claim_token` varchar(32) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL DEFAULT '',
  PRIMARY KEY (`event_id`, `week_key`, `account_id`),
  UNIQUE KEY `week_rank_no` (`event_id`, `week_key`, `rank_no`),
  KEY `account_pending` (`account_id`, `claimed_at`),
  KEY `claim_token` (`claim_token`)
) ENGINE=InnoDB;

-- One row per settled week (weekly Wednesday 04:05 run, catch-up after downtime, or the final
-- settlement at the end of the event). A week with a row here is never settled again.
CREATE TABLE IF NOT EXISTS `need_halloween_jack_rush_settle` (
  `event_id` int unsigned NOT NULL,
  `week_key` int unsigned NOT NULL,
  `accounts` int unsigned NOT NULL DEFAULT 0 COMMENT 'ranked accounts (score > 0)',
  `rewarded` tinyint unsigned NOT NULL DEFAULT 0,
  `is_final` tinyint unsigned NOT NULL DEFAULT 0 COMMENT '1 = end-of-event settlement',
  `settled_at` datetime NOT NULL,
  PRIMARY KEY (`event_id`, `week_key`)
) ENGINE=InnoDB;

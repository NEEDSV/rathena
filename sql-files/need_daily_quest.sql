-- NEED daily quest (phase 1): character progress, account day ledger, once-only stages.
-- Import into the main map-server database before enabling need_daily_quest_enable.
-- Day keys are the 04:00 logical date (host local time - 4 hours).
-- Tracks are the lower bound of the level range: 100 / 130 / 150 / 175.
-- Reward claims (dq_claim, dq_ip_owner) come in a later phase.

-- A. One row per character, logical day and activity ('delivery'; 'hunt' later).
CREATE TABLE IF NOT EXISTS `need_dq_char_progress` (
  `day_key` date NOT NULL,
  `char_id` int unsigned NOT NULL,
  `activity` varchar(8) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `account_id` int unsigned NOT NULL,
  `track` smallint unsigned NOT NULL COMMENT 'level range snapshot at accept',
  `map_name` varchar(16) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL DEFAULT '',
  `accepted_at` datetime DEFAULT NULL,
  `collected` smallint unsigned NOT NULL DEFAULT 0 COMMENT 'collected, not yet delivered',
  `submitted` smallint unsigned NOT NULL DEFAULT 0 COMMENT 'delivered by this character today',
  `count` int unsigned NOT NULL DEFAULT 0 COMMENT 'hunt kills (later phase)',
  `reaccept_count` tinyint unsigned NOT NULL DEFAULT 0,
  `abandoned_collected` smallint unsigned NOT NULL DEFAULT 0 COMMENT 'dropped by a level-range re-accept',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`day_key`,`char_id`,`activity`),
  KEY `day_account` (`day_key`,`account_id`)
) ENGINE=InnoDB;

-- B. One row per account and logical day. Activity points are recomputed from
-- need_dq_score_stage on every commit, never incremented.
CREATE TABLE IF NOT EXISTS `need_dq_account_day` (
  `day_key` date NOT NULL,
  `account_id` int unsigned NOT NULL,
  `locked_track` smallint unsigned DEFAULT NULL COMMENT 'real level range of the first score',
  `locked_char_id` int unsigned DEFAULT NULL,
  `locked_at` datetime DEFAULT NULL,
  `hunt_pts` tinyint unsigned NOT NULL DEFAULT 0,
  `delivery_pts` tinyint unsigned NOT NULL DEFAULT 0,
  `bg_pts` tinyint unsigned NOT NULL DEFAULT 0,
  `inst_pts` tinyint unsigned NOT NULL DEFAULT 0,
  `total_pts` tinyint unsigned NOT NULL DEFAULT 0 COMMENT 'min(80, sum)',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`day_key`,`account_id`)
) ENGINE=InnoDB;

-- B'. A stage scores once per account and day (d1/d2/d3, h1..h5, bg1/bg2, inst_*).
CREATE TABLE IF NOT EXISTS `need_dq_score_stage` (
  `day_key` date NOT NULL,
  `account_id` int unsigned NOT NULL,
  `activity` varchar(8) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `stage_key` varchar(16) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `char_id` int unsigned NOT NULL,
  `track` smallint unsigned NOT NULL COMMENT 'real level range at commit',
  `base_level` smallint unsigned NOT NULL COMMENT 'base level at commit',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`day_key`,`account_id`,`activity`,`stage_key`),
  KEY `day_char` (`day_key`,`char_id`)
) ENGINE=InnoDB;

-- E. Battleground games (phase 3). One row per account and match: a match credits an account once.
-- valid = 1 counts as a game; valid = 0 keeps the refused game and its reason for audit.
CREATE TABLE IF NOT EXISTS `need_dq_bg_match` (
  `day_key` date NOT NULL,
  `account_id` int unsigned NOT NULL,
  `match_key` varchar(40) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL COMMENT 'type-unixstart, set at the battle start',
  `char_id` int unsigned NOT NULL,
  `bg_type` varchar(8) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `track` smallint unsigned NOT NULL,
  `started_at` datetime NOT NULL,
  `duration_s` int unsigned NOT NULL DEFAULT 0,
  `participants` smallint unsigned NOT NULL DEFAULT 0,
  `distinct_ips` smallint unsigned NOT NULL DEFAULT 0,
  `valid` tinyint unsigned NOT NULL DEFAULT 0,
  `eligible` tinyint unsigned NOT NULL DEFAULT 0 COMMENT 'valid and scorable by the account in this range (D1)',
  `reason` tinyint unsigned NOT NULL DEFAULT 0 COMMENT '0 valid, 1 not a participant, 2 not on the map, 3 too short, 4 no action, 5 shared IPs',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`day_key`,`account_id`,`match_key`),
  KEY `day_char` (`day_key`,`char_id`)
) ENGINE=InnoDB;

-- F. Instance clears (phase 4): one row per character, kind and day, kept even when it scores nothing.
--    The account score is in B' (stage keys i1 OGH hard, i2 OGH, i3 Sky Fortress), capped by the track's Cap.
CREATE TABLE IF NOT EXISTS `need_dq_inst_clear` (
  `day_key` date NOT NULL,
  `char_id` int unsigned NOT NULL,
  `kind` tinyint unsigned NOT NULL COMMENT '1 OGH hard, 2 OGH, 3 Sky Fortress',
  `account_id` int unsigned NOT NULL,
  `track` smallint unsigned NOT NULL,
  `instance_id` int NOT NULL DEFAULT 0,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`day_key`,`char_id`,`kind`),
  KEY `day_account` (`day_key`,`account_id`)
) ENGINE=InnoDB;

-- G. Cash claims (v1.5 Phase 5-Lite): one row per account, logical day and tier (20/40/60/80).
--    status 0 = reserved (cash not confirmed: an operator checks it, never paid again automatically),
--           1 = granted, 2 = not granted (certain; the account may claim again).
CREATE TABLE IF NOT EXISTS `need_dq_claim` (
  `day_key` date NOT NULL,
  `account_id` int unsigned NOT NULL,
  `tier` tinyint unsigned NOT NULL,
  `char_id` int unsigned NOT NULL,
  `ip` varbinary(16) NOT NULL,
  `family_group_id` int unsigned NOT NULL DEFAULT 0,
  `amount` int unsigned NOT NULL,
  `status` tinyint unsigned NOT NULL DEFAULT 0,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`day_key`,`account_id`,`tier`)
) ENGINE=InnoDB;

-- H. First claiming account per IP and logical day (same rule as need_chuseok_honey_ip_daily):
--    another account on that IP may claim only as an operator approved family member.
CREATE TABLE IF NOT EXISTS `need_dq_ip_daily` (
  `day_key` date NOT NULL,
  `ip` varbinary(16) NOT NULL,
  `first_account_id` int unsigned NOT NULL,
  `first_char_id` int unsigned NOT NULL,
  `family_group_id` int unsigned NOT NULL DEFAULT 0,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`day_key`,`ip`)
) ENGINE=InnoDB;

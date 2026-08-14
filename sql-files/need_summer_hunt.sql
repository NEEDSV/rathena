-- NEED 2026 summer field hunting golden watermelon ledger.
-- Import into the main map-server database before enabling golden rewards.

CREATE TABLE IF NOT EXISTS `need_summer_hunt_golden_claim` (
  `claim_id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `event_id` int unsigned NOT NULL,
  `logical_date` date NOT NULL,
  `account_id` int unsigned NOT NULL,
  `char_id` int unsigned NOT NULL,
  `char_name` varchar(24) NOT NULL,
  `ip` varbinary(16) NOT NULL,
  `family_group_id` int unsigned NOT NULL DEFAULT 0,
  `family_exception` tinyint unsigned NOT NULL DEFAULT 0,
  `map_name` varchar(32) NOT NULL,
  `x` smallint unsigned NOT NULL DEFAULT 0,
  `y` smallint unsigned NOT NULL DEFAULT 0,
  `mob_id` int unsigned NOT NULL,
  `player_level` smallint unsigned NOT NULL,
  `mob_level` smallint unsigned NOT NULL,
  `status` tinyint unsigned NOT NULL DEFAULT 0 COMMENT '0=reserved,1=delivered',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  `delivered_at` datetime DEFAULT NULL,
  PRIMARY KEY (`claim_id`),
  UNIQUE KEY `event_date_account` (`event_id`,`logical_date`,`account_id`),
  KEY `event_date_ip` (`event_id`,`logical_date`,`ip`),
  KEY `status_created` (`status`,`created_at`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_summer_hunt_golden_ip_daily` (
  `event_id` int unsigned NOT NULL,
  `logical_date` date NOT NULL,
  `ip` varbinary(16) NOT NULL,
  `family_group_id` int unsigned NOT NULL DEFAULT 0,
  `first_account_id` int unsigned NOT NULL,
  `first_char_id` int unsigned NOT NULL,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`event_id`,`logical_date`,`ip`),
  KEY `first_account_id` (`first_account_id`),
  KEY `created_at` (`created_at`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_summer_hunt_golden_log` (
  `log_id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `claim_id` bigint unsigned NOT NULL DEFAULT 0,
  `event_id` int unsigned NOT NULL,
  `logical_date` date NOT NULL,
  `account_id` int unsigned NOT NULL,
  `char_id` int unsigned NOT NULL,
  `char_name` varchar(24) NOT NULL,
  `ip` varbinary(16) NOT NULL,
  `family_group_id` int unsigned NOT NULL DEFAULT 0,
  `family_exception` tinyint unsigned NOT NULL DEFAULT 0,
  `map_name` varchar(32) NOT NULL,
  `x` smallint unsigned NOT NULL DEFAULT 0,
  `y` smallint unsigned NOT NULL DEFAULT 0,
  `mob_id` int unsigned NOT NULL,
  `player_level` smallint unsigned NOT NULL,
  `mob_level` smallint unsigned NOT NULL,
  `result` varchar(16) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `failure_code` varchar(64) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL DEFAULT '',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`log_id`),
  KEY `claim_id` (`claim_id`),
  KEY `event_date_account` (`event_id`,`logical_date`,`account_id`),
  KEY `event_date_ip` (`event_id`,`logical_date`,`ip`),
  KEY `result_created` (`result`,`created_at`)
) ENGINE=InnoDB;

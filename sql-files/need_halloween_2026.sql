-- NEED 2026 halloween event - ledger tables.
--
-- The halloween event creates NO new ledger table. It reuses the chuseok tables
-- under its own event_id 202610:
--   need_chuseok_honey_claim / _ip_daily / _log   jack game daily reward (account + IP, 04:00)
--   need_chuseok_exchange_limit / _log             halloween exchange account limits
--
-- On the live database these tables already exist (chuseok 2026). This file only
-- creates them when they are missing, e.g. on a fresh test database. It never
-- drops or empties anything, so it is safe to run more than once.
--
-- WARNING: do NOT re-run sql-files/need_chuseok_exchange.sql while the halloween
-- event is live. That file starts with DROP TABLE need_chuseok_exchange_limit and
-- would erase the halloween (and chuseok) exchange limits.

CREATE TABLE IF NOT EXISTS `need_chuseok_honey_claim` (
  `claim_id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `event_id` int unsigned NOT NULL,
  `logical_date` date NOT NULL,
  `account_id` int unsigned NOT NULL,
  `char_id` int unsigned NOT NULL,
  `char_name` varchar(24) NOT NULL,
  `ip` varbinary(16) NOT NULL,
  `source` varchar(16) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL DEFAULT '',
  `family_group_id` int unsigned NOT NULL DEFAULT 0,
  `family_exception` tinyint unsigned NOT NULL DEFAULT 0,
  `map_name` varchar(32) NOT NULL DEFAULT '',
  `x` smallint unsigned NOT NULL DEFAULT 0,
  `y` smallint unsigned NOT NULL DEFAULT 0,
  `mob_id` int unsigned NOT NULL DEFAULT 0,
  `player_level` smallint unsigned NOT NULL DEFAULT 0,
  `status` tinyint unsigned NOT NULL DEFAULT 0 COMMENT '0=reserved,1=delivered',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  `delivered_at` datetime DEFAULT NULL,
  PRIMARY KEY (`claim_id`),
  UNIQUE KEY `event_date_account` (`event_id`,`logical_date`,`account_id`),
  KEY `event_date_ip` (`event_id`,`logical_date`,`ip`),
  KEY `status_created` (`status`,`created_at`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_chuseok_honey_ip_daily` (
  `event_id` int unsigned NOT NULL,
  `logical_date` date NOT NULL,
  `ip` varbinary(16) NOT NULL,
  `first_account_id` int unsigned NOT NULL,
  `first_char_id` int unsigned NOT NULL,
  `family_group_id` int unsigned NOT NULL DEFAULT 0,
  `source` varchar(16) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL DEFAULT '',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`event_id`,`logical_date`,`ip`),
  KEY `first_account_id` (`first_account_id`),
  KEY `created_at` (`created_at`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_chuseok_honey_log` (
  `log_id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `claim_id` bigint unsigned NOT NULL DEFAULT 0,
  `event_id` int unsigned NOT NULL,
  `logical_date` date NOT NULL,
  `account_id` int unsigned NOT NULL,
  `char_id` int unsigned NOT NULL,
  `char_name` varchar(24) NOT NULL,
  `ip` varbinary(16) NOT NULL,
  `source` varchar(16) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL DEFAULT '',
  `family_group_id` int unsigned NOT NULL DEFAULT 0,
  `family_exception` tinyint unsigned NOT NULL DEFAULT 0,
  `map_name` varchar(32) NOT NULL DEFAULT '',
  `x` smallint unsigned NOT NULL DEFAULT 0,
  `y` smallint unsigned NOT NULL DEFAULT 0,
  `mob_id` int unsigned NOT NULL DEFAULT 0,
  `player_level` smallint unsigned NOT NULL DEFAULT 0,
  `result` varchar(16) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `failure_code` varchar(64) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL DEFAULT '',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`log_id`),
  KEY `claim_id` (`claim_id`),
  KEY `event_date_account` (`event_id`,`logical_date`,`account_id`),
  KEY `event_date_ip` (`event_id`,`logical_date`,`ip`),
  KEY `result_created` (`result`,`created_at`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_chuseok_exchange_limit` (
  `event_id` int unsigned NOT NULL,
  `scope` varchar(8) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `subject` varchar(45) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `product` varchar(16) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `period_key` int unsigned NOT NULL DEFAULT 0,
  `used` int unsigned NOT NULL DEFAULT 0,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`event_id`,`scope`,`subject`,`product`,`period_key`),
  KEY `product_period` (`event_id`,`product`,`period_key`),
  KEY `scope_subject` (`scope`,`subject`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_chuseok_exchange_log` (
  `log_id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `event_id` int unsigned NOT NULL,
  `account_id` int unsigned NOT NULL,
  `char_id` int unsigned NOT NULL,
  `char_name` varchar(24) NOT NULL,
  `ip` varchar(45) NOT NULL DEFAULT '',
  `product` varchar(16) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `period_key` int unsigned NOT NULL DEFAULT 0,
  `family_group_id` int unsigned NOT NULL DEFAULT 0,
  `amount` int unsigned NOT NULL,
  `songpyun_spent` int unsigned NOT NULL COMMENT 'currency spent (halloween: coins)',
  `used_after` int unsigned NOT NULL COMMENT 'account side',
  `ip_used_after` int unsigned NOT NULL DEFAULT 0,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`log_id`),
  KEY `event_account` (`event_id`,`account_id`),
  KEY `event_ip` (`event_id`,`ip`),
  KEY `product_period` (`event_id`,`product`,`period_key`),
  KEY `created_at` (`created_at`)
) ENGINE=InnoDB;

ALTER TABLE `need_chuseok_exchange_log`
  ADD COLUMN IF NOT EXISTS `ip_used_after` int unsigned NOT NULL DEFAULT 0 AFTER `used_after`;

-- Check: all five tables must be InnoDB (expected result: 5).
SELECT COUNT(*) AS `innodb_tables` FROM `information_schema`.`TABLES`
WHERE `TABLE_SCHEMA` = DATABASE() AND `ENGINE` = 'InnoDB' AND `TABLE_NAME` IN (
  'need_chuseok_honey_claim', 'need_chuseok_honey_ip_daily', 'need_chuseok_honey_log',
  'need_chuseok_exchange_limit', 'need_chuseok_exchange_log');

-- NEED 2026 summer attendance ledger.
-- Keep this migration in sync with sql-files/need_summer_attendance.sql.

CREATE TABLE IF NOT EXISTS `need_summer_attendance_account` (
  `event_id` int unsigned NOT NULL,
  `account_id` int unsigned NOT NULL,
  `claimed_count` smallint unsigned NOT NULL DEFAULT 0,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`event_id`,`account_id`),
  KEY `updated_at` (`updated_at`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_summer_attendance_online` (
  `event_id` int unsigned NOT NULL,
  `logical_date` date NOT NULL,
  `account_id` int unsigned NOT NULL,
  `online_seconds` int unsigned NOT NULL DEFAULT 0,
  `last_char_id` int unsigned NOT NULL DEFAULT 0,
  `last_ip` varbinary(16) DEFAULT NULL,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`event_id`,`logical_date`,`account_id`),
  KEY `account_id` (`account_id`),
  KEY `updated_at` (`updated_at`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_summer_attendance_family_group` (
  `family_group_id` int unsigned NOT NULL AUTO_INCREMENT,
  `event_id` int unsigned NOT NULL,
  `group_name` varchar(64) NOT NULL,
  `active` tinyint unsigned NOT NULL DEFAULT 1,
  `approved_by` varchar(24) NOT NULL,
  `reason` varchar(255) NOT NULL DEFAULT '',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`family_group_id`),
  KEY `event_active` (`event_id`,`active`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_summer_attendance_family_member` (
  `event_id` int unsigned NOT NULL,
  `account_id` int unsigned NOT NULL,
  `family_group_id` int unsigned NOT NULL,
  `active` tinyint unsigned NOT NULL DEFAULT 1,
  `approved_by` varchar(24) NOT NULL,
  `reason` varchar(255) NOT NULL DEFAULT '',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`event_id`,`account_id`),
  KEY `event_group_active` (`event_id`,`family_group_id`,`active`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_summer_attendance_family_audit` (
  `audit_id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `event_id` int unsigned NOT NULL,
  `family_group_id` int unsigned NOT NULL DEFAULT 0,
  `account_id` int unsigned NOT NULL DEFAULT 0,
  `action` varchar(16) NOT NULL,
  `operator` varchar(24) NOT NULL,
  `reason` varchar(255) NOT NULL DEFAULT '',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`audit_id`),
  KEY `event_account` (`event_id`,`account_id`),
  KEY `created_at` (`created_at`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_summer_attendance_ip_daily` (
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

CREATE TABLE IF NOT EXISTS `need_summer_attendance_claim` (
  `claim_id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `event_id` int unsigned NOT NULL,
  `logical_date` date NOT NULL,
  `account_id` int unsigned NOT NULL,
  `char_id` int unsigned NOT NULL,
  `ip` varbinary(16) NOT NULL,
  `family_group_id` int unsigned NOT NULL DEFAULT 0,
  `claim_no` smallint unsigned NOT NULL,
  `online_seconds` int unsigned NOT NULL,
  `status` tinyint unsigned NOT NULL DEFAULT 0 COMMENT '0=reserved,1=delivered',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`claim_id`),
  UNIQUE KEY `event_date_account` (`event_id`,`logical_date`,`account_id`),
  UNIQUE KEY `event_account_claim` (`event_id`,`account_id`,`claim_no`),
  KEY `event_date_ip` (`event_id`,`logical_date`,`ip`),
  KEY `status_created` (`status`,`created_at`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_summer_attendance_reward_outbox` (
  `outbox_id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `claim_id` bigint unsigned NOT NULL,
  `event_id` int unsigned NOT NULL,
  `account_id` int unsigned NOT NULL,
  `char_id` int unsigned NOT NULL,
  `token_item_id` int unsigned NOT NULL,
  `token_amount` int unsigned NOT NULL,
  `box_item_id` int unsigned NOT NULL,
  `box_amount` int unsigned NOT NULL,
  `status` tinyint unsigned NOT NULL DEFAULT 0 COMMENT '0=pending,1=processing,2=delivered,3=retry,4=review',
  `attempts` smallint unsigned NOT NULL DEFAULT 0,
  `next_attempt_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `last_attempt_at` datetime DEFAULT NULL,
  `last_error_code` varchar(64) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL DEFAULT '',
  `last_error` varchar(255) NOT NULL DEFAULT '',
  `mail_id` bigint unsigned DEFAULT NULL,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  `delivered_at` datetime DEFAULT NULL,
  PRIMARY KEY (`outbox_id`),
  UNIQUE KEY `claim_id` (`claim_id`),
  UNIQUE KEY `mail_id` (`mail_id`),
  KEY `status_created` (`status`,`created_at`),
  KEY `event_account` (`event_id`,`account_id`)
) ENGINE=InnoDB;

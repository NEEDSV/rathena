-- NEED 2026 chuseok songpyun exchange - account AND ip level quotas.
-- Import into the main map-server database before enabling the exchange NPC.
--
-- The limited products are capped twice: once per account and once per client
-- IP, exactly like the honey songpyun daily ledger. A purchase only goes
-- through when BOTH sides still have room.
--
--   scope      : 'ACCOUNT' | 'IP'
--   subject    : account_id (as text) for ACCOUNT rows, dotted IP for IP rows
--   product    : 'DAECHUK' (Blacksmith_Blessing) | 'ARIA' (costume box)
--   period_key : weekly products use the YYYYMMDD of the last Saturday 22:00
--                reset, season products use 0
--
-- NOTE: this replaces the earlier account-only schema. The previous table had
-- `account_id` as part of the primary key and no `scope` / `subject` columns,
-- so it is dropped and rebuilt. Only do this before the event opens - once it
-- is live, migrate instead of dropping.

DROP TABLE IF EXISTS `need_chuseok_exchange_limit`;

CREATE TABLE `need_chuseok_exchange_limit` (
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
  `amount` int unsigned NOT NULL,
  `songpyun_spent` int unsigned NOT NULL,
  `used_after` int unsigned NOT NULL COMMENT 'account side',
  `ip_used_after` int unsigned NOT NULL DEFAULT 0,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`log_id`),
  KEY `event_account` (`event_id`,`account_id`),
  KEY `event_ip` (`event_id`,`ip`),
  KEY `product_period` (`event_id`,`product`,`period_key`),
  KEY `created_at` (`created_at`)
) ENGINE=InnoDB;

-- fix1 초기 스키마로 이미 임포트한 경우에만 필요합니다.
ALTER TABLE `need_chuseok_exchange_log`
  ADD COLUMN IF NOT EXISTS `ip_used_after` int unsigned NOT NULL DEFAULT 0 AFTER `used_after`;

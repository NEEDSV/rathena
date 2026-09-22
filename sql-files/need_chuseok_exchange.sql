-- NEED 2026 chuseok songpyun exchange - account level quotas.
-- Import into the main map-server database before enabling the exchange NPC.
--
-- `product`    : 'DAECHUK' (Blacksmith_Blessing) | 'ARIA' (costume box)
-- `period_key` : weekly products use the YYYYMMDD of the last Saturday 22:00
--                reset, season products use 0.

CREATE TABLE IF NOT EXISTS `need_chuseok_exchange_limit` (
  `event_id` int unsigned NOT NULL,
  `account_id` int unsigned NOT NULL,
  `product` varchar(16) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `period_key` int unsigned NOT NULL DEFAULT 0,
  `used` int unsigned NOT NULL DEFAULT 0,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`event_id`,`account_id`,`product`,`period_key`),
  KEY `product_period` (`event_id`,`product`,`period_key`)
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
  `used_after` int unsigned NOT NULL,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`log_id`),
  KEY `event_account` (`event_id`,`account_id`),
  KEY `product_period` (`event_id`,`product`,`period_key`),
  KEY `created_at` (`created_at`)
) ENGINE=InnoDB;

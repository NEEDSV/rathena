-- NEED: Memorial dungeon IP daily reward limit - per-IP effective limit overrides
CREATE TABLE IF NOT EXISTS `instance_ip_reward_override` (
  `ip` varbinary(16) NOT NULL,
  `instance_db_id` int unsigned NOT NULL DEFAULT 0,
  `daily_limit` smallint unsigned NOT NULL,
  `enabled` tinyint unsigned NOT NULL DEFAULT 1,
  `memo` varchar(255) NOT NULL DEFAULT '',
  `expires_at` datetime NULL DEFAULT NULL,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`ip`,`instance_db_id`),
  KEY `expires_at` (`expires_at`)
) ENGINE=InnoDB;

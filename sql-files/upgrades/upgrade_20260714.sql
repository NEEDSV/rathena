-- NEED: Memorial dungeon IP daily reward limit
CREATE TABLE IF NOT EXISTS `instance_ip_reward` (
  `reward_date` date NOT NULL,
  `instance_db_id` int unsigned NOT NULL,
  `ip` varbinary(16) NOT NULL,
  `use_count` smallint unsigned NOT NULL DEFAULT 0,
  `last_account_id` int unsigned NOT NULL DEFAULT 0,
  `last_char_id` int unsigned NOT NULL DEFAULT 0,
  `last_char_name` varchar(24) NOT NULL DEFAULT '',
  `last_used_at` datetime NOT NULL,
  `reward_type` tinyint unsigned NOT NULL DEFAULT 0,
  `runtime_instance_id` int NOT NULL DEFAULT 0,
  `monster_id` smallint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`reward_date`,`instance_db_id`,`ip`),
  KEY `last_used_at` (`last_used_at`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `instance_ip_reward_event` (
  `reward_date` date NOT NULL,
  `instance_db_id` int unsigned NOT NULL,
  `ip` varbinary(16) NOT NULL,
  `server_boot` bigint unsigned NOT NULL,
  `runtime_instance_id` int NOT NULL,
  `monster_gid` int NOT NULL,
  `monster_id` smallint unsigned NOT NULL,
  `reward_type` tinyint unsigned NOT NULL,
  `allowed` tinyint NOT NULL DEFAULT -1,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`reward_date`,`instance_db_id`,`ip`,`server_boot`,`runtime_instance_id`,`monster_gid`,`reward_type`),
  KEY `created_at` (`created_at`)
) ENGINE=InnoDB;

-- Optional housekeeping (run from the operator's scheduler):
-- DELETE FROM `instance_ip_reward` WHERE `reward_date` < CURDATE() - INTERVAL 90 DAY;
-- DELETE FROM `instance_ip_reward_event` WHERE `reward_date` < CURDATE() - INTERVAL 7 DAY;

-- NEED Jack Frost + Teleport pattern guard log table.
-- Import this into the main database configured by conf/inter_athena.conf.
-- Only state changes are stored (warning, captcha, captcha_success, penalty);
-- individual skill casts and teleports are never logged.

CREATE TABLE IF NOT EXISTS `need_jf_pattern_log` (
  `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
  `account_id` int(11) unsigned NOT NULL DEFAULT '0',
  `char_id` int(11) unsigned NOT NULL DEFAULT '0',
  `char_name` varchar(23) NOT NULL DEFAULT '',
  `map` varchar(24) NOT NULL DEFAULT '',
  `x` smallint(5) unsigned NOT NULL DEFAULT '0',
  `y` smallint(5) unsigned NOT NULL DEFAULT '0',
  `event` varchar(32) NOT NULL DEFAULT '',
  `pattern_count` int(11) unsigned NOT NULL DEFAULT '0',
  `suspicion_score` int(11) unsigned NOT NULL DEFAULT '0',
  `stage` smallint(5) unsigned NOT NULL DEFAULT '0',
  `penalty_count` int(11) unsigned NOT NULL DEFAULT '0',
  `penalty_duration` int(11) unsigned NOT NULL DEFAULT '0',
  `autoloot_blocked` tinyint(1) unsigned NOT NULL DEFAULT '0',
  `world_drop_blocked` tinyint(1) unsigned NOT NULL DEFAULT '0',
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  KEY `account_id` (`account_id`),
  KEY `char_id` (`char_id`),
  KEY `event` (`event`),
  KEY `created_at` (`created_at`)
) ENGINE=MyISAM;

-- Upgrade path for an installation created before the world drop restriction.
-- Safe to run repeatedly on MariaDB 10.0+; remove the IF NOT EXISTS on older MySQL.
ALTER TABLE `need_jf_pattern_log`
  ADD COLUMN IF NOT EXISTS `autoloot_blocked` tinyint(1) unsigned NOT NULL DEFAULT '0' AFTER `penalty_duration`,
  ADD COLUMN IF NOT EXISTS `world_drop_blocked` tinyint(1) unsigned NOT NULL DEFAULT '0' AFTER `autoloot_blocked`;

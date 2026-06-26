-- rAthena custom macro detector / CAPTCHA log table.
-- Import this into the main database configured by conf/inter_athena.conf.

CREATE TABLE IF NOT EXISTS `need_macro_detect_log` (
  `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
  `account_id` int(11) unsigned NOT NULL DEFAULT '0',
  `char_id` int(11) unsigned NOT NULL DEFAULT '0',
  `char_name` varchar(23) NOT NULL DEFAULT '',
  `map` varchar(24) NOT NULL DEFAULT '',
  `x` smallint(5) unsigned NOT NULL DEFAULT '0',
  `y` smallint(5) unsigned NOT NULL DEFAULT '0',
  `captcha_id` smallint(5) unsigned NOT NULL DEFAULT '0',
  `event` varchar(32) NOT NULL DEFAULT '',
  `retry_left` int(11) NOT NULL DEFAULT '0',
  `punishment` varchar(16) NOT NULL DEFAULT '',
  `punishment_time` int(11) unsigned NOT NULL DEFAULT '0',
  `reporter_aid` int(11) NOT NULL DEFAULT '-1',
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  KEY `account_id` (`account_id`),
  KEY `char_id` (`char_id`),
  KEY `event` (`event`),
  KEY `created_at` (`created_at`)
) ENGINE=MyISAM;

ALTER TABLE `need_macro_detect_log`
  MODIFY `event` varchar(32) NOT NULL DEFAULT '';

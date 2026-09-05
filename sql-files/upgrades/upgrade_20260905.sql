CREATE TABLE IF NOT EXISTS `needwiki_sessions` (
  `token_hash` char(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `code_hash` char(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `status` tinyint(3) unsigned NOT NULL DEFAULT '0',
  `account_id` int(11) unsigned NOT NULL DEFAULT '0',
  `char_id` int(10) unsigned NOT NULL DEFAULT '0',
  `session_generation` bigint(20) unsigned NOT NULL DEFAULT '0',
  `created_at` bigint(20) unsigned NOT NULL,
  `code_expires_at` bigint(20) unsigned NOT NULL,
  `expires_at` bigint(20) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`token_hash`),
  UNIQUE KEY `code_hash` (`code_hash`),
  KEY `character_session` (`account_id`,`char_id`,`session_generation`),
  KEY `expires_at` (`expires_at`),
  KEY `code_expires_at` (`code_expires_at`)
) ENGINE=InnoDB;

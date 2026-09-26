--
-- NEED Wiki article reports (POST /api/wiki/report, web-server).
-- Anonymous; limited to 5 reports per client IP per 10 minutes by the web-server.
-- `report_type` is the stable code (incorrect_info, missing_content, typo, other);
-- `report_type_raw` keeps the value the client sent (e.g. a legacy Korean label).
-- `message_hash` is the SHA-256 of the stored message, used to drop exact re-sends.
-- `status` is 0 (new) for now; there is no processing workflow yet.
-- The table is utf8mb4 on its own, whatever the database default is.
--
CREATE TABLE IF NOT EXISTS `needwiki_reports` (
  `id` int(11) unsigned NOT NULL AUTO_INCREMENT,
  `created_at` bigint(20) unsigned NOT NULL,
  `remote_ip` varchar(45) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `article_id` varchar(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `article_title` varchar(255) NOT NULL DEFAULT '',
  `report_type` varchar(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `report_type_raw` varchar(64) NOT NULL DEFAULT '',
  `message` text NOT NULL,
  `message_hash` char(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `status` tinyint(3) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  KEY `created_at` (`created_at`),
  KEY `remote_ip_created_at` (`remote_ip`,`created_at`),
  KEY `article_id` (`article_id`),
  KEY `report_type` (`report_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

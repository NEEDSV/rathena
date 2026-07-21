-- NEED equipment build sharing phase 2 supplement: likes and popularity sorting.
-- Apply after upgrade_20260721_2.sql. This file is intentionally not executed automatically.

ALTER TABLE `need_equipment_build`
  ADD COLUMN `like_count` int unsigned NOT NULL DEFAULT 0 AFTER `status`,
  DROP INDEX `idx_need_build_public_search`,
  ADD KEY `idx_need_build_public_likes` (`status`,`job_id`,`category`,`like_count`,`approved_at`,`build_id`),
  ADD KEY `idx_need_build_public_latest` (`status`,`job_id`,`category`,`approved_at`,`build_id`);

CREATE TABLE IF NOT EXISTS `need_equipment_build_like` (
  `like_id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `build_id` bigint unsigned NOT NULL,
  `account_id` int unsigned NOT NULL,
  `char_id` int unsigned NOT NULL,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`like_id`),
  UNIQUE KEY `uq_need_build_like_build_account` (`build_id`,`account_id`),
  KEY `idx_need_build_like_account` (`account_id`),
  CONSTRAINT `fk_need_build_like_build` FOREIGN KEY (`build_id`) REFERENCES `need_equipment_build` (`build_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=euckr COLLATE=euckr_korean_ci;

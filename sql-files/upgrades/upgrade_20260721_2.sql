-- NEED equipment build sharing phase 2.
-- Apply after upgrade_20260721.sql. This file is intentionally not executed automatically.

ALTER TABLE `need_equipment_build`
  ADD COLUMN `review_reason` varchar(200) NULL DEFAULT NULL AFTER `deleted_by`,
  ADD COLUMN `cancelled_at` datetime NULL DEFAULT NULL AFTER `delete_reason`,
  ADD KEY `idx_need_build_public_search` (`status`,`job_id`,`category`,`approved_at`,`build_id`);

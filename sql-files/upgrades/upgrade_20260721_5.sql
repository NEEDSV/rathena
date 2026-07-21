-- NEED equipment build sharing final stability indexes.
-- Apply after upgrade_20260721_4.sql. This file is intentionally not executed automatically.

ALTER TABLE `need_equipment_build`
  DROP INDEX `idx_need_build_reward_claim`,
  ADD KEY `idx_need_build_reward_claim` (`account_id`,`char_id`,`status`,`reward_eligible`,`reward_claimed`,`reward_claim_status`,`build_id`),
  ADD KEY `idx_need_build_owner_list` (`account_id`,`char_id`,`build_id`),
  ADD KEY `idx_need_build_admin_char_name` (`char_name`,`build_id`);

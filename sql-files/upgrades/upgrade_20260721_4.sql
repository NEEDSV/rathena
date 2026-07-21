-- NEED equipment build sharing phase 3: operator-assigned approval rewards.
-- Apply after upgrade_20260721_3.sql. This file is intentionally not executed automatically.

ALTER TABLE `need_equipment_build`
  ADD COLUMN `reward_item_id` int unsigned NOT NULL DEFAULT 0 AFTER `reward_eligible`,
  ADD COLUMN `reward_amount` int unsigned NOT NULL DEFAULT 0 AFTER `reward_item_id`,
  ADD COLUMN `reward_approved_by` int unsigned NULL DEFAULT NULL AFTER `reward_claimed_at`,
  ADD COLUMN `reward_claim_status` tinyint unsigned NOT NULL DEFAULT 0 COMMENT '0=unclaimed,1=processing,2=completed' AFTER `reward_approved_by`,
  ADD COLUMN `reward_claim_started_at` datetime NULL DEFAULT NULL AFTER `reward_claim_status`,
  ADD KEY `idx_need_build_reward_claim` (`char_id`,`status`,`reward_eligible`,`reward_claimed`,`reward_claim_status`,`build_id`);

-- Phase 2 marked approved builds reward-eligible without assigning an item or amount.
-- Normalize unclaimed legacy approvals to "approved without reward" so they cannot enter the claim flow accidentally.
UPDATE `need_equipment_build`
SET `reward_eligible`=0,
    `reward_item_id`=0,
    `reward_amount`=0,
    `reward_approved_by`=NULL,
    `reward_claim_status`=0,
    `reward_claim_started_at`=NULL
WHERE `status`=1 AND `reward_claimed`=0;

UPDATE `need_equipment_build`
SET `reward_claim_status`=2
WHERE `reward_claimed`=1;

ALTER TABLE `need_equipment_build_reward_log`
  ADD COLUMN `char_id` int unsigned NOT NULL DEFAULT 0 AFTER `account_id`,
  DROP INDEX `uq_need_build_reward_build_account`,
  ADD UNIQUE KEY `uq_need_build_reward_build` (`build_id`);

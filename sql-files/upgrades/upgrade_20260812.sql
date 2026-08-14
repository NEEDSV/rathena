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
-- NEED 2026 summer attendance milestone reward payload.
-- Apply with feature.need_summer_attendance off after taking a normal backup.

SET @need_summer_has_bonus_item_id = (
  SELECT COUNT(*) FROM `information_schema`.`COLUMNS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'need_summer_attendance_reward_outbox'
    AND `COLUMN_NAME` = 'bonus_item_id'
);
SET @need_summer_add_bonus_item_id = IF(
  @need_summer_has_bonus_item_id = 0,
  'ALTER TABLE `need_summer_attendance_reward_outbox` ADD COLUMN `bonus_item_id` int unsigned NOT NULL DEFAULT 0 AFTER `box_amount`',
  'SELECT ''need_summer_attendance_reward_outbox.bonus_item_id already exists'''
);
PREPARE need_summer_upgrade_statement FROM @need_summer_add_bonus_item_id;
EXECUTE need_summer_upgrade_statement;
DEALLOCATE PREPARE need_summer_upgrade_statement;

SET @need_summer_has_bonus_amount = (
  SELECT COUNT(*) FROM `information_schema`.`COLUMNS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'need_summer_attendance_reward_outbox'
    AND `COLUMN_NAME` = 'bonus_amount'
);
SET @need_summer_add_bonus_amount = IF(
  @need_summer_has_bonus_amount = 0,
  'ALTER TABLE `need_summer_attendance_reward_outbox` ADD COLUMN `bonus_amount` int unsigned NOT NULL DEFAULT 0 AFTER `bonus_item_id`',
  'SELECT ''need_summer_attendance_reward_outbox.bonus_amount already exists'''
);
PREPARE need_summer_upgrade_statement FROM @need_summer_add_bonus_amount;
EXECUTE need_summer_upgrade_statement;
DEALLOCATE PREPARE need_summer_upgrade_statement;

-- The event starts on 2026-08-15. This backfill makes a pre-production retry safe
-- if test claims already exist. Delivered mail is intentionally never modified.
UPDATE `need_summer_attendance_reward_outbox` AS `o`
JOIN `need_summer_attendance_claim` AS `c` ON `c`.`claim_id` = `o`.`claim_id`
SET `o`.`bonus_item_id` = CASE `c`.`claim_no`
      WHEN 5 THEN 1001592
      WHEN 10 THEN 12412
      WHEN 15 THEN 7720
      WHEN 20 THEN 399934
      ELSE 0
    END,
    `o`.`bonus_amount` = CASE `c`.`claim_no`
      WHEN 5 THEN 1
      WHEN 10 THEN 1
      WHEN 15 THEN 3
      WHEN 20 THEN 1
      ELSE 0
    END,
    `o`.`updated_at` = NOW()
WHERE `o`.`event_id` = 202608
  AND `o`.`status` IN (0,3,4)
  AND `o`.`mail_id` IS NULL;

SET @need_summer_has_bonus_item_id = NULL;
SET @need_summer_add_bonus_item_id = NULL;
SET @need_summer_has_bonus_amount = NULL;
SET @need_summer_add_bonus_amount = NULL;

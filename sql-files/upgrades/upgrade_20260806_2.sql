-- NEED 2026 summer attendance outbox machine-readable error code.
-- Apply only after taking the normal database backup and with the feature off.

SET @need_summer_has_last_error_code = (
  SELECT COUNT(*)
  FROM `information_schema`.`COLUMNS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'need_summer_attendance_reward_outbox'
    AND `COLUMN_NAME` = 'last_error_code'
);

SET @need_summer_add_last_error_code = IF(
  @need_summer_has_last_error_code = 0,
  'ALTER TABLE `need_summer_attendance_reward_outbox` ADD COLUMN `last_error_code` varchar(64) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL DEFAULT '''' AFTER `last_attempt_at`',
  'SELECT ''need_summer_attendance_reward_outbox.last_error_code already exists'''
);

PREPARE need_summer_upgrade_statement FROM @need_summer_add_last_error_code;
EXECUTE need_summer_upgrade_statement;
DEALLOCATE PREPARE need_summer_upgrade_statement;

SET @need_summer_has_last_error_code = NULL;
SET @need_summer_add_last_error_code = NULL;

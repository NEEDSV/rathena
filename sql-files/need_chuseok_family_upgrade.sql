-- NEED 2026 chuseok - 가족 IP 예외 지원을 위한 스키마 확장.
--
-- 같은 집에서 여러 계정을 쓰는 가족을 운영자가 승인하면, 해당 계정들은
-- 꿀송편 일일 제한과 대축/아리아 교환 한도의 **IP 검사를 면제**받고
-- 계정 한도만 적용받습니다. (가족 3명 = 꿀송편 하루 3개, 대축 주 15개)
--
-- 가족 명단은 여름 이벤트에서 쓰던 아래 테이블을 그대로 재사용합니다.
-- 새 테이블을 만들지 않으므로 임포트 누락 사고가 생기지 않습니다.
--   need_summer_attendance_family_group
--   need_summer_attendance_family_member
--   need_summer_attendance_family_audit
-- 추석은 event_id = 202609 행만 봅니다 (여름 202608 과 섞이지 않음).
--
-- 등록은 인게임 @chuseokfamily (GM 99) 로 합니다.
--
-- 이 파일은 기존 설치본에 컬럼만 덧붙입니다. 신규 설치는
-- need_chuseok_hunt.sql / need_chuseok_exchange.sql 에 이미 반영되어 있습니다.

-- ADD COLUMN IF NOT EXISTS 는 MariaDB 전용이라, MySQL 8 에서도 돌아가도록
-- information_schema 로 확인한 뒤 PREPARE/EXECUTE 합니다. 여러 번 실행해도 안전합니다.

SET @db := DATABASE();

SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=@db
                AND TABLE_NAME='need_chuseok_honey_claim' AND COLUMN_NAME='family_group_id') = 0,
  'ALTER TABLE `need_chuseok_honey_claim` ADD COLUMN `family_group_id` int unsigned NOT NULL DEFAULT 0 AFTER `source`',
  'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=@db
                AND TABLE_NAME='need_chuseok_honey_claim' AND COLUMN_NAME='family_exception') = 0,
  'ALTER TABLE `need_chuseok_honey_claim` ADD COLUMN `family_exception` tinyint unsigned NOT NULL DEFAULT 0 AFTER `family_group_id`',
  'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=@db
                AND TABLE_NAME='need_chuseok_honey_ip_daily' AND COLUMN_NAME='family_group_id') = 0,
  'ALTER TABLE `need_chuseok_honey_ip_daily` ADD COLUMN `family_group_id` int unsigned NOT NULL DEFAULT 0 AFTER `first_char_id`',
  'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=@db
                AND TABLE_NAME='need_chuseok_honey_log' AND COLUMN_NAME='family_group_id') = 0,
  'ALTER TABLE `need_chuseok_honey_log` ADD COLUMN `family_group_id` int unsigned NOT NULL DEFAULT 0 AFTER `source`',
  'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=@db
                AND TABLE_NAME='need_chuseok_honey_log' AND COLUMN_NAME='family_exception') = 0,
  'ALTER TABLE `need_chuseok_honey_log` ADD COLUMN `family_exception` tinyint unsigned NOT NULL DEFAULT 0 AFTER `family_group_id`',
  'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

SET @sql := IF((SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=@db
                AND TABLE_NAME='need_chuseok_exchange_log' AND COLUMN_NAME='family_group_id') = 0,
  'ALTER TABLE `need_chuseok_exchange_log` ADD COLUMN `family_group_id` int unsigned NOT NULL DEFAULT 0 AFTER `period_key`',
  'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- 가족 명단 테이블이 아직 없다면(여름 이벤트 SQL 미임포트) 함께 만들어 둡니다.
-- 이미 있으면 아무 일도 일어나지 않습니다.
CREATE TABLE IF NOT EXISTS `need_summer_attendance_family_group` (
  `family_group_id` int unsigned NOT NULL AUTO_INCREMENT,
  `event_id` int unsigned NOT NULL,
  `group_name` varchar(64) NOT NULL,
  `active` tinyint unsigned NOT NULL DEFAULT 1,
  `approved_by` varchar(24) NOT NULL,
  `reason` varchar(255) NOT NULL DEFAULT '',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`family_group_id`),
  KEY `event_active` (`event_id`,`active`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_summer_attendance_family_member` (
  `event_id` int unsigned NOT NULL,
  `account_id` int unsigned NOT NULL,
  `family_group_id` int unsigned NOT NULL,
  `active` tinyint unsigned NOT NULL DEFAULT 1,
  `approved_by` varchar(24) NOT NULL,
  `reason` varchar(255) NOT NULL DEFAULT '',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`event_id`,`account_id`),
  KEY `event_group_active` (`event_id`,`family_group_id`,`active`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_summer_attendance_family_audit` (
  `audit_id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `event_id` int unsigned NOT NULL,
  `family_group_id` int unsigned NOT NULL DEFAULT 0,
  `account_id` int unsigned NOT NULL DEFAULT 0,
  `action` varchar(16) NOT NULL,
  `operator` varchar(24) NOT NULL,
  `reason` varchar(255) NOT NULL DEFAULT '',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`audit_id`),
  KEY `event_account` (`event_id`,`account_id`),
  KEY `created_at` (`created_at`)
) ENGINE=InnoDB;

-- 여름에 이미 승인해 둔 가족을 추석으로 그대로 가져오려면 아래를 실행하세요.
-- (필요 없으면 실행하지 마세요. @chuseokfamily 로 새로 등록해도 됩니다.)
--
-- INSERT IGNORE INTO `need_summer_attendance_family_member`
--   (`event_id`,`account_id`,`family_group_id`,`active`,`approved_by`,`reason`)
-- SELECT 202609, `account_id`, `family_group_id`, `active`, `approved_by`,
--        CONCAT('copied from 202608: ', `reason`)
-- FROM `need_summer_attendance_family_member` WHERE `event_id` = 202608;

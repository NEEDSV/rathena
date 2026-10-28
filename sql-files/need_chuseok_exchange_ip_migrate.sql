-- NEED 2026 추석 - 교환 한도 테이블 IP 대응 마이그레이션 (데이터 보존)
--
-- ★ 라이브에서 need_chuseok_exchange.sql 을 다시 돌리지 마세요.
--   그 파일은 신규 설치용이고, 예전에는 DROP TABLE 이 들어 있었습니다.
--   이미 운영 중이라면 이 파일을 쓰세요. 기존 교환 기록을 지우지 않습니다.
--
-- 하는 일
--   1) need_chuseok_exchange_limit 의 현재 스키마를 자동 판별
--        - 없음            -> 새 스키마로 생성
--        - 이미 scope 있음 -> 아무것도 하지 않음 (여러 번 실행해도 안전)
--        - account_id 뿐   -> 데이터를 보존한 채 scope/subject 스키마로 이전
--   2) 구 테이블은 지우지 않고 need_chuseok_exchange_limit_pre_ip_bak 으로 보관
--   3) need_chuseok_exchange_log 에 family_group_id 가 없으면 추가
--
-- MySQL 8 / MariaDB 양쪽에서 동작하도록 ADD COLUMN IF NOT EXISTS 대신
-- information_schema 로 확인 후 PREPARE/EXECUTE 하는 방식을 씁니다.

-- ------------------------------------------------------------------
-- 설정
-- ------------------------------------------------------------------
-- 0 = IP 카운터를 0 에서 시작 (권장)
--     지금까지는 계정 한도만 있었으므로, 이미 교환한 유저를 소급해서 막지 않습니다.
--     이번 주에 한해 한 IP 에서 5개를 넘길 수 있지만 다음 주기부터 정상 동작합니다.
-- 1 = 기존 교환 이력(need_chuseok_exchange_log)으로 IP 카운터를 채움
--     규칙에는 충실하지만, 같은 집에서 2계정이 이미 교환했다면
--     그 IP 는 즉시 한도 초과가 되어 이번 주 내내 막힙니다.
SET @rebuild_ip := 0;

SET @db := DATABASE();

SET @has_limit := (SELECT COUNT(*) FROM information_schema.TABLES
                   WHERE TABLE_SCHEMA = @db AND TABLE_NAME = 'need_chuseok_exchange_limit');
SET @has_scope := (SELECT COUNT(*) FROM information_schema.COLUMNS
                   WHERE TABLE_SCHEMA = @db AND TABLE_NAME = 'need_chuseok_exchange_limit'
                     AND COLUMN_NAME = 'scope');
SET @has_acct  := (SELECT COUNT(*) FROM information_schema.COLUMNS
                   WHERE TABLE_SCHEMA = @db AND TABLE_NAME = 'need_chuseok_exchange_limit'
                     AND COLUMN_NAME = 'account_id');
SET @has_bak   := (SELECT COUNT(*) FROM information_schema.TABLES
                   WHERE TABLE_SCHEMA = @db AND TABLE_NAME = 'need_chuseok_exchange_limit_pre_ip_bak');

-- 구 스키마이고 아직 백업이 없을 때만 이전한다
SET @migrate := IF(@has_limit = 1 AND @has_acct = 1 AND @has_scope = 0 AND @has_bak = 0, 1, 0);

SELECT @has_limit AS 테이블존재, @has_scope AS scope컬럼, @has_acct AS account_id컬럼,
       @migrate AS 이전수행, @rebuild_ip AS IP카운터복원;

-- ------------------------------------------------------------------
-- 1) 구 테이블을 백업 이름으로 옮긴다
-- ------------------------------------------------------------------
SET @sql := IF(@migrate = 1,
  'RENAME TABLE `need_chuseok_exchange_limit` TO `need_chuseok_exchange_limit_pre_ip_bak`',
  'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- ------------------------------------------------------------------
-- 2) 새 스키마 테이블 (이미 있으면 그대로 둔다)
-- ------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `need_chuseok_exchange_limit` (
  `event_id` int unsigned NOT NULL,
  `scope` varchar(8) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `subject` varchar(45) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `product` varchar(16) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `period_key` int unsigned NOT NULL DEFAULT 0,
  `used` int unsigned NOT NULL DEFAULT 0,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`event_id`,`scope`,`subject`,`product`,`period_key`),
  KEY `product_period` (`event_id`,`product`,`period_key`),
  KEY `scope_subject` (`scope`,`subject`)
) ENGINE=InnoDB;

-- ------------------------------------------------------------------
-- 3) 계정 카운터를 그대로 옮긴다
-- ------------------------------------------------------------------
SET @sql := IF(@migrate = 1,
  'INSERT IGNORE INTO `need_chuseok_exchange_limit`
     (`event_id`,`scope`,`subject`,`product`,`period_key`,`used`,`created_at`,`updated_at`)
   SELECT `event_id`, ''ACCOUNT'', CAST(`account_id` AS CHAR), `product`, `period_key`,
          `used`, `created_at`, `updated_at`
   FROM `need_chuseok_exchange_limit_pre_ip_bak`',
  'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- ------------------------------------------------------------------
-- 4) (선택) 교환 이력으로 IP 카운터 복원
-- ------------------------------------------------------------------
SET @sql := IF(@migrate = 1 AND @rebuild_ip = 1,
  'INSERT INTO `need_chuseok_exchange_limit`
     (`event_id`,`scope`,`subject`,`product`,`period_key`,`used`)
   SELECT `event_id`, ''IP'', `ip`, `product`, `period_key`, SUM(`amount`)
   FROM `need_chuseok_exchange_log`
   WHERE `ip` <> ''''
   GROUP BY `event_id`, `ip`, `product`, `period_key`
   ON DUPLICATE KEY UPDATE `used` = VALUES(`used`)',
  'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- ------------------------------------------------------------------
-- 5) 교환 로그에 family_group_id 추가 (없을 때만)
-- ------------------------------------------------------------------
SET @need_fam := (SELECT IF(COUNT(*) = 0, 1, 0) FROM information_schema.COLUMNS
                  WHERE TABLE_SCHEMA = @db AND TABLE_NAME = 'need_chuseok_exchange_log'
                    AND COLUMN_NAME = 'family_group_id');
SET @sql := IF(@need_fam = 1,
  'ALTER TABLE `need_chuseok_exchange_log`
     ADD COLUMN `family_group_id` int unsigned NOT NULL DEFAULT 0 AFTER `period_key`',
  'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- ------------------------------------------------------------------
-- 6) 결과 확인
-- ------------------------------------------------------------------
SELECT `scope`, COUNT(*) AS 행수, SUM(`used`) AS 합계
FROM `need_chuseok_exchange_limit`
GROUP BY `scope`;

SELECT '백업 테이블은 need_chuseok_exchange_limit_pre_ip_bak 입니다. 확인 후 직접 DROP 하세요.' AS 안내;

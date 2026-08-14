-- ============================================================
-- (fishing13) 기존 fishing DB에 주간 랭킹 보상 outbox 테이블만 추가하는 upgrade
--  이미 need_summer_fishing.sql 을 적용한 운영 DB에서 이 파일만 수동 실행하면 됩니다.
--  기존 reward/catch/collection/weekly_best/weekly_result 테이블은 변경하지 않습니다.
--  자동 실행 금지 (운영자 수동 적용).
-- ============================================================
CREATE TABLE IF NOT EXISTS `need_summer_fishing_rank_reward_outbox` (
  `outbox_id`       BIGINT UNSIGNED  NOT NULL AUTO_INCREMENT,
  `event_id`        INT UNSIGNED     NOT NULL,
  `week_no`         TINYINT UNSIGNED NOT NULL,
  `rank_no`         TINYINT UNSIGNED NOT NULL,
  `account_id`      INT UNSIGNED     NOT NULL,
  `char_id`         INT UNSIGNED     NOT NULL,
  `char_name`       VARCHAR(30)      NOT NULL DEFAULT '',
  `reward_item_id`  INT UNSIGNED     NOT NULL,
  `reward_amount`   INT UNSIGNED     NOT NULL,
  `status`          TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=pending,1=processing,2=delivered,3=retry,4=review',
  `attempts`        SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `next_attempt_at` DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `last_attempt_at` DATETIME         DEFAULT NULL,
  `last_error_code` VARCHAR(64) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL DEFAULT '',
  `last_error`      VARCHAR(255)     NOT NULL DEFAULT '',
  `mail_id`         BIGINT UNSIGNED  DEFAULT NULL,
  `created_at`      DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at`      DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  `delivered_at`    DATETIME         DEFAULT NULL,
  PRIMARY KEY (`outbox_id`),
  UNIQUE KEY `uq_rank` (`event_id`,`week_no`,`rank_no`),
  UNIQUE KEY `uq_account` (`event_id`,`week_no`,`account_id`),
  UNIQUE KEY `uq_mail` (`mail_id`),
  KEY `status_created` (`status`,`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

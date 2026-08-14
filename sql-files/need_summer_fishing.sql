-- ============================================================
-- NEED 여름 이벤트 낚시 (fishing7) SQL 스키마
-- event_id = 202608 (need_summer 공통)
-- 04:00 logical day 기준(logical_date = YYYYMMDD 정수, now-4h 기준)
--
-- 운영 DB에 자동 적용하지 않습니다. 아래 두 테이블을 수동으로 적용한 뒤
-- 실기동 테스트하세요. (기존 need_summer_* 테이블과 동일한 InnoDB 스타일)
-- ============================================================

-- 일일 기본 보상 카운터 (account / IP 별)
--  subject_type: 0 = account, 1 = ip
--  reward_count: 해당 logical_date 에 지급된 "최종 성공 기본 보상" 횟수 (최대 10)
CREATE TABLE IF NOT EXISTS `need_summer_fishing_reward` (
  `event_id`     INT UNSIGNED     NOT NULL,
  `logical_date` INT UNSIGNED     NOT NULL,
  `subject_type` TINYINT UNSIGNED NOT NULL,
  `subject_key`  VARCHAR(63)      NOT NULL,
  `reward_count` INT UNSIGNED     NOT NULL DEFAULT 0,
  `updated_at`   DATETIME         NOT NULL,
  PRIMARY KEY (`event_id`, `logical_date`, `subject_type`, `subject_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- 최종 성공(code 8) catch 로그 (무료/미끼 성공 모두 저장, 실패는 저장하지 않음)
--  길이 = mm 원본 정수, 무게 = g 원본 정수 (향후 랭킹/도감/통계 산출용)
CREATE TABLE IF NOT EXISTS `need_summer_fishing_catch` (
  `id`                  BIGINT UNSIGNED  NOT NULL AUTO_INCREMENT,
  `event_id`            INT UNSIGNED     NOT NULL,
  `logical_date`        INT UNSIGNED     NOT NULL,
  `account_id`          INT UNSIGNED     NOT NULL,
  `char_id`             INT UNSIGNED     NOT NULL,
  `character_name`      VARCHAR(30)      NOT NULL DEFAULT '',
  `fish_id`             INT UNSIGNED     NOT NULL,
  `rarity`              TINYINT UNSIGNED NOT NULL,
  `length_mm`           INT UNSIGNED     NOT NULL,
  `weight_g`            INT UNSIGNED     NOT NULL,
  `reaction_ms`         INT              NOT NULL,
  `reaction_grade`      TINYINT UNSIGNED NOT NULL,
  `spot_id`             INT UNSIGNED     NOT NULL,
  `reel_total`          TINYINT UNSIGNED NOT NULL,
  `reel_success`        TINYINT UNSIGNED NOT NULL,
  `session_id`          INT UNSIGNED     NOT NULL,
  `rewarded`            TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `reward_token_amount` INT UNSIGNED     NOT NULL DEFAULT 0,
  `bait_used`           TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `caught_at`           DATETIME         NOT NULL,
  PRIMARY KEY (`id`),
  KEY `idx_rank`   (`event_id`, `rarity`, `weight_g`),
  KEY `idx_length` (`event_id`, `fish_id`, `length_mm`),
  KEY `idx_acc`    (`event_id`, `account_id`, `logical_date`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ============================================================
-- (fishing9) 낚시 도감/개인 기록 집계 테이블
--  - 계정 단위 도감: UNIQUE = event_id + account_id + fish_id
--  - catch(원본 이력)와 분리된 집계 테이블. 문제 시 catch 로 재집계 가능.
--  - 최종 성공(code 8)에서만 갱신(무료/미끼 성공 모두 대상, 실패 제외).
--  - 신규 row 발생 = 해당 계정이 그 어종을 처음 잡음 -> 최초 획득 Cutin 트리거.
-- ============================================================
CREATE TABLE IF NOT EXISTS `need_summer_fishing_collection` (
  `event_id`             INT UNSIGNED  NOT NULL,
  `account_id`           INT UNSIGNED  NOT NULL,
  `fish_id`              INT UNSIGNED  NOT NULL,
  `catch_count`          INT UNSIGNED  NOT NULL DEFAULT 0,      -- 해당 계정이 이 어종을 잡은 총 횟수
  `max_length_mm`        INT UNSIGNED  NOT NULL DEFAULT 0,      -- 최대 길이(mm)
  `max_length_weight_g`  INT UNSIGNED  NOT NULL DEFAULT 0,      -- 그 최대 길이 개체의 당시 무게(g)
  `max_length_caught_at` DATETIME      NULL DEFAULT NULL,       -- 최대 길이 개체 획득 시각
  `max_weight_g`         INT UNSIGNED  NOT NULL DEFAULT 0,      -- 최대 무게(g)
  `max_weight_length_mm` INT UNSIGNED  NOT NULL DEFAULT 0,      -- 그 최대 무게 개체의 당시 길이(mm)
  `max_weight_caught_at` DATETIME      NULL DEFAULT NULL,       -- 최대 무게 개체 획득 시각
  `first_caught_at`      DATETIME      NULL DEFAULT NULL,       -- 도감 최초 등록 시각
  `last_caught_at`       DATETIME      NULL DEFAULT NULL,       -- 가장 최근 획득 시각
  PRIMARY KEY (`event_id`, `account_id`, `fish_id`),
  KEY `idx_acc`  (`event_id`, `account_id`),
  KEY `idx_fish` (`event_id`, `fish_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ============================================================
-- (fishing12) 주간 지정 어종 랭킹
--  주차: 이벤트 본기간 4주(각 경계 오전 04:00). 대상 어종 1주차=4/2주차=5/3주차=8/4주차=10.
--  랭킹은 account 단위 최고 기록. 순위 = 길이 DESC > 무게 DESC > 먼저 잡은 순(ASC).
--  반응 등급/릴 횟수는 순위에 영향 없음(reaction_ms는 기록용으로만 저장).
--  운영 DB 자동 적용 금지. 수동 적용.
-- ============================================================

-- account 단위 주간 최고 기록 (한 계정 = 한 주차/대상어종 당 1행)
CREATE TABLE IF NOT EXISTS `need_summer_fishing_weekly_best` (
  `event_id`    INT UNSIGNED     NOT NULL,
  `week_no`     TINYINT UNSIGNED NOT NULL,
  `fish_id`     INT UNSIGNED     NOT NULL,
  `account_id`  INT UNSIGNED     NOT NULL,
  `char_id`     INT UNSIGNED     NOT NULL,      -- 최고 기록을 달성한 캐릭터
  `char_name`   VARCHAR(30)      NOT NULL DEFAULT '',
  `length_mm`   INT UNSIGNED     NOT NULL,
  `weight_g`    INT UNSIGNED     NOT NULL,
  `reaction_ms` INT              NOT NULL DEFAULT -1,  -- 기록용, 순위 미사용
  `caught_at`   DATETIME         NOT NULL,
  `updated_at`  DATETIME         NOT NULL,
  PRIMARY KEY (`event_id`,`week_no`,`fish_id`,`account_id`),
  KEY `idx_rank` (`event_id`,`week_no`,`fish_id`,`length_mm`,`weight_g`,`caught_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- 주차 종료 TOP10 Snapshot (최종 순위 확정). rank_no = 1..10
--  account 유일성은 weekly_best(계정당 1행)에서 자연 보장되므로 별도 UNIQUE 없이 조회 인덱스만.
CREATE TABLE IF NOT EXISTS `need_summer_fishing_weekly_result` (
  `event_id`       INT UNSIGNED     NOT NULL,
  `week_no`        TINYINT UNSIGNED NOT NULL,
  `rank_no`        TINYINT UNSIGNED NOT NULL,
  `fish_id`        INT UNSIGNED     NOT NULL,
  `account_id`     INT UNSIGNED     NOT NULL,
  `char_id`        INT UNSIGNED     NOT NULL,
  `char_name`      VARCHAR(30)      NOT NULL DEFAULT '',
  `length_mm`      INT UNSIGNED     NOT NULL,
  `weight_g`       INT UNSIGNED     NOT NULL,
  `reaction_ms`    INT              NOT NULL DEFAULT -1,
  `caught_at`      DATETIME         NOT NULL,
  `snapshotted_at` DATETIME         NOT NULL,
  PRIMARY KEY (`event_id`,`week_no`,`rank_no`),
  KEY `idx_acc` (`event_id`,`week_no`,`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- ============================================================
-- (fishing13) 주간 랭킹 보상 지급 outbox (시스템 우편 durable delivery)
--  weekly_result(순위 원장) -> 이 outbox(보상 전달 원장) -> 시스템 우편.
--  status: 0=pending,1=processing,2=delivered,3=retry,4=review (출석 outbox와 동일 의미).
--  UNIQUE(event,week,rank) = 멱등 예약 키. UNIQUE(event,week,account) = 계정 중복 방어.
--  reward_item_id=399925(코코넛 토큰). 운영 DB 자동 적용 금지(수동).
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

-- ============================================================
-- (fishing15) NEED 여름 이벤트 보상 Origin Log
--  "이 가치(Zeny/Cash/Shadow Item)가 NEED 여름 이벤트에서 생성되었다"는 출발점만 기록.
--  기록 대상: 399927 황금 수박 / 399929 1억 제니 주머니 / 399930 캐시 10만 포인트 교환권 / 399931 코코넛 쉐도우 상자.
--  reward_type: 1=ZENY, 2=CASH, 3=ITEM. reward_item_id/unique_id: ZENY/CASH=0, ITEM=실제값.
--  이후 계정 간 이동 추적은 기존 zenylog/cashlog/picklog + char.account_id/loginlog 로 관리자 웹에서 수행.
--  운영 DB 자동 적용 금지(수동). 이미 적용된 서버는 IF NOT EXISTS 로 안전.
-- ============================================================
CREATE TABLE IF NOT EXISTS `need_summer_reward_log` (
  `id`             BIGINT UNSIGNED  NOT NULL AUTO_INCREMENT,
  `event_id`       INT UNSIGNED     NOT NULL,
  `source_item_id` INT UNSIGNED     NOT NULL,          -- 399927/399929/399930/399931
  `account_id`     INT UNSIGNED     NOT NULL,
  `char_id`        INT UNSIGNED     NOT NULL,
  `ip`             VARCHAR(15)      NOT NULL DEFAULT '',
  `reward_type`    TINYINT UNSIGNED NOT NULL,          -- 1=ZENY, 2=CASH, 3=ITEM
  `reward_item_id` INT UNSIGNED     NOT NULL DEFAULT 0, -- ITEM 만 실제 item_id
  `reward_amount`  BIGINT           NOT NULL DEFAULT 0, -- 실제 지급값(Zeny/Cash/수량)
  `unique_id`      BIGINT UNSIGNED  NOT NULL DEFAULT 0, -- ITEM 만 실제 unique_id
  `created_at`     DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_account` (`account_id`),
  KEY `idx_char`    (`char_id`),
  KEY `idx_created` (`created_at`),
  KEY `idx_source`  (`source_item_id`),
  KEY `idx_unique`  (`unique_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

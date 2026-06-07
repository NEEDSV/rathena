-- NEED Costume Collection DB
-- 1차: 도감 등록
CREATE TABLE IF NOT EXISTS `need_costume_collection` (
    `account_id` INT UNSIGNED NOT NULL,
    `collection_id` INT UNSIGNED NOT NULL,
    `item_id` INT UNSIGNED NOT NULL,
    `reg_date` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,

    PRIMARY KEY (`account_id`, `collection_id`),
    UNIQUE KEY `uk_account_item` (`account_id`, `item_id`),
    KEY `idx_collection_id` (`collection_id`),
    KEY `idx_item_id` (`item_id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8mb4;
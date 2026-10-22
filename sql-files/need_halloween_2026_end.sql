-- NEED 2026 halloween event - END OF EVENT: remove every remaining halloween coin (6658).
--
-- *** Do NOT run this during development. Written and tested on the isolated test DB only. ***
-- Planned run: 2026-12-12 10:00 maintenance, by the operator, see
-- md/NEED_Halloween_2026_Operations.md section 7.
--
-- Preconditions
--   1. login / char / map servers are STOPPED. The map server writes online characters,
--      carts and storages back from memory, so a purge under a running server is undone.
--   2. A database backup was taken just before.
--
-- 6658 has no trade restriction, so it can sit in every item container:
-- inventory, cart, account storage (+ premium storage2 / storage3), guild storage,
-- RODEX mail attachments and the auction table (one item per row, no amount column).
-- Vending shops sell from the cart, so their vending_items rows that point at a purged
-- cart row are removed too.
--
-- Each table is handled as "copy into need_halloween_2026_coin_purge, then delete" right
-- after each other. If the run stops half way, the tables already done are complete and
-- the rest is untouched, so running the file again only picks up what is left.
-- Ledger and exchange logs (need_chuseok_honey_*, need_chuseok_exchange_*, picklog) stay.

CREATE TABLE IF NOT EXISTS `need_halloween_2026_coin_purge` (
  `purge_id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `source_table` varchar(32) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `row_id` bigint unsigned NOT NULL COMMENT 'id / auction_id / mail id',
  `row_index` smallint unsigned NOT NULL DEFAULT 0 COMMENT 'mail attachment index',
  `owner_id` int unsigned NOT NULL DEFAULT 0 COMMENT 'char_id / account_id / guild_id / seller_id',
  `amount` int unsigned NOT NULL,
  `purged_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`purge_id`),
  KEY `source_row` (`source_table`, `row_id`)
) ENGINE=InnoDB;

-- inventory
INSERT INTO `need_halloween_2026_coin_purge` (`source_table`, `row_id`, `owner_id`, `amount`)
  SELECT 'inventory', `id`, `char_id`, `amount` FROM `inventory` WHERE `nameid` = 6658;
DELETE FROM `inventory` WHERE `nameid` = 6658;

-- cart (vending rows first, while their cart rows still exist)
DELETE `v` FROM `vending_items` `v` JOIN `cart_inventory` `c` ON `c`.`id` = `v`.`cartinventory_id` WHERE `c`.`nameid` = 6658;
INSERT INTO `need_halloween_2026_coin_purge` (`source_table`, `row_id`, `owner_id`, `amount`)
  SELECT 'cart_inventory', `id`, `char_id`, `amount` FROM `cart_inventory` WHERE `nameid` = 6658;
DELETE FROM `cart_inventory` WHERE `nameid` = 6658;

-- account storage and premium storages
INSERT INTO `need_halloween_2026_coin_purge` (`source_table`, `row_id`, `owner_id`, `amount`)
  SELECT 'storage', `id`, `account_id`, `amount` FROM `storage` WHERE `nameid` = 6658;
DELETE FROM `storage` WHERE `nameid` = 6658;
INSERT INTO `need_halloween_2026_coin_purge` (`source_table`, `row_id`, `owner_id`, `amount`)
  SELECT 'storage2', `id`, `account_id`, `amount` FROM `storage2` WHERE `nameid` = 6658;
DELETE FROM `storage2` WHERE `nameid` = 6658;
INSERT INTO `need_halloween_2026_coin_purge` (`source_table`, `row_id`, `owner_id`, `amount`)
  SELECT 'storage3', `id`, `account_id`, `amount` FROM `storage3` WHERE `nameid` = 6658;
DELETE FROM `storage3` WHERE `nameid` = 6658;

-- guild storage
INSERT INTO `need_halloween_2026_coin_purge` (`source_table`, `row_id`, `owner_id`, `amount`)
  SELECT 'guild_storage', `id`, `guild_id`, `amount` FROM `guild_storage` WHERE `nameid` = 6658;
DELETE FROM `guild_storage` WHERE `nameid` = 6658;

-- RODEX mail attachments (the mail itself stays)
INSERT INTO `need_halloween_2026_coin_purge` (`source_table`, `row_id`, `row_index`, `owner_id`, `amount`)
  SELECT 'mail_attachments', `id`, `index`, `id`, `amount` FROM `mail_attachments` WHERE `nameid` = 6658;
DELETE FROM `mail_attachments` WHERE `nameid` = 6658;

-- auction (one item per row)
INSERT INTO `need_halloween_2026_coin_purge` (`source_table`, `row_id`, `owner_id`, `amount`)
  SELECT 'auction', `auction_id`, `seller_id`, 1 FROM `auction` WHERE `nameid` = 6658;
DELETE FROM `auction` WHERE `nameid` = 6658;

-- Check: every place must report 0 rows.
SELECT 'inventory' AS `place`, COUNT(*) AS `rows_left` FROM `inventory` WHERE `nameid` = 6658
UNION ALL SELECT 'cart_inventory', COUNT(*) FROM `cart_inventory` WHERE `nameid` = 6658
UNION ALL SELECT 'storage', COUNT(*) FROM `storage` WHERE `nameid` = 6658
UNION ALL SELECT 'storage2', COUNT(*) FROM `storage2` WHERE `nameid` = 6658
UNION ALL SELECT 'storage3', COUNT(*) FROM `storage3` WHERE `nameid` = 6658
UNION ALL SELECT 'guild_storage', COUNT(*) FROM `guild_storage` WHERE `nameid` = 6658
UNION ALL SELECT 'mail_attachments', COUNT(*) FROM `mail_attachments` WHERE `nameid` = 6658
UNION ALL SELECT 'auction', COUNT(*) FROM `auction` WHERE `nameid` = 6658;

-- Summary of what this run removed.
SELECT `source_table`, COUNT(*) AS `rows`, SUM(`amount`) AS `coins`
FROM `need_halloween_2026_coin_purge` WHERE `purged_at` >= NOW() - INTERVAL 1 HOUR GROUP BY `source_table`;

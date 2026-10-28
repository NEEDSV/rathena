-- ============================================================================================
-- NEED 2026 halloween - (A) BEFORE-START CLEANUP of item 6532 (the OLD chuseok honey songpyun)
-- ============================================================================================
--
--   !!! RUN THIS ONCE, BEFORE the 2026 halloween starts (before Jack's Candy can drop). !!!
--   !!! NEVER run it while the halloween event is running: from the halloween start on,  !!!
--   !!! every 6532 is a real Jack's Candy that a player earned and it would be destroyed. !!!
--   (The END-of-event removal of Jack's Candy is a different file:
--    sql-files/need_halloween_2026_end.sql, section B, 2026-12-12 10:00.)
--
-- Why: item 6532 keeps its id and only changes its display name (꿀송편 -> 잭의 사탕). Any
-- honey songpyun still held would simply show up as Jack's Candy. Policy (2026-10-26): all old
-- 6532 stock is removed before the change goes live.
--
-- Preconditions
--   1. login / char / map servers are STOPPED. The map server writes online characters, carts
--      and storages back from memory, so a purge under a running server is undone.
--   2. A database backup was taken just before.
--   3. The audit (sql-files/need_halloween_2026_jack_candy_audit.sql) was run and its numbers
--      were written down. This file prints the same numbers again before and after.
--
-- 6532 (old honey songpyun) was: no drop / trade / sell / cart / account storage / guild storage /
-- mail / auction, and TradePartner (spouse trade) allowed. It can therefore be in a character
-- inventory, and in RODEX mail attachments (the 2026-09-23 compensation mails). The other
-- containers are cleaned as well so that nothing is left anywhere, whatever the past flags were.
--
-- Each table is handled as "copy into need_6532_honey_songpyun_purge, then delete" right after
-- each other. If the run stops half way, the tables already done are complete and the rest is
-- untouched, so running the file again only picks up what is left (safe to re-run).
-- Logs (picklog, need_chuseok_honey_*, need_summer_reward_log) are history and stay.
-- ============================================================================================

-- 0) before: what is going to be removed
SELECT 'BEFORE' AS `step`, 'inventory' AS `place`, COUNT(*) AS `rows`, COALESCE(SUM(`amount`), 0) AS `amount` FROM `inventory` WHERE `nameid` = 6532
UNION ALL SELECT 'BEFORE', 'cart_inventory', COUNT(*), COALESCE(SUM(`amount`), 0) FROM `cart_inventory` WHERE `nameid` = 6532
UNION ALL SELECT 'BEFORE', 'storage', COUNT(*), COALESCE(SUM(`amount`), 0) FROM `storage` WHERE `nameid` = 6532
UNION ALL SELECT 'BEFORE', 'storage2', COUNT(*), COALESCE(SUM(`amount`), 0) FROM `storage2` WHERE `nameid` = 6532
UNION ALL SELECT 'BEFORE', 'storage3', COUNT(*), COALESCE(SUM(`amount`), 0) FROM `storage3` WHERE `nameid` = 6532
UNION ALL SELECT 'BEFORE', 'guild_storage', COUNT(*), COALESCE(SUM(`amount`), 0) FROM `guild_storage` WHERE `nameid` = 6532
UNION ALL SELECT 'BEFORE', 'mail_attachments', COUNT(*), COALESCE(SUM(`amount`), 0) FROM `mail_attachments` WHERE `nameid` = 6532
UNION ALL SELECT 'BEFORE', 'auction', COUNT(*), COUNT(*) FROM `auction` WHERE `nameid` = 6532
UNION ALL SELECT 'BEFORE', 'buyingstore_items', COUNT(*), COALESCE(SUM(`amount`), 0) FROM `buyingstore_items` WHERE `item_id` = 6532;

CREATE TABLE IF NOT EXISTS `need_6532_honey_songpyun_purge` (
  `purge_id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `source_table` varchar(32) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `row_id` bigint unsigned NOT NULL COMMENT 'id / auction_id / mail id / buyingstore_id',
  `row_index` smallint unsigned NOT NULL DEFAULT 0 COMMENT 'mail attachment / buyingstore item index',
  `owner_id` int unsigned NOT NULL DEFAULT 0 COMMENT 'char_id / account_id / guild_id / seller_id / mail recipient char_id',
  `nameid` int unsigned NOT NULL DEFAULT 6532,
  `amount` int unsigned NOT NULL,
  `purged_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`purge_id`),
  KEY `source_row` (`source_table`, `row_id`)
) ENGINE=InnoDB;

-- inventory
INSERT INTO `need_6532_honey_songpyun_purge` (`source_table`, `row_id`, `owner_id`, `amount`)
  SELECT 'inventory', `id`, `char_id`, `amount` FROM `inventory` WHERE `nameid` = 6532;
DELETE FROM `inventory` WHERE `nameid` = 6532;

-- cart (vending rows first, while their cart rows still exist)
DELETE `v` FROM `vending_items` `v` JOIN `cart_inventory` `c` ON `c`.`id` = `v`.`cartinventory_id` WHERE `c`.`nameid` = 6532;
INSERT INTO `need_6532_honey_songpyun_purge` (`source_table`, `row_id`, `owner_id`, `amount`)
  SELECT 'cart_inventory', `id`, `char_id`, `amount` FROM `cart_inventory` WHERE `nameid` = 6532;
DELETE FROM `cart_inventory` WHERE `nameid` = 6532;

-- account storage and premium storages
INSERT INTO `need_6532_honey_songpyun_purge` (`source_table`, `row_id`, `owner_id`, `amount`)
  SELECT 'storage', `id`, `account_id`, `amount` FROM `storage` WHERE `nameid` = 6532;
DELETE FROM `storage` WHERE `nameid` = 6532;
INSERT INTO `need_6532_honey_songpyun_purge` (`source_table`, `row_id`, `owner_id`, `amount`)
  SELECT 'storage2', `id`, `account_id`, `amount` FROM `storage2` WHERE `nameid` = 6532;
DELETE FROM `storage2` WHERE `nameid` = 6532;
INSERT INTO `need_6532_honey_songpyun_purge` (`source_table`, `row_id`, `owner_id`, `amount`)
  SELECT 'storage3', `id`, `account_id`, `amount` FROM `storage3` WHERE `nameid` = 6532;
DELETE FROM `storage3` WHERE `nameid` = 6532;

-- guild storage
INSERT INTO `need_6532_honey_songpyun_purge` (`source_table`, `row_id`, `owner_id`, `amount`)
  SELECT 'guild_storage', `id`, `guild_id`, `amount` FROM `guild_storage` WHERE `nameid` = 6532;
DELETE FROM `guild_storage` WHERE `nameid` = 6532;

-- RODEX mail attachments (the mail itself stays; owner = recipient char_id)
INSERT INTO `need_6532_honey_songpyun_purge` (`source_table`, `row_id`, `row_index`, `owner_id`, `amount`)
  SELECT 'mail_attachments', `a`.`id`, `a`.`index`, COALESCE(`m`.`dest_id`, 0), `a`.`amount`
  FROM `mail_attachments` `a` LEFT JOIN `mail` `m` ON `m`.`id` = `a`.`id` WHERE `a`.`nameid` = 6532;
DELETE FROM `mail_attachments` WHERE `nameid` = 6532;

-- auction (one item per row)
INSERT INTO `need_6532_honey_songpyun_purge` (`source_table`, `row_id`, `owner_id`, `amount`)
  SELECT 'auction', `auction_id`, `seller_id`, 1 FROM `auction` WHERE `nameid` = 6532;
DELETE FROM `auction` WHERE `nameid` = 6532;

-- buying store orders for 6532 (6532 never had BuyingStore; expected 0)
INSERT INTO `need_6532_honey_songpyun_purge` (`source_table`, `row_id`, `row_index`, `amount`)
  SELECT 'buyingstore_items', `buyingstore_id`, `index`, `amount` FROM `buyingstore_items` WHERE `item_id` = 6532;
DELETE FROM `buyingstore_items` WHERE `item_id` = 6532;

-- Check: every place must report 0 rows.
SELECT 'AFTER' AS `step`, 'inventory' AS `place`, COUNT(*) AS `rows_left` FROM `inventory` WHERE `nameid` = 6532
UNION ALL SELECT 'AFTER', 'cart_inventory', COUNT(*) FROM `cart_inventory` WHERE `nameid` = 6532
UNION ALL SELECT 'AFTER', 'storage', COUNT(*) FROM `storage` WHERE `nameid` = 6532
UNION ALL SELECT 'AFTER', 'storage2', COUNT(*) FROM `storage2` WHERE `nameid` = 6532
UNION ALL SELECT 'AFTER', 'storage3', COUNT(*) FROM `storage3` WHERE `nameid` = 6532
UNION ALL SELECT 'AFTER', 'guild_storage', COUNT(*) FROM `guild_storage` WHERE `nameid` = 6532
UNION ALL SELECT 'AFTER', 'mail_attachments', COUNT(*) FROM `mail_attachments` WHERE `nameid` = 6532
UNION ALL SELECT 'AFTER', 'auction', COUNT(*) FROM `auction` WHERE `nameid` = 6532
UNION ALL SELECT 'AFTER', 'buyingstore_items', COUNT(*) FROM `buyingstore_items` WHERE `item_id` = 6532;

-- Summary of what this run removed.
SELECT `source_table`, COUNT(*) AS `rows`, SUM(`amount`) AS `amount`
FROM `need_6532_honey_songpyun_purge` WHERE `purged_at` >= NOW() - INTERVAL 1 HOUR GROUP BY `source_table`;

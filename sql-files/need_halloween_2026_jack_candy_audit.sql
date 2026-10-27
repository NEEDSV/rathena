-- NEED 2026 halloween - item 6532 holdings audit. SELECT only, changes nothing.
--
-- 6532 was the chuseok honey songpyun and is the halloween Jack's Candy from 2026-10-26 on
-- (same id, display name changed for good). Use this file
--   * before the halloween start:  every row found here is OLD honey songpyun stock that
--     need_halloween_2026_jack_candy_cleanup.sql must remove first;
--   * after that cleanup:          every place must be 0;
--   * at the 2026-12-12 end:        what need_halloween_2026_end.sql (section B) will remove.
--
-- Safe while servers run, but the map server keeps online characters, carts and storages in
-- memory, so the numbers are exact only with login / char / map servers stopped.

-- 1) per place: rows, total amount, characters, accounts
SELECT 'inventory' AS `place`, COUNT(*) AS `rows`, COALESCE(SUM(`i`.`amount`), 0) AS `amount`,
       COUNT(DISTINCT `i`.`char_id`) AS `characters`, COUNT(DISTINCT `c`.`account_id`) AS `accounts`
  FROM `inventory` `i` LEFT JOIN `char` `c` ON `c`.`char_id` = `i`.`char_id` WHERE `i`.`nameid` = 6532
UNION ALL SELECT 'cart_inventory', COUNT(*), COALESCE(SUM(`i`.`amount`), 0), COUNT(DISTINCT `i`.`char_id`), COUNT(DISTINCT `c`.`account_id`)
  FROM `cart_inventory` `i` LEFT JOIN `char` `c` ON `c`.`char_id` = `i`.`char_id` WHERE `i`.`nameid` = 6532
UNION ALL SELECT 'storage', COUNT(*), COALESCE(SUM(`amount`), 0), 0, COUNT(DISTINCT `account_id`) FROM `storage` WHERE `nameid` = 6532
UNION ALL SELECT 'storage2', COUNT(*), COALESCE(SUM(`amount`), 0), 0, COUNT(DISTINCT `account_id`) FROM `storage2` WHERE `nameid` = 6532
UNION ALL SELECT 'storage3', COUNT(*), COALESCE(SUM(`amount`), 0), 0, COUNT(DISTINCT `account_id`) FROM `storage3` WHERE `nameid` = 6532
UNION ALL SELECT 'guild_storage', COUNT(*), COALESCE(SUM(`amount`), 0), 0, COUNT(DISTINCT `guild_id`) FROM `guild_storage` WHERE `nameid` = 6532
UNION ALL SELECT 'mail_attachments', COUNT(*), COALESCE(SUM(`a`.`amount`), 0), COUNT(DISTINCT `m`.`dest_id`), COUNT(DISTINCT `c`.`account_id`)
  FROM `mail_attachments` `a` LEFT JOIN `mail` `m` ON `m`.`id` = `a`.`id` LEFT JOIN `char` `c` ON `c`.`char_id` = `m`.`dest_id`   -- dest_id = recipient char_id
  WHERE `a`.`nameid` = 6532
UNION ALL SELECT 'auction', COUNT(*), COUNT(*), 0, COUNT(DISTINCT `seller_id`) FROM `auction` WHERE `nameid` = 6532   -- one item per row
UNION ALL SELECT 'buyingstore_items (buy orders)', COUNT(*), COALESCE(SUM(`amount`), 0), 0, 0 FROM `buyingstore_items` WHERE `item_id` = 6532
UNION ALL SELECT 'vending_items (on sale)', COUNT(*), COALESCE(SUM(`v`.`amount`), 0), 0, 0
  FROM `vending_items` `v` JOIN `cart_inventory` `c` ON `c`.`id` = `v`.`cartinventory_id` WHERE `c`.`nameid` = 6532;

-- 2) distinct accounts holding 6532 anywhere a character or an account can hold it
SELECT COUNT(DISTINCT `account_id`) AS `accounts_holding_6532` FROM (
  SELECT `c`.`account_id` FROM `inventory` `i` JOIN `char` `c` ON `c`.`char_id` = `i`.`char_id` WHERE `i`.`nameid` = 6532
  UNION SELECT `c`.`account_id` FROM `cart_inventory` `i` JOIN `char` `c` ON `c`.`char_id` = `i`.`char_id` WHERE `i`.`nameid` = 6532
  UNION SELECT `account_id` FROM `storage` WHERE `nameid` = 6532
  UNION SELECT `account_id` FROM `storage2` WHERE `nameid` = 6532
  UNION SELECT `account_id` FROM `storage3` WHERE `nameid` = 6532
  UNION SELECT `c`.`account_id` FROM `mail` `m` JOIN `char` `c` ON `c`.`char_id` = `m`.`dest_id` WHERE `m`.`id` IN (SELECT `id` FROM `mail_attachments` WHERE `nameid` = 6532)
) `holders`;

-- 3) NPC market stock that would sell 6532 (expected 0; informational)
SELECT 'market' AS `table_name`, COUNT(*) AS `rows` FROM `market` WHERE `nameid` = 6532
UNION ALL SELECT 'sales', COUNT(*) FROM `sales` WHERE `nameid` = 6532;

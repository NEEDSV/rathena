-- NEED 2026 halloween event - halloween coin (6658) holdings audit. SELECT only.
--
-- Run before the event starts (old coins from past events?) and before / after the
-- 2026-12-12 purge (sql-files/need_halloween_2026_end.sql). Safe while servers run,
-- but the map server keeps online characters in memory, so the numbers are exact
-- only with the servers stopped.

SELECT 'inventory' AS `place`, COUNT(*) AS `rows`, COALESCE(SUM(`amount`), 0) AS `coins`, COUNT(DISTINCT `char_id`) AS `owners` FROM `inventory` WHERE `nameid` = 6658
UNION ALL SELECT 'cart_inventory', COUNT(*), COALESCE(SUM(`amount`), 0), COUNT(DISTINCT `char_id`) FROM `cart_inventory` WHERE `nameid` = 6658
UNION ALL SELECT 'storage', COUNT(*), COALESCE(SUM(`amount`), 0), COUNT(DISTINCT `account_id`) FROM `storage` WHERE `nameid` = 6658
UNION ALL SELECT 'storage2', COUNT(*), COALESCE(SUM(`amount`), 0), COUNT(DISTINCT `account_id`) FROM `storage2` WHERE `nameid` = 6658
UNION ALL SELECT 'storage3', COUNT(*), COALESCE(SUM(`amount`), 0), COUNT(DISTINCT `account_id`) FROM `storage3` WHERE `nameid` = 6658
UNION ALL SELECT 'guild_storage', COUNT(*), COALESCE(SUM(`amount`), 0), COUNT(DISTINCT `guild_id`) FROM `guild_storage` WHERE `nameid` = 6658
UNION ALL SELECT 'mail_attachments', COUNT(*), COALESCE(SUM(`amount`), 0), COUNT(DISTINCT `id`) FROM `mail_attachments` WHERE `nameid` = 6658
UNION ALL SELECT 'auction', COUNT(*), COALESCE(SUM(`amount`), 0), COUNT(DISTINCT `seller_id`) FROM `auction` WHERE `nameid` = 6658;

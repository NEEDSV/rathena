-- TEST DATABASE ONLY. Review the selected database before running this file.
-- These statements are read-only EXPLAIN examples and are not production migrations.
-- Replace the sample IDs/text with representative values from a test database.

-- Public job list.
EXPLAIN
SELECT DISTINCT `job_id`
FROM `need_equipment_build`
WHERE `status`=1
ORDER BY `job_id`;

-- Public builds, most-liked order (page size 10 plus one has-more row).
EXPLAIN
SELECT `build_id`,`job_id`,`category`,`status`,`title`,`like_count`
FROM `need_equipment_build`
WHERE `status`=1 AND `job_id`=1 AND `category`=1
ORDER BY `like_count` DESC,`approved_at` DESC,`build_id` DESC
LIMIT 11 OFFSET 0;

-- Public builds, newest order.
EXPLAIN
SELECT `build_id`,`job_id`,`category`,`status`,`title`,`like_count`
FROM `need_equipment_build`
WHERE `status`=1 AND `job_id`=1 AND `category`=1
ORDER BY `approved_at` DESC,`build_id` DESC
LIMIT 11 OFFSET 0;

-- Current character's registrations.
EXPLAIN
SELECT `build_id`,`job_id`,`category`,`status`,`title`,`like_count`
FROM `need_equipment_build`
WHERE `account_id`=2000000 AND `char_id`=150000
ORDER BY `build_id` DESC
LIMIT 11 OFFSET 0;

-- Current character's claimable rewards.
EXPLAIN
SELECT `build_id`,`title`,`reward_item_id`,`reward_amount`
FROM `need_equipment_build`
WHERE `account_id`=2000000 AND `char_id`=150000 AND `status`=1
  AND `reward_eligible`=1 AND `reward_claimed`=0 AND `reward_claim_status`=0
  AND `reward_item_id`>0 AND `reward_amount`>0
ORDER BY `build_id` DESC
LIMIT 11 OFFSET 0;

-- Administrator exact character-name search.
EXPLAIN
SELECT `build_id`,`job_id`,`category`,`status`,`title`,`like_count`,`char_name`
FROM `need_equipment_build`
WHERE `char_name`='SampleCharacter'
ORDER BY `build_id` DESC
LIMIT 11 OFFSET 0;

-- All stored equipment rows for one build; one query, not N+1.
EXPLAIN
SELECT `build_item_id`,`equip_position`,`item_id`,`refine`,`grade`,`attribute`,`identify`,
  `card_0`,`card_1`,`card_2`,`card_3`,
  `random_option_0`,`random_option_value_0`,`random_option_param_0`,
  `random_option_1`,`random_option_value_1`,`random_option_param_1`,
  `random_option_2`,`random_option_value_2`,`random_option_param_2`,
  `random_option_3`,`random_option_value_3`,`random_option_param_3`,
  `random_option_4`,`random_option_value_4`,`random_option_param_4`
FROM `need_equipment_build_item`
WHERE `build_id`=1
ORDER BY `equip_position`,`build_item_id`;

-- Public detail plus current account's like state.
EXPLAIN
SELECT b.`job_id`,b.`base_level`,b.`job_level`,b.`category`,b.`title`,b.`description`,
  b.`str`,b.`agi`,b.`vit`,b.`int`,b.`dex`,b.`luk`,b.`created_at`,b.`like_count`,
  (b.`account_id`=2000000),
  EXISTS(SELECT 1 FROM `need_equipment_build_like` l WHERE l.`build_id`=b.`build_id` AND l.`account_id`=2000000)
FROM `need_equipment_build` b
WHERE b.`build_id`=1 AND b.`status`=1
LIMIT 1;

-- V20-MOCK synthetic rows for the isolated ragnarok_mbqa schema ONLY (after bc_synth.sql). Applied as root: it also
-- creates the mock balance tables, which exist in no other database (the mock writer refuses outside ragnarok_mbqa).
-- {{user}} comes from secrets.json at run time; this file holds no secret.
--   mbqa_cpa : normal player (group 0), two characters - rolls on the real board for real CP decisions, CP-1..CP-5
--   mbqa_cpb : normal player (group 0) - another account (CP-6)
-- The GM harness (@mbqacpapply <char> <payout_no>) applies a payout for the named online character.
INSERT INTO `login` (`account_id`, `userid`, `user_pass`, `sex`, `email`, `group_id`) VALUES
 (2000941, 'mbqa_cpa', '{{user}}', 'M', 'a@a.com', 0),
 (2000942, 'mbqa_cpb', '{{user}}', 'M', 'a@a.com', 0);

INSERT INTO `char` (`char_id`, `account_id`, `char_num`, `name`, `class`, `base_level`, `job_level`,
                    `str`, `agi`, `vit`, `int`, `dex`, `luk`, `max_hp`, `hp`, `max_sp`, `sp`, `hair`, `sex`,
                    `party_id`, `last_map`, `last_x`, `last_y`, `save_map`, `save_x`, `save_y`) VALUES
 (150951, 2000941, 0, 'MBQA_CPA1', 0, 30, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 0, 'prontera', 156, 191, 'prontera', 156, 191),
 (150952, 2000941, 1, 'MBQA_CPA2', 0, 30, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 0, 'prontera', 156, 191, 'prontera', 156, 191),
 (150953, 2000942, 0, 'MBQA_CPB',  0, 30, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 0, 'prontera', 156, 191, 'prontera', 156, 191);

-- Mock "cash" of the test build writer: balance per account and one row per call that changed it.
CREATE TABLE IF NOT EXISTS `mbqa_cp_mock_balance` (
  `account_id` INT UNSIGNED NOT NULL,
  `balance` BIGINT NOT NULL DEFAULT 0,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `mbqa_cp_mock_op` (
  `op_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id` INT UNSIGNED NOT NULL,
  `payout_no` BIGINT UNSIGNED NOT NULL,
  `char_id` INT UNSIGNED NOT NULL,
  `amount` INT NOT NULL,
  `mode` TINYINT UNSIGNED NOT NULL,
  `created_at` DATETIME NOT NULL,
  PRIMARY KEY (`op_id`)
) ENGINE=InnoDB;

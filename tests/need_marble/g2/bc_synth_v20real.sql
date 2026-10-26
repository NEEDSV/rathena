-- V20-REAL synthetic rows for the isolated ragnarok_mbqa schema ONLY (after bc_synth.sql). Applied as root: it also
-- creates mbqa_cp_fault, which exists in no other database (the char-server test hooks read it only there).
-- {{user}} comes from secrets.json at run time; this file holds no secret.
--   mbqa_cpr : normal player (group 0), two characters, #CASHPOINTS 1000 at the start - R1..R7 on the real char path
--   mbqa_cps : normal player (group 0), no #CASHPOINTS row at all - a first grant must read the balance as 0
--   mbqa_cpt : normal player (group 0), #CASHPOINTS 777, never logs in - must stay untouched
USE `ragnarok_mbqa`;

INSERT INTO `login` (`account_id`, `userid`, `user_pass`, `sex`, `email`, `group_id`) VALUES
 (2000943, 'mbqa_cpr', '{{user}}', 'M', 'a@a.com', 0),
 (2000944, 'mbqa_cps', '{{user}}', 'M', 'a@a.com', 0),
 (2000945, 'mbqa_cpt', '{{user}}', 'M', 'a@a.com', 0);

INSERT INTO `char` (`char_id`, `account_id`, `char_num`, `name`, `class`, `base_level`, `job_level`,
                    `str`, `agi`, `vit`, `int`, `dex`, `luk`, `max_hp`, `hp`, `max_sp`, `sp`, `hair`, `sex`,
                    `party_id`, `last_map`, `last_x`, `last_y`, `save_map`, `save_x`, `save_y`) VALUES
 (150954, 2000943, 0, 'MBQA_CPR1', 0, 30, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 0, 'prontera', 156, 191, 'prontera', 156, 191),
 (150955, 2000943, 1, 'MBQA_CPR2', 0, 30, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 0, 'prontera', 156, 191, 'prontera', 156, 191),
 (150956, 2000944, 0, 'MBQA_CPS',  0, 30, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 0, 'prontera', 156, 191, 'prontera', 156, 191),
 (150957, 2000945, 0, 'MBQA_CPT',  0, 30, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 0, 'prontera', 156, 191, 'prontera', 156, 191);

INSERT INTO `acc_reg_num` (`account_id`, `key`, `index`, `value`) VALUES
 (2000943, '#CASHPOINTS', 0, 1000),
 (2000945, '#CASHPOINTS', 0, 777);

-- One shot char-server fault of the test build (need_marble_test_char.inc): 1 stop before COMMIT,
-- 2 stop after COMMIT before the answer, 3 commit and never answer.
CREATE TABLE IF NOT EXISTS `mbqa_cp_fault` (
  `id` TINYINT UNSIGNED NOT NULL,
  `mode` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;
INSERT INTO `mbqa_cp_fault` (`id`, `mode`) VALUES (1, 0);

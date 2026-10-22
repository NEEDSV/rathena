-- G2-A synthetic rows for the isolated ragnarok_mbqa schema ONLY (after bc_synth.sql, bc_synth_g1b.sql, bc_synth_v22.sql).
-- {{user}} comes from secrets.json at run time; this file holds no secret.

-- Test commands (@mbqakill etc.) need group 99: the V18-C captcha dealer MBQA_U1 and party member MBQA_P2.
UPDATE `login` SET `group_id` = 99 WHERE `account_id` IN (2000902, 2000903);
-- Level 99 everywhere so yuno_fild02 mobs stay inside +-30 (bc_synth.sql creates level 30 novices).
UPDATE `char` SET `base_level` = 99, `base_exp` = 0 WHERE `account_id` BETWEEN 2000901 AND 2000906;

INSERT INTO `login` (`account_id`, `userid`, `user_pass`, `sex`, `email`, `group_id`) VALUES
 (2000909, 'mbqa_v11',  '{{user}}', 'M', 'a@a.com', 0),    -- V11 1,000 rolls only
 (2000910, 'mbqa_heal', '{{user}}', 'M', 'a@a.com', 0);    -- V18-C healer (no attack, heal only)

-- MBQA_HEAL: acolyte (class 4) level 99 in the V18 party, AL_HEAL (28) level 10, enough SP for the whole run.
INSERT INTO `char` (`char_id`, `account_id`, `char_num`, `name`, `class`, `base_level`, `job_level`,
                    `str`, `agi`, `vit`, `int`, `dex`, `luk`, `max_hp`, `hp`, `max_sp`, `sp`, `hair`, `sex`,
                    `party_id`, `last_map`, `last_x`, `last_y`, `save_map`, `save_x`, `save_y`) VALUES
 (150911, 2000909, 0, 'MBQA_V11',  0, 30, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 0, 'prontera', 156, 191, 'prontera', 156, 191),
 (150912, 2000910, 0, 'MBQA_HEAL', 4, 99, 50, 50, 50, 50, 99, 50, 1, 9000, 9000, 9000, 9000, 1, 'M', 1, 'prontera', 156, 191, 'prontera', 156, 191);
INSERT INTO `skill` (`char_id`, `id`, `lv`, `flag`) VALUES (150912, 28, 10, 0);

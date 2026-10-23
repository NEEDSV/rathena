-- G2-B dedicated synthetic accounts for the isolated ragnarok_mbqa schema ONLY (after bc_synth.sql).
-- {{user}} comes from secrets.json at run time; this file holds no secret.
--   mbqa_v16 : V16 fault points / re-entry / COMMIT outcome / lock wait
--   mbqa_v21 : V21 batches over three logical days, character A (slot 0) and B (slot 1)
--   mbqa_v24 : V24 04:00 KST boundaries, character A (slot 0) and B (slot 1)
-- Group 99: the test-build commands refuse anyone else. Level 99: yuno_fild02 mobs stay inside +-30.
INSERT INTO `login` (`account_id`, `userid`, `user_pass`, `sex`, `email`, `group_id`) VALUES
 (2000911, 'mbqa_v16', '{{user}}', 'M', 'a@a.com', 99),
 (2000912, 'mbqa_v21', '{{user}}', 'M', 'a@a.com', 99),
 (2000913, 'mbqa_v24', '{{user}}', 'M', 'a@a.com', 99);

INSERT INTO `char` (`char_id`, `account_id`, `char_num`, `name`, `class`, `base_level`, `job_level`,
                    `str`, `agi`, `vit`, `int`, `dex`, `luk`, `max_hp`, `hp`, `max_sp`, `sp`, `hair`, `sex`,
                    `last_map`, `last_x`, `last_y`, `save_map`, `save_x`, `save_y`) VALUES
 (150913, 2000911, 0, 'MBQA_V16',  0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150914, 2000912, 0, 'MBQA_V21A', 0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150915, 2000912, 1, 'MBQA_V21B', 0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150916, 2000913, 0, 'MBQA_V24A', 0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150917, 2000913, 1, 'MBQA_V24B', 0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191);

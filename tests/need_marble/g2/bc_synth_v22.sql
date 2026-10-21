-- V22 dedicated synthetic accounts for the isolated ragnarok_mbqa schema ONLY (after bc_synth.sql).
-- No other scenario logs these accounts in, so V22 never inherits kills, batches or credits from R1..R4.
-- {{user}} comes from secrets.json at run time; this file holds no secret.
--   mbqa_v22a: fixture 1 (clean account and day), then fixture 3 (already capped, relog + more kills)
--   mbqa_v22b: fixture 2 (one real kill and one batch before the feed)
-- Group 99: the test-build feed / kill commands refuse anyone else. No party (party rule off for the feed).
INSERT INTO `login` (`account_id`, `userid`, `user_pass`, `sex`, `email`, `group_id`) VALUES
 (2000907, 'mbqa_v22a', '{{user}}', 'M', 'a@a.com', 99),
 (2000908, 'mbqa_v22b', '{{user}}', 'M', 'a@a.com', 99);

-- Level 99 novices: yuno_fild02 mobs stay inside +-30 and one kill cannot change the level.
INSERT INTO `char` (`char_id`, `account_id`, `char_num`, `name`, `class`, `base_level`, `job_level`,
                    `str`, `agi`, `vit`, `int`, `dex`, `luk`, `max_hp`, `hp`, `max_sp`, `sp`, `hair`, `sex`,
                    `last_map`, `last_x`, `last_y`, `save_map`, `save_x`, `save_y`) VALUES
 (150909, 2000907, 0, 'MBQA_V22A', 0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150910, 2000908, 0, 'MBQA_V22B', 0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191);

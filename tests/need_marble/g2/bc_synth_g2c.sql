-- G2-C dedicated synthetic accounts for the isolated ragnarok_mbqa schema ONLY (after bc_synth.sql).
-- {{user}} comes from secrets.json at run time; this file holds no secret.
-- Group 99: the harness commands (@mbqabg*, @mbqafeed, @mbqainst) refuse anyone else.
--   mbqa_bga    : two characters (slot 0 / 1) - account sharing, character change, BG daily cap
--   mbqa_bgb    : plain attacker (walks)
--   mbqa_bgheal : acolyte, heal only, never attacks (C-02)
--   mbqa_bgafk  : idle the whole match (C-03)
--   mbqa_bgleave: leaves the match and does not come back (C-03)
--   mbqa_bgrelog: short disconnect during the match (C-03 false positive check)
--   mbqa_bgdef  : moves once, then stands still defending (C-03 false positive check)
--   mbqa_bgdead : active, dead at the end of the match (C-03 false positive check)
--   mbqa_bgc    : hunt 100 + instance 100 first, then a match (C-05 total cap)
--   mbqa_bgclk  : 04:00 KST boundary (C-06)
INSERT INTO `login` (`account_id`, `userid`, `user_pass`, `sex`, `email`, `group_id`) VALUES
 (2000921, 'mbqa_bga',     '{{user}}', 'M', 'a@a.com', 99),
 (2000922, 'mbqa_bgb',     '{{user}}', 'M', 'a@a.com', 99),
 (2000923, 'mbqa_bgheal',  '{{user}}', 'M', 'a@a.com', 99),
 (2000924, 'mbqa_bgafk',   '{{user}}', 'M', 'a@a.com', 99),
 (2000925, 'mbqa_bgleave', '{{user}}', 'M', 'a@a.com', 99),
 (2000926, 'mbqa_bgrelog', '{{user}}', 'M', 'a@a.com', 99),
 (2000927, 'mbqa_bgdef',   '{{user}}', 'M', 'a@a.com', 99),
 (2000928, 'mbqa_bgdead',  '{{user}}', 'M', 'a@a.com', 99),
 (2000929, 'mbqa_bgc',     '{{user}}', 'M', 'a@a.com', 99),
 (2000930, 'mbqa_bgclk',   '{{user}}', 'M', 'a@a.com', 99);

INSERT INTO `char` (`char_id`, `account_id`, `char_num`, `name`, `class`, `base_level`, `job_level`,
                    `str`, `agi`, `vit`, `int`, `dex`, `luk`, `max_hp`, `hp`, `max_sp`, `sp`, `hair`, `sex`,
                    `last_map`, `last_x`, `last_y`, `save_map`, `save_x`, `save_y`) VALUES
 (150931, 2000921, 0, 'MBQA_BGA1',   0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150932, 2000921, 1, 'MBQA_BGA2',   0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150933, 2000922, 0, 'MBQA_BGB',    0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150934, 2000923, 0, 'MBQA_BGHEAL', 4, 99, 50, 50, 50, 50, 99, 50, 1, 9000, 9000, 9000, 9000, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150935, 2000924, 0, 'MBQA_BGAFK',  0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150936, 2000925, 0, 'MBQA_BGLEAVE', 0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150937, 2000926, 0, 'MBQA_BGRELOG', 0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150938, 2000927, 0, 'MBQA_BGDEF',  0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150939, 2000928, 0, 'MBQA_BGDEAD', 0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150940, 2000929, 0, 'MBQA_BGC',    0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150941, 2000930, 0, 'MBQA_BGCLK',  0, 99, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191);
-- MBQA_BGHEAL: AL_HEAL (28) level 10, enough SP for the whole run.
INSERT INTO `skill` (`char_id`, `id`, `lv`, `flag`) VALUES (150934, 28, 10, 0);

-- Isolated policy only (operating defaults: active window 60 s, sample every 30 s): an 8 s activity window sampled
-- every 3 s, so one test match of about 20 s carries several samples. No new BG rule is added by these rows.
INSERT INTO `need_marble_policy` (`policy_key`, `effective_day`, `value`, `changed_by`, `changed_at`) VALUES
 (41, 1, 8, 'g2c_isolated', NOW()),
 (42, 1, 3, 'g2c_isolated', NOW());

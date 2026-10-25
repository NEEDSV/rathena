-- BG auto reward fix (BG-A): precondition of a real random match player for the G2-C accounts (after bc_synth_g2c.sql).
-- @전장매칭 / queue / join all refuse a character without #NEED_BG_RULE_AGREE (bg_random_match_addon_full_cycle.txt),
-- so every real participant agreed once in bat_room. Without it the agreement dialog (OnTouch at bat_room 154,149)
-- opens on the warp back to bat_room after the reward and blocks the next harness command for 60 s.
INSERT INTO `acc_reg_num` (`account_id`, `key`, `index`, `value`) VALUES
 (2000921, '#NEED_BG_RULE_AGREE', 0, 1), (2000922, '#NEED_BG_RULE_AGREE', 0, 1), (2000923, '#NEED_BG_RULE_AGREE', 0, 1),
 (2000924, '#NEED_BG_RULE_AGREE', 0, 1), (2000925, '#NEED_BG_RULE_AGREE', 0, 1), (2000926, '#NEED_BG_RULE_AGREE', 0, 1),
 (2000927, '#NEED_BG_RULE_AGREE', 0, 1), (2000928, '#NEED_BG_RULE_AGREE', 0, 1), (2000929, '#NEED_BG_RULE_AGREE', 0, 1),
 (2000930, '#NEED_BG_RULE_AGREE', 0, 1);

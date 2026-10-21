-- G-1(b) synthetic additions for the isolated ragnarok_mbqa schema ONLY (after bc_synth.sql).
-- mbqa_cap becomes group 99 so the test-build feed command accepts it (V22).
UPDATE `login` SET `group_id` = 99 WHERE `account_id` = 2000906;

-- One party for V18: dealer MBQA_GM1 (leader), MBQA_P2 (active, near), MBQA_P3 (idle, near), MBQA_P4 (far).
INSERT INTO `party` (`party_id`, `name`, `exp`, `item`, `leader_id`, `leader_char`)
VALUES (1, 'MBQA_PARTY', 0, 0, 2000901, 150901);
UPDATE `char` SET `party_id` = 1 WHERE `char_id` IN (150901, 150905, 150906, 150907);

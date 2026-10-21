-- F1/F2 retest synthetic setup (isolated ragnarok_mbqa ONLY), applied after bc_synth.sql and bc_synth_g1b.sql.
-- Test commands need group 99; the marble rules have no group condition.
UPDATE `login` SET `group_id` = 99 WHERE `account_id` IN (2000902, 2000903, 2000904, 2000905, 2000906);

-- Default: max novice level 99 (no level drift while hunting yuno_fild02).
UPDATE `char` SET `base_level` = 99, `base_exp` = 0 WHERE `account_id` BETWEEN 2000901 AND 2000906;

-- R1: MBQA_CAP one exp point below level 42 (novice table: level 41 needs 29175). A level 11 hornet is 30 below.
UPDATE `char` SET `base_level` = 41, `base_exp` = 29174 WHERE `char_id` = 150908;

-- R2: dealer MBQA_GM1 level 30 (no level up from one kill), party member MBQA_P2 one exp point below level 42.
UPDATE `char` SET `base_level` = 30, `base_exp` = 0 WHERE `char_id` = 150901;
UPDATE `char` SET `base_level` = 41, `base_exp` = 29174 WHERE `char_id` = 150905;
UPDATE `party` SET `exp` = 1 WHERE `party_id` = 1;   -- even share, so P2 levels up from the dealer's kill

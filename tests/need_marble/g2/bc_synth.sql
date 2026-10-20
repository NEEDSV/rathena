-- G2 synthetic rows for the isolated ragnarok_mbqa schema ONLY (bcdb.py file ddl bc_synth.sql).
-- {{...}} placeholders come from secrets.json at run time; this file holds no secret.

UPDATE `login` SET `userid` = '{{srv_user}}', `user_pass` = '{{srv}}' WHERE `account_id` = 1 AND `sex` = 'S';

INSERT INTO `login` (`account_id`, `userid`, `user_pass`, `sex`, `email`, `group_id`) VALUES
 (2000901, 'mbqa_gm',   '{{gm}}',   'M', 'a@a.com', 99),
 (2000902, 'mbqa_user', '{{user}}', 'M', 'a@a.com', 0),
 (2000903, 'mbqa_p2',   '{{user}}', 'M', 'a@a.com', 0),
 (2000904, 'mbqa_p3',   '{{user}}', 'M', 'a@a.com', 0),
 (2000905, 'mbqa_p4',   '{{user}}', 'M', 'a@a.com', 0),
 (2000906, 'mbqa_cap',  '{{user}}', 'M', 'a@a.com', 0);

-- Base level 30 novices. Hunting field prt_fild08 (mob levels well inside +-30). No items, no zeny.
INSERT INTO `char` (`char_id`, `account_id`, `char_num`, `name`, `class`, `base_level`, `job_level`,
                    `str`, `agi`, `vit`, `int`, `dex`, `luk`, `max_hp`, `hp`, `max_sp`, `sp`, `hair`, `sex`,
                    `last_map`, `last_x`, `last_y`, `save_map`, `save_x`, `save_y`) VALUES
 (150901, 2000901, 0, 'MBQA_GM1', 0, 30, 10, 99, 99, 99, 1, 99, 1, 9000, 9000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150902, 2000901, 1, 'MBQA_GM2', 0, 30, 10, 99, 99, 99, 1, 99, 1, 9000, 9000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150903, 2000902, 0, 'MBQA_U1',  0, 30, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150904, 2000902, 1, 'MBQA_U2',  0, 30, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150905, 2000903, 0, 'MBQA_P2',  0, 30, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150906, 2000904, 0, 'MBQA_P3',  0, 30, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150907, 2000905, 0, 'MBQA_P4',  0, 30, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191),
 (150908, 2000906, 0, 'MBQA_CAP', 0, 30, 10, 50, 50, 50, 1, 50, 1, 3000, 3000, 100, 100, 1, 'M', 'prontera', 156, 191, 'prontera', 156, 191);

CREATE TABLE `mbqa_instance` (`token` VARCHAR(64) NOT NULL) ENGINE=InnoDB;
INSERT INTO `mbqa_instance` (`token`) VALUES ('{{instance_token}}');

-- Marble policy for the isolated test day only (approved F-4): master switch on from day 0.
INSERT INTO `need_marble_policy` (`policy_key`, `effective_day`, `value`, `changed_by`, `changed_at`)
VALUES (1, 1, 1, 'g2_isolated', NOW());

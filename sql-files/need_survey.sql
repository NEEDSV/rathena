-- NEED in-game survey (npc/NEED/need_survey.txt)
-- Import into the main map-server database before enabling the survey.
--
-- A survey is identified only by `survey_id` (.SurveyID in the script).
-- Changing .SurveyID starts a brand new survey: every check below is keyed by
-- (survey_id, account) and (survey_id, ip), so past participants may answer
-- the new survey again.
--
-- need_survey_complete
--   One row per finished survey. The two keys are the database level guard
--   against duplicate participation / duplicate rewards:
--     PRIMARY KEY (survey_id, account_id)  -> one completion per account
--     UNIQUE      (survey_id, ip_address)  -> one completion per client IP
--   The script inserts the row BEFORE giving the reward, and gives the reward
--   only when it can read back its own row. reward_claimed is set to 1 right
--   after the items are handed out, so reward_claimed = 0 means
--   "completed, but the reward was not confirmed" (e.g. map-server crash in
--   between) and should be checked by an operator.
--
-- need_survey_answer
--   One row per (survey, account, question). Answers are saved as soon as the
--   player confirms each question so an interrupted survey can be resumed.
--   Rows of accounts that never completed the survey stay here but are NOT
--   counted - always join need_survey_complete (the views below do).
--   question_text / selected_text are snapshots of the script at answer time,
--   so old surveys stay readable after the script is edited.
--
-- Charset: the map-server talks to MySQL with default_codepage (euckr) and
-- MySQL converts to the table charset, so Korean text is stored as utf8mb4.

CREATE TABLE IF NOT EXISTS `need_survey_complete` (
  `survey_id` int unsigned NOT NULL,
  `account_id` int unsigned NOT NULL,
  `char_id` int unsigned NOT NULL,
  `char_name` varchar(24) NOT NULL DEFAULT '',
  `ip_address` varchar(45) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `completed` tinyint unsigned NOT NULL DEFAULT 1,
  `reward_claimed` tinyint unsigned NOT NULL DEFAULT 0,
  `completed_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `reward_claimed_at` datetime NULL DEFAULT NULL,
  PRIMARY KEY (`survey_id`,`account_id`),
  UNIQUE KEY `survey_ip` (`survey_id`,`ip_address`),
  KEY `completed_at` (`completed_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `need_survey_answer` (
  `survey_id` int unsigned NOT NULL,
  `account_id` int unsigned NOT NULL,
  `question_id` smallint unsigned NOT NULL,
  `char_id` int unsigned NOT NULL,
  `char_name` varchar(24) NOT NULL DEFAULT '',
  `ip_address` varchar(45) CHARACTER SET ascii COLLATE ascii_general_ci NOT NULL,
  `question_text` varchar(255) NOT NULL DEFAULT '',
  `selected_answer` tinyint unsigned NOT NULL,
  `selected_text` varchar(100) NOT NULL DEFAULT '',
  `reason` varchar(255) NOT NULL,
  `answered_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`survey_id`,`account_id`,`question_id`),
  KEY `survey_question` (`survey_id`,`question_id`,`selected_answer`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ---------------------------------------------------------------------------
-- Operator views (completed participants only)
--   SELECT * FROM need_survey_summary_v;
--   SELECT * FROM need_survey_stat_v   WHERE survey_id = 1;
--   SELECT * FROM need_survey_reason_v WHERE survey_id = 1 AND question_id = 1;
--   SELECT * FROM need_survey_complete WHERE survey_id = 1 AND reward_claimed = 0;
-- The same numbers are available in game with @surveygm (GM 60+).
-- ---------------------------------------------------------------------------

CREATE OR REPLACE SQL SECURITY INVOKER VIEW `need_survey_summary_v` AS
SELECT
  c.`survey_id`,
  COUNT(*) AS `participants`,
  SUM(c.`reward_claimed` = 1) AS `reward_claimed`,
  SUM(c.`reward_claimed` = 0) AS `reward_unconfirmed`,
  MIN(c.`completed_at`) AS `first_completed_at`,
  MAX(c.`completed_at`) AS `last_completed_at`
FROM `need_survey_complete` c
WHERE c.`completed` = 1
GROUP BY c.`survey_id`;

CREATE OR REPLACE SQL SECURITY INVOKER VIEW `need_survey_stat_v` AS
SELECT
  a.`survey_id`,
  a.`question_id`,
  MAX(a.`question_text`) AS `question_text`,
  a.`selected_answer`,
  MAX(a.`selected_text`) AS `selected_text`,
  COUNT(*) AS `respondents`
FROM `need_survey_answer` a
JOIN `need_survey_complete` c
  ON c.`survey_id` = a.`survey_id` AND c.`account_id` = a.`account_id` AND c.`completed` = 1
GROUP BY a.`survey_id`, a.`question_id`, a.`selected_answer`;

CREATE OR REPLACE SQL SECURITY INVOKER VIEW `need_survey_reason_v` AS
SELECT
  a.`survey_id`,
  a.`question_id`,
  a.`question_text`,
  a.`selected_answer`,
  a.`selected_text`,
  a.`reason`,
  a.`account_id`,
  a.`char_id`,
  a.`char_name`,
  c.`ip_address`,
  a.`answered_at`,
  c.`completed_at`
FROM `need_survey_answer` a
JOIN `need_survey_complete` c
  ON c.`survey_id` = a.`survey_id` AND c.`account_id` = a.`account_id` AND c.`completed` = 1;

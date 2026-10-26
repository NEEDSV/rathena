-- NEED marble board V20 prep - CP apply audit trail (DRAFT, NOT APPLIED)
--
-- Branch  : feature/need-marble-phase-bc-v1
-- Needs   : need_marble_phase_bc_v1.sql (need_marble_cp_payout already holds status 1..5, granted,
--           applied_at, operator_ticket; no existing table is altered).
-- Status  : migration file only, isolated test DB only. need_marble_cp_apply stays DISABLED without this
--           table, and also without a registered CP writer (none in the operating build) or with policy key 5 = 0.
-- Rollback: DROP TABLE `need_marble_cp_audit`;  (nothing else references it)
--
-- One row per status change of a CP payout: the claim before the writer runs (1 -> 3), the writer's answer
-- (3 -> 2 applied, 3 -> 1 confirmed no effect, 3 -> 3 unknown) and the operator's reconciliation (3 -> 4 / 5).

CREATE TABLE IF NOT EXISTS `need_marble_cp_audit` (
  `audit_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id` INT UNSIGNED NOT NULL,
  `payout_no` BIGINT UNSIGNED NOT NULL,
  `from_status` TINYINT UNSIGNED NOT NULL,
  `to_status` TINYINT UNSIGNED NOT NULL,
  `reason` TINYINT UNSIGNED NOT NULL,              -- 1 CLAIM 2 WRITER_APPLIED 3 WRITER_NO_EFFECT 4 WRITER_UNKNOWN 5 OPERATOR
                                                   -- 6 SENT_TO_CHAR 7 CHAR_UNPROVEN 8 OPERATOR_REFUSED 9 LATE_ANSWER 10 VOID_SENT
  `amount` INT UNSIGNED NOT NULL,
  `char_id` INT UNSIGNED NOT NULL,
  `boot_id` INT UNSIGNED NOT NULL,
  `operator_ticket` BIGINT UNSIGNED NULL,
  `operator_aid` INT UNSIGNED NULL,                -- GM account of an operator row (reasons 5, 8, 10), else NULL
  `created_at` DATETIME NOT NULL,
  PRIMARY KEY (`audit_id`),
  KEY `k_payout` (`account_id`, `payout_no`)
) ENGINE=InnoDB;

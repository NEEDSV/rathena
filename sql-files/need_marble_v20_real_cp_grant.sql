-- NEED marble board V20-REAL - char-server CP grant (DRAFT, NOT APPLIED TO ANY OPERATING DB)
--
-- Branch  : feature/need-marble-phase-bc-v1
-- Needs   : need_marble_phase_bc_v1.sql, need_marble_v20_cp_apply.sql. Run on the CHAR DB (char_server_db).
--           The map-server reads need_marble_cp_grant only as operator evidence (map_server_db = same schema here).
-- Status  : migration file only. Applied to the isolated test DB ragnarok_mbqa (127.0.0.1:3327) only.
--           Operating use needs a separate approval: this migration, a NEED_MARBLE_CP_CHAR_WRITER=1 map-server build,
--           inter_athena.conf need_marble_cp_grant: yes and policy key 5 = 1. Any one missing = no CP is granted.
--
-- 1) acc_reg_num becomes InnoDB. The char-server grants only when the idempotency row and the new #CASHPOINTS
--    value commit in ONE transaction; MyISAM (rAthena main.sql default) ignores transactions, so the char-server
--    refuses every grant while acc_reg_num is not InnoDB. The ALTER copies the table and locks it while it runs:
--    run it during maintenance with the servers stopped, after a backup.
-- 2) need_marble_cp_grant: one row per key the char-server closed. state 1 = applied grant (its presence is the proof
--    the payout reached the balance, req_id the request that applied it); state 2 = VOID tombstone written for an
--    operator's @mbcp resolve void under the same key lock: every later request for that key is refused (GR_VOIDED).
--    Refused requests write nothing. (account_id, payout_no) is the idempotency key.
--
-- Rollback: DROP TABLE `need_marble_cp_grant`;  ALTER TABLE `acc_reg_num` ENGINE=MyISAM;
--           (only after every UNCERTAIN payout is reconciled: the rows are the evidence for MANUAL_APPLIED / VOID)

ALTER TABLE `acc_reg_num` ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `need_marble_cp_grant` (
  `account_id` INT UNSIGNED NOT NULL,
  `payout_no` BIGINT UNSIGNED NOT NULL,
  `req_id` BIGINT UNSIGNED NOT NULL,             -- map boot_id << 32 | request counter of that boot
  `amount` INT UNSIGNED NOT NULL,
  `char_id` INT UNSIGNED NOT NULL,
  `balance_before` BIGINT NOT NULL,              -- acc_reg_num #CASHPOINTS read FOR UPDATE in the same transaction
  `balance_after` BIGINT NOT NULL,
  `created_at` DATETIME NOT NULL,
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 1,     -- 1 APPLIED grant, 2 VOID tombstone (req_id 0, balances 0)
  `operator_aid` INT UNSIGNED NULL,                -- VOID: the GM account that asked for it
  `operator_ticket` BIGINT UNSIGNED NULL,          -- VOID: its ticket
  PRIMARY KEY (`account_id`, `payout_no`)
) ENGINE=InnoDB;

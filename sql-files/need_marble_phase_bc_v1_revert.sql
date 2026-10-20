-- NEED marble board Phase B/C ledger - revert (rollback) of need_marble_phase_bc_v1.sql (DRAFT, NOT APPLIED)
--
-- Preferred rollback is the switch, not this file:
--   INSERT INTO need_marble_policy (policy_key, effective_day, value, changed_by, changed_at)
--   VALUES (1, <logical day>, 0, '<operator>', NOW());
-- or remove the marble NPC from the load list. Both keep the ledger for audit.
--
-- This file deletes every marble ledger row. Back up these tables first and run it
-- only with an explicit operator approval. It touches no other table.

DROP TABLE IF EXISTS `need_marble_cp_payout`;
DROP TABLE IF EXISTS `need_marble_event`;
DROP TABLE IF EXISTS `need_marble_jump`;
DROP TABLE IF EXISTS `need_marble_roll`;
DROP TABLE IF EXISTS `need_marble_state`;
DROP TABLE IF EXISTS `need_marble_exchange_req`;
DROP TABLE IF EXISTS `need_marble_hunt_batch`;
DROP TABLE IF EXISTS `need_marble_hunt_progress`;
DROP TABLE IF EXISTS `need_marble_dungeon_daily`;
DROP TABLE IF EXISTS `need_marble_activity`;
DROP TABLE IF EXISTS `need_marble_period`;
DROP TABLE IF EXISTS `need_marble_wallet_tx`;
DROP TABLE IF EXISTS `need_marble_wallet`;
DROP TABLE IF EXISTS `need_marble_account_seq`;
DROP TABLE IF EXISTS `need_marble_boot`;
DROP TABLE IF EXISTS `need_marble_policy`;
DROP TABLE IF EXISTS `need_marble_season`;

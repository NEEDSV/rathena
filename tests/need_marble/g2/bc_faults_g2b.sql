-- G2-B V16 / V21 fault injection for the isolated ragnarok_mbqa schema ONLY (bcdb.py file root bc_faults_g2b.sql).
-- A fault is armed per account by a row in mbqa_fault (mbqa_rt inserts / deletes it); without a row the
-- triggers do nothing. The triggers SIGNAL on plain UPDATE statements only: INSERT IGNORE statements are not
-- used as fault points because IGNORE may turn the error into a warning and silently change the result.
-- Ledger order inside one hunt checkpoint (src/map/need_marble.cpp hunt_checkpoint / credit_in_tx):
--   progress INSERT IGNORE + SELECT FOR UPDATE -> batch INSERT IGNORE -> [per tier: activity INSERT IGNORE ->
--   period INSERT IGNORE + SELECT FOR UPDATE -> activity UPDATE -> wallet_tx INSERT -> wallet UPDATE ->
--   period UPDATE] -> progress UPDATE -> COMMIT
--   activity_update : after the batch row and the milestone row were written
--   wallet_update   : after the EARN ledger row was written
--   period_update   : after the wallet balance was raised
--   progress_update : last statement before COMMIT (after the period counters when a tier was crossed)

CREATE TABLE `mbqa_fault` (
  `point` VARCHAR(32) NOT NULL,
  `account_id` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`point`, `account_id`)
) ENGINE=InnoDB;

-- V16 deadlock attempt: rows written by the external transaction so InnoDB picks the checkpoint as the victim.
CREATE TABLE `mbqa_pad` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `v` INT NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

DELIMITER //

CREATE TRIGGER `mbqa_fault_progress_bu` BEFORE UPDATE ON `need_marble_hunt_progress` FOR EACH ROW
BEGIN
  IF EXISTS (SELECT 1 FROM `mbqa_fault` WHERE `point` = 'progress_update' AND `account_id` = NEW.`account_id`) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mbqa fault progress_update';
  END IF;
END//

CREATE TRIGGER `mbqa_fault_activity_bu` BEFORE UPDATE ON `need_marble_activity` FOR EACH ROW
BEGIN
  IF EXISTS (SELECT 1 FROM `mbqa_fault` WHERE `point` = 'activity_update' AND `account_id` = NEW.`account_id`) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mbqa fault activity_update';
  END IF;
END//

CREATE TRIGGER `mbqa_fault_wallet_bu` BEFORE UPDATE ON `need_marble_wallet` FOR EACH ROW
BEGIN
  IF EXISTS (SELECT 1 FROM `mbqa_fault` WHERE `point` = 'wallet_update' AND `account_id` = NEW.`account_id`) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mbqa fault wallet_update';
  END IF;
END//

CREATE TRIGGER `mbqa_fault_period_bu` BEFORE UPDATE ON `need_marble_period` FOR EACH ROW
BEGIN
  IF EXISTS (SELECT 1 FROM `mbqa_fault` WHERE `point` = 'period_update' AND `account_id` = NEW.`account_id`) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'mbqa fault period_update';
  END IF;
END//

DELIMITER ;

-- lock waits end after 10 s instead of 50 s (isolated instance only; sessions opened afterwards use it)
SET GLOBAL innodb_lock_wait_timeout = 10;

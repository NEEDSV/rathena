-- NEED 2026-10-13 의상 인챈트 스톤 세탁 악용 회수 (2/2 : 적용)
--
-- 대상 : need_elr_target 중 excluded = 0 AND status = 0 인 계정 (1/2 탐지에서 채움)
-- 회수 : 대상 계정의 모든 캐릭터(삭제 대기 포함) + 계정 창고 기준
--   (1) 인챈트 스톤 재료 아이템 전량 삭제 : 인벤토리, 카트, 계정 창고 1~3, 미수령 우편
--   (2) 의상 인챈트 스톤 상자 전량 삭제    : 같은 위치 (@recall_boxes = 0 이면 건너뛴다)
--   (3) 부여된 인챈트 삭제 : 같은 위치의 의상 아이템 card0~3 중 인챈트 스톤 카드인 슬롯을 0 으로
--       (이펙트 스톤 카드, 일반 장비의 인챈트는 건드리지 않는다)
--   (4) 옷장 스톤 세팅 삭제 : char_reg_num COSTUME_STONE_TOP/MID/LOW/GARMENT/DUAL
--   길드 창고는 다른 길드원 몫과 섞여 있어 지우지 않는다. 맨 아래 확인 쿼리로 따로 보여준다.
--
-- 바뀌는 행은 지우기 전에 need_elr_bak_* 에 원본 그대로 복사한다 (되돌리기용).
--
-- 순서
--   1) 점검 공지, login/char/map 서버 전부 종료
--      (온라인 캐릭터의 인벤토리·창고는 서버 메모리에 있다. 켠 채 실행하면 저장 때 되살아난다)
--   2) DB 백업
--   3) 1/2 탐지 실행 → 대상 검토 → 필요하면 excluded = 1
--   4) 이 파일 실행 (재실행해도 status = 1 인 계정은 다시 건드리지 않는다)

SET NAMES utf8mb4;

SET @recall_boxes := 1;  -- 1 = 미개봉 스톤 상자도 회수, 0 = 스톤/부여분만

-- ------------------------------------------------------------------
-- 0) 사전 확인 : 서버가 꺼져 있어야 한다 (0 이어야 정상)
-- ------------------------------------------------------------------
SELECT COUNT(*) AS `online_chars(0이어야 함)` FROM `char` WHERE `online` <> 0;

-- 서버가 켜져 있으면 아래 단계가 아무것도 하지 않도록 막는다
SET @go := (SELECT COUNT(*) = 0 FROM `char` WHERE `online` <> 0);

-- ------------------------------------------------------------------
-- 1) 이번 실행 대상
-- ------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `need_elr_char` (
  `char_id` int unsigned NOT NULL,
  `account_id` int unsigned NOT NULL,
  PRIMARY KEY (`char_id`),
  KEY `account_id` (`account_id`)
) ENGINE=InnoDB;
DELETE FROM `need_elr_char`;

CREATE TABLE IF NOT EXISTS `need_elr_acc` (
  `account_id` int unsigned NOT NULL,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB;
DELETE FROM `need_elr_acc`;

INSERT INTO `need_elr_acc` (`account_id`)
SELECT `account_id` FROM `need_elr_target`
WHERE @go AND `status` = 0 AND `excluded` = 0;

INSERT INTO `need_elr_char` (`char_id`,`account_id`)
SELECT c.char_id, c.account_id FROM `char` c JOIN `need_elr_acc` a ON a.account_id = c.account_id;

-- 회수 아이템(스톤 + 상자) / 인챈트 카드 / 의상
CREATE TABLE IF NOT EXISTS `need_elr_del_item` (
  `item_id` int unsigned NOT NULL,
  `kind` enum('stone','box') NOT NULL,
  PRIMARY KEY (`item_id`)
) ENGINE=InnoDB;
DELETE FROM `need_elr_del_item`;
INSERT INTO `need_elr_del_item` (`item_id`,`kind`)
SELECT `item_id`, `kind` FROM `need_elr_item`
WHERE `kind` = 'stone' OR (`kind` = 'box' AND @recall_boxes = 1);

CREATE TABLE IF NOT EXISTS `need_elr_card` (
  `item_id` int unsigned NOT NULL,
  PRIMARY KEY (`item_id`)
) ENGINE=InnoDB;
DELETE FROM `need_elr_card`;
INSERT INTO `need_elr_card` (`item_id`) SELECT `item_id` FROM `need_elr_item` WHERE `kind` = 'card';

CREATE TABLE IF NOT EXISTS `need_elr_costume` (
  `item_id` int unsigned NOT NULL,
  PRIMARY KEY (`item_id`)
) ENGINE=InnoDB;
DELETE FROM `need_elr_costume`;
INSERT INTO `need_elr_costume` (`item_id`) SELECT `item_id` FROM `need_elr_item` WHERE `kind` = 'costume';

-- ------------------------------------------------------------------
-- 2) 백업 (원본 행 그대로)
-- ------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `need_elr_bak_inventory` LIKE `inventory`;
CREATE TABLE IF NOT EXISTS `need_elr_bak_cart_inventory` LIKE `cart_inventory`;
CREATE TABLE IF NOT EXISTS `need_elr_bak_storage` LIKE `storage`;
CREATE TABLE IF NOT EXISTS `need_elr_bak_storage2` LIKE `storage2`;
CREATE TABLE IF NOT EXISTS `need_elr_bak_storage3` LIKE `storage3`;
CREATE TABLE IF NOT EXISTS `need_elr_bak_mail_attachments` LIKE `mail_attachments`;
CREATE TABLE IF NOT EXISTS `need_elr_bak_char_reg_num` LIKE `char_reg_num`;

-- 지울 아이템 행 + 인챈트를 지울 의상 행
INSERT IGNORE INTO `need_elr_bak_inventory`
SELECT t.* FROM `inventory` t JOIN `need_elr_char` c ON c.char_id = t.char_id
WHERE t.nameid IN (SELECT `item_id` FROM `need_elr_del_item`)
   OR (t.nameid IN (SELECT `item_id` FROM `need_elr_costume`)
       AND (t.card0 IN (SELECT `item_id` FROM `need_elr_card`) OR t.card1 IN (SELECT `item_id` FROM `need_elr_card`)
         OR t.card2 IN (SELECT `item_id` FROM `need_elr_card`) OR t.card3 IN (SELECT `item_id` FROM `need_elr_card`)));

INSERT IGNORE INTO `need_elr_bak_cart_inventory`
SELECT t.* FROM `cart_inventory` t JOIN `need_elr_char` c ON c.char_id = t.char_id
WHERE t.nameid IN (SELECT `item_id` FROM `need_elr_del_item`)
   OR (t.nameid IN (SELECT `item_id` FROM `need_elr_costume`)
       AND (t.card0 IN (SELECT `item_id` FROM `need_elr_card`) OR t.card1 IN (SELECT `item_id` FROM `need_elr_card`)
         OR t.card2 IN (SELECT `item_id` FROM `need_elr_card`) OR t.card3 IN (SELECT `item_id` FROM `need_elr_card`)));

INSERT IGNORE INTO `need_elr_bak_storage`
SELECT t.* FROM `storage` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
WHERE t.nameid IN (SELECT `item_id` FROM `need_elr_del_item`)
   OR (t.nameid IN (SELECT `item_id` FROM `need_elr_costume`)
       AND (t.card0 IN (SELECT `item_id` FROM `need_elr_card`) OR t.card1 IN (SELECT `item_id` FROM `need_elr_card`)
         OR t.card2 IN (SELECT `item_id` FROM `need_elr_card`) OR t.card3 IN (SELECT `item_id` FROM `need_elr_card`)));

INSERT IGNORE INTO `need_elr_bak_storage2`
SELECT t.* FROM `storage2` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
WHERE t.nameid IN (SELECT `item_id` FROM `need_elr_del_item`)
   OR (t.nameid IN (SELECT `item_id` FROM `need_elr_costume`)
       AND (t.card0 IN (SELECT `item_id` FROM `need_elr_card`) OR t.card1 IN (SELECT `item_id` FROM `need_elr_card`)
         OR t.card2 IN (SELECT `item_id` FROM `need_elr_card`) OR t.card3 IN (SELECT `item_id` FROM `need_elr_card`)));

INSERT IGNORE INTO `need_elr_bak_storage3`
SELECT t.* FROM `storage3` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
WHERE t.nameid IN (SELECT `item_id` FROM `need_elr_del_item`)
   OR (t.nameid IN (SELECT `item_id` FROM `need_elr_costume`)
       AND (t.card0 IN (SELECT `item_id` FROM `need_elr_card`) OR t.card1 IN (SELECT `item_id` FROM `need_elr_card`)
         OR t.card2 IN (SELECT `item_id` FROM `need_elr_card`) OR t.card3 IN (SELECT `item_id` FROM `need_elr_card`)));

INSERT IGNORE INTO `need_elr_bak_mail_attachments`
SELECT t.* FROM `mail_attachments` t JOIN `mail` m ON m.id = t.id JOIN `need_elr_char` c ON c.char_id = m.dest_id
WHERE t.nameid IN (SELECT `item_id` FROM `need_elr_del_item`)
   OR (t.nameid IN (SELECT `item_id` FROM `need_elr_costume`)
       AND (t.card0 IN (SELECT `item_id` FROM `need_elr_card`) OR t.card1 IN (SELECT `item_id` FROM `need_elr_card`)
         OR t.card2 IN (SELECT `item_id` FROM `need_elr_card`) OR t.card3 IN (SELECT `item_id` FROM `need_elr_card`)));

INSERT IGNORE INTO `need_elr_bak_char_reg_num`
SELECT g.* FROM `char_reg_num` g JOIN `need_elr_char` c ON c.char_id = g.char_id
WHERE g.`index` = 0 AND g.`value` <> 0
  AND g.`key` IN ('COSTUME_STONE_TOP','COSTUME_STONE_MID','COSTUME_STONE_LOW','COSTUME_STONE_GARMENT','COSTUME_STONE_DUAL');

-- ------------------------------------------------------------------
-- 3) 계정별 회수량 기록 (백업 테이블에서 이번 대상 계정분만 센다)
-- ------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `need_elr_tmp_cnt` (
  `account_id` int unsigned NOT NULL,
  `kind` varchar(8) NOT NULL,
  `cnt` int unsigned NOT NULL,
  KEY `account_id` (`account_id`)
) ENGINE=InnoDB;
DELETE FROM `need_elr_tmp_cnt`;

INSERT INTO `need_elr_tmp_cnt`
SELECT x.account_id, d.kind, SUM(x.amount)
FROM (
  SELECT c.account_id, t.nameid, t.amount FROM `need_elr_bak_inventory` t JOIN `need_elr_char` c ON c.char_id = t.char_id
  UNION ALL SELECT c.account_id, t.nameid, t.amount FROM `need_elr_bak_cart_inventory` t JOIN `need_elr_char` c ON c.char_id = t.char_id
  UNION ALL SELECT t.account_id, t.nameid, t.amount FROM `need_elr_bak_storage` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
  UNION ALL SELECT t.account_id, t.nameid, t.amount FROM `need_elr_bak_storage2` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
  UNION ALL SELECT t.account_id, t.nameid, t.amount FROM `need_elr_bak_storage3` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
  UNION ALL SELECT c.account_id, t.nameid, t.amount FROM `need_elr_bak_mail_attachments` t JOIN `mail` m ON m.id = t.id
            JOIN `need_elr_char` c ON c.char_id = m.dest_id
) x JOIN `need_elr_del_item` d ON d.item_id = x.nameid
GROUP BY x.account_id, d.kind;

INSERT INTO `need_elr_tmp_cnt`
SELECT x.account_id, 'card',
       SUM((x.card0 IN (SELECT `item_id` FROM `need_elr_card`)) + (x.card1 IN (SELECT `item_id` FROM `need_elr_card`))
         + (x.card2 IN (SELECT `item_id` FROM `need_elr_card`)) + (x.card3 IN (SELECT `item_id` FROM `need_elr_card`)))
FROM (
  SELECT c.account_id, t.nameid, t.card0, t.card1, t.card2, t.card3 FROM `need_elr_bak_inventory` t JOIN `need_elr_char` c ON c.char_id = t.char_id
  UNION ALL SELECT c.account_id, t.nameid, t.card0, t.card1, t.card2, t.card3 FROM `need_elr_bak_cart_inventory` t JOIN `need_elr_char` c ON c.char_id = t.char_id
  UNION ALL SELECT t.account_id, t.nameid, t.card0, t.card1, t.card2, t.card3 FROM `need_elr_bak_storage` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
  UNION ALL SELECT t.account_id, t.nameid, t.card0, t.card1, t.card2, t.card3 FROM `need_elr_bak_storage2` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
  UNION ALL SELECT t.account_id, t.nameid, t.card0, t.card1, t.card2, t.card3 FROM `need_elr_bak_storage3` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
  UNION ALL SELECT c.account_id, t.nameid, t.card0, t.card1, t.card2, t.card3 FROM `need_elr_bak_mail_attachments` t JOIN `mail` m ON m.id = t.id
            JOIN `need_elr_char` c ON c.char_id = m.dest_id
) x JOIN `need_elr_costume` k ON k.item_id = x.nameid
GROUP BY x.account_id;

INSERT INTO `need_elr_tmp_cnt`
SELECT c.account_id, 'closet', COUNT(*)
FROM `need_elr_bak_char_reg_num` g JOIN `need_elr_char` c ON c.char_id = g.char_id
GROUP BY c.account_id;

UPDATE `need_elr_target` t
JOIN `need_elr_acc` a ON a.account_id = t.account_id
SET t.removed_stone  = COALESCE((SELECT SUM(`cnt`) FROM `need_elr_tmp_cnt` x WHERE x.account_id = t.account_id AND x.kind = 'stone'), 0),
    t.removed_box    = COALESCE((SELECT SUM(`cnt`) FROM `need_elr_tmp_cnt` x WHERE x.account_id = t.account_id AND x.kind = 'box'), 0),
    t.stripped_card  = COALESCE((SELECT SUM(`cnt`) FROM `need_elr_tmp_cnt` x WHERE x.account_id = t.account_id AND x.kind = 'card'), 0),
    t.removed_closet = COALESCE((SELECT SUM(`cnt`) FROM `need_elr_tmp_cnt` x WHERE x.account_id = t.account_id AND x.kind = 'closet'), 0)
WHERE @go;

-- ------------------------------------------------------------------
-- 4) 의상에 부여된 인챈트 지우기 (인챈트 스톤 카드인 슬롯만 0)
-- ------------------------------------------------------------------
UPDATE `inventory` t JOIN `need_elr_char` c ON c.char_id = t.char_id
SET t.card0 = IF(t.card0 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card0),
    t.card1 = IF(t.card1 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card1),
    t.card2 = IF(t.card2 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card2),
    t.card3 = IF(t.card3 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card3)
WHERE @go AND t.nameid IN (SELECT `item_id` FROM `need_elr_costume`);

UPDATE `cart_inventory` t JOIN `need_elr_char` c ON c.char_id = t.char_id
SET t.card0 = IF(t.card0 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card0),
    t.card1 = IF(t.card1 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card1),
    t.card2 = IF(t.card2 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card2),
    t.card3 = IF(t.card3 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card3)
WHERE @go AND t.nameid IN (SELECT `item_id` FROM `need_elr_costume`);

UPDATE `storage` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
SET t.card0 = IF(t.card0 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card0),
    t.card1 = IF(t.card1 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card1),
    t.card2 = IF(t.card2 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card2),
    t.card3 = IF(t.card3 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card3)
WHERE @go AND t.nameid IN (SELECT `item_id` FROM `need_elr_costume`);

UPDATE `storage2` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
SET t.card0 = IF(t.card0 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card0),
    t.card1 = IF(t.card1 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card1),
    t.card2 = IF(t.card2 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card2),
    t.card3 = IF(t.card3 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card3)
WHERE @go AND t.nameid IN (SELECT `item_id` FROM `need_elr_costume`);

UPDATE `storage3` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
SET t.card0 = IF(t.card0 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card0),
    t.card1 = IF(t.card1 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card1),
    t.card2 = IF(t.card2 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card2),
    t.card3 = IF(t.card3 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card3)
WHERE @go AND t.nameid IN (SELECT `item_id` FROM `need_elr_costume`);

UPDATE `mail_attachments` t JOIN `mail` m ON m.id = t.id JOIN `need_elr_char` c ON c.char_id = m.dest_id
SET t.card0 = IF(t.card0 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card0),
    t.card1 = IF(t.card1 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card1),
    t.card2 = IF(t.card2 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card2),
    t.card3 = IF(t.card3 IN (SELECT `item_id` FROM `need_elr_card`), 0, t.card3)
WHERE @go AND t.nameid IN (SELECT `item_id` FROM `need_elr_costume`);

-- ------------------------------------------------------------------
-- 5) 인챈트 스톤 / 스톤 상자 삭제
-- ------------------------------------------------------------------
DELETE t FROM `inventory` t JOIN `need_elr_char` c ON c.char_id = t.char_id
WHERE @go AND t.nameid IN (SELECT `item_id` FROM `need_elr_del_item`);
DELETE t FROM `cart_inventory` t JOIN `need_elr_char` c ON c.char_id = t.char_id
WHERE @go AND t.nameid IN (SELECT `item_id` FROM `need_elr_del_item`);
DELETE t FROM `storage` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
WHERE @go AND t.nameid IN (SELECT `item_id` FROM `need_elr_del_item`);
DELETE t FROM `storage2` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
WHERE @go AND t.nameid IN (SELECT `item_id` FROM `need_elr_del_item`);
DELETE t FROM `storage3` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
WHERE @go AND t.nameid IN (SELECT `item_id` FROM `need_elr_del_item`);
DELETE t FROM `mail_attachments` t JOIN `mail` m ON m.id = t.id JOIN `need_elr_char` c ON c.char_id = m.dest_id
WHERE @go AND t.nameid IN (SELECT `item_id` FROM `need_elr_del_item`);

-- ------------------------------------------------------------------
-- 6) 옷장 스톤 세팅 삭제
-- ------------------------------------------------------------------
DELETE g FROM `char_reg_num` g JOIN `need_elr_char` c ON c.char_id = g.char_id
WHERE @go AND g.`index` = 0
  AND g.`key` IN ('COSTUME_STONE_TOP','COSTUME_STONE_MID','COSTUME_STONE_LOW','COSTUME_STONE_GARMENT','COSTUME_STONE_DUAL');

-- ------------------------------------------------------------------
-- 7) 완료 표시
-- ------------------------------------------------------------------
UPDATE `need_elr_target` t JOIN `need_elr_acc` a ON a.account_id = t.account_id
SET t.status = 1, t.applied_at = NOW()
WHERE @go;

-- ------------------------------------------------------------------
-- 8) 확인
-- ------------------------------------------------------------------
-- 대상 계정에 남은 스톤/상자 (0 이어야 정상)
SELECT COUNT(*) AS `남은_스톤상자_행(0)`
FROM (
  SELECT t.nameid FROM `inventory` t JOIN `char` c ON c.char_id = t.char_id
    JOIN `need_elr_target` g ON g.account_id = c.account_id AND g.status = 1 AND g.excluded = 0
  UNION ALL SELECT t.nameid FROM `cart_inventory` t JOIN `char` c ON c.char_id = t.char_id
    JOIN `need_elr_target` g ON g.account_id = c.account_id AND g.status = 1 AND g.excluded = 0
  UNION ALL SELECT t.nameid FROM `storage` t JOIN `need_elr_target` g ON g.account_id = t.account_id AND g.status = 1 AND g.excluded = 0
  UNION ALL SELECT t.nameid FROM `storage2` t JOIN `need_elr_target` g ON g.account_id = t.account_id AND g.status = 1 AND g.excluded = 0
  UNION ALL SELECT t.nameid FROM `storage3` t JOIN `need_elr_target` g ON g.account_id = t.account_id AND g.status = 1 AND g.excluded = 0
  UNION ALL SELECT t.nameid FROM `mail_attachments` t JOIN `mail` m ON m.id = t.id JOIN `char` c ON c.char_id = m.dest_id
    JOIN `need_elr_target` g ON g.account_id = c.account_id AND g.status = 1 AND g.excluded = 0
) x
WHERE x.nameid IN (SELECT `item_id` FROM `need_elr_item` WHERE `kind` = 'stone' OR (`kind` = 'box' AND @recall_boxes = 1));

-- 대상 계정 의상에 남은 인챈트 (0 이어야 정상)
SELECT COUNT(*) AS `남은_부여인챈트_행(0)`
FROM (
  SELECT t.nameid, t.card0, t.card1, t.card2, t.card3 FROM `inventory` t JOIN `char` c ON c.char_id = t.char_id
    JOIN `need_elr_target` g ON g.account_id = c.account_id AND g.status = 1 AND g.excluded = 0
  UNION ALL SELECT t.nameid, t.card0, t.card1, t.card2, t.card3 FROM `cart_inventory` t JOIN `char` c ON c.char_id = t.char_id
    JOIN `need_elr_target` g ON g.account_id = c.account_id AND g.status = 1 AND g.excluded = 0
  UNION ALL SELECT t.nameid, t.card0, t.card1, t.card2, t.card3 FROM `storage` t
    JOIN `need_elr_target` g ON g.account_id = t.account_id AND g.status = 1 AND g.excluded = 0
  UNION ALL SELECT t.nameid, t.card0, t.card1, t.card2, t.card3 FROM `storage2` t
    JOIN `need_elr_target` g ON g.account_id = t.account_id AND g.status = 1 AND g.excluded = 0
  UNION ALL SELECT t.nameid, t.card0, t.card1, t.card2, t.card3 FROM `storage3` t
    JOIN `need_elr_target` g ON g.account_id = t.account_id AND g.status = 1 AND g.excluded = 0
  UNION ALL SELECT t.nameid, t.card0, t.card1, t.card2, t.card3 FROM `mail_attachments` t JOIN `mail` m ON m.id = t.id
    JOIN `char` c ON c.char_id = m.dest_id JOIN `need_elr_target` g ON g.account_id = c.account_id AND g.status = 1 AND g.excluded = 0
) x
WHERE x.nameid IN (SELECT `item_id` FROM `need_elr_item` WHERE `kind` = 'costume')
  AND (x.card0 IN (SELECT `item_id` FROM `need_elr_item` WHERE `kind` = 'card')
    OR x.card1 IN (SELECT `item_id` FROM `need_elr_item` WHERE `kind` = 'card')
    OR x.card2 IN (SELECT `item_id` FROM `need_elr_item` WHERE `kind` = 'card')
    OR x.card3 IN (SELECT `item_id` FROM `need_elr_item` WHERE `kind` = 'card'));

-- 대상 계정에 남은 옷장 스톤 세팅 (0 이어야 정상)
SELECT COUNT(*) AS `남은_옷장세팅(0)`
FROM `char_reg_num` g JOIN `char` c ON c.char_id = g.char_id
JOIN `need_elr_target` t ON t.account_id = c.account_id AND t.status = 1 AND t.excluded = 0
WHERE g.`index` = 0
  AND g.`key` IN ('COSTUME_STONE_TOP','COSTUME_STONE_MID','COSTUME_STONE_LOW','COSTUME_STONE_GARMENT','COSTUME_STONE_DUAL');

-- 참고 : 대상 계정 캐릭터가 마스터인 길드의 길드 창고에 있는 스톤/상자 (자동 삭제하지 않음)
SELECT gs.guild_id, gd.name AS `길드`, c.account_id AS `마스터_계정`, gs.nameid, SUM(gs.amount) AS `수량`
FROM `guild_storage` gs
JOIN `guild` gd ON gd.guild_id = gs.guild_id
JOIN `char` c ON c.char_id = gd.char_id
JOIN `need_elr_target` t ON t.account_id = c.account_id AND t.excluded = 0
WHERE gs.nameid IN (SELECT `item_id` FROM `need_elr_item` WHERE `kind` IN ('stone','box'))
GROUP BY gs.guild_id, gd.name, c.account_id, gs.nameid;

-- 계정별 회수 결과
SELECT t.account_id, t.exchange_cnt AS `교환(상자)`, t.removed_stone AS `회수_스톤`, t.removed_box AS `회수_상자`,
       t.stripped_card AS `지운_부여인챈트`, t.removed_closet AS `지운_옷장세팅`, t.status, t.applied_at
FROM `need_elr_target` t
WHERE t.excluded = 0
ORDER BY t.account_id;

-- 되돌리기 (필요할 때만, 서버 종료 상태에서)
--   삭제/변경 전 원본은 need_elr_bak_* 에 있다. 예:
--   REPLACE INTO `inventory` SELECT * FROM `need_elr_bak_inventory` WHERE `char_id` IN (...);
--   REPLACE INTO `char_reg_num` SELECT * FROM `need_elr_bak_char_reg_num` WHERE `char_id` IN (...);

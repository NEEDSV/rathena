-- NEED Homunculus S Phase 2 - 기존 호문 데이터 정리 (Lv.175 / 2017 정책)
--
-- 실행 조건 (반드시 지킬 것)
--   - map-server / char-server 종료 상태 (접속 중 호문 메모리 데이터가 DB 를 덮어쓰지 않도록)
--   - DB 전체 백업 완료
--   - need_homun_s_phase2_precheck.sql 결과 보관
--   - 서버 바이너리/DB import(Phase 2 커밋)와 같은 점검에서 함께 반영
--
-- 하는 일
--   1) 백업 테이블 생성 (최초 실행 시점 원본만 보관, 재실행해도 덮어쓰지 않음)
--   2) 원장(need_homun_s_p2_ledger)에 호문별 환급/조정 값을 최초 1회 계산
--   3) 원장 값으로 level / exp / skill_point 를 절대값으로 반영 (applied=0 인 행만)
--   4) 후대 스킬(8044~8059) 삭제, MaxLv 5 스킬의 Lv6~10 을 5 로 조정, 후대 쿨타임 삭제
--
-- 스킬 포인트
--   new_skill_point = skill_point + 삭제된 후대 스킬 Lv 합 + (Lv6~10 투자분)
--                     - (Lv175 초과분 레벨에서 얻은 포인트 = FLOOR(level/3) - FLOOR(175/3))
--   음수가 되면 0 으로 두고 review=1 로 표시 (수동 확인 대상)
--
-- 스탯(176~ 레벨 성장분)은 이 파일에서 건드리지 않습니다 -> need_homun_s_phase2_stat_fix.sql
--
-- 재실행: 원장/백업은 최초 값을 유지하고, 3) 은 applied=0 행만 반영하므로
--         서버를 다시 띄우기 전이라면 중단 후 재실행해도 결과가 같습니다.
--         서버를 띄운 뒤에는 재실행하지 마세요.

-- ------------------------------------------------------------------
-- 1) 백업
-- ------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `need_homun_s_p2_bak_homunculus` LIKE `homunculus`;
INSERT IGNORE INTO `need_homun_s_p2_bak_homunculus`
SELECT * FROM `homunculus` WHERE `class` BETWEEN 6048 AND 6052;

CREATE TABLE IF NOT EXISTS `need_homun_s_p2_bak_skill` LIKE `skill_homunculus`;
INSERT IGNORE INTO `need_homun_s_p2_bak_skill`
SELECT s.* FROM `skill_homunculus` s
JOIN `homunculus` h ON h.homun_id = s.homun_id
WHERE h.class BETWEEN 6048 AND 6052;

CREATE TABLE IF NOT EXISTS `need_homun_s_p2_bak_cooldown` LIKE `skillcooldown_homunculus`;
INSERT IGNORE INTO `need_homun_s_p2_bak_cooldown`
SELECT * FROM `skillcooldown_homunculus` WHERE `skill` BETWEEN 8044 AND 8059;

-- ------------------------------------------------------------------
-- 2) 원장 (최초 1회 계산)
-- ------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `need_homun_s_p2_ledger` (
  `homun_id` int(11) NOT NULL,
  `char_id` int(11) unsigned NOT NULL,
  `class` mediumint(9) unsigned NOT NULL,
  `old_level` smallint(4) NOT NULL,
  `old_exp` bigint(20) unsigned NOT NULL,
  `old_skill_point` smallint(4) unsigned NOT NULL,
  `late_pts` int NOT NULL DEFAULT 0 COMMENT '삭제되는 8044~8059 Lv 합',
  `over5_pts` int NOT NULL DEFAULT 0 COMMENT 'MaxLv5 스킬 Lv6~10 투자분',
  `level_pts` int NOT NULL DEFAULT 0 COMMENT 'Lv176~ 에서 얻은 스킬포인트',
  `new_level` smallint(4) NOT NULL,
  `new_exp` bigint(20) unsigned NOT NULL,
  `new_skill_point` smallint(4) unsigned NOT NULL,
  `review` tinyint unsigned NOT NULL DEFAULT 0 COMMENT '1=계산값이 음수라 0으로 둠',
  `applied` tinyint unsigned NOT NULL DEFAULT 0,
  `stat_fixed` tinyint unsigned NOT NULL DEFAULT 0,
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`homun_id`)
) ENGINE=MyISAM;

INSERT IGNORE INTO `need_homun_s_p2_ledger`
  (`homun_id`,`char_id`,`class`,`old_level`,`old_exp`,`old_skill_point`,
   `late_pts`,`over5_pts`,`level_pts`,`new_level`,`new_exp`,`new_skill_point`,`review`)
SELECT h.homun_id, h.char_id, h.class, h.level, h.exp, h.skill_point,
       IFNULL(sk.late_pts, 0),
       IFNULL(sk.over5_pts, 0),
       IF(h.level > 175, FLOOR(h.level / 3) - FLOOR(175 / 3), 0),
       LEAST(h.level, 175),
       IF(h.level >= 175, 0, h.exp),
       GREATEST(0, CAST(h.skill_point AS SIGNED) + IFNULL(sk.late_pts, 0) + IFNULL(sk.over5_pts, 0)
                   - IF(h.level > 175, FLOOR(h.level / 3) - FLOOR(175 / 3), 0)),
       IF(CAST(h.skill_point AS SIGNED) + IFNULL(sk.late_pts, 0) + IFNULL(sk.over5_pts, 0)
          - IF(h.level > 175, FLOOR(h.level / 3) - FLOOR(175 / 3), 0) < 0, 1, 0)
FROM `homunculus` h
LEFT JOIN (
  SELECT homun_id,
         SUM(IF(id BETWEEN 8044 AND 8059, lv, 0)) AS late_pts,
         SUM(IF(id IN (8019,8021,8024,8025,8029,8030,8031,8034,8041,8042) AND lv > 5, lv - 5, 0)) AS over5_pts
  FROM `skill_homunculus`
  GROUP BY homun_id
) sk ON sk.homun_id = h.homun_id
WHERE h.class BETWEEN 6048 AND 6052
  AND (h.level > 175 OR (h.level >= 175 AND h.exp > 0) OR sk.late_pts > 0 OR sk.over5_pts > 0);

-- ------------------------------------------------------------------
-- 3) level / exp / skill_point 반영
-- ------------------------------------------------------------------
UPDATE `homunculus` h
JOIN `need_homun_s_p2_ledger` l ON l.homun_id = h.homun_id
SET h.level = l.new_level,
    h.exp = l.new_exp,
    h.skill_point = l.new_skill_point
WHERE l.applied = 0;

UPDATE `need_homun_s_p2_ledger` SET `applied` = 1 WHERE `applied` = 0;

-- ------------------------------------------------------------------
-- 4) 스킬 / 쿨타임 정리
-- ------------------------------------------------------------------
DELETE FROM `skill_homunculus` WHERE `id` BETWEEN 8044 AND 8059;

UPDATE `skill_homunculus` SET `lv` = 5
WHERE `id` IN (8019,8021,8024,8025,8029,8030,8031,8034,8041,8042) AND `lv` > 5;

DELETE FROM `skillcooldown_homunculus` WHERE `skill` BETWEEN 8044 AND 8059;

-- ------------------------------------------------------------------
-- 5) 사후 확인 (모두 0 이어야 함)
-- ------------------------------------------------------------------
SELECT
  (SELECT COUNT(*) FROM `skill_homunculus` WHERE `id` BETWEEN 8044 AND 8059) AS late_skill_rows,
  (SELECT COUNT(*) FROM `skill_homunculus` WHERE `id` IN (8019,8021,8024,8025,8029,8030,8031,8034,8041,8042) AND `lv` > 5) AS over5_rows,
  (SELECT COUNT(*) FROM `skillcooldown_homunculus` WHERE `skill` BETWEEN 8044 AND 8059) AS late_cooldown_rows,
  (SELECT COUNT(*) FROM `homunculus` WHERE `class` BETWEEN 6048 AND 6052 AND `level` > 175) AS over175_rows,
  (SELECT COUNT(*) FROM `homunculus` WHERE `class` BETWEEN 6048 AND 6052 AND `level` >= 175 AND `exp` > 0) AS lv175_exp_rows,
  (SELECT COUNT(*) FROM `homunculus` h JOIN `need_homun_s_p2_ledger` l ON l.homun_id = h.homun_id
     WHERE h.skill_point <> l.new_skill_point OR h.level <> l.new_level) AS ledger_mismatch;

-- 처리 요약 (보고용)
SELECT COUNT(*) AS ledger_rows,
       SUM(old_level > 175) AS clamped_over175,
       SUM(late_pts > 0) AS homun_late_skill,
       SUM(late_pts) AS refund_late_pts,
       SUM(over5_pts > 0) AS homun_over5,
       SUM(over5_pts) AS refund_over5_pts,
       SUM(level_pts) AS removed_level_pts,
       SUM(review) AS review_rows
FROM `need_homun_s_p2_ledger`;

SELECT * FROM `need_homun_s_p2_ledger` WHERE `review` = 1;

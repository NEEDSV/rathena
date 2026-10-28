-- NEED Homunculus S Phase 2 - 운영 DB 사전 조회 (읽기 전용)
--
-- 마이그레이션(need_homun_s_phase2_migrate.sql) 전에 실행해서 결과를 보관/보고합니다.
-- 대상 클래스: 6048 Eira / 6049 Bayeri / 6050 Sera / 6051 Dieter / 6052 Eleanor
-- 후대 스킬  : 8044~8059 (RequiredLevel 210/215/230)
-- MaxLv 5 스킬: 8019 8021 8024 8025 8029 8030 8031 8034 8041 8042

-- ------------------------------------------------------------------
-- A. Homunculus S 레벨 분포
-- ------------------------------------------------------------------
SELECT COUNT(*) AS total,
       SUM(level > 175) AS over175,
       SUM(level >= 210) AS ge210,
       SUM(level >= 215) AS ge215,
       SUM(level >= 230) AS ge230,
       SUM(level >= 175 AND exp > 0) AS lv175plus_with_exp,
       MAX(level) AS max_level
FROM homunculus
WHERE class BETWEEN 6048 AND 6052;

SELECT class, COUNT(*) AS cnt, MIN(level) AS min_lv, MAX(level) AS max_lv,
       SUM(level > 175) AS over175, SUM(level >= 210) AS ge210,
       SUM(level >= 215) AS ge215, SUM(level >= 230) AS ge230
FROM homunculus
WHERE class BETWEEN 6048 AND 6052
GROUP BY class;

-- ------------------------------------------------------------------
-- B. 후대 스킬 보유 현황
-- ------------------------------------------------------------------
SELECT s.id, COUNT(*) AS holders, MAX(s.lv) AS max_lv, SUM(s.lv) AS used_points
FROM skill_homunculus s
WHERE s.id BETWEEN 8044 AND 8059
GROUP BY s.id ORDER BY s.id;

SELECT COUNT(DISTINCT s.homun_id) AS homun_with_late_skill, SUM(s.lv) AS total_refund_points
FROM skill_homunculus s
WHERE s.id BETWEEN 8044 AND 8059;

-- ------------------------------------------------------------------
-- C. MaxLv 5 초과 스킬 (Lv6 이상)
-- ------------------------------------------------------------------
SELECT s.id, COUNT(*) AS holders, MAX(s.lv) AS max_lv, SUM(s.lv - 5) AS refund_points
FROM skill_homunculus s
WHERE s.id IN (8019,8021,8024,8025,8029,8030,8031,8034,8041,8042) AND s.lv > 5
GROUP BY s.id ORDER BY s.id;

SELECT COUNT(DISTINCT s.homun_id) AS homun_with_over5, SUM(s.lv - 5) AS total_refund_points
FROM skill_homunculus s
WHERE s.id IN (8019,8021,8024,8025,8029,8030,8031,8034,8041,8042) AND s.lv > 5;

-- ------------------------------------------------------------------
-- D. 후대 쿨타임 저장
-- ------------------------------------------------------------------
SELECT skill, COUNT(*) AS cnt
FROM skillcooldown_homunculus
WHERE skill BETWEEN 8044 AND 8059
GROUP BY skill;

-- ------------------------------------------------------------------
-- E. 영향 호문 개별 목록 (마이그레이션 결과 미리보기)
--    refund_late/refund_over5 = 환급, level_pts = 176~ 레벨에서 얻은 포인트 회수
-- ------------------------------------------------------------------
SELECT h.homun_id, h.char_id, c.name AS char_name, h.class, h.level, h.exp, h.skill_point,
       IFNULL(sk.late_pts, 0)  AS refund_late,
       IFNULL(sk.over5_pts, 0) AS refund_over5,
       IF(h.level > 175, FLOOR(h.level / 3) - FLOOR(175 / 3), 0) AS level_pts,
       CAST(h.skill_point AS SIGNED) + IFNULL(sk.late_pts, 0) + IFNULL(sk.over5_pts, 0)
         - IF(h.level > 175, FLOOR(h.level / 3) - FLOOR(175 / 3), 0) AS new_skill_point_raw,
       sk.late_skills
FROM homunculus h
LEFT JOIN `char` c ON c.char_id = h.char_id
LEFT JOIN (
  SELECT homun_id,
         SUM(IF(id BETWEEN 8044 AND 8059, lv, 0)) AS late_pts,
         SUM(IF(id IN (8019,8021,8024,8025,8029,8030,8031,8034,8041,8042) AND lv > 5, lv - 5, 0)) AS over5_pts,
         GROUP_CONCAT(IF(id BETWEEN 8044 AND 8059, CONCAT(id, ':', lv), NULL) ORDER BY id) AS late_skills
  FROM skill_homunculus
  GROUP BY homun_id
) sk ON sk.homun_id = h.homun_id
WHERE h.class BETWEEN 6048 AND 6052
  AND (h.level > 175 OR (h.level >= 175 AND h.exp > 0) OR sk.late_pts > 0 OR sk.over5_pts > 0)
ORDER BY h.level DESC, h.homun_id;

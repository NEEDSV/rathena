-- NEED Homunculus S Phase 2 - Lv175 초과 호문 스탯 보정 (평균 성장분 회수)
--
-- need_homun_s_phase2_migrate.sql 실행 후, 운영 승인이 난 경우에만 실행합니다.
-- (데이터 손실 가능성이 있으므로 [1] 미리보기 결과를 먼저 확인/보고하세요)
--
-- 왜 평균인가
--   호문 레벨업 성장치는 매 레벨 rnd_value(min,max) 로 뽑고 저장하지 않으므로
--   "176~ 레벨에서 실제로 얻은 값"만 정확히 빼는 것은 불가능합니다.
--   hom_shuffle 은 1레벨부터 전부 다시 뽑아 175 이하 구간까지 바뀌므로 쓰지 않습니다.
--   여기서는 클래스별 성장 기댓값 x (원래레벨 - 175) 를 회수합니다.
--   개체 편차는 남지만, 실제 획득량의 상·하한(최소/최대 성장) 사이에 항상 들어갑니다.
--
-- 기댓값 출처: db/re/homunculus_db.yml 의 S 클래스 Growth 값 (Lv99 이상은 S 클래스 성장표 사용)
--   STR~LUK : DB 저장 단위(x10), 레벨업마다 10 미만 버림 -> E[v - v%10]
--   HP / SP : E[rnd_value(min,max)] = (min+max)/2
--
-- 재실행: ledger.stat_fixed = 1 인 행은 건너뜁니다.

CREATE TABLE IF NOT EXISTS `need_homun_s_p2_growth` (
  `class` mediumint(9) unsigned NOT NULL,
  `hp` decimal(10,4) NOT NULL, `sp` decimal(10,4) NOT NULL,
  `str` decimal(10,4) NOT NULL, `agi` decimal(10,4) NOT NULL, `vit` decimal(10,4) NOT NULL,
  `int` decimal(10,4) NOT NULL, `dex` decimal(10,4) NOT NULL, `luk` decimal(10,4) NOT NULL,
  PRIMARY KEY (`class`)
) ENGINE=MyISAM;

REPLACE INTO `need_homun_s_p2_growth` (`class`,`hp`,`sp`,`str`,`agi`,`vit`,`int`,`dex`,`luk`) VALUES
  (6048, 100.0000, 31.0000, 21.1111, 30.6667, 15.4545, 26.2857, 21.4286,  9.0000), -- Eira
  (6049, 225.0000, 50.0000, 22.6316, 17.5862, 19.4118, 28.6957, 13.8462, 24.1176), -- Bayeri
  (6050, 150.0000, 50.0000, 13.7500, 19.4118, 10.4762, 16.5517, 29.2308, 25.7143), -- Sera
  (6051, 360.0000, 80.0000, 25.7143, 15.0000, 22.6316, 22.6923, 19.4118,  5.3846), -- Dieter
  (6052, 180.0000, 15.0000, 25.7143, 25.6098, 31.2000,  5.4545, 19.6000,  1.1111); -- Eleanor

-- ------------------------------------------------------------------
-- [1] 미리보기 (변경 없음)
-- ------------------------------------------------------------------
SELECT h.homun_id, h.char_id, h.class, l.old_level, (l.old_level - 175) AS n,
       h.max_hp, GREATEST(1, CAST(h.max_hp AS SIGNED) - ROUND(g.hp * (l.old_level - 175))) AS new_max_hp,
       h.max_sp, GREATEST(1, CAST(h.max_sp AS SIGNED) - ROUND(g.sp * (l.old_level - 175))) AS new_max_sp,
       h.str, GREATEST(10, CAST(h.str AS SIGNED) - ROUND(g.str * (l.old_level - 175))) AS new_str,
       h.agi, GREATEST(10, CAST(h.agi AS SIGNED) - ROUND(g.agi * (l.old_level - 175))) AS new_agi,
       h.vit, GREATEST(10, CAST(h.vit AS SIGNED) - ROUND(g.vit * (l.old_level - 175))) AS new_vit,
       h.int, GREATEST(10, CAST(h.int AS SIGNED) - ROUND(g.int * (l.old_level - 175))) AS new_int,
       h.dex, GREATEST(10, CAST(h.dex AS SIGNED) - ROUND(g.dex * (l.old_level - 175))) AS new_dex,
       h.luk, GREATEST(10, CAST(h.luk AS SIGNED) - ROUND(g.luk * (l.old_level - 175))) AS new_luk
FROM `homunculus` h
JOIN `need_homun_s_p2_ledger` l ON l.homun_id = h.homun_id
JOIN `need_homun_s_p2_growth` g ON g.class = h.class
WHERE l.old_level > 175 AND l.stat_fixed = 0 AND l.applied = 1
ORDER BY l.old_level DESC;

-- ------------------------------------------------------------------
-- [2] 적용 (승인 후)
-- ------------------------------------------------------------------
UPDATE `homunculus` h
JOIN `need_homun_s_p2_ledger` l ON l.homun_id = h.homun_id
JOIN `need_homun_s_p2_growth` g ON g.class = h.class
SET h.max_hp = GREATEST(1, CAST(h.max_hp AS SIGNED) - ROUND(g.hp * (l.old_level - 175))),
    h.max_sp = GREATEST(1, CAST(h.max_sp AS SIGNED) - ROUND(g.sp * (l.old_level - 175))),
    h.str = GREATEST(10, CAST(h.str AS SIGNED) - ROUND(g.str * (l.old_level - 175))),
    h.agi = GREATEST(10, CAST(h.agi AS SIGNED) - ROUND(g.agi * (l.old_level - 175))),
    h.vit = GREATEST(10, CAST(h.vit AS SIGNED) - ROUND(g.vit * (l.old_level - 175))),
    h.int = GREATEST(10, CAST(h.int AS SIGNED) - ROUND(g.int * (l.old_level - 175))),
    h.dex = GREATEST(10, CAST(h.dex AS SIGNED) - ROUND(g.dex * (l.old_level - 175))),
    h.luk = GREATEST(10, CAST(h.luk AS SIGNED) - ROUND(g.luk * (l.old_level - 175))),
    l.stat_fixed = 1
WHERE l.old_level > 175 AND l.stat_fixed = 0 AND l.applied = 1;

UPDATE `homunculus` h
JOIN `need_homun_s_p2_ledger` l ON l.homun_id = h.homun_id
SET h.hp = LEAST(h.hp, h.max_hp),
    h.sp = LEAST(h.sp, h.max_sp)
WHERE l.old_level > 175;

-- 확인: 원본(백업) 대비 회수량
SELECT h.homun_id, h.class, b.level AS old_level,
       b.str - h.str AS str_removed, b.agi - h.agi AS agi_removed, b.vit - h.vit AS vit_removed,
       b.int - h.int AS int_removed, b.dex - h.dex AS dex_removed, b.luk - h.luk AS luk_removed,
       b.max_hp - h.max_hp AS hp_removed, b.max_sp - h.max_sp AS sp_removed
FROM `homunculus` h
JOIN `need_homun_s_p2_bak_homunculus` b ON b.homun_id = h.homun_id
JOIN `need_homun_s_p2_ledger` l ON l.homun_id = h.homun_id
WHERE l.old_level > 175;

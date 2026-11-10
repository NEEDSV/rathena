-- =====================================================================
-- NEED 잭프텔 스틸 감지 로그 테이블 마이그레이션 (본서버 배포용)
-- 작성일: 2026-11-10
--
-- 대상 DB : conf/inter_athena.conf (import/inter_conf.txt) 의 map_server_db
-- 대상    : need_jf_pattern_log
-- 내용    : 스틸 판정용 확장 컬럼 4개 추가
--           jf_hit_count / foreign_hit_count / steal_pattern_count / teleport_gap
--           (+ 아주 오래된 테이블이면 autoloot_blocked / world_drop_blocked 도 추가)
--
-- 이 컬럼이 없으면 스틸 점수가 오를 때마다 map-server 의 INSERT 가 SQL 오류로
-- 실패합니다(점수·제재 처리는 계속되지만 운영 로그가 남지 않음).
--
-- 여러 번 실행해도 안전합니다(IF NOT EXISTS). MariaDB 10.0.2 이상 필요.
-- MySQL 을 쓰는 경우 ADD COLUMN 의 IF NOT EXISTS 를 지우고, 이미 있는 컬럼은 빼고 실행하세요.
--
-- 이번 수정(시전 훅 이동, 워록 파티원 집계)에는 이 외의 DB 변경이 없습니다.
-- 기존 NeedJf* / #NeedJf* registry 는 건드리지 않습니다.
-- =====================================================================

-- 1) 테이블이 아예 없는 서버를 위한 생성 (있으면 아무 일도 하지 않음)
CREATE TABLE IF NOT EXISTS `need_jf_pattern_log` (
  `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
  `account_id` int(11) unsigned NOT NULL DEFAULT '0',
  `char_id` int(11) unsigned NOT NULL DEFAULT '0',
  `char_name` varchar(23) NOT NULL DEFAULT '',
  `map` varchar(24) NOT NULL DEFAULT '',
  `x` smallint(5) unsigned NOT NULL DEFAULT '0',
  `y` smallint(5) unsigned NOT NULL DEFAULT '0',
  `event` varchar(32) NOT NULL DEFAULT '',
  `pattern_count` int(11) unsigned NOT NULL DEFAULT '0',
  `suspicion_score` int(11) unsigned NOT NULL DEFAULT '0',
  `stage` smallint(5) unsigned NOT NULL DEFAULT '0',
  `penalty_count` int(11) unsigned NOT NULL DEFAULT '0',
  `penalty_duration` int(11) unsigned NOT NULL DEFAULT '0',
  `autoloot_blocked` tinyint(1) unsigned NOT NULL DEFAULT '0',
  `world_drop_blocked` tinyint(1) unsigned NOT NULL DEFAULT '0',
  `jf_hit_count` int(11) unsigned NOT NULL DEFAULT '0',
  `foreign_hit_count` int(11) unsigned NOT NULL DEFAULT '0',
  `steal_pattern_count` int(11) unsigned NOT NULL DEFAULT '0',
  `teleport_gap` int(11) NOT NULL DEFAULT '0',
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  KEY `account_id` (`account_id`),
  KEY `char_id` (`char_id`),
  KEY `event` (`event`),
  KEY `created_at` (`created_at`)
) ENGINE=MyISAM;

-- 2) 기존 테이블에 확장 컬럼 추가
ALTER TABLE `need_jf_pattern_log`
  ADD COLUMN IF NOT EXISTS `autoloot_blocked` tinyint(1) unsigned NOT NULL DEFAULT '0' AFTER `penalty_duration`,
  ADD COLUMN IF NOT EXISTS `world_drop_blocked` tinyint(1) unsigned NOT NULL DEFAULT '0' AFTER `autoloot_blocked`,
  ADD COLUMN IF NOT EXISTS `jf_hit_count` int(11) unsigned NOT NULL DEFAULT '0' AFTER `world_drop_blocked`,
  ADD COLUMN IF NOT EXISTS `foreign_hit_count` int(11) unsigned NOT NULL DEFAULT '0' AFTER `jf_hit_count`,
  ADD COLUMN IF NOT EXISTS `steal_pattern_count` int(11) unsigned NOT NULL DEFAULT '0' AFTER `foreign_hit_count`,
  ADD COLUMN IF NOT EXISTS `teleport_gap` int(11) NOT NULL DEFAULT '0' AFTER `steal_pattern_count`;

-- 3) 확인: 아래 결과가 4 이면 정상
SELECT COUNT(*) AS `jf_steal_columns_ok_if_4`
  FROM information_schema.COLUMNS
 WHERE TABLE_SCHEMA = DATABASE()
   AND TABLE_NAME = 'need_jf_pattern_log'
   AND COLUMN_NAME IN ('jf_hit_count', 'foreign_hit_count', 'steal_pattern_count', 'teleport_gap');

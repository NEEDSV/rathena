-- NEED 2026-10 낙원단(moc_para01) 거점 제거 : 저장 위치 이전 (일회성)
--
-- 배경 : 신규 계정의 시작 거점과 낙원단 성장 지원관이 구 페이욘(payo2)으로 옮겨지고,
--        moc_para01 의 카프라(저장 기능)가 철거된다. 그 전에 FirstGuide 나 카프라로
--        moc_para01 에 저장한 캐릭터는 사망·귀환 시 계속 낙원단으로 돌아가므로
--        저장 위치를 구 페이욘 시작 지점(payo2,86,126)으로 옮긴다.
--
-- 대상 : `char`.`save_map` = 'moc_para01' 인 캐릭터 전부
-- 제외 : `last_map`(마지막 접속 위치)은 건드리지 않는다. 맵은 남아 있고
--        워프 도우미#para / 구 페이욘 이동 도우미#need_eden_exit 로 나올 수 있다.
--        moc_para01 은 nomemo 맵이라 메모 포인트 정리는 필요 없다.
--
-- 순서
--   1) 점검 시작, login/char/map 서버 전부 종료
--      (접속 중인 캐릭터의 저장 위치는 서버 메모리에 있다. 켜 둔 채 실행하면 로그아웃 저장 시 되살아난다)
--   2) DB 백업
--   3) 이 파일 실행 (재실행해도 결과는 같다. 백업 테이블은 처음 실행 때 값만 남는다)
--   4) 마지막 SELECT 의 remaining 이 0 인지 확인

-- 1) 사전 현황
SELECT COUNT(*) AS `save_on_eden`, SUM(`online`) AS `online_now`
  FROM `char` WHERE `save_map` = 'moc_para01';

-- 2) 백업 (원복 : UPDATE `char` c JOIN `need_bak_eden_savepoint` b USING (`char_id`)
--                 SET c.`save_map` = b.`save_map`, c.`save_x` = b.`save_x`, c.`save_y` = b.`save_y`;)
CREATE TABLE IF NOT EXISTS `need_bak_eden_savepoint` (
  `char_id` int unsigned NOT NULL,
  `save_map` varchar(11) NOT NULL,
  `save_x` smallint unsigned NOT NULL,
  `save_y` smallint unsigned NOT NULL,
  `backed_up_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`char_id`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `need_bak_eden_savepoint` (`char_id`, `save_map`, `save_x`, `save_y`)
  SELECT `char_id`, `save_map`, `save_x`, `save_y`
    FROM `char` WHERE `save_map` = 'moc_para01';

-- 3) 이전
UPDATE `char`
   SET `save_map` = 'payo2', `save_x` = 86, `save_y` = 126
 WHERE `save_map` = 'moc_para01';

-- 4) 결과
SELECT (SELECT COUNT(*) FROM `char` WHERE `save_map` = 'moc_para01') AS `remaining`,
       (SELECT COUNT(*) FROM `need_bak_eden_savepoint`) AS `backed_up`;

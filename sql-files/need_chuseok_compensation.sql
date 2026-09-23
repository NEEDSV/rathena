-- NEED 2026 chuseok - honey songpyun outage compensation (one shot).
--
-- 2026-09-23 꿀송편 드롭이 원장 스키마 미임포트로 fail-closed 되어 하루 종일
-- 지급되지 않은 건에 대한 보상입니다.
--
-- 대상 : 그날(논리일자 04:00 기준) 이벤트 재료(7613 작은 찹쌀반죽 / 1001827 솔잎)를
--        실제로 획득한 계정. picklog 기준이라 "이벤트 사냥을 한 사람"만 잡힙니다.
-- 지급 : 꿀송편 1개를 우편으로 발송 (@chuseokcomp)
-- 원장 : 발송 시 need_chuseok_honey_claim / _ip_daily / _log 에
--        logical_date = 보상 대상일로 고정 기록 → 그날 몫을 소모한 것으로 처리
--
-- 순서
--   1) 이 파일 실행 (테이블 생성 + 대상 시드 + 기수령 계정 제외)
--   2) 인게임에서 @chuseokcomp 로 현황 확인 후 발송
--
-- ※ 아래 세 군데의 날짜를 보상 대상일로 맞춰 주세요. 기본값 2026-09-23.

CREATE TABLE IF NOT EXISTS `need_chuseok_compensation` (
  `account_id` int unsigned NOT NULL,
  `char_id` int unsigned NOT NULL DEFAULT 0,
  `char_name` varchar(24) NOT NULL DEFAULT '',
  `ip` varchar(45) NOT NULL DEFAULT '',
  `status` tinyint unsigned NOT NULL DEFAULT 0 COMMENT '0=대기,1=발송완료,2=제외(이미 수령)',
  `sent_at` datetime DEFAULT NULL,
  `note` varchar(64) NOT NULL DEFAULT '',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`),
  KEY `status` (`status`)
) ENGINE=InnoDB;

-- ------------------------------------------------------------------
-- 1) 대상 시드
--    계정별로 "그날 재료를 마지막으로 먹은 캐릭터" 한 명을 발송 대상으로 잡습니다.
-- ------------------------------------------------------------------
INSERT IGNORE INTO `need_chuseok_compensation` (`account_id`,`char_id`,`char_name`,`ip`)
SELECT t.account_id, t.char_id, c2.name, COALESCE(l.last_ip,'')
FROM (
  SELECT c.account_id,
         p.char_id,
         ROW_NUMBER() OVER (PARTITION BY c.account_id ORDER BY MAX(p.time) DESC, p.char_id ASC) AS rn
  FROM `picklog` p
  JOIN `char` c ON c.char_id = p.char_id
  WHERE p.type = 'M'
    AND p.nameid IN (7613, 1001827)
    AND p.time >= '2026-09-23 04:00:00'
    AND p.time <  '2026-09-24 04:00:00'
  GROUP BY c.account_id, p.char_id
) t
JOIN `char` c2 ON c2.char_id = t.char_id
LEFT JOIN `login` l ON l.account_id = t.account_id
WHERE t.rn = 1;

-- ------------------------------------------------------------------
-- 2) 그날 이미 꿀송편을 받은 계정은 제외
-- ------------------------------------------------------------------
UPDATE `need_chuseok_compensation` nc
JOIN `need_chuseok_honey_claim` cl
  ON cl.`event_id` = 202609
 AND cl.`logical_date` = '2026-09-23'
 AND cl.`account_id` = nc.`account_id`
SET nc.`status` = 2,
    nc.`note` = 'ALREADY_CLAIMED'
WHERE nc.`status` = 0;

-- ------------------------------------------------------------------
-- 3) 확인
-- ------------------------------------------------------------------
SELECT `status`,
       CASE `status` WHEN 0 THEN '발송 대기' WHEN 1 THEN '발송 완료' ELSE '제외' END AS 상태,
       COUNT(*) AS 계정수
FROM `need_chuseok_compensation`
GROUP BY `status`;

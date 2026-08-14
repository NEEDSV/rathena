-- ============================================================
-- NEED 여름 이벤트 낚시 (fishing9) - 기존 catch fish_id 마이그레이션
-- event_id = 202608
--
-- 8종 -> 12종 재배치로 기존 need_summer_fishing_catch 의 fish_id 의미가 바뀝니다.
-- 이 파일은 "기존 테스트 catch 데이터를 보존"할 때만 운영자가 수동 적용합니다.
-- 운영/테스트 DB에 자동 적용하지 마세요. 반드시 백업 후 적용하세요.
--
-- old -> new 매핑:
--   1 작은 정어리        -> 1  (변경 없음)
--   2 코모도 꽃게        -> 2  (변경 없음)
--   3 통통한 복어        -> 3  (변경 없음)
--   4 무지개 도미        -> 5
--   5 대왕 문어          -> 6
--   6 황금 참치          -> 9
--   7 코모도 청새치      -> 10
--   8 한여름의 황금 고래 -> 12
--
-- 단일 CASE 로 한 번에 처리하여 4->5, 5->6 같은 연쇄 변경을 방지합니다.
-- (CASE 는 각 row 의 "원래" fish_id 기준으로 한 번만 판정합니다.)
-- ============================================================

-- (선택 A) 기존 테스트 catch 데이터 전체 초기화 후 새 12종으로 시작하려면 아래 주석 해제:
-- DELETE FROM `need_summer_fishing_catch` WHERE `event_id` = 202608;

-- (선택 B) 기존 데이터를 신규 fish_id 로 보존 마이그레이션:
UPDATE `need_summer_fishing_catch`
SET `fish_id` = CASE `fish_id`
  WHEN 8 THEN 12
  WHEN 7 THEN 10
  WHEN 6 THEN 9
  WHEN 5 THEN 6
  WHEN 4 THEN 5
  ELSE `fish_id`
END
WHERE `event_id` = 202608 AND `fish_id` IN (4, 5, 6, 7, 8);

-- 도감 집계(need_summer_fishing_collection)는 신규 테이블이므로 별도 마이그레이션이 없습니다.
-- 필요 시 catch 원본을 기준으로 collection 을 재집계할 수 있습니다.

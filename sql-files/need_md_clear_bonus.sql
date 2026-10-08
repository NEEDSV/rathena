-- NEED 2026-10-08: 메모리얼 던전 클리어 보너스 IP 일일 횟수
-- npc/NEED/need_md_clear_bonus.txt 가 OnInit 에서 같은 정의로 자동 생성한다.
-- DB 계정에 CREATE 권한이 없으면 이 파일을 수동으로 적용할 것.
CREATE TABLE IF NOT EXISTS `need_md_clear_bonus_ip` (
  `day` INT UNSIGNED NOT NULL,
  `dkey` VARCHAR(32) NOT NULL,
  `ip` VARCHAR(45) NOT NULL,
  `used` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `last_account_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `last_char_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` DATETIME NULL,
  PRIMARY KEY (`day`,`dkey`,`ip`)
);

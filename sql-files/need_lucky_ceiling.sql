-- ============================================================
-- NEED 행운의 알 교환소 (시즌 천장 / 공용 마일리지) 서버 공용 재고
--
-- npc/NEED/need_lucky_ceiling.txt 가 사용합니다.
--
-- 이 테이블이 없으면 스크립트는 재고가 있는 모든 상품의
-- 구매를 자동으로 차단합니다. (공급량 초과 지급 방지)
-- map-server 를 올리기 전에 반드시 수동으로 적용하세요.
--
--   mysql --host=127.0.0.1 --user=<id> --password=<pw> --database=<db> < need_lucky_ceiling.sql
--
-- 기존 need_summer_* 테이블과 동일한 InnoDB 스타일입니다.
-- ============================================================

-- 상품별 서버 공용 재고 (상품 1개 = row 1개)
--  product_id  : 스크립트 OnInit 의 .Pid[] 값
--  period_type : 0 = 재고 제한 없음, 1 = 주간, 2 = 월간
--  period_key  : 주간 = 리셋 기준 토요일 YYYYMMDD, 월간 = YYYYMM
--  stock       : 현재 남은 서버 재고
--  base_stock  : 해당 주기의 기본 재고 (.Stock[] 스냅샷, 참고용)
--  sale_mode   : 0 = 스크립트 .Enabled 설정 사용, 1 = 강제 판매중지, 2 = 강제 판매
CREATE TABLE IF NOT EXISTS `need_lucky_ceiling_stock` (
  `product_id`  INT UNSIGNED     NOT NULL,
  `period_type` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `period_key`  INT UNSIGNED     NOT NULL DEFAULT 0,
  `stock`       INT              NOT NULL DEFAULT 0,
  `base_stock`  INT              NOT NULL DEFAULT 0,
  `sale_mode`   TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at`  DATETIME         NOT NULL,
  PRIMARY KEY (`product_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

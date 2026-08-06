# NEED 2026 여름 필드 사냥 보상 운영

## 활성화 전 준비

`sql-files/need_summer_hunt.sql` 또는 해당 업그레이드 SQL을 map-server 주 DB에 적용한 뒤 세 테이블이 InnoDB인지 확인한다. SQL 적용과 확인이 끝날 때까지 `need_summer_hunt_enable`, `need_summer_hunt_fragment_enable`, `need_summer_hunt_golden_enable`은 모두 `no`로 둔다.

황금 수박은 `event_id=202608`을 사용한다. 계정·IP 일일 기준은 서버 현지 시각 오전 4시이며, 현재 시각에서 4시간을 뺀 날짜가 `logical_date`가 된다.

## 가족 IP 예외

출석과 사냥에 중복 승인 테이블을 만들지 않는다. 기존 `need_summer_attendance_family_group`, `need_summer_attendance_family_member`, `need_summer_attendance_family_audit`를 2026 여름 이벤트 공통 승인 레지스트리로 사용한다. 출석 기능 스위치가 꺼져 있어도 활성 그룹과 구성원은 사냥 제한에서 조회된다.

승인 그룹을 등록·변경·비활성화할 때는 그룹·구성원 변경과 감사 행을 운영자 트랜잭션 하나로 기록한다. 같은 IP의 최초 획득자와 현재 계정이 모두 같은 활성 그룹일 때만 IP 예외가 적용되며, 계정별 하루 1개 제한은 예외가 없다.

## 운영 조회

오늘 오전 4시 기준 전달·거절·실패 내역:

```sql
SET @logical_date = DATE(DATE_SUB(NOW(), INTERVAL 4 HOUR));
SELECT `log_id`,`claim_id`,`logical_date`,`account_id`,`char_id`,`char_name`,
       INET6_NTOA(`ip`) AS `ip`,`family_group_id`,`family_exception`,
       `map_name`,`x`,`y`,`mob_id`,`player_level`,`mob_level`,
       `result`,`failure_code`,`created_at`
FROM `need_summer_hunt_golden_log`
WHERE `event_id`=202608 AND `logical_date`=@logical_date
ORDER BY `log_id` DESC;
```

예약 상태로 남은 원장(정상 경로에서는 없어야 함):

```sql
SELECT `claim_id`,`logical_date`,`account_id`,`char_id`,`char_name`,
       INET6_NTOA(`ip`) AS `ip`,`map_name`,`mob_id`,`created_at`,`updated_at`
FROM `need_summer_hunt_golden_claim`
WHERE `event_id`=202608 AND `status`=0
ORDER BY `claim_id`;
```

계정·IP 제한 원장 대조:

```sql
SELECT `c`.`claim_id`,`c`.`logical_date`,`c`.`account_id`,INET6_NTOA(`c`.`ip`) AS `ip`,
       `c`.`family_group_id`,`c`.`family_exception`,`c`.`status`,`c`.`delivered_at`
FROM `need_summer_hunt_golden_claim` `c`
WHERE `c`.`event_id`=202608
ORDER BY `c`.`logical_date` DESC,`c`.`claim_id` DESC;

SELECT `logical_date`,INET6_NTOA(`ip`) AS `ip`,`family_group_id`,
       `first_account_id`,`first_char_id`,`created_at`
FROM `need_summer_hunt_golden_ip_daily`
WHERE `event_id`=202608
ORDER BY `logical_date` DESC,`created_at` DESC;
```

## 비원자적 경계

인벤토리와 MySQL은 하나의 트랜잭션이 아니다. 구현은 SQL 트랜잭션을 연 상태에서 계정·IP 수령권을 예약하고 `pc_additem`에 성공한 뒤 원장을 완료하고 커밋한다. 아이템 추가가 실패하면 롤백하여 계정·IP 횟수를 소비하지 않는다.

아이템 추가 뒤 SQL 갱신 또는 커밋 결과가 불확실하면 이미 지급된 아이템을 안전하게 회수할 수 없으므로 황금 수박 처리를 재시작 전까지 fail-closed 한다. 운영자는 서버 로그, picklog, 예약 원장을 함께 대조한 뒤 수동으로 판정해야 한다.

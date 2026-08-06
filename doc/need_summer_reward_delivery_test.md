# NEED 여름 출석 보상 우편 검증 절차

## 안전 전제

이 절차는 폐기 가능한 별도 MySQL/MariaDB 데이터베이스와 테스트 캐릭터에서만
실행한다. 운영 DB, 운영 계정, 운영 우편 테이블에는 적용하지 않는다. 저장소의
기본 `feature.need_summer_attendance` 값은 계속 `off`로 둔다.

현재 작업 환경에는 MariaDB 서비스는 있으나 MySQL/MariaDB CLI가 PATH에 없고
해당 DB가 폐기 가능한 테스트 DB인지 확인되지 않았다. 따라서 이 문서 작성
시점에는 아래 SQL을 실행하지 않았으며 실제 우편 전달·수령 결과를 검증한
것으로 간주하지 않는다.

## 테스트 DB 설치

다음 예시는 `mysql.exe`가 PATH에 있고 `need_summer_2026_test`가 폐기 가능한
DB라는 전제다. 사용자명과 경로는 테스트 환경에 맞게 바꾼다.

```powershell
mysql.exe -h 127.0.0.1 -u test_admin -p --execute="CREATE DATABASE need_summer_2026_test CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci"
mysql.exe -h 127.0.0.1 -u test_admin -p --database=need_summer_2026_test --execute="SOURCE E:/tools/Need/need-summer-2026/sql-files/main.sql"
mysql.exe -h 127.0.0.1 -u test_admin -p --database=need_summer_2026_test --execute="SOURCE E:/tools/Need/need-summer-2026/sql-files/upgrades/upgrade_20260806.sql"
```

테스트 전용 `conf/import/inter_conf.txt`에서 map DB를 위 데이터베이스로
지정한다. 운영 설정 파일을 복사하거나 덮어쓰지 않는다. 테스트 작업 복제본의
`conf/import/battle_conf.txt`에만 다음 한 줄을 추가한다.

```text
feature.need_summer_attendance: on
```

`feature.attendance`는 기본 설정에서 이미 `on`이다. item DB 로딩 로그에
`db/import/need_summer_item_db.yml` 9개 항목이 나타나고 399925와 399928이
존재하는지 먼저 확인한다.

## 테스트 원장 생성

폐기 가능한 테스트 캐릭터의 실제 `account_id`, `char_id`를 사용한다.

```sql
SET @test_account_id = 2000001;
SET @test_char_id = 2000001;

SELECT `char_id`, `account_id`, `name`
FROM `char`
WHERE `char_id` = @test_char_id
  AND `account_id` = @test_account_id;

START TRANSACTION;
INSERT INTO `need_summer_attendance_claim`
  (`event_id`,`logical_date`,`account_id`,`char_id`,`ip`,`family_group_id`,
   `claim_no`,`online_seconds`,`status`)
VALUES
  (202608,'2099-01-01',@test_account_id,@test_char_id,
   INET6_ATON('192.0.2.10'),0,1,1800,0);
SET @test_claim_id = LAST_INSERT_ID();

INSERT INTO `need_summer_attendance_reward_outbox`
  (`claim_id`,`event_id`,`account_id`,`char_id`,
   `token_item_id`,`token_amount`,`box_item_id`,`box_amount`,`status`)
VALUES
  (@test_claim_id,202608,@test_account_id,@test_char_id,
   399925,10,399928,1,0);
SET @test_outbox_id = LAST_INSERT_ID();
COMMIT;

SELECT @test_claim_id AS `test_claim_id`, @test_outbox_id AS `test_outbox_id`;
```

반환된 두 ID를 별도로 기록한다. 세션이 끊기면 `@` 변수가 사라지므로 이후
조회에서는 실제 숫자로 다시 설정한다.

## consumer 실행과 기대 결과

테스트 설정으로 map-server를 일반 실행한다. `--run-once`는 5초 consumer
타이머 실행 전에 종료될 수 있으므로 전달 검증에는 사용하지 않는다. 서버를
최소 10초 유지한 뒤 정상 종료한다.

```powershell
.\map-server.exe
```

다음 조회 결과를 확인한다.

```sql
SET @test_claim_id = 1;
SET @test_outbox_id = 1;

SELECT `outbox_id`,`claim_id`,`status`,`attempts`,`mail_id`,
       `last_error`,`delivered_at`
FROM `need_summer_attendance_reward_outbox`
WHERE `outbox_id` = @test_outbox_id;

SELECT `claim_id`,`status`
FROM `need_summer_attendance_claim`
WHERE `claim_id` = @test_claim_id;

SELECT m.`id`,m.`dest_id`,m.`send_id`,m.`send_name`,m.`title`,m.`status`,
       a.`index`,a.`nameid`,a.`amount`,a.`identify`
FROM `need_summer_attendance_reward_outbox` AS o
JOIN `mail` AS m ON m.`id` = o.`mail_id`
JOIN `mail_attachments` AS a ON a.`id` = m.`id`
WHERE o.`outbox_id` = @test_outbox_id
ORDER BY a.`index`;
```

기대 결과:

- outbox: `status=2`, `attempts=1`, `mail_id`와 `delivered_at`이 NULL 아님
- claim: `status=1`
- mail: 정확히 1행, `send_id=0`, 테스트 캐릭터가 수신자
- mail_attachments: 정확히 2행
- index 0: `399925 x10`, identify 1
- index 1: `399928 x1`, identify 1

## 중복 방지 검증

완료 행의 상태나 `mail_id`를 인위적으로 수정하지 않는다. 같은 map-server를
다시 시작해 consumer를 한 번 더 동작시킨 뒤 아래 값을 비교한다.

```sql
SELECT COUNT(*) AS `outbox_rows`
FROM `need_summer_attendance_reward_outbox`
WHERE `outbox_id` = @test_outbox_id;

SELECT COUNT(*) AS `mail_rows`
FROM `mail`
WHERE `id` = (
  SELECT `mail_id`
  FROM `need_summer_attendance_reward_outbox`
  WHERE `outbox_id` = @test_outbox_id
);

SELECT COUNT(*) AS `attachment_rows`
FROM `mail_attachments`
WHERE `id` = (
  SELECT `mail_id`
  FROM `need_summer_attendance_reward_outbox`
  WHERE `outbox_id` = @test_outbox_id
);
```

기대값은 각각 1, 1, 2이며 재시작 전후에 변하지 않아야 한다.

## 클라이언트 확인

- 테스트 캐릭터 편지함에 시스템 우편이 한 통만 보이는지 확인
- 첨부물이 399925 10개와 399928 1개인지 확인
- 두 첨부물을 한 요청에서 정상 수령할 수 있는지 확인
- 수령 직후 picklog에 두 건 모두 `E` 유형으로 기록되는지 확인
- 399925는 개인 창고 입출고가 가능하고 각각 `R` 유형으로 기록되는지 확인
- 399928은 개인 창고 이동이 거절되는지 확인
- 두 아이템 모두 사용자 우편 첨부가 거절되는지 확인
- 399928 사용 시 수량이 줄지 않고 아무 보상도 지급되지 않는지 확인
- 재접속 후에도 우편이 중복 생성되지 않는지 확인

## 정리 및 롤백

테스트가 끝난 후 기록한 ID로 테스트 행만 삭제한다. outbox는 운영 중 삭제하지
않는 것이 원칙이며, 아래 정리는 폐기 가능한 테스트 DB에만 해당한다.

```sql
SET @test_claim_id = 1;
SET @test_outbox_id = 1;

START TRANSACTION;
DELETE m
FROM `mail` AS m
JOIN `need_summer_attendance_reward_outbox` AS o ON o.`mail_id` = m.`id`
WHERE o.`outbox_id` = @test_outbox_id
  AND o.`claim_id` = @test_claim_id
  AND o.`event_id` = 202608;

DELETE FROM `need_summer_attendance_reward_outbox`
WHERE `outbox_id` = @test_outbox_id
  AND `claim_id` = @test_claim_id
  AND `event_id` = 202608;

DELETE FROM `need_summer_attendance_claim`
WHERE `claim_id` = @test_claim_id
  AND `event_id` = 202608;
COMMIT;
```

`mail_attachments`는 `mail.id` 외래 키의 `ON DELETE CASCADE`로 함께 정리된다.
테스트 DB 전체가 더 이상 필요 없으면 연결된 프로세스를 모두 종료한 뒤에만
관리자가 해당 테스트 DB를 삭제한다.

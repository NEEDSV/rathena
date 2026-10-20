# NEED 라그 마블 — 배포·CP 지급 승인 게이트 v1.0

- 작성일: 2026-10-20
- 대상: `feature/need-marble-phase-bc-v1`
- 원칙: 아래 승인은 **각각 독립**이다. 앞 단계 승인이 다음 단계를 포함하지 않는다. 현재 승인된 것은 G1(이 브랜치 안의 구현·정적 검사·worktree 빌드·로컬 커밋)뿐이다.

## 1. 승인 단계

| 단계 | 내용 | 전제 | 되돌리기 | 현재 |
|---|---|---|---|---|
| A1 G2 격리 시험 | 격리 MariaDB·login/char/map 기동, 합성 계정, V01~V24 실행, 강제 종료 시험(소유 프로세스만) | 운영자 실행 승인, 포트·경로 지정, 맵 등록 방식(A1-b) | 격리 자원 삭제 | **미승인** |
| A1-b 격리 맵 등록 | 격리 복사본에만 `need_marbl` 의 `maps_athena.conf`·`map_index.txt`·`map_cache.dat` 반영 | A1 | 복사본 삭제 | 미승인 (이 브랜치에는 맵 등록 없음) |
| A2 원본 DB DDL | `sql-files/need_marble_phase_bc_v1.sql` 을 운영 DB 에 적용 | A1 PASS | 마스터 OFF 로 충분. 테이블 삭제는 `_revert.sql` + 백업 + 별도 승인 | 미승인 |
| A3 바이너리 배포 | 이 브랜치 map-server 를 운영 반영 | A1 PASS, 병합 승인(A9) | 이전 바이너리 | 미승인 |
| A4 NPC 로드 | `npc/NEED/marble/scripts_marble.conf` 를 로드 목록에 추가(또는 `need_marble_board_v2.txt` 직접), 페이욘 입장 NPC 좌표 확정 후 추가 | A2, A3 | 로드 목록에서 제거 | 미승인 |
| A5 마스터 ON | `need_marble_policy` 키 1 = 1 (다음 논리 일자) | A2~A4 | 키 1 = 0 행 추가 | 미승인 |
| A6 전장 적립 ON | 키 4 = 1, 참여 표본 기준 확정 | 전장 계측·V18 | 키 4 = 0 | 미승인 |
| A7 던전 자동 안전 ON | 키 52 = 1 | 운영자 확정(N12) | 키 52 = 0 | 미승인 |
| A8 CP 실지급 | 지급 적용 경로 설계·구현·시험(현재 없음) 후 ON | V20 PASS, 대사 절차 리허설 | 적용 경로 비활성 | **미구현·미승인** |
| A9 병합·푸시 | 원격 푸시, need-server 반영 | 위 단계별 결정 | revert | 미승인 |

**배포 결합 주의:** `npc/NEED/need_md_clear_bonus.txt`·`npc/battleground/bg_common.txt` 의 추가 1줄은 이 브랜치의 스크립트 명령을 부른다. **옛 map-server 에 이 NPC 만 올리면 스크립트 오류**가 난다. NPC 와 바이너리는 같이 반영해야 한다. 마스터 OFF 상태에서는 두 줄 모두 아무것도 하지 않는다.

## 2. 운영 반영 체크리스트 (A2~A5 때)

1. 운영 서버 OS 시간대 확인(마블은 고정 KST, 기존 이벤트 사냥은 OS 시간대).
2. 운영 `conf/log_athena.conf` 의 `log_cash`·`cashlog` 사용 확인(A8 대사 근거).
3. 스키마 적용 후 map-server 로그에 `need_marble: ledger ready, enabled=0` 확인.
4. 정책 값 넣기 전 격리 DB 에서 같은 값으로 `ready` 확인(값 검증 실패 = 마블 전체 정지).
5. 마스터 ON 은 다음 논리 일자로 넣고, 그 날 04:00 KST 이후 `@marble` 로 확인.
6. 첫 날 불변식 점검 SQL(`need_marble_phase_bc_v1_invariants.sql`) 을 **읽기 전용 계정**으로 실행, 결과 0행 확인.

## 3. CP 지급 결정 원장과 대사 절차

### 3.1 지금 빌드의 동작

- 도착 칸·던전 선택·33걸음 보너스는 `need_marble_cp_payout` 에 **status = 1 (DECIDED, 지급 대기)** 행만 만든다. 키: 도착 `(account, TILE, roll_seq, 0)`, 보너스 `(account, LAP, lap_no, 0)` → 같은 결과는 두 번 결정되지 않는다.
- `#CASHPOINTS`·`acc_reg_num`·`pc_getcash` 를 건드리는 코드는 없다(정적 검사로 확인). 지급 대기 행이 쌓이기만 한다.

### 3.2 지급 경로를 만들 때의 원칙 (A8, 별도 설계)

- 마블 원장(맵서버 DB 연결)과 `#CASHPOINTS`(char-server 가 변수마다 따로 저장)는 한 트랜잭션으로 묶을 수 없다 → 결정 → 반영 → 기록 3단계, 반영 표지(예: `#NMB_CPNO = payout_no`)를 같은 스크립트 실행에서 함께 저장(설계 v0.5 16절).
- **자동 재지급 금지.** 반영 여부를 확정할 수 없으면 status = 3 (UNCERTAIN) 으로 두고 사람이 판단한다.

### 3.3 운영자 대사 절차 (A8 이후)

| 순서 | 할 일 | 근거 데이터 |
|---|---|---|
| 1 | 지급 대기·불확실 목록 | `SELECT account_id, payout_no, src_type, src_a, amount, status, decided_at FROM need_marble_cp_payout WHERE status IN (1,3) ORDER BY decided_at;` |
| 2 | 계정별 실제 CP 변화 확인 | `cashlog`(지급 시각 전후, 해당 계정), char-server `acc_reg_num` 의 `#CASHPOINTS` 현재값 |
| 3 | 판정 | 반영됨 → status 2 로 정정(티켓 번호 기록) / 미반영 → GM 수동 지급 후 status 4 (MANUAL_APPLIED) / 지급 대상 아님 → status 5 (MANUAL_VOID) |
| 4 | 기록 | `operator_ticket` 에 처리 번호, 처리 내역 별도 보관 |
| 5 | 일일 대사 | 계정·일자별 `Σ granted(status 2, 4)` vs cashlog 마블 지급 합. 차이는 `acc_reg_num` 으로 재확인 |

## 4. G2 격리 시험 절차 (A1 승인 후에만)

1. 이전 격리 원칙 재사용: 새 MariaDB 데이터 폴더·포트, `127.0.0.1` 바인딩, 원본 3306·dqtest 접근 금지, 원본 스키마 권한 없음, 사전 점검 하나라도 FAIL 이면 중단.
2. 격리 복사본에 맵 등록(A1-b), `npc/NEED/marble/scripts_marble.conf` import 추가.
3. 격리 DB 에 `need_marble_phase_bc_v1.sql` 적용, 시험 날짜로 키 1 = 1, TEST 시즌이 필요하면 시즌 900001 + 키 6.
4. 합성 계정·캐릭터만 사용(실제 계정 복제 금지).
5. V01~V24 를 검증 계획 v0.3 순서로 실행, 각 시나리오 뒤 불변식 SQL 0행 확인.
6. 끝나면 증거 저장 후 격리 프로세스·DB 정리.

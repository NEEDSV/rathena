# 02. 시험 보고

## 시험 환경

| 항목 | 내용 |
|---|---|
| 코드 | `E:\tools\Need\rathena-refine-implementation` (작업 브랜치, 미커밋 변경 포함) |
| DB | **격리 MariaDB 12.2.2**: 세션 scratchpad 데이터 폴더, 포트 **3399**, 127.0.0.1 바인딩. 서버 스키마(`main.sql`, `logs.sql`, `need_*.sql`)를 적재 |
| 운영 DB와의 분리 | 이 PC의 MariaDB 서비스(3306, `conf/import/inter_conf.txt`가 가리키는 DB)에는 **접속하지 않았다**. `conf/import/map_conf.txt`의 `map_ip`가 공인 IP라 3306은 운영 DB일 가능성이 높다 |
| 맵서버 | `E:\tools\Need\rathena\map-server.exe` (2026-10-19 08:50 빌드, 마지막 `src/` 커밋 `a1b7c0a06` 직후). 실행 위치는 작업 worktree |
| 맵서버 격리 설정 | `--inter-config`(모든 DB를 3399로, `import` 없음), `--map-config`(기본 설정 import 후 `char_port: 1`·`map_port: 15121`로 덮어써 운영 캐릭터서버 등록 차단). 실행 시 게임 서버 프로세스 없음 확인 |
| 설정 사본 | `evidence/iso_inter.conf`, `iso_map.conf`, `iso_map_test.conf` (비밀번호는 가림) |

## 실행 명령과 결과

### T1. 정적 검사 — **PASS 31/31**

```bash
python -I docs/reports/need-refine-policy-implementation/tools/static_check.py . E:/tools/Need/rathena-refine-followup
```

출력: `evidence/static_check_output.txt`. 주요 항목:
- CP949 디코드, CRLF 유지
- 17개 배열 길이 133으로 동일, PID 고유, 201 미사용
- PID 100/102/103/104 값 정확, 신규 상품명 정확
- 104와 102의 Cycle·Stock 일치, 공유 매핑 1회·상품 배열 구간 밖
- 모든 `callsub` 대상 라벨 존재, 중복 라벨 없음, 구 가격 줄 제거
- 조건부 UPDATE + ROW_COUNT 사용, 원자 구간에 `next/select` 없음, 역교환 상품 없음
- 기존 131개 상품 중 PID 100 Zeny와 102 Point·Zeny 외 변경 없음
- 바터 YAML 로드, +7 200P + 0z, 골드 코인·전장 증표 비율 불변

### T2. SQL 수준 공유 재고·동시성 — **PASS 9/9** (격리 DB)

```bash
python -I docs/reports/need-refine-policy-implementation/tools/sql_stock_test.py "E:/MariaDB 12.2/bin/mariadb.exe" 3399 <격리 root 비밀번호>
```

출력: `evidence/sql_stock_test_output.txt`. `S_TakeStock`·`S_Normalize`와 **동일한 SQL 문**을 실행했다.

| 시험 | 결과 |
|---|---|
| 행 없음 → 기본 재고 생성 | PASS |
| 포인트 경로(102) 1건 성공 → 대축 경로(104 → 키 102) 품절 | PASS |
| 104 전용 재고 행 미생성 | PASS |
| 같은 주기 재리셋 시 품절 재고 유지(되살리지 않음) | PASS |
| 새 주기에서 기본 재고 복원 | PASS |
| 지난 주기 키로 차감 시 거부 | PASS |
| **월 재고 1개에 동시 20건 → 정확히 1건 성공, 재고 0** | PASS |
| **주 재고 100개에 동시 150건 → 정확히 100건, 재고 0(음수 없음)** | PASS |

### T3. 맵서버 run-once (스크립트 파싱·OnInit) — **PASS**

출력: `evidence/runonce_impl.log`
- exit 0. `[Error]`·`[Warning]`·script error **0건**
- NPC 8,270개(스크립트 4,291개) 로드, OnInit 715개 실행
- `need_barters.yml` 23개 항목 로드
- 격리 DB의 `need_lucky_ceiling_stock` 결과(**접속 대상이 격리 DB임을 확인**)

| product_id | period_key | stock / base |
|---|---|---|
| 100 | 20261017 | 100 / 100 |
| 101 | 20261017 | 1 / 1 |
| 102 | 202610 | 1 / 1 |
| 103·104 | 행 없음 | 무제한 / 102 공유 |

- 전체 131행. npclog에 `CYCLE RESET pid=102`는 **1회**만 기록됐다(102·104가 같은 키를 쓰지만 중복 리셋 없음). 공유 설정 오류 메시지 없음.

### T4. 재시작 시 월 재고 유지 — **PASS**

- 102 재고를 0으로 만든 뒤(이번 달 판매 완료 상태 모사) run-once를 다시 실행했다.
- 102는 0 유지, `CYCLE RESET` 없음, `SQL LOAD OK rows=131`

### T5. 엔진 내 차감 판정 — **PASS**

- 출력: `evidence/runonce_impl3.log`, 픽스처 `fixtures/test_take_stock_engine.txt`(scratch `--map-config`로만 로드)
- 맵서버 스크립트 엔진에서 `S_TakeStock`과 같은 `UPDATE` → `SELECT ROW_COUNT()`를 실행한 결과: `[RefineTakeTest] A=1 B=0 C=0`
  - A: 주간 재고 → 1 (100 → 99)
  - B: 0인 공유 월 재고 → 0
  - C: 지난 주기 키 → 0

## 실행하지 못한 시험 (NOT RUN)

플레이어 접속이 필요한 시험은 클라이언트·테스트 계정이 없어 실행하지 않았다. **PASS로 기록하지 않는다.**

| 분류 | 시험 사례 | 상태 |
|---|---|---|
| 대축 천장 | 200P+1천만 보유 / 제니 부족 / 포인트 부족 | NOT RUN |
| 대축 재고 | 주 100 경계, 토 22:00 리셋 전후 실구매 | NOT RUN (SQL·재시작 수준은 T2·T4) |
| +7 포인트 | 무기·방어구 200P / 199P / 제니 0 | NOT RUN |
| 대축 → +7 | 대축 0·1·2개, 무기·방어구 선택, 취소 | NOT RUN |
| 대축 → +9 | 대축 14·15·16개, 선택 | NOT RUN |
| +9 포인트 | 2,000P+1억 / 1,999P / 99,999,999z | NOT RUN |
| +9 월 재고 | 포인트 선구매 후 대축 / 대축 선구매 후 포인트 / 무기·방어구 교차 / 같은 계정 다른 캐릭터 | NOT RUN (DB 수준은 T2) |
| 동시성 | 다른 계정 동시 클릭 | NOT RUN (DB 수준은 T2 동시 20건) |
| 시스템 장애 | SQL 끊김 중 구매, 크래시 경계 | NOT RUN (코드상 fail-closed: 8·9 반환, 차감 전) |
| 재료 안전 | 인벤토리 가득, 무게, 포장, 거래 | NOT RUN |
| 회귀 | 전장·환생·추석·룬·행알·제련 실동작 | NOT RUN (정적: 해당 파일 미변경, 전장 비율 불변) |
| 리소스 | KO/EN 화면 실제 표시 | NOT RUN (문자열은 정적 확인) |

**권장 인게임 QA 순서:** `04_DEPLOY_AND_ROLLBACK_PLAN.md` §2

## 정리

- 테스트로 만든 격리 MariaDB 프로세스와 데이터 폴더는 세션 scratchpad에만 있다. 작업이 끝나면 프로세스를 종료한다.
- 운영 DB(3306)와 기존 테이블에는 어떤 쿼리도 실행하지 않았다.

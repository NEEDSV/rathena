# 00. 구현 요약 — 대축·+7/+9 제련권 교환 정책 (2026-10-21)

## 최종 체크리스트

| # | 항목 | 상태 | 근거 |
|---|---|---|---|
| 1 | 대축 천장 200P + 1,000만 제니 | **PASS** (정적·격리 로드) / 실구매 **NOT RUN** | PID 100 `.Zeny` 10000000, 서버 주 100 유지. 격리 run-once에서 재고행 100/100 생성 |
| 2 | +7 무기/방어구 각각 200P + 0 제니 | **PASS** (정적·YAML 로드) / 실구매 **NOT RUN** | `need_barters.yml` 인덱스 0/1 = Lucky_Egg_Point 200, `Zeny: 0`. 격리 맵서버가 23개 항목을 경고 없이 읽음 |
| 3 | 대축 1개 → +7 무기/방어구 1장 (역교환 없음) | **PASS** (정적·격리 로드) / 실구매 **NOT RUN** | 신규 PID 103: 대축 1, 6230/6234 택1, 재고 제한 없음. 역방향 상품 없음(정적 검사) |
| 4 | 대축 15개 → +9 무기/방어구 1장 | **PASS** (정적·격리 로드) / 실구매 **NOT RUN** | 신규 PID 104: 대축 15, 6228/6232 택1 |
| 5 | **+9 포인트 천장 CM안: 2,000P + 1억 제니** | **PASS** (정적) / 실구매 **NOT RUN** | PID 102 `.Point` 2000, `.Zeny` 100000000 |
| 6 | **+9 포인트/대축 공유 서버 월 1개** (계정 월 1개 아님) | **PASS** (SQL 동시성 9/9, 엔진 차감 시험, 재시작 유지) / 실구매 **NOT RUN** | 104는 102의 재고 키를 쓰고 자기 재고 행을 만들지 않음. 동시 20건 중 1건만 성공 |
| 7 | 교환·재고·자원 소모 로그와 동시성/회귀 시험 | 로그 **PASS**(천장 경로) / 동시성 **PASS**(DB 수준) / 인게임 회귀 **NOT RUN** | 천장 경로(100·102·103·104)는 npclog에 경로·자원·재고가 남는다. +7 200P 바터는 기록 경로가 없어 **보유 스냅숏 대리 관측으로 운영자가 수용**(2026-10-21, B2 ACCEPTED, 03 §2.2) |

**운영 적용 가능 여부: BLOCKED (사유는 B1 하나).** 플레이어가 붙는 실제 구매 흐름(선택창, 아이템 차감·지급, 무게 초과, 취소)을 실행하지 못했다. 인게임 QA 전에는 배포하지 않는다(`02_TEST_REPORT.md`, `04_DEPLOY_AND_ROLLBACK_PLAN.md`). 바터 계측(B2)은 운영자 결정으로 차단 사유에서 제외했다.

## 기준

| 항목 | 값 |
|---|---|
| 기준 저장소·브랜치 | `E:\tools\Need\rathena` / `need-server` = `9f55324d2e545af3e48c40669425650c61fa4570` (1·2차 조사와 동일, 이후 커밋 없음) |
| 작업 브랜치 | `feature/need-refine-economy-policy-20261021` (신규) |
| 작업 worktree | `E:\tools\Need\rathena-refine-implementation` (신규) |
| 원본 작업트리 | `E:\tools\Need\rathena`의 미커밋 7건 그대로. `rathena-need-server`·조사 worktree 무변경 |
| 커밋·푸시·병합 | 하지 않음 (변경은 worktree에 미커밋) |

## 정책 대조표

| 기능 | 이전 | 구현 | 위치 |
|---|---|---|---|
| 대축 천장 (PID 100) | 200P + 1억, 서버 주 100 | **200P + 1천만**, 서버 주 100 유지 | `need_lucky_ceiling.txt` `.Zeny` 배열 |
| +7 무기/방어구 바터 | 1,000P + 1천만 | **200P + 0** | `need_barters.yml:11-28` |
| 대축 → +7 | 없음 | **대축 1 → 6230/6234 택1**, 제한 없음 | 천장 PID 103 |
| 대축 → +9 | 없음 | **대축 15 → 6228/6232 택1** | 천장 PID 104 |
| +9 포인트 천장 (PID 102) | 5,000P + 5억 | **2,000P + 1억** | `.Point`·`.Zeny` 배열 |
| +9 월 재고 | 서버 월 1 | **서버 월 1, 102·104 합산** | `.StockShareSrc/.StockShareKey` + `S_SKey` |
| +7 → 대축 역교환 | 없음 | **없음** | — |
| 계정 주/월 한도, 귀속 변경 | — | **구현하지 않음**(미승인) | — |
| 대축 13·12개 자동 인하 | — | **구현하지 않음** | — |
| 22828 포인트 미지급 | — | **분리**(이번 범위 밖) | `05_OPEN_ISSUES.md` |

## 구현 파일

| 파일 | 변경 |
|---|---|
| `npc/NEED/need_lucky_ceiling.txt` | 가격 3곳, 신규 상품 2종(17개 배열 각 2원소), 공유 재고(`S_SKey`·`S_TakeStock`·`S_CheckShare`·`S_ShareNote`), DB 조건부 차감과 주기 리셋, KO/EN 문구 |
| `npc/NEED/need_barters.yml` | +7 무기/방어구 200P + `Zeny: 0` |

## 산출물 (`docs/reports/need-refine-policy-implementation/`)

| 파일 | 내용 |
|---|---|
| `00_IMPLEMENTATION_SUMMARY.md` | 이 문서 |
| `01_CHANGE_DETAILS.md` | 파일·라인 단위 변경, 보존 근거 |
| `02_TEST_REPORT.md` | 실행 명령, 환경, PASS/NOT RUN |
| `03_LOGGING_AND_MONITORING.md` | 기록 가능·불가 범위, 집계 SQL, 4주 관측 |
| `04_DEPLOY_AND_ROLLBACK_PLAN.md` | 실행하지 않은 적용·원복 절차 |
| `05_OPEN_ISSUES.md` | 미해결·승인 필요·권고안 충돌 |
| `tools/apply_ceiling_patch.py` | 천장 스크립트 패치(CP949 바이트 안전, 고정 지점 20개) |
| `tools/static_check.py` | 정적 검사 31종 |
| `tools/sql_stock_test.py` | 격리 DB SQL 동시성 시험 9종 |
| `fixtures/test_take_stock_engine.txt` | 엔진 차감 시험용 NPC(기본 설정에서 로드되지 않음) |
| `evidence/` | 실행 출력, 격리 설정, 전체 diff |

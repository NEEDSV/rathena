# NEED 라그 마블 — V21 사냥 배치 "같은 키·다른 내용" 결함: 재현 근거와 수정 제안서 v1.0

- 작성: 2026-10-23
- 근거: G2-B1 격리 실행 `v21a` (04:18 UTC). 증거는 `doc/need_marble/g2b_evidence/run_g2b1/` 의 `v21_checks.json`·`runner.log`·map 로그.
- **제품 코드는 바꾸지 않았다.** 이 문서는 별도 승인을 받기 위한 제안이다.

## 1. 재현

시험 시계로 D0..D2 = 2026-11-20..22 를 썼다. 장애 지점은 progress 트리거, 계정은 `mbqa_v21`. 배치 2 = `{D0:5, D1:5, D2:10}`, 배치 3 = `{D2:20}` 이 이미 반영된 상태에서 시험 훅으로 체크포인트 진입점(`hunt_checkpoint`)을 직접 불렀다.

| ID | 재전송 (세션 1) | 요청서 기대 | 실제 | 원장 변화 |
|---|---|---|---|---|
| V21-2 | 배치 2, **같은 내용** | ALREADY, 변화 0 | ALREADY(-4) | 0 → **PASS** |
| V21-3 | 배치 2, D0 을 **5 → 6** | 충돌 감지·fail-closed | **ALREADY(-4)**, 모듈 정상, 로그에 충돌 표시 없음 | 0 → **FAIL** |
| V21-4 | 배치 3 에 **D3:7 추가** | 충돌 감지·fail-closed | **ALREADY(-4)**, D3 누계 0 | 0 → **FAIL** |
| V21-4b | 배치 3 앞에 **D1:4 추가** (새 기간이 먼저 정렬됨) | 충돌 감지·fail-closed | **ALREADY(-4)**. 트랜잭션 안에서 D1 행을 먼저 쓴 뒤 D2 중복에서 ALREADY 로 끝나 전체 롤백. D1 누계 5 그대로 | 0 → **FAIL** |

- 세 경우 모두 원장·잔액·활동 누계에 **잘못된 반영이나 중복은 없었다.** 계정 원장 14개 테이블 스냅샷이 같았고 불변식 위반은 0이었다. 그래서 중단 조건에는 해당하지 않았다.
- 결함은 **충돌을 감지하지 못하고 "이미 반영됨"으로 보고하는 것**이다.

## 2. 원인

`src/map/need_marble.cpp` (worktree 커밋 `f6e620f3a` 기준):

```
515  INSERT IGNORE INTO need_marble_hunt_batch (... collect_session, batch_no, period_day, ..., kills ...)
518  if (affected() == 0)
519      return NEED_MARBLE_ALREADY;   // the whole batch was applied before (it is all or nothing)
```

- 기간별 행을 차례로 넣다가 **처음으로 키가 겹친 기간**에서 곧바로 ALREADY 를 돌려준다. 이미 있는 행의 `kills` 와 비교하지 않고, 배치의 다른 기간이 DB 에 있는지·더 있는지도 보지 않는다.
- 호출자 `hunt_flush` (593~595줄)는 ALREADY 를 성공과 같게 보고 고정 배치(`b.frozen`)를 비운다.
- 따라서 같은 키로 **내용이 다른** 배치가 들어오면, 그 차이(V21-3 의 1마리, V21-4 의 D3 7마리, V21-4b 의 D1 4마리)가 **조용히 사라진다.** 운영자가 알아챌 신호도 남지 않는다.

## 3. 영향 범위

| 항목 | 판단 |
|---|---|
| 정상 코드 경로에서 발생하나 | **현재는 발생하지 않는다.** 보내는 쪽이 `b.frozen` 으로 내용을 고정하고, 응답 전까지 같은 `frozen_no` 로 같은 내용만 다시 보낸다(설계 7.3). 새 처치는 다음 번호의 `pending` 으로 간다 |
| 발생할 수 있는 경우 | 앞으로 코드를 바꾸다 고정 규칙이 깨질 때. 예: 재전송 직전 `pending` 을 `frozen` 에 합치는 수정, `batch_no` 가 재기동 없이 되돌아가는 결함, 다른 맵서버(다중 map-server)가 같은 계정·세션 번호를 쓰는 경우, 운영 도구가 같은 키로 수동 반영하는 경우 |
| 결과 | 처치 일부가 실적에서 빠진다(점수 과소 적립). 과다 적립·중복 적립은 아니다(키가 겹치면 전부 롤백되므로) |
| 원장 정합성 | 불변식(I1·I2·I9·I10·I11)은 유지된다. 누계 = 배치 합이 깨지지 않으므로 **대사로도 드러나지 않는다** |
| 심각도 제안 | 중(Medium): 정상 경로 발생 0, 그러나 발생하면 탐지 불가 |

## 4. 최소 수정안 (제안, 미구현)

ALREADY 를 돌려주기 전에 **이미 있는 배치 행 전체가 이번 요청과 정확히 같은지** 확인한다. 다르면 충돌로 처리한다.

```cpp
// hunt_checkpoint(), 518행 대체 (같은 트랜잭션 안, 롤백 전)
if (affected() == 0) {
	// The key exists: ALREADY only when the stored batch is exactly this batch, otherwise a conflict.
	if (!sq("SELECT `period_day`,`kills` FROM `need_marble_hunt_batch` WHERE `account_id`='%u' AND `collect_session`='%" PRIu64 "' "
		"AND `batch_no`='%u' ORDER BY `period_day`", aid, collect_session, batch_no))
		return NEED_MARBLE_DB_ERROR;
	std::map<uint32, uint32> stored;
	while (next_row())
		stored[static_cast<uint32>(col_i64(0))] = static_cast<uint32>(col_i64(1));
	free_result();
	if (stored == batch_without_capped_days)   // 상한으로 건너뛴 날(F2 2층)은 비교에서 뺀다
		return NEED_MARBLE_ALREADY;
	ShowError("need_marble: hunt batch conflict (account %u, session %" PRIu64 ", batch %u) - kept for the operator.\n",
		aid, collect_session, batch_no);
	return NEED_MARBLE_CONFLICT;   // 새 반환 코드
}
```

호출자(`hunt_flush`)는 `NEED_MARBLE_CONFLICT` 를 받으면:
1. **고정 배치를 비우지 않고** 그 로그인의 사냥 적립을 멈춘다(`b.session_failed = true`, 이미 있는 "발급 실패"와 같은 처리).
2. 계정을 `locked=1, lock_reason=RECON` 으로 표시한다(설계 8.4 "대사 불일치", 운영자 해제). 아니면 최소한 오류 로그를 남긴다. 둘 중 무엇을 할지는 운영 결정이다.

같이 고칠 것:
- `NEED_MARBLE_CONFLICT` 반환 코드 추가(`need_marble.hpp`).
- 비교에서 빼는 "상한으로 건너뛴 날"은 같은 트랜잭션에서 `hunt_day_maxed()` 로 정한다. 그런 날은 배치 행이 원래 없다.
- 비용: 키가 겹칠 때(= 응답 유실 후 재전송)만 SELECT 1회가 추가된다. 정상 경로에는 비용이 없다.

## 5. 수정 후 확인할 시험 (승인 시)

| 시험 | 기대 |
|---|---|
| U1 단위 시험 | 비교 함수(같음 / 처치 수 다름 / 기간 추가 / 기간 누락 / 상한 날 제외) 경계 |
| S1 정적 검사 | 518행 대체 확인, `NEED_MARBLE_CONFLICT` 처리 경로 |
| V21-2 재실행 | 같은 내용 → ALREADY, 변화 0 (회귀) |
| V21-3·4·4b 재실행 | CONFLICT, 원장 변화 0, 고정 배치 유지, 사냥 적립 중지(또는 RECON 잠금), 오류 로그 1줄 |
| V16·V21 전체 회귀 | 지금 결과와 같음 |

## 6. 결정이 필요한 것

1. 수정 여부와 시점. 정상 경로에서 발생하지 않으므로 출시 전 필수인지 판단이 필요하다.
2. 충돌 시 처리: 사냥 적립 중지만 할지, RECON 잠금(운영자 해제)까지 할지.
3. 제품 코드 수정 승인(`src/map/need_marble.cpp`, `need_marble.hpp`). 승인 전까지 코드는 바꾸지 않는다.

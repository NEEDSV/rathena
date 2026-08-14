# 여름 이벤트 debug/info 로그 카탈로그 (출시 전 일괄 제거용)

병합된 need-server 기준. 여름 이벤트 관련 개발용 debug/info 로그의 위치를 정리한다.
**범위 제외**: `need_equipment_build.cpp`, `need_autopot.cpp` 는 여름 이벤트가 아닌 별도 NEED 기능이므로 여기 포함하지 않는다.
**두 트리 주의**: 동일 파일이 fishing 워크트리(`E:\tools\Need\need-summer-fishing`)에도 있음. 아래는 운영 브랜치(need-server) 기준.

---

## 0. 코드 삭제 전, 즉시 무력화 (출시 직전 권장)
코드를 지우지 않고도 대부분의 디버그 출력은 아래 두 스위치로 끌 수 있다.

1. `conf/battle/need_summer_fishing.conf:14` → `need_summer_fishing_debug: off`
   - C++ `need_fishing.cpp` 의 ShowInfo 트레이스(§A-3) 전부 침묵.
   - 스크립트 일부 디버그(core:193/608/739 debugmes)도 침묵.
2. `$NSF_DEBUG = 0` (GM `@summergm → DEBUG 상태 → Script DEBUG OFF`, 기본값도 0)
   - 스크립트 디버그 마커/mes(§A-2) 침묵.

> 위 둘만 꺼도 운영 콘솔/클라이언트에 디버그가 안 나온다. **코드 물리 삭제는 이 카탈로그로 나중에.**

---

## A. 제거 대상 — 순수 개발 디버그 (안전 삭제)

### A-1. NPC `debugmes` — 14곳
| 파일 | 라인 | 비고 |
|---|---|---|
| need_summer_fishing_core.txt | 184 | 스팟 생성 실패(항상) |
| need_summer_fishing_core.txt | 193 | 스팟 정보 ($NSF_DEBUG 게이트) |
| need_summer_fishing_core.txt | 210 | 안내판 셀 못찾음(항상) |
| need_summer_fishing_core.txt | 317 | 도감 select 실패(항상) |
| need_summer_fishing_core.txt | 477, 480, 485 | collection select/insert/update 실패(항상) |
| need_summer_fishing_core.txt | 582, 585 | weekly_best select/insert 실패(항상) |
| need_summer_fishing_core.txt | 608 | snapshot 정보 ($NSF_DEBUG 게이트) |
| need_summer_fishing_core.txt | 739 | reward reserve 정보 ($NSF_DEBUG 게이트) |
| need_summer_fishing_test.txt | 251, 285 | 분포 시뮬 디버그(테스트 NPC) |
| need_summer_gm.txt | 262 | GM 토글 debugmes (logmes와 중복 → §B-1 참고) |

> 참고: 184/210/317/477/480/485/582/585 는 **SQL 실패 시 debugmes** 로, 삭제하면 장애 진단이 어려워질 수 있다. "완전 제거"보다 "$NSF_DEBUG 게이트로 감싸기"를 고려할 수도 있음(선택).

### A-2. NPC `$NSF_DEBUG` 디버그 액션 — ⚠ else(플레이어용) 브랜치는 유지
| 파일 | 라인 | 내용 | 주의 |
|---|---|---|---|
| need_summer_fishing_core.txt | 45 | `viewpoint` 실제 스팟 좌표 빨강 마커 | **line 46 `else` 대표점 파랑은 유지**(플레이어 안내) |
| need_summer_fishing_core.txt | 162 | `viewpointmap` 실제 스팟 빨강 | **line 163 `else` 유지** |
| need_summer_fishing_core.txt | 259~ | `if ($NSF_DEBUG) { ... }` 디버그 블록 | 블록 전체 제거 가능 |
| need_summer_fishing_core.txt | 276 | `mes "[DEBUG] account=... ip=... family=..."` | 제거 가능 |

> line 45/162 는 `if($NSF_DEBUG) 빨강 else 파랑` 구조라 **단순 줄삭제 금지**. 디버그(빨강)만 지우고 `else` 를 무조건 실행으로 바꿔야 함.
> (참고) core:524 `if ($NSF_DEBUG && @nff_rank_test_week...) return @nff_rank_test_week;` 와 test.txt:321 `if (!$NSF_DEBUG || getgmlevel()<60)` 는 **로그가 아니라 GM 테스트 훅/가드**. 로그 제거와 별개로 취급.

### A-3. C++ `need_fishing.cpp` ShowInfo 트레이스 — 10곳 (전부 `battle_config.need_summer_fishing_debug` 게이트)
```
need_fishing.cpp:87   cleanup session_id=...
need_fishing.cpp:231  finalize char_id=...
need_fishing.cpp:265  first hook char_id=...
need_fishing.cpp:295  reel round create ...
need_fishing.cpp:317  resolving session_id=...
need_fishing.cpp:367  start char_id=...
need_fishing.cpp:482  begin_reel char_id=...
need_fishing.cpp:528  complete_catch char_id=...
need_fishing.cpp:672  reel wait fired ...
need_fishing.cpp:685  reel deadline ...
```
> 모두 `if (battle_config.need_summer_fishing_debug) { ShowInfo(...); }` 형태. §0-1로 즉시 침묵 가능. 물리 삭제 시 감싸는 if 블록째 제거.

---

## B. 판단 필요 — 운영 audit/console 로그 (지울지 결정)

### B-1. NPC `logmes` — GM 변경 감사
- `need_summer_gm.txt:261` — GM이 상점/DEBUG 토글 시 npclog 기록. **감사 목적이면 유지 권장.**

### B-2. C++ `ShowInfo` 운영 audit/console 로그
| 파일 | 라인 | 내용 |
|---|---|---|
| need_summer_fishing_reward.cpp | 425 | 랭킹 보상 우편 지급 완료 로그 |
| need_summer_attendance.cpp | 1000, 1101 | 출석 outbox delivered / 콘솔 로그 |
| need_summer_hunt.cpp | 112 | 황금수박 획득 콘솔 로그 |
| need_summer_shop.cpp | 337, 340 | 상점 처리 콘솔 로그 |
> 이들은 "지급/획득 감사" 성격. 운영 추적에 쓰이면 유지, 콘솔 스팸이면 제거. **NEED Summer Monitor 웹과 무관**(웹은 DB 로그 사용).

---

## C. 유지 권장 — 에러/경고 처리 (지우면 장애 진단 불가)
아래는 디버그가 아니라 **오류/경고 리포트**다. 출시 후에도 유지 권장.

- `ShowError(...)` — 스키마 없음/아이템 미로드/fail-closed/설정 무효:
  need_summer_fishing_reward.cpp(116/130/152/165/172/207), need_summer_attendance.cpp(195/216/238/252/260/765/1164), need_summer_hunt.cpp(74/117/133/149/163/171/263/453), need_summer_shop.cpp(142/385)
- `ShowWarning(...)` — outbox REVIEW/RETRY, origin-log insert 실패 등:
  need_summer_fishing_reward.cpp(215/218), need_summer_reward_log.cpp(33/62), need_summer_attendance.cpp(775/777/1164)
- `Sql_ShowDebug(mmysql_handle)` / `SqlStmt_ShowDebug(...)` — SQL 오류 덤프:
  need_summer_fishing_reward.cpp(100), need_summer_reward_log.cpp(61), need_summer_attendance.cpp(176), need_summer_hunt.cpp(148/161/255/289/297/300/304/408/415), need_summer_shop.cpp(166/176/353/401/407/432/465/472)

---

## D. 일괄 검색 패턴 (나중에 제거 작업 시)
```bash
# NPC 디버그
grep -rnE "debugmes|\$NSF_DEBUG" npc/NEED/need_summer_*.txt
# C++ 낚시 트레이스 (need_summer_fishing_debug 게이트)
grep -nE "need_summer_fishing_debug" src/map/need_fishing.cpp
grep -nE "ShowInfo" src/map/need_fishing.cpp
# C++ audit ShowInfo
grep -rnE "ShowInfo" src/map/need_summer_*.cpp
# 유지 대상(에러/경고) — 실수 삭제 방지용 확인
grep -rnE "ShowError|ShowWarning|Sql_ShowDebug|SqlStmt_ShowDebug" src/map/need_summer_*.cpp src/map/need_fishing.cpp
```

## E. 요약
- **출시 직전 빠른 조치**: `need_summer_fishing_debug: off` + `$NSF_DEBUG=0` (코드 삭제 X).
- **완전 제거(나중)**: §A 전부, §B는 선택. §C는 유지.
- **주의**: §A-2 viewpoint(45/162)는 else 플레이어 브랜치 보존. need_fishing.cpp ShowInfo는 감싸는 if째 제거.
- **테스트 NPC 통째 제거**도 고려: `need_summer_fishing_test.txt` 는 GM 전용 테스트 도구라 출시 시 로더(`scripts_need.conf:87`)에서 주석 처리하면 관련 디버그(251/285 등)도 함께 빠짐.

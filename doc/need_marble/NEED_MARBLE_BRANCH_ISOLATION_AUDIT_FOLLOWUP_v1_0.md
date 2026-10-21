# NEED 라그 마블 — 브랜치 격리 감사 (F1·F2 후속) v1.0

- 작성일: 2026-10-21 (확인 시각 09:13 UTC 작업 전, 09:30 UTC 작업 후)
- 요청: `NEED_MARBLE_F1_F2_FOLLOWUP_VALIDATION_Claude_Request_v1_0.md` 1절(G0)·6.4
- 결론: **G0 PASS.** 모든 수정은 독립 worktree 안의 `tests/`·`doc/` 에만 있었고, 원본 저장소·원본 DB·dqtest·운영 CP 는 건드리지 않았다. 격리 DB·서버는 띄우지 않았다.

## 1. 독립 worktree 확인 (작업 전, 읽기 전용)

| 항목 | 값 |
|---|---|
| `git worktree list --porcelain` | `E:/tools/Need/need-marble-phase-bc-v1` → `refs/heads/feature/need-marble-phase-bc-v1` (원본 `E:/tools/Need/rathena` → `need-server` 와 별개 항목) |
| `git branch --show-current` | `feature/need-marble-phase-bc-v1` |
| `git rev-parse --show-toplevel` | `E:/tools/Need/need-marble-phase-bc-v1` |
| `git rev-parse HEAD` (작업 전) | `19d6984e1632b838f9b6787bbfef19beea474247` |
| `git status --porcelain` (작업 전) | 빈 출력 |
| 보고 커밋 `f6e620f3a` | 현재 HEAD 의 조상(`merge-base --is-ancestor` exit 0), 이 브랜치에만 포함(`branch --contains`) |
| 원격 추적 | `branch -r --contains f6e620f3a` 빈 출력, 브랜치 upstream 설정 없음 → 푸시된 적 없음 |
| 정션·심볼릭 링크 | worktree 아래 재해석 지점 0개 |

## 2. 원본·다른 작업 기준선

| 항목 | 작업 전 (09:13 UTC) | 작업 후 (09:30 UTC) | 판단 |
|---|---|---|---|
| 원본 저장소 브랜치·HEAD | `need-server` `9f55324d2` | 같음 | 불변 |
| 원본 `git status --porcelain` | 7줄(M 5, ?? 2: `conf/map_athena.conf`, `conf/maps_athena.conf`, `db/map_index.txt`, `db/re/map_cache.dat`, `npc/scripts_need.conf`, `need_marble_motion_v1_1.txt`, `need_marble_motion_v1_2.txt`) | 같은 7줄 | 불변(기존 WIP) |
| 원본 `map-server.exe` | 2026-10-19 08:50:06 UTC, `a28dee9f…` | 같음 | 불변 |
| 원본 MariaDB 3306 | PID 8680 (2026-10-20 02:13 UTC 시작) | PID 8680 | 불변 |
| dqtest MariaDB 3307 | PID 68040 (`E:\tools\Need\dqtest\mariadb-data\my.ini`, 09:08:50 UTC 시작) | 프로세스 없음 | 아래 참고 |
| mhtest MariaDB 3309 | PID 61728 (`E:\tools\Need\mhtest\mariadb-data\my.ini`, 09:10:51 UTC 시작) | 프로세스 없음 | 아래 참고 |
| dqtest login/char/map | 실행 중 아님 | 실행 중 아님 | - |
| 격리 후보 포트 3327·26901·26121·25121·26906 | 비어 있음 | 비어 있음 | - |

**dqtest·mhtest 변화**: 지난 F1/F2 보고서의 dqtest MariaDB PID 20184 는 이번 작업 시작 전에 이미 68040(09:08:50 UTC 시작)으로 바뀌어 있었다. mhtest 는 이번에 처음 관측했다. 두 프로세스 모두 작업 후 확인 시 없었다. 이 작업이 실행한 명령은 `git`(읽기·worktree 안 add), `python -I`(오프라인 시험·정적 검사), `cl`·`MSBuild`(worktree·스크래치 빌드), `robocopy`(worktree → 스크래치), 스크래치 폴더 삭제뿐이고 프로세스 종료·기동 명령은 0건이다. 두 DB 는 다른 작업(다른 세션)의 것으로 보고 손대지 않았다.

## 3. 빌드 경로

| 빌드 | 경로 | 원본 exe 덮어쓰기 |
|---|---|---|
| worktree 운영용 map-server | `SolutionDir=E:\tools\Need\need-marble-phase-bc-v1\` → 산출물 `need-marble-phase-bc-v1\map-server.exe` | 없음(원본 exe 시각·해시 불변). 소스 무변경이라 링크도 일어나지 않음, 해시 `fc2718c2…` 그대로 |
| 시험 빌드(훅 포함) | 소스 복사본 `<세션 스크래치>\fu_testsrc`(`.git` 없음, `apply_test_hooks.py` 가 `.git` 있으면 거부), 산출물 `<세션 스크래치>\fu_testbuild` | 없음. 둘 다 작업 끝에 삭제(126 MB + 823 MB) |
| 단위 시험 | `<세션 스크래치>\fu_u1` | 해당 없음 |

운영용 exe 에 시험 문자열(`need_marble_test`, `MBQA`, `mbqafeed`) 0건. `src/` 와 빌드 파일에 시험 훅 코드 0건(정적 검사 9절).

## 4. 원본 DB·운영 CP 접촉

| 항목 | 결과 (확인 가능한 범위) |
|---|---|
| 원본 3306 `ragnarok` 접속 | 0건. 이번 작업은 어떤 DB 에도 접속하지 않았다(러너 건조 실행은 가짜 DB 응답 사용) |
| dqtest·mhtest DB 접속 | 0건 |
| 격리 DB 생성·기동 | 0건 |
| `#CASHPOINTS`·`acc_reg_num` 쓰기 | 0건(DB 접속 자체가 없음). 마블 코드에 `#CASHPOINTS` 문자열 없음(정적 검사) |
| 자격 증명 | 조회·출력·재사용 0건. 새 합성 SQL 은 `{{user}}` 자리표시자만 |

## 5. Git 원격·병합

| 항목 | 결과 |
|---|---|
| `git push` | 실행하지 않음 |
| 원격 브랜치·PR | 만들지 않음 |
| `main`·`master`·`need-server`·`feature/need-marble` 체크아웃·병합·리베이스·리셋·stash·clean | 0건 |
| 로컬 커밋 | `feature/need-marble-phase-bc-v1` 에만(해시는 `NEED_MARBLE_F1_F2_FOLLOWUP_REPORT_v1_0.md` 에 기록) |

참고: `tests/need_marble/g2/__pycache__/` 의 `bcclient`·`bcdb` `.pyc` 2개가 이전 커밋에 들어가 있다(이번 변경 아님, 내용 무해). 이번 작업에서 새로 생긴 `.pyc` 2개는 지웠다.

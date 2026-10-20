# NEED 라그 마블 — 브랜치 격리 감사 v1.0

- 작성일: 2026-10-20
- 대상: G0(브랜치·worktree 생성) ~ M5(보고서 커밋)
- 근거: 작업 전 기록(G0)과 작업 후 재측정의 비교. 측정 원본은 세션 스크래치 `g0/before_status.txt`, `before_sha.txt`, `before_refs.txt`.

## 1. G0 — 생성 전 기록 (원본 저장소, 읽기 전용)

| 항목 | 값 |
|---|---|
| 저장소 경로 | `E:/tools/Need/rathena` |
| 현재 브랜치 / HEAD | `need-server` / `0c69fa682a50ef48baf464e4350c947ba2538c65` |
| 작업 트리 상태 | 수정 8개(`conf/map_athena.conf`, `conf/maps_athena.conf`, `db/import/mob_db.yml`, `db/import/mob_item_ratio.yml`, `db/map_index.txt`, `db/re/map_cache.dat`, `npc/re/mobs/dungeons/lhz_dun_n.txt`, `npc/scripts_need.conf`) + 미추적 2개(`need_marble_motion_v1_1.txt`, `_v1_2.txt`) |
| worktree 목록 | 9개(원본 + need-summer-2026, need-summer-fishing, rathena-dq, rathena-eden, rathena-ep162, rathena-need-server, rathena-wiki-integ, rathena-wiki-w4b) |
| 기준 커밋 | `feature/need-marble` = `373dcfe279153dc1feaf233d980f37955d6a8beb` — 요청서 기준과 일치 |
| 원격 | origin = NEEDSV/rathena, upstream = rathena/rathena. `feature/need-marble` 에 upstream 설정 없음 |
| 훅 | `.git/hooks` 에 sample 외 훅 없음, `core.hooksPath` 미설정 → 커밋 시 자동 푸시 없음 |
| 자동 동기화 위험 | 다른 세션의 유지보수 동기화는 `feature/update26b808`(rathena-need-server)과 `need-server` 사이에서만 돈다(기록된 운영 사실). 새 브랜치는 그 흐름에 없고 원격에 없음 |
| 대상 경로 | `E:/tools/Need/need-marble-phase-bc-v1` 없음(생성 전 확인) |

## 2. 생성과 검증

```
git -C "E:/tools/Need/rathena" worktree add -b feature/need-marble-phase-bc-v1 "E:/tools/Need/need-marble-phase-bc-v1" 373dcfe279153dc1feaf233d980f37955d6a8beb
```

| 검사 (새 worktree 안) | 기대 | 결과 |
|---|---|---|
| `git branch --show-current` | feature/need-marble-phase-bc-v1 | 일치 |
| `git rev-parse --show-toplevel` | E:/tools/Need/need-marble-phase-bc-v1 | 일치 |
| `git rev-parse HEAD` | 373dcfe27… | 일치 |
| 생성 직후 원본 HEAD·브랜치·status·작업 파일 체크섬 | G0 과 동일 | 동일 |
| 심볼릭 링크·정션 | 없음 | `dir /AL /S` 결과 없음, 폴더 LinkType 없음 |
| upstream | 없음 | 없음 |

## 3. 작업 후 재측정 (M4 커밋 뒤, 문서 커밋 전)

| 항목 | 결과 |
|---|---|
| 원본 브랜치 / HEAD | `need-server` / `0c69fa682…` — **변화 없음** |
| 원본 `git status --porcelain` | G0 기록과 **동일** |
| 원본 수정·미추적 파일 SHA-256 (10개 + v1.2) | G0 기록과 **동일** |
| 원본 브랜치 참조(`refs/heads`) | 기존 13개 모두 **동일**, 추가는 `feature/need-marble-phase-bc-v1` 1개뿐 |
| `need-server`, `master`, `feature/need-marble` | 변화 없음 (요청서의 `main` 브랜치는 이 저장소에 없음, 기본 브랜치는 `master`) |
| 원본 `map-server.exe` | 6,482,944 바이트, 2026-10-19 08:50 그대로 |
| `.git` 공용 메타데이터 변화 | 새 브랜치 참조와 `worktrees/need-marble-phase-bc-v1` 항목(worktree 생성에 필요) 외 없음 |

## 4. 빌드 산출물 위치

| 산출물 | 위치 | 원본 영향 |
|---|---|---|
| 의존 lib(ryml, libconfig, yaml-cpp, common) | `need-marble-phase-bc-v1\.vs\build\` | 없음 (`$(SolutionDir)` = 새 worktree) |
| map-server.exe | `need-marble-phase-bc-v1\map-server.exe` | 없음 |
| 단위 시험 exe·obj | 세션 스크래치 `logic_test\` | 없음 |
| 빌드 로그 | 세션 스크래치 `build\` | 없음 |

vcxproj 의 OutDir/IntDir 는 모두 `$(SolutionDir)` 기준이며 절대경로가 없음을 빌드 전에 확인했다.

## 5. DB·서버·프로세스 접근

| 대상 | 접근 |
|---|---|
| 원본 MariaDB 3306 `ragnarok` | **0건** (이번 작업에서 mysql 클라이언트·스크립트로 접속하지 않음) |
| dqtest, 기타 DB | 0건 |
| 격리 MariaDB 생성·기동 | 0건 (G2 미승인) |
| login/char/map 서버 기동·종료·리로드 | 0건 |
| 기존 QA 자격증명 조회 | 0건 |
| 실행한 외부 프로세스 | git, MSBuild/cl(새 worktree 대상), python(정적 검사·오프라인 계산), 파일 체크섬 |

## 6. 푸시·병합

| 항목 | 결과 |
|---|---|
| `git push` | 실행 안 함 |
| 원격 브랜치 | `origin` 에 marble 관련 브랜치 없음 (`git branch -r` 확인) |
| upstream 설정 | 없음 |
| 병합·리베이스·리셋·stash·clean | 원본·기존 브랜치에서 실행 안 함 |
| PR | 만들지 않음 |

## 7. 판정

원본 작업 트리·기존 브랜치·원본 DB·서버 프로세스 변경 **0건**. 모든 코드·빌드 변경은 새 worktree 와 새 브랜치 안에만 있다. 로컬 커밋만 존재하며 푸시하지 않았다.

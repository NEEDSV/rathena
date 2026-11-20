# NEED 봉인된 신전 바포메트 최초 3초 무적 조사 및 수정

작성일: 2026-11-20 (Asia/Seoul)

판정: **로컬 원인 확인·최소 수정·실제 스크립트 파서 검증 완료. 실전 전투 검증은 미실시.**

## 확인된 원인

첨부 문서의 일반 스크립트와 실제 NEED 전용 스크립트는 다르다.

- `src/config/renewal.hpp`에서 Renewal이 활성화되어 있다.
- `npc/re/scripts_main.conf:35` → `npc/scripts_need.conf:75` → `npc/NEED/instances/SealedShrine.txt` 경로로 NEED 버전을 로드한다.
- `conf/map_athena.conf`의 DB 경로는 `db`, `conf/inter_athena.conf`의 `use_sql_db`는 `no`이다. 해당 import 설정에는 이를 바꾸는 설정이 없다.
- `src/map/mob.cpp::mob_load`는 Renewal 스킬 DB 이후 import 스킬 DB를 읽는다. 이 체크아웃에서는 `db/re/mob_skill_db.txt:5092–5094`의 1929 자동 `NPC_INVINCIBLE` 세 행이 이미 주석 처리되어 있다. 각 행은 idle/chase/attack, 확률 10000, 캐스팅 1000ms, 재시도 지연 60000ms지만 **비활성 행**이다.
- `db/import/mob_skill_db.txt`에는 1929 또는 전역 몬스터 대상으로 이를 다시 활성화하는 정의가 없다. 활성 설정에서 1929에 적용되는 스킬 ID 685 행은 0개다. Pre-renewal DB에는 자동 시전 행이 남아 있으나 현재 빌드 설정에서는 사용하지 않는다.
- 실제 원인은 NEED 스크립트의 수정 전 1251행이다. `control_baphomet::OnEnable`에서 몬스터를 생성한 직후 캐스팅 시간 0으로 `NPC_INVINCIBLE`을 직접 실행한다. 따라서 스크립트상 최초 피해 구간이 없었다.

이 결론은 현재 로컬 체크아웃에 대한 확인이다. 별도 운영 서버의 배포 파일·실행 바이너리·런타임 설정이 같은지는 확인하지 않았다.

## 적용한 수정

게임 동작 변경은 `npc/NEED/instances/SealedShrine.txt`의 `control_baphomet` 블록에만 한정했다. 11행 추가, 2행 삭제다.

1. 생성 직후 무적 시전을 제거하고 `initnpctimer`를 시작한다.
2. `OnTimer3000`에서 생존한 보스를 확인한 뒤 캐스팅 시간 0으로 무적을 적용한다.
3. 보스가 이동해도 적용되도록 후속 재무적과 같은 `(79,64), range=150`을 사용한다. 대상 맵은 `instance_mapname("2@cata")`이므로 다른 인스턴스와 일반 맵에 전파되지 않는다.
4. `OnDisable`/`OnInstanceInit`에서 타이머를 중지한다. 3초 이내 처치 시 기존 사망 정리 경로가 `OnDisable`을 호출한다. 타이머 이벤트 자체에도 생존 검사와 중지 처리가 있다.

설계상 생성 후 0–3초는 피해를 허용하고, 약 3초에 무적으로 전환한다. 타이머는 서버 이벤트 루프에서 처리되므로 실제 전환 시각은 부하에 따라 늦어질 수 있다. 피해 적용 여부와 정확한 지연은 아래 실전 검증이 필요하다.

몬스터/스킬 DB, 공용 엔진, 기본/Pre-renewal 스크립트는 변경하지 않았다. 파일의 CP949 인코딩과 CRLF를 유지했다.

## 봉인 및 상태 처리 조사

- `NPC_INVINCIBLE`은 `StatusSkillImpl`을 거쳐 `SC_INVINCIBLE`을 적용한다. 현재 Renewal 스킬 DB의 지속시간은 `Duration1: -1`이다.
- `src/map/skills/npc/invincibleoff.cpp`는 `status_change_end(target, SC_INVINCIBLE)`을 직접 호출한다. 이 경로에는 별도의 `SC_INVINCIBLEOFF` 상태를 기다리는 로직이 없다.
- `src/map/battle.cpp`의 공격 차단 경로는 `SC_INVINCIBLE`을 검사한다. 실제 무적 피해 차단과 해제는 전투 재현으로 추가 확인해야 한다.
- 봉인은 `(79,81)`, `(123,109)`, `(123,22)`, `(35,21)`, `(35,109)`의 다섯 곳이다. 현재 `CIRCULAR_AREA`는 비활성이므로 범위 10은 `abs(dx) <= 10 && abs(dy) <= 10`인 정사각 범위다.
- NEED 버전은 범위 안에 1929가 있는지 먼저 확인하고, 없으면 비용이나 봉인을 소모하지 않고 대화를 끝낸다. 성공 시 `NPC_INVINCIBLEOFF`를 실행한다.
- 최초 중앙 봉인은 소환 약 26초 후 활성화된다. 봉인 성공 시점부터 70초 타이머가 시작되고, 그 뒤 무적을 다시 적용하면서 다음 봉인을 선택한다. 이 부분은 원본 바이트 그대로 유지했다.
- 소환 위치 `(79,64)`는 중앙 봉인과 Y축으로 17칸 떨어져 있으므로 유인이 필요하다.

첨부 문서와 달리 **기존 NEED 봉인 블록에는 퀘스트 3041을 설정하거나 검사하는 재사용 제한이 없다.** 3041 삭제 코드만 다른 정리 경로에 남아 있다. 기존 활성화 비용은 HP 감소가 아닌 **SP 50% 감소·석화 20초 요청**이다. 최초 3초 문제와 별개이므로 이 동작을 유지했다. 3041 쿨다운 보장까지 포함한 문서의 전체 PASS 기준은 현재 충족했다고 판정할 수 없다.

참고로 [iRO Wiki의 2층 공략](https://irowiki.org/wiki/Sealed_Shrine#2nd_Floor)에서도 생성 직후 3초 피해 구간을 설명한다. 제단 비용·재사용 제한 등 나머지 설명을 NEED의 현재 구현과 동일하다고 취급하지 않았다.

## 수행한 검증과 산출물

검증 명령:

```powershell
python doc/need_sealed_shrine/verify.py --parser docs/reports/need-doram-release-gate-20261118/parser/parser_probe.exe
git diff --check -- npc/NEED/instances/SealedShrine.txt
```

결과:

```text
PASS: 28 actual script blocks; SQL and sockets not initialized
PASS: DB audit, encoding, timer placement and unchanged encounter blocks
REAL_PARSER_RESULT blocks=28 failures=0 sql_initialized=0 sockets_initialized=0 server_started=0
```

- 파일 내 28개 스크립트 본문 전체를 기존 격리 파서의 실제 `parse_script`로 컴파일했다. 파서용 `script_probe.cpp`가 현재 `src/map/script.cpp`에서 SQL 기반 `mapreg_init` 호출만 제외한 동일 소스인지 검사했다.
- 무적 스킬 활성 행, 타이머 배치·중지, CP949/CRLF, 변경 대상 외 모든 블록의 바이트 동일성을 검사했다.
- `git diff --check`를 통과했다. C++ 변경이 없어 서버 재빌드는 필요하지 않다.
- 재실행 도구: `doc/need_sealed_shrine/verify.py`
- 상세 DB 행·파일 해시: `docs/reports/need-sealed-shrine-20261120/audit.json`
- 실제 파서 로그: `docs/reports/need-sealed-shrine-20261120/parser.log`
- UTF-8로 읽을 수 있는 최소 diff: `docs/reports/need-sealed-shrine-20261120/minimal.diff`

이 검증은 문법 및 정적 조사다. 타이머 실행, 공격 피해, 실제 `SC_INVINCIBLE` 적용·해제, 클라이언트 동작을 실행한 테스트가 아니다.

## 미실시 실전 검증과 기록 방법

현재 로컬에는 실행 중인 rAthena 서버가 없고 격리된 테스트 서버/GM 클라이언트 세션도 확보되지 않았다. 운영 서버를 시작하거나 NPC를 재로드하지 않았다. 다음 항목의 재현 로그·실제 플레이 스모크 결과는 **미확인**이며, 예상값을 실측 로그로 제시하지 않는다.

테스트 환경에서는 보스 생성 직후의 서버 tick을 T0로 잡고 다음을 기록한다. 공격은 빗나감·속성 무효·회복과 구분할 수 있는 동일 공격을 사용하고, 낮은 피해로 보스가 생존하도록 한다.

| 시점 | 기록할 항목 | 기대 결과 |
| --- | --- | --- |
| T0, +500, +1000, +2000ms | 실제 tick, GID, 좌표, 전후 HP, 피해량, SC_INVINCIBLE 유무 | 상태 없음, 정상 명중 시 HP 감소 |
| +3000ms | 타이머 진입 tick, 685 시전 시작/종료, 상태 적용 tick, 전후 HP | 첫 685 시전·무적 적용, 이후 공격 차단 |
| +4000ms | 실제 tick, 피해량, SC_INVINCIBLE 유무 | 무적 유지 |

디버그 로그는 테스트용 엔진의 몬스터 생성 경로, `buildin_mobuseskill_sub`의 685/686 호출 전후, `SC_INVINCIBLE` 적용/종료 경로에서 `tick, map, gid, skill, phase, status, hp`를 기록한다. 스크립트는 cast time 0을 전달하므로 시작/완료가 같은 tick일 수 있다. 피해 로그는 공격 시도와 실제 HP 감소를 모두 포함한다.

봉인별로 아래를 반복한다.

1. 중심에서 X 또는 Y축으로 11칸 밖에서 클릭: 무적 유지, 비용·봉인·70초 타이머 소모 없음.
2. 축 방향 10칸, 그리고 모서리 `(10,10)`에서 클릭: 무적 해제, 공격 피해 복구, 봉인 비활성화, SP 비용 및 석화 요청.
3. 성공 시점부터 +69999ms까지 피해 가능 여부를 확인하고, +70000ms 근처 재무적·다음 봉인 활성화를 기록한다. 다른 봉인에서 2회 이상 반복한다.
4. 최초 3초 이내 처치, 3초 이내 인던 파괴, 보스를 소환 위치에서 10칸 밖으로 이동시키기, 별도 파티 인스턴스 및 다른 맵의 1929 무영향을 확인한다.
5. 3041은 기존 NEED 구현에 적용되지 않음을 별도로 확인한다. 문서의 3041 쿨다운을 요구사항으로 채택할 경우 별도 수정·검증이 필요하다.

위 실전 검증 전에는 운영 적용 완료 또는 전체 PASS로 표시하지 않는다.

## 유저 게시용 영어 답변 초안

Thanks for the report. We found that the NEED-specific Sealed Shrine script applies Baphomet's invulnerability immediately when he spawns. We have prepared a fix that delays the initial invulnerability until three seconds after spawning, while preserving the existing seal cycle. Local script validation has passed; in-game timing and combat checks are still pending. After the initial window, you still need to lure Baphomet close to the active seal and activate it to make him vulnerable again.

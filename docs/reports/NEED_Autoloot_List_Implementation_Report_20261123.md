# NEED 자동 루팅 통합 조회 구현 보고서

작성일 2026-11-23 · 작업 대상 `E:\tools\Need\rathena` · `need-server` 기준 커밋 `224ea8f87`

## 구현 결과

일반 유저 그룹인 `Player`(ID 0)에서 `@alootlist` 한 번으로 자기 캐릭터의 자동 루팅률, 제외 아이템, 활성 아이템 유형, 선택 루팅 아이템을 볼 수 있습니다. 빈 목록은 `None`, 루팅률이 0이면 `OFF`로 표시합니다. 유형은 코드순, 두 아이템 목록은 각각 ID 오름차순으로 표시합니다. 두 목록에 같은 아이템이 등록되어 있으면 각각의 설정에 표시합니다.

아이템 이름은 기존 `item_display_name(id, sd->need_lang)`으로 조회합니다. EN 세션에서는 영문 이름이 있는 경우 해당 이름을 사용하고, 그 외에는 서버 기본 이름을 사용합니다. 이름을 찾을 수 없는 경우에도 해당 ID와 `Unknown item`을 출력합니다. 제목과 상태 문구는 두 언어 환경에서 공통으로 읽을 수 있게 ASCII로 작성했습니다. 유형명은 기존 `itemdb_typename`에서 가져옵니다. 행마다 서버 채팅 출력 함수 `clif_displaymessage`를 사용하며, 목록 순회 범위는 실제 세션 배열 크기로 결정합니다.

## 기존 명령어와 저장 구조

| 명령어 | 구현 위치와 세션 데이터 | 저장과 동작 |
| --- | --- | --- |
| `@autoloot` | `src/map/atcommand.cpp`의 `autoloot`. `state.autoloot`는 0부터 10000까지의 정수이며 단위는 0.01% | 성공적으로 변경하면 `NeedAutoLootRate`에 저장 |
| `@noloot` | `src/custom/atcommand.inc`의 `noloot`. `state.noloot_itemid`와 `state.noloot_count`로 관리하는 최대 30개 제외 목록 | 현재 접속 세션에 적용. `src/map/mob.cpp`의 자동 루팅 판단에서 제외 항목이 루팅률·유형·선택 ID에 우선함 |
| `@autoloottype` | `src/map/atcommand.cpp`의 `autoloottype`. `state.autoloottype`에 유형별 비트를 저장 | `NeedAutoLootType`에 저장. 예를 들어 실제 코드에서 4는 Armor, 5는 Weapon |
| `@alootid` | `src/map/atcommand.cpp`의 `autolootitem` 별칭. `state.autolootid` 배열에 등록하며 현재 `AUTOLOOTITEM_SIZE`는 20 | `NeedAutoLootId`에 기존 슬롯 위치대로 저장. 0은 빈 슬롯 |

저장 대상인 세 설정은 `src/map/pc.cpp`의 `pc_autoloot_settings_save`를 통해 기존 `char_reg_num` 레지스트리에 반영됩니다. 접속 시 `pc_reg_received`에서 `NeedAutoLootSaved`로 저장 여부와 형식을 확인한 뒤 캐릭터별로 복원합니다. `@alootlist`는 호출 시점의 세션 상태를 읽으며 저장 함수를 호출하지 않습니다.

JF 패턴 제한 중에는 기존 정책에 따라 저장값이 남아 있어도 접속 세션의 자동 루팅률·유형·선택 ID가 초기화될 수 있습니다. 조회 결과는 이때의 세션 상태와 제한 안내를 표시합니다. 기존 `@alootid`, `@autoloottype`, `@noloot list`의 개별 조회를 확인했으며, 구현 전 소스·별칭·NPC 바인딩에 `alootlist` 이름 충돌은 없었습니다. `@autoloottype` 도움말과 문서의 4·5 표기를 실제 유형 코드에 맞춰 `Armor = 4`, `Weapon = 5`로 수정했습니다.

## 변경 파일

| 파일 | 변경 내용 |
| --- | --- |
| `src/custom/atcommand.inc` | 조회 핸들러와 공통 아이템 목록 출력 추가. 캐릭터 대상 명령인 `#alootlist`는 핸들러에서 차단 |
| `src/custom/atcommand_def.inc` | 기존 커스텀 atcommand 목록에 `alootlist` 등록 |
| `conf/groups.yml` | 일반 유저 그룹의 `Commands`에 `alootlist: true` 추가 |
| `conf/atcommands.yml` | 조회 명령 도움말 추가 |

## 적용 방법

1. 위 4개 파일을 운영 소스에 반영합니다. 운영 설정에 별도 그룹 파일이 있다면 실제 일반 유저 그룹에 권한 항목을 함께 배포합니다.
2. 운영 환경의 기존 옵션으로 `map-server`를 빌드합니다. Windows에서는 `rAthena.sln`의 `map-server` 프로젝트, Linux에서 기존에 configure를 완료한 빌드 트리라면 `make map`을 사용합니다.
3. 점검 중 새 `map-server` 바이너리와 두 YAML 파일을 함께 반영하고 map-server를 재시작합니다. 새 핸들러가 바이너리에 들어가므로 설정 리로드만으로는 등록할 수 없습니다. 이 변경을 위한 login-server·char-server 재빌드, DB 마이그레이션, NPC 및 클라이언트 리소스 배포는 필요 없습니다.
4. 일반 계정에서 `@alootlist`를 실행해 네 항목을 확인합니다. 되돌릴 때는 이전 바이너리와 두 YAML 파일을 복원한 뒤 map-server를 재시작합니다.

## 검증 결과

Windows Visual Studio 2022 Release x64에서 변경된 map-server의 컴파일과 링크가 성공했습니다. 오류는 0건이며, 기존 `src/map/atcommand.cpp`의 문자 인코딩 경고 C4828이 4건 발생했습니다. 변경된 4개 파일에 대한 `git diff --check`도 통과했습니다.

루프백 전용 격리 서버와 분리된 MariaDB 스키마에서 실제 명령 패킷을 보내고 채팅 응답 및 레지스트리 행을 비교했습니다. 헤드리스 스모크는 **14/14 통과**했습니다.

| 검증 항목 | 실제 관찰 결과 |
| --- | --- |
| 일반 계정, 모든 설정 미사용 | 그룹 0 계정에서 `OFF`와 세 개의 `None` 확인. 조회로 생성된 자동 루팅 레지스트리 행 0개 |
| 루팅률만 설정 | `@autoloot 12.34` 입력 후 `ON (12.34%)` 확인 |
| 제외 목록과 활성 유형 | 최대 30개 제외 아이템의 ID와 이름을 확인. 유형 3, 4, 5, 6의 코드와 이름 확인 |
| 선택 아이템 10개 초과 및 설정 조합 | 선택 루팅 최대 20개를 역순으로 등록한 뒤 20개 모두 이름과 함께 오름차순으로 한 번씩 수신. 기존 `@alootid` 조회에서도 20개 확인 |
| 저장과 재접속 | 로그아웃 전에 선택 ID 레지스트리 20개 확인. 재접속 후 루팅률·유형·선택 ID 20개 복원. 제외 목록은 세션 정책에 따라 비워짐 |
| 캐릭터 분리 | 동일 계정의 두 번째 캐릭터에는 첫 번째 캐릭터의 설정이 표시되지 않음. GM의 `#alootlist "Aloot One"` 호출에 대상 설정이 포함되지 않음 |
| 반복 조회 | 연속 3회에서 동일한 출력과 동일한 `NeedAutoLoot*` 레지스트리 행 확인 |
| KR/EN 세션 | KR 세션의 CP949 채팅 패킷에서 `501: 빨간포션`, EN 로그인 플래그를 쓴 세션에서 `501: Red Potion` 수신 확인 |
| JF 제한 | 계정 제한을 설정한 뒤 재접속해 안내와 실효 상태 `OFF`를 확인. 기존 저장 행은 동일 |

증적은 격리 테스트 폴더 `E:\tools\Need\hwtest\alootlist_20261123`의 `map-build.log`와 `smoke.log`에 있습니다. 격리 DB에는 핵심 스키마만 넣어 일부 행사 기능에서 테이블 누락 메시지가 발생했습니다. 검증한 자동 루팅 명령과 세션·레지스트리 동작에는 영향이 없었습니다.

실제 KR/EN 게임 클라이언트의 채팅 UI, DB에서 삭제된 아이템의 이름 대체 경로, 사냥 드롭, 부하 측정, Linux 빌드, 운영 배포는 미실행입니다.

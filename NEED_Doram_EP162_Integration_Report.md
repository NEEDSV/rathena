# NEED 도람족 EP16.2 통합 감사·수정 보고서

> **운영 방향 변경(2026-11-18):** 이 문서의 라자냐 NPC/퀘스트 전면 복원 부분은 [구 페이욘 통합·퀘스트 정리 보고서](F:/rathena/NEED_Doram_EP162_OldPayon_NPC_Quest_Consolidation_Report.md)로 대체되었다. 기존 전투·장비 옵션 수정 기록은 유지한다. 현재 활성 NPC/퀘스트 상태는 새 보고서를 기준으로 확인한다.

- 작성일: 2026-11-18 (Asia/Seoul)
- 작업 위치: `F:\rathena`
- 브랜치: `codex/need-pvp-dummy-vellum`
- 기준 커밋: `70511fcbb0ce42b6c7dfc33268545580bea4144e`
- 상태: **소스·DB·NPC 통합 수정 및 오프라인 검증 완료 / 테스트 서버 적용·실제 접속 검증 대기**.
- 커밋·push·병합·본서버 배포·SQL 변경·서버 실행은 하지 않았다. 실행 중인 서버 파일 대신 별도 폴더에 빌드했다.

## 1. 기준과 결론

고증 기준은 EP16.2 출시일인 2016-03-09에서, 요청서가 포함하도록 지정한 2016-04-14 본서버 해산물계 조정까지다. 출시일은 [공식 EP16.2 소개](https://ro.gnjoy.com/news/update/View.asp?seq=182), 지원계 조정은 [공식 4월 14일 점검 공지](https://ro.gnjoy.com/news/notice/View.asp?BBSMode=10001&seq=6644)에서 확인했다.

주요 장애는 아이템 자체 부재가 아니라 **라자냐 NPC 파일 전체 미로드**, 재료 분기·소비 누락, 당근 없는 당근난타의 범위 타격 누락, 장비 옵션 단위 오류였다. 기존 파일·DB·공통 회복 계산·파티 순회를 재사용해 수리했다. 따라서 부적을 제니 상점으로 새로 공급하거나 임의 교환 수량을 만들지 않았다.

**사용자가 작업 중 선택한 예외:** 기존 심령어택·냥그라스·그루밍 등 후대 스킬 13종은 유지하고 고증 차이로 보고한다. 해당 트리·스킬을 삭제하거나 스킬 포인트를 강제 초기화하지 않았다. 따라서 결과를 “2016년 4월과 완전히 동일한 스킬 구성”이라고 주장하지 않는다. 혼령사 전직은 기존 jobmaster에서 비활성화되어 있다.

첨부 문서는 조사 체크리스트와 근거 후보로 사용했다. 모든 주장·예시를 검증된 공식 수치로 취급하지 않았다. 특히 첨부한 `seq=6602`의 2015-12-23 공지에서는 동물계 조정 문구를 찾지 못했다. 그 공지를 근거로 물리 배율·확률을 임의 재설정하지 않았다.

## 2. 기존 구현 상태

### 실행 구성·생성·지역

| 항목 | 조사 당시 상태 | 처리 / 남은 확인 |
|---|---|---|
| Renewal / NEED 공식 | `src/config/renewal.hpp`: Renewal, NEED_2017_SKILL_FORMULA / BEHAVIOR 사용 | 기존 설정 유지. 도람 전체가 2016 수치로 자동 고정되는 매크로는 아니므로 개별 확인 |
| 종족·직업 | `JOB_SUMMONER` / 스크립트 `Job_Summoner`, ID 4218. char 생성 시 도람 분기 존재 | 정상 정의. 실제 KR/EN 생성 UI는 미검증 |
| 최대 레벨 | `db/re/job_exp.yml`: Summoner / Baby_Summoner 175/60 | 요청서의 운영 기준 유지. 이 값 전체의 2016 최초 출시 고증을 별도 확정한 것은 아님 |
| 시작 지점 | `conf/import/char_conf.txt`: NEED의 `ra_temsky` 시작 | 의도적 NEED 동선으로 유지. 라자냐 강제 시작으로 덮어쓰지 않음 |
| 시작 무기 | 1201 나이프가 우수 착용 상태로 지정. 1201·1627은 Summoner 장착 대상 아님 | 1681 단단한 강아지풀 지팡이 1개를 우수 무기로 연결; 나이프는 인벤토리 보존 |
| 수도 이전 | 커밋 `e3daf64c9`: 라자냐에서 `payo2`로 NEED 편의 NPC 이동 | 구 페이욘 수도·통합 인챈트 출입·기존 NPC 배치 유지 |
| 라자냐 접근 | `npc/custom/warper.txt`, `npc/re/warps/cities/lasagna.txt` 로드 경로 존재 | 기존 마을 워퍼, 필드/던전/선박 워프 재사용. 라자냐에서 이즈루드로 가는 기존 배 관련 NPC 복구 |
| 맵 | `conf/maps_athena.conf`, `db/map_index.txt`: lasagna / lasa_fild01·02 / lasa_dun01·02·03·q / lasa_in01 / conch_in | 설정 존재. 이 작업으로 새 맵이나 후대 지역 추가 없음. 클라이언트 GRF 실제 보유 여부 미확인 |
| 몬스터 | lasa_fild / lasa_dun 스폰 파일은 이미 활성화. 3495~3507 계열 및 던전 보스 정의 존재 | 스폰 수·보스 주기·드랍률은 변경하지 않음. 스폰 설정 존재와 실접속 스폰 확인은 구분 |
| 라자냐 NPC | `npc/re/scripts_athena.conf`에서 `lasagna_npcs.txt` 전체 주석 | 핵심 상점·챠라 가족·초반/성장/일일 퀘스트 복구 |
| 클라이언트 자산 | 실제 KR/EN iteminfo/skillinfo/questinfo/아이콘·스프라이트 묶음은 저장소에서 찾지 못함 | 서버 번역 처리와 별도로 TQ 남김 |

### 재료·경제·복사 제한

| 기능/ID | 기존 상태 / 근거 | 실제 처리 |
|---|---|---|
| 515 당근 | DB 기본 구매가 15z. 상점 정의는 있으나 파일 미로드 | 라자냐 166,117의 기존 상인 복구. DB 가격 사용 |
| 567 새우 | DB 기본 구매가 500z. `SU_BUNCHOFSHRIMP` 필수 재료 1개 | 가격·필수 소비 공통 로직 유지, 공급 경로 복구 |
| 11602 개박하 열매 | DB 기본 구매가 15z. 메테오 선택 재료의 ID 조회 위치 오류 | 상점 복구, 실제 선택 재료 1개 소비 및 저주 분기 연결 |
| 6417 개다래 열매 | 말랑도 재화, 별도 아이템 | 새 상점 판매·스킬 재료로 사용하지 않음 |
| 25142 도람 토큰 | DB 정의 존재, 가족 교환 NPC 미노출 | 기존 장비→토큰→부적→상위 부적 구조 복구 |
| 도작 / 리프로듀스 | 5018~5056의 유효 DB에 CopyFlags 허용이 없음. `skill_isCopyable()`은 허용 플래그 필요 | 정상. 관련 소스·NEED 도작 NPC 불필요하게 변경하지 않음 |
| 악세서리 제련 | 목걸이 3종·부적 12종 모두 Refineable 기본 false | 정상, 플래그·거래 제한·슬롯 유지 |

챠라 가족 교환의 존재는 [2015-12-02 공식 본서버 공지](https://ro.gnjoy.com/news/notice/View.asp?BBSMode=10001&curpage=122&seq=6592)로 확인했다. 제련 불가는 [2016-01-22 공식 안내](https://ro.gnjoy.com/news/notice/View.asp?BBSMode=10001&curpage=121&seq=6619)와 일치한다. 판매 품목·가격은 첨부한 당시 기록과 이미 존재하는 서버 정의를 교차 확인했으며 가격을 새로 추정하지 않았다. 할인/오버차지 처리는 기존 공통 상점 로직을 따른다.

### 확인된 누락·오류 사항

1. 정의가 있으나 노출되지 않던 라자냐 재료 상점·챠라 가족 교환·기존 성장 NPC.
2. 도람 장착 불가 시작 무기, 28382 캐스팅 감소 옵션의 단위 오류, Dandi 목록의 28424 누락.
3. 새우 떼 즉시 회복 누락, 새우의 신선함의 2016 기준과 다른 회복식.
4. 메테오·당근난타 선택 재료 조회/소비 오류, 당근 없는 당근난타 범위 피해 누락, 할퀴기 출혈 시간 조회 오류.
5. 퀘스트 14564/14577/14578 정의 누락, Sous Chef의 14564 완료 검색 누락.
6. 원본 교환의 ID 일괄 삭제와 대화 전 상태 의존으로 인한 소실 위험. 정확한 사본 재검사·삭제 및 보상 공간 검사를 보완.

## 3. 실제 수정 내용

| 수정 파일 | 수정 내용 | 보존한 부분 |
|---|---|---|
| `conf/import/char_conf.txt` | `start_items_doram` 앞에 `1681,1,2` 추가, 기존 나이프의 장착 위치만 2→0 | 사용자가 이미 수정한 서버 접속 설정, NEED 시작점, 다른 시작 아이템 전체 |
| `npc/re/scripts_athena.conf` | 교환 보조 함수 파일을 먼저 로드, 기존 라자냐 NPC 파일 활성화 | 기존 라자냐 몬스터·워프 로더 |
| `npc/re/custom/lasagna/lasagna_npcs.txt` | 기존 초반/성장/교환 NPC 활성 경로 수리. KR 원문과 같은 줄의 기존 EN 백업을 대응시켜 4,790개 대화 줄에 `needtr` 적용 | 퀘스트 변수·보상·기존 이야기·NPC 내부 고유명 |
| 같은 NPC 파일 | 새우 상인을 라자냐 166,117에 원형 배치. 연혁 미확정 타 도시 상인 복제와 후대 Extended 도구 상점 비활성 유지 | 515/567/11602 기본 가격, 일반 도구 상점 |
| 같은 NPC 파일 | Dandi 교환 목록의 `28423` 중복을 통통한 지렁이 부적인 `28424`로 수정 | 교환 수량 |
| 같은 NPC 파일 | Sous Chef 순회 시작 ID 14565→14564: 빵 심부름 완료 분기도 검색하도록 수정 | 기존 재료·수량·대기시간·보상 |
| `npc/NEED/need_doram_exchange.txt` | 보상 실물 기준 공간/무게 검사. 대화 후 인벤토리를 다시 조회해 정확한 index로 장비 1개 삭제. 실패 시 토큰 지급/소비 진행 안 함 | 기존 교환 경제와 토큰 |
| `db/re/quest_db.yml` | 기존 스크립트가 실제로 set/erase하는 14564 / 14577 / 14578 정의 복구 | 스크립트에 없는 목표·기간·새 보상을 임의 추가하지 않음 |
| `db/re/item_db_equip.yml` | 28382: `bVariableCastrate` -1000%를 `bSkillVariableCast` -1000ms로 수정 | MDEF +5, 장착·거래·레벨·슬롯 |
| `src/map/skills/summoner/bunchofshrimp.cpp` | 기존 파티 순회 대상마다 기존 `skill_calc_heal`을 호출해 즉시 회복 1회 적용 | ATK/MATK 버프, 파티 범위, 지속시간, 새우 1개 필수 소비, 바다의 혼 지속시간 효과 |
| `src/map/skills/summoner/catnipmeteor.cpp` | ItemCost가 없는 기본 스킬 대신 보조 스킬 5042의 11602 정의 조회. 소비 성공 시에만 강화/저주 버전. 메테오 여러 개마다 중복 소비하지 않음 | 기존 SP·쿨타임·중립 속성 및 기존 보조 버전의 배율·범위 |
| `src/map/skills/summoner/lunaticcarrotbeat.cpp` | 보조 스킬 5043에서 당근 조회·1개 소비. flag 1 범위 재호출을 기본 범위 피해 처리로 연결 | 기존 배율·기절 확률·재시전 제한 |
| `src/map/skills/summoner/scratch.cpp` | 할퀴기 출혈 시간이 등록된 Duration2를 조회하도록 변경 | 기존 피해·출혈 확률 |
| `src/map/status.cpp` | 새우의 신선함: 당시 대상 MHP 4% 주기 회복 복원. INT/MATK 기반 후대 계산과 구분 | 레벨별 10/9/8/7/6초 간격, 기존 지속시간. 사용자 요청에 따라 후대 바다의 혼 보유 시 회복 배수 2배 유지 |
| `doc/need_doram/` | 유효 DB·로더 감사, 실제 메서드 본문을 추출하는 네이티브 스모크 재현 도구 | 운영 DB·서버에 연결하지 않음 |

28382의 1초 감소 기준은 [2015-10-21 공식 변경 공지](https://ro.gnjoy.com/news/notice/View.asp?BBSMode=10001&curpage=124&seq=6578)에 있다. 이는 출시 전 사크라이 조정이라는 점도 구분했다. 이전 테스트 옵션인 메테오 피해 +100%·SP +20으로 되돌리지 않았다.

새우의 신선함은 공식 공지의 4배 변경과 동시대 rAthena [b02115e16, 2016-04-28](https://github.com/rathena/rathena/commit/b02115e16316fa89609733e4af6cfc52c0838d2b)의 1%→4% 구현을 함께 근거로 사용했다. 현재 대상 최대 HP를 매 틱 사용하며 곱셈은 64비트로 처리한다. 바다의 혼 보유 시 8%는 **사용자가 유지하도록 선택한 후대 효과와의 NEED 결합 동작**이며 2016-04-14 원형 수치로 표기하지 않는다.

새우 떼 즉시 회복량은 저장소에 이미 존재하던 `skill_calc_heal(SU_BUNCHOFSHRIMP)` 계산을 연결한 것이다. 공식 점검 공지는 즉시 회복의 추가만 밝히므로, 현행 계산식의 모든 계수를 공식 2016 수치로 확정하지 않는다.

### 교환 수량과 소실 방지 정책

기존 요타: 하급 28413/28416/28419 = 토큰 10개, 특화 28422~28424 = 30개. 기존 고마: 하급→중급 = 30개, 중급→상급 = 90개와 원본 부적 1개. 무기 반납은 단계별 1/2/3/4개, 방어구 반납은 현행 목록의 수량을 유지했다.

**미확정:** 현행 일반 100레벨 슈츠·망토·슈즈 반납은 각 3개지만 첨부 사크라이 기록의 2개와 다르다. 본서버 최종 수량을 확인하지 못했으므로 2개로 바꾸지 않았다. 후술하는 전수 표와 실제 파일이 구현 수량의 기준이다.

교환 안전 제한은 NEED 운영상의 차이다. **미착용·미제련·미파손·식별 완료·카드/랜덤 옵션/등급/귀속/기간/즐겨찾기 없는 장비**만 반납·승급할 수 있다. 이것은 카드·제련·인챈트가 있는 다른 사본을 ID 삭제로 잘못 소모하는 일을 막기 위한 제한이며 당시 공식 교환 조건이라고 주장하지 않는다. 승급 결과는 기존처럼 새 부적이다. 인벤토리의 정확한 사본을 재조회하고 처리하며, 대화/선택/대기 없이 검사→소비→보상을 실행한다. 이는 서버 프로세스 안에서의 연속 처리로, 서버 강제 종료까지 견디는 SQL 트랜잭션 보장이라는 의미는 아니다.

## 4. 전수 감사 매트릭스

표의 “정상”은 **정의와 코드 경로에서 확인**했다는 의미다. 실제 클라이언트 전투·NPC 거래를 통과했다는 의미로 사용하지 않는다. “기존 경로 복구”는 원래 존재하던 정의를 다시 노출한 경우다. 정확한 2016 발동 확률·후대 보정은 확정하지 못한 경우 명시한다.

### 스킬 39종

| ID / 이름 | 계열 | 감사 상태 | 처리 | 실제 코드 경로 |
|---|---|---|---|---|
| 5018 `SU_BASIC_SKILL` | 공용 | 정상 정의 | 기초 기능/생성 경로 유지 | `skill_db / skill_tree / char.cpp` |
| 5019 `SU_BITE` | 공용 | 정상 정의 | 물기 배율·재시전 경로 유지 | `bite.cpp / skill.cpp` |
| 5020 `SU_HIDE` | 공용 | 정상 정의 | 숨기 상태·맵 이동 처리 유지 | `status.cpp / status DB` |
| 5021 `SU_SCRATCH` | 공용 | 동작 오류→수정 | 할퀴기 출혈 Duration2 연결, 확률 유지 | `scratch.cpp` |
| 5022 `SU_STOOP` | 공용 | 정상 정의 | 웅크리기 상태·피해 감소·이동 해제 유지 | `status.cpp / battle.cpp` |
| 5023 `SU_LOPE` | 공용 | 의도적 NEED 제한 유지 | 도약 noteleport 및 기존 BG/GvG 제한 유지 | `lope.cpp / skill_nocast_db` |
| 5024 `SU_SPRITEMABLE` | 공용 | 정상 정의 | 공용 HP/SP 패시브 유지 | `status.cpp` |
| 5025 `SU_POWEROFLAND` | 마법 | 정상 정의 | 대지의 힘 기본/투자 조건 보너스 유지 | `status.cpp` |
| 5026 `SU_SV_STEMSPEAR` | 마법 | 정상 정의 | 줄기창 레벨별 속성·피해·출혈/재시전 유지 | `silvervinestemspear.cpp / skill.cpp` |
| 5027 `SU_CN_POWDERING` | 마법 | 정상 정의 | 개박하 가루뿌리기 11602 필수 1개·기존 장판/상태 유지 | `catnippowdering.cpp / skill_db` |
| 5028 `SU_CN_METEOR` | 마법 | 동작 오류→수정 | 선택 열매 조회/소비·보조 메테오 분기 수리. 기본 무속성·쿨타임 유지 | `catnipmeteor.cpp` |
| 5029 `SU_SV_ROOTTWIST` | 마법 | 정상 정의 / 고증 일부 미확정 | 뿌리 휘감기·대상 상태·타이머 유지 | `silvervineroottwist.cpp / skill.cpp` |
| 5030 `SU_SV_ROOTTWIST_ATK` | 마법 내부 | 정상 정의 / 고증 일부 미확정 | 뿌리 지속 공격 내부 ID, 별도 학습 추가 없음 | `skill.cpp / battle.cpp` |
| 5031 `SU_POWEROFLIFE` | 물리 | 정상 정의 | 생명의 힘·20포인트 조건 유지 | `status.cpp / battle.cpp / pc.cpp` |
| 5032 `SU_SCAROFTAROU` | 물리 | 확률 고증 확인 불가 | 타로우 상처·기절·상처 확률 현행 유지 | `scaroftarou.cpp / status.cpp` |
| 5033 `SU_PICKYPECK` | 물리 | 정상 정의 | 픽키 저체력 대상 증폭·재시전 경로 유지 | `pickypeck.cpp / skill.cpp` |
| 5034 `SU_PICKYPECK_DOUBLE_ATK` | 물리 내부 | 내부 예약 정의 | 별도 학습 추가 없음. 실제 재시전은 skill.cpp의 원 스킬 재호출 경로 | `pickypeck.cpp / skill.cpp` |
| 5035 `SU_ARCLOUSEDASH` | 물리 | 정상 정의 | 아크라우스 AGI·속도·도람 원거리 보너스 유지 | `status.cpp / skill_db` |
| 5036 `SU_LUNATICCARROTBEAT` | 물리 | 동작 오류→수정 | 당근 미보유 범위 타격 재호출 수리, 선택 당근 분기 수리 | `lunaticcarrotbeat.cpp` |
| 5037 `SU_POWEROFSEA` | 지원 | 정상 정의 | 바다의 힘 HP/SP·힐량 기본+추가 합산 유지 | `status.cpp / skill.cpp / pc.cpp` |
| 5038 `SU_TUNABELLY` | 지원 | 정상 정의 / 고증 수치 보류 | 참치 대뱃살 현행 회복·쿨타임 유지; 공식 당시 정확 쿨타임 미확정 | `tunabelly.cpp / skill_db` |
| 5039 `SU_TUNAPARTY` | 지원 | 정상 정의 / 고증 일부 미확정 | 참치 파티 보호막·목걸이/부적 쿨감 경로 유지 | `tunaparty.cpp / status.cpp / battle.cpp` |
| 5040 `SU_BUNCHOFSHRIMP` | 지원 | 누락 동작→수정 | 새우 떼 파티원별 즉시 회복 연결, 새우 1개 필수 유지 | `bunchofshrimp.cpp / skill_db` |
| 5041 `SU_FRESHSHRIMP` | 지원 | 고증 차이→수정 | 새우의 신선함 기본 MHP 4%/틱 복원. 후대 바다의 혼 배수는 사용자 선택 유지 | `status.cpp` |
| 5042 `SU_CN_METEOR2` | 마법 내부 | 선택 재료 경로 수리 | 11602 1개 정의 재사용, 저주 부여. 기존 INT/레벨 보정 등은 유지·고증 미확정 | `catnipmeteor.cpp / skill_db` |
| 5043 `SU_LUNATICCARROTBEAT2` | 물리 내부 | 선택 재료 경로 수리 | 515 1개 정의 재사용, 기절 부여. 기존 STR/레벨 보정 등은 유지·고증 미확정 | `lunaticcarrotbeat.cpp / skill_db` |
| 5044 `SU_SOULATTACK` | 후대 / 기존 NEED | 기준 밖·사용자 선택 유지 | 학습/동작/패시브 기존 유지. 새로 추가한 스킬 아님 | `skill_tree / skills/summoner / status.cpp` |
| 5045 `SU_POWEROFFLOCK` | 후대 / 기존 NEED | 기준 밖·사용자 선택 유지 | 학습/동작/패시브 기존 유지. 새로 추가한 스킬 아님 | `skill_tree / skills/summoner / status.cpp` |
| 5046 `SU_SVG_SPIRIT` | 후대 / 기존 NEED | 기준 밖·사용자 선택 유지 | 학습/동작/패시브 기존 유지. 새로 추가한 스킬 아님 | `skill_tree / skills/summoner / status.cpp` |
| 5047 `SU_HISS` | 후대 / 기존 NEED | 기준 밖·사용자 선택 유지 | 학습/동작/패시브 기존 유지. 새로 추가한 스킬 아님 | `skill_tree / skills/summoner / status.cpp` |
| 5048 `SU_NYANGGRASS` | 후대 / 기존 NEED | 기준 밖·사용자 선택 유지 | 학습/동작/패시브 기존 유지. 새로 추가한 스킬 아님 | `skill_tree / skills/summoner / status.cpp` |
| 5049 `SU_GROOMING` | 후대 / 기존 NEED | 기준 밖·사용자 선택 유지 | 학습/동작/패시브 기존 유지. 새로 추가한 스킬 아님 | `skill_tree / skills/summoner / status.cpp` |
| 5050 `SU_PURRING` | 후대 / 기존 NEED | 기준 밖·사용자 선택 유지 | 학습/동작/패시브 기존 유지. 새로 추가한 스킬 아님 | `skill_tree / skills/summoner / status.cpp` |
| 5051 `SU_SHRIMPARTY` | 후대 / 기존 NEED | 기준 밖·사용자 선택 유지 | 학습/동작/패시브 기존 유지. 새로 추가한 스킬 아님 | `skill_tree / skills/summoner / status.cpp` |
| 5052 `SU_SPIRITOFLIFE` | 후대 / 기존 NEED | 기준 밖·사용자 선택 유지 | 학습/동작/패시브 기존 유지. 새로 추가한 스킬 아님 | `skill_tree / skills/summoner / status.cpp` |
| 5053 `SU_MEOWMEOW` | 후대 / 기존 NEED | 기준 밖·사용자 선택 유지 | 학습/동작/패시브 기존 유지. 새로 추가한 스킬 아님 | `skill_tree / skills/summoner / status.cpp` |
| 5054 `SU_SPIRITOFLAND` | 후대 / 기존 NEED | 기준 밖·사용자 선택 유지 | 학습/동작/패시브 기존 유지. 새로 추가한 스킬 아님 | `skill_tree / skills/summoner / status.cpp` |
| 5055 `SU_CHATTERING` | 후대 / 기존 NEED | 기준 밖·사용자 선택 유지 | 학습/동작/패시브 기존 유지. 새로 추가한 스킬 아님 | `skill_tree / skills/summoner / status.cpp` |
| 5056 `SU_SPIRITOFSEA` | 후대 / 기존 NEED | 기준 밖·사용자 선택 유지 | 학습/동작/패시브 기존 유지. 새로 추가한 스킬 아님 | `skill_tree / skills/summoner / status.cpp` |

바다의 힘은 `status.cpp`의 HP/SP 계산과 `skill.cpp`의 힐 계산에서 기본 +1000 HP / +100 SP / +10% 회복, 조건 충족 시 추가 +3000 / +300 / +20%가 이미 합산된다. 따라서 바다의 힘 자체의 합계는 +4000 HP / +400 SP / +30%이며, 별도 공용 패시브 효과를 이 수치에 섞지 않았다. `pc_checkskill_summoner()`의 현행 후대 계열 투자 포인트도 사용자 선택에 따라 유지했다.

기본 메테오와 과일 메테오는 DB에서 속성 미지정 시 기본 Neutral을 사용한다. 후대 혼령사 속성 버프 처리도 소스에는 남아 있으나 혼령사 전직을 이 작업으로 열지 않았다. 메테오 재료 유무로 쿨타임을 없애는 추정 변경은 하지 않았다. 당근·열매는 선택 재료이고, 새우 떼의 새우는 필수다.

### 장비·악세서리 43종

드랍 표기는 기본/수입 DB에서 확인한 **정의상 경로**다. 기재한 모든 몬스터의 실제 스폰·접근 가능성·드랍률 배율을 접속 실측한 것은 아니다. 확률 단위는 DB 원값 10000 = 100%다. 후대 일루전 몬스터 등 추가 DB 경로가 섞여 있는 사실과, 원래 라자냐/기존 몬스터 경로를 구분해야 한다. 드랍 정의를 새로 복사하거나 증량하지 않았다.

| ID / 이름 | 레벨 / 슬롯 / 제련 | 기존 상태→처리 | 획득 경로(DB 정의) |
|---|---|---|---|
| 1681 새싹 강아지풀 지팡이 (`Newbie_Foxtail`) | 1 / 0 / 가능 | 생성 착용 오류→기본 강아지풀 연결 | 신규 도람 생성 시 1개 |
| 1683 풍성한 강아지풀 지팡이 (`Abundantly_Foxtail`) | 12 / 1 / 가능 | 기존 정의/드랍 유지 | DR_RODA_FROG(3499, 100/10000) |
| 1684 길다란 강아지풀 지팡이 (`Long_Foxtail`) | 12 / 1 / 가능 | 기존 정의/드랍 유지 | DR_RODA_FROG(3499, 100/10000) |
| 1685 잠자리 앉은 강아지풀 지팡이 (`DragonFry_Foxtail`) | 20 / 1 / 가능 | 기존 정의/드랍 유지 | DR_BASILISK1(3502, 100/10000) |
| 1686 커다란 강아지풀 지팡이 (`BigSize_Foxtail`) | 20 / 1 / 가능 | 기존 정의/드랍 유지 | DR_BASILISK1(3502, 100/10000) |
| 1687 초보자용 강아지풀 지팡이 (`Beginner_FoxTail`) | 3 / 1 / 가능 | 기존 퀘스트 경로 미노출→복구 | 기존 라자냐/자경단 퀘스트 보상 |
| 1690 신기한 강아지풀 지팡이 (`Amazing_Foxtail`) | 60 / 3 / 가능 | 기존 정의/드랍 유지 | METALLER(1058, 15/10000), FRILLDORA(1119, 20/10000), GOBLIN_5(1126, 20/10000), C1_METALLER(2754, 25/10000) |
| 1691 신묘한 강아지풀 지팡이 (`Strange_Foxtail`) | 100 / 2 / 가능 | 기존 정의/드랍 유지 | DEVIRUCHI(1109, 20/10000), DELETER(1384, 20/10000), BLOOD_BUTTERFLY(1408, 20/10000), BREEZE(1692, 20/10000), SIROMA(1776, 20/10000), C1_SIROMA(2661, 20/10000), C2_DEVIRUCHI(2854, 100/10000), C1_BLOOD_BUTTERFLY(2883, 30/10000) |
| 1692 마력의 강아지풀 지팡이 (`Magical_Foxtail`) | 140 / 2 / 가능 | 기존 정의/드랍 유지 | ANTLER_SCARABA(2085, 1/10000), DR_BASILISK2(3503, 1/10000) |
| 1693 마력의 노랑 강아지풀 지팡이 (`Magical_Y_Foxtail`) | 175 / 1 / 가능 | 기존 정의/드랍 유지 | DR_POM_SPIDER(3507, 1/10000) |
| 1694 강아지풀 모형 (`Model_Foxtail`) | 60 / 3 / 가능 | 기존 정의/드랍 유지 | METALLER(1058, 15/10000), FRILLDORA(1119, 20/10000), GOBLIN_5(1126, 20/10000), C1_METALLER(2754, 25/10000) |
| 1695 섬세한 강아지풀 모형 (`Detail_Model_Foxtail`) | 100 / 2 / 가능 | 기존 정의/드랍 유지 | DELETER_(1385, 20/10000), BLOOD_BUTTERFLY(1408, 20/10000), BREEZE(1692, 20/10000), SIROMA(1776, 20/10000), ROWEEN(1782, 20/10000), C1_SIROMA(2661, 20/10000), C2_ROWEEN(2677, 100/10000), C3_BREEZE(2880, 100/10000), C4_BREEZE(2881, 100/10000), C1_BLOOD_BUTTERFLY(2883, 30/10000) |
| 1696 정교한 강아지풀 모형 (`Ex_Model_Foxtail`) | 140 / 2 / 가능 | 기존 정의/드랍 유지 | ANTLER_SCARABA(2085, 1/10000), DR_BASILISK2(3503, 1/10000) |
| 1697 정교한 노랑 강아지풀 모형 (`Ex_Y_Model_Foxtail`) | 175 / 1 / 가능 | 기존 정의/드랍 유지 | DR_BASILISK3(3504, 1/10000) |
| 15126 도람 전용 슈츠 (`Doram_Only_Suit`) | 100 / 1 / 가능 | 기존 정의/드랍 유지 | ANACONDAQ(1030, 20/10000), KOBOLD_3(1135, 20/10000), HEATER(1318, 20/10000), C4_ANACONDAQ(2904, 100/10000) |
| 15129 도람 고급 슈츠 (`Doram_High_Suit`) | 140 / 1 / 가능 | 기존 정의/드랍 유지 | HORN_SCARABA(2083, 20/10000), C1_HORN_SCARABA(2799, 30/10000), ILL_SOLIDER(3802, 20/10000) |
| 15150 하얀 셔츠 (`White_Shirt`) | 6 / 1 / 가능 | 기존 퀘스트 경로 미노출→복구 | DR_FABRE(3497, 100/10000); 기존 라자냐/자경단 퀘스트 보상 |
| 15151 하얀 친환경 셔츠 (`White_Eco_Shirt`) | 15 / 1 / 가능 | 기존 정의/드랍 유지 | DR_SPORE(3501, 100/10000) |
| 15156 도람 엘레강스 슈츠 (`Doram_Ele_Suit`) | 175 / 1 / 가능 | 기존 정의/드랍 유지 | DR_POM_SPIDER(3507, 10/10000) |
| 19084 파르펠레 자경단 모자 (`Parfait_V_Hat`) | 20 / 1 / 가능 | 기존 퀘스트 경로 미노출→복구 | 기존 라자냐/자경단 퀘스트 보상 |
| 20788 도람 전용 망토 (`Doram_Only_Cape`) | 100 / 1 / 가능 | 기존 정의/드랍 유지 | KOBOLD_3(1135, 20/10000), ASSULTER(1315, 20/10000), DELETER(1384, 20/10000), C3_ASSULTER(2889, 100/10000) |
| 20789 도람 고급 망토 (`Doram_High_Cape`) | 140 / 1 / 가능 | 기존 정의/드랍 유지 | RAKE_SCARABA(2086, 20/10000), C4_RAKE_SCARABA(2693, 30/10000), ILL_ASSULTER(3799, 20/10000) |
| 20790 도람 엘레강스 망토 (`Doram_Ele_Cape`) | 175 / 1 / 가능 | 기존 정의/드랍 유지 | DR_MANDRAGORA(3506, 1/10000) |
| 22076 원목 슬리퍼 (`Hardwood_Slipper`) | 9 / 1 / 가능 | 기존 정의/드랍 유지 | DR_HORNET(3498, 100/10000) |
| 22083 도람 전용 슈즈 (`Doram_Only_Shoes`) | 100 / 1 / 가능 | 기존 정의/드랍 유지 | KOBOLD_2(1134, 20/10000), SOLIDER(1316, 20/10000), DELETER_(1385, 20/10000), C2_SOLIDER(2647, 100/10000) |
| 22084 도람 고급 슈즈 (`Doram_High_Shoes`) | 140 / 1 / 가능 | 기존 정의/드랍 유지 | HORN_SCARABA2(2084, 20/10000), ILL_FREEZER(3801, 20/10000) |
| 22085 도람 엘레강스 슈즈 (`Doram_Ele_Shoes`) | 175 / 1 / 가능 | 기존 정의/드랍 유지 | DR_BASILISK3(3504, 10/10000) |
| 28374 강아지풀 반지 (`Foxtail_Ring`) | 1 / 0 / 불가 | 기존 퀘스트 경로 미노출→복구 | 기존 라자냐/자경단 퀘스트 보상 |
| 28380 풋풋한 풀 목걸이 (`Fresh_G_Necklace`) | 100 / 1 / 불가 | 기존 정의/드랍 유지 | KOBOLD_2(1134, 20/10000), STING(1207, 20/10000), C5_STING(2640, 100/10000) |
| 28381 귀여운 풀 목걸이 (`Pretty_G_Necklace`) | 100 / 1 / 불가 | 기존 정의/드랍 유지 | STAPO(1784, 20/10000), C3_STAPO(2643, 100/10000) |
| 28382 부적 풀 목걸이 (`Charm_G_Necklace`) | 100 / 1 / 불가 | 드랍 존재·옵션 단위 오류→수정 | ROWEEN(1782, 20/10000), C2_ROWEEN(2677, 100/10000) |
| 28413 하급 전갱이 부적 (`LowSaurelCharm`) | 100 / 1 / 불가 | 정의만 존재·NPC 미노출→기존 교환 복구 | 요타: 토큰 10개 |
| 28414 중급 전갱이 부적 (`MiddleSaurelCharm`) | 140 / 1 / 불가 | 정의만 존재·NPC 미노출→기존 교환 복구 | 고마: 하급 1개 + 토큰 30개 |
| 28415 상급 전갱이 부적 (`HigherSaurelCharm`) | 175 / 1 / 불가 | 정의만 존재·NPC 미노출→기존 교환 복구 | 고마: 중급 1개 + 토큰 90개 |
| 28416 하급 잎부적 (`LowLeafCharm`) | 100 / 1 / 불가 | 정의만 존재·NPC 미노출→기존 교환 복구 | 요타: 토큰 10개 |
| 28417 중급 잎부적 (`MiddleLeafCharm`) | 140 / 1 / 불가 | 정의만 존재·NPC 미노출→기존 교환 복구 | 고마: 하급 1개 + 토큰 30개 |
| 28418 상급 잎부적 (`HigherLeafCharm`) | 175 / 1 / 불가 | 정의만 존재·NPC 미노출→기존 교환 복구 | 고마: 중급 1개 + 토큰 90개 |
| 28419 하급 토끼부적 (`LowRabbitCharm`) | 100 / 1 / 불가 | 정의만 존재·NPC 미노출→기존 교환 복구 | 요타: 토큰 10개 |
| 28420 중급 토끼부적 (`MiddleRabbitCharm`) | 140 / 1 / 불가 | 정의만 존재·NPC 미노출→기존 교환 복구 | 고마: 하급 1개 + 토큰 30개 |
| 28421 상급 토끼부적 (`HigherRabbitCharm`) | 175 / 1 / 불가 | 정의만 존재·NPC 미노출→기존 교환 복구 | 고마: 중급 1개 + 토큰 90개 |
| 28422 빛나는 가지 부적 (`ShiningTwigCharm`) | 100 / 1 / 불가 | 정의만 존재·NPC 미노출→기존 교환 복구 | 요타: 토큰 30개 |
| 28423 신선한 다랑어 부적 (`FreshTunaCharm`) | 100 / 1 / 불가 | 정의만 존재·NPC 미노출→기존 교환 복구 | 요타: 토큰 30개 |
| 28424 통통한 지렁이 부적 (`PlumpyWormCharm`) | 100 / 1 / 불가 | 정의만 존재·NPC 미노출→기존 교환 복구 | 요타: 토큰 30개 |

### 세트 12개

2015년에 장비 콤비가 추가된 사실은 [2015-11-18 공식 공지](https://ro.gnjoy.com/news/notice/View.asp?BBSMode=10001&curpage=122&seq=6589)에서 확인했다. 아래 수치는 **현행 DB 확인값**이며 공지가 개별 수치를 모두 증명하는 것은 아니다. 단품과 세트 스크립트가 분리되어 있어 단품 효과를 다시 추가하지 않았다.

| 세트 / 레벨 | 실제 조합 | 현행 세트 스크립트(단품 제외) | 감사 상태 |
|---|---|---|---|
| 100 / 기본 방어구 | 15126 + 20788 + 22083 | `bonus bMaxHP,2000; bonus bNoCastCancel; bonus bFixedCastrate,-15-(getequiprefinerycnt(EQI_ARMOR)+getequiprefinerycnt(EQI_GARMENT)+getequiprefinerycnt(EQI_SHOES));` | 정상 연결, 수치 유지 |
| 100 / 잎 | 15126 + 20788 + 22083 + 28382 + 28416 | `bonus bMaxHPrate,10; bonus bMaxSPrate,10; bonus2 bSkillAtk,"SU_CN_METEOR",100;` | 정상 연결, 수치 유지 |
| 100 / 토끼 | 15126 + 20788 + 22083 + 28380 + 28419 | `bonus bMaxHPrate,20; bonus bMaxSPrate,3; bonus2 bSkillAtk,"SU_LUNATICCARROTBEAT",100;` | 정상 연결, 수치 유지 |
| 100 / 전갱이 | 15126 + 20788 + 22083 + 28381 + 28413 | `bonus bMaxHPrate,15; bonus bMaxSPrate,5; bonus bHealPower,10; skill "AL_HEAL",5,1;` | 정상 연결, 수치 유지 |
| 140 / 기본 방어구 | 15129 + 20789 + 22084 | `bonus bMaxHP,3000; bonus bNoCastCancel; bonus bFixedCastrate,-20-(getequiprefinerycnt(EQI_ARMOR)+getequiprefinerycnt(EQI_GARMENT)+getequiprefinerycnt(EQI_SHOES));` | 정상 연결, 수치 유지 |
| 140 / 잎 | 15129 + 20789 + 22084 + 28382 + 28417 | `bonus bMaxHPrate,15; bonus bMaxSPrate,15; bonus2 bSkillAtk,"SU_CN_METEOR",150;` | 정상 연결, 수치 유지 |
| 140 / 토끼 | 15129 + 20789 + 22084 + 28380 + 28420 | `bonus bMaxHPrate,25; bonus bMaxSPrate,6; bonus2 bSkillAtk,"SU_LUNATICCARROTBEAT",150;` | 정상 연결, 수치 유지 |
| 140 / 전갱이 | 15129 + 20789 + 22084 + 28381 + 28414 | `bonus bMaxHPrate,20; bonus bMaxSPrate,10; bonus bHealPower,20; skill "AL_HEAL",7,1;` | 정상 연결, 수치 유지 |
| 175 / 기본 방어구 | 15156 + 20790 + 22085 | `bonus bMaxHP,4000; bonus bNoCastCancel; bonus bFixedCastrate,-25-(getequiprefinerycnt(EQI_ARMOR)+getequiprefinerycnt(EQI_GARMENT)+getequiprefinerycnt(EQI_SHOES));` | 정상 연결, 수치 유지 |
| 175 / 잎 | 15156 + 20790 + 22085 + 28382 + 28418 | `bonus bMaxHPrate,20; bonus bMaxSPrate,20; bonus2 bSkillAtk,"SU_CN_METEOR",200;` | 정상 연결, 수치 유지 |
| 175 / 토끼 | 15156 + 20790 + 22085 + 28380 + 28421 | `bonus bMaxHPrate,30; bonus bMaxSPrate,9; bonus2 bSkillAtk,"SU_LUNATICCARROTBEAT",200;` | 정상 연결, 수치 유지 |
| 175 / 전갱이 | 15156 + 20790 + 22085 + 28381 + 28415 | `bonus bMaxHPrate,25; bonus bMaxSPrate,15; bonus bHealPower,30; skill "AL_HEAL",9,1;` | 정상 연결, 수치 유지 |

28413~28421: 레벨 100/140/175, 슬롯 1, 계열별 VIT/INT/DEX +1/+2/+3. 해당 단계의 슈츠·망토·슈즈와 **전갱이=귀여운 풀 / 잎=부적 풀 / 토끼=풋풋한 풀** 조합이다. 단품에 세트 피해/회복 보너스를 복제하지 않았다. 전용 부적은 기존 거래·버리기·카트·길드창고·우편·경매 제한을 유지하고, 개인 창고 금지는 새로 만들지 않았다.

특화 28422/28424: 기존 변동 캐스팅 -50%, INT 또는 DEX 기반 스킬 피해 계산 유지. 28423: 참치파티 쿨타임 -5초 및 피격 오토스펠 정의 유지. 확률 인자 10과 1693/1697 무기의 인자 100은 현재 스크립트 값이며, 이를 근거 없이 역사적 확률로 다시 설정하지 않았다. `_BR` / `_J` 및 Cat_Can_BR 연계는 후대/지역 전용 자료로 별도 취급하고 새 공급 경로로 사용하지 않았다.

### 성장·퀘스트·NPC

- 복구 파일에는 스크립트 NPC 정의 124개, 참조 맵 15개가 있다. 라자냐·파르펠레·선박·필드·던전·기존 110~140레벨 성장 구간을 재사용한다. 각 세부 퀘스트의 공식 본서버 최초 날짜를 모두 독립 확정한 것은 아니다.
- 직접 참조하는 퀘스트 ID 87개를 유효 quest DB와 대조했다. 누락 14564 / 14577 / 14578을 기존 스크립트 주석의 제목으로 정의했고 새 목표/기간/보상은 만들지 않았다.
- 초반 7711~7723, 자경단/라자냐 14531~14592 구간 등의 기존 변수 `lasagna_quest[0..2]`, 기존 일일 대기·보상 플래그 유지. 숫자 범위 안의 모든 ID가 실제 사용된다는 뜻은 아니다.
- 초보 강아지풀 1687, 흰 셔츠 15150, 자경단 모자 19084, 강아지풀 반지 28374 등 기존 퀘스트 보상 경로를 복구했다.
- 새 상점/토큰 무한 지급·수령 초기화·운영 편의 무료 장비 자판기는 추가하지 않았다. 장기 재접속/반복 보상의 중복 수령은 테스트 서버에서 확인할 항목이다.

## 5. 추가 확인이 필요한 사항 / 구현 보류

| 구분 | 남은 확인 | 이번 처리 |
|---|---|---|
| 공식 연혁 | 첨부 2015-12-23 동물계 조정과 실제 본서버 반영 날짜. 해당 첨부 공지 본문에서 조정 문구 미발견 | 배율·기절·상처 확률을 추정 수정하지 않음 |
| 후대 스킬 13개 | 정확한 본서버 도입일 원문. 저장소 `5f7a143d3`의 2017 구현 및 후대 구조와 분리 | 사용자 답변대로 유지, 순수 2016 고증 예외 |
| 참치 대뱃살 | 4월 14일 공식 공지는 쿨타임 감소만 명시. 현행 2/5/8/11/14초의 당시 최종값 여부 | 기존 동작 유지. 구형 에뮬레이터 수치를 공식 수치로 단정해 덮어쓰지 않음 |
| 선택 재료 공격 | 메테오2의 INT/레벨 보정, 당근난타2의 STR/레벨 보정 및 보조 버전 DB 범위·시간의 정확한 도입일 | 기존 수치 유지. 재료 소비·분기 오류만 수정 |
| 교환 수량 | 100레벨 방어구 반납 3개 vs 첨부 사크라이 2개, 승급 30/90의 본서버 최종 확인 | 현행 코드 수량 유지 |
| 자동 발동 | 특화 부적·175 무기 오토스펠의 당시 정확 확률/발동 조건 | 현행 값 유지, 새 확률 발명 없음 |
| 무기 수치 | 2015-11-18 공지의 140/175 무기 변경은 사크라이 구분. 본서버 최종 반영일의 추가 근거 | 현행 장비 스탯·슬롯 유지 |
| 퀘스트 편성 | 원본 스크립트는 iRO 플레이 기록 기반 2017 공개 구현. 모든 세부 내용이 kRO 2016과 같다고 단정 불가 | 기존 콘텐츠 동선 복원, 신규 이야기/보상 구현 없음 |
| 실접속 검사 | 실제 상점 대량구매, 파티/맵이동/사망 상태 유지, 창고·재접속·교환 실패/중복, 실제 BG/PvP | 오프라인 검증과 구분하여 대기 |
| 클라이언트 | 아래 KR/EN TQ 전체 | 서버만으로 완료했다고 표기하지 않음 |

## 6. 빌드와 핵심 스모크

**최종 결과: map-server Debug / Win32 빌드·링크 성공(종료코드 0), 소스 본문 기반 네이티브 14개 시나리오 성공, 유효 YAML·로더·참조 감사 성공.** 새로 발견한 출혈 시간 연결까지 포함한 최종 빌드 결과다. 긴 부하 테스트·대규모 반복 검증은 하지 않았다.

| 검사 | 결과 / 한계 |
|---|---|
| MSBuild | `src/map/map-server.vcxproj`, Debug / Win32, 별도 OutDir. 기존 C4819 코드페이지 경고와 common.pdb 부재 경고가 있었으나 오류 없음 |
| 마법 5개 | 열매 없음 / 있음 / 삭제 실패 / 6417만 있음 / PC 아닌 시전자. 다중 메테오에서 선택 재료 1개만 소비 |
| 물리 5개 | 당근 없음 / 있음 / 삭제 실패 / 범위 재호출 / 할퀴기 Duration2. 일반·강화 경로에서 범위 피해 전달, 피해자마다 추가 재료 소비 없음 |
| 지원 3개 | 혼자 / 3인 파티 / PC 아닌 시전자. 파티원마다 버프·즉시 회복 한 번씩, 루트에서 중복 적용 없음 |
| 주기 회복 1개 | 대상 MHP 4%, 다른 MHP/나눗셈 경계/큰 값, 유지한 바다의 혼 배수 확인 |
| 유효 DB·로더 | 43개 장비, 39개 스킬, 15개 악세 제련불가, 87개 퀘스트 ID, 재료·가격·헬퍼 선로드·단일 로드·맵 설정 |
| NPC 검사 | 괄호/중괄호/문자열 밖 구문 균형, 신규 NPC 고유명과 기존 활성 정의 충돌 없음. **실제 rAthena script parser 실행을 대신하는 검사가 아님** |

네이티브 스모크는 변경된 실제 메서드 본문을 읽어 컴파일하되, 월드·파티·인벤토리·패킷 API는 테스트 대역이다. 서버 전체 전투, 실제 재료 차감 DB 저장, 클라이언트 패킷을 검증한 것으로 해석하면 안 된다. 가격·카드·거래·제련 등은 유효 데이터의 정적 확인이다.

재현 도구:

```powershell
Set-Location F:\rathena
python doc/need_doram/audit.py
python doc/need_doram/generate_smoke.py
cmd /d /c doc\need_doram\build_smoke.cmd
```

`audit.py`에는 PyYAML이 필요하다. 검사 로그·유효 DB 스냅샷·격리 빌드는 `F:\rathena\docs\reports\need-doram-ep162-20261118\`에 있다. 이 폴더는 저장소의 기존 ignore 대상이다. 재현 스크립트와 이 보고서는 ignore 밖에 두었다.

## 7. PvP/BG·경제 영향과 롤백

- 기존 전장 배율, 맵별 스킬 금지, 도약 제한, 무적/준비 구간, 전장 보상·다른 직업 상점·인던 파일을 수정하지 않았다.
- 작업 도중 별도 변경으로 나타난 `src/map/atcommand.cpp`와 `NEED_Berserk_Load_PvE_Only_Implementation_Report.md`는 이 도람 작업의 변경에 포함하지 않았고 손대지 않았다.
- 공통 스킬 수리이므로 BG/PvP에서도 새우 즉시 회복·주기 회복·정상 재료 분기·당근 없는 범위 피해·출혈 시간이 영향을 준다. PvE만 바뀐다고 주장하지 않는다. 장비 옵션 -1000% 오류 수정으로 마법 도람의 실제 캐스팅이 정상화된다.
- 목걸이 드랍과 기존 토큰 경제를 재사용한다. 부적 공급이 실제로 열리므로 유통량은 늘 수 있으나 교환 단가·드랍률을 임의 완화하지 않았다. 깨끗한 장비만 교환하는 제한은 운영상 차이로 안내한다.
- 롤백은 **이 보고서 3절의 변경 hunk만 역적용**하고 기존 빌드로 교체한다. `conf/import/char_conf.txt`에는 작업 전 사용자 수정이 있었으므로 파일 전체 checkout/restore 금지. 도람 시작 무기 추가 hunk만 되돌려야 한다. 기존 사용자 변경이 있던 다른 conf 및 `npc/scripts_need.conf`도 복구 대상으로 삼지 않는다.
- 새 교환 헬퍼를 제거할 때는 원본 NPC의 헬퍼 호출 변경도 함께 되돌린다. 퀘스트 정의만 먼저 지워 진행 중 캐릭터를 끊지 않는다. SQL 스키마 변경/정리 SQL은 없다.
- 검토용 diff는 `git diff -- conf/import/char_conf.txt db/re/item_db_equip.yml db/re/quest_db.yml npc/re/scripts_athena.conf npc/re/custom/lasagna/lasagna_npcs.txt src/map/status.cpp src/map/skills/summoner`와 신규 `npc/NEED/need_doram_exchange.txt`, `doc/need_doram/`, 이 보고서를 함께 확인한다. 신규 파일은 아직 커밋되지 않았으므로 일반 diff에 자동 표시되지 않을 수 있다.

## 8. 운영자 적용·클라이언트 TQ

1. 테스트 서버에 3절 파일을 함께 반영한다. 시작 무기는 char 설정 재읽기/재시작 뒤 생성한 신규 도람부터 적용된다. 기존 캐릭터에 소급 지급하는 새 이벤트는 만들지 않았다.
2. map-server 신규 바이너리·NPC·DB를 함께 반영하고 재시작한다. 라자냐 NPC 파일이 참조하는 헬퍼 파일을 빼먹지 않는다. `reloadscript`만으로 C++·아이템·quest DB 변경까지 적용되지 않는다. SQL import 없음.
3. 물리/마법/지원 캐릭터로 각 1회 PvE, 대표 BG/PvP에서 확인한다. 515/567/11602 구매·소비, 메테오 중립속성/저주/쿨타임, 당근 유무 범위 피해/기절, 새우 떼 파티 회복, 새우 주기 회복, 세 계열 대표 세트·제련불가·도작불가를 포함한다.
4. 토큰 부족·과적·인벤토리 꽉 참·카드/제련/장착 장비 제외·일반 장비 교환·부적 승급을 확인하고 창고/재접속 후 수량·단계·퀘스트 중복을 한 차례 확인한다. 과거 MATK 기반 회복량을 담은 저장 상태가 남았더라도 새 주기 계산은 이전 val2를 회복량으로 재사용하지 않는다. 바다의 혼 유지 효과는 새 시전에 적용된다.

**TQ — 서버 소스와 구분되는 클라이언트 작업**

| 실제 클라이언트에서 찾을 파일군 | 대상 | 적용·검사 |
|---|---|---|
| `System/itemInfo.lub` 또는 `itemInfo.lua`, 구형 `data/idnum2item*table.txt` / `num2item*table.txt` | 515,567,11602,6417,25142,28374,28380~28382,28413~28424 및 4절 장비 전부 | KR/EN 이름·설명·아이콘/리소스·슬롯·장착 제한을 실물 파일에서 확인. 실제 배포본 파일명은 미확인 |
| `data/luafiles514/lua files/skillinfoz/skilldescript.lub`, `skillinfolist.lub`, `skilltreeview.lub` 또는 대응 lua | SU_ 5018~5056 | 28382 설명은 “개박하 메테오 변동 캐스팅 1초 감소” / “Catnip Meteor variable cast time -1 second”. 새우 주기 회복, 선택/필수 재료 설명 및 사용자 선택으로 유지한 후대 스킬 표기 일치 |
| `data/luafiles514/lua files/quest/questinfo.lub` 또는 대응 `questid2display` 계열 | 기존 참조 퀘스트, 특히 14564/14577/14578 | 일지 제목/목표/완료 전환 확인. 이름은 각각 기존 스크립트의 빵 심부름 / 도시락 배달 / 배달 완료 보고에 대응 |
| KR/EN GRF 맵·아이콘·스프라이트 및 클라이언트 EXE/패킷 설정 | Doram 생성·외형, lasagna/lasa_*/conch_in, 해당 아이템과 NPC | 생성 UI→시작 무기→마을 이동→상점→스킬→아이템 설명 순서로 각 언어 한 번 확인 |

현재 서버 DB의 핵심 영문 이름: Carrot / Prawn(Shrimp) / Nepeta Cataria(Catnip Fruit), Doram Token, Fresh Grass Necklace(28380) / Cute Grass Necklace(28381) / Charming Grass Necklace(28382). 6417 Silvervine과 11602 Catnip Fruit을 구분한다. 지원 목걸이는 28381, 마법 목걸이는 28382다. 기존 클라이언트 파일이 확보되기 전 임의 아이콘 파일이나 확인 안 된 GRF 경로를 만들지 않았다.

## 9. 유저 공지·건의 답변 초안

**한국어:** 도람 전용 재료 상점과 라자냐 성장·장비 교환 동선을 점검해 누락된 연결을 보완했습니다. 개박하 열매·당근 소비, 당근난타 범위 타격, 새우 회복, 부적 풀 목걸이 캐스팅 옵션 오류를 수정했습니다. 기존 후대 스킬은 서버 정책에 따라 유지합니다. 현재 테스트 서버 적용·실접속 검증 대기이며 본서버 적용 완료 공지가 아닙니다.

**English:** Restored the existing Lasagna supply, quest and Doram Token exchange routes. Fixed optional Catnip Fruit/Carrot consumption, Carrot Beat splash dispatch, shrimp healing, Scratch's bleeding duration and Charming Grass Necklace's cast-time modifier. Existing later skills remain available under NEED policy. The changes are awaiting test-server deployment and in-game verification; they are not yet live.

**건의 답변:** 전용 악세서리·장비·드랍 정의는 상당 부분 이미 존재했지만, 라자냐 NPC가 비활성화되어 상점·교환·성장 동선이 끊겨 있었습니다. 기존 구조를 복구하고 실제 코드 오류를 함께 수정했습니다. 공식 수치가 아직 확정되지 않은 교환량·발동 확률은 추정 변경하지 않았으며, 테스트 서버 확인 후 적용 내용을 안내하겠습니다.

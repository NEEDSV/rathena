# NEED 황혼의 약제조합 III 구현 보고서

- 작성일: 2026-11-20 (KST)
- 대상: `AM_TWILIGHT3`, 스킬 ID 498
- 결과: **서버 수정·Debug/Win32 빌드 및 링크 성공, 네이티브 스모크 24 PASS / 0 FAIL, KR/EN 설명 수정·Lua 5.1 검증·GPF 제작 완료.**
- 실제 서버 접속·인게임 제작·KR/EN 툴팁 표시·운영 배포는 미실시다. 아래 PASS는 로컬 소스/DB/테스트 대역 검증 결과다.

## 1. 변경 파일과 산출물

| 구분 | 경로 및 내용 |
| --- | --- |
| 서버 | `src/map/skills/merchant/twilightalchemy3.cpp`: 알코올 검사/제조 호출 삭제, 나머지 두 제조법의 DB 재료 합산 검사 |
| KR 원본 | `F:/rathena/generated/twilight3/client-kr/data/luafiles514/lua files/skillinfoz/skilldescript.lub` |
| KR 작업 기준 | 리소스 저장소 `main`의 `43124b4a`에서 별도 worktree/브랜치 `codex/twilight3-client-kr` 생성. 설명 파일의 마지막 원본 커밋은 `df2569cc` |
| EN 원본 | `F:/need/need_client/NEEDResource/data/luafiles514/lua files/skillinfoz/skilldescript.lub` (`client-en`, 작업 전 HEAD `2d912411`) |
| KR 배포용 | `doc/need_twilight3/client/261120_twilight3_kr.gpf` |
| EN 배포용 | `doc/need_twilight3/client/261120_twilight3_en.gpf` |
| 설명 파일 사본 | `doc/need_twilight3/client/{KR,EN}/data/luafiles514/lua files/skillinfoz/skilldescript.lub` |
| 변경 패치 | `doc/need_twilight3/client_kr.patch`, `client_en.patch` |
| 클라이언트 증거 | `doc/need_twilight3/client/manifest.json`, 각 언어의 `entry.before.txt`, `entry.after.txt`, `skilldescript.before.lub` |
| 검증 코드/로그 | `doc/need_twilight3/generate_smoke.py`, `update_clients.py`, `native_smoke.log`, `build_result.json` |
| 빌드 결과 | `generated/twilight3/bin/map-server.exe`, `generated/twilight3/build.log` |

KR은 CP949, EN은 기존 UTF-8 인코딩과 줄바꿈을 보존했다. 각 파일에서 498 항목의 문자열 5개만 바꿨다. 다른 스킬 항목·테이블 키·배열 구조·캐스팅 정보는 보존했다. 설치된 GRF, 패처 목록, 운영 실행 파일은 교체하지 않았다. 작업 전부터 있던 서버·NPC·설정 변경도 보존했다.

## 2. 실제 NEED 소스 추적 결과

1. `skill_factory_merchant.cpp`가 498을 `SkillTwilightAlchemy3`에 연결한다.
2. `skill_check_condition_castbegin()`의 기존 `party_skill_check()` 및 `party.cpp`의 태권계열 파티 판정을 유지한다. `skill_tree.yml`의 파머시 10, 영혼 스킬 플래그 및 기존 스킬 노출/사용 조건도 수정하지 않았다.
3. `skill_check_condition_castend()` 이후 `skill_castend_id()`는 `skill_consume_requirement(..., 1)`로 SP 등의 공통 비용을 처리하고, `skill_castend_nodamage_id()`가 개별 스킬 메서드를 실행한다.
4. 기존 498 메서드는 알코올 100회, 염산병 50회, 화염병 50회의 제조 가능 여부를 모두 먼저 검사했다. 특히 화염병 검사 때문에 **변경 전에도 인벤토리에 완제품 알코올 50개가 필요했다.** 중간 제조 알코올만으로 선행 검사를 통과할 수 있는 구현이 아니었다.
5. `skill_can_produce_mix()`는 제조법·파머시·제조책·개별 재료·결과 스택 한도를 검사하며 재료를 차감하지 않는다.
6. `skill_produce_mix()`는 전달받은 제조 DB 인덱스를 사용하고, 제조 결과 추첨 **전에** `qty × mat_amount`를 차감한다. 따라서 제조 실패분도 재료를 소비한다. 수량 0인 제조책은 보유 검사 대상이며 소비되지 않는다.
7. 개별 메서드에서 `return`해도 바깥의 `skill_castend_nodamage_id()`는 정상적으로 이어져 `skill_consume_requirement(..., 2)`를 호출한다. **레시피 부족으로 실패해도, 그 단계까지 도달했다면 SP 200 및 약사발 200개가 소비되는 기존 정책을 유지했다.** 시전 전/공통 조건 실패까지 무조건 소비한다는 뜻은 아니다.

이번에는 알코올의 제조 가능 검사·100회 제조 호출·200 빈병 고정 검사를 제거했다. 염산병과 화염병을 각각 검사한 뒤, 두 DB 레시피의 양수 소모량을 아이템 ID별로 합산해 한 번 더 확인한다. 빈병 등 공유 재료가 부족할 때 첫 번째 제조만 진행되는 것을 방지한다. 재료 차감 코드를 별도로 추가하지 않았으며, 실제 차감은 기존 제조 함수의 두 호출에서만 이루어진다.

DB 인덱스를 전달하므로 제조 함수 내부의 선택적 사전 검사는 반복되지 않는다. 합산 검사는 읽기 전용이며, 알코올을 스킬 `ItemCost`에 추가하거나 수동 차감하지 않아 이중 차감 경로도 만들지 않았다. 현재 DB에서 약사발은 제조 레시피와 겹치지 않는 공통 비용이다.

## 3. 실제 레시피 및 변경 전후 재료표

활성 Renewal 설정과 `db/re/produce_db.txt` → `db/import/produce_db.txt` 덮어쓰기 순서를 확인했다. import에는 아래 세 제조법의 변경이 없다. 스킬 DB는 `db/re/skill_db.yml` → import 기준이며, 498 override는 없다. 이름이 비슷한 `skill_db_2017cast.yml`은 현재 `db/skill_db.yml`의 import 대상이 아니다.

```text
128,7135,22,228,1,7128,0,970,1,1059,1,713,1
129,7136,22,228,1,7129,0,713,1,929,1
139,970,22,228,1,7127,0,713,1,7033,5,905,5,1092,1
```

| 재료/조건 | ID | 변경 전 검사 및 소비 | 변경 후 검사 및 소비 |
| --- | --- | --- | --- |
| 완제품 알코올 | 970 | 시작 시 50개 보유 검사, 화염병 제조 시 50개 소비. 별도 알코올 제조 성공분 0~100개 생성 | 시작 시 50개 필요, **50개 소비**, 신규 생성 0개 |
| 빈병 | 713 | 200개 | **100개**: 염산병 50 + 화염병 50 |
| 독버섯 포자 | 7033 | 500개 | 불필요/소비 0 |
| 식물의 줄기 | 905 | 500개 | 불필요/소비 0 |
| 시험관 | 1092 | 100개 | 불필요/소비 0 |
| 죽지않는 심장 | 929 | 50개 | 50개 |
| 투명한 천조각 | 1059 | 50개 | 50개 |
| 약사발 | 7134 | 공통 ItemCost 200개 | **200개 유지** |
| 알코올 제조 매뉴얼 | 7127 | 보유 필요, 비소모 | 불필요/소비 0 |
| 화염병 제조 매뉴얼 | 7128 | 보유 필요, 비소모 | 보유 필요, 비소모 |
| 염산병 제조 매뉴얼 | 7129 | 보유 필요, 비소모 | 보유 필요, 비소모 |
| SP | — | 기본 200 | 기본 200 유지 |
| 고정 캐스팅 | — | 3,000ms, IgnoreDex | 변경 없음 |

변경 전 수치는 알코올 100회 + 염산병/화염병 각 50회를 모두 처리한 경우의 소비량이다. 변경 전 알코올 인벤토리 순증감은 `알코올 제조 성공 수 − 50`이며, 변경 후는 항상 `−50`이다. 표의 SP는 DB 기본값이며 기존 장비·상태 보정은 유지한다.

## 4. 성공률·실패·부수 효과

- 공통 `skill_produce_mix()`와 두 개의 50회 호출을 유지했다. 파머시/러닝포션/직업 레벨/INT·DEX·LUK, 호문클루스 보너스, 병 종류 난수 보정, `pp_rate`, 베이비 보정 및 각 시도별 확률 계산을 수정하지 않았다.
- 실제 지급은 각각 0~50개다. 50개 고정 지급을 추가하지 않았다. 알코올 제조를 없앴으므로 동일 난수 시드에서 전체 난수열 위치까지 이전과 같다는 보장은 하지 않는다. 나머지 두 품목의 확률 공식과 추첨 방식은 같다.
- 제조 이름 각인 설정 `produce_item_name_input & 0x2`, 결과 지급·인벤토리 초과 시 기존 바닥 드롭 정책을 유지했다.
- 해당 함수의 제조 연속 성공 카운터/명성 획득은 슬림 포션 성공에 한정된다. 염산병·화염병 성공에는 명성을 새로 주지 않으며, 실패 시 기존 `potion_success_counter` 초기화 경로를 유지했다. 별도 경험치 지급은 추가하지 않았다.
- 황혼 I/II, 일반 파머시, 제조 DB, 스킬 DB 및 공통 스킬 코드는 수정하지 않았다.

## 5. T1~T8 검증 결과

네이티브 하네스는 현재 소스의 **전체** `skill_can_produce_mix()`, `skill_produce_mix()`와 황혼 I/II/III 메서드 본문을 읽어 컴파일한다. 변경 전 III 메서드는 Git blob `00e0250d365e3a3b59fb9c373637f337de9513c1`로 고정해 커밋 이후에도 동일 기준으로 비교한다. 실제 DB의 import 적용 결과를 사용한다. 월드·인벤토리 API·아이템 DB·패킷·난수는 테스트 대역이다. SP/약사발 등 바깥 시전 생명주기는 소스 추적으로 확인했으며, 하네스가 실제 서버 시전을 실행한 것은 아니다.

| TC | 로컬 결과 | 핵심 근거 |
| --- | --- | --- |
| T1 | PASS | 알코올 원재료 3종과 알코올 매뉴얼 없이 50+50 제조 가능 |
| T2 | PASS | 알코올 49개: 제조 호출 0, 레시피 재료 차감 0, 지급 0. 공통 SP/약사발 정책은 위 2절 참조 |
| T3 | PASS | 빈병 99개·심장 49개·천조각 49개를 각각 검사. 두 제조 모두 시작하지 않고 레시피 차감/지급 0 |
| T4 | PASS | 염산병 시도 50, 화염병 시도 50, 알코올 시도 0 |
| T5 | PASS | 알코올 50, 빈병 100, 심장 50, 천조각 50 차감. 매뉴얼 보존, 알코올 신규 지급 0 |
| T6 | PASS | 전부 실패와 혼합 RNG 모두 동일 재료 소비. 혼합 결과 각각 25개, 전부 실패 결과 0개. 제조 오류/중복 차감 없음 |
| T7 | PASS | 황혼 I 흰포션 200, 황혼 II 슬림 흰포션 200, 일반 파머시 화염병 1회의 기존 경로 검증 |
| T8 | PASS (데이터) / 미실시 (화면) | KR/EN Lua 5.1 로딩, 498 외 항목 동일, 기존 3초 캐스팅 줄 유지, GPF 재추출 일치. 인게임 툴팁은 미관측 |

추가로 알코올 분산 스택, 각 매뉴얼 누락, 파머시 미습득, 인벤토리 추가 실패 시 바닥 드롭, DB 재료량 변경 및 빈병 이외 공유 재료 부족도 검사했다. 변경 전 본문 실행으로 기존 원재료 소비량과 완제품 알코올 선행 요구도 확인했다. 전체 **24 PASS / 0 FAIL**, 로그는 `doc/need_twilight3/native_smoke.log`다.

혼합 RNG 시 레시피 재료 인벤토리 비교:

| 아이템 | 시작 | 종료 | 차감/생성 |
| --- | --- | --- | --- |
| 알코올 | 50 | 0 | -50 / 생성 0 |
| 빈병 | 100 | 0 | -100 |
| 심장 | 50 | 0 | -50 |
| 천조각 | 50 | 0 | -50 |
| 각 매뉴얼 | 1 | 1 | 0 |
| 염산병/화염병 | 각각 0 | 각각 25 | 각각 +25 |

서버 빌드는 MSBuild `src/map/map-server.vcxproj`, Debug/Win32, 별도 OutDir로 완료했다. 종료코드 0, 오류 0, 경고 18개(C4819 코드페이지 및 LNK4099 기존 common.pdb 부재), 빌드 로그의 경과 시간은 26.53초다. 빌드 결과는 현재 작업 폴더의 다른 기존 변경도 포함한다. 실행 파일 해시는 `doc/need_twilight3/build_result.json`에 기록했다.

## 6. 클라이언트 설명과 확인 한계

KR/EN 모두 염산병·화염병 각각 50개 **제작 시도**, 완제품 알코올 50개 및 실제 기타 재료/매뉴얼/약사발 200개를 표기했다. 파머시 10·영혼상태·태권계열 파티원과 기존 성공률/캐스팅/사용 조건 유지를 명시했다. 제목에 III를 붙이고 알코올 100개 생산 문구를 제거했다.

로컬 KR `NEED/DATA.ini`는 `need_data.grf` → `data.grf`, EN `DATA_EN.INI`는 `need_english.grf` → `need_data.grf` → `data.grf`를 사용한다. EN 배치는 `-lang en`을 전달한다. 기존 EN GRF의 498 설명에도 오래된 100 Alcohol 문구가 남아 있음을 직접 읽어 확인했다.

KR `need_data.grf`의 해당 엔트리는 인덱스상 존재하고 원본 크기는 614,408바이트다. 확보한 KR main 작업 파일과 크기가 같으나, 압축 데이터를 기존 읽기 도구로 해제하지 못해 배포본과 원본의 바이트 일치까지 입증하지는 못했다. GPF는 확인 가능한 최신 로컬 main 원본 기준이다. 실제 운영 패키지와의 최종 대조 및 화면 확인은 배포 검수에 남는다.

**별도 발견: 기존 EN 전체 빌더의 검증 해시 불일치.** 기존 stamp/supersession 체인의 기대값은 `3e31924be409…`인데 작업 전 설명 파일은 `d510f5ed1981…`이다. 이번 변경 전부터 일치하지 않았다. 현재 변경 후 파일은 `42dec416b008…`이다. 전체 EN 빌더에서 기존 기록을 갱신하려면 이번 498 변경과 작업 전 누적 차이를 별도로 검토해야 한다. 이 작업에서는 과거 PASS 기록을 덮어쓰거나 검증을 우회하지 않았고, 498 변경만 확인한 별도 GPF를 제공한다. 자세한 해시는 `client/manifest.json` 참조.

## 7. 서버 및 KR/EN 업데이트 방법

1. 적용할 서버 릴리스 소스에 이번 `twilightalchemy3.cpp` 변경을 반영해 map-server를 빌드한다. DB·SQL·NPC 변경은 필요 없다. 테스트 빌드 위치는 `generated/twilight3/bin/`이며 운영 루트 실행 파일은 그대로다.
2. 테스트 서버에서 파머시/영혼/파티 조건을 만족한 캐릭터로 T1~T7을 실행한다. 정상 시전에서 약사발 200개·기본 SP 200과 표의 레시피 재료를 확인한다. T2/T3에서는 기존 공통 비용 정책과 레시피 무차감을 구분한다.
3. KR GPF의 단일 `data/luafiles514/lua files/skillinfoz/skilldescript.lub`를 한국어 `need_data.grf`에, EN GPF의 동일 경로를 **영어 `need_english.grf`**에 반영한다. 기존 패처가 사용하는 방식에 따라 각 언어 패치 목록에 별도로 등록한다. 이번 작업에서 등록/게시하지 않았다.
4. 각 GPF는 해당 작업 원본의 **전체 스킬 설명 테이블**을 담는다. 운영 패키지에 더 최신 설명이 있으면 제공한 `client_kr.patch`/`client_en.patch`의 498 항목만 먼저 병합하고 GPF를 다시 만든다. KR 원본과 배포 GRF의 실제 대조도 이 단계에서 완료한다.
5. 한국어와 영어 실행 모드에서 각각 클라이언트를 완전히 종료 후 재실행해 T8을 확인한다. 특히 EN은 영어 GRF가 우선이므로 KR 패치만으로는 영어 문구가 갱신되지 않는다. 느슨한 data 폴더만 복사하는 경우 로딩 우선순위가 확인되지 않아 반영 완료로 판정하지 않는다.
6. 검수 완료 후 서버와 두 언어 패치를 함께 배포한다. 소스 변경은 NPC reload만으로 적용되지 않으므로 새 바이너리 반영 및 정상 재시작이 필요하다.
7. 롤백 시 이번 서버 변경만 되돌려 재빌드하고 각 언어 설명은 제공한 before 파일/운영 백업으로 복원한다. 다른 작업 전체를 reset하지 않는다.

검증 재실행:

```powershell
python doc/need_twilight3/generate_smoke.py
# 생성된 generated/twilight3/smoke/twilight_smoke.vcxproj를 MSBuild Debug/Win32로 빌드
& generated/twilight3/smoke/bin/twilight_smoke.exe
python doc/need_twilight3/update_clients.py --check
```

클라이언트 검증에는 `lupa.lua51`이 필요하다. 이번에는 `generated/twilight3/python`에만 설치했다. 서버 프로세스 기동, SQL 저장 결과, 실제 패킷/클라이언트 화면은 위 오프라인 검증의 범위에 포함하지 않는다.

# NEED 도람족 — 구 페이욘 NPC 통합·퀘스트 정리 결과

- 작성: 2026-11-18, Asia/Seoul / 작업 위치 `F:\rathena`
- 기준 브랜치/커밋: `codex/need-pvp-dummy-vellum` / `70511fcbb0ce42b6c7dfc33268545580bea4144e`
- 이번 변경: **운영용 NPC/로더 5개 파일 수정, 감사·스모크 도구 3개 파일 수정/추가**. 결과 보고서와 선행 보고서의 대체 안내는 별도다.
- **본서버 배포·서버 실행·SQL 변경·commit/push/merge 미실행.** 다른 작업의 미커밋 변경을 보존했다.

## 1. 결론 / [필수 유지 후보]

**구 페이욘 기존 NPC로 통합했다. 라자냐 원형 NPC 파일은 로드하지 않는다. 원형을 반드시 유지해야 하는 퀘스트는 확인되지 않았다.**

재료는 기존 **도구 상인 → 소비**, 토큰 반납·부적 교환·승급은 기존 **교환 자판기 → 도람 장비/부적**에서 처리한다. 새로운 독립 NPC나 도람 상인 복제본은 만들지 않았다. 중요도가 있는 응급치료 습득만 기존 성장 지원관으로 이관했고, 경험치·장비·퀘스트 완료 보상은 따라 옮기지 않았다.

| 유지 후보 / 이름·ID | NPC·시작 조건·핵심 기능 | 비활성 영향 | 원형 유지가 필수인가 / 최종 추천·처리 | 실제 근거 |
|---|---|---|---|---|
| **확인된 필수 원형 퀘스트 없음** | 생성·직업·도람 계열 스킬·일반 던전·부적 교환에 라자냐 퀘스트 선행 잠금을 확인하지 못함 | 아래 선택 보상·스토리 경로는 사라짐 | 원형 퀘스트 전부 비활성 | 실제 NPC/스킬트리/워퍼/활성 파일 의존성 검사 |
| 기능 보존: **EMT / 응급치료, 7722** | Haru, NV_FIRSTAID 미습득이면 별도 레벨·재료 조건 없이 스킬 1레벨과 EXP 100/100. 7722는 즉시 등록·완료되는 기록 | 그대로 끄기만 하면 이 NPC를 통한 도람 응급치료 습득처 소실 | **퀘스트 자체는 비필수. 스킬 기능만 기존 성장 지원관에 이관**. 경험치·7722 등록/완료 없음 | 원형 `Healer Haru#doram07t`, `skill "NV_FIRSTAID",1,SKILL_PERM_GRANT`; 일반 novice 스킬 NPC는 Class Novice/Baby 조건 |
| 수급 검토: **Now I can leave / 출항 준비 완료, 14544** | Doram, LQ1=28 상태로 Stello에게 보고. 모자 19084·반지 28374·EXP 500/500 등 | 두 장비의 확인된 신규 수급처 소실 | **유용한 선택 보상으로 분류**, 원형 비활성. 새 무료 지급·임의 판매/교환 추가 없음; 수급 공백 공개 | Stello 보상 코드 및 활성 지급처/드랍/아이템 그룹 조사 |
| 이동 검토: 자경단 11435~11445 / 선박·용 이야기 | 일부 단계는 conch_in, lasa_dun_q, 이야기용 실내로 이동 | 선박 이야기·퀘스트 방의 NPC를 통한 신규 입장/진행은 닫힘 | **일반 사냥·직업 진행에 필수 아님**. 원형 비활성; 일반 맵 워프·1~3층 워퍼 유지 | `npc/re/warps/cities/lasagna.txt`, `npc/custom/warper.txt` |

`LQ0/1/2`는 각각 저장 변수 `lasagna_quest[0/1/2]`다. 응급치료도 전투 진행에 필수라고 과장하지 않았으며, 기존의 유용한 스킬 기능을 잃지 않도록 분리했다.

**검증 판정:** 오프라인 구조/레시피/거래 본문 스모크 PASS. **검증용 테스트 서버 제출은 가능**, 실제 테스트 서버 검증 완료 판정과 **본서버 운영 배포는 NO-GO**다. 실제 NPC 파서·KR/EN 접속·구매·재접속·Linux 환경 검사가 남아 있다. 이번 작업은 배포하지 않았다.

## 2. 기존 NPC 통합 위치와 메뉴

| 기존 NPC / 실제 위치 | 파일 / 메뉴 경로 | 추가 기능 | 기존 기능 보존 |
|---|---|---|---|
| **도구 상인 / Tool Dealer**, `payo2,76,125` | `npc/NEED/shop.txt` → **소비 / Usable** → 기존 `1기타잡템` 상점 | 515 당근 / 567 새우 / 11602 개박하 열매 | 기존 메뉴·상점 품목 전체 유지, 세 품목만 1회씩 추가 |
| **교환 자판기#1 / Exchange Machine**, `payo2,76,121` | 같은 파일 → **도람 장비/부적 / Doram Equipment/Charms** (새 20번 메뉴) | 무기→토큰 / 방어구·악세→토큰 / 토큰→부적 / 부적 승급 | 원래 1~19번 메뉴 순서와 호출 유지. EP16.1/16.2·다른 재화 상점 불변 |
| **낙원단 성장 지원관#need_beginner / Eden Growth Supporter**, `payo2,96,123` | `npc/NEED/need_beginner.txt` → 미습득 도람에게만 **도람 응급치료 배우기** | NV_FIRSTAID 1레벨 영구 습득 | “성장 지원 이용” 선택 시 원래 등록·코스·보상 처리로 진행. 기존 계정도 스킬만 배울 수 있도록 신규 계정 검사 앞에 배치 |

창고 보급 상점 `payo2,78,125`도 조사했으나 별도 C++ 구매 체계이며 할인 정책도 다르다. 이번 재료는 **표준 상점의 기존 소비 품목**에 편입하여 구매·수량·무게·인벤토리·디스카운트 로직을 그대로 사용한다. 창고 보급 설정과 구매 코드는 수정하지 않았다.

| 품목 | ID | DB 기본 구매가 | 구현 |
|---|---|---|---|
| 당근 / Carrot | 515 | 15z | `515:-1` |
| 새우 / Prawn | 567 | 500z | `567:-1` |
| 개박하 열매 / Nepeta Cataria | 11602 | 15z | `11602:-1` |

6417 개다래열매는 이 상점에 추가하지 않았다. 목걸이 28380~28382의 드랍·아이템 DB·세트도 손대지 않았으며 구매 상품으로 만들지 않았다. 부적 28413~28424는 제니 직판하지 않는다.

### 토큰·부적 경제와 안전 조건

- 하급 28413/28416/28419: 토큰 10개. 특화 28422/28423/28424: 30개.
- 각 계열 하급→중급: 원본 1개 + 30토큰. 중급→상급: 원본 1개 + 90토큰. 상급은 추가 승급 없음.
- 무기 반납 1690~1697의 수량은 1/2/3/4, 1/2/3/4개. 방어구/악세 반납 24개 항목도 원래 목록과 전수 비교했다. 100레벨 슈츠·망토·슈즈는 기존 **3토큰**을 유지했다. 28424 누락 및 28423 중복 없음.
- 이관한 레시피는 원형과 동일하다. 반납 시 선택한 종류의 **조건에 맞는 첫 번째 일반 사본**을 찾고, 확인 창 뒤에 다시 조회한 정확한 인벤토리 index의 1개만 소비한다.
- 미착용·미제련·식별 완료·미파손·카드/랜덤 옵션/등급/기간/귀속/즐겨찾기 없음 조건 유지. 보호할 사본을 임의로 소비하지 않는다.
- 토큰·실제 보상 아이템의 공간/무게를 검사하고, 삭제 실패 시 토큰 지급/차감·승급 보상을 진행하지 않는다. 메뉴 보기·취소는 소모 없음. 위 검사는 원래 안전 헬퍼를 그대로 재사용한다.
- 승인부터 소비·지급까지 대화/대기 지점을 두지 않는다. 이는 서버 내 연속 처리이며 강제 종료까지 보장하는 SQL 트랜잭션이라는 뜻은 아니다.

## 3. 라자냐 원형 NPC 정리와 저장 데이터

| 대상 | 최종 상태 | 이유 / 보존 사항 |
|---|---|---|
| `npc/re/custom/lasagna/lasagna_npcs.txt` | **전체 미로드** | 파일은 삭제·덮어쓰기하지 않음. 상인·챠라 가족·퀘스트·성장/일일·선박 NPC가 모두 생성되지 않음 |
| 원형 새우 상인·도구 상점 | 미노출 | `doram_shrimp`와 원형 NPC를 따로 분리 로드하지 않음. 구 페이욘 기존 상점만 활성 경로 |
| 요타·고마·단디·죠기 | 미노출 | 기능과 수량만 기존 교환 자판기의 함수 메뉴로 이관 |
| 자경단·튜토리얼·110~140으로 알려진 성장/일일 | 전부 미노출 | 실제 코드의 고레벨 시작 조건은 주로 **Base 140 이상**이며 원형 헤더의 설명과 구분했다 |
| 원형 스크립트 정의 수 | 주석 제외 **123개 script + 7개 duplicate** 미로드 | 이전 보고서의 124개는 주석 안 디버그 정의도 포함한 텍스트 집계였음. 숨은 상점 정의도 전체 파일과 함께 미로드 |
| `npc/NEED/need_doram_exchange.txt` | **함수 정의만 1회 로드** | 좌표 NPC, 독립 상점, 퀘스트 시작/완료/보상 이벤트 없음 |
| `lasa_fild.txt` / `lasa_dun.txt` | 기존 로드 유지 | 몬스터·보스·드랍 변경 없음 |
| `npc/re/warps/cities/lasagna.txt` / 기존 NEED 워퍼 | 기존 로드 유지 | 필드·일반 던전 1~3층·실내/선박 출구·구 페이욘 인챈트관 출입 유지 |
| Hypnotist 초기화 / Cat Paw 저장·창고 | 원형 서비스 미노출 | 기존 구 페이욘 카프라 `payo2,83,128`의 스탯/스킬 초기화·창고 등과 중복, 새 복제본 없음 |
| quest DB / 캐릭터 저장 변수·아이템 | **유지** | 14564/14577/14578을 포함한 기존 quest DB 정의, LQ·conch_in_*·sous_chef_quest·퀘스트 로그·기존 보상 미삭제 |

활성 로더를 따라 전체 파일을 조사했을 때 이 87개 퀘스트를 직접 조회·시작하는 외부 NPC나, 사라지는 원형 NPC로 보내는 명시적 이벤트 호출은 발견되지 않았다. 범용 퀘스트 초기화나 관리 기능까지 없다는 뜻은 아니다.

비활성화 후 NPC를 통한 **새 시작·완료·보상·재지급은 불가능**하다. 이미 진행 중인 캐릭터의 로그는 남으며, 공통 엔진은 보존한 quest DB의 사냥 카운트나 퀘스트 드랍을 계속 처리할 수 있다. 이를 막으려고 저장 퀘스트/데이터를 지우거나 공통 엔진을 바꾸지 않았다. 이전 퀘스트를 완료할 수 없게 된다는 운영 변화는 안내해야 한다.

기존 conch_in 캐릭터는 보존한 선박 출구를 사용할 수 있고, 일반 던전은 기존 층간 워프가 남는다. 이야기용 실내에 남은 캐릭터의 귀환은 테스트 서버에서 확인해야 한다. 일반 Player 그룹의 기존 `@load` 허용 설정도 유지했으며, 새 워프 NPC나 자동 이동 이벤트를 만들지 않았다.

## 4. 퀘스트 전수 현황 — 87개

아래 이름은 유효 `quest_db`의 실제 Title이다. KR 클라이언트의 별도 현지화 이름을 추측하지 않았다. 시작 NPC는 실제 `setquest` 또는 난수/변수 시작 경로를 추적했다. 보상은 **코드에 있는 지급 명령**을 기준으로 적었고, “받으라”는 대사만 있는 것을 실제 보상으로 계산하지 않았다.

공통 처리 **D** = 선택/비필수, 원형 NPC 비활성·quest DB/저장 데이터 보존. **S** = 유용한 유일 보상 수급 검토 대상이지만 퀘스트는 비활성. **F** = 퀘스트 비활성·기능만 구 페이욘으로 이관. 원형 유지가 필수인 항목은 0개다. EXP는 Base/Job 순서, “다음 단계”는 다른 퀘스트/이야기 진행만 의미한다.

| 이름 / ID | 시작 NPC (원형) | 시작 조건·수행 목적 | 주요 보상·기능 | 필수 여부 / 최종 처리·이유 |
|---|---|---|---|---|
| Beginning / **7711** | [#doint_evt01](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:25); [Vigilante Ajegna#doram0](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:71) | Summoner, 미시작, 시작 필드 OnTouch; 입문 대화 시작 | 다음 이야기 단계; 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Where am I? Who am I?? / **7712** | [Vigilante Ajegna#doram0](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:73); [Vigilante Aglio#doram02](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:302) | Summoner, Ajegna와 대화; 자신/마을 소개 | 다음 이야기 단계; 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| First Battle! / **7713** | [Vigilante Aglio#doram02](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:304) | Summoner, LQ0=0; 에그링 3마리 처치 | EXP 70/70 (완료), 사과주스 531×1 (다음 단계 재료) | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Refreshing Apple Juice / **7714** | [Vigilante Aglio#doram02](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:341) | 7713 완료, LQ0=2; 지급받은 사과주스 사용/소지 확인 | 1687×1, 초보포션 569×30, EXP 70/70 | 비필수 S — 수급 공백 공개, 임의 보상 없음 |
| Midding Box / **7715** | [Vigilante Aglio#doram02](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:464) | LQ0=3→4; 배달 상자 1081 회수 | EXP 100/100, 다음 마을 단계 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| To the Village! / **7716** | [Vigilante Aglio#doram02](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:499) | 배달 상자 회수, LQ0=5; 마을 아이/상인에게 상자 전달 | EXP 70/70, LQ0=6 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Merchant's Kindness / **7717** | [Tool Merchant#doram05t](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:932) | LQ0=6; 상인 지원품 수령/약초 안내 | 15150×1, 569×50, 12323/12324 각10, 7059×10, 12849×5, EXP 50/50 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| I'm used to this / **7718** | [Healer Haru#doram07t](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:1120) | Haru, 미시작 또는 LQ0=7; 약초 확인; 약초·조합 키트 체험 | 11058×1, 12849×10, 569×30, 507/512 각1, EXP 200/200 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Time to Digest / **7719** | [Sloth#doram](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:584) | Sloth, 미시작; 빨간 풀 3개체 사냥·빨간허브 소지 | 11601×20, 12323/12324 각10, EXP 80/80 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Service Manual / **7720** | [Cat Paw Service#lasagna](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:1579) | Summoner, Cat Paw 방문, 미시작; 저장·창고 사용 안내 | 12323/12324 각10, EXP 50/50; 기존 카프라로 기능 대체 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Cat showoff contest / **7721** | [Well-known Troublemaker](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:689) | Summoner, 미시작; 실패 시 재도전; +10분 안에 루나틱3/파브르2 | EXP 120/120 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| EMT / **7722** | [Healer Haru#doram07t](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:1060) | Haru 별도 메뉴, NV_FIRSTAID 미습득; 응급치료 습득 기록 | NV_FIRSTAID Lv.1 + EXP 100/100; 현재는 스킬만 기존 지원관으로 이관 | 비필수 F — 스킬만 기존 지원관으로 이관 |
| The potential within / **7723** | [Applicant Paul#doramt13](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:140) | Summoner, 지원자 Paul/MacCarnie와 대화; Gnocchi의 잠재력 안내 | EXP 50/50 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Vigilante Corp / **11435** | [Vigilante Guide#lasa](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:3887) | LQ1=0~1; 자경단 안내/서장 소개 | 다음 자경단 단계; 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Vigilante Corp / **11436** | [Chief Peperoncino#lasa](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:4094) | 서장 상담 후 LQ1=2; Olio의 자경단 설명 | 다음 자경단 단계; 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Vigilante Corp / **11437** | [Vigilante Olio#lasa](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:4035) | Olio, LQ1=2→3; 자경단 가입/출항 안내 | Summoner: 22076×1, EXP 500/400 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Vigilante Corp / **11438** | [Chief Peperoncino#lasa](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:4123) | 서장, LQ1=3→4; 이야기용 실내로 이동 | 다음 자경단 단계; 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Vigilante Corp / **11439** | [Chief Peperoncino#lasai](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:4447) | 실내 서장, LQ1=4→5; 바실리스크 포로 심문 소개 | 다음 자경단 단계; 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Vigilante Corp / **11440** | [Basilisk Prisoner#lasai](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:4270) | 포로, LQ1=5→6; 가시덩굴 재료 의뢰 | 다음 자경단 단계; 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Vigilante Corp / **11441** | [Jindou#lasain](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:4399) | Jindou, LQ1=6, 25044×5; 가시덩굴 피리 제작 | 이야기 아이템 25043×1 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Vigilante Corp / **11442** | [Basilisk Prisoner#lasai](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:4374) | 포로, LQ1=7; 피리 선택 대화; 심문 마무리 | 1683 또는1684×1 선택, EXP 500/400 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Vigilante Corp / **11443** | [Chief Peperoncino#lasa](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:4147) | 마을 서장, LQ1=8→9; Tali 소개 | 다음 자경단 단계; 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Vigilante Corp / **11444** | [Tali#lasa](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:4537) | Tali, LQ1=9→10; 로드프로그에서 옷감 25045×2 획득 | 다음 자경단 단계; 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Vigilante Corp / **11445** | [Tali#lasa](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:4560) | Tali에게 옷감2 전달 후 서장 LQ1=11; 자경단 정리/승선권 수령 | 15151×1, 25046×1, EXP 700/500 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Fragrant herb mix / **14154** | [Sous Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7321) | 선박 이야기 완료 LQ1=29, Base≤30, sous_chef_quest=1; 허브무침 만들기; 705/511/507 각1 | 11551×1 (+1 스탯 음식) | 비필수 S — 수급 공백 공개, 임의 보상 없음 |
| Crunchy salad / **14155** | [Sous Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7344) | 선박 이야기 완료 LQ1=29, Base≤30, sous_chef_quest=1; 사과 당근 샐러드; 512×1 + 515×2 | 11552×1 (+1 스탯 음식) | 비필수 S — 수급 공백 공개, 임의 보상 없음 |
| Anything shabushabu / **14156** | [Sous Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7368) | 선박 이야기 완료 LQ1=29, Base≤30, sous_chef_quest=1; 전골 만들기; 949/909/914 각1 | 11553×1 (+1 스탯 음식) | 비필수 S — 수급 공백 공개, 임의 보상 없음 |
| Baked golden apple / **14157** | [Sous Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7391) | 선박 이야기 완료 LQ1=29, Base≤30, sous_chef_quest=1; 사과구이 만들기; 705/512 각1 | 11554×1 (+1 스탯 음식) | 비필수 S — 수급 공백 공개, 임의 보상 없음 |
| Everything about Con-Chliina / **14531** | [Eggling#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:4643) | 승선권25046, LQ1=12→13; 선박 탑승·Purser 소개 | Summoner는 25047×1 (배달재료); conch_in 이동 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Greetings to Captain Gamberi / **14532** | [Purser#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:4994) | Summoner, Purser, LQ1=13→14; 선장 인사 | EXP 200/300 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Say hi to recorder / **14533** | [Captain Gamberi#dorcon0](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:5606) | Summoner, 선장, LQ1=14→15; 기록관 인사 | EXP 200/300 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| To the kitchen / **14534** | [Recorder#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:5839) | Summoner, 기록관, LQ1=15→16; 주방 방문 | EXP 200/300 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| To Purser / **14535** | [Boatswain#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:6545); [Helmsman#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:6975); [Sous Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7086) | 갑판장/조타수/부주방장, LQ1=16→17; Purser에게 복귀 | EXP 200/300 (먼저 완료한 분기) | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Delivery to Chef / **14536** | [Purser#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:5042); [Sous Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7189) | Purser, LQ1=17→18; 25047을 부주방장에게 배달 | EXP 200/300; 이후 초보 요리 분기에서도 기록 재사용 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| When do we take off? / **14537** | [Sous Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7160) | 부주방장, LQ1=18→19; 출항 사정 보고 | 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Can we take off? / **14538** | [Purser#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:5102) | Purser, LQ1=19→20; Stello에게 출항 문의 | 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Crazy Dragon / **14539** | [Stello#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:5275) | Summoner, Stello, LQ1=20→21; 용 조사 시작 | 569×30, 12323×20 (시작 시) | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| An errand out of nowhere / **14540** | [Eggling#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:4916) | Summoner, Eggling, LQ1=21; 반환 LQ1=27; 바실리스크 조개25049×3 전달 | 11601×10, EXP 300/300 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| No news is not a good news / **14541** | [Vigilante#dorcon01](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7721) | 마을 Vigilante#dorcon01, LQ1=21→22; 동굴 자경단원 탐색 | 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| How is the Dragon / **14542** | [Vigilante#dorcon02](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7876) | 동굴 Vigilante#dorcon02, LQ1=22→23; Tartufi에게 용 상태 확인 | 이야기용 lasa_dun_q 이동 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Basilisk Hunt / **14543** | [Tartufi#dorcon01](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:8053) | Tartufi#dorcon01, LQ1=23→24; DR_BASILISK1 20마리 처치 | EXP 500/500 (완료) | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Now I can leave / **14544** | [Tartufi#dorcon01](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:8090) | 사냥 완료 후 LQ1=24→27, Eggling 거쳐 Stello LQ1=28; 도람 출항 이야기 완료 | 19084/28374 각1, 11601×50, 12325×20, EXP 500/500; 22984×5, 이즈루드 이동 | 비필수 S — 수급 공백 공개, 임의 보상 없음 |
| To Purser / **14545** | [Eggling#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:4641) | LQ1=12, Eggling, 승선권25046; 선박 안내·Purser 기록 | 즉시 완료 기록; 14531과 같은 탑승 분기 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Take care of grass thief / **14546** | [Shepherd#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:2502); [Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:3295) | Base6~9, Shepherd/Chef, 14592 없음/만료; DR_LUNATIC5 + DR_FABRE5 | EXP120/200 (상위레벨 Shepherd 완료 분기는150/250) | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Can we set out the sailing? / **14547** | [Purser#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:4958) | 비Summoner, Purser, LQ1=13→14; 인간용 출항 대화 | 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Crazy Dragon / **14548** | [Captain Gamberi#dorcon0](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:5544) | 비Summoner, 선장, LQ1=14→21; 인간용 용 조사 | 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| No news is not a good news / **14549** | [Vigilante#dorcon01](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7692) | 비Summoner, 마을 자경단원, LQ1=21→22; 동굴 자경단원 탐색 | 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| How is the Dragon / **14550** | [Vigilante#dorcon02](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7925) | 비Summoner, 동굴 자경단원, LQ1=22→23; 용 상태 확인 | 이야기용 경로/다음 단계 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Chief Basilisk / **14551** | [Tartufi#dorcon02](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:8254) | 비Summoner, Tartufi#dorcon02, LQ1=23→24; 바실리스크 우두머리 소개 | 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Lord Imuk's message / **14552** | [Lord Imuk#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:8164) | 비Summoner, Lord Imuk, LQ1=24→25; 이무기의 메시지 | 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Basilisk Hunt / **14553** | [Tartufi#dorcon02](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:8309) | 비Summoner, Tartufi#dorcon02, LQ1=25→26; DR_BASILISK3 20마리 처치 | 별도 지급 명령 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| We can start sail now / **14554** | [Tartufi#dorcon02](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:8341) | 비Summoner, 사냥 완료 LQ1=26→27; Stello LQ1=28; 인간용 출항 이야기 완료 | 22984×5, 이즈루드 이동 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Whole new world of taste / **14555** | [Sous Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7489) | Base≥140, 선박 진행 LQ1=29 이후; 522×4 + 11536×1 제출 | 명시적 EXP/아이템 보상 없음; 14563 대기 시작 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| There also is a scary thing for me / **14556** | [Boatswain#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:6702) | Base≥140, LQ1≥29, conch_in_boatswain=0; 갑판장 두려움 상담 | 명시적 EXP/아이템 보상 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Truth is alway harsh / **14557** | [Shaman meow#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:3786) | conch_in_boatswain=1→2, Shaman 상담; 진실을 듣고 갑판장에게 복귀 | 명시적 EXP/아이템 보상 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Dirty Creatures / **14558** | [Boatswain#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:6773) | conch_in_boatswain=2→3; 25049×5 및22985×20 제출 | EXP 500000/0, 14559 대기 시작 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Charm Effect Duration / **14559** | [Boatswain#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:6850) | 14558 또는14560~14562 완료; 04:00 기준 반복 대기 | 자체 보상 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| New Ingredient / **14560** | [Shaman meow#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:3848) (난수) | conch_in_boatswain=4, 14559 만료 후 Shaman 난수 선택; 25049×5 + 523×1 제출 | EXP 500000/0 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Dirty and Filty / **14561** | [Shaman meow#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:3848) (난수) | 동일 반복 조건, Shaman 난수 선택; 25049×20 제출 | EXP 500000/0 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| The basic are the best / **14562** | [Shaman meow#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:3848) (난수) | 동일 반복 조건, Shaman 난수 선택; 25049×5 + 22985×20 제출 | EXP 500000/0 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Free time / **14563** | [Sous Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7511) | 14555 또는14564~14566 완료; 04:00 기준 부주방장 대기 | 자체 보상 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| I love buns, too / **14564** | [Sous Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7609) | Base≥140, 14555 완료, 14563 만료 후 난수; 553 빵20개 전달 | 명시적 EXP/아이템 보상 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Emergency food supplies / **14565** | [Sous Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7620) | 같은 반복 조건, 난수 선택; 11536/12636 각5개 전달 | 명시적 EXP/아이템 보상 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Brilliant idea / **14566** | [Sous Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:7630) | 같은 반복 조건, 난수 선택; 7066 얼음조각20개 전달 | 명시적 EXP/아이템 보상 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Kaluna milk is in Danger! / **14567** | [Kalu#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:8763) | Base≥140, Kalu, LQ2=0→1; 우유 사정 조사/Legalas 방문 | 명시적 EXP/아이템 보상 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Time to persuade / **14568** | [Legalas#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:9028) | Legalas, LQ2=1→2; Daldal/Gopara 설득 | 다음 이야기 단계 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Babysitter / **14569** | [Gopara#dorcon01](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:9250); [Daldal#dorcon01](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:9406) | Daldal 또는Gopara, LQ2=2→3; 아기 돌보기 | 다음 이야기 단계 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Processing Anchovy / **14570** | [Cute Baby#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:9538) | Cute Baby, LQ2=3→4; 멸치 작업장 조사 | 다음 이야기 단계 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Anchovy fishing ship / **14571** | [Workshop Manager#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:9656) | Workshop Manager, LQ2=4→5; 멸치잡이 선장 방문 | 다음 이야기 단계 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Helpless... / **14572** | [Captain of Anchovy Fish](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:9810) | Captain of Anchovy Fish, LQ2=5→6; Legalas에게 보고 | 다음 이야기 단계 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| I need the time on my own / **14573** | [Legalas#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:9108) | Legalas, LQ2=6→7; Kalu에게 개인 시간 문제 보고 | 다음 이야기 단계 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Meal Box Delivery / **14574** | [Kalu#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:8834) | Kalu, LQ2=7→8; Panna에게 25048 도시락 전달 | 25048×1은 배달용 재료 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Delivery Complete Report / **14575** | [Panna#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:9956) | Panna, 도시락 전달 LQ2=8→9; Kalu에게 배달 완료 보고 | 명시적 EXP/아이템 보상 없음; 14576 대기 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Meal Box Delivery Time / **14576** | [Kalu#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:8894) | Kalu, LQ2=9→10 또는14578 완료; 04:00 기준 도시락 반복 대기 | 자체 보상 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Meal Box Delivery / **14577** | [Kalu#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:8960) | LQ2=10, 14576 만료; Panna에게 반복 도시락 전달 | 25048×1은 배달용 재료 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Delivery Complete Report / **14578** | [Panna#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:9961) | Panna, 반복 도시락 전달 후; Kalu에게 반복 완료 보고 | 명시적 EXP/아이템 보상 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Interview with Captain / **14579** | [Recorder#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:6024) | 기록관, LQ1=29 이후, conch_in_recorder=0→1; 선장 인터뷰 | 다음 이야기 단계 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| To Recorder / **14580** | [Captain Gamberi#dorcon0](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:5766) | 선장, conch_in_recorder=1→2; 기록관에게 선장 인터뷰 보고 | 다음 이야기 단계 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Interview with the Chief / **14581** | [Recorder#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:6078) | 기록관, conch_in_recorder=2→3; 서장 인터뷰 | 다음 이야기 단계/이야기 실내 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| To Recorder / **14582** | [Chief Peperoncino#dorco](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:8521) | 서장, conch_in_recorder=3→4; 기록관에게 서장 인터뷰 보고 | 다음 이야기 단계 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| To Granma / **14583** | [Recorder#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:6144) | 기록관, conch_in_recorder=4→5; Grandma 인터뷰 | 다음 이야기 단계 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Back to Recorder / **14584** | [Grandma#dorcon01](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:8613) | Grandma, conch_in_recorder=5→6; 기록관에게 보고; 기록관 이야기 완료 | EXP 700/700 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| The Competition / **14588** | [Florin#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:2635); [Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:3320) | Base10~13, Florin/Chef, 대기 없음/만료; DR_HORNET10 | EXP200/300 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Sticky ingredient / **14589** | [Prok#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:2781); [Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:3333) | Base14~15, Prok/Chef, 대기 없음/만료; DR_RODA_FROG10 + 918×5 | EXP300/500 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Hunter's destination / **14590** | [Fisher#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:2910); [Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:3352) | Base16~19, Fisher/Chef, 대기 없음/만료; DR_DESERT_WOLF_B10 | EXP400/800 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Secret Ingredient / **14591** | [Sleepy meow#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:3078); [Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:3419) | Base20, Sleepy meow/Chef, 대기 없음/만료; DR_SPORE10 + 921×5 | EXP500/1000 | 비필수 D — 경험치·스토리·심부름/이동 중복 |
| Resting Time / **14592** | [Shepherd#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:2461); [Florin#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:2588); [Prok#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:2745); [Fisher#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:2869); [Sleepy meow#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:3099); [Chef#dorcon](F:/rathena/npc/re/custom/lasagna/lasagna_npcs.txt:3461) | 위 레벨별 사냥 심부름 완료; 04:00 기준 반복 대기 | 자체 보상 없음 | 비필수 D — 경험치·스토리·심부름/이동 중복 |

**전수 확인에서 달라진 판단**

- `7722`는 라자냐 튜토리얼 전체를 완료해야 얻는 필수 퀘스트가 아니다. 미습득 상태에서 Haru의 별도 메뉴로 바로 배우며, 새 기능도 스킬만 이관했다.
- `14555`, `14564~14566`, `14575/14578` 등은 보상을 암시하는 대사가 있어도 명시적 EXP/아이템 지급 코드가 없는 구간이다. 누락 보상을 추정 구현하지 않았다.
- `TimeLimit: 4h`는 `+4h`와 다르다. `src/map/quest.cpp`는 +가 없는 값을 특정 시각으로 파싱하므로 이 목록의 대기표시는 **04:00 기준**이다. 4시간 경과 후 반복으로 임의 해석하지 않았다. `7721`의 `+10mn`은 상대 10분 제한이다.
- `14585~14587` 등 숫자 범위 안의 다른 퀘스트는 이 라자냐 파일의 실제 참조가 아니므로 끄거나 DB에서 지우지 않았다.

## 5. 보상 대체 수급과 공백

| 보상 / 관련 퀘스트 | 실제 다른 경로·대체 확인 | 비활성 후 영향 / 최종 처리 |
|---|---|---|
| **1687 초보자용 강아지풀**, 7714 구간 | 동일 아이템의 다른 활성 지급/드랍 경로 미발견. 신규 생성 기본 무기 **1681**은 유지; 12/20레벨 드랍 무기, 기존 성장 지원의 Lv.100 26120 경로 존재 | 동일 물건 신규 수급 공백. 초반 무기 기능은 다른 장비로 대체되므로 퀘스트 비활성, 임의 무료 지급 없음. 26120은 Lv.100이므로 3레벨 장비와 동급 대체라고 표현하지 않음 |
| **15150 하얀 셔츠**, 7717 | `DR_FABRE`(3497) 드랍 정의·기존 lasa_fild01 스폰 | 드랍 유지. 성장 퀘스트를 유지할 이유 없음 |
| **15151 하얀 친환경 셔츠**, 11445 | `DR_SPORE`(3501) 드랍 정의·기존 lasa_dun01 스폰 | 드랍 유지 |
| **22076 원목 슬리퍼**, 11437 | `DR_HORNET`(3498) 드랍 정의·lasa_fild02 스폰 | 드랍 유지 |
| **1683/1684 강아지풀**, 11442 | `DR_RODA_FROG`(3499) 드랍 정의·필드 스폰 | 선택 퀘스트 보상을 끄고 드랍 유지 |
| **19084 파르펠레 자경단 모자**, 14544 | 다른 활성 동일 아이템 획득처 미발견. LUK+1·공격 시 Blessing 자동 발동은 유용하지만 다른 도람 시스템의 필수 키가 아님 | **유일 신규 수급처 공백**. 기존 소지품 유지. 임의 비용·교환·무료 지급 추가 없음. 필요하면 별도 운영 결정으로 기존 보상 체계에서 비용/조건을 정해야 함 |
| **28374 강아지풀 반지**, 14544 | 다른 활성 동일 아이템 획득처 미발견. EXP+5%와 레벨별 ATK/MATK/HP/SP 보조 옵션. 초보 지원은 기능상 대안일 뿐 동일 옵션 보장은 아님 | **유일 신규 수급처 공백**. 기존 소지품 유지. 부적·목걸이·세트의 필수 구성품이 아니므로 원형 퀘스트를 강제 유지하지 않음 |
| **11551~11554 +1 음식**, 14154~14157 | academy에도 동일 레시피 지급 코드가 있지만 그 파일은 현 활성 로더 밖. 같은 ID의 활성 대체 지급처를 확정하지 못함 | 초보 요리 체험 수급 공백으로 기록. 버프/요리 기존 시스템 보존, 새로운 무료 지급 없음 |
| **569/11601/12323/12324/12325/7059/11058/12849 등 보조품** | 일반 포션·귀환/이동·돋보기 기능은 기존 상점/카프라/성장 지원이 담당. `Chef` 상점의 12849 등 기존 판매 유지 | **같은 비매품 ID의 재지급까지 보장하는 것은 아님**. 진행에 필수이지 않으므로 경험치/소모품 보상 퀘스트 비활성 |
| **25043/25044/25045/25046/25047/25048/25049, 22984 등 이야기 재료** | 피리·가시덩굴·옷감·승선권·도시락·조개·우유 등 해당 이야기/소모품 | 원형 진행/재지급 경로 닫힘. 기존 물품 강제 회수·교환·자동 처분 없음. 스킬 재료 515/567/11602와 구분 |

“다른 활성 경로 미발견”은 정적 로더·DB·직접 지급 코드 검색 결과다. 운영 DB의 과거 GM 지급·이벤트 재고·거래 기록까지 조사했다는 뜻은 아니다. 같은 보상을 임의로 성장 지원관에 추가하지 않았다.

## 6. 실제 수정 파일과 검증

| 파일 | 이번 수정 |
|---|---|
| `npc/NEED/shop.txt` | 기존 소비 상점에 세 재료 추가, 기존 교환 자판기에 20번 도람 메뉴 연결 |
| `npc/NEED/need_doram_exchange.txt` | 기존 정확한 사본/무게/실패 안전 헬퍼 유지. **함수만**으로 무기/방어구 반납·부적 구매/승급·응급치료 추가 |
| `npc/NEED/need_beginner.txt` | 기존 성장 지원관 입구에 미습득 도람 응급치료 선택지만 추가; 원래 지원·코스·보상 불변 |
| `npc/scripts_need.conf` | 공용 도람 함수 파일을 기존 shop.txt 앞에서 1회 로드. 다른 기존 미커밋 hunk 보존 |
| `npc/re/scripts_athena.conf` | 앞선 작업의 원형 라자냐 활성화를 되돌려 주석 처리, 헬퍼는 NEED 로더로 이동. **이번 수정 결과 이 파일은 HEAD와 같아 일반 git diff에 안 보일 수 있음** |
| `doc/need_doram/audit.py` | 라자냐 활성 전제 제거, 현재 구 페이욘 토폴로지·재료·보존 DB 검사로 갱신 |
| `doc/need_doram/consolidation_audit.py` | 기존 메뉴/재고·반납 레시피 비교, 87개 퀘스트/NPC/외부 의존성·보존 해시 검사 및 증거 JSON 생성 |
| `doc/need_doram/consolidation_smoke.js` | 실제 함수 본문을 읽어 변수/명령 문법만 변환하고 대역 엔진으로 핵심 교환·취소·중복/실패·스킬 기능 검사 |
| `NEED_Doram_EP162_Integration_Report.md` | 앞선 보고서의 NPC/퀘스트 운영 방향이 이 보고서로 대체됨을 상단 표시 |
| 이 보고서 | 새 운영 기준·전수 목록·공백·검증 한계·반영/롤백 기록 |

작업 시작 시 별도 변경이 있던 Berserk·장비 검사·PvP Dummy·BG·접속 conf·메시지 파일 등은 수정/초기화하지 않았다. 원형 라자냐 파일의 앞선 4,790줄 번역도 다시 쓰거나 운영 대사로 검증하지 않았으며 파일만 보관했다.

| 검사 | 결과 | 의미와 한계 |
|---|---|---|
| 도람 전투·아이템·시작점·quest DB·스폰/워프 보존 | **PASS** | 작업 전 해시를 저장한 11개 파일과 일치. C++ 14개 스모크를 다시 장시간 실행하지 않음 |
| 기존 메뉴·재고 | **PASS** | 기존 교환 선택지 19개·상점 재고 목록 12개 불변, 정확히 재료 3개만 추가 |
| 교환 경제 | **PASS** | 원형의 무기 8개·방어구/악세 24개 반납 쌍과 수량 일치; 부적 6개 구매·6개 승급 전 경로 검사 |
| 오프라인 함수 본문 거래 스모크 | **PASS — 14그룹** | 무기/방어구·부적/승급·중복 사본·카드/제련 등 제외·취소·재료/토큰 부족·과적/공간 실패·삭제 실패·확인 후 상태 재검사·KR/EN 메뉴·응급치료 중복 방지 |
| 로더·퀘스트 의존성 | **PASS** | 원형 미로드, 헬퍼 1회 선로드, 87개 quest DB 보존, 활성 외부의 명시적 퀘스트/NPC 이벤트 의존성 없음 |
| 구문 기본 검사 / diff | **PASS** | 활성 파일 구분자 균형 및 범위 diff 검사. **rAthena 실제 파서 PASS라는 뜻 아님** |
| 실제 NPC 파서/서버 시작 | **NOT TESTED** | 연결 대상 DB와 격리 테스트 서버가 확정되지 않은 상태에서 운영 설정으로 서버를 실행하지 않음 |
| 소량·대량 실제 구매 / 할인·패킷 | **NOT TESTED** | 표준 상점 코드 재사용과 DB 가격 정적 검사만 수행 |
| 재접속·창고·저장·중복 패킷 / 실제 KR/EN 화면 | **NOT TESTED** | 실접속 클라이언트·테스트 계정 미사용. 저장 일관성까지 대역 스모크로 통과 처리하지 않음 |
| Linux 본서버 빌드/기동 | **NOT TESTED** | 이번에는 NPC만 변경. C++ 재빌드 필요 없음. Linux 테스트 환경 검증 완료로 표기하지 않음 |

스모크의 API 대역은 인벤토리·메뉴·회복/스킬 저장 엔진의 실물 대체물이 아니다. 실제 `.txt` 함수 본문으로 분기/소모를 확인하지만 실제 서버 파서·SQL 영속화·클라이언트를 대신하지 않는다. 보고서의 PASS와 NOT TESTED를 구분해야 한다.

재현:

```powershell
Set-Location F:\rathena
python doc/need_doram/consolidation_audit.py
node doc/need_doram/consolidation_smoke.js
```

Python 감사 도구는 PyYAML이 필요하다. 작업 시작 시 해시/수정 전 스냅샷 및 `quest_inventory.json`, `inventory.json`, `consolidation-audit.log`, `transaction-smoke.log`는 `F:\rathena\docs\reports\need-doram-oldpayon-20261118\`에 있다. 기존 ignore 폴더이므로 배포 대상이 아니다.

## 7. 테스트 서버 반영·운영자 TQ·롤백

1. **반영 전 백업:** 운영의 `shop.txt`, `need_beginner.txt`, 도람 함수 파일, 두 로더를 백업한다. 관련 없는 기존 변경을 전체 checkout/reset하지 않는다.
2. **동시에 전달:** 이번 운영 파일 5개를 검증용 테스트 서버에 함께 반영한다. `npc/scripts_need.conf`는 함수 파일→shop.txt 순서이고, `npc/re/scripts_athena.conf`의 원형은 계속 주석이어야 한다. 기존 스킬/목걸이/시작 무기의 선행 변경은 유지한다. 새로운 SQL·item/quest DB 변경은 없음.
3. **로딩 확인:** 서버 재시작 또는 적절한 전체 스크립트 재로드로 새 함수·기존 NPC 메뉴·원형 NPC 제거를 함께 적용한다. 도람 파일만 개별 reload해서 기존 라자냐 NPC가 남은 상태를 완료로 보지 않는다. 실제 parser 오류/누락 함수/중복 이름 로그를 확인한다.
4. **접속 TQ:** KR/EN 각각 도구 상인 소비 메뉴에서 515/567/11602를 1개와 100개씩 구매하고 제니/수량/무게·할인·6417 미판매를 확인한다. 기본가 합계는 각 1개 530z, 각 100개 53,000z이며 실제 할인 적용가는 표준 상점 기준이다.
5. **거래 TQ:** 기존 1~19 메뉴, 무기 1691→2토큰, 하급 부적 10토큰, 중급/상급 30/90토큰+원본, 28424 특화 30토큰, 장착/카드/제련 사본과 일반 사본 동시 보유, 확인 취소·토큰 부족·공간 부족·창고·재접속을 한 번씩 확인한다. 새 도람 관련 독립 NPC가 라자냐나 구 페이욘에 생기지 않아야 한다.
6. **퀘스트/스킬 TQ:** 원형 퀘스트 새 시작/완료/지급이 불가능하고 기존 저장 로그·장비가 유지되는지 확인한다. 도람 응급치료 학습·취소·기습득·신규 지원 제외 계정 접근, 원래 성장 코스 메뉴도 확인한다. 미습득 도람만 새 선택지가 표시된다.
7. **이동 TQ:** 라자냐 필드/던전 1~3층·인챈트관 왕복과 기존 선박/이야기 실내 잔류 캐릭터의 귀환을 확인한다. 기존 캐릭터 로그/물품을 자동 초기화하는 작업은 하지 않는다.
8. **클라이언트:** 아이템/스킬 설명 파일 신규 변경 없음. 기존 전투 작업의 iteminfo/skillinfo TQ는 여전히 별도다. 이번 신규 텍스트는 서버 `needtr`이며 기존 NPC EN 표시명 체계를 그대로 사용한다.

**롤백:** 이 작업에서 추가한 shop.txt의 20번 분기/재료 3개, 성장 지원관 응급치료 입구, 함수 메뉴를 함께 되돌린다. 함수 파일을 참조하는 메뉴를 남긴 채 파일만 삭제하지 않는다. 필요하다면 작업 전 스냅샷의 두 로더 구성을 복원하되, 그것은 **이전의 라자냐 전체 복원 방향으로 되돌리는 것**임을 인지해야 한다. 라자냐를 계속 끄려면 원형 주석은 유지하고 소비/교환 연결만 이전 운영본으로 되돌린다. 다른 작업의 hunk·전투 수정·quest DB·캐릭터 데이터를 통째로 되돌리지 않는다. SQL 롤백 없음.

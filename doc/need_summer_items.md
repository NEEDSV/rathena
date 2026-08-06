# NEED 2026 여름 이벤트 아이템 사양

## 적용 범위

여름 이벤트 아이템은 `db/import/need_summer_item_db.yml`에 등록하며
`db/item_db.yml`의 import 체인에서 읽는다. 기존 `db/import/item_db.yml`은
다른 NEED 커스텀 아이템을 계속 담당한다. 399932와 399933은 예비 번호로
남겨 두며 어떠한 아이템 행도 등록하지 않는다.

이번 단계에서는 사냥 드롭, 교환 상점, 낚시, 상자 개봉, 제니·캐시 지급 및
의상 선택 기능을 연결하지 않는다. `feature.need_summer_attendance`도 SQL과
보상 전달 검증 전까지 `off`를 유지한다.

## 서버 아이템 정의

| Id | AegisName | Name | Type | Weight | Flags | Trade | DelayConsume | Script |
|---:|---|---|---|---:|---|---|---|---|
| 399925 | `NEED_Summer_Coconut_Token` | 코코넛 토큰 | Etc | 0 | 없음 | 개인 창고만 허용 | 아니요 | 없음 |
| 399926 | `NEED_Summer_Watermelon_Piece` | 수박 조각 | Etc | 0 | 없음 | 전부 차단 | 아니요 | 없음 |
| 399927 | `NEED_Summer_Golden_Watermelon` | 황금 수박 | DelayConsume | 0 | 없음 | 전부 차단 | 예 | 없음 |
| 399928 | `NEED_Summer_Consumable_Box` | 여름 소모품 상자 | DelayConsume | 0 | 없음 | 전부 차단 | 예 | 없음 |
| 399929 | `NEED_Summer_Zeny_Pouch_100M` | 1억 제니 주머니 | DelayConsume | 0 | 없음 | 전부 차단 | 예 | 없음 |
| 399930 | `NEED_Summer_Cash_Ticket_100K` | 캐시 10만 포인트 교환권 | DelayConsume | 0 | 없음 | 전부 차단 | 예 | 없음 |
| 399931 | `NEED_Summer_Coconut_Shadow_Box` | 코코넛 쉐도우 상자 | DelayConsume | 0 | 없음 | 전부 차단 | 예 | 없음 |
| 399934 | `NEED_Summer_Costume_Part_Ticket` | 의상 부위 변경권 | DelayConsume | 0 | 없음 | 전부 차단 | 예 | 없음 |
| 399935 | `NEED_Summer_Aria_Costume_Box` | 아리아 의상 선택 상자 | DelayConsume | 0 | 없음 | 전부 차단 | 예 | 없음 |

Buy, Sell, 슬롯, 제련, 장착 위치, 기간제 속성은 모두 정의하지 않는다.

## 이동 제한

`NoTrade`는 직접 거래 검사에 사용되며 노점 등록도 같은
`itemdb_cantrade` 검사를 사용한다. `NoCart`는 아이템을 노점용 카트에 넣는
경로까지 차단한다. 따라서 별도의 노점 전용 Trade 필드는 필요하지 않다.
`TradePartner`는 설정하지 않아 배우자 예외도 허용하지 않는다.

| Id | Drop | Trade/노점 | NPC Sell | Cart | 개인 창고 | 길드 창고 | Mail | Auction |
|---:|---|---|---|---|---|---|---|---|
| 399925 | 차단 | 차단 | 차단 | 차단 | 허용 | 차단 | 차단 | 차단 |
| 399926, 399927, 399928, 399929, 399930, 399931, 399934, 399935 | 차단 | 차단 | 차단 | 차단 | 차단 | 차단 | 차단 | 차단 |

`NoMail`은 사용자가 자신의 인벤토리 아이템을 우편 첨부물로 선택하는
`mail_setitem` 경로에서 검사된다. 시스템 우편 생성이나 첨부 수령 경로는
`itemdb_canmail`을 검사하지 않는다. 수령 시에는 `pc_additem(...,
LOG_TYPE_MAIL)`로 인벤토리에 추가되므로 399925와 399928 모두 시스템 우편
첨부 수령이 가능하고 picklog 유형 `E`의 대상이 된다. 수령 후 399925만
개인 창고에 넣을 수 있으며, 두 아이템 모두 사용자 우편 재발송은 불가능하다.

## 사용 미구현 상태와 소모 근거

399927~399931, 399934, 399935는 `DelayConsume`으로 정의하고 Script와
`consumeitem` 호출을 두지 않는다. 아이템 DB 로딩이 끝나면 DelayConsume은
일반 Usable 타입과 `DELAYCONSUME_TEMP` 플래그로 변환된다. `pc_useitem`은
delay-consume 플래그가 있으면 사용 전에 `pc_delitem`을 호출하지 않고
Script만 실행한다. 이 아이템들은 Script가 없고 이후 명시적인 소비 경로도
없으므로 사용 요청, Script 종료 또는 실패만으로 소모되지 않는다.

반대로 일반 Usable 아이템은 Script 실행 전에 `pc_delitem(...,
LOG_TYPE_CONSUME)`을 호출하므로 Script가 없다는 이유만으로 안전하지 않다.
후속 구현에서는 모든 검사를 먼저 통과시킨 다음 마지막 성공 지점에서만
`consumeitem`을 호출해야 한다.

## 후속 사용 사양

### 399927 황금 수박

| 결과 | 확률 |
|---|---:|
| 제니 100,000,000 | 2% |
| 제니 50,000,000 | 12% |
| 제니 10,000,000 | 50% |
| 캐시 100,000 | 1% |
| 캐시 50,000 | 10% |
| 캐시 10,000 | 25% |

합계는 100%이다. 제니 결과가 `MAX_ZENY`를 초과하면 아이템을 소비하지
않는다. 추첨 결과와 제니·캐시 전후값을 이벤트 로그에 기록해야 한다.

### 399928 여름 소모품 상자

- 의문의 알 12610: 99%
- 코코넛 쉐도우 상자 399931: 1%

### 399929 및 399930

- 399929: 제니 100,000,000 지급. `MAX_ZENY` 초과 시 미소모.
- 399930: NEED 서버의 계정 변수 `#CASHPOINTS`에 100,000 지급하고 전후값 기록.

### 399931 코코넛 쉐도우 상자

17429 `Shadow_Box3`은 `IG_SHADOW_BOX3`에서 아래 6종 중 한 개를 같은
확률로 지급한다. 여름 상자의 확정 사양은 한 개 지급이 아니라 50%로 세트를
선택한 뒤 해당 세트 3종을 모두 지급하는 것이므로 기존 그룹을 그대로
호출하면 안 된다.

| 세트 | Id | AegisName | Name |
|---|---:|---|---|
| 스피리츄얼 | 24078 | `S_Spiritual_Weapon` | 스피리츄얼 웨폰 쉐도우 |
| 스피리츄얼 | 24079 | `S_Spiritual_Earring` | 스피리츄얼 이어링 쉐도우 |
| 스피리츄얼 | 24080 | `S_Spiritual_Pendent` | 스피리츄얼 펜던트 쉐도우 |
| 매리셔스 | 24083 | `S_Malicious_Shield` | 매리셔스 쉴드 쉐도우 |
| 매리셔스 | 24081 | `S_Malicious_Armor` | 매리셔스 아머 쉐도우 |
| 매리셔스 | 24082 | `S_Malicious_Shoes` | 매리셔스 슈즈 쉐도우 |

### 399934 및 399935

399934의 부위 변경 기능은 후속 작업으로 남긴다. 399935는 클라이언트 선택
UI와 서버 허용 목록이 모두 필요하다. 현재 허용 목록은 비어 있으며, 후속
구현도 목록이 비어 있거나 요청 ID가 목록에 없으면 아이템을 소비하지 않고
fail-closed 해야 한다. 추측한 아리아 의상 ID는 등록하지 않는다.

## 클라이언트 Lua 전달표

모든 행에 MAIN 20250604용 itemInfo 항목, 인벤토리 아이콘 BMP 및 collection
BMP가 필요하다. 실제 리소스 이름은 클라이언트 작업자가 Lua의
`identifiedResourceName`과 일치하도록 정한다.

| Item ID | AegisName | 한글 표시명 | 유형 | 현재 사용 | 대표 설명 | 이동 제한 요약 | 아이콘/컬렉션 | 출석 UI 대표 |
|---:|---|---|---|---|---|---|---|---|
| 399925 | `NEED_Summer_Coconut_Token` | 코코넛 토큰 | Etc | 불가 | 여름 이벤트 교환 토큰 | 개인 창고만 허용 | 둘 다 필요 | `x10` 표시 |
| 399926 | `NEED_Summer_Watermelon_Piece` | 수박 조각 | Etc | 불가 | 여름 이벤트 재료 | 모든 이동 차단 | 둘 다 필요 | 아니요 |
| 399927 | `NEED_Summer_Golden_Watermelon` | 황금 수박 | DelayConsume | 비활성 | 제니·캐시 추첨 예정 | 모든 이동 차단 | 둘 다 필요 | 아니요 |
| 399928 | `NEED_Summer_Consumable_Box` | 여름 소모품 상자 | DelayConsume | 비활성 | 의문의 알 또는 쉐도우 상자 예정 | 모든 이동 차단 | 둘 다 필요 | 아니요 |
| 399929 | `NEED_Summer_Zeny_Pouch_100M` | 1억 제니 주머니 | DelayConsume | 비활성 | 1억 제니 지급 예정 | 모든 이동 차단 | 둘 다 필요 | 아니요 |
| 399930 | `NEED_Summer_Cash_Ticket_100K` | 캐시 10만 포인트 교환권 | DelayConsume | 비활성 | 캐시 10만 지급 예정 | 모든 이동 차단 | 둘 다 필요 | 아니요 |
| 399931 | `NEED_Summer_Coconut_Shadow_Box` | 코코넛 쉐도우 상자 | DelayConsume | 비활성 | 50% 확률로 3종 세트 지급 예정 | 모든 이동 차단 | 둘 다 필요 | 아니요 |
| 399934 | `NEED_Summer_Costume_Part_Ticket` | 의상 부위 변경권 | DelayConsume | 비활성 | 의상 부위 변경 예정 | 모든 이동 차단 | 둘 다 필요 | 아니요 |
| 399935 | `NEED_Summer_Aria_Costume_Box` | 아리아 의상 선택 상자 | DelayConsume | 비활성 | 허용 목록 기반 의상 선택 예정 | 모든 이동 차단 | 둘 다 필요 | 아니요 |

출석 UI 대표 표시는 399925 코코넛 토큰 10개만 사용한다. 서버의 실제 출석
우편에는 399925 10개와 399928 1개가 함께 첨부된다.

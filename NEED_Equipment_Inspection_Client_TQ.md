# NEED 장비 열람 클라이언트 수동 작업 TQ

- 작성일: 2026-11-18 (Asia/Seoul)
- 연결 보고서: `NEED_Equipment_Inspection_Implementation_Report.md`
- 이번 작업에서는 클라이언트 파일 수정·GRF 생성·본서버 배포를 수행하지 않았다.

## 1. 선택한 방식과 필요한 작업

서버의 기존 `0x02d6` 요청 / `0x0b37` 응답을 그대로 사용한다. OFF 응답은 서버에서 기본 아이템만 직렬화하므로, 제련·카드·인챈트·랜덤옵션을 보호하기 위한 클라이언트 패킷/실행 파일 수정은 현재 구현에 필요하지 않다. 패킷에 별도 “상세 비공개” 비트는 추가하지 않았다.

클라이언트에서 필요한 작업은 **기존 장비 공개 체크박스와 상태 안내의 의미를 상세 공개로 바꾸는 문구 수정 및 실제 표시 검수**다. 원본 아이템 상세 정보를 수신한 뒤 UI에서 숨기는 방식으로 바꾸지 않는다.

현재 서버 소스는 `src/custom/defines_pre.hpp`의 `PACKETVER 20250604`, MAIN 계열이다. 요청서의 2025-07-16은 확인되지 않았다. 이 TQ 때문에 패킷 버전이나 실행 파일을 다른 날짜로 교체하지 않는다.

## 2. 확인한 파일과 로딩 경로

| 구분 | 실제 확인 위치 |
|---|---|
| KR 클라이언트 | `F:\need\need_client\NEED\NEEDClient.exe` |
| EN 테스트 클라이언트 | `F:\need\need_client\NEED_EN_TEST\NEEDClient.exe` |
| 리소스 CSV 후보 A | `F:\need\need_client\NEEDResource_END\msgstringtable.csv` |
| 리소스 CSV 후보 B | `F:\need\need_client\NEEDResource\data\msgstringtable.csv` |
| EN CSV 제작 원본 | `F:\need\need_client\NEEDResource\en_build\msgstringtable\msgstringtable_en.csv` |
| 클라이언트 내부 경로 | `data\msgstringtable.csv` |

CSV 후보 A/B 모두 아래 대상 키와 기존 영문 값이 존재함을 확인했다. **폴더 이름만 보고 KR/EN 운영 원본이라고 확정하지 않는다.** 실제 테스트용 GRF에서 추출한 동일 경로 파일과 대조한 뒤, 해당 언어의 제작 원본에 적용한다.

- KR `DATA.ini`: `0=need_data.grf`, `1=data.grf`.
- EN `DATA_EN.INI`: `0=need_english.grf`, `1=need_data.grf`, `2=data.grf`.
- EN 폴더의 일반 `DATA.ini`는 KR 순서와 같다. 테스트 실행 시 실제 선택한 INI를 확인한다.
- 확인한 두 EXE에 `msgstringtable` 및 아래 `MSI_*` 키 문자열이 존재한다. 예: `MSI_OPEN_EQUIPED_ITEM` 파일 오프셋 `12818316` (0부터). 이는 읽기 조사 증거이며, EXE 바이트 수정 위치가 아니다.
- 별도 `data\luafiles514\lua files\msgstring_kr.lub`는 Lua 5.1 `MsgStrID` 테이블이다. 조사한 파일의 463개 항목에는 아래 공개 체크박스 키가 없었다. `System\MsgString.lub`나 이 LUB를 추정으로 수정하지 않는다.

## 3. 수정할 키와 문구

CSV는 **1열 키 / 2열 값 모두 Base64**다. 행 번호나 패킷의 숫자 메시지 ID로 찾지 말고, 1열을 Base64 해제해 키로 찾는다. 값은 기존 파일과 같은 UTF-8 문자열을 Base64로 인코딩한다.

| 키 | 확인한 기존 값 | KR 변경값 | EN 변경값 |
|---|---|---|---|
| `MSI_OPEN_EQUIPED_ITEM` | `Show Equip` | `상세 정보 공개` | `Show Details` |
| `MSI_VIEW_EQUIP_MSG` | `Equipment On/Off` | `장비 상세 정보 공개 ON/OFF` | `Equipment Details On/Off` |
| `MSI_OPEN_EQUIPEDITEM_REFUSE` | `Equipment information not open to the public.` | `상세 정보 비공개 (장비 종류는 표시)` | `Hide Details (Equipped Items Visible)` |
| `MSI_OPEN_EQUIPEDITEM_ACCEPT` | `Equipment information open to the public.` | `상세 정보 공개 (카드/제련/인챈트 포함)` | `Show Details (Including Cards/Refine/Enchants)` |

`MSI_OPEN_EQUIPEDITEM_REFUSED`는 기존 열람 거절 문구다. 새 서버의 일반 열람 경로에서는 더 이상 전송하지 않으므로 변경하지 않아도 된다. 숫자 ID 1357을 서버 메시지 1557과 혼동하지 않는다.

체크박스 공간에는 짧은 라벨을 사용하고, ON/OFF의 정확한 의미는 상태 메시지에 둔다. 서버에도 KR/EN 메시지 1557~1559를 추가하여 토글 시 정책을 안내하고 OFF 열람 시 “빈 카드 칸이 실제 장착 상태를 뜻하지 않는다”는 설명을 출력한다.

## 4. 수동 적용 순서

1. 사용하는 KR/EN 테스트 GRF와 INI 및 위 CSV 원본을 백업한다.
2. GRF에서 실제 `data\msgstringtable.csv`를 추출해 해당 키의 존재와 기존 값을 확인한다. CSV를 일반 텍스트 문구 검색으로 치환하지 않는다.
3. 각 언어 제작 원본에서 위 네 키의 값만 변경한다. 행 수·키 순서·대상 외 값·Base64 문법을 보존한다. 같은 키가 중복되면 먼저 실제 사용 행을 확인한다.
4. EN 제작 파이프라인을 사용할 경우 기존 검증 도구 `en_build\msgstringtable\verify_msgstringtable_english.py`와 승인/stamp 절차에 새 문구를 반영한다. 검증용 해시를 수동으로 위조하지 않는다.
5. 별도 테스트용 패치 GRF에 `data\msgstringtable.csv`로 넣고, 테스트 클라이언트의 실제 로딩 순서 최상단에서 읽게 한다. 운영 GRF를 바로 덮어쓰지 않는다.
6. 서버 변경을 적용한 **테스트 서버**에 KR/EN으로 각각 접속하여 아래 검수를 한다. 클라이언트를 완전히 종료했다가 다시 실행하여 리소스 캐시를 갱신한다.

## 5. 짧은 클라이언트 검수 / 합격 기준

- A OFF / B 열람: 일반 10개 부위, 의상 4개 부위, 쉐도우 6개 부위에서 장착된 기본 아이템 이름과 기본 설명이 보이고 빈 부위는 비어 있다. 다중 부위 장비도 확인한다.
- A의 +제련, 카드 접두·접미어/아이콘, 의상 스톤 인챈트, 쉐도우 옵션, 랜덤옵션, 등급, 제작자 이름이 B의 창/툴팁에 나타나지 않는다. 기본 DB 슬롯 수 `[1]` 등은 허용한다.
- OFF의 0/빈 슬롯을 실제 무제련·미장착 카드라는 단정으로 표시하지 않는지 확인한다. 서버 안내도 확인한다. `+0`, 가짜 옵션 문구 또는 예전 ON 툴팁이 나타나면 합격 처리하지 않는다.
- A ON / B 재열람: 기존 상세 정보가 복구된다. A OFF / A 본인 인벤토리·장비 창의 상세 정보는 유지된다.
- ON → OFF → ON마다 열람 창을 닫고 다시 열어 확인한다. 재장착·맵 이동·재접속 후에도 확인한다. 서버는 요청마다 새 패킷을 만들지만 이미 열린 상대 창을 강제로 갱신하는 새 프로토콜은 추가하지 않았다.
- 공개 장비 빌드 NPC 창에서도 OFF의 상세가 숨겨지고, 소유자 본인에게는 보이는지 확인한다. 다른 맵서버에 접속한 소유자는 SQL 설정 지연을 피하기 위해 ON이어도 상대에게 기본 장비만 보일 수 있다.
- 패킷 캡처로 OFF의 카드 4칸, 랜덤옵션 5개, 제련/등급 필드가 0인지 확인한다. **실제 상세 수신 후 UI에서만 숨기는 결과는 불합격**이다.

표시 문제로 EXE 변경이 필요하다고 실제 확인되면, 해당 창의 툴팁 캐시/0값 렌더링 처리만 별도 개발한다. 현재는 그 문제가 재현되지 않아 임의의 실행 파일 패치나 오프셋은 제시하지 않는다. 현 단계에서는 소스·패킷 검증 완료이며 실제 UI 합격 판정은 보류다.

롤백은 테스트 패치 GRF를 로딩 순서에서 제거하거나 백업 CSV로 복구하고 클라이언트를 재실행한다. 서버 개인정보 필터는 클라이언트 문구 패치와 독립적으로 동작한다.

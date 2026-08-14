# NEED 2026 여름 이벤트 아이템 로그 정책

## 강제 기록 범위

`src/map/log.cpp`의 `should_log_item`은 399925부터 399935까지를 기존
`log_filter`보다 먼저 허용한다. 현재 등록되지 않은 399932와 399933도 향후
같은 시즌 용도로 사용할 경우 자동으로 강제 기록 대상이 된다.

이 변경은 전역 `log_filter`를 1로 바꾸지 않는다. 따라서 다른 Etc, Usable,
Cash 아이템의 로그량은 증가하지 않는다. `enable_logs`의 유형별 활성화는
계속 존중하며, 현재 `conf/log_athena.conf`는 `0xFFFFFFFF`로 모든 표준
picklog 유형을 활성화하고 있다.

## 표준 경로별 기록

| 작업 | LOG_TYPE | picklog 문자 | 기록 경로 |
|---|---|---|---|
| 바닥 드롭·획득 | `LOG_TYPE_PICKDROP_PLAYER` | P | 인벤토리 삭제·추가 |
| 몬스터 드롭·획득 | `LOG_TYPE_PICKDROP_MONSTER` / `LOG_TYPE_LOOT` | M / L | 몬스터 및 획득 처리 |
| 아이템 소비 | `LOG_TYPE_CONSUME` | C | `pc_delitem` |
| NPC·스크립트 지급/삭제 | `LOG_TYPE_SCRIPT` | N | `getitem`, `delitem` 계열 |
| GM 지급·삭제 | `LOG_TYPE_COMMAND` | A | `@item`, `@item2`, 삭제 명령 계열 |
| 개인 창고 입출고 | `LOG_TYPE_STORAGE` | R | 저장소와 인벤토리 사이 이동 |
| 길드 창고 입출고 | `LOG_TYPE_GSTORAGE` | G | 길드 저장소 이동 |
| 거래 | `LOG_TYPE_TRADE` | T | 거래 커밋 |
| 노점 | `LOG_TYPE_VENDING` | V | 노점 거래 |
| 우편 발송·수령 | `LOG_TYPE_MAIL` | E | 첨부 제거 및 `pc_additem` 수령 |
| 경매 | `LOG_TYPE_AUCTION` | I | 경매 이동 |

현재 이벤트 아이템 정책상 거래·노점·길드 창고·사용자 우편·경매는 차단된다.
399925의 개인 창고 입출고와 시스템 우편 수령은 각각 R, E 유형으로 기록된다.
399928은 시스템 우편 수령만 E 유형으로 기록되고 개인 창고 이동은 거절된다.

인벤토리와 카트 사이 이동은 rAthena 표준 구현에서 원래 picklog를 남기지
않는다. 이번 아이템은 모두 `NoCart`이므로 정상 사용자가 해당 경로에 진입할
수 없다. 추후 카트 이동을 허용하는 이벤트 아이템이 생기면 별도 로그 유형과
호출 지점을 설계해야 한다.

## 정적 검증 기준

- `should_log_item(399925..399935, 수량, 제련)`은 수량·가격·드롭률과
  무관하게 `true`가 된다.
- 399924와 399936은 기존 `log_filter` 판정을 그대로 사용한다.
- `enable_logs`가 특정 작업 유형을 끈 경우에는 해당 작업을 기록하지 않는다.
- 시스템 우편 첨부 수령은 `mail_getattachment`에서
  `pc_additem(..., LOG_TYPE_MAIL)`을 호출한다.

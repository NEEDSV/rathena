# 01. 변경 상세

전체 diff는 `evidence/implementation.diff`에 있다(+268 / −63, 파일 2개).
- `need_lucky_ceiling.txt`는 CP949·CRLF 파일이다. 편집 도구가 아니라 `tools/apply_ceiling_patch.py`(디코드 → 고정 지점 치환 → CP949 재인코딩)로 수정했다. 변경 후에도 CP949·CRLF가 그대로임을 확인했다.
- 변경 후 SHA-256: `need_lucky_ceiling.txt` `570e9026e4be…`, `need_barters.yml` `5e816011c689…`
- 라인 번호는 **변경 후 파일** 기준이다.

## 1. `npc/NEED/need_barters.yml` (UTF-8, LF 유지)

| 라인 | 변경 |
|---|---|
| 11-14 | 주석: "2026-10-21 운영자 확정: 200P + 0 Zeny", `Zeny: 0`을 명시하는 이유 |
| 17, 24 | `Zeny: 10000000` → **`Zeny: 0`** |
| 21, 28 | `Amount: 1000` → **`Amount: 200`** |

- **키를 지우지 않고 `Zeny: 0`으로 둔 이유:** `barter_parseBodyNode`(`src/map/npc.cpp:654-671`)는 `Zeny` 키가 없으면 **이미 로드된 항목의 기존 가격을 유지**한다. 키를 지우면 `@reloadnpc` 등 재적재 때 1천만이 남는다. `Zeny: 0`은 `asUInt32` 정상값이고, 결제는 `pc_payzeny(0)`이 성공을 반환한다(`pc.cpp:5890-5904`). 전장 증표 바터가 이미 같은 0제니 경로를 운영 중이다.
- 아이템(Guarantee_Weapon_7Up 6230 / Guarantee_Armor_7Up 6234)과 기존 지급 조건은 그대로다. 수량 제한은 새로 만들지 않았다.
- 같은 상점의 골드 코인(200P + 1천만)과 전장 증표 비율(50/500/500)은 그대로다(정적 검사).

## 2. `npc/NEED/need_lucky_ceiling.txt`

### 2.1 가격 (기존 상품 3곳만)

| 배열 | 상품 | 이전 | 이후 |
|---|---|---|---|
| `.Zeny` | PID 100 대축 | 100000000 | **10000000** |
| `.Point` | PID 102 +9 | 5000 | **2000** |
| `.Zeny` | PID 102 +9 | 500000000 | **100000000** |

- 정적 검사 결과: 기존 131개 상품 중 바뀐 필드는 **정확히 이 3개뿐**이다(`static_check.py` "only PID 100 Zeny / 102 Point+Zeny changed").
- 재고(100 주 100 / 102 월 1), 주기, 귀속(Bound 0), 지급 아이템은 그대로다.

### 2.2 신규 상품 2종 (17개 상품 배열 끝에 각 2원소 추가)

| 필드 | PID 103 | PID 104 |
|---|---|---|
| Code$ | BS_TO_REFINE7 | BS_TO_REFINE9 |
| Name$ / Name_en$ | 대축 1개 → +7 보장권 / Blessing x1 -> +7 Certificate | 대축 15개 → +9 보장권 / Blessing x15 -> +9 Certificate |
| Cat | 2 (공용 희귀) | 2 |
| Point / Zeny | 0 / 0 | 0 / 0 |
| EggId / EggAmt (범용 비용 칸) | 6635 / **1** | 6635 / **15** |
| Choice$ | "6230,6234" (무기/방어구 택1) | "6228,6232" |
| RewardAmt | 1 | 1 |
| Stock / Cycle | 0 / 0 (제한 없음) | 1 / 2 (월간) — **102와 같은 값, 재고는 102 행 공유** |
| Bound | 0 (보장권 자체가 NoTrade, 기존 성질 유지) | 0 |

- **PID 선택:** 공용 범위의 미사용 번호 103·104다.
  - 시즌 PID(현재 최대 328)를 매기는 행알 출시 도구(로컬 `tools/need_lucky_egg_ceiling_sync.py`)는 `max(스크립트 최대 PID, last_assigned 328, …) + 1`로 번호를 정하므로 충돌하지 않는다.
  - 201은 사용하지 않았다.
- **배열 형식:** 기존 줄은 마지막 줄의 `;` → `,` 한 글자만 바뀐다. 추가 줄은 도구 파서(`ENTRY_RE`)와 같은 형식이다.
- **범용 비용 칸 재사용:** `S_Buy`의 `.EggId/.EggAmt` 검사와 차감(`countitem`/`delitem`)은 아이템 종류를 가리지 않는다. 그래서 대축 지불에 그대로 쓴다.
  - `countitem`은 인벤토리만 센다. 창고 재고는 합산되지 않는다.
  - 상세·부족 메시지는 `mesitemlink`/`getitemname`으로 대축 이름을 표시한다.
- **선택형 지급:** 기존 `.Choice$` 흐름(파츠 선택 → 최종 확인 → `S_Buy`가 선택 인덱스 재검증)을 그대로 쓴다. 지급은 선택한 1장만이다(`RewardAmt` 1).

### 2.3 공유 재고 (102 + 104 = 서버 월 1개)

| 요소 | 위치(라벨) | 동작 |
|---|---|---|
| 매핑 | OnInit, 상품 배열 구간 **밖** | `setarray .StockShareSrc[0], 104;` / `setarray .StockShareKey[0], 102;` (행알 출시 도구가 상품 배열 구간 안의 미등록 setarray를 거부하므로 구간 밖에 둠) |
| `S_CheckShare` | OnInit | 원본 존재, 자기 공유 금지, `.Cycle`·`.Stock` 일치(0 주기 금지)를 검사. 실패하면 `.StockShareOK=0` → 104 판매 거부 |
| `S_SKey` | 신규 | 상품 → 재고 키. 104 → 102, 설정 오류 시 0 |
| `S_StockTag`, `S_Detail`, `S_Buy`, `S_Normalize`, `S_SaveStock`, `S_AdminItem` | 기존 | 재고 참조(`$NeedLPStock/$NeedLPCyc`)를 상품 PID 대신 재고 키로 바꿈. 판매 모드(`$NeedLPMode`)는 상품별 그대로라 대축 경로만 따로 중지할 수 있다 |
| 결과 | — | 104는 **자기 재고 행을 만들지 않는다.** 격리 실행에서 재고 행은 131개(상품 133 − 무제한 103 − 공유 104) |

### 2.4 재고 차감·리셋을 DB 기준으로 변경

| 변경 전 | 변경 후 | 이유 |
|---|---|---|
| `S_Buy`: 메모리 값 −1을 계산해 **절대값**으로 저장(`S_SaveStock`) | `S_TakeStock`: `UPDATE … SET stock=stock-1 WHERE product_id=키 AND period_key=주기 AND stock>=1`, 이어서 같은 연결에서 `SELECT ROW_COUNT()`. 1이면 성공, 0이면 품절(2)이나 주기 불일치(9) | 맵서버가 여럿이거나 두 경로(102·104)가 동시에 노려도 DB 재고 아래로 팔 수 없다. `query_sql`은 UPDATE의 영향 행 수를 돌려주지 않아(`script.cpp:18858-18860`) ROW_COUNT로 확인한다 |
| `S_Normalize`: 메모리 주기 키가 다르면 기본 재고로 **무조건** 덮어씀 | `INSERT … ON DUPLICATE KEY UPDATE stock=IF(period_key=VALUES(period_key), stock, VALUES(stock)), …, period_key=VALUES(period_key)` 후 재조회 | 다른 맵서버가 이미 새 주기로 리셋하고 판매한 재고를 되살리지 않는다. 같은 맵서버 하나에서는 기존과 결과가 같다 |

- 관리자 "재고 직접 수정·기본 재고로 초기화"는 기존대로 절대값을 저장한다(`S_SaveStock`). 운영자의 명시 조작이라 그대로 뒀고, 104는 102 행을 수정한다.
- 원자 구간 규칙(`next/select/input/sleep` 금지)을 `S_Buy`·`S_TakeStock`에서 정적으로 확인했다.

### 2.5 구매 흐름과 실패 경로

```
S_Buy (next/select 없음, 한 맵서버 안에서 원자적)
 ├ 판매중? ─아니오→ 1 (차감 없음)
 ├ 재고상품 & SQL 불가 → 8 (차감 없음)
 ├ S_Normalize(키) ; 재고 키 0? → 7 (차감 없음)
 ├ 선택 인덱스 검증 → 7
 ├ 메모리 재고 ≤0 → 2 (차감 없음)
 ├ 포인트 / 대축(EggId) / Zeny 부족 → 3 / 4 / 5 (차감 없음)
 ├ checkweight 실패 → 6 (차감 없음)
 ├ S_TakeStock: DB 조건부 −1
 │    ├ rc=0, 같은 주기 → 2 (차감 없음)
 │    └ 오류/주기 불일치 → 9 (차감 없음)
 ├ delitem 포인트 / 대축, Zeny 차감
 ├ getitem 선택 보장권 ×1
 └ logmes [LuckyCeil] … (npclog)
```

- **남는 한계(기존 구조와 동일):** DB 재고 차감과 인벤토리 지급 사이에 맵서버가 죽으면 **"재고는 줄고 아이템은 미지급"**이 될 수 있다(중복 지급 쪽으로는 기울지 않음).
  - 복구 근거: `TAKE` 이후 `logmes`가 없는 경우를 npclog와 재고로 대조한다(`03` 문서).
  - DB와 게임 인벤토리 간 완전 원자성은 주장하지 않는다.

### 2.6 문구 (KO/EN, `needtr`)

- 상세·최종 확인: 포인트 또는 Zeny가 0이면 그 줄을 숨긴다. 기존 상품은 모두 0이 아니라 표시가 바뀌지 않는다.
- 102·104 상세·최종 확인: "※ 서버 전체 공유 재고 / 포인트 교환과 대축 교환이 같은 재고를 씁니다 / 어느 쪽이든 먼저 교환하면 나머지는 품절됩니다" (`S_ShareNote`)
- 교환 규칙 2쪽: "포인트·Zeny 또는 대장장이의 축복으로 교환", "+9 보장권은 포인트 교환과 대축 15개 교환을 합쳐 서버 전체 월 1개"
- 관리자 화면: 공유 상품에 "재고 공유 : PID 102" 표시
- 머리말 주석·PID 정책 주석 갱신(현재 사용 100~104 / 시즌 ~328)

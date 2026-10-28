# 룬 타블렛 버프 툴팁 생성기

룬 타블렛 효과가 적용된 캐릭터의 버프창에 세트별 아이콘을 띄우고, 마우스를 올리면 **현재 적용 중인 합산 수치**를 보여 준다.

## 이렇게 만든 이유

NEED 클라이언트는 서버가 보낸 val1~3을 툴팁 문구에 넣지 않는다. 툴팁에서 서식이 적용되는 줄은 남은 시간 줄 하나뿐이다. 근거는 `md/rune_tablet_tooltip_20261019/`의 조사·PoC 보고서에 있다.

그래서 이렇게 한다.

- 룬 상태(세트, 강화 0~15, 조각 0~6)가 만들 수 있는 합산값 조합마다 EFST를 하나씩 둔다. 현재 106개다.
- 각 EFST의 툴팁에는 그 상태의 정확한 합산값을 미리 적어 둔다.
- 서버는 현재 상태에 맞는 EFST 하나만 켠다.

## 원천과 산출물

원천은 `db/rune_db.yml` 하나다. 수치는 모두 세트 스크립트를 평가해서 나온다. `config.yml`에는 EFST 블록, 아이콘, 문구 틀만 있다.

| 산출물 | 위치 |
|---|---|
| 서버 표 | `src/map/rune_display_table.inc` |
| KR/EN 공통 lub | `data/luafiles514/lua files/stateicon/`의 `efstids.lub`, `stateiconimginfo.lub`, `stateiconinfo_f.lub` |
| 툴팁 문구 | `stateiconinfo.lub` (KR/EN 각각) |
| 아이콘 | `data/texture/effect/need_rune_<세트>.tga` (룬 UI `rune_icon` bmp → 32×32) |

lub은 표시된 블록 하나만 바뀐다. 다시 생성하면 그 블록만 교체된다.

## 사용법

```
python tools/need_rune_tooltip/generate.py --kr ../NEEDResoruce --en ../NEEDResoruce_EN --out <폴더>   # 미리보기 (states.md 포함)
python tools/need_rune_tooltip/generate.py --kr ../NEEDResoruce --en ../NEEDResoruce_EN --install     # 저장소에 쓰기
python tools/need_rune_tooltip/generate.py --kr ../NEEDResoruce --en ../NEEDResoruce_EN --check       # 드리프트 검사
python tools/need_rune_tooltip/verify_client.py --kr ../NEEDResoruce --en ../NEEDResoruce_EN          # Lua 실행 검증 (lupa)
```

## rune_db.yml을 고친 뒤 할 일

1. `--install`로 다시 생성하고 map-server를 다시 빌드한다.
2. `verify_client.py`를 실행한다.
3. EN 저장소에서 `en_build/rune_tooltip/verify_rune_tooltip_stateiconinfo.py --write`를 실행한다. 새 PASS 기록이 생기고, `build_english.py` 게이트가 이 기록 체인을 따라간다.
4. 게임에서 GM이 `@runetooltipcheck`를 실행한다. 모든 상태를 실제 `status_calc_pc`에 통과시켜 비교하므로, 테스트 캐릭터에서만 쓴다.

생성기를 다시 돌리지 않으면 서버가 로드할 때 스크립트 해시가 다르다. 그 세트는 효과는 그대로 적용되지만 아이콘은 숨겨지고 경고가 남는다.

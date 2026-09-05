# NEED Wiki Bootstrap 인증 전환 작업 보고서

작성일: 2026-09-05  
적용 브랜치: `need-server`  
대상: `rathena` map/web-server 및 `NeedWikiDll`

## 1. 결론

기존 DLL의 `recv`/`send` IAT hook과 로그인 패킷 파싱 기반 `WikiSessionCapture`를 제거하고, 게임에서 직접 입력하는 1회용 `@wikicode`로 현재 `map_session_data`를 바인딩하는 구조로 변경했다.

DLL은 account ID, character ID, 공식 web token을 더 이상 수집하거나 전송하지 않는다. 각 클라이언트 프로세스가 독립적인 256-bit Wiki bearer token을 만들며, 서버는 그 원문이 아닌 SHA-256 hash만 저장한다. 동일 IP는 bootstrap 발급 남용 제한에만 사용하며 캐릭터 선택이나 action 대상 결정에는 사용하지 않는다.

```text
NeedWiki DLL (프로세스별 256-bit token)
  -> POST /api/wiki/bootstrap/start
web-server (token_hash + 8자리 1회용 code_hash 저장)
  -> 사용자에게 @wikicode 12345678 안내
map-server (@wikicode를 실행한 sd 직접 사용)
  -> token_hash + AID + CID + session_generation 바인딩
DLL
  -> Authorization: Bearer <token>으로 showitem/showgroup/navi 호출
web-server
  -> token_hash만 loopback map-server로 전달
map-server
  -> 현재 live sd와 AID/CID/generation을 모두 검증한 뒤 그 sd에만 실행
```

## 2. 기존 구조와 원인

기존 DLL은 게임 실행 파일의 Winsock import를 찾아 `recv`/`send`를 IAT hook하고, 특정 클라이언트 패킷 ID/offset에서 AID/CID/공식 web token을 추출했다. 실클라이언트에서는 callback 자체가 발생하지 않아 세 값이 전부 누락됐으며, 클라이언트 버전과 네트워크 구현에 강하게 결합된 방식이었다.

기존 web API는 DLL이 전달한 AID/CID와 IP를 이용했다. 동일 PC의 다중 클라이언트 및 `@접속유지` 세션에서는 동일 IP가 여러 `map_session_data`에 대응하므로 올바른 클라이언트를 안정적으로 특정할 수 없었다.

## 3. 변경된 Bootstrap 흐름

1. DLL 로드 후 작업 스레드에서 Windows CNG `BCryptGenRandom`으로 32-byte token을 생성한다.
2. ITEM, ITEMGROUP, NAVI 최초 클릭 시 `/api/wiki/auth`를 확인한다.
3. `NOT_BOUND` 또는 `EXPIRED`이면 `POST /api/wiki/bootstrap/start`를 호출한다.
4. web-server는 8자리 code를 생성하고 token/code 각각의 SHA-256 hash만 DB에 기록한다. code TTL은 120초다.
5. DLL은 `@wikicode <code>`를 안내하고 명령 전체를 클립보드에 복사한다. 자동 polling은 추가하지 않아 Wiki 서버 부하를 늘리지 않았다.
6. 사용자가 실제 캐릭터 채팅창에서 명령을 실행하면 map-server는 그 명령을 실행한 `sd`를 직접 바인딩한다.
7. 다음 게임 연동 클릭부터 bearer token hash에 바인딩된 live `sd`만 찾아 action을 실행한다.

## 4. 저장 위치와 lifetime

| 값 | 위치 | 원문 저장 | lifetime |
|---|---|---:|---|
| Wiki bearer token | DLL 프로세스 메모리 | 예 | DLL 프로세스 lifetime, unload 시 zero/clear |
| `token_hash` | `needwiki_sessions` | 아니오, SHA-256만 | 최대 8시간, 로그아웃/세션 변경 시 즉시 revoke |
| Bootstrap code | DLL 안내창/HTTP 응답 메모리 | DB에는 저장 안 함 | 120초 |
| `code_hash` | `needwiki_sessions` | 아니오, SHA-256만 | 1회 사용 또는 만료 이후 재사용 불가 |
| `session_generation` | live `map_session_data` 및 binding row | 해당 없음 | map 인증 세대마다 새 값 |

DB 상태는 `WAITING(0)`, `READY(1)`, `REVOKED(2)`, `EXPIRED(3)`으로 관리한다. 신규 테이블은 `sql-files/upgrades/upgrade_20260905.sql`에 정의했다.

## 5. session_generation과 revoke

- `pc_authok`마다 새 64-bit generation을 만들고 같은 AID/CID의 과거 READY binding을 revoke한다.
- `chrif_save(..., CSAVE_QUITTING)`에서 현재 generation binding을 revoke하고 메모리 값을 0으로 만든다.
- 위 경로는 로그아웃, 캐릭터 선택 화면 이동, `@접속유지` 전환, 실제 map-server 프로세스 이동을 포함한다.
- map-server 비정상 재시작으로 DB row가 남아도 `/api/wiki/auth`가 map-server에 live status를 확인한다. sd 부재 또는 generation 불일치 시 fallback하지 않고 row를 revoke한다.
- 동일 프로세스 내부의 일반 맵 이동은 같은 live sd/generation을 유지한다.

## 6. @wikicode 처리와 action 검증

`@wikicode <code>`는 콘솔, NPC script, autotrade 상태에서 호출할 수 없도록 등록했다. code는 숫자 6~8자리만 허용하고 DB transaction 및 `SELECT ... FOR UPDATE`로 동시 사용을 직렬화한다. 실패는 접속 generation별 1분에 5회로 제한한다.

모든 action 직전에 다음 조건을 다시 검사한다.

- binding status가 READY이고 TTL이 남아 있음
- `sd != nullptr`
- `sd->state.pc_loaded`
- `!sd->state.autotrade`
- `sd->fd > 0` 및 socket active
- binding과 live sd의 account ID 일치
- binding과 live sd의 character ID 일치
- binding과 live sd의 `session_generation` 일치

어느 하나라도 실패하면 동일 IP나 다른 세션을 탐색하지 않고 `NOT_BOUND` 또는 `EXPIRED`로 종료한다.

## 7. 적용 API와 기존 Wiki 영향

인증 적용:

- `GET /api/wiki/showitem`
- `GET /api/wiki/showgroup`
- `GET /api/wiki/navi`
- 기존 map-server 전달형 `/api/wiki/test`

인증/상태:

- `POST /api/wiki/bootstrap/start`
- `GET /api/wiki/auth` (`READY`, `WAITING`, `NOT_BOUND`, `EXPIRED`만 사용하며 ID를 반환하지 않음)

일반 Wiki 문서, 검색, 이미지, item group 목록 조회에는 인증을 추가하지 않았다. 백그라운드 polling도 추가하지 않았으므로 일반 Wiki 열람 속도와 서버 요청량에는 이번 인증 변경이 개입하지 않는다.

## 8. 보안 조치

- DLL token: CSPRNG 256-bit, 프로세스별 독립 생성
- bearer token은 URL query에 넣지 않고 `Authorization` header로만 전달
- web/map 저장소와 내부 loopback 프로토콜에는 SHA-256 token hash만 전달
- AID/CID는 DLL/API 입력으로 받지 않고 `@wikicode`를 실행한 live sd에서만 획득
- bootstrap code: 8자리, 120초, unique index, transaction 기반 1회 사용, 입력 실패 제한
- bootstrap 발급: 동일 IP 1분 10회 제한. IP는 이 DoS 보조 제한 외에는 사용하지 않음
- map-server Wiki socket은 `127.0.0.1` 연결만 허용
- 기존 `auth.cpp` token 오류 로그를 `[redacted]`로 변경
- 공통 web request logger의 `Authorization`, 구형 `X-NeedWiki-Token`, bootstrap 응답 body를 마스킹
- token/code 원문을 별도 DLL 파일 로그에 남기지 않음

## 9. HTTPS 및 배포 절차

운영 bearer 인증은 HTTPS endpoint가 준비되어야 활성화된다. DLL 기본값은 평문 HTTP 인증을 차단한다.

1. map DB에 `sql-files/upgrades/upgrade_20260905.sql`을 적용한다.
2. 새 `map-server.exe`, `web-server.exe`, `NeedWikiDll.dll`을 함께 배포한다.
3. web-server 앞에 TLS reverse proxy 또는 HTTPS endpoint를 구성한다.
4. DLL 실행 폴더의 `NeedWikiDll.json`을 다음처럼 설정한다.

```json
{
  "wiki_base_url": "http://151.145.74.228/patch/wiki/",
  "wiki_api_base_url": "https://wiki-api.example.com/api/wiki/",
  "allow_insecure_wiki_auth": false
}
```

기존 JSON 파일은 자동 덮어쓰지 않으므로 이미 배포된 파일에는 `wiki_api_base_url`을 직접 추가해야 한다. 로컬 폐쇄 개발환경에서만 임시로 `allow_insecure_wiki_auth: true`를 사용할 수 있으며 운영 배포에서는 사용하면 안 된다.

배포 순서는 DB migration -> map/web-server -> HTTPS endpoint -> DLL/config 순서를 권장한다.

## 10. 추가/수정 파일

### rAthena

- 추가: `src/common/needwiki_crypto.hpp`
- 추가: `sql-files/upgrades/upgrade_20260905.sql`
- 추가: `tests/needwiki_crypto_tests.cpp`, `tests/needwiki_crypto_tests.vcxproj`
- 수정: `sql-files/main.sql`
- 수정: `src/web/needwiki_controller.cpp/.hpp`, `src/web/web.cpp`, `src/web/auth.cpp`
- 수정: `src/map/needwiki.cpp/.hpp`, `src/map/atcommand.cpp`, `src/map/pc.cpp/.hpp`, `src/map/chrif.cpp`
- 수정: `conf/groups.yml`

### NeedWikiDll

- 추가: `NeedWikiDll/WikiSession.cpp/.h`
- 추가: `tests/WikiSessionTests.cpp/.vcxproj`
- 수정: `NeedWikiDll/WikiView.cpp`, `HttpClient.cpp/.h`, `NeedWikiConfig.cpp/.h`, `dllmain.cpp`
- 수정: DLL/Viewer 프로젝트 및 filter 파일
- 제거: `WikiSessionCapture.cpp/.h`
- 제거: 구 패킷 캡처 테스트 프로젝트/소스
- 제거: 이전 임시 `WikiDebugLog` 코드 및 프로젝트 참조

사용자가 보유한 로컬 DB 접속 설정인 `conf/import/inter_conf.txt`와 콘솔 설정인 `conf/map_athena.conf`는 이번 작업에서 수정하지 않았다.

## 11. 자동 검증 결과

| 검증 | 결과 |
|---|---|
| `NeedWikiDll` Release/Win32 빌드 | PASS (`v143` override 사용, 오류 0) |
| `NeedWikiViewer` Release/Win32 빌드 | PASS |
| `map-server` Release/x64 빌드 | PASS (오류/경고 0) |
| `web-server` Release/x64 빌드 | PASS (오류/경고 0) |
| DLL CSPRNG token 형식/프로세스 내 유지/shutdown 폐기/재생성 | PASS |
| SHA-256 빈 문자열 및 `abc` 표준 벡터/hex 형식 | PASS |
| 패킷 hook/capture 참조 정적 검색 | PASS (잔여 참조 없음) |
| API query의 account ID/character ID 정적 검색 | PASS (잔여 전달 없음) |
| token 로그 출력 경로 정적 검색 | PASS (민감 header/body 마스킹) |

참고: 저장소 전체 solution 빌드는 기존 `map-server-generator`의 `need_storage_shop_*` 미해결 심볼 때문에 실패했다. 이번 대상인 실제 `map-server`와 `web-server` 프로젝트는 의존 라이브러리 재생성 후 각각 정상 빌드됐다.

## 12. 실제 202505 클라이언트 필수 수동 회귀 테스트

DB migration과 HTTPS endpoint가 포함된 배포 환경에서 아래를 수행해야 한다. 이 작업 환경에는 실제 게임 클라이언트 세션이 없어 결과를 임의로 PASS 처리하지 않았다.

| 번호 | 시나리오 | 기대 결과 |
|---:|---|---|
| 1 | 단일 클라이언트에서 최초 ITEM 클릭 -> code 입력 -> 재클릭 | 클릭한 캐릭터 채팅창에만 item link 출력 |
| 2 | 잘못된 code 입력 | 실패 안내, binding 생성 안 됨 |
| 3 | 120초 지난 code 입력 | 만료 안내, binding 생성 안 됨 |
| 4 | 같은 code를 두 캐릭터에서 순서대로 입력 | 첫 캐릭터만 성공, 두 번째는 이미 사용 안내 |
| 5 | 동일 PC에서 클라 A/B 각각 별도 code 인증 | A/B가 서로 다른 token/AID/CID/generation으로 독립 binding |
| 6 | A/B에서 번갈아 ITEM 클릭 | 매번 클릭한 그 클라이언트 캐릭터에만 출력 |
| 7 | `@접속유지` 노점이 있는 상태에서 본계정 인증/ITEM 클릭 | 본계정에만 출력, 동일 IP 노점은 영향 없음 |
| 8 | 접속유지 캐릭터가 기존 token action 대상이 되는지 확인 | `NOT_BOUND`, 노점 채팅/action 실행 없음 |
| 9 | 인증 후 로그아웃하고 기존 token으로 클릭 | `NOT_BOUND`, 과거 캐릭터에 출력 없음 |
| 10 | 동일 캐릭터 재접속 후 이전 token 사용 | 새 bootstrap 필요, 이전 generation 자동 승계 없음 |
| 11 | 본계정과 접속유지 계정 각각 직접 접속해 Wiki ITEM 클릭 | 현재 실제 접속하여 인증한 캐릭터에만 출력 |
| 12 | showgroup 클릭 | 해당 캐릭터에만 group item link 출력 |
| 13 | NAVI 클릭 | 해당 캐릭터에만 안내 및 navigation 실행 |
| 14 | 일반 Wiki 문서/검색/이미지/목록 탐색 | 인증창 없이 기존 기능 정상, 체감 지연 증가 없음 |
| 15 | map-server 재시작 후 기존 token 사용 | `NOT_BOUND` 또는 일시 `UNAVAILABLE`, 재인증 전 action 없음 |
| 16 | 동일 code 동시 입력 경쟁 | 정확히 한 세션만 성공 |

특히 유저 제보의 필수 조합인 단일 클라이언트, 동일 PC + `@접속유지`, 동일 PC + 2클라이언트, 양 계정 각각 클릭, 두 클라이언트 교대 클릭을 모두 포함한다.

## 13. 배포 전 임시 서버 진단 로그

실서버 smoke test에서 bootstrap 상태 전이만 확인할 수 있도록 map/web-server 공통 진단 매크로를 `src/common/needwiki_diagnostics.hpp`로 분리했다. 현재 기본값은 `NEEDWIKI_DIAGNOSTIC_LOG_LEVEL 2`(Info)다. 확인이 끝나면 이 값만 `1`(Debug) 또는 `0`(Off)으로 변경하면 된다.

기록되는 주요 상태는 다음과 같다.

```text
[NeedWiki] bootstrap start: WAITING
[NeedWiki] bootstrap start: INVALID_TOKEN | RATE_LIMITED | DB_ERROR | ISSUE_FAILED
[NeedWiki] wikicode bind: READY aid=*** cid=*** generation=<number>
[NeedWiki] wikicode bind: INVALID_CODE | EXPIRED | ALREADY_USED | AUTOTRADE
[NeedWiki] wikicode bind: SESSION_NOT_READY | RATE_LIMITED | DB_ERROR
[NeedWiki] action auth: READY
[NeedWiki] action auth: NOT_BOUND | GENERATION_MISMATCH | AUTOTRADE
[NeedWiki] action auth: EXPIRED | IDENTITY_MISMATCH | BAD_REQUEST | DB_ERROR
```

보안 규칙은 다음과 같이 적용했다.

- bearer token 원문, bootstrap code 원문, `token_hash`, `code_hash`는 진단 로그의 포맷 문자열이나 인자로 전달하지 않는다.
- AID/CID는 실제 숫자 대신 항상 리터럴 `***`만 출력한다.
- `generation`만 세션 세대 비교를 위해 숫자로 출력한다.
- 민감 hash가 포함된 마지막 SQL query 전체를 출력할 수 있는 NEED Wiki 경로의 `Sql_ShowDebug` 호출을 제거하고 고정된 `DB_ERROR` 상태 로그로 교체했다.

검증 결과:

| 항목 | 결과 |
|---|---|
| `web-server` Release/x64 빌드 | PASS (경고/오류 0) |
| `map-server` 변경 소스 컴파일 | PASS |
| `map-server` 최종 링크 | 현재 작업 트리의 별도 미등록 `need_jf_pattern.cpp` 관련 외부 심볼 4개로 실패; NEED Wiki 컴파일 오류 없음 |
| 진단 매크로 민감값 인자 정적 검색 | PASS (`token_hash`, `code_hash`, code 원문, AID/CID 실제값 전달 없음) |

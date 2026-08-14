# NEED Summer Monitor v1

여름 이벤트 보상(Origin) 이후 **자산이 어느 계정으로 집중되는지**를 기존 로그만으로 조회하는
**읽기 전용** 운영 보조 웹. 서버(map-server)에 추가 부하를 주지 않으며, DB에 아무것도 쓰지 않습니다.

- 데이터 출처: `need_summer_reward_log`(Origin) + `picklog` / `zenylog` / `char` / `loginlog`(기존 로그).
- 자동 제재·악용 확정·IP 차단·실시간 감시 **없음**. 운영자가 패턴을 빠르게 찾는 도구입니다.
- 신뢰도 배지: `정확`(Shadow unique_id) / `높음`(Zeny src_id, Gold T·E) / `중간`(Gold G) / `후보`(Gold P·V/B) / `내부이동`(개인창고 R).

## 기능 (v1)
1. 이벤트 Origin 목록 (기간/Account/Character/IP 필터)
2. Account / Character / IP 검색
3. Account 상세 타임라인 (Origin + Shadow + Zeny + Gold 병합)
4. Shadow 이동 추적 (unique_id 기반, 계정 간 이동 표시)
5. Zeny 이동 조회 (zenylog src_id → 상대 account)
6. Gold(399990) 이동 조회 (type별 신뢰도 + 직접거래 상대 페어링)
7. 최종 수령 계정 집중 현황 (특정 계정 유입 + 기간 내 자동 탐지)

## 기술 구성
- **Node.js (>=18) + Express + mysql2**. 프런트엔드는 프레임워크 없는 바닐라 JS SPA.
- 단일 프로세스. Windows 개발 / Linux 배포 동일 코드. 외부 빌드 단계 없음.
- 인증: 환경변수 기반 단일 관리자 + HMAC 서명 세션 쿠키(HttpOnly). **비밀번호 하드코딩 없음.**

---

## 1. Windows 개발 실행

```bash
cd tools/need_summer_monitor
npm install
# 환경변수 설정 후 실행 (PowerShell 예시는 아래)
node src/server.js
```

PowerShell에서 환경변수와 함께 실행:
```powershell
$env:MONITOR_USER="admin"; $env:MONITOR_PASS="원하는비번"
$env:SESSION_SECRET="$(node -e "console.log(require('crypto').randomBytes(32).toString('hex'))")"
$env:DB_HOST="127.0.0.1"; $env:DB_PORT="3306"
$env:DB_USER="nsm_reader"; $env:DB_PASS="리더비번"; $env:DB_NAME="ragnarok"
node src/server.js
```

브라우저에서 `http://localhost:8787` 접속 → 로그인 → 조회.

`config.example.env`를 `.env`로 복사해 값만 채워도 됩니다(`.env`는 gitignore됨). `.env`를
쓰려면 `--env-file` 플래그를 사용하세요(Node 20+):
```bash
node --env-file=.env src/server.js
```

### 환경변수
| 변수 | 필수 | 설명 |
|---|---|---|
| `MONITOR_USER` / `MONITOR_PASS` | O | 운영자 로그인 계정. 하드코딩 금지, 환경에서 주입 |
| `SESSION_SECRET` | O | 세션 서명 키. 16자 이상 랜덤 |
| `SESSION_TTL_HOURS` | | 세션 유효시간(기본 12) |
| `DB_HOST`/`DB_PORT`/`DB_USER`/`DB_PASS` | O | DB 접속 |
| `DB_NAME` | O | 메인 DB (`char`, `need_summer_reward_log`) |
| `LOG_DB_NAME` | | 로그 DB (`picklog`/`zenylog`/`loginlog`). 미설정 시 `DB_NAME`과 동일(개발 기본) |
| `LOG_LOGIN_TABLE` | | loginlog 테이블명(기본 `loginlog`) |
| `PORT`/`HOST` | | 기본 `8787` / `0.0.0.0` |
| `COOKIE_SECURE` | | HTTPS 뒤에서 서비스 시 `1` (쿠키 Secure 플래그) |

> 메인 DB와 로그 DB가 **다른 스키마**여도 동일 MySQL 서버라면 동작합니다(모든 테이블을
> 스키마 한정자로 참조). 서로 다른 서버로 분리된 경우 v1은 지원하지 않습니다(단일 접속).

---

## 2. DB 계정 — SELECT 전용 권장 (쓰기 불가 보장)

웹은 어떤 경우에도 INSERT/UPDATE/DELETE를 하지 않지만, **DB 권한으로도 강제**하세요:

```sql
-- 개발/운영 공통: 읽기 전용 계정 생성 (로그와 메인이 같은 스키마면 한 줄이면 충분)
CREATE USER 'nsm_reader'@'127.0.0.1' IDENTIFIED BY '리더비번';
GRANT SELECT ON `ragnarok`.* TO 'nsm_reader'@'127.0.0.1';
-- 로그 DB가 분리돼 있으면 로그 스키마에도 SELECT 부여
-- GRANT SELECT ON `ragnarok_log`.* TO 'nsm_reader'@'127.0.0.1';
FLUSH PRIVILEGES;
```

이 계정을 `DB_USER`/`DB_PASS`로 사용하면 웹에서 물리적으로 쓰기가 불가능합니다.

---

## 3. Linux 실서버 배포 준비

> 이번 단계에서는 **실서버 배포는 하지 않습니다.** 아래는 배포 시 절차입니다.
> systemd로 띄우면 환경변수는 `EnvironmentFile`이 주입하므로 `--env-file` 플래그는 필요 없습니다.

### 3-0. 사전 점검 (안 하면 기동 실패)
- **Node 버전**: `dnf install nodejs`가 배포판에 따라 구버전(예: 16)을 설치할 수 있음.
  ```bash
  node -v    # v18 이상이어야 함. 낮으면 nodesource로 LTS 설치
  ```
  (수동 실행에서 `node --env-file=...`를 쓰려면 Node **20.6+** 필요. systemd 경로는 무관.)
- **npm 접속**: 서버에서 `npm ci`는 npm 레지스트리 접속이 필요. 폐쇄망이면 `node_modules`를 통째로 복사.
- **DB 리더 계정**: env의 `nsm_reader`(SELECT 전용, §2)를 미리 생성해 둘 것.

### 3-1. 실행 계정 + 디렉터리
`node_modules`는 gitignore이므로 소스만 복사하고 서버에서 `npm ci`로 재설치한다.
```bash
# systemd 유닛의 User=nsm 가 존재해야 하므로 서비스 전용 계정 생성
sudo useradd -r -s /sbin/nologin nsm

sudo mkdir -p /opt/need_summer_monitor
# (여기서 소스 배치: git clone / scp / rsync 등)
sudo chown -R nsm:nsm /opt/need_summer_monitor

# 의존성 설치 (서버에서, nsm 권한으로)
cd /opt/need_summer_monitor
sudo -u nsm npm ci --omit=dev          # 폐쇄망이면 생략하고 node_modules 복사
```

### 3-2. 환경변수 파일
`/etc/need_summer_monitor.env` — `EnvironmentFile`은 systemd가 root로 읽으므로 `root:root 600`이면 충분.
```
MONITOR_USER=admin
MONITOR_PASS=...
SESSION_SECRET=...(32+ random)   # node -e "console.log(require('crypto').randomBytes(32).toString('hex'))"
DB_HOST=127.0.0.1
DB_PORT=3306
DB_USER=nsm_reader
DB_PASS=...
DB_NAME=ragnarok
LOG_DB_NAME=ragnarok
PORT=8787
COOKIE_SECURE=1
```
```bash
sudo chmod 600 /etc/need_summer_monitor.env
```

### 3-3. systemd 서비스 등록(권장)
`/etc/systemd/system/nsm.service`:
```ini
[Unit]
Description=NEED Summer Monitor v1 (read-only)
# DB가 같은 호스트면 mariadb.service, 원격이면 아래를 network-online.target 로만 두세요.
After=network-online.target mariadb.service
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=/opt/need_summer_monitor
EnvironmentFile=/etc/need_summer_monitor.env
ExecStart=/usr/bin/node src/server.js
Restart=on-failure
User=nsm
Group=nsm

[Install]
WantedBy=multi-user.target
```
> `ExecStart`의 `/usr/bin/node`는 실제 경로에 맞추세요(`which node`로 확인, nodesource/nvm이면 다를 수 있음).

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now nsm
sudo systemctl status nsm       # 정상 기동 확인 (active running)
sudo journalctl -u nsm -f       # 오류 시 로그 확인
```

### 3-4. 배포 후 동작 확인
```bash
curl -s http://127.0.0.1:8787/api/session      # {"authed":false,"configured":true}
```
`configured:true`면 로그인 준비 완료. `false`면 env의 MONITOR_USER/PASS/SESSION_SECRET(16자+) 확인.

### 3-5. 포트/노출
- 기본 포트 `8787`. **공개 인터넷에 직접 노출하지 마세요.**
- 권장: 사내망/VPN 한정, 또는 nginx 리버스 프록시 + HTTPS(그 뒤에서 `COOKIE_SECURE=1`).
- 방화벽에서 운영자 IP만 8787 허용:
  ```bash
  sudo firewall-cmd --permanent --add-rich-rule='rule family="ipv4" source address="운영자IP/32" port port="8787" protocol="tcp" accept'
  sudo firewall-cmd --reload
  ```

---

## 4. 성능 원칙 (실서버 DB 보호)
- 기본 조회 기간 **최근 7일**(변경 가능). 모든 목록 쿼리에 **시간 범위 + LIMIT** 강제.
- 2단계 조회: 1차 `need_summer_reward_log`(인덱스: created_at/account/char/unique_id)로 좁힘 →
  2차 picklog/zenylog는 **명시적 char_id 목록 + 시간범위 + nameid/unique_id**로만 스코프.
- picklog/zenylog **전체 스캔·전체 JOIN 금지**. 자동 새로고침 없음. 무거운 자동 탐지는 버튼 클릭 시에만.

> **주의(대용량 로그)**: 기본 rAthena `picklog`/`zenylog`에는 `PRIMARY(id)`, `INDEX(type)`만
> 있고 `char_id`/`unique_id`/`nameid`/`time` 인덱스가 없습니다. 로그가 커지면 조회가 느려질 수
> 있습니다. 필요 시 **읽기 성능용 인덱스**(구조 변경 아님)를 운영자 판단으로 추가 권장:
> ```sql
> ALTER TABLE `picklog` ADD INDEX `idx_uid` (`unique_id`),
>                       ADD INDEX `idx_char_time` (`char_id`,`time`),
>                       ADD INDEX `idx_name_time` (`nameid`,`time`);
> ALTER TABLE `zenylog` ADD INDEX `idx_char_time` (`char_id`,`time`),
>                       ADD INDEX `idx_src` (`src_id`);
> ```
> 이는 서버 코드/스키마 컬럼 변경이 아니며 웹 없이도 안전합니다. v1은 인덱스 없이도 동작합니다.

---

## 5. 보안 메모
- 외부 공개 페이지 아님(운영자 전용). 비밀번호/시크릿은 환경변수로만.
- 쓰기 권한 없는 DB 계정 사용 권장(§2). 웹 자체도 쓰기 경로 없음.
- 세션 쿠키 HttpOnly + SameSite=Lax. HTTPS 뒤에서는 `COOKIE_SECURE=1`.

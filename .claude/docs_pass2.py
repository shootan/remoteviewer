# -*- coding: utf-8 -*-
import io

OPS = u'''
### 운영 배치 — 리버스 프록시 뒤에 둘 때 (2026-09-09)

⚠️ 아래 수치는 **저장소 코드에서 읽은 값**이다. **라이브 NAS 설정은 확인하지 않았다**(이 작업은 NAS 무접속).
포트 숫자는 전부 환경변수이며, 괄호 안이 코드 기본값이다.

**프록시로 넘길 것 — HTTP 만, 경로 9개**
- 외부 443 → upstream **`http://<디렉터리 호스트>:<REMOTE60_DIR_PORT>`**(기본 `8080`, 현 배포 `29180`).
  프록시와 디렉터리가 같은 기계면 `127.0.0.1`.
- 경로는 **평면 9개, rewrite 불필요**: `GET /healthz` · `POST /api/signup` · `POST /api/login` ·
  `POST /api/host/register` · `POST /api/host/heartbeat` · `GET /api/hosts` · `POST /api/connect` ·
  `POST /api/logs` · `GET /api/update/manifest`.
- **`client_max_body_size` ≥ 512KB** — `POST /api/logs` 상한이 `LOG_MAX_UPLOAD_BYTES = 512 KiB`
  (`server.js:251`)다. 나머지 JSON 은 `MAX_BODY_BYTES = 16 KiB`(`:484`). 프록시가 512KB 를 막으면
  **로그만 조용히 사라지고** 나머지는 정상 동작하므로 증상이 로그 부재로만 나타난다.
- 전달해야 하는 헤더: `Authorization` · `x-host-token` · `x-log-device` · `x-log-stream`.
  전부 하이픈 표기라 nginx 기본 전달 대상이다(**밑줄 헤더가 아니라서 `underscores_in_headers` 와 무관**).
- **WebSocket upgrade 불필요** — 이 서버에 업그레이드 경로가 없다. 쿼리스트링은 보존.
  `proxy_read_timeout` 60s 권장(클라 타임아웃이 8~10s 대라 여유).

**⚠️ UDP 는 443 뒤로 가지 않는다**
- **관측**(`REMOTE60_DIR_UDP_PORT`, 기본 `8081`, 현 배포 `29181`)과 **릴레이**(`REMOTE60_RELAY_PORT`,
  기본 `43000`, 현 배포 `29190`)는 **UDP** 이고 리버스 프록시를 타지 않는다.
- "전부 443 으로 통일" 로 읽고 **UDP 포워딩을 걷으면 제품이 죽는다** — 관측이 도착하지 않으면 호스트는
  하트비트를 끝내지 못해 목록에 뜨지 않고, 릴레이는 아예 경로가 없다. **기존 포트포워딩 유지.**

**⚠️ 운영에서 새로 켜야 하는 스위치는 이 둘뿐이다**
- **`REMOTE60_DIR_OBSERVE_PORT`** — 클라이언트에게 **광고할** UDP 포트. 미설정이면 서버가 실제로 듣는
  `REMOTE60_DIR_UDP_PORT` 를 광고한다. **1..65535 밖이면 광고 자체를 생략**한다(클라가 버릴 값을 내보내지
  않는다). ⚠️ **TLS 를 앞에 두면 이 값이 실질적으로 필수다** — https 인데 광고가 없으면 클라이언트는
  **거부**한다(444 로 보내지 않는다).
- **`REMOTE60_DIR_OBSERVE_HOST`** — 광고할 host. **비우면 클라가 접속한 host** 를 쓴다(대개 이것으로 충분).
  ⚠️ **서버는 이 값을 검증하지 않는다.** 잘못 넣으면 클라가 거부하고 디렉터리 host 로 폴백하며, 그 사실은
  클라 진단에 남는다 — 즉 **틀려도 조용히 도는 대신 기록이 남는다.**

**운영자용 — 로그에서 409 를 봤을 때**
- `observation_required` / `observation_expired` 는 **인증 실패가 아니다.** 토큰·세션은 멀쩡하고, 서버가
  **이 클라이언트의 주소 관측을 가지고 있지 않다**는 뜻이다. **클라이언트가 스스로 복구한다**: 관측을 다시
  보내고 **1회만** 재시도한다. 반복해서 보이면 **관측 UDP 가 서버에 도달하지 못하는 것**이므로
  포트포워딩·방화벽·위 두 환경변수를 본다.
- **401 은 다르다** — 그건 토큰이 서버에 없다는 뜻이고, 호스트는 **재등록**한다. 409 에서는 재등록하지
  않는다(캐시 토큰을 지키기 위해서다).

**진단용 수치**
- 하트비트 주기 **25초**(`HostAgentConfig::heartbeatSeconds`) · 호스트 오프라인 판정 **90초**
  (`HOST_OFFLINE_MS`) · 관측 TTL **5분**(`REMOTE60_DIR_OBSERVE_TTL_MS`, 만료 후 **2배까지 보존**해
  "만료" 와 "부재" 를 구분) · 클라이언트 재시도 **1회**(무한 루프 없음, 재로그인 강제 없음).
'''

p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
assert u'### 운영 배치' not in d
d = d.rstrip('\n') + '\n' + OPS
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok plan ops')

# ---------------------------------------------------------------- history cross-reference
p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()
old = u'- 확인만 함: **29180 하드코딩 삽입 0건**(제품 코드). 포트 출처는 url 아니면 서버 광고, 제3의 출처 없음.'
new = (u'- 확인만 함: **29180 하드코딩 삽입 0건**(제품 코드). 포트 출처는 url 아니면 서버 광고, 제3의 출처 없음.\n'
       u'- ⚠️ **인접한 미착수 항목**: `directory_client.cpp:451` 은 UDP 관측 응답의 포트를 여전히 느슨하게 읽는다'
       u'(**상한 검사 없음** → `65537` 이 `1` 로 절단). 이번에 엄격 리더를 넣은 곳은 `:183` 의 **광고 metadata** 이고,'
       u' 이 자리는 **고치지 않았다** — 대장 `docs/작업목록.md` **1.10**, 착수 여부는 Codex 판단.')
assert d.count(old) == 1, 'history anchor'
d = d.replace(old, new, 1)

old2 = u'⚠️ **마이그레이션 영향 (origin 비교)**'
new2 = (u'**운영 배치 · 리버스 프록시 · 새 환경변수**는 `docs/구현계획.md` "운영 배치 — 리버스 프록시 뒤에 둘 때" 에 적었다.\n'
        u'⚠️ 그 절의 수치는 **저장소 코드에서 읽은 것**이고 **라이브 NAS 설정은 확인하지 않았다**(이 작업은 NAS 무접속).\n\n'
        u'⚠️ **마이그레이션 영향 (origin 비교)**')
assert d.count(old2) == 1, 'history anchor 2'
d = d.replace(old2, new2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok history xref')

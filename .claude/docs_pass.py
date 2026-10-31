# -*- coding: utf-8 -*-
import io

HISTORY = u'''
### 482) 2026-09-09 관측 엔드포인트 · https 전송 · 프록시 IP 오염 — **한 산술 가정이 제품을 못 쓰게 만들고 있었다**
커밋 8개: `2ff2fa6`(스킴 단일화) · `a68f493`(https 전송) · `19a1f6c`(서버 409) · `c36fb75`(TLS 픽스처) · `68d3936`(Android) · `f1cd2fb`(재시도 회귀) · `887d44e`(origin 비교) · `b924286`(삼런타임 벡터).

- **뿌리**: 모든 클라이언트가 관측 UDP 포트를 **`http 포트 + 1`** 로 계산했다. TLS 뒤 443 이면 **444** 이고 거기엔 아무것도 없다 → 관측이 도착하지 않는다 → 호스트는 하트비트를 못 끝내 **목록에 아예 안 뜨고**, 뷰어는 `/api/connect` 까지 못 가서 **릴레이 주소도 못 배운다**. **그동안 모든 스위트가 초록이었다 — 테스트가 코드와 같은 가정을 하고 있었기 때문이다.**
- **서버가 말한다**: `REMOTE60_DIR_OBSERVE_PORT`(기본 `UDP_PORT`)·`REMOTE60_DIR_OBSERVE_HOST`(빈 값이면 클라가 접속한 host)를 login·host/register·`/healthz` 에 `observe:{port,host}` 로 실어 보낸다. **새 REST 경로 없음.**
- **클라가 읽는 규칙**(공유 벡터로 고정, 아래 (F)): 광고 우선 → http 는 `+1` → **https 는 기본값 없음(거부)** → 65535 는 `+1` 불가. 포트는 **`observe` 안에서만** 읽고 **1..65535 정수만** 인정한다(`29181.5`·`"29181"`·`-1`·`65536` 전부 아님).
- **광고 host 는 서버가 검증하지 않는다** — 운영자가 입력한 값 그대로 나간다. 그래서 **클라가 검증**하고, 나쁜 host 가 **좋은 포트를 같이 버리지 않게** `hostRejected` 로 기록하고 디렉터리 host 로 폴백한다.

**https 전송 (`a68f493`)**
- `parse_directory_url` 이 https 를 받아들이고 **스킴을 돌려준다**(`outSecure`). **스킴을 정하는 곳은 이제 여기 하나**이고 6개 호출자가 전부 여기서 받는다. 기본 포트도 스킴에서 나온다(80/443).
- **같은 함수 안에 대소문자 비대칭**이 있었다: https 는 무시, http 는 구분 → **`HTTP://host` 가 "unsupported url scheme"** 이었다. 올바르게 입력한 url 이 대문자 때문에 거부됐다.
- https 는 **WinHTTP**, http 는 **기존 raw socket 그대로**(현 배포 무변경). 두 전송을 **같은 서버에 붙여 계약 동등성 회귀**를 돌렸고 **실제 차이 2건**이 나왔다:
  - **응답 상한이 64KiB/256KiB/4MiB 세 개**였고 **셋 다 조용히 잘라서 돌려줬다.** 잘린 body 를 파싱하면 "malformed json" 이 나오는데 그건 **받은 것에 대해선 참이고 서버에 대해선 거짓**이다 → 상한 하나, 초과는 **실패 + 사유**.
  - **초과 실패인데 WinHTTP 는 status 에 200 을 남겼다**(socket 은 0). false 반환은 "교환이 없었다" 인데 200 이 남으면 **반환값 안 보고 status 만 보는 코드**를 부른다.
- **업데이터는 정책 비공유**: 전송만 공유하고 **http 거부는 그대로**다(가져오는 아티팩트가 관리자 권한으로 실행되므로). 회귀로 못 박았다.

**프록시 IP 오염 (`19a1f6c`)**
- 관측이 없으면 서버가 **HTTP 소켓의 주소**로 폴백했다. 그 주소는 **중간에 아무것도 없을 때만** 클라이언트의 것이다 — 프록시·로드밸런서·TLS 종단 뒤에서는 **모든 호스트가 같은 주소로 게시**되고, punch 는 듣지 않는 중간 상자로 가며, **서로 다른 계정이 구분되지 않는다.**
- 폴백을 **고치지 않고 없앴다**(`remoteIp()` 함수 삭제). 관측만이 NAT 가 실제로 매핑한 포트를 알기 때문이다. body 의 `udpPort`(클라 자기신고)도 같이 제거.
- **409**, 401 아님 — 자격증명은 멀쩡하므로 **호스트가 재등록·뷰어가 재로그인하면 안 된다.** 사유는 `observation_required` / `observation_expired`.
  - 이 둘을 구분하려면 **sweep 이 만료 즉시 지우면 안 됐다**(그러면 "안 보냈다" 와 "너무 늦었다" 가 같은 부재 항목). TTL 2배까지 보존한다.
- **거부는 어떤 쓰기보다 먼저** 일어난다: `lastSeen` 미갱신(갱신하면 **닿지 않는 주소로 online 유지**), `pendingPunch` 미소모(거부된 하트비트가 punch 를 먹으면 **제대로 한 뷰어가 고립**).
- 옛 주석은 *"configured 면 프록시 헤더를 존중한다"* 고 적혀 있었지만 **헤더를 읽는 코드가 없었다.**

**클라이언트 유한 재시도 (`19a1f6c`·`68d3936`, 회귀 `f1cd2fb`)**
- 호스트: 409 → **스트림용 같은 소켓**에서 재관측 → **하트비트 1회만** 재시도 → 실패면 상태만 남기고 25초 주기가 backoff. 뷰어·폰도 같은 모양(**409 이고 사유가 `observation*` 일 때만**).
- 회귀는 **요청 수를 센다.** "동작했다" 는 **40번 재시도한 클라이언트도 참**이고 **성공하는 길에 로그아웃한 클라이언트도 참**이기 때문이다: `/api/connect` **정확히 2회** · 계속 거부해도 **2회에서 정지** · 404 는 **1회** · `/api/login` **0회**.
- 호스트는 `HostAgent` **자체**로 돌린다: **heartbeat 2회 · register 정확히 1회**(캐시 토큰 생존). **401 대조군**이 옆에 있어야 이 단정에 뜻이 생긴다 — 없으면 `register == 1` 은 **어떤 상황에서도 재등록하지 않는 클라이언트에게도 초록**이다. 401 에서는 `register == 2` 다.
- 토큰을 버리면 **무인 PC 는 사람이 걸어가야** 복구된다. 그것이 이 단정이 지키는 것이다.

**Android (`68d3936`)**
- 🔴 `DirectoryClient` 가 **모든 409 를 "호스트가 오프라인입니다"** 로 찍고 있었다. 서버가 관측 없음에도 409 를 내게 되면서 **폰이 "PC 가 꺼져 있다" 고 거짓말하는** 상태가 됐다 — 사용자를 **고칠 수 있는 것(서버 설정)에서 떼어내 엉뚱한 기계로** 보낸다. (G) 가 만든 결함이고 **(D) 를 (G) 뒤에 둔 순서 덕에 드러났다.**
- `LogUploader` 는 url 을 **두 번째로 판정**하고 있었다(`startsWith("http")` — `https://` 도 `httpx://` 도 참이고, 그 외에는 스스로 `http://` 를 붙였다). 그 요청 헤더에는 **세션 토큰**이 실린다. 이제 `DirectoryClient.normalizedUrl()` 하나를 쓴다.
- ⚠️ 단위 테스트에 **실제 `org.json`** 을 넣었다. 흔한 처방인 `isReturnDefaultValues = true` 는 **쓰지 않았다** — 그러면 `JSONObject` 가 null·0 을 돌려주고 **파서 테스트가 아무것도 파싱하지 않은 채 초록**이 된다. APK 에는 안 들어간다.
- ⚠️ **자기 함정 하나**: `/healthz` 실패를 `catch` 로 삼켰더니 **"못 닿음" 과 "아무 말 없음" 이 같은 빈 결과**가 됐다 → https 에서 **다운된 서버를 "설정이 필요합니다" 로 보고**한다. `HealthObserve(reached, advertised, error)` 로 분리했다.
- 배선(`MainActivity`)은 테스트를 억지로 붙이는 대신 **결정을 밖으로 뺐다**(`resolveObserveTarget(url, cached, probe)`). 도달 불가한 곳에 단정을 못 넣겠으면 **도달 가능하게 만드는 것**이 답이다.
- **불가침 확인**: `UpdateFlow.kt`·`UpdateManifest.kt`·`AndroidManifest.xml`·`network_security_config.xml` **diff 0**. cleartext 확대 없음, signer 무변경.

**origin 비교 (`887d44e`)**
- 캐시된 host token 이 발급처에 속하는지를 **url 문자열 그대로** 비교했다 → `http://h:8080/` 과 `http://h:8080` 이 다른 서버, `http://h` 와 `http://h:80` 도 다른 서버 → **매번 토큰 폐기 + 재등록**. **후행 슬래시 하나가 무인 PC 의 자격증명을 날린다.**
- `directory_origin_key()`: 포트 항상 명시 · host 소문자 · **스킴은 남긴다.** 합쳤으면 **한쪽에서 발급된 토큰이 다른 origin 으로 흘러간다** — 정규화의 목적이 "같은 것을 같게" 인데 **다른 것을 같게 만들면 정반대**다.
- **저장·표시값은 사용자가 입력한 그대로** 둔다. 비교 전용이다.
- 확인만 함: **29180 하드코딩 삽입 0건**(제품 코드). 포트 출처는 url 아니면 서버 광고, 제3의 출처 없음.

**삼런타임 계약 (`b924286`)**
- `apps/shared/observe_endpoint_vectors.txt` — 규칙 9개 + `parse` 21행 · `port` 11행. C++·Kotlin 이 읽고 **같은 답**을 내야 한다. 양쪽 다 **읽은 행 수 하한을 단정**한다(공유 벡터 테스트의 고유 실패 형태가 **파일을 못 찾고 아무것도 안 읽은 채 초록**이라서).
- **JS 다리는 일부러 다르다.** 서버는 파싱하지 않고 **배출**하므로, 클라 둘의 일치만으로는 **둘 다 올바르게 파싱해서 둘 다 폴백**하는 경우 — 이 작업이 없애려던 바로 그 실패 — 를 못 잡는다. `observe_contract_test.js` 는 서버가 내는 문서를 규칙에 대본다: **중첩 위치** · **범위 내 정수** · 세 문서 동일값 · **감당 못 하는 포트는 아예 안 냄**.

**⚠️ TLS 증거 등급 — 한 덩어리로 "TLS 검증 완료" 라고 쓰지 않는다**

| 항목 | 증거 |
|---|---|
| 인증서 거부 | **실행 증거** — 자체서명 loopback 픽스처에 제품 전송으로 붙어 handshake 거부 확인 |
| https→http 강등 거부 | **런타임 옵션 증거** — 살아 있는 세션에서 `WinHttpQueryOption` 되읽기 `policy=1`(`DISALLOW_HTTPS_TO_HTTP`, 기본 `ALWAYS` 아님). **실경로 미실행** |
| TLS 1.2 강제 | **소스 증거만** — `WINHTTP_OPTION_SECURE_PROTOCOLS` 는 **되읽기 불가**(error 87). 런타임 증거 없음 |
| 유효 HTTPS 성공 대조 | **미실행** — 신뢰되는 TLS 엔드포인트가 없다. **거부만 검사하므로 전부 거부하는 전송도 이 파일을 통과한다.** NAS 준비 후 현장 |

- 강등 거부는 **자체서명으로는 검증할 수 없다** — 신뢰되지 않으면 **handshake 가 먼저 실패**해 redirect 에 도달하지 못한다. 그 실패를 PASS 로 적으면 **정책이 있든 없든 초록인 단정**이 된다. 그래서 `SKIP` 으로 출력하고 이유를 문구에 박았다.
- 검증을 끄는 인자는 **만들지 않았다.** 그런 것은 대개 "테스트용" 인자로 열린다.

**⚠️ 하드 의존 — 서버 앱 업데이트가 필요하다 (nginx·프록시 설정과 별개)**

| 조합 | 결과 |
|---|---|
| **구서버 / 신클라** | 서버가 `observe` 를 안 낸다 → **http 는 `+1` 로 정상 동작**, **https 는 거부**(사유 명시). https 로 가려면 **서버 앱 업데이트 필수** |
| **신서버 / 구클라** | 구클라가 광고를 무시하고 `+1` → **https 에서 444 로 간다**. 즉 **클라이언트 업데이트도 하드 의존** |
| 신서버 / 신클라 | 광고대로 동작. `REMOTE60_DIR_OBSERVE_PORT` 미설정이면 서버가 **자기 UDP 포트**를 광고 |
- 즉 **https 전환은 nginx vhost 만으로 끝나지 않는다.** 서버 앱과 양쪽 클라이언트가 모두 이 버전이어야 한다.

**⚠️ 마이그레이션 영향 (origin 비교)** — 후행 슬래시 차이로 **매번 재등록하던 경우는 사라진다.** 반대로 서버 주소를 **http→https 로 바꾸면 재로그인·재등록이 필요**하다. **의도된 것이다**: 다른 origin 이고, 한쪽에 발급된 토큰을 다른 쪽으로 보내지 않는다.

**실행**(RDP 종료 상태 `console` Active, `^PASS` 기준, 전체 재빌드 오류 0)
- C++: `observe_vectors` **34** · `directory_session_client` **93** · `directory_http_contract` **43** · `directory_retry` **23** · `version_compare` **139** · `winhttp_tls_posture` **8 PASS / 3 SKIP** · `updater_options` **50** · `update_http` **36** · `client_shell_bridge` **29** — 전부 exit 0 / FAIL 0
- JS 전체 **335 PASS**, exit 0 (프록시 오염 24 · observe 계약 11 포함)
- Android **61 tests / 0 failures / skipped=0** (`--no-daemon`)
- **하지 않은 것**: 실배포 · NAS · nginx · 운영 인증서 · DNS · 설치 · `dist/` 변경 · APK 생성 · push. 라이브 불변.
'''

LEDGER_NEW = u'''1.11 [x] **관측 엔드포인트 · https 전송 · 프록시 IP 오염** (history #482, 커밋 `2ff2fa6`~`b924286`) — 클라 전부가 관측 포트를 `http+1` 로 계산해 **TLS 뒤 443 이면 444** 로 보내고 있었다(호스트가 목록에 안 뜨고 뷰어가 릴레이 주소를 못 배움). 서버가 `observe:{port,host}` 를 광고하고 세 런타임이 **공유 벡터**(`apps/shared/observe_endpoint_vectors.txt`)로 합의. https 는 WinHTTP·http 는 기존 소켓(계약 동등성 회귀로 **실제 차이 2건** 발견: 응답 상한 3종 무음 절단 · 실패인데 status 200). 서버는 관측 없는 요청의 **소켓 주소 폴백을 삭제**하고 **409**(`observation_required`/`observation_expired`, 쓰기 이전 거부). 클라 재시도는 **1회**(카운트 회귀). Android 는 **모든 409 를 "PC 오프라인"** 으로 표시하던 결함 수정. origin 비교로 **후행 슬래시가 무인 PC 토큰을 날리던 것** 수정. ⚠️ **하드 의존**: https 전환은 nginx 설정만으로 안 되고 **서버 앱 + 양쪽 클라 업데이트**가 필요하다. ⚠️ **TLS 미검증 3건**: 1.2 런타임 증거 없음 · 강등 거부 실경로 미실행 · **유효 HTTPS 성공 대조 미실행**(NAS 준비 후 현장)

'''

PLAN = u'''
## 관측 엔드포인트 · https 디렉터리 (2026-09-09, history #482)

**결정**: 관측 UDP 포트는 **서버가 광고**한다. 클라이언트의 `http+1` 산술은 **http 무광고일 때의 기본값**으로만 남는다.
합의는 `apps/shared/observe_endpoint_vectors.txt` 에 고정하고 C++·Kotlin·JS 세 스위트가 읽는다.

**서버 설정** (`apps/directory/server.js`)
- `REMOTE60_DIR_OBSERVE_PORT` — 광고할 UDP 포트. 기본값은 서버가 실제로 듣는 `REMOTE60_DIR_UDP_PORT`.
  **1..65535 밖이면 광고 자체를 하지 않는다**(클라가 버릴 값을 내보내지 않는다).
- `REMOTE60_DIR_OBSERVE_HOST` — 광고할 host. **빈 값이면 클라가 접속한 host** 를 쓴다.
  ⚠️ 서버는 이 값을 **검증하지 않는다**. 잘못 넣으면 클라가 거부하고 디렉터리 host 로 폴백한다(진단에 기록됨).
- `REMOTE60_DIR_OBSERVE_TTL_MS` — 관측 유효 시간(기본 5분). 만료 분기가 실제 분기라서 노출한다.
- 실린 곳: `POST /api/login` · `POST /api/host/register` · `GET /healthz`. **새 REST 경로 없음.**

**TLS 를 앞에 두는 경우 (nginx 등)**
- HTTP 만 프록시하면 **관측 UDP 는 프록시를 타지 않는다.** 그래서 `REMOTE60_DIR_OBSERVE_PORT`/`_HOST` 로
  **UDP 가 실제로 도달하는 곳**을 광고해야 한다. 이것을 설정하지 않으면 **https 클라이언트는 거부**한다
  (444 로 보내지 않는다 — 침묵은 "PC 가 꺼졌다" 처럼 읽히기 때문이다).
- 관측이 없는 요청은 **409** 로 거부된다. 프록시 뒤에서는 서버가 볼 수 있는 주소가 **프록시의 것**이라
  폴백이 존재하지 않는다(폴백을 삭제했다).

⚠️ **하드 의존**: https 전환에는 **서버 앱 업데이트 + 양쪽 클라이언트 업데이트**가 모두 필요하다.
구서버/신클라는 https 에서 **거부**되고, 신서버/구클라는 **여전히 444 로 간다**.

⚠️ **TLS 증거는 세 등급으로 나뉜다** — 인증서 거부는 **실행 증거**, 강등 거부는 **런타임 옵션 증거(실경로 미실행)**,
TLS 1.2 강제는 **소스 증거만**(옵션 되읽기 불가). **유효 HTTPS 성공 대조는 미실행**이며 NAS 엔드포인트 준비 후
현장 단계다. 셋을 묶어 "TLS 검증 완료" 라고 쓰지 않는다.
'''

# ---------------------------------------------------------------- history
p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()
assert '### 482)' not in d, 'entry already present'
d = d.rstrip('\n') + '\n' + HISTORY
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok history')

# ---------------------------------------------------------------- ledger
p = 'docs/작업목록.md'
d = io.open(p, encoding='utf-8').read()
anchor = u'1.10 [ ] **느슨한 포트 파싱'
assert d.count(anchor) == 1, 'ledger anchor'
d = d.replace(anchor, LEDGER_NEW + anchor, 1)
d = d.replace(u'(history #482 예정)', u'(history #482)', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok ledger')

# ---------------------------------------------------------------- plan
p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
assert u'## 관측 엔드포인트' not in d
d = d.rstrip('\n') + '\n' + PLAN
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok plan')

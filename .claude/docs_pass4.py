# -*- coding: utf-8 -*-
import io

# ---------------------------------------------------------------- retraction + evidence upgrade
p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

old = u'- 🔴 **응답에 `observe` 가 없다 → 현 배포는 구 서버다.** 신 서버라면 `withObserve()` 가 `/healthz` 에 실었을 것이고, 기본값이 listen 포트라 값이 비는 경우가 없다.'
new = (u'- 🔴 **응답에 `observe` 가 없다.** 확정된 사실은 여기까지다: **현 엔드포인트가 신규 metadata 계약을 제공하지 않는다.**\n'
       u'  - ⚠️ **정정(#484)**: 처음에 이것을 *"현 배포는 구 서버"* 라고 적었는데 **단정할 근거가 없다.** '
       u'**프록시가 `/healthz` 를 자체 응답할 수도 있고**, 그 응답이 backend 에서 왔다는 구성 증거가 없다. 배포 버전·실파일 대조도 하지 않았다. '
       u'**원인은 미확정**이며 배포 의존성 결론은 어느 쪽이든 같다.')
assert d.count(old) == 1, 'retract'
d = d.replace(old, new, 1)

old2 = u'| 유효 HTTPS 성공 대조 | **미실행** — 신뢰되는 TLS 엔드포인트가 없다. **거부만 검사하므로 전부 거부하는 전송도 이 파일을 통과한다.** NAS 준비 후 현장 |'
new2 = (u'| 유효 HTTPS 성공 대조 | **제품 전송으로 확인**(2026-09-09, #484) — `winhttp_transport.cpp` 를 **무변경 링크**한 read-only probe 로 '
        u'`GET https://rem.shotan.net/healthz` → `sent=1 status=200 bytes=11`. 음성 대조(없는 호스트) `sent=0 status=0 (12007)`. '
        u'⚠️ **강등 실경로는 이것으로 상쇄되지 않는다** |')
assert d.count(old2) == 1, 'evidence row'
d = d.replace(old2, new2, 1)

HIST = u'''
### 484) 2026-09-09 업데이트 manifest URL 을 디렉터리 origin 에서 파생 + 실측 2건(정정·승격)
- Codex 확정: 이것은 **원 task(t-twp27ott, "Host·PC·APK 접속 주소를 https://rem.shotan.net 하나로 통일") 범위 항목**이라 추가 승인 없이 착수.

**① 왜 필요했나 — 주소는 있는데 아무도 잇지 않았다**
- manifest URL 이 **환경변수(Windows `REMOTE60_UPDATE_MANIFEST_URL`) / 빌드 상수(Android `BuildConfig.UPDATE_MANIFEST_URL`)** 로만 왔다. 서버에는 `GET /api/update/manifest` 라우트가 **이미 있었다.** 즉 **주소도 있고 라우트도 있는데 둘을 잇는 것이 없어서**, 서버 주소 하나만 설정한 기계는 업데이트를 **영원히 확인하지 못했다.**
- 이제 **override 가 있으면 override, 없으면 디렉터리 origin 에서 파생**한다. override 를 쓰던 배포는 **그대로**다.

**② 규칙 — 문자열 접합이 아니라 origin 에서 만든다**
- `directory_origin_key()`(C++) · `originKey()`(Kotlin)가 **스킴·host·포트**를 정하고, 거기에 `/api/update/manifest?platform=<windows|android>` 를 통째로 붙인다. 그래서 **후행 슬래시·경로·대소문자·기본 포트**가 url 한가운데로 새어 들어가지 못한다.
- **`.sig` 를 따로 받지 않는다** — 서버는 **한 응답에 `{manifest, signature}`** 를 담는다.
- ⚠️ **http 디렉터리는 아무것도 파생하지 않는다.** https 로 **추측해 올리면** 아무도 설정하지 않은 서버를 발명하는 것이고, http 로 **따라 내려가면** 관리자 권한으로 실행될 아티팩트를 **누구나 고쳐 쓸 수 있는 구간**으로 받는 것이다. http 배포는 **명시 https override** 로 말해야 한다. 그때까지는 **미설정**(실패가 아니다).

**③ 🔴 Android 는 서버가 낸 적 없는 주소를 받고 있었다**
- `UpdateFlow.check()` 가 문서를 받은 뒤 **`"$manifestUrl.sig"`** 를 따로 요청했다. 서버 라우트는 `/api/update/manifest?platform=android` 이고, 여기에 `.sig` 를 붙이면 **쿼리 값 뒤에 붙는다**(`...platform=android.sig`) — **서버가 낸 적 없는 주소**다.
- **아무도 몰랐던 이유는 그 줄이 한 번도 실행되지 않았기 때문이다**: URL 이 빈 빌드 상수라 그 위에서 `NotForUs` 로 끝났다. **파생이 그 줄을 실행 가능하게 만들었다.**
- 그래서 Windows 클라이언트와 **같은 모양**으로 고쳤다: 한 응답에서 `manifest`·`signature` 를 뽑는다. **https 전용 가드(`:79`)는 손대지 않았다.**
- ⚠️ 이것은 **불가침으로 지정됐던 `UpdateFlow.kt` 를 고친 것**이다. 고친 부분은 **fetch 모양뿐**이고 https 정책은 그대로다. 그렇게 하지 않으면 파생이 **동작하지 않는 URL 을 만들어내는** 기능이 된다.

**④ 🔴 미해결 — 파생 URL 은 지금 401 을 받는다**
- 서버 `handleUpdateManifest` 는 **세션 또는 `x-host-token`** 을 요구한다(`server.js` 첫 블록). 그런데 C++ `https_get_text` 와 Android `fetchText` 는 **둘 다 인증 헤더를 보내지 않는다.**
- 즉 **파생된 URL 로 실제로 받아오면 401** 이다. env override 로 **인증 없는 다른 서버**를 가리키던 기존 사용에는 없던 문제이고, **이번 파생으로 처음 드러났다.**
- **고치지 않았다.** 토큰을 업데이트 fetch 경로로 넣는 것은 범위가 다르고(누가 어떤 토큰을 어느 origin 에 보내는지가 걸린다), **같은 origin 이라도 결정은 따로 받아야 한다.** → **Codex 판단 요청.**

**⑤ 실측 2건**
- **정정**: #483 ③ 의 *"현 배포는 구 서버"* 를 **철회**한다. 확정된 것은 **"현 엔드포인트가 신규 metadata 계약을 제공하지 않는다"** 뿐이고, **프록시가 `/healthz` 를 자체 응답할 가능성**이 배제되지 않았다. 배포 버전 대조도 하지 않았다. 결론(하드 의존)은 어느 쪽이든 같다.
- **승격**: 검증용claude 가 `winhttp_transport.cpp` 를 **무변경 링크**한 read-only probe 로 **제품 TLS 자세**로 `GET https://rem.shotan.net/healthz` → **`sent=1 status=200 bytes=11`**, 음성 대조(없는 호스트) **`sent=0 status=0 (12007)`**. 증거 등급표의 *"유효 HTTPS 성공 대조 — 미실행"* 을 **"제품 전송으로 확인"** 으로 올렸다. ⚠️ **강등 실경로는 여전히 미검증**이고 이것으로 상쇄되지 않는다.

**⑥ 범위 대조 (원 task 문과)**
"Host·PC·APK 접속 주소를 하나로" 기준으로 훑었다. 하나의 주소에서 나오는 것: **login · hosts · connect · 관측 엔드포인트 · 로그 업로드 · (이번에) 업데이트 manifest**. 제품 코드에 **하드코딩된 운영 주소·포트는 0건**(`rem.shotan.net`·공인 IP·`29180` 모두 0). 유일한 리터럴은 `host_app_main.cpp` 의 **`--ui-preview` 전용 `http://127.0.0.1:8080`** 으로, 실제 접속 경로가 아니다. **릴레이 주소는 서버 응답에서 온다.**

**실행**(`^PASS`, RDP 종료 상태): `directory_session_client` **105**(93→105) · `directory_retry` 44 · `observe_vectors` 34 · `directory_http_contract` 43 · JS 335 — 전부 exit 0 / FAIL 0. Android **65 tests / 0 failures / skipped=0**(`DirectoryObserve` 26). 전체 재빌드 오류 0.
'''

assert u'### 484)' not in d
d = d.rstrip('\n') + '\n' + HIST
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok history')

# ---------------------------------------------------------------- plan: correct the wording too
p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
old = u'''그리고 **2026-09-09 실측 기준 현 배포는 구 서버**다
(`GET https://rem.shotan.net/healthz` → `{"ok":true}`, **`observe` 없음**) — 지금 신 클라이언트가 붙으면
**설계대로 거부된다**. 라이브 상태는 변할 수 있으므로 그날의 사실로 읽을 것.'''
new = u'''그리고 **2026-09-09 실측 기준 현 엔드포인트는 신규 metadata 계약을 제공하지 않는다**
(`GET https://rem.shotan.net/healthz` → `{"ok":true}`, **`observe` 없음**) — 지금 신 클라이언트가 붙으면
**설계대로 거부된다**. ⚠️ **원인은 미확정**이다: 서버가 구버전일 수도, **프록시가 `/healthz` 를 자체
응답**하는 것일 수도 있고, 배포 버전 대조는 하지 않았다. 라이브 상태는 변하므로 그날의 사실로 읽을 것.'''
assert d.count(old) == 1, 'plan retract'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok plan')

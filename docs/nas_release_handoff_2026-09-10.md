# NAS 인계서 — 2026-09-10 업데이트 게시 (0.2.108 / APK 0.2.16)

> 이 문서는 **NAS 에서 실행할 사람**을 위한 것이다. 작성자(작업용 Claude)는 **NAS 를 건드리지 않았다.**
> 아래 "확인" 항목은 **NAS 쪽에서 실제로 해야** 하는 것이고, 여기 적힌 값은 **이 PC 에서 만든 산출물의 사실**이다.

---

## 0. 한 줄 요약

앱에 **운영 공개키를 내장**했고, **테스트 쌍**(손으로 설치할 baseline / 서버가 배포할 target)을 만들었고,
target 의 **manifest 를 운영키로 서명**했다. **NAS 는 ① 서버앱 갱신 ② env 추가 ③ 아티팩트·manifest 게시**를 한다.

---

## 1. 출처 (source commit)

| 항목 | 값 |
|---|---|
| **target 빌드 커밋** | **`47a63b9`** — 트리 그대로 |
| **baseline 빌드 커밋** | **같은 `47a63b9`**, 버전 상수만 `0.2.107` / `0.2.15`(vc14) 로 두고 빌드 |
| 두 빌드의 차이 | ⚠️ **버전 값뿐이다.** 인앱 업데이트를 시험하려면 "낮은 것" 과 "높은 것" 이 둘 다 필요해서 만든 쌍이다 |

⚠️ **"버전 값뿐" 을 실제로 확인했다.** 두 APK 를 항목 단위로 비교하면 **내용이 다른 항목은 두 개**뿐이다 —
`AndroidManifest.xml`(versionCode·versionName)과 `classes3.dex`(`BuildConfig` 상수). 나머지 118개 항목은 **CRC 까지 같다.**
- 다만 **APK 파일 크기는 다르다**(11,518,126 vs 11,846,438). 압축 방식은 같고 **모든 항목의 원본 크기도 같으므로**,
  차이는 **내용이 아니라 아카이브 압축·배치**에서 온다. **크기가 같아야 한다고 기대하지 말 것.**

`47a63b9` 는 버전 상수가 **0.2.108 / 0.2.16(vc15)** 인 상태로 커밋돼 있다. baseline 은 그 상수만 낮춘 채 빌드했고
**커밋하지 않았다**(트리는 커밋 상태로 되돌려 두었다).

---

## 2. 산출물

### 2.1 사용자가 **손으로 설치**할 baseline

| 파일 | 크기 | SHA256 |
|---|---|---|
| `dist/GNLinkSetup-0.2.107.exe` | 4,339,712 | `51ba24b9f78eb0dee8dfe772410040293ff91a6f35ac5d66f7ad5258f5c5fa95` |
| `dist/GNLink-0.2.15.apk` | 11,518,126 | `9748a54eb0e845188558663665be8611c191b234a648e88e72c61866f6eaed99` |

### 2.2 **서버가 배포**할 target

| 파일 | 크기 | SHA256 |
|---|---|---|
| `dist/GNLinkSetup-0.2.108.exe` | 4,339,712 | `def1e344a7a5d928d9ce3477e392b63472801908d110d34bf429f822e2dc465c` |
| `dist/GNLink-0.2.16.apk` | **11,846,438** | `c4077363a87fcf245dc13e5479fa52293bf1709b8d1e6d0761a0e347924f2d4f` |

> ⚠️ 설치기(`GNLinkSetup-*.exe`)는 **업데이트 아티팩트가 아니다.** 인앱 업데이트가 받는 것은 **아래 3.2 의 개별 파일**이다.
> 설치기는 사용자가 baseline 을 손으로 깔 때만 쓴다.

---

## 3. NAS 가 게시할 것

### 3.1 manifest 와 서명 (이 PC 에 있음, 미추적 경로)

| 로컬 파일 | 크기 | SHA256 | 서버에서의 이름 |
|---|---|---|---|
| `.claude/rel/0.2.108/windows.manifest` | 1,533 | `b65e2198021780244b0dbf3f5c0dc2575227f2f34cf8472a9b1625a156855907` | `<REMOTE60_UPDATE_DIR>/windows.manifest` |
| `.claude/rel/0.2.108/windows.sig` | 128 | (서명 hex 자체) | `<REMOTE60_UPDATE_DIR>/windows.sig` |
| `.claude/rel/0.2.108/android.manifest` | 326 | `78737f8bce97096f65b9fcdef8a2b0cdfd987b21efbfd1a9bd631ae940d9e13d` | `<REMOTE60_UPDATE_DIR>/android.manifest` |
| `.claude/rel/0.2.108/android.sig` | 128 | (서명 hex 자체) | `<REMOTE60_UPDATE_DIR>/android.sig` |

⚠️ **바이트를 그대로 옮겨야 한다.** 서명은 **정확한 바이트**를 덮는다 — 편집기로 열어 저장하거나, 줄바꿈을 바꾸거나,
스크립트가 중간에 재직렬화하면 **서명이 깨지고 서버가 게시를 거부한다.** 전송 후 위 SHA256 을 **다시 확인**할 것.

### 3.2 아티팩트 — 🔴 **디렉터리 서버는 이 파일들을 서빙하지 않는다**

`server.js` 의 라우트는 **9개**(`/healthz`, `/api/signup`, `/api/login`, `/api/host/register`,
`/api/host/heartbeat`, `/api/hosts`, `/api/connect`, `/api/logs`, `/api/update/manifest`)이고
**아티팩트 다운로드 경로가 없다.** manifest 안의 URL 이 가리키는 파일은 **nginx 정적 경로로 따로 게시**해야 한다.

manifest 가 지목하는 URL(그대로 맞춰야 함):

| 서버 경로 | 로컬 원본 | 크기 | SHA256 |
|---|---|---|---|
| `/updates/0.2.108/GNLinkHost.exe` | `.claude/rel/0.2.108/payload/GNLinkHost.exe` | 658,432 | `f440afbac17a9b90c5dc3333cee18ecb11e25e96e9948b5cc9b07495a4437a7f` |
| `/updates/0.2.108/GNLinkStream.exe` | 〃 `GNLinkStream.exe` | 995,328 | `784180b15bb71fd7fd5fc19d5f02b987fb5225b900c94513e1e0da4d089caa65` |
| `/updates/0.2.108/GNLinkInputService.exe` | 〃 `GNLinkInputService.exe` | 226,304 | `1fd2aa569880cbe965ee9125cde6fd5c4b8cb08bcb4bbf807bd81fddfa69c8e0` |
| `/updates/0.2.108/GNLinkCapture.exe` | 〃 `GNLinkCapture.exe` | 127,488 | `253734df347adb2b88e718e3ffb42eadff4b643b3bd2cfe0ced8046a4feab723` |
| `/updates/0.2.108/GNLinkClient.exe` | 〃 `GNLinkClient.exe` | 705,536 | `3d79e981cd8e81fc93a7a939bcc694bceb2aead59dc7e3829841b6b2ef8664a0` |
| `/updates/0.2.108/GNLinkViewer.exe` | 〃 `GNLinkViewer.exe` | 864,256 | `3de194a9291399db0df8cf79f59db2827c6993d5ede4c1f666edd77ca63ce4ed` |
| `/updates/0.2.108/GNLinkUpdater.exe` | 〃 `GNLinkUpdater.exe` | 532,480 | `a42687a8072d531a13ce6786b70cdf9b96618d2f878b178667a9cfe05bdf2505` |
| `/updates/0.2.108/ui/shell.html` | 〃 `shell.html` | 12,648 | `15aa648adf28a6a27b62a4e0548c425d6339ff2bd45ce700d3c4c79aeee4b4a4` |
| `/updates/0.2.108/ui/macro.html` | 〃 `macro.html` | 10,996 | `f589f5df16d24fc8932d230da35f1661f0e97c1c63a6f2d43ad7a8fcc095529b` |
| `/updates/0.2.16/GNLink-0.2.16.apk` | `dist/GNLink-0.2.16.apk` | **11,846,438** | `c4077363a87fcf245dc13e5479fa52293bf1709b8d1e6d0761a0e347924f2d4f` |

- **읽기 권한**: 익명 GET 으로 받을 수 있어야 한다(클라이언트는 아티팩트 요청에 **자격증명을 붙이지 않는다**).
- **HTTPS 필수**: 클라이언트는 `https://` 가 아닌 아티팩트 URL 을 **거부**한다.
- 경로 이름을 바꾸려면 **manifest 를 다시 만들고 다시 서명해야 한다** — URL 이 서명 대상 안에 있다.

---

## 3.5 🔴 이 배포의 실제 구성 (확정본 — 바꾸지 말 것)

앞선 안내 중 *"외부 443 → upstream `http://…:29180`, 프록시가 종료하니 서버 TLS 는 비워 둔다"* 는 **이 서버에 해당하지 않는다.**
구성 선택지 중 하나였고, **이 서버는 다른 쪽을 이미 골랐다**:

| 항목 | 확정값 |
|---|---|
| nginx | `443` → **`https://127.0.0.1:29180`** (`proxy_ssl_verify off`) |
| 내부 29180 | **HTTPS 전용**, cert `/opt/gnlink/certs`, dropin `10-tls.conf` |
| 앱 경로 | `/opt/gnlink/remote60-directory` |
| 서비스 | **user-systemd `remote60-directory`** (linger) |
| UDP | **29181 observe · 29190 relay** — **443 뒤로 가지 않는다** |
| 인증서 | **wildcard `*.shotan.net` 이미 발급** — **DNS/TLS 변경 불필요·금지** |
| nginx body | **512M**(우리 상한 512KB 는 여유롭게 통과) |
| relay IP env | `175.207.45.151` 고정 |

- ⚠️ **서버 TLS 를 끄거나 upstream 을 http 로 바꾸지 말 것.** 이 배포는 **서버가 TLS 를 하고 프록시가 HTTPS 로 전달**한다.
- **loopback upstream 의 `proxy_ssl_verify off` 는 기존 구성으로 기록만 한다.** 이번 배포에서 **검증 우회 옵션을 추가·확대하지 않는다.**
  공인 HTTPS 클라이언트(제품)의 인증서 검증은 **정상 유지**다.
- **TLS env·dropin·다른 vhost 는 건드리지 않는다.** 이번에 추가하는 env 는 **4.2 의 네 개뿐**이다.

## 3.6 정적 아티팩트 경로 — **결정 사항**

`server.js` 에 아티팩트 라우트가 없으므로(3.2), **어디서 서빙할지는 NAS 가 정한다.** 조건만 적는다:

- **총 용량 ≈ 15.3 MiB** — Windows payload 9개 **3.94 MiB** + APK **11.30 MiB** + manifest·서명 **2,115 B**.
- **버전별 불변 경로**: `…/updates/0.2.108/…` · `…/updates/0.2.16/…`. **덮어쓰지 않는다** — manifest 가 가리키는 URL 이
  나중에 **다른 바이트**가 되면 서명은 그대로인데 내용이 달라진다. 새 릴리스는 **새 경로**다.
- **공개 URL 은 manifest 안의 값과 정확히 일치**해야 한다(3.2 표). 다르게 두려면 **manifest 재생성·재서명**이 필요하다.
- **익명 GET**, **HTTPS**, 읽기 전용.
- ⚠️ nginx 가 `/` 를 디렉터리 서버로 프록시하고 있다면, **정적 `location` 이 그 catch-all 보다 먼저** 잡혀야 한다.

## 4. 서버앱 갱신

### 4.1 무엇이 바뀌었나

`apps/directory/server.js` 는 이번 주기에 **두 커밋**에서 바뀌었다:

| 커밋 | 내용 |
|---|---|
| `acb3a2b` | 관측 엔드포인트 **광고**(`observe:{port,host}`)를 login·host/register·`/healthz` 에 추가 |
| `19a1f6c` | 관측 없는 요청의 **소켓 주소 폴백 제거** → **409**(`observation_required` / `observation_expired`) |

⚠️ **현재 운영 코드와 비교부터 할 것.** NAS 에 이미 손으로 넣은 수정이 있으면 **덮어쓰지 말고 병합**한다.
`automation/deploy_directory.ps1` 이 배포 경로를 안다(과거 이전 기록: `docs/history.md:7878-7883`).

### 4.2 필요한 env (델타)

| 변수 | 값 | 없으면 |
|---|---|---|
| `REMOTE60_UPDATE_DIR` | manifest·sig 를 둘 디렉터리 | `/api/update/manifest` 가 **503** |
| `REMOTE60_UPDATE_PUBLIC_KEY` | `8709ea70daac6464af4ed0fff1ed7489ec9e9a4a908d48babe5b62753242d9a872a4556df0c9ffa2e98dc70e6c553a624a8a235e8c4303488743f37ba240193e` | **게시 전 자체 검증을 건너뛴다** |
| `REMOTE60_DIR_OBSERVE_PORT` | 광고할 관측 UDP 포트(현 배포 기준 `29181`) | https 클라이언트가 **거부**된다 |
| `REMOTE60_DIR_OBSERVE_HOST` | 비워도 됨(클라가 접속한 host 사용) | — |

⚠️ **공개키를 넣는 이유**: 서버는 게시 직전 manifest 를 **스스로 검증**하고, 맞지 않으면 **서빙을 거부**한다.
소스 주석 그대로 — *"the first person to find out should not be a user"*. 넣지 않으면 그 그물이 없다.

### 4.2.1 코드 계약 (서버가 실제로 하는 것)

- 파일은 `REMOTE60_UPDATE_DIR` 아래 **`<platform>.manifest` + `<platform>.sig`** 두 개다(`server.js:84-87`, 핸들러 `:24-25`).
  `<platform>` 은 **`windows` 또는 `android`** — 그 외 query 는 **400**(화이트리스트).
- ⚠️ **파일은 detached 둘, 응답은 envelope 하나**다: 서버가 `{"manifest":…,"signature":…}` 로 **합쳐서** 돌려준다(`:42`).
  파일을 합쳐 두면 안 되고, 응답을 나눠 주어도 안 된다.
- `REMOTE60_UPDATE_PUBLIC_KEY` 가 설정돼 있으면 서버는 **서빙 직전 스스로 검증**하고, 맞지 않으면 **거부**한다(`:434-436`).
  **설정하지 않으면 그 검사는 아예 수행되지 않는다** — 없는 것과 통과한 것은 다르다.
- manifest 라우트는 **세션 또는 `x-host-token`** 을 요구한다. 무인증 요청은 **401** 이 정상이다.

### 4.3 재시작의 영향

- **호스트 토큰은 살아남는다**(디스크에 해시로 저장).
- **클라이언트 세션은 메모리에만 있다 → 재시작하면 전부 사라진다.** 사용자·폰은 **다시 로그인**해야 하고,
  로그 업로더는 401 을 받고 **자동으로 멈췄다가 재로그인 후 재개**한다. 짧은 중단이 생긴다.
- 관측(`observed`)도 메모리다. 재시작 직후 첫 하트비트는 **409** 를 받고 클라이언트가 **스스로 한 번 재시도**한다.

### 4.4 🔴 구버전 클라이언트 호환 — 409 의 영향

`19a1f6c` 이후 **관측 없는 하트비트/connect 는 409 로 거부**된다.

| 클라이언트 | 결과 |
|---|---|
| 이번 빌드(0.2.107 이상) | 관측을 보내고, 409 를 받으면 **재관측 후 1회 재시도** — 정상 |
| 구버전 | 관측을 **보내기는 한다**(예전부터 보냈다). 다만 **광고된 포트가 `http+1` 이 아니면** 엉뚱한 포트로 보내 **409 가 반복**된다 |
| 구버전 + https | 애초에 https 를 파서가 거부하거나 444 로 보낸다 — **연결되지 않는다** |

→ **`REMOTE60_DIR_OBSERVE_PORT` 를 `http 포트 + 1` 로 두면 구버전도 그대로 동작한다.** 현 배포는 `29180`/`29181` 이므로
**기본값(= listen 포트)** 그대로가 안전하다. 다른 포트로 옮기려면 **모든 클라이언트가 이번 빌드 이상**이어야 한다.

---

## 5. 배포 순서 (이 순서를 지킬 것)

1. **원본 백업**: 현재 `server.js`, 현재 env(유닛 파일), 현재 `REMOTE60_UPDATE_DIR` 내용. **되돌릴 수 있어야 한다.**
2. **아티팩트 먼저** 전부 올리고 **크기·SHA256 을 전량 검증**한다(3.2 표).
3. 서버앱 갱신 + env 추가 → 재시작.
4. **manifest·sig 를 마지막에**, **원자적으로** 놓는다(임시 이름으로 쓰고 `mv` 로 교체 — 반쯤 쓰인 파일이 보이면 안 된다).
   ⚠️ 순서가 뒤집히면 **아직 없는 파일을 가리키는 manifest** 가 잠깐 유효해지고, 그 사이에 확인한 클라이언트는
   **다운로드에 실패**한다.
5. 확인(6절) 후 사용자에게 알린다.

**rollback**: manifest·sig 를 먼저 치우면(이름 변경이면 충분) 인앱 업데이트는 **즉시 멈춘다** — 아티팩트는 남아 있어도 무해하다.
서버앱은 백업본으로 되돌리고 재시작한다.

---

## 6. 게시 후 확인 (NAS 에서)

```
# 1) metadata 광고 — 이 셋이 같은 값을 내야 한다
curl -s https://rem.shotan.net/healthz            # observe:{port,...} 가 있어야 한다
curl -s -X POST https://rem.shotan.net/api/login  -H 'content-type: application/json' -d '{"id":"...","pw":"..."}'
curl -s -X POST https://rem.shotan.net/api/host/register -H 'content-type: application/json' -d '{...}'

# 2) manifest — 인증이 필요하다(세션 또는 x-host-token). 무인증이면 401 이 정상이다.
curl -s -o /dev/null -w '%{http_code}\n' 'https://rem.shotan.net/api/update/manifest?platform=windows'   # 401 기대
curl -s -H "authorization: Bearer <세션>" 'https://rem.shotan.net/api/update/manifest?platform=windows' | head -c 120

# 3) 아티팩트 — 익명으로 받아지고 해시가 맞아야 한다
curl -s https://rem.shotan.net/updates/0.2.108/GNLinkHost.exe | sha256sum

# 4) 체인·헤더 — 토큰이 응답/리다이렉트에 새지 않는지
curl -sI https://rem.shotan.net/healthz
```

- **기존 서비스가 그대로인지**: HTTP 라우트 9개 + **UDP 관측(29181)** + **UDP 릴레이(29190)** 가 살아 있어야 한다.
  ⚠️ **UDP 는 443 뒤로 가지 않는다.** 리버스 프록시로 옮길 수 없고, 포트포워딩을 걷으면 제품이 죽는다.
- **manifest 응답이 `{manifest, signature}` envelope** 인지(클라이언트는 파생 URL 에 이 모양을 기대한다).

---

## 7. 사용자 실기 안내 (이 순서로)

1. **baseline 을 손으로 설치**: `GNLinkSetup-0.2.107.exe` 실행, 폰에 `GNLink-0.2.15.apk` 설치.
   (⚠️ 기존 APK 와 **같은 signer** 이므로 덮어쓰기 설치가 된다.)
2. 앱에서 **서버 주소 하나**(`https://rem.shotan.net`)로 로그인.
3. **업데이트 확인** — 0.2.108 / 0.2.16 이 있다고 나와야 한다.
4. 설치 진행. Windows 는 **UAC 한 번**, Android 는 **"알 수 없는 앱 설치" 허용**을 물을 수 있다.
5. 끝난 뒤 **버전 표시**가 0.2.108 / 0.2.16 인지 확인.

---

## 8. 🔴 이 인계서가 보장하지 않는 것

- **인앱 업데이트가 실제로 끝까지 도는 것** — 이 PC 에서는 **서버 없이** 검증할 수 없는 단계가 있다.
  검증된 것은 **서명이 세 검증기에서 통과한다**는 것까지다(9절).
- **UI 응답성** · **인증 헤더가 실제로 나가는 모양** · **https→http 강등 거부의 실경로** — **이번 검사에서 미실행**.
- **현 운영 backend 의 버전·구성** — 미확인. 2026-09-09 실측으로 확인된 것은
  **TLS GET 성공** 과 **`/healthz` 에 `observe` 가 없다**는 것까지다(원인은 구버전일 수도, 프록시 자체 응답일 수도 있다).
- **아티팩트 정적 경로의 존재** — 아직 없다. **이 인계서가 요청하는 새 작업**이다.

## 8.5 NAS 가 확인해서 돌려줘야 하는 것 2건

이 PC 에서는 알 수 없고, **둘 다 "설정했다" 가 아니라 "확인했다" 로** 답해야 한다.

1. **서버 자체 검증이 실제로 켜져 있는가** — `REMOTE60_UPDATE_PUBLIC_KEY` 가 **실행 중인 프로세스의 환경에** 있는지.
   유닛 파일에 적는 것과 프로세스가 갖는 것은 다르다(재시작을 해야 반영된다). 확인법: manifest 를 **한 바이트 고쳐** 두고
   요청하면 **500 + 로그에 `refusing to serve`** 가 나와야 한다. 나오지 않으면 **검사가 꺼져 있는 것**이다.
   ⚠️ 확인 뒤 **원본 manifest 로 되돌릴 것.**
2. **공개키 128자가 세 곳에서 같은가** — 앱에 박힌 값 · 서버 env 값 · `public_xy.hex`.
   앱 쪽 두 곳은 이 PC 에서 대조했다(9절). **서버 env 는 NAS 만 볼 수 있다.**

## 9. 이 PC 에서 검증된 것

| 검증 | 결과 |
|---|---|
| 운영키 = 파일값 = Codex 지정값 | `public_xy.hex` 128자, SPKI `f6121bca…c95b49` 일치 |
| 앱에 박힌 값 = 파일값 | **C++ `update_manifest.cpp` MATCH** · **Kotlin `build.gradle.kts` MATCH**(문자 단위 비교). 서버 env 는 **8.5-2 로 NAS 확인** |
| **C++ 정식 `default_verifier()`** 로 windows.manifest | **Ok**, 1바이트 변조 시 **SignatureInvalid** |
| **Node(서버가 쓰는 것)** 로 windows/android manifest | 둘 다 **Ok**, 변조 시 **SignatureInvalid** |
| 서명 직후 자체 검증(공개키로) | windows·android 둘 다 **True** |
| 설치기 payload 바이트 대조 | 이전 후보에서 9/9 일치(같은 방식) |
| APK signer | 기존과 **동일 인증서**, `versionCode` 14 → 15 |
| 전체 회귀 | C++ 62 스위트 `^PASS` 1733 · JS 335 · Android 68 |

⚠️ **Kotlin 검증기로는 실제 android.manifest 를 통과시켜 보지 않았다.** Kotlin 테스트는 platform=windows 로 고정돼 있어
android manifest 를 주면 `WrongPlatform` 이 난다(서명 문제가 아니다). Android 앱은 **같은 공개키 상수**를 갖고,
공유 벡터 검증은 통과한다 — **그러나 실제 릴리스 manifest 를 Kotlin 으로 검증한 기록은 없다.**

## 10. 하지 않은 것

**NAS 무접속.** 실서버 게시·설치·서비스 중지·nginx/DNS/TLS 변경·push **전부 없음.**
개인키·DPAPI 블롭·비밀번호·세션 토큰은 **이 문서·NAS·Git·로그 어디에도 없다.** 나간 것은 **공개키·서명·manifest·공개 파일뿐**이다.
**서명키 독립 백업은 여전히 미완료**이며(별건), 이번에 키를 **새로 만들지 않았다.**

# NAS 인계서 — 0.2.109 (Windows 단독 후속 게시)

2026-09-10. `docs/nas_release_handoff_2026-09-10.md` 의 **후속**이다. 그 문서의 구성(nginx 443 → `https://127.0.0.1:29180`,
정적 아티팩트 경로, env, 409 호환)은 **그대로 유효**하고, 여기서는 **달라지는 것만** 적는다.

## 0. 한 줄 요약

**Windows `0.2.109` 하나만 게시한다.** manifest·서명·아티팩트 9개를 올리고 끝이다.
**서버 재시작 불필요**(근거는 4절). **Android 는 손대지 않는다.** **기존 0.2.107·0.2.108·APK 파일은 전부 그대로 둔다.**

## 1. 출처

| 항목 | 값 |
|---|---|
| 릴리스 commit | `9fed965` (`chore(release): 0.2.109 -- Windows only, and no new baseline`) |
| 담긴 수정 | `422196b` 캐시 토큰 → `/healthz` 광고 취득 · `b4d2a7a` 업로더 종료 계약 |
| 브랜치 | `refactor/viewer-split` (**push 하지 않았다** — 이 PC 로컬) |
| 빌드 | Release 전체 재빌드, 오류 0 |

### 1.1 왜 Windows 하나뿐인가

- **Android 는 `0.2.16`/versionCode 15 게시 그대로.** 두 수정 모두 **Android 파일을 하나도 건드리지 않는다.**
  바뀐 게 없는데 버전을 올리면 **그 번호가 아무것도 증명하지 않게 된다.**
- **새 baseline 불필요.** 이미 설치된 **0.2.108 이 출발점**이다.
- 🔴 *"0.2.108 은 스스로 업데이트할 수 없다"* 는 **근거 없는 추론이었고 철회됐다.**
  `start_update_check()`(`apps/native_poc/src/host_app_main.cpp:799`) 는 `current_update_endpoint()` 로
  **캐시된 url·host token** 을 받아 **https 로 독립 호출**한다. **`directory=online` 도 하트비트 성공도 요구하지 않고**,
  트레이 메뉴는 **조건 없이** 부른다. *"연결이 막혔다"* 와 *"업데이트가 막혔다"* 는 **서로를 필요로 하지 않는다.**

## 2. 이 PC 에 있는 것 (미추적 경로)

`D:\remote\remote\.claude\rel\0.2.109\` — **`.claude\rel\0.2.108\` 은 읽지도 쓰지도 않았다.**

| 파일 | 크기 | sha256 |
|---|---|---|
| `windows.manifest` | 1,534 | `743fbb1f19f16c024cec89797a1c5259aa1d414c16f186018cf124be43e1b26a` |
| `windows.sig` | 128 | `85fdc3c76fce72cf803e0b786516e7ccd9132904af1bbbf856b3ea0e8c696f14` |

서명값(파일 내용) = `0de7ca480a6de20b47bac4990a95a4640c399b4ec4940e4ba02e628ab9d075e06a9784987f271a3047e234ee8782bd75415babab463ae627f58516b510a30c88`

### 2.1 아티팩트 — `payload/` 아래

| 게시 경로 (URL) | 로컬 파일 | 크기 | sha256 |
|---|---|---|---|
| `/updates/0.2.109/GNLinkHost.exe` | `payload/GNLinkHost.exe` | 660,480 | `a02e8ccb3fe2c7e8fd5b58df5311b01322579fea2a8c74f2a981ea21c734ee62` |
| `/updates/0.2.109/GNLinkStream.exe` | `payload/GNLinkStream.exe` | 1,000,448 | `797af6e2213614c7ca802e019738b75d5526fdb03d58fd178dd11e66eae8873d` |
| `/updates/0.2.109/GNLinkInputService.exe` | `payload/GNLinkInputService.exe` | 226,304 | `1fd2aa569880cbe965ee9125cde6fd5c4b8cb08bcb4bbf807bd81fddfa69c8e0` |
| `/updates/0.2.109/GNLinkCapture.exe` | `payload/GNLinkCapture.exe` | 127,488 | `253734df347adb2b88e718e3ffb42eadff4b643b3bd2cfe0ced8046a4feab723` |
| `/updates/0.2.109/GNLinkClient.exe` | `payload/GNLinkClient.exe` | 707,584 | `210e514060a4eb46a46703e8bea1cbaf1bf1ae0ce76e170a5f81c31de8cd7047` |
| `/updates/0.2.109/GNLinkViewer.exe` | `payload/GNLinkViewer.exe` | 867,328 | `9a5937df1d6b4f334d1107dad17a445256f17fda44ac16b09d6f4de91deb29df` |
| `/updates/0.2.109/GNLinkUpdater.exe` | `payload/GNLinkUpdater.exe` | 532,480 | `a333df2726b67d254b27a7cb17a2d5c6ea142b69236eeb65fe444d113d8f4fa5` |
| `/updates/0.2.109/ui/shell.html` | `payload/ui/shell.html` | 12,648 | `15aa648adf28a6a27b62a4e0548c425d6339ff2bd45ce700d3c4c79aeee4b4a4` |
| `/updates/0.2.109/ui/macro.html` | `payload/ui/macro.html` | 10,996 | `f589f5df16d24fc8932d230da35f1661f0e97c1c63a6f2d43ad7a8fcc095529b` |

합계 약 **4.0 MiB**. `shell.html`·`macro.html` 은 **0.2.108 과 같은 파일**이지만, **경로가 서명 대상 안에 있으므로**
`/updates/0.2.109/ui/` 아래에 **다시 놓아야 한다**(0.2.108 것을 가리키게 두면 안 된다).

- ⚠️ **스테이징 배치는 URL 과 같은 모양이어야 한다.** `ui/` 두 개는 **하위 디렉터리**다.
  평면으로 두면 exe 7개는 받아지고 **html 2개만 404** 가 되며, 증상은 **다운로드 도중 실패**라 **서버 문제처럼 보인다.**
- ⚠️ **경로 이름을 바꾸려면 manifest 를 다시 만들고 다시 서명해야 한다** — URL 이 서명 대상 안에 있다.
- **올린 뒤 대조**: manifest 의 `artifact=` 9줄을 **실제 파일의 크기·SHA256 과 다시 맞춰 볼 것.**
  ⚠️ **서명 검증은 이것을 대신하지 못한다** — 서명은 *"문서가 서명 이후 바뀌지 않았다"* 만 말하고,
  *"문서가 실제로 존재하는 파일을 가리킨다"* 는 말하지 않는다. 0.2.108 에서 실제로 그 둘이 갈렸다.

## 3. 무엇을 어디에 놓나

1. **아티팩트 9개** → 정적 경로 `/updates/0.2.109/…` (0.2.108 과 같은 방식, **버전별 불변 경로**)
2. **`windows.manifest` · `windows.sig`** → `<UPDATE_DIR>/windows.manifest`, `<UPDATE_DIR>/windows.sig` **덮어쓰기**
   - ⚠️ **`android.manifest` · `android.sig` 는 건드리지 않는다.** 0.2.16 게시가 그대로 유효하다.
   - ⚠️ **교체 전에 기존 두 파일을 백업**해 둘 것(롤백은 그 둘을 되돌리는 것이 전부다 — 6절).

## 4. 🔴 서버 재시작은 **필요 없다**

`apps/directory/server.js` `handleUpdateManifest` 는 **요청마다 디스크에서 읽는다**:

```js
manifest = fs.readFileSync(path.join(UPDATE_DIR, `${platform}.manifest`), 'utf8');   // :425
const trusted = String(process.env.REMOTE60_UPDATE_PUBLIC_KEY || '');                 // :434
```

- 캐시가 없다. **파일을 바꾸면 다음 요청부터 새 것이 나간다.**
- 그러므로 **manifest·서명 교체만으로 끝**이고, **재시작하지 않는다.**
- ⚠️ **예외는 env 다.** `REMOTE60_UPDATE_PUBLIC_KEY` · `REMOTE60_UPDATE_DIR` 을 **바꾸는 경우에만** 재시작이 필요하다.
  이번 게시는 **env 를 바꾸지 않으므로** 해당 없음.
- 서버는 내보내기 전에 **스스로 서명을 검증**하고, 실패하면 **500 + `published manifest did not verify`** 를 낸다(:439).
  즉 **잘못 올라간 manifest 는 조용히 나가지 않는다.**

## 5. 이 PC 에서 검증된 것

| 항목 | 결과 |
|---|---|
| 수정이 산출물에 실제로 들어갔는가 | `/healthz` **0.2.108 없음 → 0.2.109 있음**(`GNLinkStream.exe`) · `stop budget spent` **없음 → 있음**(`GNLinkHost.exe`·`GNLinkClient.exe`) |
| manifest ↔ 실제 파일 | **9/9 일치**(크기·SHA256, 스테이징 경로 그대로) |
| Node `update_manifest.js` (서버 구현) | **실제 manifest** Ok · `version=0.2.109` · artifact 9 · **1바이트 변조 거부** · **다른 키 거부** |
| Python `cryptography`(OpenSSL) | **실제 manifest** Ok · raw r‖s 64B · **1바이트 변조 거부** |
| C++ `default_verifier()`(제품에 박힌 키) | 같은 키로 서명한 **fixture** Ok · 변조 거부 · 다른 키 거부 (86 checks) |
| 전체 회귀 | C++ **63 스위트 `^PASS` 1762** · JS **335** · 실패 1건 `udp_control_e2e`(**이번 변경 이전부터 red**) |

⚠️ **C++ 은 `windows.manifest` 자체가 아니라 fixture 를 검증했다.** *"제품에 박힌 키가 이 키의 서명을 받아들인다"* 는
증명됐고, *"제품이 이 문서를 읽었다"* 는 아니다. 그 구분을 지운 채 "3중 검증" 이라고 읽지 말 것.

⚠️ `GNLinkHost.exe` 에서 `/healthz` 를 찾다가 **없다고 볼 뻔했다.** `HostAgent` 는 **`GNLinkStream.exe`** 안에서 돈다 —
`GNLinkHost.exe` 는 그 **감독자**다. **엉뚱한 바이너리를 보고 판단하지 말 것.**

## 6. 롤백

**아티팩트는 지우지 않는다.** `/updates/0.2.109/` 는 그대로 두고 **`<UPDATE_DIR>/windows.manifest`·`windows.sig` 를
백업본으로 되돌리면** 끝이다 — 다음 요청부터 0.2.108 이 나간다. **재시작 불필요.**
버전별 경로가 불변이므로 0.2.108 아티팩트도 제자리에 있다.

## 7. 게시 후 확인 (NAS 에서)

```sh
# 1) manifest — 인증이 필요하다(세션 또는 x-host-token). 무인증 401 은 정상이다.
#    version=0.2.109 와 artifact 9줄이 나와야 한다.

# 2) 아티팩트 — 익명으로 받아지고 해시가 2.1 표와 맞아야 한다. ui/ 두 개를 반드시 포함할 것.

# 3) 서버가 스스로 거부하는지 — manifest 1바이트를 바꾸면 500 + "published manifest did not verify",
#    확인 뒤 즉시 원복. (REMOTE60_UPDATE_PUBLIC_KEY 가 실행 중인 프로세스에 실제로 있는지도 이걸로 드러난다.)
```

## 8. 이 인계서가 보장하지 않는 것

- **실제 업데이트가 끝까지 도는 것.** 이 PC 에서 확인한 것은 **문서·서명·파일의 일치**까지다.
- **`REMOTE60_UPDATE_PUBLIC_KEY` 가 실행 중인 프로세스에 들어 있는지** — 7절 3번으로만 드러난다.
- **캐시 토큰 결함(`422196b`)이 실기에서 고쳐지는 것** — 회귀는 가짜 디렉터리 상대로만 확인했다.
  실기 판정은 **업데이트 후 Host 가 `directory=online` 이 되는지**로만 가능하다.
- **BEX64/c0000409 가 사라지는 것.** `b4d2a7a` 는 **독립적으로 옳은 수정**이고, 그 크래시의 원인이라고 **단정하지 않았다**
  (해당 빌드의 PDB 가 없어 offset 미해석, 다른 terminate 경로 미배제).

## 9. 작업용 Claude 가 하지 않은 것

게시·설치·라이브 Host 실행/중지/교체·캐시 삭제·`git push`·NAS 접속·nginx/DNS/운영 인증서 변경 **없음**.
APK 재생성 **없음**. 새 서명키 생성 **없음**. `.claude/rel/0.2.108` **불변**.

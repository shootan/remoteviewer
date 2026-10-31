# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 496) 2026-09-10 `0.2.109` 후보 — **Windows 단독**, 새 baseline 없음
`b4d2a7a`(#495) 까지를 담아 `kProductVersion` 만 `0.2.108` → `0.2.109`. **Release 전체 재빌드, 오류 0.**

**왜 Windows 하나뿐인가 (검증용이 Codex 확인 후 확정)**
- **Android 는 `0.2.16`/vc15 게시 그대로.** `422196b`·`b4d2a7a` 는 **Android 파일을 하나도 건드리지 않았다.** 바뀐 게 없는데 버전을 올리면 **그 번호가 아무것도 증명하지 않게 된다.**
- **새 baseline/target 쌍 불필요.** 이미 설치된 **0.2.108 이 출발점**이다.
- 🔴 **정정 — "0.2.108 은 스스로 업데이트할 수 없다" 는 근거가 없었다.** 검증용이 철회했고 코드가 그렇다: `start_update_check()`(`host_app_main.cpp:799`) 는 `current_update_endpoint()` 로 **캐시된 url·host token** 을 받아 **https 로 독립 호출**한다. **`directory=online` 도 하트비트 성공도 요구하지 않고**, 트레이 메뉴도 **조건 없이** 부른다.
  ⚠️ *"연결이 막혔다"* 에서 *"업데이트도 막혔다"* 를 **추론으로 이어붙인 것**이었다. 두 경로는 서로를 필요로 하지 않는다.

**수정이 실제로 들어갔는지 — 바이너리에서 확인**(빌드 로그가 아니라 산출물에서)
| 문자열 | 0.2.108 | 0.2.109 | 어디에 |
|---|---|---|---|
| `/healthz` | **없음** | **있음** | `GNLinkStream.exe`(HostAgent 가 사는 곳) · `GNLinkViewer.exe` |
| `stop budget spent` | **없음** | **있음** | `GNLinkHost.exe` · `GNLinkClient.exe`(업로더 소유자 둘) |
| 운영 공개키 128자 | 있음 | **있음** | `GNLinkHost.exe` |
⚠️ 처음엔 `GNLinkHost.exe` 에서 `/healthz` 를 찾다가 **없다고 볼 뻔했다.** `HostAgent` 는 **`GNLinkStream.exe`** 안에서 돈다 — `GNLinkHost.exe` 는 그 감독자다. **엉뚱한 바이너리를 보고 "수정이 빠졌다" 고 할 뻔한 것**이 이 표를 만든 이유다.

**산출물**
| 파일 | 크기 | sha256 |
|---|---|---|
| `GNLinkHost.exe` | 660,480 | `a02e8ccb3fe2c7e8…` |
| `GNLinkStream.exe` | 1,000,448 | `797af6e2213614c7…` |
| `GNLinkInputService.exe` | 226,304 | `1fd2aa569880cbe9…` |
| `GNLinkCapture.exe` | 127,488 | `253734df347adb2b…` |
| `GNLinkClient.exe` | 707,584 | `210e514060a4eb46…` |
| `GNLinkViewer.exe` | 867,328 | `9a5937df1d6b4f33…` |
| `GNLinkUpdater.exe` | 532,480 | `a333df2726b67d25…` |
| `ui\\shell.html` | 12,648 | `15aa648adf28a6a2…` (불변) |
| `ui\\macro.html` | 10,996 | `f589f5df16d24fc8…` (불변) |
| `.claude/rel/0.2.109/windows.manifest` | 1,534 | `743fbb1f19f16c02…` |
| `dist/GNLinkSetup-0.2.109.exe` | 4,352,000 | `f52ba327c6a897e1…` (수동 설치용 예비, **manifest 대상 아님**) |

**manifest ↔ 실제 파일 대조 9/9 일치.** ⚠️ 서명은 *"문서가 서명 이후 바뀌지 않았다"* 만 말한다 — *"문서가 존재하는 파일을 가리킨다"* 는 말하지 않는다(#493 에서 실제로 갈렸다).
**기존 파일 불변**: `0.2.107`·`0.2.108`·APK 전부 손대지 않았고, `.claude/rel/0.2.108` 은 **읽지도 쓰지도 않았다.**

**아직 안 된 것**: **서명 미실행** — 서명 스크립트가 권한 게이트에서 차단됐다. 사용자 승인 후 `windows.sig` 생성·자체 검증·3중 검증을 하고, 그 다음이 NAS 인계다.
**하지 않은 것**: 게시·설치·라이브 실행·프로세스 중지·캐시 삭제·push·NAS 변경 없음. APK 재생성 없음.
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok history 496')

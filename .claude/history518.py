# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 518) 2026-09-11 🔴 **알림은 배선됐고 행동은 배선되지 않았다** — Client 에 누를 것이 없었다
`0.2.114` 는 *"Client 가 업데이트를 물어본 적이 없다"* 를 고친 릴리스였다. 물어보기는 고쳐졌는데,
**답을 받은 뒤 할 수 있는 일이 없었다.**

**무엇이 문제였나** (Codex 발견, 검증용 확인, 저도 소스에서 재확인)
- `ui/shell.html` 의 `<button>` 전수: `signIn` `signOut` `refresh` `openSettings` `openLog`
  `closeSettings` `saveSettings` — **업데이트 버튼 0개**
- 페이지에서 `type:'update'` 를 보내는 곳 **0건**
- `client_shell_main.cpp:1037` 에 **수신 handler 는 있다.** 처음부터 있었다. **아무도 보내지 않았다.**
- 결과: 화면에 *"새 버전 0.2.115 이 있습니다."* 만 뜨고 **누를 것이 없다.**

**🔴 이건 이번 라운드 내내 경계한 바로 그 실패다.** *"코드는 있는데 아무도 안 부른다"* —
`log_upload_stop()` · `request_process_stop` · `GNLinkUpdater.exe` 의 `payloadNames` 와 같은 모양이다.
그런데 이번엔 **그것을 검사하겠다고 만든 테스트가 통과시켰다.** `client_update_flow_test` 는
`start_update_check` **호출 지점**을 셌고, **행동 경로는 보지 않았다.** 소스 grep 은 이 틈을 볼 수 없다 —
**양쪽이 다 있고, 없는 것은 둘을 잇는 사실뿐**이기 때문이다.

**고친 것**
- `shell_update_available_json` / `shell_update_cleared_json` / `shell_update_busy_json`
  (`client_shell_bridge.cpp`) — **버전이 필드로 간다.** 상태 줄의 한국어 문장을 파싱해 버전을 얻는
  페이지는 문구가 바뀌는 순간 깨진다.
- `shell.html` 에 **상단 배너 + `업데이트` · `나중에`**. `업데이트` → `send({type:'update'})` →
  기존 `:1037` → `start_client_update`. **`나중에` 는 연기일 뿐** — 로그인·목록·설정을 건드리지 않는다.
- **중복 기동 0**: 페이지가 버튼을 죽이고, **네이티브가 `gUpdateStarting` 으로 실제로 막는다.** 페이지에서만
  막으면 이미 날아간 두 번째 메시지를 못 막는다. 실패하면 **버튼을 돌려준다** — 죽은 채로 두면 알림만 있고
  할 수 있는 게 없는, 이 변경이 없애려던 바로 그 상태가 된다.
- **로그아웃은 제안을 회수한다** — 그 답은 물어본 세션의 것이고, 다음 사람에게 넘길 것이 아니다.
- ⚠️ **401·실패는 원래부터 표시되지 않는다** — `ShellUpdateNotice::show` 가 `UpdateAvailable` 에서만
  참이다. 이번에 바꾸지 않았고, 테스트로 확인했다.

**검증 — 소스를 읽지 않는다**
`client_update_ui_test` **17 PASS** (`remote60_client_update_ui_test`).
**실제 WebView2** 에 **배포되는 `ui/shell.html` 그 파일**을 띄우고, **제품 메시지 빌더**가 만든 문서를
넣고, **렌더된 요소를 DOM 으로 클릭**해서, **`{"type":"update"}` 가 실제로 나오는 것**을 확인한다.
스크린샷: `build-local/apps/native_poc/Release/client_update_ui.png` — 배너에 `새 버전 0.2.115 이
있습니다.` 와 두 버튼이 보인다.
양성/음성 모두: 제안 전 클릭 무응답 · 표시 · 버전 명시 · 클릭 송신 · busy 중 비활성 · 실패 후 복귀 ·
`나중에` 후 로그인 화면 불변 · cleared 회수 · 로그아웃 회수 · 회수 후 클릭 무응답.
회귀: `client_shell_bridge_test` 29 PASS · `client_update_flow_test` 23 PASS.

**부수 발견 2건**
- 테스트가 **빌드 디렉터리의 `ui/shell.html` 사본을 먼저 읽어 한 판 낡은 페이지를 검사할 뻔했다.**
  저장소 사본 우선으로 바꿨다. *"배포되는 그 파일"* 이 아니면 이 테스트는 의미가 없다.
- 첫 렌더의 문구가 깨졌다 — **제 테스트 타깃에 `/utf-8` 이 빠진 것**이고 제품은 `:1353` 에서 붙는다.
  **제품 결함이 아니다.** 타깃에 추가했다.
- WebView2 환경 생성이 `0x800401f0`(`CO_E_NOTINITIALIZED`) 로 실패했다 — 이번 세션 등록 경로와
  **같은 종류**다. 테스트에 아파트먼트를 붙였다.

**아직 아닌 것**: 실서버 확인 → 이 메시지 발생은 **미검증**이다. 계정과 네트워크가 필요하고 별도 단계다.
여기서 증명한 것은 **없던 절반** — 메시지가 도착했을 때 버튼이 있고, 누르면 네이티브가 기다리던 것이
나간다는 것뿐이다.
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('history ok')

p2 = 'docs/작업목록.md'
t = io.open(p2, encoding='utf-8').read()
old = u'0.0.12 [x] **`0.2.115` 게시'
new = (u'0.0.13 [~] **Client 에 업데이트 버튼이 없었다 — 수정 완료, 실서버 미검증** (history #518) — '
       u'알림만 뜨고 누를 것이 없었다. `shell.html` 에 배너+`업데이트`/`나중에`, 구조화 메시지, '
       u'네이티브 중복 기동 차단, 로그아웃 회수. **검증은 실제 WebView2 에 배포 `shell.html` 을 렌더하고 '
       u'DOM 클릭**(`client_update_ui_test` 17 PASS + 스크린샷). ⚠️ **`0.2.115` 에도 이 버튼이 없다** — '
       u'게시분은 불변으로 두고 **다음 릴리스**로 전달. 남은 것: 실서버 확인 → 팝업\n'
       u'0.0.12 [x] **`0.2.115` 게시')
assert t.count(old) == 1, 'tasklist anchor'
io.open(p2, 'w', encoding='utf-8', newline='').write(t.replace(old, new, 1))
print('tasklist ok')

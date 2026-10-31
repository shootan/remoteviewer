# report lockscreen-keys r1 — 잠금 화면 키 입력 불가 조사 (작업용1 remote#rqxbj3jb, 2026-10-21)

task t-nr6r1jja / rev r1 / kind=investigate. 제품 코드 수정 없음, 버전·게시·설치본·서비스·세션 조작 없음, SendInput 미사용.
고정: 소스 `main` 5063cd9 (0.2.161, worktree `.claude/worktrees/main-stability-integration`, 깨끗함), 대조 0.2.159 a622f04, 0.2.160 c1956be.
환경: `qwinsta` → console session 1 shotan 활성, RDP 없음. 로그는 이 PC의 `host_app.log`, `C:\ProgramData\GNLink\secure_input.log`, 이벤트 로그를 직접 읽음.
**NAS viewer.log 는 직접 보지 못했다**: `claude@` 계정이 이 PC의 모든 키로 거부됐고, `gnlink@` 시도는 권한 분류기가 막았다(재시도 안 함). 그래서 뷰어 쪽 사실 3건은 아래 §5 의 grep 으로 확인을 요청한다.

## 1. 결론 (요약)
회귀 아님. 0.2.159 에도 있던 기존 동작 3개가 겹쳤다.
1. **키**: 회사 PC 뷰어가 호스트 IME 모드(`REMOTE60_HOST_IME` opt-in)였다(추정). 그래서 키가 kind 5 InputEvent 가 아니라 **PhysicalKey** 로 나갔고, 호스트는 비-Default 데스크톱(잠금/UAC)에서 PhysicalKey 를 **조용히 버린다**. ACK 는 그대로 보내므로 양쪽 로그 모두 정상으로 보인다. "key down 0건" 은 PhysicalKey/Text 가 `eventKind=0` 으로 기록돼서 나온 집계 착시일 가능성이 높다.
2. **Ctrl+Alt+U**: 사용자 세션 Host 에서는 기존 sealed unlock 으로 간다. 이 경로는 **연결 끊긴(disconnected) 잠금 세션을 WTSConnectSession 으로 콘솔에 붙이는 용도**다. 콘솔에서 활성 상태로 잠긴 세션(오늘 상황)은 서비스가 `RejectedPolicy` 로 거절하고 로그도 남기지 않는다. 뷰어는 결과를 stdout 에만 쓰고 창은 띄우지 않는다. 처음 입력한 비밀번호는 저장되는데 RejectedPolicy 에서는 지우지 않으므로, 그 뒤로는 프롬프트가 다시 뜨지 않는다.
3. **툴바 'Windows 로그인'**: `DecidePreloginCred` 가 `kCaptureFlagPreloginV1`(로그인 전 worker 만 세움)일 때만 열린다. 잠금 화면에서는 숨겨진다.
4. **화면 종류**: Windows Update 재시작 뒤 ARSO 로 자동 로그인된 다음 잠긴 화면이다(로그인 전 화면 아님). GNLink 의 원격 재시작은 ARSO 를 쓰지 않으므로 로그인 전 화면이 된다. 그래서 사용자는 두 화면을 모두 만난다.

## 2. 막힌 지점 — 코드 (5063cd9)
| # | 지점 | 근거 |
|---|---|---|
| K1 | 뷰어 opt-in: env `REMOTE60_HOST_IME` + 호스트 v2 → `ImeStateRequest` → `kMsgHostImeActivate` → imeMode=2 | `viewer_input_forward.cpp:36-42`, `viewer_control_client.cpp:375`, `:887` |
| K2 | imeMode=2 이면 WM_KEYDOWN 이 `forward_physical` → `enqueue_physical_key`(PhysicalKey) | `viewer_window_proc.cpp:527-528`, `:92` |
| K3 | **호스트 PhysicalKey 게이트**: `!interactive_desktop_is_default()` 면 inject 안 함, 로그 없음, `send_input_ack` 는 보냄 | `host_control_session.cpp:1014`, `:1049` |
| K4 | 뷰어 input-timing 은 PhysicalKey/Text 를 `eventKind=0` 으로 남김(actionKind 로 구분) | `viewer_control_client.cpp:942-946` |
| K5 | 뷰어 prelogin 게이트는 잠금 화면 pong 에서 키를 막지 않음(roleKnown 은 매 pong 마다 갱신됨) | `prelogin_input_gate.hpp:37`, `viewer_control_client.cpp:386-387` + repro C1-C3 |
| U1 | Ctrl+Alt+U: PreloginV1 이 없으면 기존 unlock. `has_unlock_password(0)` 이면 프롬프트 없이 `unlockRequested` | `viewer_window_proc.cpp:236-250` (`:243`) |
| U2 | 서비스 challenge: `reqActive` 면 RejectedPolicy, 로그 없음(log_line 은 발급·WTS 결과만) | `secure_unlock_service.cpp:48-55` |
| U3 | 뷰어: 결과는 stdout 한 줄뿐(창 없음). 비밀번호는 AuthFailed/DecryptFailed 일 때만 삭제 | `viewer_control_client.cpp:738-758`, `viewer_unlock.cpp:453` |
| T1 | 툴바·메뉴 `preloginCredAvailable` = `DecidePreloginCred` = PreloginV1 && Pc | `reboot_menu_policy.hpp:45-51`, `viewer_picker.cpp:113` |
| P1 | (정책 충돌) 호스트 IME 를 안 쓰는 뷰어의 kind 5/6 키와 InputText 는 잠금/UAC 화면에서 SYSTEM agent 로 **평문 입력된다** | `host_control_session.cpp:1153`(secure_dispatch_event), `:1368`(secure_dispatch_text) |

**회귀 판정**: a622f04 에도 같은 PhysicalKey 게이트(`host_control_session.cpp:788`), 같은 reqActive 정책(`secure_input_service_main.cpp:540-547`), 같은 `has_unlock_password(0)` 경로, 같은 `REMOTE60_HOST_IME` opt-in 이 있다 → **기존 동작**이다. 0.2.160 의 'Windows 로그인' 버튼은 새 기능이고 로그인 전 화면 전용이다. 0.2.160→0.2.161 사이 뷰어 소스 변화는 e2e 테스트 5줄뿐이다.

## 3. 로그 근거 (이 PC, 직접 읽음)
- `host_app.log:4160` 14:35:35.692 `[ime] state-req seq=1 gen=1 action=1 -> status=1` — 뷰어가 IME 협상에 들어갔다는 증거다(뷰어는 `host_ime_optin()` 일 때만 이 요청을 보낸다). 응답은 st≠2 이므로 뷰어 코드상 activate 로 간다. **activate 자체는 뷰어 로그로 미확인**(§5).
- 14:35~14:38 호스트에 kind 5 없음. PhysicalKey 는 K3 경로에서 로그 없이 사라지므로 **호스트 로그만으로는 송신 여부를 알 수 없다**.
- 기이점(가능성으로만): 세션은 14:34:29 부터 잠겨 있었는데 14:35:35 IME align 은 `interactive_desktop_is_default()==true` 분기를 탔고, 호스트가 secure 를 말한 것은 14:35:43 이다. 이 8초 동안의 키도 세션 내 SendInput 으로는 Winlogon 에 닿지 않았을 것이다. 판정 캐시의 초기값이 후보다.
- Ctrl+Alt+U: 호스트 pid 9000(14:34~) 에서 unlock 파이프 연결 로그 `trust[unlock]=trusted` 는 **15:08:03 한 번뿐**이다(`host_app.log:18102`). 이 로그는 값이 바뀔 때만 찍히고, 연결은 relay worker 의 첫 작업에서만 일어난다(`host_unlock_relay.cpp:121-125`; ProbeConnectForTest 는 테스트 전용). 즉 **14:35~15:08 사이에는 unlock 교환이 호스트에 도달하지 않았다**. 그 시기 프롬프트는 취소됐거나 다른 사유로 보이며, 미확인이다. 15:08:03 교환은 15:04:36 에 접속한 **0.2.161 뷰어**(어느 기기인지는 미확인) 연결에서 왔다. `secure_input.log` 에는 하루 종일 `unlock challenge issued` 가 없다 → U2 RejectedPolicy 와 맞는다.
- 마우스 25/25 는 `secure-system-agent` 로 도달했다(`secure_input.log` agent desktop=Winlogon inject=ok).

## 4. ARSO (읽기 전용 확인)
- System 1074 **14:33:25** `MoNotificationUx.exe` 가 다시 시작을 요청했다. 사유는 "운영 체제: 서비스 팩(계획됨)" 0x80020010 으로, Windows Update 재시작이다(GNLink 재시작 아님).
- 부팅 14:34:14 → 7001 사용자 로그온 14:34:24 → (OSLink 로그) 14:34:29 잠금.
- `Policies\System` 에 DisableAutomaticRestartSignOn/AutomaticRestartSignOnConfig 없음, `Winlogon\UserARSOPreferences` 없음(=기본값 사용), AutoAdminLogon 없음 → **ARSO 기본 동작**과 맞는다. Security 4624 는 권한 때문에 읽지 못해 logon type 은 미확인이다.
- GNLink 재시작은 `InitiateSystemShutdownExW`(`prelogin_admin_service.cpp:121`)로, ARSO 플래그가 없다 → 로그인 전 화면(worker·버튼 있음). **Windows Update 재시작이면 잠금 화면(버튼 없음).** 사용자는 두 경우를 모두 만난다.

## 5. 남은 확인 (검증용이 NAS 에서 1회 grep)
`/opt/gnlink/remote60-directory/logs/shotan/68f79d01-d6c4-4a4a-a3ae-69da5be54826/viewer.log`:
1. `grep -n "\[ime\] activated\|\[ime\] align done" ` 가 14:35:35 근처에 있으면 → K1/K2 확정.
2. 14:35~14:38 `input-timing` 중 `actionKind=` 가 PhysicalKey 이고 `eventKind=0` 인 줄의 수 → 키가 송신됐다는 확정(“0건” 착시 해소).
3. `grep -n "\[unlock\]"` → `challenge rejected: RejectedPolicy` 또는 다른 status. 15:08:03 교환이 어느 기기(0.2.161 뷰어)의 것인지는 같은 시각 다른 기기 viewer.log 로 확인.

## 6. 격리 재현 (pure-logic, 제품 결정 코드 직접 구동)
- 소스 `C:\work\gnlink-wt\lockkeys-r1-repro\lock_repro.cpp` sha256 `9cd372c9…60fd8933`, exe `90c5607b6e540263016061cb893f4062d09e2452df79d16d853cef92f07c3928`, 빌드 `build.bat`(`/MT`, 5063cd9 의 `secure_unlock_service.cpp` + `remote60_sealed_unlock.lib` from 별도 빌드 `C:\work\gnlink-wt\lockkeys-r1-build`).
- 결과 exit=0, `SUMMARY checks=8 failures=0`:
  - A1 콘솔 활성+잠금 → RejectedPolicy, 서비스 로그 0줄. A2 대조: disconnected → ChallengeIssued, 로그 1줄.
  - B1 잠금 화면 pong(SecureDesktop|UnlockSealed|HostIme v1·v2|RemoteReboot = 0x403C, PreloginV1 없음) → sealedEntryAvailable=false. B2 대조: prelogin pong → true.
  - C1-C3 잠금 화면 pong + roleKnown → 뷰어 게이트는 kind5·PhysicalKey·Text 를 막지 않는다. C4 대조: roleKnown=false → 막는다.
- 같은 빌드의 제품 테스트: secure_unlock_service_test 12/12, prelogin_input_gate_test 13/13, reboot_menu_policy_test PASS (모두 exit 0).
- **증명하지 않는 것**: K3 호스트 PhysicalKey 게이트는 람다라 구동하지 못했다(코드 인용만). UI·실기·Winlogon 입력도 다루지 않았다. 0.2.159 는 소스 대조만 했다(바이너리 실행 없음).

## 7. 최소 변경안 (사용자 결정 2 기준 — Codex 확정 필요, 구현 안 함)
**판정 축**: "secure desktop" 과 "잠금 화면" 은 같은 것이 아니다. UAC 자격 증명 창도 secure desktop 이므로, SecureDesktopActive 로 글자를 막으면 UAC 가 깨진다. 판별에는 **호스트 자기 세션의 WTS 잠금 상태**가 필요하다.
1. **새 pong 비트** `kCaptureFlagLockCredV1`(이름 미정): 사용자 세션 Host 가 "내 세션이 잠김 + 서비스가 잠금 화면 입력을 지원함" 일 때 세운다. `PreloginV1` 은 재사용하지 않는다 — 그 비트는 control allowlist·링크 종료까지 켠다(`viewer_control_client.cpp:922-926`).
2. **toolbar/메뉴/Ctrl+Alt+U**: `DecidePreloginCred` 를 `PreloginV1 || LockCredV1` 로 넓힌다 → 버튼 표시와 핫키가 같은 결정을 쓴다. Ctrl+Alt+U 는 둘 중 하나면 `request_prelogin_login` 계열(매번 프롬프트, **저장하지 않음**, 결과 창 `kMsgPreloginCredResult`)로 보낸다. 기존 `has_unlock_password`/저장 경로는 disconnected 세션용으로만 남기거나, 결과 창 표시를 추가한다(U3: 지금은 stdout 만). **latch 원인은 저장된 비밀번호**다 — 회사 PC 에 `save_unlock_password(0)` 로 남아 있을 수 있다(사용자 기기라 확인·삭제하지 않음).
3. **잠금 화면 sealed 전달**: 호스트 → 서비스 relay 에 **새 job**(WTSConnectSession 아님)을 둔다. 서비스가 열고, 이미 Winlogon 에 떠 있는 agent 가 LogonUI.exe 의 암호 칸(UIA IsPassword, `ProductCredTarget` 과 같은 규칙)에 Unicode 로 한 번에 입력한 뒤 즉시 지운다. 결과는 Injected(입력됨)와, 뒤이은 세션 이벤트 8 로 보는 SessionUnlocked(해제됨)를 구분한다. 기존 `reqActive → RejectedPolicy` 는 **그대로 둔다**(WTSConnectSession 보호). **sealed purpose(Unlock / PreloginCred / 신규)는 신뢰 경계 결정**이다.
4. **안내 조건**: 뷰어가 `LockCredV1`(roleKnown) 을 보면 글자 키·텍스트를 보류하고(내비게이션 키는 prelogin 목록과 같게) `kMsgPreloginTypingHeldBack` 문구를 "잠금 화면" 에도 맞게 띄운다. 호스트 IME PhysicalKey 가 K3 에서 조용히 버려지는 것도 같은 보류로 덮인다(뷰어가 아예 보내지 않음).
5. **P1 결정 필요**: 결정 2("직접 키 입력 허용 없음") 대로라면, 잠금 화면에서 kind 5/6 문자·InputText 를 SYSTEM agent 로 평문 입력하는 지금 경로(호스트 IME 를 안 쓰는 뷰어·Android)를 **호스트 쪽에서도 닫을지** 정해야 한다. 닫으면 오래된 뷰어는 잠금 화면에서 글자를 못 친다. UAC(잠금 아님)는 영향 없게 4번과 같은 축(세션 잠김)으로 거른다.
6. 계측(선택): K3 드롭 카운터 1줄, 서비스 RejectedPolicy 사유 1줄. 지금은 양쪽 모두 무음이다.

## 8. 미검증
뷰어 로그 3건(§5), 회사 PC 의 env·저장된 unlock 비밀번호 존재 여부, 14:35:35 desktop 판정 기이점, 4624 logon type, 실기 UI.

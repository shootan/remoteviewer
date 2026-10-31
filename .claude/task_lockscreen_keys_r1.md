# task lockscreen-keys r1 — 잠금 화면에서 PC 뷰어 키 입력이 전혀 안 감 (검증용 remote#izxg3q0o, 2026-10-21) — 담당 작업용1

사용자 보고(10-21 0.2.161 실기): "로그인 화면 정상으로 나오는데, 키 입력이 안 된다." 보고는 검증용 pin. 조사 먼저 — 원인 확정 전 제품 수정 금지(확정 뒤 Codex 경유).

## 검증용이 로그로 확정한 사실 (출처: 이 PC `C:\ProgramData\GNLink\secure_input.log`, `%LOCALAPPDATA%\GNLink\host_app.log`, NAS `/opt/gnlink/remote60-directory/logs/shotan/68f79d01-d6c4-4a4a-a3ae-69da5be54826/viewer.log`)
- 14:34:14 부팅 → 14:34:21 prelogin supervisor·worker 로그 폴더 ok·worker 기동 → **14:34:24 "user host handoff -> launched"** = 부팅 3초 만에 사용자 세션 존재. AutoAdminLogon=0 이므로 Windows 의 재시작 뒤 자동 로그인+잠금(ARSO 추정 — 확인 필요). 즉 사용자가 본 것은 **로그인된 세션의 잠금 화면**일 가능성이 높다(로그인 전 화면 아님).
- 뷰어(0.2.160, 68f79d01) 14:35:34 접속 → 14:35:39 `secure-desktop-active=1`. 호스트(사용자 세션, 0.2.161) 14:35:43 `desktop-promotion-deferred reason=secure-desktop`, secure input agent `desktop=Winlogon attach=ok journal=ok inject=ok`.
- **마우스는 도달**: 서비스 로그 inject landed eventKind 2/3 = 25/25 (`secure-system-agent` 경유).
- **키는 뷰어가 아예 안 보냄**: 뷰어 로그 14:35~14:38 input-timing eventKind 5(key down) **0건**, 6(key up) 9건(접속 직후 modifier 해제뿐). 호스트·서비스에도 키 이벤트 없음 → 뷰어 쪽에서 막힘.
- 14:35:52 툴바 '다시 시작' 클릭 기록(뒤이은 재부팅 없음 — 취소 추정). 14:38:26 세션 이벤트 8(unlock), 14:38:57 이벤트 7(lock).
- 0.2.160 에서 뷰어 입력 경로에 들어간 prelogin 게이트: `prelogin_input_gate.hpp` EffectiveHostFlags(roleKnown=false → PreloginV1 로 취급) / ViewerMaySend* — held_back_at_logon 은 roleKnown 일 때만 안내. 사용자 세션 Host 는 PreloginV1(0x2000) 을 안 세움(host_control_session.cpp:795-797). 그런데도 key down 0 → 다른 막힘 지점(잠금/secure-desktop 상태의 기존 뷰어 정책, 포커스, roleKnown 미갱신 등) 후보.

## 할 일
1. 뷰어(0.2.160/0.2.161 소스)에서 key down 이 송신되지 않는 지점을 **코드로 특정**: secure-desktop-active·잠금 상태에서의 키 처리(기존 lock-screen/sealed unlock 정책 포함), prelogin 게이트(roleKnown 갱신이 잠금 중 pong 에서 되는지), 포커스/IME 경로. 0.2.159(a622f04) 와 비교해 이것이 **회귀인지 기존 동작인지** 판정.
2. 격리 재현(사용자 화면 조작 금지): 가짜/시험 host 가 secure-desktop-active 와 일반 pong 을 보낼 때 뷰어 key down 송신 여부(제품 뷰어 코드), 0.2.159 대조. 필요한 경우만 SendInput — 검증용 경유 사용자 확인.
3. ARSO(재시작 후 자동 로그인·잠금) 여부를 이 PC 설정·이벤트로 읽기 전용 확인(변경 금지): 사용자가 본 화면이 잠금인지 로그인 전인지.
4. 원인·수정안(있다면)과 근거를 보고 — 수정은 검증용→Codex 확정 뒤.

## 규칙
사용자 설치본·서비스·세션 조작 금지. 제품 수정·버전·게시 금지(조사 단계). 측정 슬롯 CreateNew.

## 보고 (1회)
`D:/remote/remote/.claude/report_lockscreen_keys_r1.md` + 검증용 pin.


## 사용자 결정 (10-21, 확정 — 선택지 2)
- 비밀번호 입력은 'Windows 로그인' 버튼·Ctrl+Alt+U(암호화 경로)로. **로그인 전 화면·잠금 화면 모두** 버튼 표시·동작, Ctrl+Alt+U 가 매번 뜨고 실제로 전달·결과 표시.
- 키보드 직접 입력 허용은 이번에 하지 않음. 글자를 막는 경우 반드시 안내(버튼 위치).
- 조사 보고에 최소 변경안(toolbar 표시 조건·Ctrl+Alt+U 경로 선택과 latch·잠금 화면 sealed unlock 전달·안내 조건)을 포함 — 구현은 Codex 확정 뒤.


## 주소 갱신 (10-21 저녁 세션 clear)
이전 세션 주소(검증용 4houkzd5 등)는 무효. 보고는 a2a_agents 의 displayName 'remote_claude' 세션에 pin. 이전 세션이 끊겨 보고가 없었으므로 처음부터 진행.

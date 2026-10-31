# OSLink 잠금 화면·캡처 동작 — 로그 관찰 (검증용 remote#4houkzd5, 2026-10-21)

출처: 이 PC `C:\LDPlayer\OSLink\bin\log\{ldremoteservice,ldremoveevent,capture}.log`(읽기만). 디컴파일·바이너리 분석 아님 — OSLink 자체가 남긴 로그 관찰. 코드 복제 목적 아님(설계 비교).

## 오늘 사건 시간선 (OSLink 서비스 로그의 WTS 세션 이벤트)
- 14:34:24 eventType 5(WTS_SESSION_LOGON) session 1 → 14:34:29 eventType 7(LOCK). **부팅 직후 Windows 가 자동 로그인 후 잠금**(재시작 후 로그인 정보 사용 계열) — 사용자가 본 "로그인 화면" 은 잠금 화면. GNLink secure_input.log 의 14:34:24 user host handoff 와 일치.
- 이후 잠금/해제: 14:38:11 해제, 14:38:53 잠금, 14:39:13 해제, 15:04:06 잠금, 15:05:13 해제(OSLink 로 비밀번호 입력), 15:07:57 잠금, 15:08:38 해제.

## 캡처: 데스크톱마다 캡처 프로세스를 새로 띄움
- `ldremoteevent`(SYSTEM) 의 CaptureMonitor 가 데스크톱 전환(desktop_change)을 감지하면 `reboot capture request → kill capture → current desktop name:Winlogon → run_on_desktop capture.exe desktop:winsta0\Winlogon`. 해제 뒤엔 다시 Default 로 같은 절차.
- 캡처는 DXGI Desktop Duplication(`DxgiOutputDuplicator::Duplicate`, 기존 분석 `docs/OSLink_구조분석.md`), 네트워크는 `ldremote.exe` 가 따로 소유 → 캡처를 죽였다 살려도 연결 유지.

## 잠금 화면 입력: UI Automation 으로 암호 칸 포커스 + 키 그대로 전달
- 클라이언트가 `screenCheck`(IPC msg 2001)를 보내면 `WJAutomaticLogin::SwitchToLoginScreen`: LogonUI 창(`LogonUI Logon Window`, `Windows 로그인`) → UIA 트리 탐색(`UserControlPageRoot` → 사용자 이름 `shotan` → `PasswordBox` AutomationId `PasswordField_2`) → `set focus success` → `loginScreenEvent`. 그 뒤 사용자의 키 입력이 Winlogon 데스크톱으로 들어가 15:05:13 해제.
- 즉 별도 비밀번호 창이 아니라 **암호 칸에 포커스를 맞춰 주고 키보드를 그대로 보냄**. (전송 암호화 여부는 이 로그로 알 수 없음.)

## GNLink 와의 차이 (설계 시사점)
| | OSLink | GNLink 0.2.161 |
|---|---|---|
| 잠금/로그인 화면 캡처 | 데스크톱 전환마다 캡처 프로세스 kill → Winlogon 데스크톱에 재기동, 연결 유지 | 사용자 세션 Host 가 secure desktop 을 'paused'(desktop-promotion-deferred) 로 처리, 로그인 전은 prelogin worker |
| 잠금 화면 키 | 그대로 전달 + UIA 로 암호 칸 자동 포커스 | 뷰어가 key down 0 송신(원인 조사 중), 'Windows 로그인' 버튼은 로그인 전에만 |
| 캡처 장애 | 캡처 프로세스만 재기동 | GNLinkStream 전체 재기동 → 연결 끊김(10-21 회사 PC exit 44) |

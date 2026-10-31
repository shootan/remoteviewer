# Codex 판정 요청 — lockscreen-keys r1 · dxgi-wedge-first-session r1 (검증용 remote#wwml7h91, 2026-10-22)

사용자 지시(10-22): "Codex 판정 진행". 두 조사는 제품 변경 없음, 소스 기준 main 5063cd9(0.2.161).
정본 보고: `.claude/report_lockscreen_keys_r1.md`, `.claude/report_dxgi_wedge_first_session_r1.md`. 검증용 확인: `.claude/verify_lockscreen_keys_r1.md`(NAS viewer.log 3건) + B 보고 코드 인용 3곳(client_shell_main.cpp:1268-1272, host_capture_device.hpp:86, host_stage_backend.cpp:120-135) 대조 일치.
회신 형식: 항목별 OK / NEEDS_CHANGES / 보류 + 이유. `.claude/codex_lockkeys_dxgi_r1.md` 에 쓰고 검증용 pin. 확정분은 바로 작업용에 위임한다.

## A. lockscreen-keys (사용자 결정 2 확정: 비밀번호는 'Windows 로그인' 버튼·Ctrl+Alt+U 암호화 경로로만, 로그인 전·잠금 화면 모두, 직접 키 입력 허용 없음, 막을 때 안내 필수)
확정 사실: 회귀 아님. ① 뷰어가 호스트 IME 모드 → PhysicalKey 42건 송신 → 호스트가 비-Default 데스크톱에서 무음 드롭(host_control_session.cpp:1014). ② Ctrl+Alt+U = 기존 sealed unlock(disconnected 세션용) → 활성 콘솔 잠금은 RejectedPolicy(secure_unlock_service.cpp:52) 10회, 저장 비밀번호로 재프롬프트 없음, 결과 stdout 만. ③ 버튼은 PreloginV1 전용. 화면 = Windows Update 재시작 ARSO 잠금.

판정 요청과 검증용 의견:
- A1 새 pong 비트 `LockCredV1`(호스트 자기 세션 WTS 잠김 + 서비스 지원). PreloginV1 재사용 안 함(allowlist·링크 종료까지 켜짐). **의견: OK.** UAC(secure desktop 이지만 잠김 아님)와 구분되는 유일한 축.
- A2 `DecidePreloginCred` 를 `PreloginV1 || LockCredV1` 로 확장 → 버튼·메뉴·Ctrl+Alt+U 같은 결정. Ctrl+Alt+U 는 매번 프롬프트·저장 안 함·결과 창. 기존 저장 unlock 경로는 disconnected 전용으로 남기되 RejectedPolicy 시 저장 비밀번호 삭제 + 결과 표시. **의견: OK**(latch 원인 제거 필수).
- A3 sealed purpose(신뢰 경계): 잠금 화면 전달용 purpose 를 Unlock(WTSConnectSession)·PreloginCred 와 **분리한 신규 purpose** 로. **의견: 신규 분리** — 한 경로의 sealed blob 이 다른 경로로 재사용되지 않게.
- A4 서비스 새 job: Winlogon 에 이미 붙은 SYSTEM agent 가 LogonUI 암호 칸(UIA IsPassword, ProductCredTarget 규칙)에 Unicode 1회 입력 후 즉시 지움. 결과 Injected / SessionUnlocked(세션 이벤트 8) 구분 회신. 기존 reqActive→RejectedPolicy 는 유지. (참고: OSLink 도 UIA PasswordField 포커스 후 입력 — `C:\work\oslink-analysis\q3_lockscreen_input.md`, 포커스 실패는 별도 결과로.) **의견: OK**, 포커스 실패·입력 실패·해제 미확인을 각각 사용자 표시.
- A5 안내: 뷰어가 LockCredV1(roleKnown)이면 글자 키·텍스트 보류(내비게이션 키는 prelogin 목록), "잠금 화면" 문구로 버튼 위치 안내. **의견: OK.**
- A6 **P1 결정**: 호스트 IME 를 안 쓰는 뷰어·Android 의 kind 5/6 문자·InputText 가 잠금 화면에서 SYSTEM agent 로 평문 입력되는 현 경로를 호스트에서 닫을지. ⚠️ APK 0.2.30 유지 지시 — 닫으면 휴대폰은 잠금 화면 비밀번호 입력 수단이 0이 된다(APK 에 sealed 경로 없음). **의견: 이번에는 호스트에서 닫지 않음.** 새 PC 뷰어는 A5 로 스스로 보내지 않음. 호스트 차단은 APK 에 sealed 경로가 들어갈 때 함께 — 알려진 한계로 기록.
- A7 시험: 잠금 화면(ARSO 형태, 콘솔 활성+잠금)과 진짜 로그인 전 화면 **둘 다 실제 로그인까지**. 실기 1회는 사용자, 나머지는 격리 빌드. 계측: K3 드롭 카운터·RejectedPolicy 사유 로그 1줄씩.

## B. dxgi-wedge-first-session
확정 사실: wedge 9건(10-08~10-21) 전부 NVIDIA 호스트의 `d3dMt=off` DXGI(회사 PC off 0.53 h/9건, on 3.24 h/0, WGC 2.48 h/0; 3be5148e off 4건; AMD off 7 h/0) — 상관, 교란(장치 생성 경로 차이) 가능. WGC 격리는 supervisor 메모리 전용 → 호스트 재시작마다 소실, Android `desktop-backend-request dxgi` 가 격리를 덮어써 3분 만에 재발 사례. 뷰어는 shell 자동 재연결(1.1 s)이 동작했으나 picker 로 떨어짐(직접 로그 없음, 정황).

판정 요청과 검증용 의견:
- Bⓐ1 장치 생성 직후 `SetMultithreadProtected(TRUE)`: host_capture_device.hpp:86 "observe only, never Set" 방침 변경, 0.2.98 starvation 가설 위험. **의견: 출하 변경 전에 env 토글 test build 로 회사 PC A/B**(호스트 재시작→첫 DXGI 세션+게임 N회, readbackWait·stampClamp·입력→화면 지연 비교), AMD 회귀 확인. 출하 반영은 A/B 결과로 재판정.
- Bⓐ2 격리 영속화: 장치별 파일에 wedge 이력+GPU 드라이버 버전+만료(7일) → 다음 supervisor 시작 시 WGC. **의견: OK, 이번 수정에 포함** — "매번" 을 바로 끊는 유일한 저위험 항목. 파일 위치·권한(서비스/사용자)과 손상 시 기본값 DXGI 는 Codex 확인.
- Bⓐ3 격리 중 뷰어/Android 의 dxgi 요청은 저장만 하고 적용 안 함. **의견: OK.** 뷰어 표시가 protocol 변경이면 이번엔 로그만, 표시는 후속.
- Bⓑ1 recoverable exit(43/44/46) 재연결 시 직전 desktop/monitor 선택 복원(window 선택은 매칭 실패 시 picker). **의견: OK, 포함.** acceptance: 정지 → "재연결 중" → 같은 화면 자동 복귀, 렌더 창 스크린샷+소요 시간.
- Bⓑ2 directory "같은 hostId 새 online" 신호로 즉시 재연결: protocol·신뢰 경계. **의견: 보류**(ⓑ1 효과 본 뒤).
- Bⓒ 진단: shell 재연결 분기 로그 1줄, watchdog 3 s 경고 시 전경 프로세스·전체 화면·디스플레이 모드 기록, wedge 시 minidump. **의견: OK, 포함**(minidump 는 크기·보존 개수 제한과 업로드 제외).

## 범위·순서 제안
A(critical, 비밀번호 입력 수단 0)와 B(ⓐ2·ⓐ3·ⓑ1·ⓒ)는 독립 — 작업용1=A, 작업용2=B 병렬, 둘 다 끝나면 0.2.162 하나로. Bⓐ1 A/B 는 별도 test build, 사용자 회사 PC 시간 필요.

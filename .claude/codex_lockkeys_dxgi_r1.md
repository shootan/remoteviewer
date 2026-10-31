# lockscreen-keys · dxgi-wedge-first-session r1 — Codex 설계 판정

2026-10-22 · task=lockscreen-keys·dxgi-wedge-first-session / rev=r1 / kind=design-review-result
검증용 reply_to `remote#wwml7h91`; Codex `remote#am104clp`.

**A는 아래 수정 조건으로 구현 진행. A6의 ‘구형 APK를 위해 잠금 화면 평문 입력 유지’는 NEEDS_CHANGES이며 채택하지 않는다. B는 격리 영속화·강제 DXGI 차단·선택 복원·제한된 진단을 진행한다. D3D 보호 설정은 시험 빌드 A/B만 OK, 출하 변경은 보류다.** 이는 설계 확정이며 구현·실기·출하 승인이 아니다.

## 확인 범위와 정정

요청서 `codex_req_lockkeys_dxgi_r1.md`, 두 `report_*_r1.md`, `verify_lockscreen_keys_r1.md`를 끝까지 읽고, main `5063cd9ae4269c93c99c4ecc69600f353481d624`의 관련 제품 소스와 대조했다. 검토 checkout은 `.claude/worktrees/main-stability-integration`이다. 현재 tracked 변경은 별건 OSLink 문서 작업이며 제품 소스 변경은 없다. 루트 `659f788`을 현재 제품으로 취급하지 않았다.

직접 읽은 주요 경계는 `host_control_session.cpp`의 PhysicalKey/평문 key/text/role, `secure_unlock_service.cpp`의 challenge와 WTSConnectSession, `secure_input_service_main.cpp`의 WTS 잠금 조회, `viewer_window_proc.cpp`의 Ctrl+Alt+U, `viewer_control_client.cpp`의 두 결과 경로, `viewer_unlock.cpp`와 `sealed_unlock.cpp`, `prelogin_cred_host.cpp`의 실제 LogonUI/UIA/입력 검사다. B는 `host_capture_device.hpp`, `host_stage_backend.cpp`, `host_recovery_policy.hpp`, `host_app_main.cpp`의 Supervise, `host_startup_capture.cpp` watchdog, `client_shell_main.cpp`의 exit/reconnect를 읽었다.

**앞선 ‘뷰어가 키를 전혀 보내지 않음’은 정정한다.** 검증용 NAS 대조에서 PhysicalKey(actionKind=12) 42건과 IME 활성화가 확인됐다. 호스트 PhysicalKey의 비-Default 데스크톱 거부는 코드로 확인했다. 내가 NAS 원본을 다시 조회하거나 K3/Winlogon 동작을 실행한 것은 아니다. 10회의 RejectedPolicy도 검증용의 NAS 확인 근거다. ARSO는 Windows Update 재시작·즉시 사용자 로그온·잠금과 설정으로 뒷받침되며 Security logon type 미확인 한계가 남는다. ‘처음 접속할 때만’의 DXGI 설명과 MT on/off 통계는 보고 근거이며 원인 인과를 확정하지 않는다. OSLink 정적 분석은 설계 참고이지 GNLink 성공 증거가 아니다.

## A. 잠금 화면 Windows 로그인

| 항목 | 판정 | 확정 내용 |
|---|---|---|
| A1 LockCredV1 pong | **OK** | 사용자 Host 자기 세션의 권위 있는 잠김 상태와 새 서비스 경로 지원을 함께 확인한다. UAC와 구분하며 PreloginV1은 재사용하지 않는다. |
| A2 공통 버튼/단축키 | **OK, 아래 조건** | 로그인 전·잠금 화면에서 버튼/메뉴/Ctrl+Alt+U가 같은 역할 결정을 사용하고 매 시도 프롬프트, 무저장, 사용자 결과 창을 제공한다. |
| A3 신규 sealed purpose | **OK — 신규 분리 확정** | Unlock/WTSConnectSession, PreloginCred, LockCred 3개 목적을 KDF와 AAD 모두 분리한다. 잠금용 서비스 job/IPC도 명시적으로 구분한다. |
| A4 서비스→SYSTEM agent job | **OK, 아래 조건** | 활성 콘솔의 잠긴 자기 세션만 대상. 기존 disconnected용 reqActive 거부는 그대로 둔다. LogonUI 암호 칸을 확인하고 일회 Unicode 입력, 결과와 메모리 정리를 보장한다. |
| A5 차단 안내 | **OK, A6와 함께** | 새 PC 뷰어는 알려진 잠금 역할에서 문자/IME/text/macro 경로를 보류하고 실제 보이는 Windows 로그인 버튼 위치·Ctrl+Alt+U를 안내한다. |
| A6 호스트 평문 경로 유지 | **NEEDS_CHANGES — 원안 불채택** | ‘직접 문자 입력 금지’ 확정안에 맞춰 잠금 상태의 호스트/최종 secure injection 경계에서도 막는다. 새 뷰어의 자발적 차단만으로 충족했다고 보고하지 않는다. |
| A7 행동 검증·계측 | **OK, 아래 보완** | 활성 콘솔+잠금과 진짜 로그인 전 화면 각각 버튼/단축키→sealed 전달→결과→로그인·해제 행동을 검증한다. 두 상태를 하나의 사용자 실기 순서에 묶을 수 있지만 하나를 생략하지 않는다. |

### A 구현 계약

1. **상태/호환성:** LockCredV1은 기능 플래그가 아니라 현재 잠긴 자기 세션에서 새 경로를 제공한다는 신호다. WTS 조회 실패를 ‘잠기지 않음’으로 처리하지 않는다. 서비스 지원은 신뢰된 서비스 연결의 새 버전/기능 응답으로 확인하고 구형 서비스에는 새 요청을 보내지 않는다. 상태 변경/재접속은 stale prompt·credential·결과를 폐기한다. 호스트의 평문 차단 정책은 비트가 켜졌는지와 독립적이다. 서비스가 없다고 잠금 화면에서 평문을 허용하지 않는다.
2. **단일 진입과 latch:** 버튼과 Ctrl+Alt+U가 같은 진입 함수를 사용한다. 한 번 누름당 prompt 하나; 키 repeat·동시 클릭·중첩 modal을 막고 취소/완료/실패/연결 변경 후 다시 누를 수 있게 한다. modifier/down-up bookkeeping도 균형을 지킨다. 저장 암호를 매번 자동 재사용하는 현재 동작을 잠금/로그인 전 경로에 남기지 않는다. 기존 disconnected unlock이 RejectedPolicy/미지원/암호 읽기 실패로 끝나도 사용자에게 보이며, 요청된 RejectedPolicy 시 저장 암호 제거를 적용한다. 실제 사용자 저장 파일을 개발 중 임의로 지우는 지시는 아니다.
3. **목적/연결 바인딩:** 신규 purpose는 label 문자열만 바꾸고 수신 측 자동 추측에 맡기는 방식이 아니다. 새 kind/op 또는 명시 버전이 붙은 요청과 응답으로 선택하고, 서비스는 발급 당시 목적을 저장해 sealed 수신/결과 조회까지 고정한다. 기존 wire 구조를 크기/version 검사 없이 재해석하거나 세 목적을 차례로 복호화하지 않는다. host/account/requestId/control cookie/pipe caller process·생성시각/session/console/lock·topology generation/만료를 묶고, 실행 직전 재검증한다. replay·중복은 재입력하지 않고 기존 결과만 돌려준다. 상대 세션·목적·만료·토폴로지 중 하나라도 달라지면 거부한다.
4. **LogonUI 대상:** 사용자 세션 Host는 평문 암호를 열지 않고 trusted service로 relay한다. 서비스↔agent는 해당 세션에 이미 인증된 사설 경계를 사용하고 일반 InputText 메시지를 새 credential job으로 취급하지 않는다. 에이전트가 붙은 Winlogon 데스크톱, 실제 system directory의 LogonUI.exe, SYSTEM 토큰, 같은 대상 session, 해당 프로세스의 UIA IsPassword 필드를 확인한다. 임의 HWND/제목만 맞춰 입력하지 않는다. 기존 ProductCredTarget의 필수 판정 규칙을 공유하되 prelogin 전용 ‘다른 데스크톱을 보면 프로세스 종료’ 수명 규칙을 사용자 agent에 그대로 가져오지 않는다. UIA 포커스 보조를 추가한다면 동일 검증 대상만 선택하고 입력 직전에 재검사한다. 실패 시 입력하지 않고 포커스/준비 실패를 회신한다. 실행 중 사용자 데스크톱을 강제 전환하지 않는다.
5. **입력·결과:** 사용자 1회 확인당 한 번만 입력한다. 부분 SendInput·포커스 변경·timeout을 성공으로 바꾸거나 자동 재전송하지 않는다. ‘즉시 지움’은 서비스/agent/viewer의 평문·INPUT 버퍼를 모든 종료 경로에서 지운다는 뜻이며 입력 직후 LogonUI 필드를 지우라는 뜻이 아니다. Injected는 입력됨, SessionUnlocked는 **그 요청 이후 동일 session·lock generation에 대응하는 해제 관측**이다. 무관한 session 이벤트 8, 오래된 이벤트, pong에서 Prelogin 비트가 사라졌다는 사실만으로 잠금 해제 성공을 만들지 않는다. 이 버전에서 자동 제출을 하지 않는다면 Enter/제출 버튼 필요를 결과 창에 명시하고 실제 해제까지 시험한다. focus 실패/입력 실패/입력 후 해제 미확인/사용자 취소/미지원은 각각 구분한다.
6. **A6 수정 확정과 APK 영향:** 잠긴 세션으로 향하는 kind 5/6 문자, InputText, raw/IME/macro 등 평문 입력 경로를 호스트에서 차단하고, 지연·큐 잔류·위조된 역할에도 최종 service/agent가 일관되게 거부하게 한다. 단순 SecureDesktopActive 전체 차단은 금지다. **잠기지 않은 UAC의 기존 허용 동작은 유지**하고 mouse/내비게이션은 기존 prelogin 목록 수준으로 유지한다. 이미 보낸 키를 해제하는 release 정리는 문자열 차단과 분리해 stuck key를 만들지 않는다. ACK는 수신 확인이며 입력 성공으로 보고하지 않는다.

   **APK 0.2.30은 변경/배포하지 않는다. 따라서 새 호스트의 잠금 화면에서는 구형 APK로 비밀번호를 입력할 수 없고, 새 PC 뷰어 또는 로컬 입력이 필요하다.** 이것을 알려진 호환성 영향으로 사용자 최종 안내·릴리스 문서·구형 APK 대조 시험에 명시한다. 일반 데스크톱 접속/입력과 마우스·내비게이션까지 전부 끊기는 것으로 확대하지 않는다. 모바일 비밀번호 수단을 유지해야 한다면 sealed APK 또는 사용자의 명시적 평문 예외 결정이 필요하다. 에이전트끼리 알려진 한계로 기록하는 것만으로 사용자의 직접 입력 금지 결정을 뒤집을 수 없다. 현재 확정 범위는 **호스트 차단 포함**이며 A 나머지 구현을 이 결정 질문으로 세우지 않는다.
7. **시험:** 역할 unknown/구형 서비스/일반 desktop/UAC/활성 콘솔 lock/disconnected lock/prelogin, 취소→다시 누름·held key repeat·실패→재시도·연결/세션 변경, 목적 교차 blob/replay, 비암호 필드·위조 LogonUI/다른 session·부분 입력·해제 미확인을 포함한다. DOM/logic PASS와 product-equivalent 실제 UI·service/agent 전달 증거를 구분하고 출하 시험 seam/키/완화 manifest 부재를 확인한다. 새 PC의 blocked 문자 안내는 실제 버튼의 가시성·동작과 함께 본다. APK0.2.30이 평문 문자를 보내도 새 호스트에서 거부되고 UAC/일반 desktop 대조가 유지되는지 검증한다. K3 드롭/RejectedPolicy는 payload·암호·토큰 없이 bounded 사유/카운터만 로그로 남긴다. OS 실기는 사용자 승인된 조작 범위에서 한 묶음으로 준비한다.

## B. 첫 DXGI 세션 wedge와 복구

| 항목 | 판정 | 확정 내용 |
|---|---|---|
| Bⓐ1 SetMultithreadProtected | **시험 빌드 OK / 출하 변경 보류** | 동일 코드의 test-only on 대조를 먼저 수행. 기존 출하 장치 정책은 A/B 판정 전 변경하지 않는다. |
| Bⓐ2 격리 영속화 | **OK, 아래 위치·권한·범위 확정** | 일반 사용자 Host supervisor의 exit44 격리를 장치/드라이버별로 영속화한다. prelogin은 적용 제외. |
| Bⓐ3 격리 중 DXGI 요청 | **OK** | 사용자 희망값과 실제 적용값을 분리. dxgi 요청은 저장만 하고 격리가 모든 적용 경로에서 우선한다. 기존 APK 변경 없이 시행한다. |
| Bⓑ1 재연결 선택 복원 | **OK, 아래 identity 조건** | recoverable 43/44/46에 같은 host desktop/monitor 복원. window는 확실한 검증 실패 시 picker, 자동 다른 창 선택 금지. |
| Bⓑ2 directory 새 online 신호 | **보류** | 이번에 protocol/인증 신호를 추가하지 않는다. Bⓑ1의 실제 복귀 시간을 먼저 측정한다. |
| Bⓒ 로그·상태 진단 | **OK** | shell 재연결 분기와 bounded OS 상태 snapshot 진행. |
| Bⓒ minidump | **NEEDS_CHANGES → 아래 제한으로 구현 가능** | 단순 watchdog 내부 동기 MiniDumpWriteDump는 승인하지 않는다. 별도 helper와 복구 시간·보관·업로드 제외 조건이 필요하다. |

### B 구현 계약

1. **MT 시험:** test-only compile gate 아래에서 장치가 생성되고 다른 스레드/MF/WGC가 쓰기 전에 적용한다. off 대조는 현재 생성 경로 그대로이며 이미 쓰는 공유 context에서 강제 off로 바꾸지 않는다. QI 실패/상태 unknown은 on/off로 계산하지 않는다. `SetMultithreadProtected` 반환값은 성공 여부가 아니라 **호출 전 상태**이므로 `GetMultithreadProtected`로 이후 상태를 기록한다([Microsoft API](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_4/nf-d3d11_4-id3d11multithread-setmultithreadprotected)). 같은 adapter/driver/source/장치 생성 경로/게임·해상도 조건으로 재시작→첫 DXGI 세션을 arm별 최소 5회, 회당 가능하면 과거 최대 재발창 350초를 넘겨 10분 관찰한다. 순서 편향을 줄여 교차 실행하며 wedge 수, 최초 프레임, readbackWait/stampClamp, 실제 입력→화면 지연 p50/p95, CPU/프레임률을 비교한다. 안전상 재현 중단·시간 미달은 그대로 기록한다. AMD 대조도 포함한다. 0건만으로 원인 확정은 하지 않는다. 회사 PC/게임/Host 재시작 실기는 사용자 조율 후이며 지금 임의 조작하지 않는다. 실기 대기만 남아도 B의 다른 확정분은 계속한다. 출하에는 MT env 토글/시험 exe를 넣지 않는다.
2. **파일과 권한:** 출하 일반 Host는 requireAdministrator다(CMake 확인). known-folder API로 구한 **ProgramData/GNLink/capture-recovery/** 아래에 장치 key의 고정 안전 문자열(hash) 이름으로 저장한다. SYSTEM/Administrators 소유·보호 DACL `SY/BA` 전용이며, 일반 사용자 쓰기 경로로 폴백하거나 서비스 ACL을 완화하지 않는다. ProgramData 환경변수/뷰어 문자열/임의 경로 인자를 신뢰하지 않는다. 기존 prelogin_store_chain/worker log의 handle 기반 parent-chain·소유자/DACL·reparse/hardlink 검사 원칙을 공유하고 쓰기/교체 전에 검사한다. 임시 파일도 같은 보호 폴더에 CREATE_NEW하고 완성 후 교체한다. privileged code가 untrusted 링크를 따라 쓰거나 정리하지 않는다. test-only root는 격리하며 출하에 없다.
3. **내용과 손상:** schema/version, 실제 wedge child가 사용한 adapter의 안정된 식별 hash, 해당 driver version, 관측 exit44와 최초/최근 시각, 만료 UTC만 저장한다. 부팅 때 바뀔 수 있는 LUID 하나를 영속 key로 쓰거나 기본 GPU를 실제 capture adapter라고 가정하지 않는다. `expires=마지막 실제 wedge+7일`; 정상 시작/재접속/단순 DXGI 요청으로 만료를 연장하지 않는다. driver 또는 adapter 변경/만료면 그 기록은 적용하지 않는다. 미래 시각·역전·크기/건수 초과·unknown schema·불명확한 adapter·손상/권한 실패는 기록을 거부하고 사유만 남긴다. 파일은 예컨대 4KiB/장치, 최대16개로 제한하고 정리는 자기 보호 파일만 검증 후 한다. 동시 supervisor 쓰기도 교체/merge 충돌로 이력을 잃지 않게 직렬화한다. 거부/읽기 실패 시 **기존 in-memory 정책으로 복귀**한다(새 supervisor의 정상 기본은 DXGI지만 같은 run의 이미 확정된 격리를 지우면 안 된다). 쓰기 실패도 현재 run의 WGC 복구는 계속한다.
4. **prelogin 제외:** 로그인 전 worker는 Winlogon DXGI 전용이며 WGC 폴백 대신 종료하는 계약이다. machine 파일이 있다고 service/prelogin worker를 WGC로 띄우지 않는다. 일반 Host supervisor와 그 child에만 적용하고, ‘격리 중 실제 backend=WGC’ 상태를 child에 명시적으로 전달한다. prelogin→user Host handoff 이후 일반 Host가 해당 adapter의 기록을 적용한다. **prelogin에서 wedge가 나도 이 변경으로 WGC를 활성화하지 않는다.**
5. **격리 우선순위:** `host_stage_backend.cpp`의 요청 분기 하나만 막으면 부족하다. startup/restart/desktop 전환/monitor·window 선택 후 desktop 복귀/자동 promotion에서도 requested=dxgi가 실제 WGC를 되돌리지 않게 공유 결정을 쓴다. 격리 만료/driver 변경 시 해제는 다음 안전한 재시작/선택 경계에서 적용하며 동작 중 capture를 임의로 갈아끼우지 않는다. WGC 실패 시 격리를 몰래 풀어 DXGI를 재활성화하지 않고 실패/재시도 상태를 드러낸다. `requested`, `effective`, 격리 사유/만료는 로그로 구분한다. APK UI가 dxgi를 희망값으로 보여줄 수 있다는 한계는 기록하고 실제 적용됐다고 주장하지 않는다. 이번에 상태 protocol 확장은 하지 않는다.
6. **선택 복원:** 마지막으로 **확정·렌더된** 대상만 복원한다. 연결 실패 중 pending selection이나 옛 global 값은 사용하지 않는다. shell↔viewer 전달은 해당 자식/operation/ownerEpoch/hostId에 묶인 제한된 handoff로 하고 원격 capability·토큰·암호를 복원 상태에 넣지 않는다. 재연결은 기존처럼 새 인증/API connect를 거친다. monitor는 새 호스트가 열거한 안정된 output identity로 재해석하고 raw HMONITOR/index만 재사용하지 않는다. 사라졌거나 모호하면 picker와 이유를 표시한다. window도 새 목록의 process identity/생성 시각 등으로 엄격 검증하며 HWND/제목만 같다고 다른 창을 자동 선택하지 않는다. 최소 구현은 window에 대해 picker 유지여도 된다. 사용자의 취소/다른 host 선택/로그아웃/새 operation은 기존 복원·retry를 취소한다. 자동 재접속에만 복원하고 명시적인 새 연결의 선택을 덮지 않는다. 기존 bounded retry 3회 정책을 확대하지 않는다.
7. **B 행동 근거:** fixture가 실제 supervisor 하위 child를 exit44시킨 뒤 `재연결 중`→같은 desktop/monitor의 **새 프레임 렌더**까지 UI·프로세스·시간을 남긴다. Bⓐ2는 supervisor 자체를 끝내고 새로 시작해도 같은 driver에서는 WGC, driver 변경/만료/손상에서는 정상 기본임을 실제 child 환경/시작 로그로 확인한다. 구형 APK dxgi 요청과 monitor/window 전환이 격리를 뚫지 않는 반례, 동시 다른 host·취소·monitor 제거·창 identity 재사용을 검사한다. picker가 안 뜬다는 소스 확인이나 단순 연결 성공으로 완료하지 않는다.
8. **진단/덤프:** 재연결 로그는 exit/wait/operation/attempt/선택 복원 결과를 한 줄로, credential 없이 쓴다. 3초 watchdog snapshot은 전경 프로세스명·PID, 창/monitor rect와 표시 모드 정도를 bounded 비동기 수집하고 창 제목/문서명/키 입력/환경 전체를 수집하지 않는다. rect가 화면을 덮는다는 사실을 독점 fullscreen 확정으로 부르지 않으며 판별 실패는 unknown이다. watchdog은 GPU/UIA/락/디스크 작업을 기다리지 않는다.

   **덤프는 owned child process handle/생성 identity로 pin한 별도 helper**에서 실행하고 DbgHelp 호출을 직렬화한다. [Microsoft MiniDumpWriteDump 문서](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump)는 불안정한 대상 내부 호출의 loader deadlock 위험과 별도 process 권고, DbgHelp 동시 호출 제한을 명시한다. 3초 slow 경고부터 best-effort 요청 가능하지만 slow와 실제 wedge를 구분한다. 기존 5초 terminate/relaunch를 덤프 완료 대기로 늦추지 않는다. helper timeout/실패/디스크 정체가 복구·다음 관측을 막지 않아야 한다. 생성시각과 고정 handle이 없는 PID 재탐색/다른 프로세스 덤프는 금지다.

   일반 사용자 capture child에 한정하고 prelogin/SYSTEM worker·입력 service·credential agent는 덤프하지 않는다. full-memory/heap dump는 넣지 않고 thread/stack 중심 minidump도 비밀이 포함될 수 있는 **로컬 민감 자료**로 취급한다. known-folder ProgramData/GNLink/diagnostics에 동일 SY/BA 보호/링크 거부 규칙을 적용한다. 예산은 파일당16MiB, 최대3개/총48MiB, 최대7일; 크기 초과 시 생성을 취소/무효로 표기하고 무한 partial 파일을 남기지 않는다. 일반 로그 수집·NAS 업로드·installer payload에서 제외한다. helper가 멈춘 반례에서도 5초 복구가 유지되는 실행 증거가 없으면 dump 부분은 보류하고 허용된 로그 진단만 먼저 진행한다. 덤프 수집을 이유로 A/나머지 B 자동 작업을 세우지 않는다.

## 위임·완료 경계

검증용은 이 판정 조건을 반영한 **A 정본 하나 / B 정본 하나**로 정리하고 작업용1/2에 최초 구현 task를 각각 연결한다. 조사 task와 구현 task의 ID를 구분하되 동일 구현을 중복 위임하지 않는다. A/B는 독립 진행 가능하다. A6는 호스트 차단 포함으로 확정했고 별도 사용자 예외를 가정하지 않는다. Bⓐ1 출하 보류와 Bⓑ2 보류는 다른 확정분의 blocker가 아니다. 구현자가 다른 신뢰 경계/새 기능을 제안할 때만 재판정을 요청한다.

통합 후보는 제안된 PC0.2.162로 준비하되 버전/후보 commit·제품 소스·공유 UI hash·시험/출하 차이를 고정해 검증용 독립 검사 후 Codex 최종 검토로 잇는다. APK0.2.30 유지, 사용자 설치본/서비스/세션 임의 조작 금지, git push 보류는 유효하다. 제품 기록은 해당 기능 task의 history/계획/commit에 취합한다. 이 파일은 미추적 검토서라 별도 공개 문서/commit을 만들지 않는다.

직접 수행은 요청/보고/제품 소스·Microsoft 공식 API 문서 검토, 기준 HEAD와 변경 범위 확인이다. 제품 빌드·테스트 실행·NAS 원본 재조회·키 입력·서비스/설치·실기·배포를 수행하지 않았다. 현재 상태는 **설계 판정 완료, 확정 범위 구현·검증·실기·출하 미완료**다.

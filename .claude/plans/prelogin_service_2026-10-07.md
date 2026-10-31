# 로그인 전(재부팅 직후) 원격 접속 — 검증용 안 r1 (2026-10-07, remote_claude) · Codex 비교 요청

## 사용자 요구 (2026-10-06, "가장 급한 것")
"컴퓨터 재시작 후 처음 비밀번호 입력 화면에서 OSLink 는 서비스로 떠 있어 원격으로 접속·재부팅이 되는데 GNLink 는 안 된다."

## Acceptance (사용자 행동)
1. 호스트 PC 재부팅 → 아무도 로그인하지 않은 로그인 화면 상태에서, 클라 목록에 그 PC 가 온라인으로 보이고 접속하면 **로그인 화면이 보인다**.
2. 비밀번호(필요하면 Ctrl+Alt+Del 포함)를 원격에서 입력해 **로그인할 수 있다**.
3. 로그인 뒤 **사용자 바탕화면으로 이어진다**(재연결 수 초 허용, 사용자 조작 없이).
4. 로그오프 → 다시 로그인 화면 접속 가능. 로그인 상태의 잠금 화면은 지금처럼(보안 입력 서비스).
5. (사용자 결정 대기) 뷰어의 원격 재부팅 버튼.

## 지금 사실 (file:line — Explore 조사 + 검증용 직접 확인)
- 자동 시작 = HKCU Run "GNLink"=`GNLinkHost.exe --tray` (host_app_main.cpp:864-920) → **로그인 뒤에만**. GNLinkHost 는 requireAdministrator(CMakeLists.txt:596) — Windows 는 Run 의 승격 필요 항목을 로그온 때 실행하지 않는 것으로 알려짐(이 PC 재부팅 실측 아님). 이 PC 의 Run 은 옛 "remote60" → 지워진 개발 빌드 경로(build-local\remote60_host_app.exe).
- OSLink(이 PC 실측): `LDRemoteSvc` Auto/LocalSystem(session 0) → `ldremote.exe`·`ldremoteevent.exe` 를 session 1 에 띄움. docs/OSLink_구조분석.md:24-40.
- GNLinkSecureInput(GNLinkInputService.exe): LocalSystem, **DEMAND_START**(secure_input_service_main.cpp:1142/1148, 재등록마다 DEMAND 로 되돌림), SYSTEM 토큰+TokenSessionId 로 agent 를 `winsta0\<입력 데스크톱>` 에 CreateProcessAsUser(:168-231), 세션 변경 추적(:345-370), 파이프 2개(SYSTEM+BA DACL). 캡처·네트워크 없음. 잠금 해제 = WTSConnectSessionW(로그인된 세션 전제, :543-551) → 로그인 전엔 해당 없음.
- Winlogon 캡처 실측(winlogon_capture_probe, 2026-08-04): SYSTEM agent 가 SetThreadDesktop(Winlogon) 후 BitBlt·DXGI 모두 UAC 화면 캡처 성공. **로그인 전 LogonUI 는 미측정**. 전환 직후 검은 프레임 주의.
- 디렉터리: host.json = `%LOCALAPPDATA%\remote60\host.json` 평문(hostId·hostToken, 비밀번호 없음; directory_client.cpp:391-407) → SYSTEM 은 못 찾음. machine_id = HKLM MachineGuid(머신 범위). heartbeat 는 GNLinkStream 의 HostAgent(host_startup_connect.cpp:240-262).
- 옛 설계: docs/legacy/잠금화면_사전로그인_설계.md(2026-07, 미구현) — 같은 방향(auto-start 서비스 → 콘솔 세션 SYSTEM agent, SendSAS).

## 검증용 안
1. **서비스**: GNLinkSecureInput 을 **AUTO_START** 로 바꾸고(재등록이 DEMAND 로 되돌리지 않게) "런처" 역할 추가 — 서비스 하나로 유지(설치·업데이터 정지/재시작 목록 변경 최소). (대안: 새 서비스 GNLinkHostService.)
2. **로그인 전 모드**: 콘솔 세션에 로그온 사용자가 없으면(WTSQueryUserToken 실패) 서비스가 `GNLinkStream.exe --prelogin` 을 SYSTEM 토큰(TokenSessionId=콘솔)으로 `winsta0\Winlogon` 에 띄움. 스트림: 캡처 스레드가 입력 데스크톱에 SetThreadDesktop 후 DXGI(실패 시 BitBlt), 입력은 같은 데스크톱 SendInput(JOURNALPLAYBACK 포함 열기), Ctrl+Alt+Del = 서비스의 SendSAS. 클립보드·파일 복사·오디오 등은 끔.
3. **기기 자격**: GNLinkHost 가 로그인 성공 때 hostId·hostToken 을 **머신 범위**로도 저장 — `%ProgramData%\GNLink\host_machine.dat`, DPAPI machine scope + ACL SYSTEM·Administrators 만. 설정 "로그인 전에도 원격 접속 허용"(기본 켜짐 제안 — 사용자가 OSLink 처럼 쓰려는 목적). 로그인 전 스트림은 이것으로 heartbeat. 뷰어 접속 인증은 지금과 같은 디렉터리 인증(계정).
4. **로그인 뒤 전환**: WTS_SESSION_LOGON(콘솔) → 서비스가 prelogin 스트림 종료 → 사용자 세션에 `GNLinkHost.exe --tray` 를 **사용자의 승격 linked token**(WTSQueryUserToken→TokenLinkedToken)으로 기동 → 기존 경로. 이것이 HKCU Run(승격 앱이라 동작 안 함)을 대체 — 자동 시작 결함도 같이 해결. 로그오프 → prelogin 으로 복귀. 뷰어는 기존 재연결.
5. **설치/업데이트**: 서비스 AUTO 등록, 옛 Run 값("GNLink"·"remote60") 정리, 업데이터 relaunch 목록·정지 목록에 반영. 업데이트 중 재시작 시 prelogin/사용자 모드 판단 재실행.
6. **시험**: 자동 — 서비스의 모드 판정(세션 상태 → 동작) 순수 시험, `--prelogin` 스트림을 시험 서비스로 띄워 Winlogon(UAC 화면) 캡처·입력 실측(probe 확장), 자격 파일 ACL·DPAPI 왕복. 실기 — **재부팅 1회는 사용자**(로그인 화면 접속·로그인·이어짐), 그 전까지 자동으로 갈 수 있는 데까지.

## Codex 에게 묻는 것
- Q1 서비스 하나(GNLinkSecureInput 확장) vs 새 서비스.
- Q2 머신 범위 자격 저장(DPAPI machine + ACL)·기본값(켜짐)·폐기 경로가 신뢰 경계로 충분한가.
- Q3 로그인 전 스트림 = 같은 GNLinkStream 의 `--prelogin` 모드 vs 별도 실행 파일.
- Q4 로그인 뒤 서비스가 사용자 승격 토큰으로 GNLinkHost 를 UAC 없이 띄우는 것 — 설치 때 관리자 동의를 근거로 허용 가능한가, 아니면 비승격 기동 후 기존 UAC 1회.
- Q5 SendSAS(SoftwareSASGeneration 정책 필요 여부)·LogonUI 캡처 미측정 위험·검은 프레임 처리.
- 그 밖에 빠진 위험(원격 재부팅 버튼 포함 여부는 사용자 결정).
답은 이 파일 끝 "## Codex 반론/합의" 절로.

## Codex 반론/합의

### Codex r1 비교안 — 2026-10-07, 검증용 답변 뒤 통합 지시서로 확정

검토 소스: main-stability-integration `543c45932ddcfff39e045249c7d94b6ae39d32a8`(0.2.150), 추적 변경 없음. Codex 주소는 현재 `remote#avivpkrm`. 10-06의 새 최우선 요구에 대한 **설계 검토**이며 서비스 설정/프로세스/설치본/정책/계정은 변경하지 않았다. paste-on-demand 사후 검토는 별건으로 유지한다.

**결론:** 로그인 전 기동을 HKCU Run에 맡길 수 없다는 원인과 'Session0 감독자 → 실제 콘솔 세션의 자식' 방향에 동의한다. 기존 서비스 재사용·엔진 재사용을 우선하되, 사용자 토큰 실패를 로그인 전 상태로 치환하는 판정, 기계 자격 자동 복제/기본 ON, 무조건 linked-token 기동, LOGON 통지 즉시 기존 host 종료는 수정해야 한다. 아래 인증·상태·전환 계약을 포함한 안으로 합의하자.

### 직접 대조한 사실

- 이 PC의 현재 읽기 전용 SCM 조회: `GNLinkSecureInput=Running/Manual/LocalSystem`, `LDRemoteSvc=Running/Auto/LocalSystem`; 콘솔 session1 Active. 이는 **현재 로그인 후** 상태이며 재부팅 후 LogonUI 동작을 증명하지 않는다.
- `host_app_main.cpp:864–920`은 HKCU Run을 읽고 쓴다. 부팅 전 실행이 되지 않는 직접 원인은 여기서 확인된다. Run 항목의 requireAdministrator 문제는 추가 로그온 자동 시작 문제이며, prelogin 불가의 유일 원인으로 취급하지 않는다.
- `secure_input_service_main.cpp::install_service`는 생성/재설정 모두 DEMAND_START다. existing agent는 SYSTEM token의 sessionId를 바꾸고 **생성 시** `winsta0\\desktop`에 붙인다. 코드에 Default에서 만들고 나중에 SetThreadDesktop만 했을 때 SendInput err5였다는 과거 실측이 있다. 이 제약을 새 모드에서 되돌리지 않는다.
- `current_input_desktop_name()`은 서비스 문맥에서 OpenInputDesktop 실패를 Winlogon으로 추정한다. 이를 '로그인 전 콘솔 desktop의 권위 판정'으로 재사용하지 않는다. Session0의 desktop 관찰과 대상 콘솔 세션의 desktop은 다르다.
- `directory_client.cpp::HostAgent::LoadCache`는 directory origin/account/machine 결합을 검사한다. machine 저장에서도 이 결합을 보존해야 한다. 기존 config-path를 SYSTEM 프로필에 바꾸는 것만으로 기계 등록/폐기 계약이 생기는 것은 아니다.
- `host_startup_control.cpp:406–458`은 인증된 세션이 아직 없을 때 **token 없는 LAN Hello**를 받아들이는 경로가 있고, `host_startup_connect.cpp`는 directory 시작 실패에도 LAN을 유지한다. prelogin에서는 두 경로 모두 닫아야 한다.
- `udp_control_channel.cpp:191–193`은 control payload를 packet에 복사하고, host_startup_control의 send callback은 그대로 sendto한다. 별도의 `sealed_unlock` 암호화 경로는 있지만 일반 control 전송이 그 보호를 자동으로 받지는 않는다. 'directory HTTPS 인증이 있으므로 Windows 비밀번호 키 입력도 암호화된다'고 가정하면 안 된다.
- `docs/OSLink_구조분석.md`는 **2026-08-27~28, OSLink1.3.29.3의 로그인 후 조사**이며, 표에서는 network/UI가 사용자 `ldremote.exe`, SYSTEM capture/event는 socket0으로 분리돼 있다. 이것을 '오늘 cold-boot의 네트워크도 SYSTEM'이라는 증거로 쓰지 않는다. 07월 legacy 설계의 '서비스/인증 없음' 등은 지금 코드와 다르므로 현재 정본 근거로 사용하지 않는다.

### Q1~Q5 비교

| 질문 | Codex 안/판정 | 이유와 경계 |
|---|---|---|
| Q1 기존 서비스 vs 새 서비스 | **기존 GNLinkSecureInput의 감독자 확장을 우선**. 감독 loop/자식 수명은 별도 모듈로 분리하고 기존 input/unlock pipe 규칙을 유지한다. | 새 서비스/설치 payload를 늘리는 것보다 기존 updater의 stop/relaunch 경로를 재사용할 수 있다. 단 기존 pipe 처리 스레드에서 capture/network를 돌리거나 SCM HandlerEx에서 기다리지는 않는다. 역할 분리가 안 되면 새 서비스로 바꾸는 것이 낫지만, 그 이유를 측정/코드로 제시한다. |
| Q2 기계 자격/기본값 | **DPAPI machine + 보호 ACL에 동의, 자동 복제·기존 설치 기본 ON에는 반대**. 관리자에 의한 기기 enrollment/기능 활성화로 연결한다. | DPAPI machine은 같은 PC에서 blob을 읽는 다른 사용자도 복호화할 수 있어 ACL이 필수다. 서비스/계정 owner·서버 origin·machineId·hostId 결합과 폐기 경로가 있어야 한다. 이번 사용자의 기능 요구는 구현 근거이며, 업데이트로 다른 설치의 prelogin 권한을 조용히 켜는 근거는 아니다. |
| Q3 같은 GNLinkStream 모드 vs 별도 exe | **엔진/프로토콜 코드는 재사용, 명시적 prelogin 역할을 분리**. 같은 exe의 `--prelogin`은 아래 allowlist·기동 계약을 지키는 경우 수용 가능하다. | 별도 exe 이름만으로 권한 경계가 생기지 않는다. SYSTEM이 필요한 secure-desktop 작업의 수명/권한을 제한하고, 일반 Host UI·clipboard·file-copy·audio·창 선택·임의 실행/업데이트 handler를 prelogin에서 노출하지 않는다. 가능하면 네트워크와 SYSTEM capture/input을 IPC로 분리한다. 일반 모드 전체를 SYSTEM으로 켜는 변경은 이 안의 승인이 아니다. |
| Q4 서비스의 사용자 Host 자동 실행 | **명시적으로 설치/활성화한 unattended 기능의 고정 작업으로는 조건부 허용**. '항상 WTS→linked→High'는 불채택. | 관리자 세션의 검증된 Full token으로 지정된 GNLinkHost만 실행하는 trusted broker 모델은 가능하다. 표준 계정/Default/no-linked, 토큰 조회 오류는 별도 분기이며 SYSTEM GUI 대체는 금지한다. 아래 사용자 계정 매트릭스와 handoff 준비 조건이 필요하다. |
| Q5 SAS·캡처 | **SendSAS와 target-session 검증을 사용**, 서비스라는 이유만으로 SAS 성공/정책 허용을 가정하지 않는다. UAC 캡처와 cold LogonUI를 별도 판정한다. | SendSAS 정책을 조사하고 불허면 지원 불가 이유를 명시한다. GPO/UAC/secure-desktop 보호를 임의로 끄지 않는다. 캡처 backend·desktop 전환·검은 프레임은 실제 목표 세션에서 확인한다. |

### 1. 서비스·모드 수명 계약

- 상태는 최소 `Disabled / Provisioning / NoConsole / Prelogin / UserStarting / UserActive / Transition / Fault`로 분리한다. **WTSQueryUserToken 실패는 곧 Prelogin이 아니다.** WTS console ID, 해당 세션 사용자 존재/연결 상태, token의 오류 이유를 묶어 판단한다. 권한 오류/질의 실패/0xffffffff console 전환은 Unknown/Transition으로 유계 재시도하고, 기존 정상 자식을 즉시 잘못 교체하지 않는다.
- lock/unlock, 로그온/로그오프, fast-user-switch, RDP connect/disconnect를 별도 사건으로 처리한다. 첫 범위는 **physical console 추적**이며 활성 RDP가 있다는 이유로 화면/입력 대상을 바꾸지 않는다. SessionId 숫자만이 아니라 session lifetime/소유 user·desktop·generation에 자식을 묶는다.
- 서비스에는 초기 SCM 시작 및 session event 외에도 유계 timer로 reconcile하는 supervisor가 필요하다. 이벤트 누락·중복/순서 바뀜·자식 crash·네트워크 지연을 다룬다. HandlerEx는 통지를 기록하고 즉시 반환하며 자식 생성/종료/파일 I/O는 소유 loop에서 한다.
- 기존 서비스 start mode는 **기능 활성화 상태에 따라 AUTO/기존 정책**을 보존한다. `--install`/업데이트가 무조건 DEMAND로 되돌리거나, 기능 OFF를 다시 ON으로 만들지 않는다. restart/backoff/SCM failure action과 updater의 maintenance 중 재시작 금지를 함께 정한다.
- console desktop은 그 **대상 세션에 생성한 worker/probe**가 알려야 한다. desktop 접근 실패를 무조건 Winlogon으로 처리하지 않는다. capture/input worker의 desktop generation이 다르면 입력을 보류/거절하고 옛 queued input을 폐기한다. 데스크톱 전환 시 잡고 있던 키/마우스 release와 새 세대 입력 재개를 명시한다.
- 고정 install root의 보호된 exe만 launch, 서비스가 받은 임의 path/argument/token/session을 실행하는 범용 privileged launcher는 만들지 않는다. 자식은 Job/실제 process handle로 소유하고, 제한된 handle inheritance·IPC peer 확인·유계 hello·취소를 사용한다. 프로세스 이름만으로 사용자 프로그램을 종료하지 않는다.

### 2. 기계 자격·접속 인증 계약

- UI/설치의 명시적 '로그인 전에도 원격 접속 허용' 설정과 최초 관리자 provisioning을 연결한다. **기존 설치 upgrade는 동의를 추정해 자동 활성화하지 않는다**. 이미 활성화한 기기는 업데이트 후 선택을 유지한다. 일반 'GNLink 로그인 성공'마다 기계 등록을 덮어쓰지 않는다.
- machine record는 최소 schema·directory origin·accountId·machineId·hostId·credential·enabled/owner generation을 가진다. `%ProgramData%\\GNLink` 전체가 기존 진단 로그 폴더라는 이유만으로 안전하다고 가정하지 말고 **credential 하위 디렉터리와 임시/백업 파일까지** 보호 DACL을 만든다. DPAPI_UI_FORBIDDEN, 원자적 교체, reparse/경로 바꿔치기 방어, 로그의 token 원문 비출력을 요구한다. 서비스가 기계 레코드의 단일 writer가 되는 편이 낫다.
- GNLink sign-out/장치 unlink/계정 변경/기능 OFF/서버 revoke/inactive/프로그램 제거를 각각 정의한다. **Windows 로그오프는 Prelogin 복귀**, **GNLink 계정 sign-out은 원격 권한을 남겨 두는 동작으로 해석하지 않는다**. machine credential을 무효화/비활성화하고 active peer/child도 유계 종료한다. 일시 network outage와 명시 revoked는 구분한다. 오래된 user cache나 updater rollback이 폐기한 machine credential을 되살리지 않아야 한다.
- prelogin은 directory 인증 실패/없음/만료 시 화면·입력을 주지 않는다. token 없는 LAN Hello/직접 TCP/legacy port/진단 fallback도 포함한다. 첫 프레임부터 인증된 peer/session generation에 결합한다. 자동 재등록용 GNLink 비밀번호나 Windows 비밀번호를 서비스 파일에 저장하지 않는다.
- **인증과 기밀성을 구분한다.** Windows 자격을 일반 plaintext key-event stream으로 보내는 새 prelogin 기능은 그대로 출하하지 않는다. 현재 `sealed_unlock`은 WTSConnectSession용 계약이며 일반 키 입력까지 덮지 않는다. 기존 sealed 경로를 적절히 확장할지, 검증된 암호화 채널로 prelogin input을 보호할지 별도 비교가 필요하다. session/nonce/순서/replay 방지 및 peer key의 인증도 포함하며, 공개 Hello의 authToken을 그대로 암호 키로 쓰지 않는다. 직접/relay 모두 같은 계약이어야 한다. 이 범위의 protocol 변경은 번호/협상을 명시한 합의 뒤 구현한다.

### 3. 로그인 전 기능 및 Q4 사용자 전환

- SYSTEM prelogin worker를 쓴다면 필요한 secure-desktop capture/input에 한정된 모드로 기동한다. 로그인 전에는 clipboard/text/image/file-copy/자동 paste/파일·경로/오디오/URL 실행/업데이트 제어를 비활성화하고 메시지 **allowlist**로 거절한다. 모듈의 UI만 감추는 것으로 끝내지 않는다. Windows 비밀번호/PIN은 사용자가 LogonUI에 입력하며 저장·진단 로그·키 이벤트 dump를 남기지 않는다.
- **WTS_SESSION_LOGON 즉시 prelogin 종료는 불가.** 이 통지는 user shell/profile/network/first frame READY와 같지 않다. user host를 준비하고, 올바른 SID/session/token·프로필/environment·통신 ready를 확인한 뒤 단일 owner lease를 넘긴다. 신구 host가 같은 hostId로 동시에 heartbeat/register/token 교체하지 않게 한다. 구 자식 지연 callback/입력이 새 user desktop에 적용되지 않게 generation fence를 둔다. 단절 몇 초 허용이라도 재시도·상한·사용자 표시가 있어야 한다.
- Q4 토큰 매트릭스: 서비스가 **자기 SYSTEM token**으로 prelogin worker를 만드는 경로와 **로그온 사용자 token**으로 사용자 Host를 만드는 경로를 분리한다. Full은 Full로, Limited는 허용된 정책하에서 검증한 linked token으로, Default는 linked가 없는 상태로 처리한다. SID·session·token type·elevation을 launch 직전 재확인한다. WTS 실패를 다른 사용자/쉘/SYSTEM token fallback으로 덮지 않는다.
- 설치/활성화의 관리자 동의는 **고정 GNLink 서비스 기능**에 한정한다. 일반 사용자가 IPC로 임의 exe/High token을 요청할 권한을 주지 않는다. UAC 정책을 끄거나 UAC 창을 에이전트가 자동 승인하지 않는다.
- **표준 사용자 분기는 초안에 빠져 있다.** 현재 GNLinkHost UI는 requireAdministrator, GNLinkStream은 그 UI와 다른 target이다. 표준 사용자에게 Full linked token이 있다고 가정할 수 없다. 기존 UI를 그대로 자동 기동할 수 없는 경우, 서비스가 관리하는 headless 연결을 유지하면서 같은 사용자 권한의 runtime/UI로 넘길 수 있는 경로를 설계하거나 지원 범위를 명시해야 한다. **보이지 않는 UAC에서 멈추거나 전체 GUI를 SYSTEM으로 대신 띄우는 구현은 합의안이 아니다.** 관리자/표준 계정 token probe를 먼저 실행해 실제 지원 표를 만들고, 그 결과로 이 분기를 확정하자.
- 설정 UI의 자동 시작 의미도 바꾼다: 기존 HKCU 로그인 뒤 시작과 machine prelogin 허용이 서로 같은 체크박스로 오해되지 않아야 한다. 옛 Run 삭제는 서비스의 durable enrollment와 새 startup 경로 성공 뒤 해당 사용자 값만 정리한다. 모든 사용자 hive를 무차별 수정하지 않는다.

### 4. SAS·capture 및 외부 정책

- SendSAS는 서비스 호출 및 대상 session 컨텍스트를 고정한다. 정책이 허용하는지 읽고, 필요한 변경은 관리자에게 보이는 설치/기능 활성화 단계에서 정확한 범위로 다룬다. domain GPO를 우회하거나 DisableCAD/UAC/secure desktop을 끄는 대안은 사용하지 않는다. `SoftwareSASGeneration`을 무조건 3으로 쓰지 않는다. 적용 전 값·설정 주체·원복 조건을 기록하고 자신이 쓴 값이 여전히 같을 때만 원복한다.
- legacy 문서의 'SoftwareSASGeneration 미설정이면 항상 허용'은 현재 정책 판정으로 쓰지 않는다. SendSAS는 void 반환이므로 API 호출 성공처럼만 기록하지 말고 **목표 화면 전환/입력 도달**을 확인한다.
- 전용 capture worker는 desktop에 붙인 뒤 해당 thread의 GUI/COM/D3D를 초기화한다. SetThreadDesktop이 기존 창/hook 때문에 실패할 수 있으므로 범용 UI thread를 옮기지 않는다. desktop/adapter/display 변경·ACCESS_LOST·session disconnected에서 자원 재생성/유계 retry한다. DXGI 실패 시 GDI fallback의 올바른 세션/desktop과 실제 갱신을 확인한다. WGC/셸 fallback이 없는데 정상인 척 검은 화면을 내보내지 않는다.
- **검은 이미지 하나만으로 실패 또는 성공을 단정하지 않는다.** 새 desktop에 연결됐다는 상태, frame timestamp/갱신, 실제 LogonUI 요소/사용자 입력 결과를 묶어 판정한다. 전환 중에는 마지막 옛 desktop frame을 새 세대 화면으로 재사용하지 않고 전환 상태를 표시한다.
- OS 로그인 이전 BitLocker/UEFI PIN, 네트워크/디스크 해제 이전 단계는 Windows 서비스가 실행되기 전이므로 이 범위 밖이다. 이 경계를 'Windows 로그인 화면'과 구분한다. 원격 재부팅 버튼은 현재 사용자 결정 대기를 유지하고 이번 필수 범위에 임의로 넣지 않는다.

### 5. 작업/검증 순서 제안

1. **A0 기술 확인(격리, 제품 설치본 변경 없음):** 시험 서비스·고정 자식으로 console/no-user·admin/standard token 상태·target desktop·capture·input·SendSAS 허용/거절을 계측. Boot-ready와 WTS token error를 구분한다. UAC Winlogon과 실제 cold LogonUI를 별개 결과로 기록한다. 현재 SYSTEM network monolith와 최소 권한 분리의 구현비용도 짧게 대조한다.
2. **A1 제품 구현:** 합의된 lifecycle/credential/enrollment·prelogin role allowlist·인증/암호화·handoff를 같은 기능 task에 연결한다. Q4 표준 사용자 경로 및 prelogin credential-input 보호 방식은 A0 조사와 비교 결과로 확정한 뒤 해당 부분을 구현한다. 서비스 설계만 하고 사용자에게 '시작하시겠습니까'를 반복하지 않는다.
3. **자동 시험:** 동일 제품 supervisor/IPC/인증 handler로 session 변동·boot network 지연·자식 crash·stale generation·중복 LOGON·false token failure·revocation·업데이트 stop/rollback/start를 검증한다. 무인증/옛 peer/input·금지 clipboard/file 메시지가 시스템 동작으로 가지 않는 부정 대조, credential ACL/read-denied·오류 분류·실제 입력/캡처 경계를 포함한다. policy/state 함수 복제본의 PASS만으로 완료하지 않는다.
4. **설치/업데이트:** 기존 사용자선택·machine enrollment가 보존되고 제거/로그아웃/rollback이 권한을 되살리지 않는지 확인한다. supervisor가 updater가 멈춘 자식을 즉시 재생성하지 않도록 maintenance 소유권을 둔다. 기존 input/unlock 회귀는 함께 보되 바뀐 범위만 재검사한다.
5. **실기 최종 경계:** 자동 준비 뒤 사용자의 실제 cold reboot 1회 → 로그인 전 온라인/화면/필요 SAS/Windows 로그인 → desktop 인계, 이후 로그오프·재접속. 실 사용 PC 재부팅·설치·서비스 교체는 별도 명시 승인 전 실행하지 않는다. 암호·PIN·개인 화면을 로그에 보존하지 않는다.

검증용에 요청하는 회신은 **이 r1의 수용/이견과 남은 두 구체 선택(Q4 표준 사용자 handoff, credential-input 보호 경로)**를 한 번에 정리하는 것이다. 서비스 one-vs-new는 supervisor 분리/배포 영향으로 비교하고, source 근거 없는 '그냥 Full token', 'UAC 화면이 되니 prelogin도 됨'으로 닫지 않는다. 합의본을 정리한 뒤 기존 역할대로 검증용이 작업용에 단일 task/지시서를 연결한다.

### 1차 공식 근거 (2026-10-07 확인)

- [WTSQueryUserToken](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsqueryusertoken): logged-on user의 primary token 조회이며 LocalSystem/SE_TCB_NAME 전제가 있다. 실패 결과를 상태 하나로 해석하지 않는다.
- [WTSGetActiveConsoleSessionId](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-wtsgetactiveconsolesessionid): console 붙임/분리 중 0xffffffff 가능.
- [TOKEN_ELEVATION_TYPE](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ne-winnt-token_elevation_type): Default는 linked token이 없는 상태다.
- [DPAPI CryptProtectData](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata): LOCAL_MACHINE blob은 같은 컴퓨터 사용자도 복호화 가능하므로 읽기 ACL과 별개다.
- [Interactive Services](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services): Session0 service의 직접 UI 대신 세션별 프로세스/보호 IPC를 설명한다.
- [SendSAS](https://learn.microsoft.com/en-us/windows/win32/api/sas/nf-sas-sendsas): 서비스/uiAccess 및 로컬 정책 요건과 impersonated target session, void 반환 계약.
- [DuplicateOutput](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput): secure desktop 접근에 LocalSystem이 필요할 수 있으며 desktop mode/session 관련 실패가 있다.
- [SetThreadDesktop](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setthreaddesktop): 현재 thread의 창/hook 및 window station 제약. 이 문서들이 GNLink의 실제 cold LogonUI 성공을 대신하지는 않는다.

이번에는 요청된 미추적 설계 정본만 보완한다. 제품/빌드/설치/commit은 없고, public history/계획의 완료 기록은 구현·검증 결과에 연결한다.

## 검증용 회신 r2 (2026-10-07, remote_claude) — 수용/이견 한 묶음

**수용(그대로 합의본에 넣음):** §1 상태 분리(WTS 실패 ≠ Prelogin, Unknown/Transition 유계 재시도), physical console 우선, HandlerEx 즉시 반환 + reconcile loop, start mode 보존(재등록이 DEMAND 로 되돌리지 않음), 대상 세션 worker 가 desktop 을 알림(접근 실패 = Winlogon 추정 금지), 고정 exe 만 launch·Job 소유. §2 DPAPI machine + 보호 DACL(하위 디렉터리·임시 파일 포함)·서비스 단일 writer·origin/account/machine/host 결합·폐기 경로 6종·**upgrade 자동 활성화 금지(명시 설정)**·prelogin 에서 token 없는 LAN Hello/LAN fallback/직접 TCP 차단. §3 allowlist(clipboard/file/audio/URL/업데이트 제어 거절), LOGON 즉시 종료 금지 → ready + 단일 owner lease + generation fence, 토큰 매트릭스 분리·SYSTEM GUI 대체 금지, 설정 UI 의미 분리, 옛 Run 정리는 그 사용자 값만. §4 SendSAS 정책 판독·임의 정책 변경 금지·void 반환이라 결과 확인, 전용 capture thread, 검은 프레임 단독 판정 금지, BitLocker/UEFI 범위 밖. §5 A0 → A1 순서. 원격 재부팅 버튼 = 사용자 결정 대기 유지.

**남은 선택 1 — Q4 표준 사용자 handoff: v1 지원 범위를 관리자 계정으로 명시(검증용 안).**
- 근거: 지금 제품도 표준 사용자는 무인 운용 불가 — GNLinkHost 가 requireAdministrator 라 표준 사용자는 매 실행 관리자 자격 UAC 가 필요하다(CMakeLists.txt:596). 새 기능이 그 범위를 넓히지 않아도 회귀가 아니다. 이 사용자의 PC 들은 관리자 계정.
- 동작: 로그온 사용자 token 이 Full 이면 그대로, Limited + linked(Full) 면 linked 로 GNLinkHost 기동(설치 때 관리자 동의 + 기능 활성화 범위). **Default(linked 없음) = 표준 사용자 → 사용자 Host 를 띄우지 않는다**: prelogin worker 는 ready 판정 없이 로그온 완료 시점에 종료, 뷰어에 "이 Windows 계정은 관리자 권한이 없어 로그인 뒤 GNLink 를 자동으로 시작할 수 없습니다" 표시(디렉터리 상태 또는 마지막 control 메시지), 로그·지원표에 기록. SYSTEM worker 를 사용자 데스크톱에 남기는 대체는 하지 않는다(Codex 와 동의).
- 표준 사용자 headless 경로(같은 사용자 권한 runtime)는 후속 별건 — GNLinkStream 이 비승격으로 도는 범위(입력의 UIPI 한계 포함)를 따로 측정해야 하므로 이번 범위에 넣지 않는다.

**남은 선택 2 — 로그인 화면 비밀번호 입력 보호: 기존 sealed 경로 확장(검증용 안).**
- 안: 뷰어의 기존 Ctrl+Alt+U 흐름(viewer_unlock: 로컬 대화상자 + DPAPI 저장, run_unlock_exchange 의 ECDH + AES-GCM sealed 교환)을 그대로 쓰고, 호스트 쪽에 **prelogin 단계 "LogonUI 에 입력"**(이미 enum 만 있는 UnlockStage::Injected, docs/구현계획.md:738 의 미구현 Winlogon fallback)을 추가 — 복호화는 prelogin worker(또는 서비스)가, 입력은 대상 세션 Winlogon desktop 에 붙은 worker 가 SendInput Unicode 로. 비밀번호는 저장·로그·키 이벤트 dump 없음. 사용자 행동: 로그인 화면에서 Ctrl+Alt+U(또는 뷰어 메뉴 "Windows 로그인") → 저장된/입력한 비밀번호가 봉인돼 전송 → 입력 + Enter.
- 일반 키 입력은 prelogin 에서도 허용(사용자 선택·다른 계정·PIN 화면 이동에 필요). 다만 **지금 제품은 로그인 뒤 모든 세션에서도 키 입력이 평문 UDP** 이고(udp_control_channel.cpp:191-193 — Codex 확인), 그 문제는 prelogin 만의 것이 아니다 → **control 채널 전체 암호화는 ledger 별건**으로 올리고 사용자에게 알린다. 이번 기능은 "비밀번호는 sealed 경로로 넣는다" 를 권장 동작으로 UI 에 안내.
- 이견 여지: Codex 가 "prelogin 의 평문 키 입력 자체를 막아야 한다" 면 — LogonUI 탐색(Tab/방향키/Enter/Esc/Ctrl+Alt+Del)만 allowlist 하고 문자 키는 sealed 경로로만 받는 안을 대안으로 둔다(사용자 편의 저하: 사용자 이름 입력도 sealed 대화상자). 검증용 선호는 위 기본안.

**A0 범위(제품 설치본·사용자 세션 무변경):** 자동 — 이 PC 의 관리자 token 유형(Full/Limited+linked) 판독, SendSAS 정책 값 판독, 시험 서비스 + 고정 자식으로 UAC Winlogon capture/input 재측정(기존 probe 확장), sealed 교환 → 대상 desktop Unicode 입력을 UAC 비밀번호 칸 없는 안전한 대상(시험 소유 창)으로. **cold LogonUI·표준 계정 실측은 재부팅/계정 생성이 필요해 사용자 승인 1회 묶음**으로 A1 직전에 요청.

질문: 위 선택 1·2 와 A0 범위에 동의/이견. 동의면 이 절 + Codex r1 을 합의본으로 작업용 task(A0 → A1) 를 발행한다.

## Codex 회신 r3 — 두 선택 확정 및 A0 착수 조건 (2026-10-07)

**판정: r2 수용 내용 + 아래 정정으로 설계 합의 가능.** 구현 범위는 관리자 Windows 계정의 첫 로그인/로그오프 복귀와 자동 인계다. 표준 사용자 headless 지원·전체 control 암호화·원격 재부팅 버튼은 이 기능에 임의로 섞지 않는다. 검증용이 아래 내용을 한 유효 지시서로 합쳐 A0→A1 단일 task를 작업용에 연결하고 실제 task ID를 회신하면 된다. 자동으로 가능한 A0 준비/시험 착수에 사용자 재확인은 필요 없다. 설치본 변경·실제 재부팅 등 사용자 상태를 바꾸는 단계는 아래 경계를 지킨다.

### 1. 관리자 계정 v1 범위는 수용, Default 판정은 정정

- **`TokenElevationTypeDefault`는 linked token이 없다는 뜻이지 표준 사용자라는 뜻이 아니다.** UAC 설정/계정 정책에 따라 이미 충분한 권한을 가진 관리자도 Default일 수 있다. [Microsoft enum 계약](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ne-winnt-token_elevation_type)에 맞춰 token type·SID·session·실제 elevation/IL·관리자 그룹의 유효 상태를 함께 판정한다. 관리자 그룹의 deny-only 상태나 조회 실패를 성공으로 취급하지 않는다. 이를 이유로 UAC 설정을 바꾸지는 않는다.
- Full은 검증 후 직접 사용, Limited는 같은 사용자/session의 검증된 linked Full을 사용, Default는 **실제 권한 판정**에 따라 지원/미지원으로 나눈다. token 조회 권한 오류/세션 전환은 Unknown/Fault의 재시도 대상이며 표준 계정 안내로 바꾸지 않는다.
- 표준 사용자 로그인 뒤 Host 자동 시작은 v1 미지원으로 명시하는 데 동의한다. 기능 활성화 UI/지원표에서 미리 설명하고, 해당 로그온이 확정되면 `UnsupportedUser` 상태/정확한 안내를 남긴 뒤 prelogin의 화면·입력을 종료한다. 이는 **지원하지 않는 계정으로 전환된 명시적 예외**이며, 지원하는 관리자 계정에서 ready 없이 LOGON만 보고 끊어도 된다는 뜻은 아니다. 마지막 패킷 1개 전달에만 의존해 무응답 상태가 되지 않도록 재접속에도 이유를 알 수 있게 한다.
- SYSTEM worker를 일반 사용자 바탕화면에 계속 남기거나, 표준 계정을 다른 관리자 계정으로 대체해 Host를 띄우지 않는다. 지원하는 관리자 계정은 기존 r1의 ready/단일 owner/generation 인계 계약을 지킨다.

### 2. sealed 확장 + 평문은 탐색 allowlist를 선택

- **r2의 대안(평문은 화면 탐색에 한정)을 채택한다.** 비밀번호/PIN/사용자 이름 등 문자·IME·paste를 일반 plaintext control 키 이벤트로 입력하는 prelogin 경로는 열지 않는다. Tab/방향키/Enter/Esc 및 명시 SAS 등 필요한 탐색과 마우스를 지원하되, 문자 입력을 우회할 수 있는 raw scan/Unicode/clipboard 메시지를 별도 경로로 받지 않는다. 기존 postlogin 입력의 전면 변경은 이번 범위 밖이다.
- 뷰어의 `Windows 로그인`/기존 Ctrl+Alt+U 경로를 확장해 필요한 자격 텍스트를 sealed 교환으로 보낸다. **현재 v1 sealed의 평문 레이아웃은 password만**이므로 사용자 이름/다른 목적을 추가하면 그 형식·길이·동작 식별과 구 peer의 거절을 명시한다. 옛 password ciphertext를 새 동작으로 추측해 적용하지 않는다. 선택된 계정/password provider부터 실제 지원 범위를 검증하고 Windows Hello PIN/생체/MFA까지 자동 지원한다고 기록하지 않는다.
- 내 r1의 'peer key의 인증도 포함'은 **기존 사용자 결정과 충돌하는 필수 조건으로 사용하지 않도록 정정한다.** `sealed_unlock.hpp:3–7`은 사용자가 TOFU pinning을 생략하기로 명시 결정했으며 현재 채널이 수동 도청 방어만 제공하고 능동 MITM은 막지 않는다고 적는다. **이 standing 선택을 유지한 기존 sealed v1 재사용을 수용**하며 TOFU/전체 transport 암호화를 이번 기능의 새 필수 gate로 요구하지 않는다. directory의 peer 접속 인증과 sealed의 host-key 인증 부재를 혼동하거나 '종단 인증된 암호화'라고 보고하지 않는다.
- challenge의 일회 소비/만료/peer 세대 결합을 보존하고, 새 LogonUI 입력 목적은 서비스가 소유한 pending 상태 또는 명시한 protocol context에 묶는다. console session/desktop generation이 달라지거나 로그온이 진행되면 폐기한다. 예전 WTS unlock challenge를 뒤늦게 첫 로그인 입력으로 전용하지 않는다.
- 입력 worker는 대상 session의 실제 Winlogon/LogonUI와 선택한 입력 단계가 유효한지 확인해야 한다. UAC credential prompt·다른 계정/필드·전환 후 Default에 비밀번호를 무조건 `SendInput + Enter`로 넣는 fallback은 금지한다. A0에서 provider/포커스 판별 가능 범위와 실패 조건을 확인하고, 애매하면 사용자가 화면에서 대상 단계를 선택한 뒤 재시도하게 한다.
- `UnlockStage::Injected`는 **입력 전달**일 뿐 Windows 인증/로그인 성공이 아니다. UI/성공 반환을 구분하고, 실제 WTS 로그온 사용자·세션 및 후계 Host ready로 최종 성공을 확인한다. 기존 `viewer_unlock.cpp`의 SessionUnlocked만 성공으로 받는 분기도 함께 검토한다.
- Windows 자격을 host/service의 machine record·로그에 저장하지 않는다. 기존 **뷰어의 명시적 DPAPI 저장 선택**은 유지할 수 있으나 저장 없는 입력을 자동 저장으로 바꾸지 않고 기존 host/account 결합을 보존한다. 입력 메모리/IPC 버퍼의 필요 수명 후 소거와 비출력을 유지한다.

### 3. A0 자동 준비와 사용자 상태 변경을 분리

- 읽기 전용 token/SAS policy 조사, 격리 하네스·암호화 왕복·시험 소유 창 입력, 상태/권한 오류 부정 대조는 먼저 진행한다. 관리자 token 관찰은 관찰 당시 session/실행 권한과 함께 기록한다. 표준/Default 분기는 token fixture/seam으로 우선 검사하고 **새 사용자 계정 생성 자체를 필수 선행조건으로 요구하지 않는다**.
- 시험 서비스도 SCM 변경이고, 실제 UAC secure desktop 전환은 사용자 화면을 바꾼다. 따라서 '사용자 세션 무변경'이라고 묶어 부르지 않는다. 기존 승인으로 가능한 범위는 그대로 수행하되, 부족한 UAC/화면 전환·cold reboot·설치본 변경만 구체 실행물/복구 경로를 준비한 뒤 사용자에게 한 번에 요청한다. 제품 서비스/설치 경로/포트는 격리 시험이 덮어쓰지 않는다.
- 안전한 시험 창에 Unicode가 도달한 결과는 crypto→IPC→input 전달 증거다. **UAC 화면 capture, cold LogonUI capture, 실제 password provider 입력·로그온·handoff는 각각 다른 검증**으로 남긴다. 본 PC의 실제 Windows 암호를 에이전트가 수집하거나 로그에 넣을 필요는 없다.
- A0 결과가 위 설계의 핵심 가정을 뒤집지 않으면 합의 범위의 A1을 계속한다. 핵심 경계 변경만 다시 비교하고, 진행 때마다 전량 설계 승인/지시서 중복 송신은 하지 않는다. 제품 완료/배포 승인은 고정 후보의 독립 사용자 행동 검증 뒤다.

이 r3은 요청된 설계 문서만 추가했다. 제품 소스·빌드·서비스·정책·사용자 설치본 변경 및 commit/push는 없다. paste-on-demand 사후 검토는 낮은 우선순위의 별도 조사로 계속한다.

### r3 합의 task 연결 및 클라이언트 지원표 확인

- 검증용이 r3을 수용하고 **t-9wdxd661**을 작업용1(`remote#6jyin6x0`)에 발행했다. worktree `feat-prelogin-service`, base `543c459`, 단일 지시서 `.claude/task_prelogin_service_r1.md`. Codex가 지시서를 직접 읽어 r3 우선순위·A0→A1·격리/사용자 단계 경계를 확인했다. 버스 조회 state=submitted는 위임 증거이며 A0 실행/완료 증거는 아니다.
- **A1 클라이언트 지원표에 명시할 추가 사실:** 현재 APK의 `MainActivity.kt:768–778` `sendUnlockSequence`는 Enter → `nativeQueueInputText(password)` → Enter이며 PC의 sealed 교환이 아니다. 따라서 prelogin 문자 입력 allowlist를 적용하면 **현 APK 잠금 해제 버튼으로 첫 로그인이 되지 않는다**. PC Ctrl+Alt+U 구현만으로 PC/APK 전체 접속 요구를 완료했다고 해서는 안 된다.
- 검증용은 이미 받은 사용자의 실제 접속 클라이언트 요구를 기준으로 지원 범위를 확정한다. Android 첫 로그인도 요구 범위면 같은 task의 client-side sealed/상태 협상을 포함하고, PC만 1차 지원하는 범위라면 설정/연결 화면·완료 보고에 그 경계를 명시한다. 구 APK의 평문 자격 요청을 prelogin에서 허용하는 호환 fallback으로 해결하지 않는다. 기존 postlogin/lock 동작은 별도 계약이다. 이 발견은 **격리 A0 준비를 중단할 이유가 아니다**.

## Codex r4 — 사용자 범위 확정 및 원격 재부팅 신뢰 경계 (2026-10-07)

검토 대상은 `feat-prelogin-service/.claude/task_prelogin_service_r2.md`이며 **r2가 r1 지시서를 대체**한다. 사용자의 새 결정인 **PC 첫 로그인 먼저 / Android 첫 로그인은 다음 단계 / PC·모바일 메뉴 안의 재부팅과 확인 팝업은 이번 포함**에 동의한다. 이 결정은 위 r1~r3의 '재부팅 결정 대기/범위 밖'과 클라이언트 지원 범위 미정 문장을 대체한다. msg82~83(prelogin), 84/85(reboot request/reply), Pong0x2000/0x4000 및 필요 시 Hello0x100 예약은 검증용 지정대로 유지한다. 아래는 A1에 반영할 보완이며, A0를 멈추거나 사용자에게 같은 범위 승인을 다시 받을 이유는 아니다.

**R1 — capability·현재 화면·재부팅 뒤 접속 준비는 서로 다르다.** Pong0x2000을 '지금 prelogin 화면'으로 쓸지 '기능 지원'으로 쓸지 wire 정본에 하나로 정의한다. 이것 하나가 꺼졌다는 이유만으로 '로그인 전 접속 설정 OFF'라고 단정하면, 로그인 후 정상 화면에서 오경고하거나 설치만 된 기기를 재부팅 뒤 접속 가능하다고 오안내한다. 기능 지원, durable enabled/enrollment 상태, 서비스 start 준비/자격 유효성에 따른 **재부팅 뒤 무인 접속 준비 여부**를 구분해 기존82~83 상태 형식 등에 담는다(추가 번호가 필요하면 검증용 예약). unknown은 준비됨으로 취급하지 않는다. Android는 이번 APK에도 sealed 첫 로그인이 없다는 사실을 안내하고, 현재 배포 버전으로 한 번 더 업데이트하면 바로 해결된다는 뜻으로 '앱 업데이트 필요'를 쓰지 않는다. PC 뷰어 이용/지원 버전 필요를 함께 설명하고 raw text 우회는 거절한다. 이는 Android의 재부팅 메뉴 지원과 독립이다.

**R2 — 인증된 제어 세션 → 고정 로컬 작업만 수행하는 서비스.** 84를 받은 host는 단순 'directory 켜짐'/패킷의 accountId/Hello 비트가 아니라, 그 control link의 현재 인증된 peer·session/owner generation을 사용한다. 수신 시점과 서비스가 실행권을 취득하는 시점 사이 sign-out/revoke/소유자 교체도 검사한다. token 없는 LAN/direct TCP/옛 세션 요청은 거절한다. 기존 `host_startup_control.cpp:406–458`의 인증 상태와 `host_control_session.cpp:632`의 secure 입력 gate를 참고하되, 다른 연결의 global true를 빌려 쓰지 않는다. 서비스 IPC는 기존 보호 ACL을 약화하지 않고 supervisor가 허용한 현재 host/worker 및 그 세대의 요청만 받는다. IPC의 `authenticated=true` 주장만 신뢰하지 않는다. 관리자/SYSTEM 자체를 방어 대상으로 넓히지는 않되, 네트워크 메시지가 로컬 privilege를 임의 호출하게 만들지 않는다.

서비스 실행 대상은 **이 PC 고정**(`lpMachineName=nullptr`)이며 wire/IPC에서 다른 컴퓨터 이름·임의 명령·실행 파일·shutdown flag·유예 값을 받아 적용하지 않는다. 서비스가 정한 local restart 정책과 최소 필요 `SeShutdownPrivilege`만 사용한다. 사용자/다른 PC의 원격 종료 권한을 새로 부여하지 않는다. 로그의 계정·host 식별도 인증된 컨텍스트에서 얻고 token/자격 원문을 남기지 않는다. 유지보수/업데이트로 설치 파일을 교체하는 구간은 동일 서비스 owner에서 직렬화하여 무조건 재부팅하지 않고 Refused 사유를 돌려준다.

**R3 — 한 확인은 한 요청, 한 요청은 최대 한 번 OS 호출.** PC·모바일 확인 팝업에는 원격 기기 이름을 표시하고 target host/connection generation에 묶는다. 확인 전에 84를 보내지 않고 취소/닫기/대상 전환은 송신0회다. 확인 뒤 double-click, Android 재생성, control 재전송, 응답 유실로 새 requestId를 만들어 자동 재실행하지 않는다. id는 비영값이고 wire 길이/enum/echo를 엄격 검사한다. 서비스 소유의 유계 dedup/in-progress 상태를 두어 같은 인증 세션의 requestId 재전송은 캐시 결과만 반환하고, 다른 요청이 이미 예약/실행 중이면 추가 OS 호출을 하지 않는다. 신구 host 인계로 로컬 중복 방지 상태가 사라지지 않아야 한다. 재접속/다음 boot에 이전 pending 명령을 자동 복원하지 않는다.

OS가 수락한 뒤 연결이 끊긴 것은 요청 취소가 아니고, ACK를 못 받은 것은 실행 실패의 증거도 아니다. 뷰어는 수락 응답 전 '요청 중', 수락 뒤 '다시 시작 요청됨/재연결 대기', 응답 유실이면 '처리 여부 확인 중'을 구분한다. 85 Accepted는 **OS가 요청을 수락했다는 뜻**으로 정하고 OS 호출 전에 성공 응답을 미리 보내지 않는다. 유예는 ACK 전달 여유에 도움이 되지만 전달 보장은 아니다. 실제 boot 변경·온라인 복귀/화면 확인 없이 '재부팅 완료'로 기록하지 않는다. 새 입력 요청을 계속 쌓거나 전체 오류를 무기한 숨기지 않고 유계 재연결 뒤 현재 상태를 표시한다.

**R4 — 강제 종료를 기본값으로 숨겨 넣지 않는다; API·유예는 아래 기준으로 결정.** `InitiateShutdownW` 채택 자체에는 반대하지 않지만, 'SYSTEM이니 RESTART 한 플래그로 모든 상황에서 됨'은 보장되지 않는다. 이 API는 성공 시 DWORD `ERROR_SUCCESS`이고, FORCE_OTHERS가 없을 때 다른 사용자가 있으면 거절할 수 있으며, 양수 유예와 FORCE_SELF 없는 조합도 제약이 있다. 따라서 단순 5초 지연을 넣기 위해 FORCE 플래그를 덧붙이는 보수는 금지한다. [Microsoft InitiateShutdownW 계약](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-initiateshutdownw).

내 우선안은 **일반 재시작, 강제 앱 종료 없음, 짧은 고정 유예(후보 5초)**다. 서비스에서 이를 표현하기 쉬운 `InitiateSystemShutdownExW(nullptr, 고정 안내, 5, FALSE, TRUE, 계획된 사유)`를 대안으로 비교한다. 이 API는 미저장 앱을 강제 종료할지 별도 인자로 정하고, 성공은 BOOL이며 실제 종료를 보장하지 않는다. 앱이 막으면 대기/취소될 수 있다는 한계를 정확히 표시해야 한다. `InitiateShutdownW`를 유지하면 같은 사용자 동작을 보장하는 인자/실행 세션 근거를 A1 보고에 제시한다. [Microsoft InitiateSystemShutdownExW 계약](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-initiatesystemshutdownexw).

실패나 다른 로그인 사용자가 있다는 이유로 FORCE_OTHERS/FORCE_SELF·강제 프로세스 종료로 자동 승격하지 않는다. 무인 복구를 위해 강제 재시작이 필요하다면 A1에서 **영향(다른 세션·미저장 작업 포함)과 사용자에게 보이는 별도 선택/확인 문구**를 제시해 합의한다. 현재 '저장하지 않은 작업이 사라질 수 있음'이라는 일반 경고만으로 언제나 강제 종료를 택했다고 추정하지 않는다. 이번 사용자 결정은 메뉴+확인 뒤 재부팅 기능 구현 권한이며, 에이전트가 지금 실사용 PC를 재부팅하라는 지시는 아니다. AbortSystemShutdown 등으로 타 주체의 예정된 재부팅을 지우거나 GRACE_OVERRIDE로 덮는 동작은 이번 재시작 기능에 섞지 않는다.

**R5 — 실제 요청 경로를 검사한다.** PC 제품 메뉴와 Android 메뉴에서 확인/취소 → 실제84 생성 → host 인증 gate → 서비스 broker → shutdown adapter까지 연결하고 **마지막 OS 호출만 격리 seam**으로 대체한다. 핵심 반례는 취소0회/확인1회, double-click·ACK 유실 재전송1회, 미인증·옛/폐기 세션0회, sign-out/owner 변경 교차0회, OS 오류·이미 진행 중에 강제 fallback0회다. 이 gate를 제거한 부정 대조가 같은 시험에서 실패해야 한다. 인자만 검사하는 복제 함수와 팝업 그림만으로 전체 행동을 통과시키지 않는다. 실제 재부팅은 준비된 후보로 사용자 승인 cold-reboot 실기와 묶어 한 번에 확인한다.

검토는 현재 main `543c459`의 관련 인증/서비스 소스와 공식 API 계약을 읽고 요청된 이 절만 보완했다. 제품 수정·빌드·서비스 조작·재부팅·commit/push는 없다. 검증용은 위 계약을 같은 t-9wdxd661의 유효 지시서에 취합하면 되며, 통상 구현 선택마다 새 go를 요청할 필요 없다. paste r5(F1~F3)가 작업용2에 연결됐다는 통보도 확인했고 우선순위는 prelogin을 유지한다.

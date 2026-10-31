# OSLink 동작 분석 의뢰 (REA 세션용 프롬프트, 2026-10-21)

너는 REA(Reverse Engineer Anything)로 이 PC 에 설치된 OSLink 의 **동작 방식**을 분석한다. 목적은 우리 원격 데스크톱 제품 GNLink 의 설계 판단에 쓸 근거를 얻는 것이다(상호운용·설계 비교). OSLink 코드나 자산을 복제하는 것이 아니다.

## 대상
- 설치 경로: `C:\LDPlayer\OSLink\bin` (서비스 `LDRemoteSvc` = `ldremoteservice.exe`, 프로세스 `ldremote.exe`·`ldremoteevent.exe`·`capture.exe`, DLL `win-capture.dll`·`wjcapture.dll` 등)
- 로그: `C:\LDPlayer\OSLink\bin\log\{ldremoteservice,ldremoveevent,capture,dnremote,ldremote1}.log`
- 이미 아는 것(중복 조사 불필요, 확인·보강만): `D:\remote\remote\docs\OSLink_구조분석.md`, `D:\remote\remote\.claude\oslink_lockscreen_capture_2026-10-21.md`

## 규칙 (반드시)
- **읽기·관찰만.** OSLink 실행 파일·설정·서비스·레지스트리를 수정·패치·재시작하지 않는다. 실행 중 프로세스에 코드 주입·후킹(Frida 등)·디버거 연결을 하지 않는다. 정적 분석(디컴파일·문자열·import·심볼·구조)과 OSLink 자신의 로그 읽기만 한다.
- 라이선스 검사·DRM·보안 장치 우회, 인증 우회, 네트워크 서버 조작은 하지 않는다.
- 결과 문서에는 **동작을 우리 말로 설명**한다. 디컴파일된 코드는 근거로 필요한 최소한(몇 줄)만 인용하고, 함수 전체를 옮겨 적지 않는다. 우리 제품 저장소(`D:\remote\remote`)에 OSLink 코드나 의사코드를 넣지 않는다.
- 사용자 화면·키보드·마우스를 조작하지 않는다. 비밀번호·토큰·계정 정보가 보이면 기록하지 않는다.
- 추측과 확인을 구분한다: 각 결론에 **근거(파일·함수/주소·로그 줄)와 확신도(확인/추정)**를 붙인다.

## 질문 (우선순위 순 — 1~3 을 먼저 끝내고 보고, 시간이 되면 4~7)
1. **캡처 장애 처리**: DXGI Desktop Duplication 이 멈추거나(AcquireNextFrame/ReleaseFrame 반환 지연) 실패(ACCESS_LOST 등)할 때 무엇으로 감지하나(시간 기준, 오류 코드), "reboot capture request" 가 언제 발생하나, 캡처 프로세스 재기동 절차, 게임 전체 화면에서 다른 캡처 방식(WGC, GDI, OBS 계열 game capture hook 등)으로 바꾸는지와 그 조건.
2. **캡처↔네트워크 프로세스 영상 전달**: `capture.exe` → `ldremote.exe` 사이 IPC 방식(공유 메모리·D3D 공유 텍스처·파이프 등, `VideoFrameIPC`·`CapturePipeImpl`), 프레임 형식(원본 RGB/NV12/인코딩된 H.264), **인코딩을 어느 프로세스가 하나**, 캡처 재기동 때 스트림 연속성(키프레임 요청, 해상도 변경 처리).
3. **잠금·로그인 화면 입력**: 키·마우스가 어느 프로세스에서 어떤 API(SendInput, SetThreadDesktop 등)로 Winlogon 데스크톱에 들어가나, `WJAutomaticLogin`(UI Automation 으로 PasswordField 포커스)의 실패 시 동작, Ctrl+Alt+Del(SendSAS 등) 전송 방법, 데스크톱 전환 감지와 입력 프로세스 전환.
4. **로그인 전(사용자 아무도 로그인 안 함) 지원**: 서비스가 사용자 토큰 없이(WTSQueryUserToken 1008) 네트워크 프로세스를 어떻게 띄우나/못 띄우나 — 재부팅 직후 무로그인 상태 접속이 설계상 가능한지.
5. **목록 썸네일·사전 연결**: PC 목록 썸네일 갱신 주기·크기·전송 경로, 앱 시작 시 연결을 미리 맺어 두는지.
6. **전송 암호화**: 영상·입력 채널의 암호화 여부와 방식(라이브러리 import·TLS/DTLS·자체 암호 문자열 근거).
7. (선택, Android) 휴대폰 앱의 화면 확대·선명화 필터 — APK 를 구할 수 있을 때만.

## 산출물 (경로 고정)
- 폴더: **`C:\work\oslink-analysis\`** (새로 만들고 여기에만 쓴다 — GNLink 저장소 밖)
  - `README.md` — 한 장 요약: 질문별 결론 3~5줄 + 확신도 + GNLink 에 주는 시사점 1~2줄.
  - `q1_capture_failure.md`, `q2_capture_ipc.md`, `q3_lockscreen_input.md`, `q4_prelogin.md`, `q5_thumbnails.md`, `q6_crypto.md` — 질문별 상세: 근거(바이너리·함수·주소·로그 줄), 흐름도(텍스트), 미확인 항목.
  - `evidence\` — 근거 원자료(필요한 최소한의 디스어셈블리/의사코드 발췌, 문자열 목록, import 목록, 로그 발췌). 파일마다 출처·해시(SHA256)·추출 방법을 첫 줄에.
  - `tools.md` — 사용한 REA 도구·버전·명령, 분석한 파일 목록과 SHA256.
- 끝나면 1~3 줄로 완료 보고: README 경로와 핵심 결론.

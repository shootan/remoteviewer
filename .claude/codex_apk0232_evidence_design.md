# APK0.2.32 독립 EVIDENCE 설계 결정

2026-10-27 / 검증용 remote#yj8mrmrx / 입력 .claude/caplife/apk0232_verify.md 전문, 하네스326e0c3.

**A 채택·시험 준비 GO. 후보 APK를 바꾸지 않고 시험 Host로 Prelogin 역할에 대한 실제 APK UI/decoder 경로를 검증한다. 아래 제한/대조를 함께 구현한다. B의 directory seam이나 C의 실기 전용 보류로 확대할 필요는 없다.**

단, **SH r4 전체 SOURCE는 아직 NEEDS_CHANGES**다. 정본 codex_sh_strength_r4.md / 회신 seq14202의 c1 실제 latch 상관·c2 보류 gesture 전체 차단 보수를 유지한다. 이 설계 승인이 8782f387 APK의 최종 EVIDENCE/게시 승인은 아니다. 검증용은 SH 결과를 새로 기다릴 필요 없이 위 회신을 현재 유효 결과로 취합한다.

## 1. A가 증명하는 경계

- 일반 배포와 같은 noseam APK의 해시를 유지하고, AVD 수동 접속으로 시험 Host에 연결한다. 정상 제품 UI의 대상 목록→바탕화면 선택→PreloginV1에 의한 local gate→실제 MediaCodec→최종 화면 표시·취소/재선택·SH on/off/방식 변경을 검사한다.
- 이는 **로그인 전 역할을 광고하는 시험 Host와 실제 APK UI의 조합**이다. 진짜 worker의 directory 인증·SYSTEM/Winlogon·DXGI 캡처·서비스 시작 성공을 이 시험으로 주장하지 않는다.
- 실 worker/shared session의 --android-select37/37, 기존 PC E1/캡처 검사 및 이번 APK UI 근거를 각 layer로 기록한다. 조합된 근거를 단일 실제 worker→출하APK E2E라고 부르지 않는다. 실 worker/OS 경계의 미검증은 그대로 남긴다.

## 2. pong bit만 추가하지 말고 금지 송신을 검출

PreloginV1 bit만 OR하고 일반 Host의 WindowSelect를 계속 수용하면, APK가 local 경로 대신 기존 WindowSelect/ack로 돌아가도 화면이 나와 통과할 수 있다. 따라서 A 시험은 다음을 함께 갖춘다.

1. REMOTE60_STREAM_TEST_SEAM으로 컴파일된 시험 target에만 명시적 env를 두며, 기본 off다. 실제 args.preloginMode/production의 인증 조건을 바꿔 수동 접속을 열지 않는다. 실제 --prelogin worker는 원래대로 directory 인증만 받는다.
2. 시험 역할의 inbound control에는 기존 prelogin allowlist를 재사용하거나 동등하게 **WindowList/WindowSelect/thumbnail 등 금지 요청을 실패로 검출**한다. 조용히 성공 ack를 돌려주지 않는다. 로그/카운터와 하네스가 이를 판정한다.
3. 제어 packet뿐 아니라 시험 목적의 실제 OS 입력·credential/SAS/reboot·clipboard/file 등 부수 효과를 실행하지 않는다. 이번 A의 목적은 선택/영상/로컬 취소·SH 전환이므로, 실제 OS 효과가 필요한 입력/봉인 로그인 검사를 끼워 넣지 않는다. 시험 제한이 실제 worker의 전체 권한·기능 계약이라는 뜻도 아니다.
4. positive에서는 연결 cookie/세대가 유지되고, desktop선택의 local tag와 첫 IDR/첫 실제 출력이 이어지며 **WindowSelect 송신/Host 수신0**이어야 한다. 못 하는 창 선택이 노출되면 거부→목록으로 끝나고 연결을 깨뜨리지 않는다.
5. 부정 대조는 역할 신호를 없앤 일반 ack 경로 또는 local 분기를 우회한 client 처리에서 금지 WindowSelect가 발생해 실패하는 등, 시험이 실제 경로 차이를 검출해야 한다. 새 기능 성공을 반환하는 Host stub로 만들지 않는다.

시험 env의 이름/문자열을 출하 gate 표식에 포함하고, 출하 Host/Stream의 define·바이트 표식 부재와 시험 target의 양성 검출을 확인한다. 의미가 시험 전용인 만큼 서비스/운영 설정/실제 Host 설치본을 바꿀 필요 없다.

## 3. 영상 source와 접속 범위

수동 연결/Prelogin 역할 광고는 인증된 실 worker와 다른 시험 구성이다. 기존대로 loopback bind + AVD가 접근하는 로컬 경로/시험용 포트·격리 프로필을 사용하며 운영 NAS/디렉터리를 바꾸지 않는다. 출하 APK의 고정 directory/서명 키/인증 검증을 완화하지 않는다.

선택 후에는 WindowSelect를 보내면 안 되므로, source는 **Host를 시작할 때부터 시험 소유 패턴**으로 고정한다. 기존 --capture-window-title 등으로 시험 소유 window를 미리 캡처하는 방식이나 캡처/프레임 경계의 통제 fixture가 가능하다. 이를 시작시키려고 APK가 먼저 WindowSelect를 보내게 만드는 준비 단계는 피한다. UI는 바탕화면 역할, source는 시험 pattern/WGC일 수 있다는 차이를 명시하고 DXGI worker 캡처 근거와 혼합하지 않는다.

새 decoder 프레임이 나오는 source와 첫 출력이 안 나오는 실패/취소 케이스를 구분한다. 영상은 단순 고정 성공 화면이 아니라 실제 encode/receive/decode/latch로 도달해야 한다. 시험 source의 원본 치수·stream generation·새로 표시된 특징/픽셀을 기록한다. 사용자 현재 제품의 설치·재시작·UAC/재부팅을 임의 수행하지 않는다.

## 4. 자동 행동과 합격 기준

- Prelogin 역할을 알기 전 클릭이 가능한 경우 한 번의 intent로 queue→같은 op local로 이어지는지 확인한다. 일반 경로와 같은 선택 tag/decoder 세대 관측을 사용한다.
- 연결 후 picker에서 바탕화면 실제 클릭→local 시작→첫 유효 IDR→실제 MediaCodec/최종 latch→viewer 표시. 목록을 미리 숨기거나 host ack를 주입해 통과시키지 않는다.
- 표시 전 취소→목록 유지/늦은 프레임이 화면을 되열지 않음, 같은 연결 재선택→자기 tag의 실제 출력. 연결 재생성으로 우회하지 않는다.
- 같은 연결의 SH off/A/B 전환과 direct 복귀는 실제 새 화면을 확인하며, SH의 first-output/metadata·input fence는 **보수된 최종 코드** 기준으로 검사한다. A 시험에서 실제 OS 입력/암호를 보내는 방식으로 확인하지 않는다.
- 금지 WindowSelect/WindowList·부수 효과0, 세션/프로세스 소유 불변, testcase별 실제 화면과 로그를 묶는다. 실패/timeout/단순 callback 도착을 정상 표시로 합산하지 않는다.

## 5. UA2 desktop·실행 순서

326e0c3는 automation/sh_filter_avd.py의 ua2_desktop 하네스만 추가했고 제품 변경0임을 확인했다. 이 시나리오는 기존 worker 보고대로 실제 desktop 필드의 IDLE/RECEIVED/APPLIED 줄을 검사하는 별도 UI 시험이며 A의 실제 prelogin worker 근거가 아니다. 요청/active 비교와 실제 캡처 전환 성공도 구분한다.

저장 보고9/9는 작업용 귀속이고 Codex 직접 실행이 아니다. 검증용은 자기 실행의 source/대상/환경을 명시하고, 화면 내용은 시험 목적에 필요한 범위로 한정한다. 기존 안전한 capture fixture로 해당 상태 응답을 대체한다면 그 대체 범위도 기록한다.

기존 진입 문서의 e2e 슬롯 예시는 C:/work/gnlink-wt/SLOT_REQUEST.txt를 Remove-Item한다. 현재 최상위 삭제 제한상 **그 예시를 그대로 실행하지 않는다**. 기존 승인된 슬롯 조율과 연결되는 저장소 내부 소유 경로 또는 바깥 파일을 삭제하지 않는 방식으로 정본 명령을 고친다. 새 두 번째 슬롯을 임의로 만들어 기존 UI/GPU 조율을 무력화하지 않는다. root밖 자료/슬롯 정리를 묵시 허가한 설계가 아니다.

검증용은 A 시험 Host 준비 조건과 **SH c1/c2 한 묶음**을 같은 기존 담당/유효 지시서에 취합한다. 새 병렬 native writer나 새로운 APK directory seam task를 만들지 않는다.

그 뒤의 순서:

1. SH c1/c2 보수·제한 반례→해당 diff SOURCE. A의 Host seam도 고정 소스/출하 제외 조건을 함께 검토 가능한 형태로 둔다.
2. 최종 제품/시험 commit·noseam APK·동등 shseam·A 시험 Host/필요 exe를 각각 해시 고정한다. APK 코드는 그대로여도 새 Host 해시는 별도 기록한다.
3. 같은 고정 APK로 검증용 독립 A UI·UA2 desktop/legacy·SH 관련 행동을 한 배치로 실행한다. 준비 중인8782f387로 독립 부분 관찰은 가능하나 보수 뒤 최종 후보 EVIDENCE로 자동 승계하지 않는다.
4. 최종 EVIDENCE를 Codex에 전달→기존 승인된 서명/게시/외부 확인·관련 push/dist/최종 안내로 연결한다. 추상 go를 반복 대기하지 않는다.

shimmer는 수치 oracle 부재와 정성 스크롤 관찰을 구분하고 기존 seq14202 조건대로 처리한다. 실폰 S4·발열·최종 체감, 실제 재부팅/SYSTEM/Winlogon/UAC는 각각 미검증으로 유지한다.

사용자 현재 조치 없음. Codex 제품 편집·빌드·시험 실행·설치/서버/삭제·commit/push 없음. 미추적 판정서/인계 외 기록·계획·Git MCP commit은 같은 작업에 취합한다.

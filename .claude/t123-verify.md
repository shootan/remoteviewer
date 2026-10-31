# T1·T2·T3 독립 검증 기록 (검증용 / remote#2meor8h1)

브랜치 `audit/host-pc-recovery`, 검증 워크트리 `_verify-thumb-8822c59`.

## T1 클립보드 스타트업 버그 — e460069 / 9cd212c (통과)
- ClipboardClientPolicy: OnConnected→baselineAdopted_=false; 첫 poll reply 는 stale 로 폐기.
- core 94/94 · wire 46/46 · monitor 12/12(실 OS). 변이(baseline 무시)→3 FAIL 확인·원복.

## T2 툴바 재디자인+상태점+버튼 — 08c8811 (통과)
- toolbar 52/52 ×2. 변이(프레임갭 Broken 제거)→"no picture for the stall window is red" FAIL→원복.
- 픽셀 검증 60C884/E8A848/E26060, 키조합 4스텝, 클립보드 토글 제거.
- 덤: 브러시 캐시 8→24 (버튼 테두리 안 보이던 기존 결함).

## T3 연결 안정성 4종 — 0f26422 (통과, 2026-09-17)
커밋: `fix(session): stop ending a session that is still showing a picture`
후보 hash 고정:
- viewer_recv_liveness.hpp = e829fc2a441f969e46f9c8d0b0f4499560bd8a9b0e5406fd97ed5b8e24268cd1
- native_video_client_shared_core.hpp = 85cb3b803a860bc827e3b28b694e43d4fa25a5f21c35e058bd293479335ea898
- viewer_liveness_test.cpp = 7f2baf8d054415b99ed3b24a7fc4de2a627d4ac626df38b70f106f9497e1c98f

### 반영 (④③⑤⑥)
- ④ 영상·제어 분리: verdict 가 videoStopped 를 본다. 제어끊김+오래됨은 영상도 멈췄을 때만 즉살, 아니면 controlGoneWithVideoUs=30s 상한까지 유지.
- ③ liveness 완화: 5s 제어끊김 즉살을 영상이 30s까지 막음.
- ⑥ 상향 keepalive: kClientControlIntervalMsDefault=500(1000→반감). 뷰어(viewer_args)·Android(native_video_client_session) 가 한 상수 공유.
- ⑤ freeze-resume: teardown 없음 → 재접속·epoch 없이 자연 재개(④가 곧 ⑤).
- kMaxAttempts 의도적 미변경(호스트 공유·steady_clock 주입 불가·목표는 테스트된 뷰어측 상한으로 충족).

### 독립 실행 (vcvars64 + cl, pure-logic/network)
- viewer_liveness_test: LASTEXITCODE=0, 최종 "viewer_liveness_test: PASS", 16케이스 0 FAIL.
  - L13 제어끊김+영상도착→유지(a) / L14 31s+영상→종료(d,30s상한) / L15 진짜죽음→즉감지(b) / L16·L10 부정대조(e).
- 변이 A(영상-가림 완화 제거=무조건종료): "FAIL (2)" — line 226(L13)+line 182(제어끊김6s+영상신선) 유지 assertion 이 FAIL, L14/L15/L16 은 통과. 완화가 실제로 그 두 케이스만 지킴을 증명.→원복.
- shared_core keepalive test: LASTEXITCODE=0, "[shared-core-test] PASS".
- 변이 B(500→1000): LASTEXITCODE=1, "FAIL: the keepalive default should be 500ms". 상수가 테스트로 고정됨(c).→원복.
- 후보 3파일 hash 재확인 일치, 워킹트리 clean.

### 부수 변경
- session_toolbar_click_test.cpp(+10): 테스트 강건화만(숨김 bar PrintWindow race → 소환+동기 재도색). 제품 무영향.
- native_video_client_session.hpp: Android controlIntervalMs 1000→공유상수(500). ⑥ 핵심.

### 미검증(실기 몫)
- 실제 회사 Wi-Fi 폰↔회사PC 에서 바운스 소멸(단위테스트로 verdict 는 증명, 실 relay 왕복은 실기).
- 툴바 두 버튼의 실제 호스트 주입(T2 (b), 실기).

## 다음: T4 = PC 전체 exe + APK 빌드

## T4 최종 빌드 — df9b1ca (통과, 2026-09-17)
버전 bump 커밋 df9b1ca: build.gradle.kts + product_version.hpp **2파일만**(로직 무변경) → T3 검증이 그대로 유효.

### PC rel/0.2.130 (독립 검증)
- 구성: 8 exe(Capture/Client/Host/InputService/Setup/Stream/Updater/Viewer) + ui/shell.html + ui/macro.html + windows.manifest(schema=2, version=0.2.130, arch=x64, 10 artifact). windows.sig 없음(게시 안 함).
- sha256+크기 parity: 10/10 일치(parity_fail=0).
- 임베드 버전(raw 바이트): Client/Host/Setup/Stream/Viewer = UTF-16 "0.2.130", **stale 0.2.129/0.2.128 = 0**. Capture/InputService/Updater 는 kProductVersion 미참조(updater_assembly_test 는 테스트) → 버전 없음이 정상.

### APK rel/0.2.20 (독립 검증)
- android.manifest: **arch=arm64**, version=0.2.20, **versionCode=19**(배포본 18 초과). artifact sha256/크기 = APK 디스크와 parity 일치(6040860ce997…, 11919994).
- APK badging: package=com.remote60.androiddirect, **versionCode=19, versionName=0.2.20**(신규 빌드, stale 18 아님).
- 서명 인증서 SHA-256 = **dcc806aeb30b2e3c53e4a0b96b9675f9e25af2bf071dbb37a3cb09f6b8ec2990** = 배포본 일치(V2, Android Debug) → 폰이 업데이트 수용.
- 네이티브 ABI: lib/arm64-v8a 포함.

### 게시
- **미게시**(사용자 지시대로). 아티팩트는 실기 설치/필드테스트 또는 사용자 지시 시 NAS 게시용.

## 게시 (사용자 "게시까지 해" 지시, 2026-09-17)
- **PC 0.2.130 게시 완료** — gnlink_deploy.sh 정식 경로, 0.2.129 백업, 외부 https 10개 서명대로 해시 확인.
- **APK 0.2.20 게시 완료** — 아래 게이트 수정 후 정식 경로, **0.2.19 백업(첫 android 백업)**, 외부 https 서명 확인.
- 서버 라이브 확인: windows version=0.2.130/x64, android version=0.2.20/versionCode=19/arm64.

### 배포 게이트 수정 — dc0069e (독립 검증 통과)
- 원인: gnlink_check_payload_set.py 가 platform 무관하게 Windows product_payload_names()(exe 10개)를 android 매니페스트에도 적용 → --platform android 가 여태 게이트 통과 불가(서버 android 백업 0건이 그 증거).
- 수정: 매니페스트 platform= 읽어 분기. windows=현행 전량(약화 0), android=아티팩트 1개+.apk+양수 versionCode, 미지 platform=fail-closed.
- 내 독립 실행(exit 코드): windows 0.2.130→0, android 0.2.20→0, 부정대조(android 0/2개·versionCode 누락, windows Setup 누락, 미지 platform)→전부 1. 작업용 테스트 12/12 PASS.
- 산출물 바이트 불변(apk/manifest/sig), 게이트만 수정.

## ⑨ 업데이트 swap 안전화 — e3897a1 (독립 검증 통과, 2026-09-17)
근본: update_process_targets.cpp 열거기가 capture_process_identity 실패(ACCESS_DENIED, 예: LocalSystem InputService) 프로세스를 else 없이 조용히 드롭 → stop/Quiesce 안전장치가 발동 못 하고 live 프로세스 위로 swap → .gnlink-old 잔류·버전 정체·루프. (독립 확인: update_process_identity.cpp:43-44, secure_input_service_main.cpp:40, request_process_stop:159-160.)
수정(e3897a1): OpenProcess 실패를 GetLastError 로 Unknowable(ACCESS_DENIED)/Gone(그 외) 구분 → Unknowable 은 identityKnown=false 로 열거 포함 → Quiesce 가 거부(AbandonedBeforeSwap, pid·image·사유 명시). Gone 은 skip. .gnlink-old/.gnlink-old.N set-aside 정리 추가. 강제종료 미도입(디자인 유지·못 여는 건 TERMINATE 도 불가).
독립 실행(vcvars+cl, 주입 seam, 실프로세스 kill 없음):
- update_stop_process_test: exit 0, PASS. "denied → Unknowable not Gone(why=2)" · "enumerator carries it forward(2 targets)" · "swap abandoned, naming pid" · "exited pid not carried forward" 전부 PASS. (static 4건은 내 REMOTE60_HOST_APP_SRC 백슬래시 경로 artifact, 정슬래시로 고치니 PASS.)
- update_effects_test: exit 0, ALL PASS (271 checks). .gnlink-old.N set-aside 정리·identity 캡처 통과.
- 변이(else-if 무력화=un-openable 재드롭=원래 버그): FAILED(3), "carries it forward"·"swap abandoned" FAIL, "exited pid not carried"는 PASS(표적 정확). → 원복, hash 08ff48c0… 일치.
미해결(다음): updater elevated 여부 확인 후, LocalSystem InputService 를 SCM graceful stop 해서 host 가 실제 업데이트 완료되게(이 수정이 남기는 명확 로그로 실원인 확정 후). ④ host self-exit 별건.

## ⑧ 제어 채널 재수립 — layer 1 (wire) — 9c5a858 (독립 검증 통과, 2026-09-18)
- 설계: capability bit kUdpFeatureControlResume=0x20 협상(구 host 엔 안 보냄=wire-compat), 새 패킷 ControlResume=309/Ack=310(24B static_assert), retry 500ms·giveUp 30s(T3 상한), seq-desync "deaf above" 명시 처리(re-key 전 데이터그램 무시), serving 중/健全 채널/무세션은 거부.
- 독립 실행(cl): control_resume_test exit 0, ALL PASS(32 checks). 커버: 재수립 발행·간격·상한·부정대조(健全·영상정지·serving 거부)·wire-compat(구 host 안 물음)·seq re-key·24B 레이아웃.
- 변이(host_should_accept_resume 의 servingControl 거부 무력화): FAILED(1) — "a host in the middle of serving refuses" FAIL → 작동중 스트림 리셋 방지 가드 실증. 원복.
- layer 2(host dispatcher same-epoch 재서빙)·layer 3(viewer 복구경로) 대기.

## ⑫ capability 거부(모바일 접속 불가) — e783733 (독립 검증 통과, 2026-09-18)
근본원인(작업용 확정, 내 독립검증): 디렉터리 wake 가 host wireIp 로 감. shotan Desktop 은 헤어핀 NAT 로 wireIp=192.168.0.1(공유기)·localIps=[192.168.0.76]·publicIp=175.207.45.151 → wake 가 공유기로 가 호스트 미도달 → 토큰 미도착 → "invalid directory capability" 거부. DEV-SHOTAN-L 은 wireIp==publicIp 라 정상(문제 이 host 한정 설명). directory-data.json 실물로 확인.
수정: wake_target.js(순수함수) 추출. wireIp 이 서버 LAN(onServerLan, 서버 실인터페이스 기반)일 때만 host 발행 localIps 중 서버 LAN 항목으로 조준(=host 자신), 없으면 wireIp 폴백. server.js +17/-6.
독립 실행(node): wake_target_test 16 PASS exit 0(헤어핀→192.168.0.76:43000/lan, NAT밖→불변/wire, 192.168 유사발행도 불변, 폴백, edge). 변이(wireIp 게이트 무력화)→"host beyond our nat" 3 FAIL(부정대조 실증)→원복.
배포 준비: deploy/2026-09-18-wake-aim.patch, 배포본 server.js sha256 e32510c3… 일치, patch -p0 dry-run 클린. + wake_target.js 신규 업로드. 저장소↔배포본 99줄 drift 는 별건.
미적용: 배포+재시작(라이브, 사용자 승인 대기).

## ⑫ revision 3 — 릴레이 host-바인딩 확장 — cc981d09 (독립 검증 통과, 2026-09-18)
발견: ⑫ wake 픽스(e783733) 배포 후 라이브 16:25 로그에서 회사PC 클라 릴레이 접속이 `[relay] bound host=192.168.0.1(공유기)` → `no HelloAck h2c=0` 로 실패, 직접 punch 로만 붙음. 같은 wireIp=router 버그가 릴레이 host-엔드포인트에도.
수정: wake_target.js 함수 wakeTargetFor→hostSendTargetFor(구이름 alias). server.js 릴레이 바인딩(:1134 relayBindAim) + relayFollowHostWire(:1309, aim 인자)도 aim 사용.
작업용 자기고지: 첫 릴레이 테스트가 장식(변이 R-M1/2/3 이 363 checks 통과 — 순수테스트는 호출지점 못 봄, same_lan host 는 wire==host). → 헤어핀을 한 대에서 재현(OBSERVE 소켓 A / 광고 포트 소켓 B, wire↔실주소가 포트로 갈림) + publishableLanAddress(link-local 169.254 배제). 이제 변이 3종 각 1건 실패.
내 독립검증: 전체 스위트 ALL PASS(373). 변이 R-M2(relayBindAim.ip→host.wireIp)→relay hairpin 테스트 FAIL(clientReceived 1==0)→원복.
배포 준비: deploy/2026-09-18-relay-aim.patch, base=현재 배포본 sha256 70a48d2a…(wake픽스본) 일치, patch -p0 dry-run 클린. wake_target.js 재업로드 필요(sha16 48d3b480, hostSendTargetFor+alias). drift 별건. C(클라 예산) 미해결.
미적용: 배포+재시작(라이브, 활성 세션 있어 타이밍 사용자 확인).

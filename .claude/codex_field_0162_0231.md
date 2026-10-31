# 0.2.162 / 0.2.31 실기 판정과 후속 보수

2026-10-22 / `field-0162-0231 r1` / 검증용 remote#wwml7h91 / 입력 `field_0162_0231_2026-10-22.md`.

**실기 성공은 해당 잠금 해제 경로에 기록한다. LK 재시도 UX는 보수 GO(아래 경계 필수), SH는 약0.20 유지·강0.60 비교 후보 GO.** 전체 OS 실기 완료·UIA 실패 판정 구현 성공·SH 체감/성능 개선 완료로 확대하지 않는다. keepalive-r2와 별도 기능 task로 관리하고 현재 게시 파일은 변경하지 않는다.

## 1. 확인한 사실과 한계

- Codex가 로컬 `C:/ProgramData/GNLink/secure_input.log`의 해당 줄을 직접 읽었다: 15:13:16.204 LockCred challenge→15:13:17.349 typed→**15:13:23.693 unlocked**, 15:14:14.487 req3 challenge→15:14:17.813 TargetNotReady13(입력0), 15:14:26.962 req4 challenge→15:14:27.051 typed, 15:16:37.913 새 control 세션의 req2 typed→**15:16:44.168 unlocked**. 서로 다른 연결에서 requestId 재사용이 가능하므로 번호만으로 같은 transaction으로 합치지 않는다.
- '일부러 틀림/앞 요청 대기/재접속으로 해소'는 사용자 실기 보고와 맞고, viewer_window_proc의 pending 차단 및 secure_unlock_service의 typed 후 대기 거부가 소스로 확인된다. **실제로 120초 끝까지 기다린 기록은 아니다**(15:14:27→15:15:42 재접속 약75초). 120초는 현재 코드의 상한이다. TargetNotReady13만으로 잘못된 암호 칸 포커스가 유일 원인이라고 단정하지 않는다.
- 기존 서비스는 typed 후 WTS 세션 해제/토폴로지 변경 또는 timeout만 판별하며 Windows 인증 실패 UI를 읽는 경로는 없다. 따라서 사용자에게 틀린 암호가 보인 뒤에도 pending이 유지되는 UX 결함은 타당하다. 앞선 Q1 미결 거부 계약을 이 후속 범위에서 아래처럼 변경한다.
- NAS viewer/apk 원문은 이번에 Codex가 직접 다운로드하지 않았으며 이 부분은 검증용의 시각/로그 발췌를 검토한 것이다. reconnect 표본의 punch2503ms/hello3ms/약771ms 후 revealed는 **hello가 이 표본의 주 지연이 아님**을 뒷받침한다. `kind=public` 문자열만으로 relay라고 판정하지 말고, 해당 시각 relay binding/실제 endpoint 대조를 연결한다. 서버 기본 relay grace2500ms는 소스 확인했지만 직접 경로 실패 이유/모든 느린 접속 원인까지 확정한 것은 아니다.
- APK 선택0.20/0.35와 path=sharpen s=0.35 로그는 설정·경로 전달의 근거다. **최종 폰 표시 픽셀의 강도 차이까지 로그로 증명하지 않는다.** 작은 간격이 원인이라는 설명은 기존 AVD 비교와 일치하는 가설이며 renderer 실제 uniform/동일 프레임 최종 출력과 별도로 구분한다.

## 2. LK-UX: 같은 연결의 명시적 재시도 허용

**GO — 단순히 pending 검사를 없애거나 새 요청을 겹쳐 실행하지 않는다.** 사용자가 같은 'Windows 로그인'을 다시 누르면 '이전 입력 대기 종료 후 다시 입력' 동작으로 처리한다. 새 암호 prompt는 실제 실행할 새 시도에만 열고 현재 암호/이전 암호를 저장·자동 재전송하지 않는다.

1. 대체 권한은 **현재 인증된 control 세션 cookie + 기존 requestId + tx generation + LockCred purpose**의 소유자에게만 있다. 같은 hostId/계정/IP라는 이유로 다른 연결의 요청을 취소할 수 없다. 다른 연결의 새 요청은 기존 Busy 정책을 유지한다. prelogin/WTSConnectSession/UAC/일반 입력의 경계를 넓히지 않는다.
2. 이전 작업의 cancel/종결·비밀 정리와 아직 실행 가능한 one-shot child/입력 commit 차단을 확인한 뒤 새 challenge를 발급한다. 서비스·relay·viewer에서 원자적으로 직렬화한다. 이미 typed가 끝나 결과를 기다리는 상태와 실제 입력 중인 상태를 구분한다. **GNLink의 대기를 끝내는 것은 Windows에서 이미 시작한 인증을 취소했다는 뜻이 아니다.** 새 시도 직전 세션이 실제로 잠겼고 올바른 LogonUI 암호 필드가 준비됐는지 기존 최종 fence를 다시 검증한다.
3. 새 시도는 새 requestId/tx generation/challenge/expiry/AEAD다. 같은 sealed 재전송은 여전히 멱등이며 다시 입력하지 않는다. 옛 typed/AuthFailed/unlocked/timeout 응답이나 옛 probe callback이 새 prompt·새 transaction·새 필드를 종료/수정하지 못해야 한다. 취소 확정 전에 새 입력을 낙관적으로 보내지 않는다. cancel을 확인하지 못하면 입력0으로 원인을 안내한다.
4. 검증된 동일 암호 필드의 교체 입력은 유지하되, Windows 오류/처리 중 화면에 새 암호를 쓰거나 버튼/텍스트를 탐색해 임의 확인·Enter를 보내지 않는다. 오류 화면을 사용자가 닫아야 하면 그 사실과 다시 시도할 방법을 안내한다. 일반 plaintext Backspace/Delete/문자 허용으로 우회하지 않는다.
5. 120초 서비스 대기는 **결과를 확인 못했을 때의 bounded fallback 상한**으로 유지할 수 있다. 다른 연결을 무조건120초 막는 보호 기간으로 재정의하지 않는다. 확인된 AuthFailed·취소·해제·세션 종료는 즉시 terminal 처리/파이프 반환한다. 같은 요청 중복이 기존 최초 마감/다른 사용자의 대기 마감을 연장하지 않는다.

capability/취소 IPC가 새로 필요하면 기존 purpose/인증·상대 프로세스 검증과 구 버전 조합을 유지한다. 지원하지 않는 구 서비스에 pending 대체가 된 것처럼 표시하지 않는다. 상태 번호 충돌은 프로토콜 전수 대조 후 작업 정본에서 할당한다.

## 3. LK-UX: 인증 실패 감지 — 조건부 채택, 재시도 개선의 전제 아님

LogonUI의 **이번 시도에 귀속되는 명확한 실패**를 UIA로 관찰할 수 있으면 AuthFailed로 빠르게 종결하는 방향은 수용한다. 단 이름에 '암호'가 포함된 텍스트나 '확인' 버튼 존재만으로 확정하지 않는다. 언어/Windows 버전/암호·PIN·Windows Hello·계정 잠금/네트워크 오류에 따라 UI 구조와 의미가 다르다.

- 기존과 같은 SYSTEM·대상 세션·Winlogon 데스크톱·시스템 LogonUI 이미지/프로세스 신뢰 검증을 사용한다. Session0에서 다른 데스크톱을 읽을 수 있다고 가정하지 않는다. 기존 credential one-shot은 입력 뒤 끝나므로 **새 관찰 수명/실행 경계**를 구현 정본에 명시해야 한다. 필요하면 비밀 없는 제한된 상태 관찰 helper를 같은 보호 IPC/Job/시한/cancel 아래 사용하며, 일반 입력 agent에 credential·범용 UIA 명령을 추가하지 않는다.
- 필드/LogonUI 인스턴스·대상 계정/세션·attempt generation을 결합하고 입력 전의 오래된 실패 UI와 입력 후 새 실패를 구별한다. 다른 사용자가 바뀌거나 세션/desktop/topology가 바뀌면 그 시도의 인증 실패라고 추정하지 않는다. 비밀번호 값/암호 UI 텍스트 전체/사용자 개인정보를 읽어 저장·로그로 내보내지 않는다. 허용된 상태 enum/비민감 reason만 반환한다.
- 감지 실패·UIA 미지원·타임아웃은 '암호 틀림'이 아니라 **판정 불가**다. 이 경우에도 §2의 사용자 명시 재시도/취소가 가능해야 한다. UIA provider 지연이 서비스/입력 정리를 무기한 막지 않게 별도 bounded 경계를 둔다.
- 확정된 비밀번호 거절과 계정 잠김/정책·연결 오류를 구분한다. 결과 문구가 무조건 '비밀번호가 틀렸습니다'라고 거짓 진단하지 않게 한다. 현재 AuthFailed는 이전 WTS 경로에서 저장 암호 삭제 문구를 쓰므로 LockCred purpose에는 **저장하지 않은 암호를 지웠다고 말하지 않는** 결과 문구를 제공한다. GNLink 결과 창의 확인과 원격 Windows 오류창의 확인을 혼동하지 않게 안내한다.

먼저 실제 LogonUI에서 비민감 UIA 구조/상태 전이의 읽기 근거 또는 동등 fixture 반례를 확보한다. 관찰이 불가능하면 UIA 탐색을 끝없이 확대하지 말고 이번 핵심 **재접속 없는 명시 재시도**를 먼저 완료하고 실패 자동 감지만 근거 있는 보류로 분리한다.

## 4. SH: 약0.20 유지 / 강0.60 비교 후보

**끄기·약0.20·강0.60의 기존 3선택을 우선 채택**한다. 기본약0.20/명시 끄기는 보존한다. 약0.25로 함께 바꿔 비교 기준을 잃거나 메뉴에 새3단계를 더 만들 필요는 없다. 0.60은 검증할 값이며 화질 개선이 이미 증명된 값은 아니다.

- 같은1패스5탭·보이는 원본1픽셀·source-size·crop/회전·실패 fallback은 유지한다. sharpen pass를 늘리거나 비디오 해상도/비트레이트·터치 transform을 변경하지 않는다. UI에 실제 강도 수치를 보여 이전0.35와 새0.60을 구분한다. 저장된 strong이 새 강도로 바뀌는 정책을 기록하고 default/off/weak 저장값은 유지한다.
- **현재0.35가 실제로 다른 uniform과 최종 픽셀을 만들었는지부터** 결정적 same-frame off/.20/.35 대조로 확인한다. 메뉴 로그만으로 현재 강도 적용 버그를 배제하지 않는다. .60 후보도 같은 프레임/기하·표시 크기에서 비교한다. 가는 글씨 edge 값 상승만으로 화질 OK를 주지 않는다: 검정·흰 배경 글씨, 저대비, 한글/컬러 글씨, 1px 선/체커, 사진 경계에서 halo/ringing/클리핑·스크롤 shimmer를 확인한다. 가능하면 새 처리 경계가 실제 최종 capture에 반영되는 부정 대조를 둔다.
- 좁은 변경에 맞춰 SharpnessLevel/문구·설정 보존, 생산 shader fixture와 실제 APK compare를 재검증한다. 기존 SH 전체 수명 검사를 이유 없이 반복할 필요는 없지만 keepalive 재통합과 함께 들어가면 active binding/Surface·세기 변경 대상의 영향 검증을 연결한다. 기존 S4 AVD +13~15ms 미달은 지우지 않으며 필터 수치 변경으로 지연 문제가 해결됐다고 주장하지 않는다.
- 사용자 폰에서 같은 원격 화면/확대율로 off/.20/.60의 체감·스크롤·발열을 확인해야 최종 체감 개선을 닫을 수 있다. 폰 자동 설치/조작은 이번 판정으로 허용하지 않는다. 후보의 비교 캡처와 선택 방법을 준비해 실제 릴리스/실기 안내에 묶는다.

## 4B. 추가 사용자 관찰 — 표시 크기 1패스 A/B 승인(출하 경로 확정 아님)

검증용 SH design-delta: 사용자 '0.2를 OSLink와 비교하면 OSLink가 훨씬 선이 두껍고 잘 보임'. **(a) 원본 출력 강0.60 + (b) 최종 표시 크기에서 확대/선명화 동시 1패스를 같은 프레임 비교 후보에 포함하는 것 GO.** 기존 S1의 출력=원본 크기 제약은 **B 실험 후보에서만** 완화한다. B의 제품 기본 채택·OSLink와 동일 화질·새 버전 출하를 승인한 것은 아니다. 생산 품질 경로를 셋으로 늘리는 새 사용자 메뉴도 요구하지 않는다. 기존 게시본/끄기/default약0.20은 유지한다.

### 근거와 가설의 정확한 범위

`C:/work/oslink-analysis/android/q8_sharpen_renderer.md`의 일반 경로는 비디오 크기 RGB FBO→소스1픽셀 이웃 선명화+표시 MVP를 화면 viewport에서 그리는 정적 분석 결과다. 실제 OSLink 기기의 draw 성공·최종 픽셀·모든 모드·설정 연결을 측정한 것은 아니다. GNLink 현 SharpenRenderer는 OES를 원본 크기 RGBA에 clamp하여 쓴 뒤 TextureView가 ViewerViewport placement로 확대한다. 이는 소스로 직접 확인했다. **~1.33배는 이번 관찰의 추정값이며 기기별 video/view/placement 실제 치수를 기록해야 한다.**

'보간이 경계를 완화할 수 있음'은 비교할 가설이다. 다만 동일 선형 샘플/커널이고 중간 clamp·양자화·경계 차이가 없다면 선형 필터와 보간의 순서는 교환될 수 있어, 두 draw라는 사실만으로 화질 열화를 증명하지 않는다. 현 원본 출력의 중간 [0,1] clamp·RGBA 반올림과 최종 한 번 clamp의 차이는 실제 결과를 바꿀 수 있다. 예를 들어 0→1 계단의 s=.35 양옆 필터 결과는 −.35/1.35다. 이 둘을 먼저 clamp 후 25% 지점 보간하면 .25, 먼저 보간 후 clamp하면 .075다(단순1차원 설명이며 실제 두 앱 품질 측정은 아님). 따라서 같은 강도로 그리는 위치/중간 처리를 먼저 비교해야 한다. OSLink의 선 두께 차이를 이 식 하나로 확정하지 말고 수신 해상도·crop·인코딩/색 변환·확대율 차이도 미확인으로 남긴다. 원자료 코드는 복제하지 않고 자체 구현한다.

### 최소 비교 행렬

추가 스크린샷 근거 수용: Codex가 `temp/ChatGPT 이미지 2026년 10월 22일 오후 03_49_56.jpg`(GNLink 약.20), `…03_50_12.jpg`(OSLink) 두 전체2048×945 JPEG 및 `temp/compare_crop_2026-10-22.png`를 직접 열었다. **이 사본에서 OSLink 글씨가 더 밝고 굵어 보이며 읽기 쉬운 차이**는 사용자 관찰과 일치한다. 그러나 viewport/글씨 크기와 강도(.20 vs 약.35), 캡처 시각·압축/축소 과정이 같지 않은 비교다. 같은2048×945 JPEG라는 사실로 두 이미지의 재압축/리샘플 이력이 동일하다고 증명할 수 없다. 폭1645/1715·p90/p99/밝은화소비율 수치는 검증용 산출 인용이며 해당 ROI/밝음 기준/전처리가 제공되지 않아 Codex 독립 재계산 수치로 쓰지 않는다. 배경중앙값32가 같아도 전체 명암/색변환 동일성은 증명되지 않는다. 이 자료는 **§4B 후보를 진행할 문제 근거**이고 특정 원인·B 우위·폰에서.20/.35가 같은 픽셀이라는 증거는 아니다.

이 증거로 후속 범위는 **기존 승인 (a)A.60 + (b)표시크기1패스B, 같은강도.35대조를 포함한 검증**으로 고정한다. 강도를 높이는 안만으로 축소하지 않고, UI 패널 폭/폰트 굵기/색 범위를 임의 변경하는 별건을 추가하지 않는다. 비교 fixture에서는 동일 source 문구·폰트/remote DPI·content placement/표시 배율·색/명암과 입력 프레임을 맞춘다. 이미 재압축된 GNLink 그림을 다시 확대해 수치만 맞춘 결과를 원래 파이프라인 동등성으로 간주하지 않는다. 출력 픽셀 차이가 진짜인지 확인한 뒤 가독성/halo/지연·터치 조건을 함께 판정한다.

- A = 기존 원본 크기 출력, B = 표시 크기 출력. **A.35 vs B.35**를 포함해 그리는 위치 효과를 분리하고, A.60 vs B.60으로 강도 변경과의 상호작용을 비교한다. 약.20도 양쪽 비교 기준으로 둔다. direct off는 기준, B s=0은 시험용 identity/기하·색 대조로 둔다(제품 '끄기'를 GL0으로 바꾸지 않는다).
- 같은 decoder handoff 프레임·crop/색/viewport/확대율에서 실제 최종 latch 후 캡처한다. 먼저 결정적 OES fixture로 입력 동일성을 확보하고 실제 APK UI compare로 실제 생산 경로 도달을 확인한다. 움직이는 장면은 같은 입력 구간·프레임 대응/누락을 기록한다. 서로 다른 시각의 원격 화면 두 장을 same-frame 결과로 쓰지 않는다.
- 사용자가 제공할 OSLink/GNLink 비교 스크린샷은 확대율·출력 크기·화면 내용이 맞는지 확인해 정성 근거로 사용한다. 이미 요청된 스크린샷을 중복 요청하거나 도착 전 원인을 단정하지 않는다. 해당 자료 도착 전에도 GNLink 내부 A/B 준비는 진행 가능하다.

### B의 기하 계약

권장 구조는 **출력 버퍼=실제 TextureView view 픽셀 크기**, shader가 기존 `ViewerViewport.place`의 같은 placement로 비디오 quad를 그리는 방식이다. B에서는 TextureView의 추가 placement transform을 identity로 두어 같은 확대/이동을 두 번 적용하지 않는다. 기존 ViewerViewport가 논리 source→표시 및 입력 역변환의 유일 기준이고, 같은 snapshot/generation을 GL에 전달한다. 단순히 setDefaultBufferSize만 view 크기로 바꾸고 기존 transform을 그대로 두는 것으로 최종 확대+선명화가 구현됐다고 주장하지 않는다.

- **source video/crop 치수, output buffer 치수, view/placement 치수와 세대는 분리**한다. nativeGetVideoSizePacked·Host 입력 좌표 기준은 계속 원격 visible source다. B 출력 크기를 decoder 비디오 크기나 터치 원격 해상도로 보고하지 않는다. off/direct 복귀 시 원래 source buffer+TextureView transform을 복원한다.
- shader 이웃은 그 프레임의 **visible source1픽셀**(quarter-turn 축 교환 포함), frame SurfaceTexture crop/flip/rotation을 정확히1회 적용한다. 출력/view 픽셀 역수로 이웃을 바꾸지 않는다. **현 renderer의 ring miss→output size fallback은 B에 그대로 쓰면 잘못된 간격**이므로, source 근거가 없으면 안전하게 frame을 보류/폴백하고 진단한다. coded1088 패딩 샘플0을 유지한다.
- 레터박스·클리핑·overlay/가상커서·전체보기/크게보기/pan·회전·컨트롤 숨김에 대해 기존 placement와 시각/입력 mapping을 유지한다. B가 이미 placement를 그렸는데 UI가 다시 setTransform하거나 확대해서는 안 된다. margin touch는 계속 host로 보내지 않는다. geometry 변경 시 진행 중 gesture를 안전하게 취소하고 stale binding/layout의 입력이 새 화면으로 가지 않게 한다.
- pan/zoom/strength는 정지 프레임에도 새 placement로 redraw할 수 있어야 한다. source timestamp는 그대로이며 새 decode/첫 출력으로 중복 계수하지 않는다. UI 이벤트마다 무한 draw task를 쌓지 말고 최신 placement를 합류한다. 실제 view 크기 변경 때만 bounded output 재설정하고, 확대4배라고 view4배 크기의 버퍼를 할당하지 않는다. 늦은 이전 resize/callback이 새 surface·active entry를 변경하지 못해야 한다.
- 단일 producer·RELEASING/BLOCKED/늦은 release·첫 qualified output gate·metadata-before-buffer·keepalive binding 소유 경계를 유지한다. 실패 시 기존 원본 direct 자동 복귀·안내를 보존하며 B→direct 시에도 old EGL producer가 남은 SurfaceTexture에 decoder를 붙이지 않는다.

### 비교 승인과 검증 경계

B는 시험 후보로 선택 가능하게 하되 새 실험 intent/env/define을 출하 APK에 남기지 않는다. 제품 채택 시 최종 경로 하나를 고정하거나 실제 사용자가 이해할 이유가 있는 옵션만 별도 검토한다. GL shader 코드만 있는 stub 비교로 끝내지 않는다.

필수는 source크기1:1·실제 확대율·축소/비정수 placement, crop1088→1080·flip·rot90, 전체/크게/pan/뷰 크기 변경에서 최종 캡처와 앱→Host owned창 터치 대조다. 포인트는 중앙/모서리/letterbox/확대 후 화면가장자리이며 기존 ≤1 source px 기준과 무입력 margin을 유지한다. 부정 대조로 double-transform·output-pixel tap·old layout/crop가 실제 판정을 깨는지 확인한다. SH 가는글씨 edge 수치만 올리고 halo/클리핑·미세선 소실·scroll shimmer를 숨기지 않는다.

**1패스라도 출력 픽셀 수가 증가하면 비용이 증가할 수 있다.** 동일 입력의 A/B GPU draw/swap·전체 matched handoff→최종latch p95·frame miss·메모리/스레드·실폰 발열을 측정한다. 이전 S4 미달을 유지하고 '추가 pass 없음=추가 지연 없음'으로 보고하지 않는다. 구조·기하·성능 결과를 하나의 고정 후보로 검증용이 독립 확인한 뒤 Codex에 B 채택 여부를 요청한다. B가 이득이 없거나 비용/기하 기준을 못 맞추면 A.60 후보를 별도로 판단하며 두 실험을 무기한 묶지 않는다.

## 5. 재접속 지연·우선순위·검증 연결

이번 관측의 주 지연은 punch/relay 경로이므로 **NP 후속으로 분리**한다. hello sleep/timeout을 다시 늘리거나 전역 relay grace를 이번 UX/SH 보수에서 바꾸지 않는다. NP는 이전 문서의 hairpin/광고 mapping 가설을 검증할 별도 설계/시험이 필요하며 이 실기 한 번으로 원인이 확정되거나 NP 구현이 승인된 것은 아니다. keepalive가 연결 재사용 때 일부 재접속을 피하더라도 cold 연결 지연을 해결한 것으로 보고하지 않는다.

검증용이 **LK-UX / SH 강도**를 각 유효 정본에 취합하고 담당 여유가 생기는 대로 실제 최초 task ID를 연결한다. 사용자가 재현한 LK 재시도 차단 보수를 우선 배정한다. 기존 keepalive-r2 Host t-y1lnbgnd·APK 준비 t-x27113l6를 중복 지휘하거나 같은 native/브리지 파일을 두 담당이 동시에 수정하지 않는다. SH 후보는 별도 branch에서 준비하고 APK keepalive 재통합과 조합되는 부분만 소유자를 정한다. 버전 번호는 결과가 먼저 준비된 기능 묶음의 릴리스 정본에서 확정한다(현재 keepalive .163/.32는 후보이지 영구 예약이 아님).

LK 필수 자동 행동: 같은 실제 control 연결에서 입력→사용자 제출 실패→오류/판정 또는 미지원 상태→명시 다시 입력→새 challenge·필드 바이트 교체→성공, reconnect0. typed 전 cancel/중간 cancel/새 시도 중 옛 결과/옛 seal 재전송/새 topology/다른 Host Busy 무간섭·해제 후 정상 진행을 함께 검증한다. 비밀번호·sealed 원문 로그0, 의도하지 않은 자동 Enter/재전송0. 실제 OS 실기의 실패 UIA 상태는 fixture 증거와 구분한다. 이번 성공 두 건으로 prelogin·UAC·재부팅·회사 PC까지 성공했다고 표시하지 않는다.

현재 산출물은 Codex 미추적 판정서와 인계뿐으로 별도 제품 commit은 없다. 구현·독립검증·기능 history/계획 checkbox·Git MCP commit은 해당 기능 task에 묶고, 고정 후보의 SOURCE/EVIDENCE를 검토한 후 릴리스로 연결한다.

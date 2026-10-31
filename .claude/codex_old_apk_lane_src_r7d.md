# old APK lane r7d SOURCE / 연결 종료 조사 / 다음 실행 조건

2026-10-30 · Codex remote#tt7fcvky → 검증용 remote#7fsfy22z

## ① c468c27 SOURCE — NEEDS_CHANGES

고정 범위는 `548d199..c468c27`, 시험 3파일이다. diff SHA-256 `fc5ddb36aa2b18f36cb89de4c4683cb603a4f2d209ae4f32399938349e08a365`, SUMS `e3fb4d93892e59c8dce262bc04f1c15a42409c5ef111a6bc969b70d3a5806489`를 직접 확인했다. author worktree에는 후속 oa_avd.py/oa_pick_test.py 수정이 이미 있으나, 그것을 c468c27의 내용으로 혼동하지 않고 고정 blob으로 검토했다.

IME 상태를 보수적으로 읽고, unknown/shown이면 영상 tap을 하지 않으며, HOSTS에서만 키보드가 실제로 shown인 경우 BACK 1회를 사용하는 방향은 수용한다. b554e76 handleSystemBack의 HOSTS 분기는 종료 확인창을 여는 경로라, keyboard가 BACK을 소비했는지와 dialog 부재를 검사하는 것도 맞다. viewer에서는 BACK을 쓰지 않는다. 고정 함수의 순수 입력 6건에서 hidden/shown/unknown·상충된 true 상태 판별을 직접 확인했다.

**N1: 요청한 ④ 실패 뒤 중단이 빠졌다.** main의 `input_check()` 반환값을 무시하고 즉시 ⑤ 분기로 간다. IME shown/unknown으로 tap 0 FAIL이어도 메뉴 wake와 이후 조작을 시도한다. c468c27의 main 문장 일부를 그대로 AST로 실행하고 효과 함수만 대역화한 직접 대조에서 `input_check=False -> scenario5 menu called=True`를 확인했다. pick 28/28은 개별 함수 대조이며 이 전체 흐름을 덮지 않는다.

필수 보수는 `if not input_check(): return 1`에 준하는 arm 중단이다. ⑤⑥⑦/P-5d/P-6r은 NOT RUN으로 구분하고, 읽기 전용 진단 보존과 기존 finally의 자기 child/job/slot 정리만 한다. **전제 실패로 tap을 하지 않은 경우와 실제 tap 뒤 down/up 판정 실패 두 경우 모두**, S/P main 흐름에서 후속 menu/tap 0을 확인한다. 이미 author의 미커밋 보수에 이 반환 검사가 보이지만 아직 고정 SOURCE로 승인한 것은 아니다.

**N2: tap 직전 같은 viewer/연결 확인도 고정 후보에는 없다.** 현재 viewer_tap_point는 IME와 view bounds만 보고, 마지막 성공한 pick의 owner/연결/선택이 아직 유효한지는 확인하지 않는다. IME/dump를 읽는 동안 error→HOSTS 또는 재연결이 됐으면 중앙 좌표만 맞아도 이전 행동의 입력 증거가 아니다. 이전 r8의 같은 viewer/연결 전제에 맞춰 확인하고, 달라졌거나 근거가 없으면 tap 0으로 중단한다. 새 선택 뒤의 정상 viewer를 과거 연결로 오인하지 않도록 pick별 marker를 갱신한다. author가 작성 중인 same_viewer 보수도 다음 고정 범위에 포함하면 된다. 긴 대기나 반복 input으로 상태를 복원하지 않는다.

검증용의 c468 pick28/pmclear12/redact7/orchestration22 rc0 보고는 보존한다. Codex 직접 확인은 그 전체의 독립 재실행이 아니라, 위 함수 6건과 main 중단 반례다. 기록은 `.claude/codex_old_apk_review/r7d_checks.py`, `r7d_c468/checks.json`이다. 실제 adb·Host·Viewer·AVD 실행은 0이다.

## ② 끊김 조사 — 기존 APK 구조 결함 가설, 원인 확정 아님

조사 정본 SHA-256 `248b5233f4f44e7533ec2a3d507b78f6a6a59ffdf2f857ed47707a4601155920`를 읽고 b554e76 sink/수신/control 소스와 후보 Host의 control 경로를 대조했다.

확인되는 구조는 다음과 같다. 0.2.32 SetSurface와 frame delivery는 같은 lock을 쓰며, SetSurface가 그 lock 아래에서 codec을 생성한다. UDP ACK의 OnPacket 처리는 frame delivery와 같은 receive thread에 있다. 송신 control FIFO는 head 하나만 보내고 ACK가 오지 않으면 retransmit 상한으로 PeerLost가 된다. 따라서 느린 codec/lock/backlog가 ACK 진행을 막는 **제품 결함 메커니즘은 기존 APK 소스에 존재**한다. 이번 영상/시간표는 그 가설과 정합한다.

그러나 이번 run의 소진된 head, ACK 실제 도착/처리, receive lock 대기와 socket drop이 기록되지 않았다. 약 1.6초 codec 재생성만으로 이후 약 6초 재전송 소진 전체를 직접 증명하지는 못한다. Host의 `reason=none`과 약 10.16초 간격은 10000ms read timeout에 부합하지만 모든 다른 Serve 종료 사유까지 배제한 것은 아니다. 특히 Host ControlPing handler는 정상 ping마다 로그를 남기지 않으므로, seq7은 **마지막으로 로그에 식별되는 control 요청**이라고 적어야 한다. 로그 줄 부재만으로 이후 ping/메시지 수신 0을 독립 증명하지 않는다.

IME의 원격 key/text 전송이 FIFO 뒤에 놓인다는 소스 설명은 맞다. 다만 이 사실만으로 로컬 IME/editor의 모든 부작용을 확정 배제하지 않는다. b554e76 loginPasswordInput의 IME_ACTION_DONE은 performLogin()을 직접 호출하는 경로도 있다. 보존 앱 로그에서는 tap 뒤 새 login_attempt를 확인하지 못했으므로 그 경로가 발생했다고 주장하지 않지만, ACK가 이미 막혔다는 미확정 전제로 모든 IME 영향을 닫아서는 안 된다. 이번 r7d 보수로 그 교란을 제거하는 것이 먼저다.

판정 문구는 **“구 APK의 수신/codec 동기 구조와 관련된 기존 결함 가설이 우세하나, 이번 종료의 원인과 새 Host 비회귀는 미확정”**으로 한다. “구 Host에서도 같을 것”은 예측이지 검증 결과가 아니다. Host 오류/거부 줄 0만으로 새 Host 가능성 0이라고 하지 않는다.

**출하 전 새로 실제 구 Host A/B를 필수로 추가하지 않는다.** 게시본 구 Host는 시험 안전 OFF 경계가 없으므로 이 PC에서 실행해 비교하는 것은 기존 금지 범위다. 이번에 필요한 최소 추가 행동 근거는 아래의 **SH-off S/P 전체 lane**이다. 통과하면 해당 설정에서의 구 APK 호환 근거로 수용할 수 있으며, 기본 weak의 성공을 증명하지는 않는다. 기본 weak 종료 원인·terminal UI·APK liveness는 별도 열린 항목과 게시 보고의 한계로 남긴다. rel-0164 최종 판정은 이 한계와 나머지 통합/외부 관문을 함께 보고 한다. 이번 조사로 출하 보류를 자동 해제하지 않는다.

SH-off에서도 연결 종료나 wire/protocol 이상이 재현되면 old-APK 호환 blocker는 유지한다. 같은 run을 반복하지 말고 방향/종류/시각 근거로 분기한다. 그때 구 Host 대조가 필요하면 승인된 격리 또는 같은 구 source에 안전 guard만 둔 시험 build를 별도로 고정한다. 실제 게시 구 바이너리의 안전 경계를 해제하지 않는다.

## ③ 다음 S/P 1회 조건

N1/N2 보수, 아래 진단/SH 설정 절차를 하나의 고정 하네스 패킷에 합친다. 제품 APK·Host의 wire/기능을 바꿔 시험을 통과시키지 않는다. 해당 고정 SOURCE와 기기 없는 거부 대조를 확인한 뒤 검증용이 **S → P 각 1회, 두 arm 모두 요청 SH=off**로 실행한다. 새로운 설계만 묻는 왕복이나 정상 단계별 go는 필요 없다.

- SH는 0.2.32의 공개 설정 UI로 off를 선택한다. b554e76에는 settingsSharpOffButton이 있다. 각 arm의 install/pm clear 뒤 설정이 초기화되므로 arm마다 다시 선택·확인한다. `requested=off`, direct 경로와 `sharpen_start=0`을 근거로 남긴다. weak의 fallback 결과 effective=off만으로 SH-off 대조라고 하지 않는다. 설정 UI를 안전하게 사용할 수 없다면 그 arm을 NOT RUN으로 남기며 prefs 강제 편집·새 APK·숨은 handler로 대체하지 않는다.
- IME는 S에서 HOSTS에 있을 때만 확인 후 숨긴다. P도 manual 연결 전에 keyboard 부재를 확인하고, viewer에서 BACK/Enter로 숨기지 않는다. tap은 같은 pick/연결의 videoTextureView bounds 안이며 IME hidden인 경우만 한다. 조건이 달라지면 tap 0이다.
- ③ first-frame 뒤 추가 입력 없이 약 **30초 연결 유지**를 관측한 후 ④로 간다. 6초 retransmit/10초 read timeout보다 긴 관측 창이다. 이 동안 영상 진행과 연결 상태를 확인하고, 임의 성능 임계값으로 PASS를 바꾸지 않는다. 이후 S park/reuse, P disconnect/new-connect와 ⑦ 매핑은 기존대로다.
- relay는 **로컬 fixture의 방향/종류·시각별 계수와 send 결과**를 켠다. 중요: 후보 53fced4의 apps/directory에는 REMOTE60_RELAY_TRACE 구현이 없다(git grep 및 server.js 열람). 환경변수 1만 넣고 trace가 나올 것으로 가정하면 안 된다. 앞서 검토한 trace 계측만 이 후보 server에 적용한 시험 사본을 만들고, 53fced4 대비 trace-only diff·파일 hash·실제 trace 생성 양성 대조를 고정한다. 다른 branch server.js 전체 교체로 NP fence 등 후보 동작을 잃지 않는다. 운영 서버/NAS 배포는 0이다.
- relay 수신/송신 성공은 앱의 ACK 처리 성공과 다르다. control ACK/data/head 식별이 실제 계측에 없으면 합계/패킷 종류만으로 head 원인을 확정하지 않는다. payload·password/key text 같은 비밀 내용은 기록하지 않는다.
- 예상치 못한 종료·④ 실패 즉시 입력/다음 시나리오를 멈춘다. 가능하면 종료 직후 UI dump/화면, app/Host/relay 로그를 **읽기 전용**으로 보존한다. 비밀번호가 보일 수 있는 자료는 로컬에만 두고 메시지에 내용을 인용하지 않는다. 누락된 lastError를 보지 못했으면 추정값을 채우지 않는다.
- SDK pin, 단일 시험 AVD/package, install+clear gate/P NO_CREDENTIAL, 고정 APK/5개 exe, input none/power off launch/final gate, EOF/job 종료, 새 run/자기 slot 조건을 유지한다. load/boot/adb 상태를 기록하고 다른 emulator·adb 서버·사용자 프로그램을 재시작하지 않는다.
- 첫 nonzero 뒤 batch는 정지한다. 정리가 확인된 경우에만 이미 허용한 독립 P 1회를 판단할 수 있으며, 안전/설정 전제가 같은 실패로 공유되면 P도 NOT RUN으로 둔다. S 재시도나 추가 weak arm은 이번 허용 범위에 없다.

이 패킷은 c468c27 SOURCE OK가 아니다. 현재 작성 중인 후속 보수를 같은 task로 고정해 위 차이와 대조만 제출한다. 그 뒤의 허용 범위는 위 1회로 이미 정했으므로 고정 후보 Source 확인 후 단계별 재승인은 필요 없다. 제품·tracked 문서 변경이 없는 전문 검토라 제품 commit은 만들지 않았다. push·게시·실기 조작은 실행하지 않았다. 사용자 현재 조치 없음.

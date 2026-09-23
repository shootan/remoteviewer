# 2026-09-23 재검수 — feat/connect-bandwidth (C1·C2·C3·릴리스 스크립트·테스트)

- 대상: `feat/connect-bandwidth` `0fdefe8..ef533f2`(커밋 32개, 2026-09-22 00:34 ~ 2026-09-23 09:35).
  워킹트리의 C3 r5 미커밋 편집(`control_resume_e2e_test.cpp`·`control_resume_e2e_support.hpp`)은 RV-16 에서만 다룬다.
- 방법: 1차 Fable 5.1(직접 읽기 + 영역별 에이전트 3개), 2차 Opus 5.5(1차 결과를 모르는 새 에이전트 3개 +
  1차 지적 전수 재확인 + 직접 추가 검토). **여기에는 코드나 이 PC 에서 재확인된 것만 적는다.** 에이전트 보고를
  코드와 대조하지 못한 세부는 "(보고)" 로 표시한다.
- 실행한 것: 순수 논리 테스트 4종을 작업용 빌드(`build-0.2.134`) 바이너리로 재실행했다. punch_reply 55,
  control_resume 124, viewer_control_resume 60, udp_control_channel 12, 전부 exit 0. **독립 빌드가 아니고 e2e 는
  재실행하지 않았다.** 이 PC 측정 2건: `OpenProcess` 의 pid 하위 2비트 무시(RV-15), 호스트 캐시 파일 내용(RV-00).
  세션은 콘솔(RDP 아님).
- 작업 순서는 `작업목록.md` 의 V0 행과 §2 "재검수 보수" 표가 정본이다. 이 문서는 근거와 수정 방향만 둔다.

## 요약

| ID | 영역 | 심각도 | 한 줄 | 회귀 |
|---|---|---|---|---|
| RV-00 | 테스트 → 사용자 PC | **긴급** | 호스트 e2e 가 이 PC 의 실제 호스트 캐시를 테스트 값으로 덮어썼다 | — |
| RV-01 | C3 입력 | 중대 | 끊김 중에 뗀 키·마우스 버튼이 재개 뒤 호스트에 눌린 채 남는다 | **예**(modifier) |
| RV-02 | C3 범위 | 중간 | 실제로 복구되는 끊김은 약 15초 이하이고 하향 끊김은 거의 복구되지 않는다 | — |
| RV-03 | C3 설계 | 낮음 | 채널은 살아 있고 요청만 시간 초과면 30초 동안 거부만 받다 끝난다 | 아니오 |
| RV-04 | C3 호스트·채널 | 낮음 | 낯선 Hello 가 재개 협상을 끈다 · 재키잉 직후 Close 경합 · 종료 중 채널 재개방 · 서비스 없는 동안의 위조 재개 | 일부 |
| RV-05 | C2 셸 UI | 중간 | 교체된 구 뷰어의 종료가 셸을 idle 로 되돌려, 재클릭이 진행 중인 연결을 계속 취소할 수 있다 | 예(시점) |
| RV-06 | C2 셸 | 낮음 | 교체 경로의 순서·상태 문제 4건 | 예 |
| RV-07 | C2 취소 | 낮음 | 취소가 닿지 않는 뷰어 상태 2개, HelloAck 와 취소의 동시 도착 | 일부 |
| RV-08 | C2·C3 문서 정확성 | 낮음 | 제품에서 발동하지 않는 guard 들, 취소 종료 코드 기재 오류 | — |
| RV-09 | C1 | 낮음~중간 | 헤어핀 NAT 뒤 같은 LAN 호스트는 서버 wake 를 디렉터리 이벤트로 알아보지 못한다 | — |
| RV-10 | 디렉터리 서버 | 낮음 | 재전송이 다음 connect 의 burst 를 막는다 · 옛 host 객체 · 교체 무기록 | 예 |
| RV-11 | 릴리스 스크립트 | 중간 | 대체 게이트가 deploy·빌드 환경변수를 보지 않는다 | — |
| RV-12 | 릴리스 스크립트 | 중간 | 요약이 결과가 아니라 플래그·고정 문구로 성공을 적는다 | — |
| RV-13 | 릴리스 스크립트 | 중간 | 게이트 도구를 대상 워크트리가 아니라 스크립트가 있는 체크아웃에서 가져온다 | — |
| RV-14 | 릴리스 스크립트 | 낮음 | 경로 정규화 · git 실패 · TOCTOU · 무한 루프 등 묶음 | — |
| RV-15 | 테스트 안전 | **중대** | 실패한 실행의 정리가 pid 로 프로세스를 죽인다. 가짜 pid 가 실제 pid 로 열린다(측정) | — |
| RV-16 | 테스트 유효성 | 중간 | 자기 복사본을 검사하거나 실패할 수 없는 단언, 미커밋 편집의 판정 누락 | — |
| RV-17 | 테스트 → 기계 | 낮음 | 실제 외부 주소로 송신, 캐시를 cwd 에 씀, 고아 프로세스·잔여 디렉터리 | — |
| RV-18 | 테스트 판정 | 낮음 | 아무것도 테스트를 자동 실행하지 않고, SKIPPED 가 exit 0 이다 | — |

## RV-00 — 호스트 e2e 가 이 PC 의 실제 호스트 캐시를 덮어썼다 (긴급)

**측정(이 PC, 2026-09-23).** `%LOCALAPPDATA%\remote60\host.json` 의 값이 `directoryUrl=http://127.0.0.1:55530`,
`accountId=tester` 다. 수정 시각은 2026-09-23 01:18:12 이다. 토큰 값은 출력하지 않았다.

**경로.** `host_punch_reply_e2e_test.cpp:211-217` 은 GNLinkStream 을 `--directory-url <가짜> --directory-id tester
--directory-pw ...` 로 띄우고 캐시 경로를 주지 않는다. 제품은 빈 경로를 `default_host_cache_path()` 로 채운다
(`directory_client.cpp:433`). 캐시의 origin 이 달라 `LoadCache` 가 거부하고, 가짜 디렉터리에 등록한 뒤
`SaveCache` 가 실제 파일을 `MOVEFILE_REPLACE_EXISTING` 으로 바꾼다. `SHGetKnownFolderPath` 를 쓰므로 환경변수로
돌릴 수도 없다.

**영향.** 지금 실행 중인 GNLinkHost·GNLinkStream 은 2026-09-22 13:14 에 시작해 이전 토큰을 메모리에 들고 있어
정상이다. **호스트 앱이 다시 시작되면**(재부팅, 인앱 업데이트, 충돌) `host_app_main.cpp:2114` 가 이 파일을 읽어
테스트 주소와 `tester` 계정으로 스트리밍을 시작한다. 그러면 이 PC 가 실제 PC 목록에서 사라지고, 로그 업로드도
테스트 주소로 가서 NAS 로그가 끊긴다.

**사용자 조치(1회).** 실행 중인 GNLinkHost 창에서 **Sign out** 한 뒤 실제 서버·계정으로 다시 로그인한다.
`sign_out`(`host_app_main.cpp:1451-1464`)이 메모리의 실제 서버·계정으로 파일을 다시 쓰고, 로그인이 새 토큰을
저장한다. 로그아웃 동안에는 원격 접속이 끊긴다.

**함께 나온 것.** 테스트가 띄운 호스트도 실제 `%LOCALAPPDATA%\remote60\diagnostics` 에 로그를 미러링한다
(`stream-slot-1.*`·`stream-slot-2.*`, 09-23). 업로더는 이 폴더를 읽지 않으므로 NAS 로그는 오염되지 않았다.
로컬 진단 폴더를 볼 때 슬롯 1·2 는 테스트 호스트일 수 있다.

**수정 방향.** 테스트 호스트에 격리된 캐시 경로를 줄 수단이 필요하다. 호스트 인자나 환경변수 추가는 제품 표면이라
Codex 확정이 필요하다. 출하물에서의 의미(사용자에게 노출해도 되는지)도 함께 정한다. **고치기 전에는
`remote60_host_punch_reply_e2e_test` 를 `REMOTE60_ALLOW_HOST_E2E=1` 로 실행하지 않는다.** 다시 덮어쓴다.

## RV-01 — 끊김 중에 뗀 키·마우스 버튼이 재개 뒤 호스트에 눌린 채 남는다 (C3, 중대, 회귀)

2차 검수에서 직접 찾았고, 1차 결과를 모르는 C3 검토 에이전트도 같은 결론을 냈다. 1차 검수는 놓쳤다.

**경로.**
1. 사용자가 키 X 를 누른다. `forward_key_down` 이 `forwardedKeyDown[X]=true` 로 두고 down 이 호스트에 도달한다.
2. 제어가 끊기고 worker 가 Resuming 에 들어간다. `inputEnabled` 는 켜진 채라 입력은 계속 큐에 쌓인다.
3. 사용자가 X 를 뗀다. `forward_key_up`(`viewer_input_forward.cpp:96-98`)이 **큐에 넣는 시점에** 플래그를 false 로 바꾼다.
4. 그 up 은 사라진다. 전송 중이던 동작이면 실패하고 재전송하지 않는다(`viewer_control_client.cpp:607`).
   큐에 있던 것이면 재개 때 비운다(`viewer_control_client.cpp:367`).
5. 재개 뒤 `kMsgControlResumed`(`viewer_window_proc.cpp:354`)의 `enqueue_release_for_pressed_keys` 는 플래그를 읽는다.
   X 는 이미 false 라 up 을 보내지 않는다.
6. 호스트의 VK 경로는 주입한 down 을 추적하지 않는다. 세션 끝에 풀어 주는 것은 host-IME 물리 스캔 경로뿐이다
   (`host_control_session.cpp:295-300`, `:1120`).

**결과.** Shift 면 이후 입력이 전부 대문자·기호로 들어간다. Ctrl 이면 클릭이 Ctrl+클릭이 된다. 게임이면 이동 키가
계속 눌린다.
- 끊김 중 창 전환(`WM_KILLFOCUS`)도 모든 up 을 큐에 넣고 플래그를 지우는데, 재개 때 전부 버려진다.
- 마우스도 같다. `enqueue_release_for_pressed_mouse_buttons`(`viewer_input_forward.cpp:124`)가 있는데 재개 처리는
  부르지 않는다. 로그는 "released held keys and buttons" 라고 적는다.
- (보고) 재개 직후에 눌러 아직 누르고 있는 키가, 뒤늦게 도착한 해제 처리에 풀릴 수 있다.

**회귀인 이유.** C3 전에는 끊김이 새 세션으로 이어졌고, 새 세션은 시작할 때 Ctrl·Alt·Shift·Win 의 up 을 무조건
보낸다(`viewer_startup.cpp:811-823`). 재개 경로는 그 해제를 대신하면서 더 약하다. modifier 가 아닌 키는 C3 전에도
남았다.

**왜 테스트가 못 잡았나.** `viewer_release_all_e2e_test` 는 key-up 이 **아예 생기지 않는** 경우, 즉 재개 시점에
아직 누르고 있는 키만 본다.

**수정 방향(Codex 확정 — 입력 경로).**
- 최소: 재개 때 세션 시작과 같은 modifier 무조건 해제 + `enqueue_release_for_pressed_mouse_buttons` 호출.
  큐 비우기는 down·이동만 버리고 up 은 남기는 쪽을 검토한다.
- 완전: 호스트가 주입한 VK down·마우스 down 을 추적하고, `ResumeWith` 와 세션 끝에서 전부 푼다. 물리 경로가 이미
  하는 일을 넓히는 것이다.

**검증.** e2e 사례 추가: down 전달 → 절단 → 절단 중 WM_KEYUP → 재개 → 호스트 쪽 창에 WM_KEYUP 도착.
Shift 와 마우스 왼쪽 버튼으로 각각. 기존 "아직 누름" 사례와 중복 up 0 도 유지.

## RV-02 — 실제 복구 범위가 문서의 30초보다 좁다 (C3, 중간)

2차 직접 검토와 C3 에이전트가 같은 결론을 냈다. 코드 읽기로 낸 결론이고 **측정은 아직 없다.**
- 호스트 디스패처의 `Serve()` 는 10초 읽기 시간 초과로 끝나고(`host_startup_control.cpp:798`), 재개 대기 중이
  아니면 나가면서 영상을 끈다(`host_control_session.cpp:1130-1133`).
- 뷰어는 영상이 살아 있을 때만 재개를 요청하고(`control_resume.hpp:68`), 영상이 멈추고 5초가 지나면 멈춘다.
- **상향 끊김**은 대략 10초+5초 안에 풀려야 복구된다.
- **응답만 유실**되면 호스트가 약 6초에 peer-lost 로 영상을 끄고, 뷰어는 12초 읽기 시간 초과에야 알아채서 이미 영상
  규칙에 걸려 있다.
- **하향 끊김**은 영상도 같이 끊기므로 뷰어가 끊김을 선언하는 약 6초 시점에 이미 요청하지 않는다.
- e2e 는 제어 datagram 만 떨어뜨리고 영상은 통과시키므로 실제 끊김과 모양이 다르다.

**할 일.** e2e 에 영상까지 끊는 절단을 넣는다. 상향 8·12·20초, 하향 3·8초. 결과로 `구현계획.md` C3 의 범위 문장을
고친다. 범위를 넓힐지(예: 협상된 세션은 `Serve()` 시간 초과 뒤에도 재개 유예 동안 영상 유지)는 측정 뒤 Codex 와 정한다.

## RV-03 — 요청 시간 초과로 Resuming 에 들어가면 30초 동안 거부만 받는다 (C3, 낮음)

- 뷰어는 어떤 동작 실패든 재개로 들어간다(`viewer_control_client.cpp:607`·`:703`·`:714`). 12초 읽기 시간 초과도 포함이다.
- 호스트 디스패처가 긴 처리에 묶여 있으면(2026-09-15 유형) 채널은 멀쩡하다. probe 가 alive 로 끝나 served=0 만
  돌아온다. 뷰어는 거부를 받아도 Running 으로 돌아가는 길이 없고 30초 뒤 끝난다.
- C3 전에도 같은 30초 뒤 종료였다. 표시만 "제어 연결을 복구하는 중…" 으로 바뀐다.
- 개선 방향: "채널이 살아 있어 거부" 는 기존 링크로 돌아가도 된다는 증거다. 이 답을 구분해 Running 으로 복귀한다.

## RV-04 — C3 호스트·채널의 경합과 경계 (낮음)

- **낯선 Hello 가 협상을 끈다.** `host_startup_control.cpp:340` 이 인증 거부(`:356`, `:400`)보다 **먼저**
  `controlResumeNegotiated` 를 쓴다. 재개 비트 없는 Hello 하나(구버전 뷰어, 거부될 요청)가 활성 세션의 협상값을
  0 으로 바꾸고, 이후 진짜 뷰어의 재개 요청은 답 없이 버려진다. 줄 자체는 `035cce7` 부터 있었고 협상이 실제로
  성립하기 시작한 `bebc04b` 부터 의미가 생겼다.
- (보고) **재키잉 직후 Close.** 리더가 `controlServing=1` 을 읽은 뒤 Serve 가 스스로 끝나면, 디스패처가 먼저
  `ResumeWith` 하고 다시 Serve 에 들어간 다음에 리더의 `Close(PeerLost)` 가 도착할 수 있다(`:619-639`). 뷰어의 증명
  왕복이 약 6초 뒤 실패하고 새 id 로 회복한다. 창이 좁다.
- (보고) **종료 중 재개방.** `ResumeWith`(`udp_control_channel.cpp:88-89`)는 닫힌 이유와 무관하게 채널을 연다.
  뷰어 종료 중 Shutdown 으로 닫힌 채널이 다시 열리면 증명 왕복이 3초 join 을 넘겨 종료 코드 44 로 끝날 수 있다.
- (보고) **서비스 없는 동안의 위조 재개.** 클라이언트가 떠난 뒤에도 `sessionActive` 는 참으로 남는다. 바인드된
  IP:포트를 위조한 새 resumeId 는 Serve 판정을 받고, Serve 재진입이 영상을 다시 켠다(`host_control_session.cpp:86`).
  인증 안 된 LAN Hello 가 이미 더 강한 경로라 새 위협은 아니지만, 디렉터리 인증 세션에서는 이전에 capability 가 필요했다.
- (보고) 리더 스레드가 매 datagram 마다 `Tick()` 을 돌고(`:314`), 서비스 중에도 1.5초 대기를 한다. 성능 문제다.

## RV-05 — 교체된 구 뷰어의 종료가 셸을 idle 로 되돌린다 (C2, 중간)

- 셸은 PC 를 누르면 세션 동안 목록을 잠근다(`shell.html` `setBusy`). 같은 PC 를 다시 누르려면 먼저 "새로 고침" 을
  눌러야 한다. 다시 누르면 구 뷰어가 취소되어 약 0.1~0.3초 뒤 종료한다.
- 그 종료는 stale 분기(`client_shell_main.cpp:930`)로 가서 idle 과 "1개 연결이 계속 실행 중입니다" 를 게시한다.
  셸은 잠금을 풀고 카드를 "연결 가능" 으로 되돌린다. 새 뷰어는 아직 최대 약 44초 connect 중이다.
- 그 상태에서 사용자가 다시 누르면 진행 중인 연결이 또 취소된다. **연결이 느려서 다시 누르는 사용자는 연결을 계속
  스스로 취소하게 된다.**
- 심각도 이력: 1차는 "릴리스 전 수정", 2차 첫 판단은 "낮음", 1차를 모르는 C2 에이전트는 "major" 였다. 재클릭
  반복 경로를 근거로 **중간**으로 정한다.
- 수정 방향: 교체로 취소된 뷰어의 종료이거나 더 새로운 작업의 뷰어가 살아 있으면 idle 을 게시하지 않는다.

## RV-06 — 교체 경로의 순서·상태 (C2, 낮음)

- **거부해도 작업 번호가 오른다.** `begin_session` 은 `++gViewerOperation`(`client_shell_main.cpp:958`)과 재연결
  타이머 해제를 먼저 하고 교체 거부로 돌아간다(`:1001`). 남은 구 뷰어가 43/44/46 으로 끝나도 자동 재연결하지 않고,
  실패해도 error 대신 idle 이 뜬다. 다른 PC 의 대기 중 자동 재연결도 사라진다.
- **거부 문구가 불가능한 일을 요구한다.** "기존 창을 닫아 주세요" 인데, connect 중인 뷰어 창은 메시지 펌프가 없어
  hello 가 끝날 때까지(최대 30초) 닫기가 동작하지 않는다.
- **새 뷰어를 띄울 수 있는지 알기 전에 구 뷰어를 취소한다.** 취소·삭제(`:968-1006`)가 토큰·exe 확인(`:1019-1028`)과
  `CreateProcessW` 실패(`:1178-1189`)보다 앞이다. 실패하면 두 뷰어가 모두 없다.
- (보고) **pid 로 슬롯을 지운다.** `handle_viewer_exit` 가 pid 로 비교한다(`:917-922`). 종료 게시가 늦게 처리되는
  사이 새 뷰어가 같은 pid 를 받으면 새 뷰어의 슬롯을 지운다. 작업 번호로 비교하면 된다.
- 도달 조건: 앞의 둘은 파이프 없는 폴백·신호 실패로 거부될 때, 셋째는 로그아웃·exe 누락·실행 실패일 때다. 드물다.

## RV-07 — 취소가 닿지 않는 뷰어 상태 (C2, 낮음)

- 실패 화면(`show_startup_failure`, `viewer_startup.cpp:450-538`)은 `connectCancelled` 를 보지 않는다. 교체된 뷰어의
  "연결하지 못했습니다 / 다시 시도" 창이 남는다.
- 연결이 성립한 뷰어는 감시 스레드가 멈춰 있다. 셸은 `asked=1` 을 기록하고 슬롯을 지운 뒤 같은 PC 에 두 번째 뷰어를
  띄운다. 동작은 C2 전과 같지만 `0ea294b` 가 경계한 "성공이라고 적고 아무것도 안 하는 취소" 모양이 된다.
- (보고) HelloAck 와 취소가 겹치면 뷰어는 7 을 반환하고 소켓을 닫는다(`viewer_startup.cpp:674-684`). 호스트는 이미
  받아들인 세션을 닫힌 소켓 쪽으로 들고 있다가 시간 초과로 정리한다.

## RV-08 — 제품에서 발동하지 않는 guard 와 기재 오류 (C2·C3 문서 정확성, 낮음)

- `connectGeneration` 을 올리는 곳은 `native_video_client_main.cpp:78`(시도 사이, 같은 스레드) 하나뿐이다. hello 결과
  fence(`viewer_startup.cpp:668`)와 C3 답의 세대 검사(`viewer_control_resume.hpp:129`)는 제품에서 발동하지 않는다.
  C3 e2e 사례 4 는 세대를 손으로 올리고 "셸이 하는 일" 이라고 적었지만 셸은 뷰어의 세대를 바꾸지 않는다. 실제
  교체는 호스트의 peer 검사가 막는다. `구현계획.md` 의 "세대 교체(superseded) → 답 무시" 는 **하네스에서만 도달** 이다.
- 뷰어의 peer 검사는 자기 필드끼리 비교한다(`viewer_control_resume.hpp:405-406`). 실제 보호는 `connect()` 된 소켓이다.
- 제품 호출자 없음: `ViewerControlResume::EndSession()`, `ViewerPendingControlRequest`·`viewer_resume_may_resend`·
  `viewer_resume_reply_is_late`, `host_should_accept_resume`·`ProbeDecidedUs`. `host_should_accept_resume` 의 주석은
  리더가 더는 따르지 않는 규칙을 적고 있다.
- `구현계획.md` C2 의 "취소는 고유 종료 코드 7" 은 내부 반환값이다. **프로세스 종료 코드는 0** 이다
  (`native_video_client_main.cpp:88`). 취소된 뷰어는 셸에서 항상 stale 이라 동작은 맞다.

## RV-09 — 헤어핀 NAT 뒤 같은 LAN 호스트의 wake 인식 (C1, 낮음~중간)

- 서버는 호스트의 wire IP 가 자기 LAN 에 있을 때만 `via=lan` 으로 호스트 LAN 주소에 wake 를 보낸다(`wake_target.js`).
  그 경우는 호스트가 **공인 주소로** 디렉터리에 붙은 헤어핀 NAT 다. 그래서 호스트의 `observeAddr_` 는 공인 IP 인데
  wake 는 서버 LAN 주소에서 온다. `fromDirectory` 는 항상 거짓이다(`directory_client.cpp:542-545`).
- 영향: wake 가 응답 창을 열지 못하고(창은 capability 수집 때만 열린다), wake 의 refresh 요청이 클라 펀치용 2초
  cooldown 에 걸린다. 빠른 재접속·교체에서 직전 뷰어의 펀치가 cooldown 을 쓰고 있으면 capability 수집이 최대 2초
  늦어져 릴레이(2.5초)가 다시 이길 수 있다. 이것이 C1 이 대상으로 삼은 바로 그 배치다.
- 창이 열린 뒤에는 호스트가 서버 LAN 주소로 펀치에 답한다. 예산 안이고 서버는 무시한다.
- 수정 방향(Codex 확정 — 신뢰 경계): 디렉터리 wake 를 주소 말고 다른 표지로 알아보게 하거나, 호스트가 서버의 LAN
  주소를 디렉터리로 알게 한다. **실기 로그에 "(directory wake)" 줄이 없는 것을 따로 결함으로 읽지 않는다.**

## RV-10 — 디렉터리 서버 wake 재전송 (낮음, 서버 patch 에 포함)

- **다음 connect 의 burst 를 막는다.** 재전송 tick 이 매초 `wakeLastSentByHost` 를 찍으므로(`server.js:1158`), 재전송
  중에 온 새 connect 는 `sendWakePunch` 에서 거의 항상 `reason=rate` 로 burst 가 막힌다(`:994-1000`). 새 재전송의 첫
  tick 은 1초 뒤다. 교체 connect 의 첫 wake 가 약 1초 늦고 3발이 아니라 1발이 된다.
- **옛 host 객체.** tick 이 클로저의 `host` 로 `lastSeen` 과 주소를 읽는데(`:1140`, `:1157`), 재등록은
  `store.hosts[hostId]` 를 새 객체로 바꾼다(`:849`). 지금은 재등록 직후 heartbeat 가 재전송을 멈춰서 가려진다.
- **교체 무기록.** 두 번째 connect 가 첫 번째 재전송을 바꿀 때 로그·카운터가 없다.
- 서버 patch 가 아직 배포 전이므로 **배포 전에 고치는 것이 싸다.** 결정은 게시 담당.

## RV-11 — 릴리스 스크립트: 대체 게이트의 구멍 (중간)

- `gnlink_release.sh:355-376` 은 도구 5개만 대체 여부를 본다. `gnlink_deploy.sh:43-57` 은 `GNLINK_REMOTE_MODE`·
  `GNLINK_LOCAL_ROOT`·`GNLINK_VERIFY_PUBLIC`·`GNLINK_DEPLOY_HOST`·`GNLINK_DEPLOY_KEY`·`GNLINK_REMOTE_ROOT` 를 따로 읽는다.
  `gnlink_deploy_test.sh` 가 쓰는 바로 그 시험 스위치다.
- `GNLINK_REMOTE_MODE=local` 로 `--sign --deploy` 를 돌리면 실제 키로 서명하고 로컬 디렉터리에 "게시" 한 뒤 대체 0,
  `deployed : yes`, exit 0 이다. `GNLINK_VERIFY_PUBLIC=0` 만 주면 외부 확인 없는 실게시가 같은 요약을 낸다.
- 빌드를 바꾸는 환경변수(`_CL_`·`CL`·`LINK`·`CMAKE_TOOLCHAIN_FILE`·`CMAKE_GENERATOR`·`GNLINK_WEBVIEW2_SOURCE`)도
  대체로 세지 않는다.
- 대체된 채 `--dry-run` 없이 돌린 빌드는 exit 0 으로 실제 릴리스 경로(`.claude/rel/<버전>`)에 산출물을 남긴다. 나중에
  손으로 서명·배포하면 stub 산출물이 게시된다.
- 수정 방향: 실제 `--sign`/`--deploy` 에서는 이 변수들이 하나라도 있으면 거부한다. 대체된 실행의 산출물은 실제 경로에
  두지 않거나 표지를 남긴다.

## RV-12 — 릴리스 스크립트: 요약이 성공을 지어낸다 (중간)

- `deployed : yes`(`:802`)는 deploy 결과가 아니라 플래그에서 나온다.
- 외부 확인 요약(`:825-829`)은 `https|sha256|published|manifest|sig` 로 거른다. 제목 "verify from outside, over https"
  는 남고 `SKIPPED -- not attempted, so not verified` 와 `WARN curl unavailable` 줄은 버려진다(`deploy.sh:460-467`).
  확인을 건너뛴 실행이 확인한 것처럼 읽힌다.
- "installer payload 9/9 PASS"(`:811`)는 고정 문구다. 검사가 stub 이어도 찍힌다.
- "scripts" 해시(`:816-818`)는 "호출한 도구" 라고 적지만 `gnlink_deploy.sh`·`gnlink_check_payload_set.py`·
  `gnlink_verify_manifest.js` 를 빼고, 없는 파일은 조용히 빠진다.

## RV-13 — 릴리스 스크립트: 게이트 도구의 출처 (중간)

- manifest 작성기와 그 `ARTIFACTS` 목록(`:611`), parity(`:631`), payload-set 게이트(`:636`, 자기 체크아웃의
  `update_process_targets.cpp` 를 읽는다), 설치본 검사, 검증기, deploy 스크립트가 모두 `SCRIPT_DIR` 에서 온다.
  `SCRIPT_DIR` 이 `--worktree` 안에 있어야 한다는 검사가 없다.
- main 의 스크립트로 기능 워크트리를 릴리스하면, 기능이 payload 파일을 추가·개명했을 때 main 의 작성기와 main 의
  게이트가 서로 맞아 통과하고 후보의 업데이터는 manifest 에 없는 파일을 기다린다. **0.2.109 실패의 재현 경로다.**
- 수정 방향: `SCRIPT_DIR` 이 `WORKTREE` 안이어야 실행한다. 또는 도구를 워크트리 쪽에서 가져온다.

## RV-14 — 릴리스 스크립트: 작은 것 묶음 (낮음)

- 경로 정규화 불일치: 한쪽은 `pwd -P`(`/D/...` 유지), 다른 쪽은 `cygpath -u`(`/d/...`, `%TEMP%` 는 `/tmp`). 이 PC 에서
  측정했다. 정당한 경로가 "워크트리 밖" 으로 거부되고, 문서가 허용한 `D:/...` 형식은 동작하지 않는다.
- git 실패 미검사(`:410` `:416` `:659` `:661`): 빈 출력이 "깨끗함"·"HEAD 불변" 으로 읽힌다.
- 값 없는 마지막 옵션(`--worktree` 로 끝남)은 `shift 2` 가 실패해 **무한 루프**다(`:341-344`, `set -e` 없음).
- `make_fresh_dir` 의 `[ -e ]` 뒤 `mkdir -p`(`:301-302`) TOCTOU. 빌드 로그를 확인 없이 `>` 로 덮어쓴다(`:572`, `:580`).
- 작성(단계 5)과 서명(단계 8) 사이에 `windows.manifest` 를 고정하지 않는다.
- deploy 의 `exit 3`(ESCALATE)과 manifest/sig 부분 교체 실패가 모두 `exit 1` 로 뭉개진다(`:784-785`).
- dry-run 이 같은 버전의 실제 실행을 막는다. 리허설 분기(`:761-764`, `:801`)는 도달 불가이고, 도달하면 실제 deploy
  `--dry-run` 이 ssh 로 NAS 에 `mkdir -p` 를 한다.
- 실제 단계 8(키 읽기·서명·검증기)을 도는 테스트가 없다. 테스트 쪽: `KEY_MADE` 는 대입의 종료 코드를 읽어 항상 1,
  "두 실행 중 하나는 거부" 는 빌드 디렉터리 거부로도 충족된다.

## RV-15 — 테스트 정리가 pid 로 프로세스를 죽인다 (중대, 테스트 안전)

- `client_recovery_ui_test.cpp:379-390` 은 실패한 실행의 뒷정리로 맵의 모든 항목에 `OpenProcess(PROCESS_TERMINATE, pid)`
  → 2초 대기 → `TerminateProcess` 를 한다. 슬롯의 SYNCHRONIZE 핸들은 쓰지 않는다.
- 같은 파일이 가짜 pid 를 맵에 넣는다: `1000+i`(마지막 1049, `:145`), `4242`(`:171`). `recovery_check` 는 실패 시
  throw 하므로 `:175`·`:190` 검사가 실패하면 그 항목이 남은 채 정리가 돈다. 변이 실행에서 일부러 실패시키는 바로 그 검사다.
- **측정(이 PC): `OpenProcess` 는 pid 의 하위 2비트를 무시한다.** 자기 pid+1·+2·+3 으로 열어도 같은 프로세스가
  열렸다. 따라서 1049 는 1048, 4242 는 4240 을 연다. 같은 사용자의 그 pid 프로세스가 종료될 수 있다.
- 정정: 1차 검수는 "4의 배수가 아니라 우연히 안전" 이라고 적었다. **틀렸다.**
- 수정 방향: 테스트가 띄운 프로세스의 핸들로만 대기·종료한다. 핸들이 없는 슬롯은 건너뛴다.

## RV-16 — 자기 복사본 검사·실패할 수 없는 단언·판정 누락 (중간)

- 핸들 누수 50회 루프 두 개(`client_recovery_ui_test.cpp:128-160`)가 `begin_session` 대신 테스트가 손으로 쓴 복사본을
  돈다. 제품의 `CloseHandle(previous->second.cancelEvent)`(`client_shell_main.cpp:1004`)를 지워도 통과한다.
- `liveViewersFor(...) <= 1`(`:226-231`, `:249`)은 도우미가 0 또는 1 만 돌려주므로 실패할 수 없다.
- (보고) `control_resume_e2e_test.cpp:540` "the viewer re-keyed nothing" 은 진행 중인 복구가 없어 실패할 수 없다.
- **워킹트리의 C3 r5 미커밋 편집**은 "프레임이 있었다" 검사를 `NOT JUDGED` 출력으로 바꾸고 check 로 세지 않는다.
  복구가 5초보다 짧고 그동안 호스트가 아무것도 보내지 않으면 이전에는 FAIL, 지금은 ALL PASS 다. 새
  `--capture-window-title` 전제는 확인하지 않는다(창을 못 찾으면 모니터 캡처로 조용히 돌아간다). 새 등식
  `mediaDuring == proxyMediaDuring` 은 두 스레드의 계수기를 다른 순간에 읽어 가짜 FAIL 을 낼 수 있다.
  **커밋 전에 SKIP 계수로 세고, 전제를 확인하고, 등식에 여유를 둔다.**

## RV-17 — 테스트가 기계에 하는 일 (낮음)

- `directory_retry_test.cpp:383-397` 은 기록한 송신을 **실제로도** 보낸다. 대상에 `211.218.222.1:60420`(history 의
  회사 공인 주소), `192.168.20.16:60420`, `239.1.2.3` 이 있다. 소켓이 127.0.0.1 바인드라 OS 가 거부할 수 있다(미측정).
- 같은 파일의 `exe_directory()`(`:88`)는 `"\/"` 를 `"/"` 로 읽어 항상 `"."` 을 돌려준다. 캐시 파일이 exe 옆이 아니라
  현재 디렉터리에 쓰인다.
- (보고) 대역 프로세스 `cmd /c ping -n 60` 을 죽이면 ping 이 최대 60초 고아로 남는다. e2e 의 job 생성·할당 결과를
  확인하지 않는다. `%TEMP%\remote60_punch_e2e_*` 가 남는다. `update_stop_process_test` 의 고정 이름
  `childexit-999999-1.txt` 는 동시 실행끼리 서로 지운다.

## RV-18 — 아무것도 테스트를 자동으로 돌리지 않는다 (낮음)

- CMake 에 `add_test`/`enable_testing` 이 없다. `gnlink_release.sh` 는 테스트를 돌리지 않는다.
- 새 e2e 4종은 `REMOTE60_ALLOW_HOST_E2E` 없이 `RESULT: SKIPPED` 와 exit 0 을 낸다.
- `gnlink_update_suites.ps1` 은 아무도 부르지 않는다. 기본 `-BuildDir` 는 다른 워크트리이고, stderr 한 줄에 전체가
  중단된다(PS 5.1 측정 보고). `update_stop_process_test` 는 0 checks 에도 PASS 를 찍는다.
- (보고) `viewer_udp_recovery_test` S15 의 "보류 1개, 뒤에 2개" 는 단언이 아니다. `waitForKeyFrame` 은 다른 스레드에서
  읽는 일반 bool 이다. 가짜 디렉터리는 `WSAECONNRESET` 한 번에 UDP 스레드가 끝난다.
- `wake_resend_test.js` 3단계 상한 20 은 burst rate limit 회귀를 통과시킨다(실측 13). 교체·만료·stale·tick-error·
  timer-cap 경로를 시험하지 않는다.
- `gnlink_viewer_connect_shots.ps1` 은 "3장이면 exit 0" 이라 적고 2장에도 exit 0 이다. 취소 뒤 뷰어가 실제로 끝났는지
  확인하지 않고 죽인다.

## 1차 검수 정정

- 셸 idle(RV-05): 1차 "릴리스 전 수정" → 2차 첫 판단 "낮음" → 최종 "중간"(재클릭 반복 근거).
- pid 정리(RV-15): 1차 "우연히 안전" 은 틀렸다. 측정으로 확인했다.
- 1차가 놓친 것: RV-00, RV-01, RV-02 의 하향·응답 유실 경우, RV-04, RV-06 셋째·넷째, RV-07, RV-09 의 헤어핀 조건,
  RV-10 첫째, RV-12, RV-13, RV-14 무한 루프.

## 재확인했고 문제 없던 것

- C1: 응답 예산(소스 25, 창 200, 초당 50, 맵 64, 포화 시 신규 거부). 창은 디렉터리 이벤트로만 열린다.
  `AuthorizePeer` 무변경. 서버 wake 패킷(49바이트, 버전 2) 일치. 안드로이드 펀치도 같은 공유 코드(49바이트,
  버전 2, 2026-08-03 이후)라 엄격해진 패킷 검사의 회귀가 아니다.
- C2: 이름 없는 이벤트와 상속 목록 크기, 핸들 소유권 이동, 취소 뒤 "trying anyway" 차단. 연결 후 남는 `host_wait`
  토큰은 목록 요청이 덮어쓰고, 알 수 없는 토큰은 화면에 찍히지 않는다.
- C3: 잠금 순서(뷰어는 resume→channel, 호스트는 `epochMu` 아래서 channel 을 잡지 않는다). probe seq 규칙, stream id
  펜싱, 재키잉 전 datagram 폐기, 중복 Ack 멱등, 실패한 동작 미재전송. `OnPacket` 순서 변경은 닫힌 경우만 바꾼다.
- 테스트: 이름으로 죽이는 곳은 없다. 주입은 `--input-target-pid` 로 테스트 창 안에 머문다. 가짜 디렉터리는
  127.0.0.1 에만 바인드한다. `ef533f2` 의 세 보완은 조건이 오지 않으면 여전히 실패하는 유계 대기다.
- 디렉터리 배포 patch 는 트리와 바이트 동일하다. 릴리스 스크립트는 자기 생성물 밖을 지우지 않고, dry run 은 키와
  네트워크를 건드리지 않는다. 서명 스크립트는 신뢰 키 확인 → 서명 → 공개키 검증 → 임시 파일 교체 순서이고 키를
  출력하지 않는다.

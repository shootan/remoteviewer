# report hello-delay-fix r1 — PunchAny 150 ms cadence + hello bounded 수신 (작업용1 remote#rqxbj3jb, 2026-10-22)

task t-q4eq9aiz / rev r1 / kind=implement-report. 정본 `.claude/task_hello_delay_fix_r1.md`, 계약 `codex_reconnect_hello_delay_r1.md` ①②.

## 고정 후보
- worktree `C:/work/gnlink-wt/fix-hello-delay`, 브랜치 `fix/hello-delay`, 기준 main `5063cd9`, HEAD **`83c3ac5`**. 워킹트리 깨끗.
- 커밋:
  - 제품 **`7646eef`**: directory_rendezvous.cpp · native_video_client_tcp_control.cpp/.hpp · viewer_startup.cpp(로그에 `recvErrors=` 만 추가)
  - 시험 **`4d8e4a8`**: hello_backlog_test.cpp(신규) · punch_any_test.cpp · CMakeLists
  - 문서 **`83c3ac5`**: docs/history.md
- 버전·서명·게시·push 없음.
- 출하 구성 빌드(`fix-hello-delay/build`, Release) sha256:
  - GNLinkViewer `66D7BF9D…806D0D`
  - GNLinkStream `5FC7D0EB…2EFFD`
  - GNLinkClient `ECFE393E…5D5EEB`
  - gate `--dir` 9/9, self-test 16 PASS.

## 구현 (계약 대응)
### ① PunchAny
- monotonic `nextSend` 기준으로 라운드를 최소 150 ms 간격으로 보낸다. 다음 송신 시각은 "지금 + 150 ms" 로 잡아서, 늦어진 라운드가 몰아서 나가지 않는다.
- `recvfrom` 의 결과가 아래 중 무엇이든 다음 라운드를 앞당기지 않는다: 오류, 빈 datagram, 후보가 아닌 곳의 datagram.
- 후보 응답은 받는 즉시 채택한다. 한 라운드의 송신은 기존대로 후보 전부.
- 한 라운드의 수신 상한은 256 이다. 넘으면 남은 라운드를 25 ms 단위로 기다린다(spin 방지). stop 은 매 송신·매 대기 전에 확인하고, 대기 한 번은 150 ms 를 넘지 않는다. 전체 budget 은 그대로다.
- 오류 분류는 기존 `classify_udp_recv` 를 쓴다.
  - Windows 의 WSAECONNRESET·WSAEMSGSIZE, 시간초과, 빈 datagram, (POSIX) ECONNREFUSED → 일시 오류로 계속.
  - 그 밖의 회복 불가 소켓 오류 → `punch receive failed (error N)` 로 즉시 끝낸다(삼키지 않음).
- 단일 `Punch()` 는 제품 호출자가 없어서 바꾸지 않았다.

### ② hello
- Hello 1회 송신 뒤 그 slice 가 끝날 때까지 큐를 계속 읽는다. 라운드당 수신 상한은 512 이고, 매 수신 전에 stop 을 확인한다.
- ack 가 아닌 것을 읽었을 때: datagram 은 `badAcks`, 빈 datagram 은 무시, ICMP 리셋·잘림은 새 보조 지표 `recvErrors` 로 센다. 셋 다 같은 라운드에서 다음 항목을 계속 읽는다.
- 시간초과나 회복 불가 오류는 라운드를 끝낸다(종전과 같음).
- 다음 Hello 는 원래 시계(slice 뒤 `retrySleepMs`)에만 나간다. 송신 실패 처리·budget·stop 의미는 그대로다.
- ACK 판정(길이·magic·kind·version·FEC·토큰 시 DirectoryAuth)과 지표 의미(`attempts`=Hello 송신 수, `badAcks`=읽었지만 유효하지 않은 datagram)는 불변이다.
- 사전 flush·SIO_UDP_CONNRESET·timeout·재시도·인증·호스트 punch 예산은 바꾸지 않았다.

## 시험 (종료 코드 + 마지막 요약 줄)
### 신규·변경
- **hello_backlog_test 18/18 exit 0** (실제 `udp_hello_handshake`, loopback 실소켓):
  - 25 punch 응답 + ICMP 리셋 100 이 ack 앞에 쌓인 상태에서:
    - 뷰어 옵션(200/50): **0 ms, attempts 1**, badAcks 25, recvErrors 100.
    - Android 옵션(250/0): 0 ms, attempts 1, badAcks 25, recvErrors 62.
  - ack 없이 punch 가 계속 들어올 때: budget 1000 ms 에 1067 ms 로 종료, attempts 4. Android 옵션도 attempts 4. stop(300 ms) 은 372 ms 에 종료.
  - DirectoryAuth 없는 ack · 다른 version · FEC 없는 ack 는 거부. 대조: 토큰 없는 LAN 은 수락, 정상 ack 는 첫 Hello 에 수락.
- **punch_any_test PASS** (기존 3 + 신규 4):
  - 즉시 ICMP 오류 후보 2 + 무응답 1: 1.5 s 에 **10 라운드**(budget 1508 ms).
  - 무관·빈 datagram 연속 유입에서도 10 라운드.
  - 500 ms 에 열리는 후보는 **626 ms** 에 채택.
  - stop(300 ms) → 321 ms, `cancelled`.
- hello_stats_test 14/14.

### 변이 (원복 뒤 모두 PASS)
- H1 PunchAny 시계 제거(`nextSend = now`) → **397 · 192 라운드 FAIL**.
- H2 구식(ack 아니면 라운드 종료) → 뷰어 **1554 ms FAIL**. Android 는 같은 backlog 에 Hello 26회, 연속 유입에 66회로 FAIL.
- H3 ICMP 오류에 라운드 종료 → **6278 ms FAIL**(현장 규모 재현).
- 컴파일되지 않은 변이 H4 는 무효로 버렸다.

### 영향 시험 (두 파일을 쓰는 대상, UI 뷰어 e2e 제외) — 29종 exit 0
- shared_core · connect_op_gate · directory_session_client · directory_retry(133) · clipboard_wire(53) · udp_fec_interleave · udp_fec_single_chunk(501) · udp_fec_single_chunk_loss(405) · client_udp_recv_loop · bandwidth_observe_wire(25) · host_wire_cap_idr(14)
- viewer_udp_recovery · udp_control_e2e · control_resume_e2e(60) · host_two_dispatcher(23) · host_punch_reply_e2e(102) · host_control_deadline(16) · host_reboot_delegate(22) · android_session_reboot(17) · bwe_observe_e2e(14) · fec_single_chunk_host(136) · host_monitor_fallback(173)
- abr_client_evidence `--mode none/present/stop` 각 11
- client_session_role_park 13
- pinned_seam_boundary PASS — 첫 실행은 pinned 시험 호스트를 빌드하지 않아 FAIL 이었다. 빌드한 뒤 PASS.
- 제외: `ka_relay_client_test` (기준판에서도 실패).

## 실 재현 전후 (이 PC, 콘솔, SLOT CreateNew→삭제, 설치본·실서비스 무조작, 잔여 프로세스 0)
- 측정용 scratch worktree:
  - `inv-hello-before` = 5063cd9 + 시험 seam(loopback 포트, 입력 off)만
  - `inv-hello-after` = 4d8e4a8 + 같은 seam
  - 두 빌드 모두 진단 로그 없음.
- runner `lockcred-scratch/hello_reconnect_runner.js`: Host 생존 + 뷰어 재생성. 각 배치의 1번은 첫 연결이다.

| 조건 | before (5063cd9) | after (4d8e4a8) |
|---|---|---|
| 11 s 유지 재연결 11회 | **3304 ms 1회**, 나머지 324–810 | **전부 264–546** |
| 4 s 유지 재연결 7회 | 558–929 | 264–543 |
| 첫 연결 | 559 · **5977** | 538 · 544 |

- **기제 대조:**
  - before 의 느린 두 회차(3.3 s·6.0 s)는 모두 PunchAny `ms=2~4`(폭주)였고, 이어서 badAcks 25 였다.
  - after 에서도 "즉시 응답 없음" 상황은 12회 중 6회 있었다. 이 경우 PunchAny 는 **`ms=152–160`(한 간격 대기)** 였고, hello 는 badAcks 1–2, **recvErrors 0** 이었다.
  - 이전 조사의 base/cand 수치(4/14, 1/14 >3 s)는 진단 빌드 기준이라 대조로만 인용한다.
- **실제 사슬** (DW 4db9f17 + 7646eef cherry-pick, scratch `inv-hello-chain`, `dxgi_wedge_reconnect_runner.js`) — 3회 모두 exit 0 ALL PASS:
  - presentfail ×2, desktop ×1.
  - **첫 재연결** hello 538 / 538 / 269 ms(badAcks 1–2, recvErrors 0) → `result=restored target=desktop`.
  - 첫 연결 hello 530–543 ms. 2번째 재연결은 필요 없었다.

## Android 영향
- 같은 코드가 들어간다: `apps/android_direct_client/app/src/main/cpp/CMakeLists.txt` 가 directory_rendezvous.cpp(PunchAny, `native_bridge.cpp:307`)와 native_video_client_tcp_control.cpp(`udp_hello_handshake`, `native_video_client_session.cpp`, retrySleepMs 0·slice 250)를 컴파일한다.
- 확인한 것:
  - `gradlew :app:externalNativeBuildDebug` 4 ABI(arm64-v8a·armeabi-v7a·x86·x86_64) 빌드 성공.
  - Android 옵션의 backlog·연속 유입·라운드 시계 시험은 Windows 에서 같은 portable 코드로 PASS.
  - android_session_reboot_e2e 17 PASS.
- 동작 변화: 종전 Android 는 ack 가 아닌 항목마다 Hello 를 즉시 다시 보냈다(backlog 1개당 Hello 1회). 이제는 라운드 시계(250 ms)로만 보내서 Hello 송신 수가 줄어든다.
- **미검증:** 실제 기기 실행. POSIX 에서 ECONNREFUSED 를 일시 오류로 둔 판단(연결되지 않은 소켓은 보통 보고하지 않음)도 확인하지 않았다. APK 버전·빌드·배포 무변경.

## 한계·남은 것
- ACK 송신 로그는 클라이언트 큐 도착의 wire 증거가 아니다. 오류 recvfrom 의 `from` 으로 ICMP 발신 주소를 단정하지 않는다. 25개의 송신 경로 귀속은 추정이다.
- 재현은 loopback 시험 호스트(LAN 후보가 ICMP 를 내는 조건) 기준이다. 실 LAN/NAT·회사 PC 전체 원인과 빈도는 일반화하지 않는다.
- 검증용이 관찰한 첫 연결 12.8 s 는 같은 기제로 추정하지만, 그 실행 로그로는 대조하지 않았다.
- **조사용 scratch 삭제(정정 전 실행, 이미 통지 seq 13329):**
  - inv-hello-base/cand/diag 는 당시 정본의 "정리 대상" 에 따라 지웠다. 순서는 junction rmdir → reparse point 0 확인 → `git worktree remove --force` 였고, WebView2 SDK 원본은 무사했다.
  - 대표 로그는 그 전에 `lockcred-scratch/hello_evidence/` 로 복사했다.
  - 이후 만든 `inv-hello-before`·`inv-hello-after`·`inv-hello-chain` 은 보존한다. 각각 `third_party\webview2` junction 이 있다.

다음 담당: 검증용 — `83c3ac5` 핵심 반례(backlog·cadence·변이)와 실 재현·사슬 독립 실행 → Codex.

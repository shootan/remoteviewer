# report hello-delay-fix r2 — Codex 83c3ac5 NEEDS_CHANGES N1·N2 (작업용1 remote#rqxbj3jb, 2026-10-22)

task t-q4eq9aiz / rev r2 (정본 `task_hello_delay_fix_r1.md` 'r2 보수', 근거 `codex_hello_delay_fix_r1.md`) / kind=implement-report.

## 고정 후보
- `fix/hello-delay` HEAD **`6e89962`**, 워킹트리 깨끗.
- r2 커밋:
  - 제품 **`bcb6a46`**: directory_rendezvous.cpp/.hpp · native_video_client_tcp_control.cpp/.hpp
  - 시험 **`3e625d8`**: CMakeLists · punch_any_test · hello_backlog_test
  - 문서 **`6e89962`**
- r1 커밋(7646eef·4d8e4a8·83c3ac5) 위에 쌓았다. 버전·서명·게시·push 없음.
- 출하 구성 sha256:
  - GNLinkViewer `60487752AB753A4B632B075A950E57DF48315D510C8AFE10270F23ABD9E9FBB2` (r1 대비 변경)
  - GNLinkStream `5FC7D0EB…2EFFD` · GNLinkClient `ECFE393E…5D5EEB` (r1 과 같음 — 두 exe 는 이 코드를 쓰지 않는다)
  - gate `--dir` 9/9, self-test 16.

## N1 — read batch 상한이 송신 시계를 앞당기던 것
- `native_video_client_tcp_control.cpp` hello 라운드: batch(512)가 slice 보다 먼저 차면 `fullBatches++` → **10 ms 쉬고 다시 읽는다. 라운드의 원래 끝(roundEnd)까지** 계속한다.
- 다음 Hello 는 roundEnd 뒤 `retrySleepMs`(종전 시계)에만 나간다. 상한 확대·timeout 추가·Android pause 추가는 하지 않았다.
- stop/deadline 은 매 수신과 매 pause 전에 확인한다.
- hpp 의 낡은 주석(`pause after … a bad ack`)을 라운드 정의에 맞게 고쳤다. 관측용 `fullBatches` 를 추가했다.
- 시간초과·회복 불가 오류에서 라운드가 끝나는 동작은 종전과 같다(이번 범위 밖).

## N2 — PunchAny 의 오류 분류
- `punch_receive_is_transient` 를 **PunchAny 로컬 판정**으로 바꿨다. 광역 `classify_udp_recv` 와 세션 수신 정책은 불변이다.
- 계속(150 ms 박자 유지):
  - 시간초과·EINTR
  - UDP recvfrom 문서의 경로별 보고: WSAECONNRESET(이전 송신의 ICMP Port Unreachable), **WSAENETRESET(TTL 만료)**, WSAEMSGSIZE
  - POSIX: ECONNREFUSED
- 종료: 그 밖(WSAENOTSOCK·EINVAL·ESHUTDOWN·NETDOWN 등 소켓 자체 실패) → `punch receive failed (error N)`.
- 문서 근거가 없는 코드(WSAEHOSTUNREACH 등)는 일시 오류로 두지 않았다. 실제 재현으로 확인한 것도 아니다.
- 시험 seam: `REMOTE60_RENDEZVOUS_TEST_SEAM`(recvfrom 대체 hook)은 시험 대상에서만 정의되고 제품에는 없다(gate 9/9).

## 시험 (종료 코드 + 마지막 줄)
- **hello_backlog 28/28**
  - N1 반례: 3000개를 미리 채우고 송신 스레드가 쉬지 않고 계속 보탠다.
    - PC(200/50): **full batch 52회, Hello 4회** / 1059 ms.
    - Android(250/0): **full batch 65회, Hello 4회**.
    - 스레드 CPU 는 측정 해상도(15.6 ms) 미만으로 보고됐다.
  - 2000개 뒤의 ack: 1라운드 51 ms(full batch 3회).
  - torrent 중 stop: 310 ms.
  - r1 시험(backlog 25+100, 인증 음성 등)은 모두 유지.
- **punch_any PASS** (기존 + r1 4종 + r2 3종):
  - 한 경로 TTL 보고 13회 뒤 응답 후보를 204 ms 에 채택.
  - WSAENOTSOCK 은 즉시 `error 10038`.
  - TTL 이 계속 와도 1.5 s 에 10 라운드, stop 329 ms.
  - 기존 10054 loopback 대조 유지.
- hello_stats 14/14.
- **변이 대상**(검증용이 제품 소스 수정 없이 실행 가능, 둘 다 기대대로 FAIL):

  | 대상 | 결과 | 내용 |
  |---|---|---|
  | `remote60_hello_backlog_mutant_batch_test` | exit 1, 3 FAIL | PC Hello 16 · Android **301** Hello(CPU 437 ms) · 2000 뒤 ack 4라운드 |
  | `remote60_punch_any_mutant_netreset_test` | exit 1, 6 FAIL | 첫 TTL 에 14 ms 만에 `error 10052` 로 포기, 응답 후보 놓침 |

- **영향 시험(r2)**: connect_op_gate · shared_core · udp_control_e2e · control_resume_e2e 60 · android_session_reboot 17 · host_punch_reply 102 exit 0.
  - **directory_retry 1/133 FAIL**(`address observation timed out`): 기존 간헐 건, 작업용2 별도 조사.
  - **viewer_udp_recovery 3 FAIL**(`stream flowed` 처리량 기준, 줄 1081/1098/1146): 같은 시각 **기준판 5063cd9 빌드도 2/2 같은 세 줄 FAIL**(max seq 53–74). r1 시점 후보는 PASS. **원인 미확정 — ledger 후보.** (정정: 처음 적었던 'qemu 부하' 귀속은 철회한다. 검증용 측정 당시 qemu 0.15 core/s·전체 CPU 3–4% 로 뒷받침되지 않는다.) directory_retry 간헐과 원인을 합치지 않는다.
- 실 재현(r2, inv-hello-after=3e625d8+seam): 11 s 유지 재연결 8회 + 첫 연결 1회, **266–539 ms**(>3 s 0).
- 실제 사슬(DW 4db9f17 + r1/r2 패치, inv-hello-chain): presentfail·desktop 모두 **ALL PASS**. 첫 재연결 hello 262/543 ms, recvErrors 0 → `restored target=desktop`.

## Android 영향 (0/250)
- 공유 코드 두 곳이 모두 들어간다. `externalNativeBuildDebug` 4 ABI 빌드 성공(r2).
- Android 옵션(250/0)의 N1 반례 PASS, 변이 301 Hello FAIL.
- 실제 기기 실행과 POSIX 실제 오류 의미는 **미검증**이다.

## 한계·보존
- WSAENETRESET 은 seam 주입으로만 시험했다. 실제 TTL 만료는 재현하지 않았다.
- 실 LAN/NAT·회사 PC 일반화는 하지 않는다.
- scratch 보존: repo root 밖은 삭제하지 않았다. `inv-hello-before/after/chain`(각각 junction 포함)과 `lockcred-scratch/hello_*` 를 남겨 두었다. inv-hello-after 는 3e625d8 로 checkout 했고(seam 패치 유지), inv-hello-chain 은 r2 패치를 staged 로 추가했다.

다음 담당: 검증용 — `6e89962` 변이 대상·반례·gate 독립 실행 → Codex.

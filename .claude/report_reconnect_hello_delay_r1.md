# report reconnect-hello-delay r1 — 첫 재연결 hello 지연 조사 (작업용1 remote#rqxbj3jb, 2026-10-22)

task t-uu1zshde / rev r1 / kind=investigate-report. 정본 `.claude/task_reconnect_hello_delay_r1.md`, 근거 `codex_dxgi_wedge_fix_r3.md` ②. **조사만. 제품 수정·커밋 없음.**

## 결론
- **기준판 5063cd9 에서도 재현된다.** 지연을 만드는 코드 경로는 5063cd9 와 4db9f17 사이에 **바이트 단위로 같다.** 아래 11개 파일의 `git diff --stat 5063cd9 4db9f17` 출력이 비어 있다: rendezvous · hello loop · punch_reply · directory_client · host_startup_control · host_session · directory_session_bootstrap · viewer_udp_session · server.js.
- **기제(로그 근거):**
  1. 새 뷰어의 PunchAny 가 첫 응답을 기다리는 동안, 응답 없는 LAN 후보에서 온 ICMP(WSAECONNRESET)를 받을 때마다 대기 없이 바로 다음 라운드를 보낸다. 이 tight loop 가 수백 라운드 돈다.
  2. 그 사이 **호스트의 punch 응답(소스당 최대 25개) + 남은 ICMP 오류 수십~백여 개**가 뷰어 소켓 수신 큐에 쌓인다.
  3. hello 루프는 큐에서 **한 번에 한 개**를 읽고, ack 가 아니면 **50 ms 쉬고** 다시 보낸다. 그래서 호스트가 이미 보낸 HelloAck(시작 ≈0.3 s 에 도착)가 그 뒤에 묻힌다.
  4. 결과: hello 시간 ≈ 62 ms × (25 + 남은 ICMP + 1).
  5. 이것이 호스트의 control 읽기 마감 10 s 를 넘으면 그 세션은 이미 닫혀 있다 → C1PKrA 의 `window_list_timeout` → exit 43.
- 조건: 호스트의 punch 응답 창(디렉터리 이벤트가 연 10 s)이 **닫혀 있을 때** 재연결하면 PunchAny 첫 응답이 늦어 폭주가 난다. 창이 열려 있으면 즉시 응답(ms=0)이 와서 빠르다.

## 재현·측정 (이 PC, 콘솔 세션, SLOT CreateNew → 삭제 완료, 설치본·실서비스 무조작, 잔여 프로세스 0)

### 방법
1. **자체 runner** `C:\work\gnlink-wt\lockcred-scratch\hello_reconnect_runner.js`
   - 그 worktree 의 운영 directory(server.js, loopback) + GNLinkHostUiTest(UIA 로그인) + GNLinkStreamMigrationTest(loopback 포트, 입력 주입 off).
   - GNLinkViewer 를 **셸과 같은 인자·env** 로 직접 띄운다.
   - 연결 → 유지 → 인사 없이 종료(TerminateProcess) → 1 s 뒤 새 뷰어. 즉 "Host 생존 + Viewer 재생성".
2. **원래 사슬** `dxgi_wedge_reconnect_runner.js presentfail` (후보 진단 빌드).

### 빌드 (모두 미추적 scratch worktree, 커밋 없음)
- `inv-hello-cand` = 4db9f17 그대로.
- `inv-hello-diag` = 4db9f17 + 진단 로그만:
  - 뷰어 hello 의 recv 결과·오류·출처
  - PunchAny 의 recvfrom
  - 호스트의 hello 분류·ack 송신·reader WSAECONNRESET 수
- `inv-hello-base` = 5063cd9 + **4db9f17 의 시험 seam 만 이식**(`streamExtraArgs`: loopback 바인드·자체 포트·입력 off, `REMOTE60_HOST_TEST_SEAM`/host_app_ui_test 안) + 같은 진단 로그.
  - seam 이 없으면 기준판 시험 호스트가 설치본 포트를 잡거나 LAN 에 바인드해 방화벽 대화상자를 띄운다.
  - 제품 hello 코드는 바꾸지 않았다.

### 분포

| 조건 | 판 | 재연결 수 | >3 s | 값(ms, 각 배치의 1번은 첫 연결) |
|---|---|---|---|---|
| 뷰어 4 s 유지 (응답 창 열린 채 재연결) | base | 10 (+2 첫 연결) | **0** | 432–937 |
| 뷰어 11 s 유지 (창 닫힌 뒤 재연결) | base | 14 (+2) | **4** | 6821 · 8057 · 6411 · 8431 / 나머지 233–685 |
| 뷰어 11 s 유지 | cand(무수정) | 14 (+2) | **1** | 3808 / 1556·1562·1564(25-punch 대역) / 나머지 158–1183 |
| presentfail 사슬 | cand 진단 | 20 | 2 | 6102 · 3067 / 1557·1562·1566 / 나머지 427–818 |

- 사슬 실행 중 2회는 셸 로그인 단계에서 끝났다(뷰어 시작 전, `auto sign-in: no-credential` 뒤 중단). 이 표에서는 INVALID 로 제외했다.
- 기준판과 후보의 빈도 차이(4/14 대 1/14)는 N 이 작고, 기준판 쪽에는 recv 마다 로그를 남기는 진단 빌드가 쓰였다. **빈도 차이로 판단하지 않는다.** 같은 기제가 양쪽에 있다는 것만 근거로 쓴다.

## 근거 — 진단 로그 (모든 hello 44+ 건 집계)
- **빠름**(0.4–0.9 s):
  - PunchAny `answered ms=0`.
  - hello 가 읽은 것: Punch(kind 303) 3–7개 + WSAECONNRESET 4–8개 → HelloAck(301).
- **느림:**
  - PunchAny `ms=2–24`. 그 사이 recvfrom 이 `err=10054 from=192.168.0.76:<호스트포트>`(이 PC 의 LAN 후보. 시험 호스트는 127.0.0.1 에만 바인드) 를 **291–297회** 반환했다(PunchAny 로그가 있는 빌드).
  - 이어지는 hello 가 읽은 것: **Punch 정확히 25개 + WSAECONNRESET 0–104개 → HelloAck.**

  | 출처 | hello ms | 남은 오류 | Punch |
  |---|---|---|---|
  | base | 6821 | 84 | 25 |
  | base | 8057 | 104 | 25 |
  | base | 6411 | 78 | 25 |
  | cand 진단 eybk6A | 6102 | 73 | 25 |
  | cand 진단 wyahk0 | 3067 | 24 | 25 |
  | 오류 없음 | 1557–1566 | 0 | 25 |

- **호스트 쪽**(같은 실행, eybk6A):
  - 05.912 에 새 세션 epoch 2 를 열었다.
  - 그 뒤 뷰어의 매 hello 를 `Retransmit` 로 분류하고 **매번 ack 를 송신**했다(`sendto=49 err=0`, 62 ms 간격).
  - 뷰어가 첫 ack 를 읽은 시각은 11.706 이다. 즉 **ack 는 도착해 있었고, 큐 앞의 항목을 소비하느라 늦게 읽혔다.**
- **25 의 출처:** `punch_reply.hpp:45 kPunchReplyPerSource = 25`(응답 창 안에서 소스당 응답 상한). 25개가 1.5 s 안에 읽혔다. 호스트의 자체 punch(`directory_client.cpp:42-43`, 25개를 200 ms 간격 = 5 s)보다 빨라서, 응답 경로 쪽으로 **추정**한다. 패킷 단위 귀속은 확인하지 않았다.
- **C1PKrA**(검증용 원본, 4db9f17) 타임라인도 같은 모양이다:
  - punch 응답 창 열림 16.422.
  - 거절 5회(`none-held`) → capability 갱신 → epoch 2 개설 16.733.
  - 뷰어 hello 181회 / badAcks 25 / 11.252 s.
  - **호스트 control 읽기 마감 10 s**(`host_startup_control.cpp:936 UdpControlLink(..., 10000)`)로 epoch 2 종료 26.913.
  - 뷰어 hello ok 27.677 → 닫힌 세션 → window-list 15 s 무응답 → exit 43.
  - 두 번째 재연결은 빨랐다.

## 소스 위치 (4db9f17 = 5063cd9 동일)
- `directory_rendezvous.cpp:256-258`: PunchAny 의 `recvfrom` 이 오류(WSAECONNRESET 포함)면 `if (n <= 0) continue;` → **대기 없이** 다음 라운드(3 후보 전부 송신). `kPunchIntervalMs=150`(:17)는 recv 시간초과에만 쓰여서, 오류가 즉시 돌아오면 간격이 없다.
- `native_video_client_tcp_control.cpp:292-308`: hello 1회 = 송신 1 + recv 1. ack 가 아니면 `retrySleepMs`(=50, `viewer_udp_session.hpp:74`) 쉰다. 큐에 쌓인 항목 하나당 ≈62 ms.
- 저장소 전체에 `SIO_UDP_CONNRESET` 사용 없음(grep). Windows UDP 의 ICMP port-unreachable 이 매번 recv 오류로 올라온다.

## 원인 후보와 최소 수정안 (제안만 — 결정은 검증용→Codex. timeout 증가·재시도 확대·인증 완화 아님)
1. **PunchAny 의 오류 구동 tight loop 제거.** recvfrom 오류(특히 WSAECONNRESET)를 받아도 다음 라운드는 `kPunchIntervalMs` 가 지난 뒤에만 보낸다. 같은 간격 안에서는 recv 만 계속한다. 송신량이 원래 설계(150 ms 마다 1 라운드)로 돌아가므로 재시도가 줄어든다.
2. **hello 루프가 ack 아닌 수신 항목 뒤에 쉬지 않기.** 같은 slice 안에서 다음 항목을 즉시 계속 읽는다(새 hello 를 더 보내지 않음). 또는 hello 시작 전에 punch 단계가 남긴 stale datagram/오류를 비블로킹으로 비운다. hello 횟수·budget·인증은 그대로다.
3. (선택) 뷰어 rendezvous/세션 소켓에 `SIO_UDP_CONNRESET`=FALSE. ICMP 오류 자체가 큐에 올라오지 않는다. Windows 전용이고 소켓 동작 변경이라 Codex 판단 사항이다.
- 1 또는 2 하나만으로도 C1PKrA 처럼 10 s 마감을 넘는 경우를 없앨 것으로 **예상**한다. 검증하지 않았다(수정 금지 범위).

## 미확인·한계
- 같은 PunchAny 에서 `ms=168–174` 인데 오류 0 인 경우(빠름)가 있다. 왜 그때는 ICMP 가 오지 않았는지는 확인하지 못했다.
- 진단 로그가 PunchAny 루프 속도를 바꾼다(관찰 효과). 그래서 빈도 비교에 쓰지 않았다.
- 실 LAN·NAT(LAN 후보가 실제로 도달 가능한 환경)에서 같은 폭주가 나는지는 미검증이다. 이 재현은 시험 호스트가 loopback 에만 바인드해 LAN 후보가 ICMP 를 내는 조건에 기댄다. 실기기에서도 도달 불가 후보(다른 망의 사설 주소 등)가 있으면 같은 경로가 열린다고 **추정**한다.
- 현장 영향(회사 PC 등)은 NAS 로그로 확인하지 않았다.

## 산출물 (모두 미추적, 커밋 없음)
- runner: `C:\work\gnlink-wt\lockcred-scratch\hello_reconnect_runner.js`
- 진단 패치: `hello_diag_patch.py`(+ inv-hello-diag 의 수동 추가 2곳), `base_seam_patch.py`
- 사슬 출력: `C:\work\gnlink-wt\lockcred-scratch\hello_runs\`
- fixture 로그:
  - `C:\work\gnlink-wt\inv-hello-{base,cand,diag}\.claude\test-tmp\{hello-reconnect-*,dxgi-wedge-*}`
  - 대표: base `hello-reconnect-mgxmQQ`, diag `dxgi-wedge-eybk6A` · `b55Nxq`
- worktree `C:\work\gnlink-wt\inv-hello-{base,cand,diag}`(detached) 은 재현 확인용으로 남겨 두었다.
  - ⚠️ 셋 다 `third_party\webview2` junction 이 있다. 지울 때는 junction 을 `rmdir` 로 먼저 떼고 `git worktree remove` 한다. `Remove-Item -Recurse` 나 `--force` 를 쓰지 않는다.

다음 담당: 검증용 — 원인 판단·수정 여부 → Codex.


## 9. 15:14 추가 실기 (15:09:57~15:17:41, 뷰어 로그 확보) — A2A task t-k0c8knr4

### 9.0 근거 범위
- **뷰어 로그 확보**: 회사 클라이언트가 15:09:56 재로그인(새 세션 토큰)하면서 `viewer.log`/`client.log` 업로드가 재개됐다(NAS 15:09:57~15:17:41, 8,790줄). 이는 재로그인의 결과이지 **P10(업로더 인증 결함)의 코드 수정이 아니다**. 이 PC 호스트의 업로드는 여전히 401(15:10 이후에도 `send FAILED … status=401`)이라 host.log 는 NAS 에 없고, 호스트 측 근거는 로컬 `host_app.log`(stdout 사본) 이다.
- 장치: viewer 68f79d01(회사) gen=11, host 8ec6ecb1(이 PC, 13:44 시작 프로세스 그대로 — 14:27 이후 `d3dMt=on` 상태 유지). 뷰어 0.2.100 은 `udp hello ack … nackNegotiated=1`·`keyRetries=` 필드(0.2.100 신설)로 직접 확인.
- 구간은 사용자 타임라인 기준의 근사치다(영상 시작 초 단위 미확정): P1 타이핑/정적 15:10:30~15:14:20, P2 영상 15:14:20~15:15:12, P3 정적 15:15:12~15:15:50, P4 UAC 15:15:50~15:16:20, P5 이후 ~15:17:41. 뷰어 1초 stats 라인(`recvFrames=`) 은 **전수** 카운터, `[present]`/`[control] seq=` 는 **전수 이벤트**, `[user-feedback]` 은 1초 rate-limit 표본. 양단 QPC 를 직접 빼지 않았고, 뷰어 `avgLatencyUs` 는 뷰어의 정렬(aligned) 시계 기준이며 합성 프레임은 제외된 값이다.

### 9.1 경로
- **직접 연결(P2P)**: 뷰어 `directory chose 175.207.45.151:43000 (public)`, 릴레이 저널은 `punch … answering in 2500ms x3` 뒤 `bound` 없음, 호스트 `session epoch=11 peer=new` 에 `endpoint translated`(릴레이 표시) 없음. 14시 세션(릴레이)과 다르며, 그 사이 무엇이 달라졌는지는 미확정(공유기 설정 변경 여부 미확인).
- 제어 RTT(뷰어 `[control] seq= rttUs=`, 전수 n=459): med 2.0ms, p95 4.5~10.2ms(구간별), max 20.2ms(UAC 구간). 회사↔집 왕복이 2ms 대라 **이 세션에서는 네트워크 몫이 작다**. 14시 릴레이 세션의 RTT 는 여전히 미측정.

### 9.2 이벤트별 판정

| 구간 | recv/s (decoded) | 실제/합성 | present gap med/p95 (ms) | avgLatencyUs med/p95 (정렬 시계, 실제 프레임) | 판정 |
|---|---|---|---|---|---|
| P1 타이핑/정적 | 5.8 (5.8) | 979 / 351 | 218 / 530 | 113 / 158 ms | 정상. 희소 콘텐츠라 present 간격은 콘텐츠 cadence(정적 refresh·kick) |
| P2 영상(근사) | 50.5 (50.5) | 2,620 / 6 | 16.1 / 46.9 | 8.4 / 26 ms | 정상. 호스트 30s 창 `udpTxFrames` 증분 1,695(≈56fps)·33Mbps(15:14:31~15:15:01) 와 부합. drop/stale/congestion 0 |
| P3 정적 복귀 | 12.7 (12.7) | 404 / 78 | 93 / 250 | 101 / 156 ms | 정상 |
| P4 UAC | 10.6 (10.4) | 254 / 58, skipped 6 | 94 / 296 (max 809) | 83 / 207 ms | 복귀 정상. **Congested 진입 2회**(9.3) — 사용자 체감 없음 |
| P5 이후 | 8.1 (8.1) | 450 / 213 | 155 / 342 | 127 / 196 ms | 정상 |

- 전 구간 `congestionState` 는 UAC 의 2회(각 ~50~300ms)를 빼면 normal, `staleRefRecoveries=0`, `holdLatestDrops=0`, `[liveness]`/peer-lost 없음, decode 실패 없음. 15:17:41 `done`(사용자 종료).
- 뷰어 로컬 지연(user-feedback 표본): decUs med 5.0~5.3ms, decode→present(d2pUs) med 4.7~8.7ms, queueToPresent ~0.3ms — 뷰어 몫은 ~10ms 수준. 타이핑 구간 avgLatency ~113ms 는 호스트 측 인코더 보유(150ms trailing kick 설계)와 크기가 부합하나, 호스트 전수 per-frame 계측이 없어 확정하지 않는다.

### 9.3 복구 상태 머신·NACK
- **NACK 협상 실기 확인**: `nackRequested=1 nackNegotiated=1 nackHoldUs=120000 recvTimeoutMs=25`, 모든 udp-assembly 라인 `nackOn=1`. 그러나 세션 전체(청크 310,337개) `nackSent=0 nackChunks=0 nackExhausted=0 reorder=0 fecRecovered=0` — **청크 손실이 없어 NACK 재전송 효과는 미입증**(협상만 입증).
- **완성 IDR 무요청 재동기 실기 확인**: `dropped` 합 57 = `keyResync` 합 57, `keyReq` 합 0 — 호스트가 큐를 비우고 보낸 IDR 57건 모두 seq gap 을 가진 채 도착했고(09-05 의 90/90 패턴과 동일 원인), 0.2.100 은 한 건도 reason=2 를 보내지 않았다. 호스트 로그의 keyframe-request 는 세션 시작 reason=7 1회 + UAC reason=1 2회뿐(총 3, 09-05 같은 구간 대비 폭주 없음).
- 복구 타이머: 세션 시작 첫 IDR 대기 중 1회(`waitedUs=529618`) 뒤 `keyRetries=1` 로 고정 — 그 외 발동 없음.
- **UAC 구간 Congested 2회(신규 관측, 원인 후보)**: 15:15:57.972 `reason=decode_queue decodeQueueLagUs=566679 in.recvGapUs=2245` → IDR(reason 1) → 52ms 뒤 recovering → 308ms 뒤 normal; 15:15:59.178 `decodeQueueLagUs=847684 in.recvGapUs=2701` → IDR → 216ms 뒤 recovering → 600ms 뒤 normal. 호스트 로그의 같은 시각: `capture-pipeline-flushed reason=dxgi-runtime-fallback`(15:15:56.177, WGC 시작) 의 첫 프레임 `sendQpcUs−captureQpcUs=126ms`, `desktop-backend-restored`(15:15:59.610, DXGI 복귀) 의 첫 프레임 **820ms**. 즉 백엔드 전환 직후 프레임의 capture stamp 가 송신 시각보다 크게 뒤처진 채 촘촘히 도착해 뷰어가 decode 적체로 읽었다. 3단계에서 고친 synthetic 시계 문제와는 다른 경로(실제 프레임의 stamp 지연)이며, 비용은 IDR 2회(~300KB×2)·burstDrops 8·present gap max 809ms. 사용자 체감 없음. 원인(전환 시 stamp 기준 불일치 vs flush 지연)은 미확정 → 후속 항목.

### 9.4 UAC 좌표 오차 — 이번엔 판정 불가
- `secure_input.log` 에 15:15:5x 의 `inject landed` 기록이 없다. 원인은 에이전트의 진단 로그 예산: `diag_inject_landing` 은 에이전트 프로세스당 **처음 12개 버튼 이벤트만** 기록(`secure_input_service_main.cpp` `static std::atomic<int> remaining{12}`)하며, 14:27:25 에 뜬 에이전트가 그 예산을 이미 소진했다(15:15 에 새 `agent started` 없음). 따라서 15:15 UAC 클릭이 어디에 떨어졌는지는 로그로 알 수 없고, 사용자의 "문제없음" 은 좌표 오차 해소의 근거가 아니다. 호스트 프로세스도 13:44 것 그대로라 4절의 stale target rect(2236x1232) 는 그대로 남아 있을 가능성이 높다(추정).
- 후속: (a) 진단 예산을 UAC 에피소드마다 재충전하거나 시간 기반 rate-limit 으로 바꿔 매 secure 클릭의 `in/target/mapped/cursor` 를 남길 것(계측 항목), (b) P9 수정 후 재검증.

### 9.5 이전 미검증 항목 중 해소/잔여
- 해소: NACK **협상**(뷰어 측 확인), 완성 IDR 무요청 재동기 실기 확인(57/57), 복구 타이머 폭주 없음, 뷰어 decode/present 지연(~10ms), 제어 RTT(직결 2ms), liveness/peer-lost 없음, 이번 세션 경로(P2P).
- 잔여: NACK 재전송 **효과**(손실 0), 14시 릴레이 세션의 RTT/릴레이 몫, 호스트 전수 capture→wire 분포, P2P 성립/미성립을 가른 요인, UAC 클릭 좌표(진단 예산 소진), 5분 장기, 백엔드 전환 시 stamp 지연 원인, P10 코드 수정.

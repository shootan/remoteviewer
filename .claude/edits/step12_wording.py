import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
REPORT = ROOT + r'\docs\field_test_2026-09-07_0.2.100.md'
PLAN = ROOT + r'\docs\구현계획.md'
HISTORY = ROOT + r'\docs\history.md'

# ---------------- report section 9 ----------------
edit(REPORT, [
# 9.0 avgLatencyUs definition
("""양단 QPC 를 직접 빼지 않았고, 뷰어 `avgLatencyUs` 는 뷰어의 정렬(aligned) 시계 기준이며 합성 프레임은 제외된 값이다.""",
 """양단 QPC 를 직접 빼지 않았다. 뷰어 `avgLatencyUs` 는 `FrameGate::aligned_lag_us`(`viewer_frame_gate.cpp:96-111`) 의 1초 평균이다: 첫 실제 프레임의 (capture stamp, 뷰어 시각) 쌍을 기준 0 으로 두고, 그 뒤 프레임마다 뷰어 경과 − capture 경과 를 취하되 음수는 0 으로 절단한 값으로, decode 뒤·present 전에 평가된다(`viewer_video_receiver_frame.cpp:424`). 즉 **첫 샘플 대비 상대 타임라인 lag 지표**이지 RTT/오프셋으로 맞춘 절대 capture→display·입력 지연이 아니며, 합성 프레임은 제외된다."""),
# 9.2 table header
("""| 구간 | recv/s (decoded) | 실제/합성 | present gap med/p95 (ms) | avgLatencyUs med/p95 (정렬 시계, 실제 프레임) | 판정 |""",
 """| 구간 | recv/s (decoded) | 실제/합성 | present gap med/p95 (ms) | avgLatencyUs med/p95 (1초 평균 상대 타임라인 lag 지표, 절대 지연 아님) | 판정 |"""),
# 9.2 viewer-local bullet: no host attribution, no viewer-total generalization
("""- 뷰어 로컬 지연(user-feedback 표본): decUs med 5.0~5.3ms, decode→present(d2pUs) med 4.7~8.7ms, queueToPresent ~0.3ms — 뷰어 몫은 ~10ms 수준. 타이핑 구간 avgLatency ~113ms 는 호스트 측 인코더 보유(150ms trailing kick 설계)와 크기가 부합하나, 호스트 전수 per-frame 계측이 없어 확정하지 않는다.""",
 """- 뷰어 로컬 단계 지표(`[user-feedback]` 1초 rate-limit 표본, 각각 별개 조건부 표본): decUs med 5.0~5.3ms, decode→present(d2pUs) med 4.7~8.7ms, queueToPresent ~0.3ms. 서로 다른 표본의 median 이라 합산해 "뷰어 총 지연" 으로 일반화하지 않는다.
- 표의 avgLatencyUs(타이핑 113ms, 영상 8.4ms 등)는 첫 샘플 대비 상대 lag 이라 호스트/네트워크/뷰어 어느 쪽 몫으로도 귀속하지 않는다. 절대 입력·종단 지연은 이 세션에서 측정하지 않았다."""),
# 9.3 NACK bullet
("""— **청크 손실이 없어 NACK 재전송 효과는 미입증**(협상만 입증).""",
 """— **재전송/FEC/재정렬이 한 번도 관측되지 않아 NACK 재전송 효과는 미입증**(협상만 입증). 이는 "손실 0 확정" 이 아니다: 청크 단위 결손이 없었다는 뜻일 뿐, AU 단위 불연속(`dropped` 57, 아래) 은 별도로 있었다."""),
# 9.3 keyResync bullet: do not assert the gap origin
("""- **완성 IDR 무요청 재동기 실기 확인**: `dropped` 합 57 = `keyResync` 합 57, `keyReq` 합 0 — 호스트가 큐를 비우고 보낸 IDR 57건 모두 seq gap 을 가진 채 도착했고(09-05 의 90/90 패턴과 동일 원인), 0.2.100 은 한 건도 reason=2 를 보내지 않았다.""",
 """- **완성 IDR 무요청 재동기 실기 확인**: `dropped` 합 57 = `keyResync` 합 57, `keyReq` 합 0 — seq 불연속 뒤 완성 IDR 이 도착한 경우 57건이 모두 keyResync 로 처리됐고, 0.2.100 은 한 건도 reason=2 를 보내지 않았다. 이 57건의 불연속이 송신 측 의도적 gap(큐 비움 뒤 IDR)인지 AU 전체 손실인지는 뷰어 로그만으로 구분할 수 없다(09-05 의 90/90 과 패턴은 같으나 원인 동일성은 미확정)."""),
# 9.3 UAC bullet: seq-linked chain, downgraded conclusions
("""- **UAC 구간 Congested 2회(신규 관측, 원인 후보)**: 15:15:57.972 `reason=decode_queue decodeQueueLagUs=566679 in.recvGapUs=2245` → IDR(reason 1) → 52ms 뒤 recovering → 308ms 뒤 normal; 15:15:59.178 `decodeQueueLagUs=847684 in.recvGapUs=2701` → IDR → 216ms 뒤 recovering → 600ms 뒤 normal. 호스트 로그의 같은 시각: `capture-pipeline-flushed reason=dxgi-runtime-fallback`(15:15:56.177, WGC 시작) 의 첫 프레임 `sendQpcUs−captureQpcUs=126ms`, `desktop-backend-restored`(15:15:59.610, DXGI 복귀) 의 첫 프레임 **820ms**. 즉 백엔드 전환 직후 프레임의 capture stamp 가 송신 시각보다 크게 뒤처진 채 촘촘히 도착해 뷰어가 decode 적체로 읽었다. 3단계에서 고친 synthetic 시계 문제와는 다른 경로(실제 프레임의 stamp 지연)이며, 비용은 IDR 2회(~300KB×2)·burstDrops 8·present gap max 809ms. 사용자 체감 없음. 원인(전환 시 stamp 기준 불일치 vs flush 지연)은 미확정 → 후속 항목.""",
 """- **UAC 연관 Congested 2회 — 뷰어 측 메커니즘은 seq 로 연결, 호스트 측 원인 미확정**: 15:15:57.972 `reason=decode_queue decodeQueueLagUs=566679 in.recvGapUs=2245 seq=24471` → IDR(reason 1) → 52ms 뒤 recovering → 308ms 뒤 normal; 15:15:59.178 `decodeQueueLagUs=847684 in.recvGapUs=2701 seq=24488` → IDR → 216ms 뒤 recovering → 600ms 뒤 normal. 비용은 IDR 2회(~300KB×2)·burstDrops 8·present gap max 809ms, 사용자 체감 없음.
  - `decodeQueueLagUs` 의 정의(`viewer_frame_gate.cpp:155-165`): 들어온 프레임의 capture stamp − 마지막 present 된 프레임의 capture stamp(Normal 상태에서는 `presentAnchorFloorUs` 와 max). recvGap>250ms 뒤 재개된 실제 프레임은 그 stamp 로 floor 를 재고정한다. 즉 이 값은 **호스트 stamp 끼리의 차**이지 뷰어 큐 길이 실측이 아니다.
  - 2차(seq 24488): 호스트 `desktop-backend-restored`(15:15:59.610 host) 의 `capture-switch first-frame`(59.643) 과 같은 ms 의 wire 라인이 `seq=24484 wireIntUs=809059` 이고, 뷰어는 seq 24484 를 present gap 793,566us·`c2eUs=812685`(호스트 stamp 끼리의 차) 로 받았다 — 첫 프레임 = seq 24484, 그 capture stamp 는 입큐 전 송신 준비 시각(`sendQpcUs`)보다 ~820ms 오래된 값. 809ms 침묵 뒤 재개라 floor 가 이 오래된 stamp 로 재고정됐고, 이어서 seq 24486(IDR 298KB, `decodeQueueLagUs=803306`, pts 64845091122 − 803306 = 24484 의 stamp 64844287816 과 일치)·24487·24488 이 2~3ms 간격의 신선한 stamp 로 도착해 24488 에서 임계 초과. 뷰어→호스트 요청 도착 두 쌍(57.988→58.544, 59.186→59.742) 이 모두 +556ms 라 벽시계 순서(뷰어 59.178 < 호스트 59.610)는 시계 오프셋으로 설명되며 seq 순서와 모순 없음.
  - 1차(seq 24471): WGC 시작(`dxgi-runtime-fallback` 15:15:56.177 host) 의 첫 프레임은 같은 ms 의 wire 라인 `seq=24456`(stamp 126ms 오래됨) 이고 뷰어는 24457 을 gap 110ms 로 정상 present 했다 — **전환 첫 프레임에서는 혼잡이 없었다**. 혼잡 직전 앵커는 2.3s 뒤 809ms 침묵 후 재개된 **seq 24469**(present gap 809,419us, `c2eUs=502680`) 이고, 24470(+77ms)·24471(+2ms) 이 신선한 stamp 로 이어져 566ms 차가 났다. 즉 1차는 "전환 첫 프레임 stamp" 로 설명되지 않는다.
  - 공통점은 "침묵 뒤 재개된 실제 프레임의 capture stamp 가 송신 준비 시각보다 0.5~0.8s 오래됨(→ 앵커가 오래된 stamp 로 잡힘) + 직후 신선한 stamp 프레임의 밀집 도착" 이며, 그 오래된 stamp 의 호스트 측 출처(WGC/DXGI 시계 차, flush 중 보유된 프레임의 실제 나이, 캐시 프레임 재인코딩 등)는 미확정이다. 3단계에서 고친 합성 프레임 시계 문제와의 관계도 미확정(24469/24484 는 호스트에서 실제 캡처 경로로 기록됐으나 stamp 출처는 확인 안 됨). `capture-switch first-frame` 의 `sendQpcUs` 는 주스레드 입큐 전 시각이라 wire 총 지연을 뜻하지 않는다. → P11 은 "UAC 연관 혼잡 2회 · 원인 후보/미확정" 수준으로 유지."""),
# 9.5 resolved / remaining
("""복구 타이머 폭주 없음, 뷰어 decode/present 지연(~10ms), 제어 RTT(직결 2ms)""",
 """복구 타이머 폭주 없음, 뷰어 decUs/d2pUs 표본 지표(별개 표본), 제어 RTT(직결 2ms)"""),
("""- 잔여: NACK 재전송 **효과**(손실 0), 14시 릴레이 세션의 RTT/릴레이 몫, 호스트 전수 capture→wire 분포, P2P 성립/미성립을 가른 요인, UAC 클릭 좌표(진단 예산 소진), 5분 장기, 백엔드 전환 시 stamp 지연 원인, P10 코드 수정.""",
 """- 잔여: NACK 재전송 **효과**(재전송/복구 미관측이라 미입증; 57건 불연속의 원인 구분 불가), 절대 입력/종단 지연(미측정), 14시 릴레이 세션의 RTT/릴레이 몫, 호스트 전수 capture→wire 분포, P2P 성립/미성립을 가른 요인, UAC 클릭 좌표(진단 예산 소진), 5분 장기, 침묵 뒤 재개 프레임의 오래된 capture stamp 출처(P11), P10 코드 수정."""),
])

# ---------------- plan P2 / P11 ----------------
edit(PLAN, [
("""nackNegotiated=1 협상 확인, 청크 손실 0 이라 재전송 효과 미입증 (history #400)]**""",
 """nackNegotiated=1 협상 확인, 재전송/FEC/재정렬 미관측이라 효과 미입증 — 손실 0 확정 아님, dropped 57 은 keyResync 처리 불연속 (history #400)]**"""),
("""- [ ] **P11** 캡처 백엔드 전환(WGC↔DXGI) 직후 실제 프레임 capture stamp 지연 → 뷰어 가짜 decode_queue 혼잡 — **2026-09-07 15:15 실기 관측 (history #400, 보고서 9.3)**
  - 증상: UAC 전환 시 Congested 2회(lag 567/848ms), IDR 2회·burstDrops 8·present gap max 809ms. 호스트 `capture-switch first-frame` 의 `sendQpcUs−captureQpcUs` 126ms/820ms. 사용자 체감 없음.
  - 완료조건: [ ] 전환 첫 프레임 stamp 기준(WGC/DXGI 시계·flush) 원인 확인 [ ] 전환 직후 프레임이 혼잡 트리거를 내지 않도록 stamp 보정 또는 게이트 유예 [ ] 회귀 테스트""",
 """- [ ] **P11** UAC(캡처 백엔드 전환 WGC↔DXGI) 연관 뷰어 decode_queue 혼잡 2회 — 원인 후보/미확정 — **2026-09-07 15:15 실기 관측 (history #400, 보고서 9.3)**
  - 증상: Congested 2회(seq 24471 lag 567ms / seq 24488 lag 848ms), IDR 2회·burstDrops 8·present gap max 809ms. 사용자 체감 없음.
  - seq 연결 결과: 2차는 DXGI 복귀 첫 프레임 seq 24484(capture stamp 가 입큐 전 송신 준비 시각보다 ~820ms 오래됨, 뷰어 c2eUs 812ms) 가 809ms 침묵 뒤 앵커로 잡힌 직후 24486~24488 이 신선한 stamp 로 밀집 도착; 1차는 전환 첫 프레임(seq 24456) 이 아니라 2.3s 뒤 809ms 침묵 후 재개된 seq 24469(c2eUs 503ms) 가 앵커. 공통 후보는 "침묵 뒤 재개 프레임의 오래된 stamp + 직후 밀집 도착" 이며, 오래된 stamp 의 호스트 측 출처와 합성 프레임 경로와의 관계는 미확정. `sendQpcUs` 는 입큐 전 시각이라 wire 지연이 아님.
  - 완료조건: [ ] 호스트에서 재개/전환 첫 프레임의 capture stamp 출처(WGC/DXGI 시계·flush 보유·캐시 재인코딩) 를 로그로 구분 [ ] 원인 확정 뒤 stamp 보정 또는 게이트 유예 결정 [ ] 회귀 테스트"""),
])

# ---------------- history #400 ----------------
edit(HISTORY, [
("""avgLatency(정렬 시계) med 8.4ms; 타이핑 5.8/s·present gap med 218ms·avgLatency med 113ms(희소 콘텐츠·인코더 보유 설계와 부합, 미확정); 뷰어 decode 5ms·decode→present 5~9ms. (3) **NACK 협상 실기 확인**(`nackNegotiated=1`, `nackOn=1`)이나 청크 310,337개 손실 0(`nackSent=0`) → 재전송 효과 미입증.""",
 """avgLatencyUs(1초 평균 상대 타임라인 lag 지표 — `aligned_lag_us`: 첫 샘플 기준 0, 음수 절단; 절대 입력/종단 지연 아님) med 8.4ms; 타이핑 5.8/s·present gap med 218ms·같은 지표 med 113ms(어느 쪽 몫으로도 귀속하지 않음); 뷰어 decUs med 5ms·d2pUs med 5~9ms 는 각각 별개 user-feedback 표본(합산 안 함). (3) **NACK 협상 실기 확인**(`nackNegotiated=1`, `nackOn=1`)이나 청크 310,337개 수신 동안 `nackSent=0 fecRecovered=0 reorder=0` 로 재전송/복구 미관측 → 효과 미입증(손실 0 확정 아님; `dropped` 57 은 keyResync 로 처리된 AU 불연속이며 송신 측 의도적 gap 인지 AU 손실인지 구분 불가)."""),
("""(5) UAC(15:15:56 wgc→15:15:59.6 dxgi): Congested 2회(reason=decode_queue, lag 567/848ms, 각 IDR 1회로 0.3~0.6s 내 normal) — 호스트 backend 전환 첫 프레임의 `sendQpcUs−captureQpcUs` 가 126ms/**820ms** 로, 전환 직후 실제 프레임의 capture stamp 지연이 뷰어에 decode 적체로 읽힌 것(3단계 synthetic 시계 문제와 별개, 원인 미확정 → 신규 항목 P11). 사용자 체감 없음.""",
 """(5) UAC(15:15:56 wgc→15:15:59.6 dxgi, host 시각): Congested 2회(reason=decode_queue, seq 24471 lag 567ms / seq 24488 lag 848ms, 각 IDR 1회로 0.3~0.6s 내 normal). seq 연결: 2차는 DXGI 복귀 첫 프레임 seq 24484(같은 ms 의 wire 라인, wireIntUs 809ms; capture stamp 가 입큐 전 송신 준비 시각보다 ~820ms 오래됨, 뷰어 c2eUs 812ms) 가 침묵 뒤 앵커로 잡힌 직후 24486~24488 이 신선한 stamp 로 밀집 도착한 것; 1차는 전환 첫 프레임(seq 24456) 이 아니라 2.3s 뒤 침묵 후 재개된 seq 24469(c2eUs 503ms) 가 앵커. 벽시계 역순(뷰어 59.178 < 호스트 59.610)은 요청 도착 두 쌍이 모두 +556ms 인 시계 오프셋으로 설명됨. 오래된 stamp 의 호스트 측 출처와 합성 프레임 경로와의 관계는 미확정 → "UAC 연관 혼잡 2회 · 원인 후보/미확정" 으로 신규 항목 P11. 사용자 체감 없음."""),
("""- 잔여: NACK 재전송 효과(손실 필요), 14시 릴레이 세션 RTT, 호스트 전수 capture→wire, P2P 성립 요인, UAC 좌표(진단 예산 재충전 계측 필요), 5분 장기, P10·P11 코드 수정.""",
 """- 잔여: NACK 재전송 효과(재전송 관측 필요), 절대 입력/종단 지연(미측정), 14시 릴레이 세션 RTT, 호스트 전수 capture→wire, P2P 성립 요인, UAC 좌표(진단 예산 재충전 계측 필요), 5분 장기, P10·P11 코드 수정.
- 정정(2026-09-07, 73445cb 에 대한 Codex 리뷰 반영, 문서만): avgLatencyUs 를 절대 지연처럼 쓰고 113ms 를 호스트 몫으로 귀속한 문구, decUs+d2pUs 를 "뷰어 총 ~10ms" 로 합산한 문구, "손실 0" 단정, UAC 혼잡을 전환 첫 프레임 stamp 로 확정한 문구를 위와 같이 고침(보고서 9.0/9.2/9.3/9.5, 구현계획 P2/P11)."""),
])
print('all edits ok')

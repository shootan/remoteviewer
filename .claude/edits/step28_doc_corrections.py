import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
H = ROOT + r'\docs\history.md'
P = ROOT + r'\docs\구현계획.md'
R = ROOT + r'\.claude\viewer_stabilization_followup_report.md'

# ---- P10 retry contract: initial + 2 retries at 2 s and 4 s (3 sends total), not "2/4/8 s x3" ----
edit(H, [
("""미도달/5xx/408/429→2·4·8s 백오프 최대 3회·60s 나이, 기타 4xx→즉시 폐기.""",
 """미도달/5xx/408/429→재시도(초기 송신 포함 총 3회: 2s·4s 뒤 재시도 2회)·60s 나이 상한, 기타 4xx→즉시 폐기."""),
])
edit(P, [
("""[x] 실패 배치 재시도 상한(3회·60s·보관 4개, 4xx 즉시 폐기)""",
 """[x] 실패 배치 재시도 상한(초기 포함 총 3회 송신: 2s·4s 재시도, 60s 나이, 보관 4개, 4xx 즉시 폐기)"""),
])

# ---- P11 #405: reproduction path, viewer wording ----
edit(H, [
("""**실 재현**: `remote60_host_encode_epoch_test` [2] — 이 PC 의 `mft_enum_hw` 를 제품과 같은 D3D11 NV12 surface 입력 경로(`set_d3d11_device`+`encode_frame_surface`)로 구동해 A→flush→B(forceKey) 에서 A 의 AU 가 flush 뒤 호출에 800,000us 오래된 stamp 로 나옴을 확인(CPU 버퍼 경로 `encode_frame` 에서는 같은 MFT 가 호출마다 자기 AU 를 돌려줘 재현되지 않음 — 보유는 이 backend 의 surface 경로에서 관측된 패턴이지 일반 계약이 아님).""",
 """**메커니즘 재현(다른 입력 경로)**: `remote60_host_encode_epoch_test` [2] — 이 PC 의 `mft_enum_hw` 를 D3D11 NV12 surface 입력 경로(`set_d3d11_device`+`encode_frame_surface`)로 구동하면 A→flush→B(forceKey) 에서 A 의 AU 가 flush 뒤 호출에 800,000us 오래된 stamp 로 나온다. **정정(Codex 검토)**: 현장 호스트(15:15:01/31 stats `nv12SurfaceFrames=0 nv12Converted=0`)는 BGRA 버퍼 경로(`encode_frame_bgra`)였고, 같은 MFT 의 CPU 버퍼 경로에서는 테스트에서 호출마다 자기 AU 를 돌려줘 재현되지 않았다(#407 에서 BGRA 경로 재시도 결과 병기) → **현장 호스트의 seq 24484 원인은 미확정**이며, 재현은 "다른 입력 경로에서의 메커니즘 재현" 이다. 보유는 backend·경로별 관측 패턴이지 일반 계약이 아님."""),
("""secure desktop 에서 kick 이 차단돼 실제 AU 가 0.5s 보유되는 15:15:57 형상(seq 24469)은 호스트 측 **미해결**(secure kick 허용은 Codex 지시로 이번 범위 제외, 별도 설계) — 뷰어 보정이 그 비용을 없앤다.""",
 """secure desktop 에서 kick 이 차단돼 실제 AU 가 0.5s 보유되는 15:15:57 형상(seq 24469)은 호스트 측 **미해결**(secure kick 허용은 Codex 지시로 이번 범위 제외, 별도 설계) — 뷰어 보정은 그로 인한 **오탐 IDR 비용을 줄일 뿐**, 오래된 픽셀의 실제 지연은 남는다."""),
("""S14(FakeHost 가 실 인코더 + 제품 `host_epoch_gate.hpp` 를 그대로 통과, `Flush()`=epoch+1·forceKey, 와이어 stamp=AU 자기 입력 stamp) flush 뒤 첫 와이어 AU 가 key·요청 0·Congested 0·decode 지속 — **한계**: FakeHost 는 CPU 입력 경로라 동기식이어서 S14 에서 gate 의 old 폐기는 0건(폐기 경로의 실 MFT 검증은 epoch test [2]); 기존 S1~S12 회귀 유지.""",
 """S14(FakeHost 가 실 인코더 + 제품 `host_epoch_gate.hpp` 를 그대로 통과, `Flush()`=epoch+1·forceKey, 와이어 stamp=AU 자기 입력 stamp) flush 뒤 첫 와이어 AU 가 key·요청 0·Congested 0·decode 지속 — **한계**: FakeHost 는 CPU 입력 경로라 대개 동기식이어서 S14 에서 gate 의 old 폐기는 round 별 0~1건(폐기 경로의 실 MFT 검증은 epoch test [2] surface 경로); FakeHost 는 sender 스레드가 없으므로 송신 경계는 #407 의 실 sender 테스트가 담당; 기존 S1~S12 회귀 유지."""),
("""- 계약(수명): (i) 일반 데스크톱: 실제 stamp=callback qpc, 이 backend surface 경로의 보유 = 다음 입력까지(kick 150ms 요청이 그 다음 입력), 뷰어 anchor 는 신선한 실제 stamp 만. (ii) flush(epoch+1): 새 epoch 첫 송출=IDR, 그 전 AU 폐기·카운트, generation 표기는 첫 IDR 부터. (iii) 복귀 flush 동일. 뷰어는 보유된 재개 프레임을 표시만 하고 anchor 는 신선 프레임에.""",
 """- 계약(수명): (i) 일반 데스크톱: 실제 stamp=callback qpc, 이 backend surface 경로에서 관측된 보유 = 다음 입력까지(kick 150ms 요청이 그 다음 입력; CPU 경로는 관측상 호출마다 반환), 뷰어 anchor 는 신선한 실제 stamp 만. (ii) flush(epoch+1): emit 단계 gate 는 새 epoch 의 첫 송출을 IDR 로 보장하되 **범위는 "flush 이후 새로 wire 에 시작되는 AU"** — 이미 sender 큐에 있던 old AU 는 #407 의 dequeue 펜스가 폐기하고, flush 시점에 chunk 송신이 시작된 AU 1개는 완료됨(in-flight 예외; 뷰어의 held-resume 규칙이 그 stamp 를 anchor 로 쓰지 않음). 미상(0)/미래 epoch AU 는 fail-closed 폐기. generation 표기는 첫 IDR 부터. (iii) 복귀 flush 동일. 뷰어는 보유된 재개 프레임을 표시만 하고 anchor 는 신선 프레임에."""),
])

# ---- #406 tally: per commit / per run ----
edit(H, [
("""테스트 전수 39종 실행(`.claude/test_sweep_0.2.101.txt`): 36 PASS. `remote60_viewer_udp_recovery_test` 는 S7 가 간헐 실패(5회 중 2회; 비동기 인코더에서 forceKey 호출이 직전 P 를 먼저 내놓아 하네스의 seq gap 이 IDR 이 아닌 P 앞에 놓임 → 뷰어의 정당한 reason=3 요청) → 하네스가 gap 을 다음 키프레임 앞에 적용하도록 수정한 뒤 3/3 PASS(14 시나리오). `remote60_gdi_capture_process_test`(3.7fps, BitBlt 258ms)·`remote60_udp_control_e2e_test`(udp hello ack 실패, 포트 43000) 는 이 PC 에서 설치된 GNLinkHost/GNLinkStream 이 실행 중(DXGI duplication·43000 점유)이라 **판정 제외** — 콘솔 Active·RDP 없음이었고 라이브 프로세스는 지시 없이 중지하지 않음; 두 테스트는 이번 변경 영역(GDI 워커·e2e 하네스)과 무관.""",
 """테스트 집계(정정, Codex 검토): 1607404 시점 바이너리로 전수 39종 실행(`.claude/test_sweep_0.2.101.txt`) = **36 PASS + 3 FAIL**(recovery S7 간헐, gdi_capture_process, udp_control_e2e). recovery 는 23b0f3c(하네스: gap 을 다음 키프레임 앞에 적용) 뒤 3/3 PASS(14 시나리오) → 그 시점 기준 **37 PASS + 2 미해소**. `remote60_gdi_capture_process_test`(3.7fps, BitBlt 258ms)·`remote60_udp_control_e2e_test`(udp hello ack 실패, 포트 43000) 는 실행 당시 이 PC 에서 설치된 GNLinkHost/GNLinkStream 이 실행 중이었고(DXGI duplication·포트 43000) 라이브 프로세스를 지시 없이 중지하지 않아 통제 재실행이 없다 → **환경 영향 추정, 미해소** 로 표기(인과 확정 아님). 두 테스트는 이번 변경 영역(GDI 워커·e2e 하네스)과 겹치지 않는다. S7 간헐 실패 원인: 비동기 인코더에서 forceKey 호출이 직전 P 를 먼저 내놓아 하네스의 seq gap 이 IDR 이 아닌 P 앞에 놓임 → 뷰어의 정당한 reason=3 요청."""),
])

# ---- plan P11 wording ----
edit(P, [
("""[x] 출처 구분: 인코더 FIFO 보유(비동기 MFT, surface 경로)가 원인 — `capture-switch first-frame … auEpoch/curEpoch/holdUs`, `epoch-gate` 로그로 구분 (history #405)""",
 """[x] 출처 구분: 인코더 FIFO 보유(비동기 MFT)를 surface 경로에서 재현, `capture-switch first-frame … auEpoch/curEpoch/holdUs`·`epoch-gate`·`sender dropped pre-flush AU` 로그로 구분 — **현장 호스트(BGRA 버퍼 경로)의 seq 24484 원인은 미확정** (history #405/#407)"""),
("""[x] 뷰어: hold>300ms 인 재개 프레임은 anchor 아님(pending 8프레임/500ms 상한, 진짜 backlog 유지)""",
 """[x] 뷰어: 유효 stamp(send≥capture)에서 hold>300ms 인 재개 프레임은 anchor 아님(pending 8프레임/500ms 상한·같은 episode 재개 프레임으로 연장 없음·키프레임 wait/decode/상태 전환 시 종료, 진짜 backlog 유지) — 오탐 IDR 비용 감소이며 오래된 픽셀의 실제 지연은 남음"""),
])

# ---- report file ----
edit(R, [
("""- 원인(실 재현): 비동기 HW MFT(surface 입력 경로)가 이전 입력의 AU 를 다음 호출에 내놓고 flush 가 인코더를 비우지 않아 복귀 첫 호출에서 전환 전 AU 가 오래된 stamp·real 로 송출(seq 24484) → 뷰어가 anchor 로 삼고 신선 burst 를 backlog 로 판정. `remote60_host_encode_epoch_test` [2] 가 이 PC 의 `mft_enum_hw` 에서 800ms 보유를 재현.""",
 """- 원인(메커니즘 재현, 현장 원인 미확정): 비동기 HW MFT 가 이전 입력의 AU 를 다음 호출에 내놓고 flush 가 인코더를 비우지 않으면 복귀 첫 호출에서 전환 전 AU 가 오래된 stamp·real 로 송출된다 → 뷰어가 anchor 로 삼고 신선 burst 를 backlog 로 판정. `remote60_host_encode_epoch_test` [2] 가 이 PC 의 `mft_enum_hw` **surface 입력 경로**에서 800ms 보유를 재현했으나, 현장 호스트는 BGRA 버퍼 경로(stats `nv12SurfaceFrames=0`)였고 그 경로에서는 재현되지 않아 seq 24484 의 현장 원인은 미확정."""),
("""- 한계: secure desktop 중 kick 차단으로 실제 AU 가 0.5s 보유되는 형상(seq 24469)의 호스트 측은 미해결(별도 설계) — 뷰어 보정이 비용을 없앰(S13 회귀). S14 의 FakeHost 는 CPU 입력 경로라 동기식이어서 gate 의 old 폐기는 0건(폐기 경로 실 MFT 검증은 epoch test [2]).""",
 """- 한계: secure desktop 중 kick 차단으로 실제 AU 가 0.5s 보유되는 형상(seq 24469)의 호스트 측은 미해결(별도 설계) — 뷰어 보정은 오탐 IDR 비용을 줄일 뿐 오래된 픽셀의 실제 지연은 남음(S13 회귀). S14 의 FakeHost 는 CPU 입력 경로라 대개 동기식이어서 gate 의 old 폐기는 round 별 0~1건(폐기 경로 실 MFT 검증은 epoch test [2] surface 경로); 송신 경계는 실 sender 테스트(#407)."""),
("""- 39종 중 36 PASS. `viewer_udp_recovery_test` 는 하네스 수정(23b0f3c) 뒤 3/3 PASS(14 시나리오).""",
 """- 1607404 시점 sweep: 39종 = 36 PASS + 3 FAIL. 23b0f3c 뒤 recovery 3/3 PASS → 37 PASS + 2 미해소(환경 영향 추정)."""),
])
print('doc corrections ok')

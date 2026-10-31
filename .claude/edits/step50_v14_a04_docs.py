import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
ROOT = r'D:\remote\remote'
SWEEP = open(ROOT + r'\.claude\edits\v14_sweep_placeholder.txt', encoding='utf-8').read().strip()

def rw(path, fn):
    raw = open(path, 'rb').read(); t = raw.decode('utf-8')
    nl = '\r\n' if '\r\n' in t else '\n'
    out = fn(t, nl).encode('utf-8')  # build first: an exception must not truncate the file
    open(path, 'wb').write(out); print('ok', path)

def history(t, nl):
    assert '### 417)' not in t
    L = [
"### 417) 2026-09-08 V14+A04 Windows 수신 recovery — 모든 경로가 지나는 시간 maintenance, 후속 없는 미완성 head 의 유계 포기(Codex 확정 규칙) (A2A task t-uhqkw7h9, 검증용 remote#865h2pvp 위임, Codex 계획 t-thjz1nw2 + 보완)",
"- 결함: (V14) `viewer_video_receiver.cpp` run_udp 의 시간 작업(hold drain·NACK 라운드·keyframe 복구 타이머)이 영상 청크 뒤와 recv 타임아웃 경로에서만 실행돼, 제어 응답·커서·malformed/ignored datagram 이 `continue` 로 빠지는 링크(제어 패킷이 25 ms 안에 계속 오면 recv 가 타임아웃되지 않음)에서는 타이머가 굶음. (A04/N3) Normal 상태에서 어셈블러 미완성 head 뒤에 완료 AU 가 없으면 PopDelivery 는 아무것도 하지 않고 NACK 스케줄러는 라운드 소진 뒤 조용히 멈춰, 정적 화면의 마지막 AU 청크 손실이 옛 그림으로 남음(secure desktop 중에는 후속 AU 보장 없음). 원본 코드 재현 `.claude/v14_prefix_recovery.log`: S16 타이머 재요청 0회, S17/S18 NACK 3라운드 뒤 keyframe 요청 +0·waitForKey 0.",
"- 수정 1 (V14, `viewer_video_receiver.cpp`): 루프 상단 공통 `maintenance(nowUs, force)` = `drain_deliveries` → `maybe_send_nack` → (N3 판정) → `fg.tick`; 모든 경로(video chunk·control 304/305/306·cursor·malformed/ignored/short·continue·recv 타임아웃)가 통과, elapsed(qpc) 5 ms rate-limit(제어 ACK 가 5 ms 마다 와도 5 ms 마다 실행), 타임아웃 경로는 force. 하단 중복 `maybe_send_nack`/`fg.tick` 제거(완료 AU 즉시 배달용 `drain_deliveries` 유지).",
"- 수정 2 (A04/N3, Codex 확정 규칙 — 후속 완료 AU 가 없는 미완성 head 에만, A03 의 hold 120 ms 정책 불변): 포기 = ① 스케줄러가 그 AU 의 마지막 적용 단계까지 소진(`spent_for(hasTail)`: tail 있으면 tail 3라운드, 없으면 hole 3라운드 — hole 소진 뒤 tail 단계가 남으면 미소진) ∧ ② now ≥ 마지막 실제 NACK 송신 + replyAllowance ∧ ③ now ≥ lastProgressUs + replyAllowance(진행 = 그 AU 의 새 data chunk 또는 FEC 복구 chunk; 중복 chunk·control·다른 AU 는 진행 아님) ∧ ④ now ≥ firstPacketUs + terminalMin. 별도 ⑤ now ≥ firstPacketUs + hardCap 이면 진행과 무관하게 포기(1 episode, 기존 리미터·backoff). replyAllowance = 제어 스레드의 최근 RTT(≤ 5 s 신선)가 있으면 max(50 ms, 2×RTT) 를 상한 1 s 로 clamp, 없으면 50 ms; terminalMin = tailGrace + maxRounds×round + replyAllowance(기본값 120+75+50 = 245 ms, 고정 아님); hardCap 기본 5 s(env `REMOTE60_NATIVE_VIDEO_GIVEUP_HARDCAP_MS`, 상한 env `REMOTE60_NATIVE_VIDEO_REPLY_ALLOWANCE_MAX_MS`, terminalMin 보다 작아지지 않게 정규화). NACK 미협상/비활성(스케줄러가 그 AU 를 쫓지 않음)이면 ③+④ 로 유계 폴백(legacy 즉시 배달 모드 포함). waitForKey 중 비-key head 는 ③ 만으로 포기하되 새 요청/reset 은 head 마다 반복하지 않음(기존 alreadyWaiting 억제). 포기 = `GiveUpIncomplete(gen, seq)` 로 판정에 쓴 동일 (gen,seq) 만 제거(완료됐으면 취소) → 스케줄러 Reset → 기존 `handle_udp_discontinuity()`(waitForKeyFrame + decoder reset + request_keyframe(2), 리미터·500 ms→2 s backoff). 로그 `stuck head given up …` 에 reason(hard-cap / nack-spent / no-progress / no-progress-nack-off / no-progress-during-key-wait)·ageUs·sinceProgressUs·hasTail·chased·rttUs·replyAllowanceUs·terminalMinUs·hardCapUs 명시.",
"- 공유 코드: `native_video_client_shared_core.hpp/.cpp` — Assembly·IncompleteAuInfo 에 `lastProgressUs`(생성·새 data chunk·FEC 복구 시 갱신, 중복은 미갱신), `AnyComplete()`, `GiveUpIncomplete(generation, seq)`(정확 identity, 완료 시 false). `udp_video_nack.hpp` — `current_generation()`·`spent_for(hasTail)`·`last_sent_us()`. `viewer_control_state.hpp`/`viewer_control_client.cpp` — pong 마다 `lastRttUs`·`lastRttAtUs` atomic(제어 스레드 write, recv read). `viewer_video_receiver.hpp` NackOptions 에 replyAllowanceMin/Max·rttStale·giveUpHardCap, `viewer_startup.cpp` env 연결. Android(hold 0, 자체 루프) 불변, wire 불변.",
"- 회귀(`viewer_udp_recovery_test.cpp`, 제품 run_udp 경로; `native_video_client_shared_core_test.cpp`): S16 keyframe wait + 5 ms 제어 노이즈(FakeHost `SendNoise`, `idle_with_noise`) → 타이머 재요청 2~4회 → 회복(S8 = 완전 무수신 타임아웃 경로 회귀). S17/S18 정적 화면 마지막 P/IDR 꼬리 손실 + NACK 무응답 + 후속 없음(`LossPlan::stopAfterDrop`/`ResumeAfterDrop`) → tail 3라운드 → nack-spent 포기(≈245 ms)·keyframe 요청(+1, 타이머 재요청 +1 허용) → IDR 로 회복(정확한 IDR seq 의 key publish). S19 꼬리 손실 + NACK 응답 200 ms 지연(`nackAnswerDelayMs`) + 제어 RTT 150 ms 주입(`SetControlRtt`) → replyAllowance 300 ms 로 늦은 응답 수락, 요청 0. S20 hole(idx 1)+tail 동시 손실 무응답 → 150 ms 시점 미포기(hole 소진만으로 안 끝남), tail 3라운드 뒤 폴백 1회(NACK 4~6). S21 NACK 미협상 정적 꼬리 손실 → no-progress+terminalMin 폴백·회복. S22 IDR 을 30 ms/청크(1.2 s) 페이싱·NACK 무응답 → 진행이 유지돼 포기 0·완성·표시. S23 hardCap 1 s(rig 튜너블) + 50 ms/청크 2 s IDR → cap 에서 1 episode 포기 → 새 IDR 로 회복. shared_core: lastProgressUs 는 새 chunk 에만 갱신(중복 무시), GiveUpIncomplete 는 정확한 seq 만 제거·미보유 false·완료 시 취소. S1~S15·S2(구 peer)·S5(재정렬) 유지. 수정 전(임시 규칙 빌드) `.claude/n3_prerule_recovery.log`: S19 202 ms 포기(응답 320 ms 놓침)·S20 101 ms 포기·S21 폴백 없음·S22 235 ms 포기 → 11 FAIL; 수정 후 단독 ×5 `.claude/n3_rule_recovery_run1..5.log` + shared_core `.claude/n3_rule_shared_core.log`: 아래 결과.",
"- " + SWEEP,
"- 결론 범위: 제어 채널 블록이나 경로 blackhole 을 해결했다는 주장 아님 — 제어만 오는 링크에서 복구 타이머가 굶지 않고, 후속 없는 정적 손실이 규칙대로 유계로 IDR 경로에 들어간다는 것까지. 검증용 기준선 `.claude/probe/probe_nack_trace.cpp` A04-1~4 대조는 검증용 몫.",
"- 변경 파일: `apps/native_poc/src/viewer_video_receiver.cpp`, `viewer_video_receiver.hpp`, `viewer_startup.cpp`, `viewer_control_state.hpp`, `viewer_control_client.cpp`, `native_video_client_shared_core.hpp`, `native_video_client_shared_core.cpp`, `udp_video_nack.hpp`, `viewer_udp_recovery_test.cpp`, `native_video_client_shared_core_test.cpp`, `docs/history.md`, `docs/구현계획.md`, `docs/stabilization_audit_2026-09-08.md`(A04 상태), `docs/작업목록.md`(1.5). 설치본·설치·push 없음(릴리스는 A02 뒤 한 번).",
"- 다음: A02+HN07+A06(encoder provenance, Codex 확정 세부) — 같은 task, fix(host) 커밋. A03(hold/tail 재설계)·A05 는 범위 밖.",
    ]
    return t.rstrip('\r\n') + nl + nl + nl.join(L) + nl

def plan(t, nl):
    a = "- [ ] A03/A04/A05 NACK tail/hold·무후속 소진 폴백·seq 기준 대상 선택"
    assert t.count(a) == 1
    t = t.replace(a, "- [~] A03/A04/A05 NACK tail/hold·무후속 소진 폴백·seq 기준 대상 선택 — A04 완료 (history #417, S17~S23); A03·A05 미착수")
    b = "- [ ] V07/V08/V14 렌더러 복구·수신 maintenance 공통화"
    assert t.count(b) == 1
    t = t.replace(b, "- [~] V07/V08/V14 렌더러 복구·수신 maintenance 공통화 — V14 완료 (history #417, S16); V07·V08 미착수")
    return t

def audit(t, nl):
    a = "- [ ] A03/A04/A05 NACK/hold/복구 episode 통합 설계·유계 회귀"
    assert t.count(a) == 1
    return t.replace(a, "- [~] A03/A04/A05 NACK/hold/복구 episode 통합 설계·유계 회귀 — A04(무후속 소진 폴백, Codex 확정 규칙) 완료 (history #417); A03·A05 미착수")

def tasks(t, nl):
    anchor = "1.4 [x] **A01 업로더 401 busy loop**"
    assert t.count(anchor) == 1
    line = "1.5 [x] **V14+A04 수신 recovery** — 루프 상단 공통 maintenance(5 ms rate-limit, 모든 경로) + 후속 없는 미완성 head 의 Codex 확정 포기 규칙(라운드 소진·replyAllowance·no-progress·terminalMin·hardCap)→IDR 경로, 회귀 S16~S23 수정 전 FAIL→후 PASS (history #417). A02 는 같은 task 진행 중"
    idx = t.index(anchor); end = t.index(nl, idx)
    return t[:end] + nl + line + t[end:]

rw(ROOT + r'\docs\history.md', history)
rw(ROOT + r'\docs\구현계획.md', plan)
rw(ROOT + r'\docs\stabilization_audit_2026-09-08.md', audit)
rw(ROOT + r'\docs\작업목록.md', tasks)
print('V14+A04 docs ok')

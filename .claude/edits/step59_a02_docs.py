import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
ROOT = r'D:\remote\remote'
SWEEP = open(ROOT + r'\.claude\edits\a02_sweep_placeholder.txt', encoding='utf-8').read().strip()

def rw(path, fn):
    raw = open(path, 'rb').read(); t = raw.decode('utf-8')
    nl = '\r\n' if '\r\n' in t else '\n'
    out = fn(t, nl).encode('utf-8')  # build first: an exception must not truncate the file
    open(path, 'wb').write(out); print('ok', path)

def history(t, nl):
    assert '### 418)' not in t
    L = [
"### 418) 2026-09-08 A02+A06+HN07 인코더 provenance — 입력 FIFO 통합, overflow 무효 래치와 재동기, unknown 출력에서만 체인 재닫힘 (A2A task t-uhqkw7h9, 검증용 remote#srb0qig4 위임, Codex 확정 세부)",
"- 결함: (A02/F1) `mf_h264_codec.cpp` 의 세 병렬 deque(타임스탬프·synthetic·epoch) 중 \"바이트 없는 출력\" 경로가 앞 둘만 pop 해 그 뒤 모든 AU 가 이전 입력의 epoch 를 달고 나옴 → flush 가 강제한 IDR 이 old 로 판정돼 drop-old-epoch, 뒤 P 는 drop-awaiting-key(감사 F1). (A06/C2) FIFO overflow 시 가장 오래된 항목을 버리는 trim 은 뒤 출력에 **더 새 입력의** provenance(=epoch)를 붙여, pre-flush 픽처가 current key 로 게이트를 열 수 있음. (HN07) 게이트가 열린 뒤 provenance 없는 출력이 나와도 체인이 유지돼 뷰어가 갖지 못한 참조를 쓰는 P 가 이어질 수 있음.",
"- 수정 1 (codec): 세 deque → `struct PendingInput{tsHns, synthetic, epoch}` + `class PendingInputFifo`(mf_h264_codec.hpp 공개, MFT 없이 검증 가능). push·빈 출력 pop·정상 pop·initialize/shutdown 이 한 레코드를 함께 움직여 lockstep 을 구조로 강제. overflow(64) 는 trim 대신 **provenanceInvalid 래치 + FIFO clear**: 이후 모든 AU 는 epoch 0(unknown)으로 나가고, 래치는 `initialize()` 성공에서만 해제(`shutdown()` 의 `Clear()` 는 큐만 비움 — 재초기화가 끝나야 MFT 가 쥔 옛 출력도 사라지므로). `provenance_invalid()` 접근자, `H264EncodeFrameStats.provenanceInvalid` 노출.",
"- 수정 2 (gate, `host_epoch_gate.hpp`): `EpochGate.provenanceInvalid` — 세워지면 열린 체인을 즉시 닫고, 겉보기 current key 도 수락하지 않으며 모든 출력을 DropUnknownEpoch 로 폐기(기존 bound → ResetEncoder 는 유지). HN07: 게이트가 열린 뒤 **unknown(0)/future** 폐기가 나면 awaitingKey 재진입(`reclosedByUnknown`) + 호출자 forceKeyNext, **DropOldEpoch 는 체인을 닫지 않음**(provenance 가 확실하므로 불필요한 IDR 방지). `epoch_gate_take_reset_budget()` 로 재빌드 예산(3회/10 s)을 request_reset 과 공유.",
"- 수정 3 (stage): `EncoderState::TryProvenanceResync(capture, nowUs)` = 예산 확보 → shutdown → initialize → 실패면 pending·래치 유지(카운터), 성공이면 래치 해제·타임라인 앵커/starvation 리셋·forceKeyNext·`epoch_gate_note_reset`. `host_stage_encode_send_h264.cpp` 는 **encode 반환 직후, AU 배치 루프 밖**(units 가 비어도)에서 `provenance_invalid()` 를 보고 게이트를 닫고 재동기를 시도하며, 성공 시 그 호출의 units 를 버림. 예산 소진·초기화 실패로 미룬 요청은 `host_stage_time_limit.cpp` 의 매 tick 에서 재시도(정적 화면이라 새 프레임이 없어도 진행, 성공 시 kick 재무장). `host_stage_encode_send_h264_au.cpp` 는 DropUnknownEpoch 에서 forceKeyNext 를 세우고, 유일한 enqueue 지점에 provenanceInvalid 명시 차단을 둔다(게이트의 epoch 0 규칙과 이중).",
"- 회귀 `host_encode_epoch_test` [4](결정론 — 제품 `PendingInputFifo`·제품 `epoch_gate_judge`·제품 `TryProvenanceResync` 를 직접 구동, 비동기 MFT 타이밍 불개입; 테스트 타깃에 capture 라이브러리 링크): a 빈 출력 1회 뒤 flush → 새 IDR AcceptKey(pre-A02 는 epoch 를 남겨 drop-old-epoch); b/c/f overflow → 래치·FIFO 비움·Pop 실패(=epoch 0)·게이트가 current key 까지 거부, `Clear()` 는 래치 유지·`Reset()` 이 해제; d 예산 3회/창·4번째 거부·다음 창 허용; e initialize 실패 → pending·래치·실패 카운터 유지; g 성공 → 래치 해제·forceKey·awaitingKey·새 epoch IDR 수락·P 재개; h unknown/future 는 열린 체인을 닫고 다음 P DropAwaitingKey→IDR 로 복구, known-old 는 체인 유지·다음 P Emit. 기존 [2b] 를 HN07 계약으로 갱신. 단독 ×3 PASS([1][2][2b][3][4]).",
"- " + SWEEP,
"- 한계: 실제 MFT 에서 FIFO overflow(출력 없이 입력 64개)를 강제할 수단이 없어 [4]는 제품 FIFO·게이트·재동기 객체를 직접 구동한다. 즉 \"현장에서 overflow 가 일어난다\"는 재현이 아니라 \"일어나면 이렇게 처리된다\"는 계약 회귀다. 실기 관측용으로 stats 의 `pendingOverflow`·새 로그(`encoder provenance invalid …`, `provenance resync ok/deferred`)를 남겼다.",
"- 변경 파일: `apps/native_poc/src/mf_h264_codec.hpp`, `mf_h264_codec.cpp`, `host_epoch_gate.hpp`, `host_encoder_manager.hpp`, `host_stage_encode_send_h264.cpp`, `host_stage_encode_send_h264_au.cpp`, `host_stage_time_limit.cpp`, `host_encode_epoch_test.cpp`, `apps/native_poc/CMakeLists.txt`, `docs/history.md`, `docs/구현계획.md`, `docs/stabilization_audit_2026-09-08.md`, `docs/full_code_audit_2026-09-08.md`, `docs/작업목록.md`. 설치·push·서버 배포 없음.",
"- 다음: 세 항목(A01 #416, V14+A04 #417, A02/A06/HN07 #418) 완료 → 검증용 검사 뒤 0.2.104 릴리스 빌드·설치본 1회.",
    ]
    return t.rstrip('\r\n') + nl + nl + nl.join(L) + nl

def plan(t, nl):
    a = "- [ ] A02/A06 인코더 빈 출력·overflow provenance 정렬/무효화·재동기"
    assert t.count(a) == 1
    return t.replace(a, "- [x] A02/A06 인코더 빈 출력·overflow provenance 정렬/무효화·재동기 (history #418, 회귀 [4])")

def audit(t, nl):
    a = "- [ ] A02/A06 입력 provenance 일원화·실패 경계 검증"
    if t.count(a) == 1:
        return t.replace(a, "- [x] A02/A06 입력 provenance 일원화·실패 경계 검증 (history #418, 회귀 [4])")
    return t

def full_audit(t, nl):
    a = "- [ ] HN02/HN04/HN07/V06 출력·참조 체인·입력 수명 결과형"
    assert t.count(a) == 1
    return t.replace(a, "- [~] HN02/HN04/HN07/V06 출력·참조 체인·입력 수명 결과형 — HN07(축소판: unknown/future 출력에서만 체인 재닫힘) 완료 (history #418); HN02·HN04·V06 미착수")

def tasks(t, nl):
    anchor = "1.5 [x] **V14+A04 수신 recovery**"
    assert t.count(anchor) == 1
    line = "1.6 [x] **A02+A06+HN07 인코더 provenance** — 입력 FIFO 한 레코드 통합(빈 출력이 epoch 까지 pop), overflow 무효 래치·재동기(예산 3회/10 s·tick 재시도), unknown/future 출력만 체인 재닫힘, 회귀 [4] (history #418)"
    idx = t.index(anchor); end = t.index(nl, idx)
    return t[:end] + nl + line + t[end:]

rw(ROOT + r'\docs\history.md', history)
rw(ROOT + r'\docs\구현계획.md', plan)
rw(ROOT + r'\docs\stabilization_audit_2026-09-08.md', audit)
rw(ROOT + r'\docs\full_code_audit_2026-09-08.md', full_audit)
rw(ROOT + r'\docs\작업목록.md', tasks)
print('A02 docs ok')

import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
ROOT = r'D:\remote\remote'
SWEEP = open(ROOT + r'\.claude\edits\cx_sweep_placeholder.txt', encoding='utf-8').read().strip()

def rw(path, fn):
    raw = open(path, 'rb').read(); t = raw.decode('utf-8')
    nl = '\r\n' if '\r\n' in t else '\n'
    out = fn(t, nl).encode('utf-8')  # build first: an exception must not truncate the file
    open(path, 'wb').write(out); print('ok', path)

def history(t, nl):
    assert '### 420)' not in t
    # (4) the GDI failure: cause unknown, do not claim it is unrelated to the binaries
    old = "`gdi_capture_process_test`(GDI_DELIVERED_FPS 3.66·복사 평균 258 ms 로 fps ≥ 50 요구 불충족 — 오늘 오전부터 이 PC 에서 간헐적으로 재현되며 뷰어/호스트 변경과 무관, 원인 미확정)"
    assert t.count(old) == 1
    t = t.replace(old, "`gdi_capture_process_test`(GDI_DELIVERED_FPS 3.66·복사 평균 258 ms 로 fps ≥ 50 요구 불충족 — 오늘 오전부터 이 PC 에서 간헐적으로 재현, **원인 미확정**)")
    L = [
"### 420) 2026-09-08 Codex 최종 검토 보완 3건 — provenance 신호 배선 누락, tombstone 은퇴 경계, 재동기 로그 스로틀 (검증용 remote#srb0qig4 전달)",
"- (1) A06 배선 누락(제품 결함): `host_stage_encode_send_h264.cpp` 는 `encode_frame_surface` 거부(:337)·`encode_frame_bgra` 실패(:348)에서 `provenance_invalid()` 검사(배치 밖 지점)보다 **먼저 반환**한다. MFT 가 입력을 수용한 뒤 drain/ProcessOutput 실패로 false 를 낼 수 있고, 그 호출에서 FIFO overflow 래치가 서면 `provenanceResyncPending` 이 세워지지 않아 — tick 재시도는 pending 만 보므로 — 이후 성공 encode 가 없으면 재동기 요청이 영구 유실된다(확정 사양 \"encode 반환 직후, 성공 여부·units.empty 무관\" 미충족). 수정: `EncoderState::NoteProvenance(bool)`(멱등, 최초 래치 시 true 반환)을 두고 **두 early-return 앞과 기존 지점 모두**에서 호출. HN04 partial-result 확장은 하지 않음.",
"- (2) tombstone 은퇴 경계(제품 결함): 5 s TTL·용량 16 만으로는 호스트가 그보다 오래 그 AU 를 계속 보내거나 그 사이 포기가 16회 더 나면 같은 (gen,seq) 가 재조립·재포기돼 \"identity 당 포기 1회\" 가 경계에서 깨진다. **시간 TTL 삭제**하고 은퇴를 의미 있는 경계로: (a) 같은 generation 에서 배달이 그 seq 를 지나감(그 뒤로는 기존 stale 가드가 덮으므로 tombstone 중복) (b) 다른 generation 의 배달(옛 seq 공간 소멸) (c) `Reset()` (d) 용량 16 초과 시 가장 오래된 것 폐기. 은퇴는 `DeliverAssembly` 에서 수행. (d) 는 \"배달 하나 없이 16개를 포기한 경우에만 생기는 유계 구멍\"이며 무한 증가 대신 택한 트레이드오프임을 헤더 주석에 명시. `IsAbandoned(gen,seq)` 시계 인자 제거, `AbandonedCount()` 추가.",
"- (3) 로그 스로틀: `TryProvenanceResync` 는 예산 소진 시 `provenanceResyncFailed` 를 올리지 않고 false 를 반환하므로 `failed % 30` 조건이 매 프레임 참이 됐다. **시간 기준 1 s**(`provenanceDeferLogUs`)로 교체.",
"- 회귀: shared_core tombstone 단위를 경계 규칙으로 재작성 — 배달 없이 10 s 뒤 늦은 청크도 여전히 Ignored·tombstone 유지, 배달이 지나가면 은퇴(그 뒤는 stale 가드), 다른 generation 배달로 옛 항목 은퇴, 용량 16 상한(가장 오래된 것부터)·`Reset()` 소거. epoch_test [4] 에 (i) 추가 — `NoteProvenance(false/true)` 멱등성, 래치 중 current key 도 거부, **예산 소진 시 pending·invalid 유지·`provenanceResyncFailed` 미증가**, 다음 창에서 재시도 성공. **한계**: (1) 의 early-return 경로 자체는 실행 회귀가 아니다 — `H264Encoder` 에 IMFTransform 주입 seam 이 없어 fake transform 으로 \"encode false + overflow\" 를 만들 수 없다(seam 추가는 범위 밖, 검증용에 판단 요청). 현재는 호출되는 헬퍼의 결정론적 회귀 + 호출 지점 코드 대조까지.",
"- 검증: recovery 단독 ×8(부하 없는 재실행 포함) PASS(포기 6/실행 = 시나리오당 1), shared_core PASS ×2, epoch_test 단독 ×3 PASS([1][2][2b][3][4]). " + SWEEP,
"- 변경 파일: `apps/native_poc/src/host_encoder_manager.hpp`, `host_stage_encode_send_h264.cpp`, `native_video_client_shared_core.hpp`, `native_video_client_shared_core.cpp`, `native_video_client_shared_core_test.cpp`, `host_encode_epoch_test.cpp`, `docs/history.md`(이 항목 + #419 표기 정정). 설치·push·서버 배포 없음.",
"- 다음: 검증용 재검사 → Codex 최종 확인 → 그 뒤에만 0.2.104 릴리스 빌드·설치본 1회.",
    ]
    return t.rstrip('\r\n') + nl + nl + nl.join(L) + nl

rw(ROOT + r'\docs\history.md', history)
print('Codex fix docs ok')

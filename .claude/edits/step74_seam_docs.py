import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
ROOT = r'D:\remote\remote'

def rw(path, fn):
    raw = open(path, 'rb').read(); t = raw.decode('utf-8')
    nl = '\r\n' if '\r\n' in t else '\n'
    out = fn(t, nl).encode('utf-8')  # build first: an exception must not truncate the file
    open(path, 'wb').write(out); print('ok', path)

def history(t, nl):
    assert '### 423)' not in t
    L = [
"### 423) 2026-09-08 A06 encode 경로 실행 회귀 — 동작 보존 추출 + fake transform seam (Codex 승인 조건 ①~⑤, 검증용 remote#srb0qig4 위임)",
"- 남아 있던 공백: #420 의 배선 수정은 옳았지만 **encode 실패 early-return 경로가 실행 회귀로 덮이지 않았다**(실 MFT 로는 \"출력 없이 입력 64개 수용 후 drain 실패\"를 만들 수 없음). Codex 가 이번 회귀에 한해 transform 주입 seam 을 승인했고, 조건은 제품 기본 동작 불변·상태 직접 세팅 금지·**실제 stage 호출 경로 실행**·한계 분리 보고·범위 확장 없음.",
"- ① 표기: 아래 추출은 **동작 보존 추출(behaviour-preserving extraction)** 이며 순수 함수가 아니다(인코더를 구동하고 EncoderState 를 바꾼다). 주석에 그대로 적었다.",
"- ② 추출(`host_encoder_manager.hpp`): `EncoderState::RunEncodeCall(encodeCall)` 이 **encode 호출과 그 직후 provenance 처리를 함께 소유**하고 `{ok, provenanceLatched}` 를 돌려준다. 얇은 래퍼 `EncodeSurfaceWithProvenance`·`EncodeBgraWithProvenance` 로 두 제품 경로를 노출. `host_stage_encode_send_h264.cpp` 의 surface 분기와 BGRA 분기가 각각 이 함수를 호출하고, 두 early-return 에서 provenance 호출은 **삭제**했다 — 처리가 이미 함수 안에서 끝나므로 어떤 반환 경로(false / 부분 출력 / units 0)도 우회할 수 없는 구조가 된다. 로그는 latch 된 호출에서만 1회.",
"- ③ seam(`mf_h264_codec.hpp/.cpp`): `H264Encoder::set_transform_for_test(IMFTransform*)`. 미설정이면 열거·백엔드 선택·설정 순서가 **완전히 동일**하고, 설정 시에만 그 transform 을 채택한다(ComPtr 로 소유, `shutdown()` 에서 해제; 호출자도 자기 참조를 유지). 외부 설정·UI·wire 확장 없음.",
"- 회귀 `host_encode_epoch_test` [5](결정론, 상태 직접 세팅 없음): 입력만 받고 출력을 내지 않는 fake transform 으로 **실제 FIFO overflow** 를 만든다(로그 `overflow after 65 accepted inputs`) → gate 차단·pending 확인. 이어 (a) drain 실패·units 0, (b) 부분 출력 뒤 drain 실패 두 변형에서 **encode 가 false 를 반환해도** 같은 호출 안에서 gate 가 닫히고 rebuild 가 pending 이 된다(`provenanceLatched`). 그 뒤 **무입력 tick** 경로: 공유 예산(3회/10 s) 소진 → pending 유지·`provenanceResyncFailed` 미증가, 다음 창에서 재초기화 실패 → failed=1·pending 유지, 그 다음 창에서 성공 → 래치 해제·forceKeyNext. 단독 ×3 PASS.",
"- ④ **커버리지 구분(중요)**: *실행 회귀로 덮인 것* = `RunEncodeCall` 이하의 순서(encode → provenance → 호출자의 실패 처리), 두 실패 변형, tick 재시도의 세 상태. *코드 대조로만 확인한 것* = stage 가 그 함수를 원래 지점에서 호출한다는 사실 — 테스트는 `HostContext`/`TickContext` 전체(캡처 readback·sender 스레드·mailbox·watchdog)를 조립하지 않으므로 `encode_send_h264` 자체는 실행하지 않는다. diff 로 surface·BGRA 두 분기가 각각 래퍼를 호출하고 그 뒤에 `return Flow::Continue` 만 남았음을 확인했다.",
"- ⑤ 추출 전후 동일성: `host_sender_epoch_test` PASS, `host_encode_epoch_test` PASS, 전체 Release 빌드 exit 0(오류 0), sweep `.claude/seam_sweep1.txt` **38 PASS + 2 FAIL** — `udp_control_e2e_test`(UDP 43000 을 라이브 GNLinkStream 이 점유하는 환경), `gdi_capture_process_test`(GDI_DELIVERED_FPS 3.6x, **원인 미확정**). 두 실패 모두 이번 변경과 같은 파일을 쓰지 않으며, PASS 로 상쇄하지 않고 그대로 기록한다.",
"- 변경 파일: `apps/native_poc/src/mf_h264_codec.hpp`, `mf_h264_codec.cpp`, `host_encoder_manager.hpp`, `host_stage_encode_send_h264.cpp`, `host_encode_epoch_test.cpp`, `docs/history.md`, `docs/구현계획.md`, `docs/작업목록.md`. 설치·push·서버 배포 없음.",
"- 상태: 이로써 A01·V14/A04(+tombstone·포화)·A02/A06/HN07 의 구현과 회귀가 모두 끝났다. 릴리스(0.2.104)는 검증용 검사와 Codex 최종 확인 뒤에만.",
    ]
    return t.rstrip('\r\n') + nl + nl + nl.join(L) + nl

def plan(t, nl):
    a = "- [x] A02/A06 인코더 빈 출력·overflow provenance 정렬/무효화·재동기 (history #418, 회귀 [4])"
    assert t.count(a) == 1
    return t.replace(a, "- [x] A02/A06 인코더 빈 출력·overflow provenance 정렬/무효화·재동기 (history #418·#420·#423, 회귀 [4][5])")

def tasks(t, nl):
    a = "1.6 [x] **A02+A06+HN07 인코더 provenance**"
    assert t.count(a) == 1
    idx = t.index(a); end = t.index(nl, idx)
    return t[:end] + " — encode 실패 경로까지 배선(#420) + 동작 보존 추출·fake transform 실행 회귀(#423)" + t[end:]

rw(ROOT + r'\docs\history.md', history)
rw(ROOT + r'\docs\구현계획.md', plan)
rw(ROOT + r'\docs\작업목록.md', tasks)
print('seam docs ok')

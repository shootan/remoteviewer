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
    assert '### 422)' not in t
    L = [
"### 422) 2026-09-08 A04 포화(saturation) episode — 기록을 잊지 않고 유계로 회복 (Codex 확정 설계, 검증용 remote#srb0qig4 위임)",
"- 배경: #421 이 남긴 미완 — 배달 없이 여러 identity 를 포기하면 tombstone 목록이 계속 늘어난다. 용량 초과 시 폐기(조용한 망각)와 시간 TTL 은 Codex 가 반려(잊으면 같은 identity 를 다시 수용 = 원래 결함 재발). 또 제가 대안으로 제시한 `abandonedHighWaterSeq`(prefix 차단)도 반려됐다 — **포기 대상은 `OldestIncomplete`(도착순 첫 미완성)이지 seq 최소가 아니므로**(seq 최소 선택은 `PopDelivery` 뿐) 재정렬 시 31 을 먼저 포기하면 뒤늦게 온 정상 30(수리 가능한 IDR 포함)까지 prefix 로 막힌다.",
"- 설계(확정): 용량은 **트리거**이지 축출 한도가 아니다. `abandoned_` 가 `kAbandonedSaturationCap`(16)에 닿으면 기록은 전부 보존한 채 `saturated_` 래치가 서고 단일 recovery episode 가 시작된다. episode 동안 — (1) 새 **비-키** 조립 금지(진입 시 남아 있던 비-키 조립은 폐기하되 기록 불요: 재생성이 막혀 있으므로), (2) 키 후보 슬롯 **K=2** 재사용(새 키가 오면 seq 최소 후보가 물러나고 floor 가 그 seq 로 상승 → 물러난 후보는 재생성 불가), (3) floor 는 episode 동안 단조·후보 0개여도 보존, (4) **live 조립 조회가 floor 보다 우선**(수리 중인 후보는 방해받지 않음), (5) 후보 생성·교체 **직전**(패킷 구조 검증 뒤) 주입 predicate 로 호출자의 generation 판정을 물어 **비Accept 면 후보·floor·배달 상태를 전혀 바꾸지 않음**.",
"- 해제: 완성 키 AU 가 `AdmitGeneration==Accept` + 프레임 게이트 `Decode` 를 통과한 **직후·decode 전** 수신기가 `NoteKeyAccepted(gen,seq)` 를 부르고, **그 episode 가 배달한 후보 identity 와 일치할 때만** 해제한다(디코드 성공 주장 아님 — 이후 실패는 기존 reason 4/5/7 복구 담당). 해제 시 그 워터마크가 덮는 기록만 회수하고, **더 새로운 기록이 여전히 cap 이면 래치 유지**. 시간 기반 종료·축출 없음, 초기화는 `Reset()`(새 세션 객체)뿐 — \"seq 차가 반주기면 리셋\" 규칙은 넣지 않았다(unsigned 차로는 재정렬과 재시작을 구별할 수 없다).",
"- 배선: shared_core 는 뷰어 상태에 의존하지 않도록 `SetSaturationAdmitFilter(std::function<bool(uint64_t)>)` 주입만 받고, 수신기가 제품 공용 판정 `SelectionGateState::AdmitGeneration(gen)==Accept` 를 그대로 넘긴다(복제 없음). Android 경로(`native_video_client_session.cpp`, hold 0)는 미설정 → 동작 불변. 해제 신호는 `VideoReceiver::activeAssembler_`(run_udp 수명 동안만 유효, 밖에서는 null)로 프레임 경로에서 전달. wire·ABI 변경 없음. **한계**: 후보 admission 은 그 시점의 선택 상태를 묻는 것이고 최종 수락은 기존 디코드 경로 게이트가 결정한다(그 사이 선택이 바뀌는 경쟁은 최종 게이트가 처리).",
"- 회귀(`native_video_client_shared_core_test.cpp`, 결정론): cap 도달 → episode 시작·기록 16개 전부 보존·비-키 조립 폐기, 새 P 거부·키는 후보, K=2 교체·floor 401 상승·물러난 후보 재생성 차단·live 후보는 늦은 중복에 방해받지 않음, 배달만으로는 미해제, 미등록 키·**다른 generation** 해제 0(후보·floor·기록 불변), 정상 키로 해제·P 재개, **해제해도 더 새로운 기록 16개면 래치 유지**(floor 는 해제), 거부 generation 은 상태 불변 후 허용 generation 은 교체 성공, old seq 99 vs last 100 → 초기화 0, 재정렬(31 먼저 포기·30 나중 도착) **30 미차단**.",
"- 검증: shared_core PASS ×2, recovery 단독 ×5 PASS(포기 6/실행 = 시나리오당 1), 전체 Release 빌드 exit 0(오류 0), sweep `.claude/sat_sweep1.txt` **39 PASS + 1 FAIL**(e2e: UDP 43000 을 라이브 GNLinkStream 이 점유하는 환경). RDP 세션 없음.",
"- 상태: #421 이 남긴 용량 분기 미완이 이 항목으로 닫힌다. 남은 릴리스 차단 요소는 **A06 encode early-return 경로의 실행 회귀 부재**(승인된 fake transform seam 으로 제작 예정).",
"- 변경 파일: `apps/native_poc/src/native_video_client_shared_core.hpp`, `native_video_client_shared_core.cpp`, `native_video_client_shared_core_test.cpp`, `viewer_video_receiver.hpp`, `viewer_video_receiver.cpp`, `viewer_video_receiver_frame.cpp`, `docs/history.md`, `docs/구현계획.md`, `docs/작업목록.md`.",
    ]
    return t.rstrip('\r\n') + nl + nl + nl.join(L) + nl

def plan(t, nl):
    a = "- [~] A03/A04/A05 NACK tail/hold·무후속 소진 폴백·seq 기준 대상 선택 — A04 본체 완료 (history #417·#419), **tombstone 용량 분기 미완·릴리스 차단**(history #421, 포화 설계 확정 대기); A03·A05 미착수"
    assert t.count(a) == 1
    return t.replace(a, "- [~] A03/A04/A05 NACK tail/hold·무후속 소진 폴백·seq 기준 대상 선택 — A04 완료 (history #417·#419·#421·#422: 포기 규칙·tombstone·포화 episode); A03·A05 미착수")

def tasks(t, nl):
    marker = "1.7 [~] **A04 후속(포기 identity tombstone)**"
    assert t.count(marker) == 1
    idx = t.index(marker); end = t.index(nl, idx)
    line = "1.7 [x] **A04 후속(포기 identity tombstone + 포화 episode)** — 포기한 AU 의 늦은 청크가 head 를 재차단하던 결함 수정, 용량은 축출이 아니라 recovery episode 트리거(키 후보 K=2·floor·generation 판정 주입·수락된 키로만 해제), 포기 횟수 직접 단정·S24 (history #419·#421·#422)"
    return t[:idx] + line + t[end:]

rw(ROOT + r'\docs\history.md', history)
rw(ROOT + r'\docs\구현계획.md', plan)
rw(ROOT + r'\docs\작업목록.md', tasks)
print('saturation docs ok')

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
    assert '### 425)' not in t
    L = [
"### 425) 2026-09-08 포화 episode 경계 결함 6건 — 무한 증가·오래된 키의 후보 축출·iterator UB·gen 한정 floor/회수·후보 목록 증가·진입 시 K 위반 (검증용 remote#srb0qig4 NEEDS_CHANGES, 구현 OK 철회 뒤)",
"- 배경: #422 는 설계 형태는 맞았으나 경계 동작 6건이 확정 조건과 달랐다. 검증용이 코드로 짚었고 전부 재현됐다 — 특히 (2) 는 **수정 전 실행이 Segmentation fault**(`.claude/bnd_prefix.log`, 종료 코드 139).",
"- (1) 포화 중 후보 실패가 기록을 늘림: `GiveUpIncomplete` 가 `saturated_` 와 무관하게 항상 기록해, 후보가 반복 실패하면 목록이 무한히 늘었다(수정 전 회귀: 20회 실패 뒤 16 → **36**). 확정 조건대로 **포화 중 포기는 기록하지 않고**, 키 후보였다면 floor 만 올린다(같은 episode 의 재시도이지 새 identity 가 아니다). 수신기의 terminal 경로도 같은 함수를 쓰므로 함께 적용된다.",
"- (2) 교체 규칙과 iterator UB: 후보가 둘일 때 incoming 이 **더 새로운지 보지 않고** 최소 seq 후보를 지워, 재정렬로 도착한 (floor 보다는 새롭지만 두 후보보다 오래된) 키가 수리 중인 후보를 밀어냈다. 또 `assemblyIt` 를 계산한 뒤 `assemblies_`(deque)에서 erase 하고 그 iterator 를 비교해 **UB**였다. 수정: incoming 이 교체 대상보다 새로울 때만 교체(아니면 Ignored, 후보·floor 불변), 포화 admission 을 `find_if` **앞**으로 옮겨 erase 뒤 iterator 를 새로 계산.",
"- (3)(4) generation 한정 제거: floor 검사·갱신과 회수(`NoteKeyAccepted`·배달 워터마크) 가 같은 generation 으로 제한돼 있었다. 호스트 seq 는 UDP 세션 전역에서 단조(`hdr.seq = ++encoder.encodedSeq`, 리셋은 TCP 재접속 경로뿐)이므로 **전역 seq 기준**으로 바꿨다. 구 generation 기록이 cap 을 유지시켜 새 generation 의 IDR 이 수락된 뒤에도 P 가 막히던 반례가 사라진다. `saturationFloorGen_` 삭제(잔존 0건).",
"- (5) `deliveredKeyCandidates_` 무한 증가: 최종 게이트가 계속 거부하는 완성 키가 반복되면 늘었다. **8개 링**으로 유계화하고, 밀려난 키가 나중에 수락돼도 해제되지 않는 **보수적 동작**(해제 자격만 제한, 기록은 잊지 않음)을 주석에 명시.",
"- (6) 진입 시 K 상한 미보장·floor 초기화 위반: 진입 시 비-키만 지우고 키 조립(최대 8)을 줄이지 않아 상한을 즉시 위반했고, `NoteKeyAccepted` 는 래치가 유지되는 경우에도 floor 를 지워 episode 단조성을 깼다. 수정: 진입 시 **최신 seq 기준 K=2 로 트림**(밀려난 seq 로 floor 상승), 해제 후 래치가 유지되면 **floor 도 유지**.",
"- 회귀(`native_video_client_shared_core_test.cpp`, 각 케이스가 제품 상태를 직접 단정; 한 케이스가 실패해도 나머지가 실행되도록 집계식으로 작성): (1) 포화 중 후보 20회 실패 → 기록 증가 0·episode 1, (2) 후보 200·202 에 198(둘보다 오래됨) → Ignored·후보/floor 불변 + 같은 상태에서 live 후보의 청크 처리 정상(UB 회귀) + 203 은 교체·floor 200, (3) 다른 generation 의 키가 floor 이하면 차단, (4) gen 1 기록 16개 + gen 2 IDR 수락 → 전부 회수·episode 종료·**P 재개**, (5) 수락되지 않는 완성 키 50회 → 후보 목록 ≤ 8, (6) 진입 시 키 조립 4개 → 2개로 트림·floor 801, (6b) 해제 후에도 cap 이면 래치·floor 유지. 수정 전 `.claude/bnd_prefix.log`(FAIL 3건 + segfault) → 수정 후 `.claude/bnd_postfix.log`·×3 PASS.",
"- 검증: shared_core ×3 PASS, recovery 단독 ×5 PASS(포기 6/실행 = 시나리오당 1), 전체 Release 빌드 exit 0(오류 0), sweep `.claude/bnd_sweep1.txt` **38 PASS + 2 FAIL** — `udp_control_e2e_test`(UDP 43000 라이브 점유 환경), `gdi_capture_process_test`(**원인 미확정**). 상쇄하지 않고 기록.",
"- 변경 파일: `apps/native_poc/src/native_video_client_shared_core.hpp`, `native_video_client_shared_core.cpp`, `native_video_client_shared_core_test.cpp`, `docs/history.md`. 설치·push·서버 배포 없음. 릴리스는 재검사·Codex 최종 확인 뒤에만.",
    ]
    return t.rstrip('\r\n') + nl + nl + nl.join(L) + nl

rw(ROOT + r'\docs\history.md', history)
print('bounds docs ok')

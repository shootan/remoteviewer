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
    assert '### 421)' not in t
    L = [
"### 421) 2026-09-08 tombstone 커밋 범위 교정 — 반려된 용량 망각·미확정 generation 은퇴 제거, 용량 분기는 미완(릴리스 차단) (검증용 remote#srb0qig4 전달, Codex 반려)",
"- 배경: #420 은 우편함 지연으로 Codex 의 반려가 닿기 전에 커밋됐다. Codex 는 (d) \"용량이 차면 가장 오래된 기록 폐기\"를 두 번 반려했다 — 기록을 잊으면 같은 identity 를 다시 수용하게 되고, 그것이 바로 이 tombstone 이 막으려던 결함이다(\"유계 구멍\"으로 문서화하는 것도 불가). generation 변경 시 은퇴도 미확정이다(어셈블러가 늦은 패킷의 generation 을 따라가면 안 되고, 현재 수신 경로에서 `assembler.Reset()` 호출은 0건이라 별도 알림 배선이 필요).",
"- 이 커밋: (a) 용량 `pop_front` 삭제 — 이제 아무 기록도 버리지 않는다. (b) 다른 generation 배달로 은퇴시키던 분기 삭제 — 은퇴는 **같은 generation 에서 배달이 그 seq 를 지나갈 때만**(그 뒤로는 기존 stale 가드가 덮는다)과 `Reset()` 뿐. (c) 헤더에 **\"UNFINISHED, RELEASE-BLOCKING\"** 으로 명시: 배달 없이 여러 identity 를 포기하면 목록이 그대로 늘어난다(항목 24 B, 포기 규칙상 병적인 경우), 이를 닫는 포화(saturation) 설계는 확정 대기.",
"- 회귀: 용량 케이스를 \"20개를 연속 포기해도 첫 identity 까지 전부 기억한다\"로 바꾸고(잊지 않음이 계약), 다른 generation 은퇴 케이스는 제거. shared_core PASS, recovery 단독 ×3 PASS(포기 6/실행 = 시나리오당 1).",
"- 상태: 용량 분기는 **미완**이므로 이 항목으로 완료 표시하지 않는다(구현계획·작업목록의 A04 관련 항목에 미완 표시). 0.2.104 릴리스는 이 분기가 확정·구현될 때까지 차단.",
"- 미실행 회귀(명시): A06 배선의 encode early-return 경로는 여전히 **실행 회귀로 덮이지 않았다** — fake IMFTransform 주입 seam 은 Codex 가 승인했고(이번 A06 회귀 한정), 그 seam 으로 stage 경로를 실행하는 방식은 검증용에 먼저 설명한 뒤 진행한다.",
"- 변경 파일: `apps/native_poc/src/native_video_client_shared_core.hpp`, `native_video_client_shared_core.cpp`, `native_video_client_shared_core_test.cpp`, `docs/history.md`, `docs/구현계획.md`, `docs/작업목록.md`.",
    ]
    return t.rstrip('\r\n') + nl + nl + nl.join(L) + nl

def plan(t, nl):
    a = "- [~] A03/A04/A05 NACK tail/hold·무후속 소진 폴백·seq 기준 대상 선택 — A04 완료 (history #417, S17~S23); A03·A05 미착수"
    assert t.count(a) == 1
    return t.replace(a, "- [~] A03/A04/A05 NACK tail/hold·무후속 소진 폴백·seq 기준 대상 선택 — A04 본체 완료 (history #417·#419), **tombstone 용량 분기 미완·릴리스 차단**(history #421, 포화 설계 확정 대기); A03·A05 미착수")

def tasks(t, nl):
    a = "1.7 [x] **A04 후속(포기 identity tombstone)**"
    assert t.count(a) == 1
    idx = t.index(a); end = t.index(nl, idx)
    line = t[idx:end].replace("1.7 [x]", "1.7 [~]") + " — **용량 분기 미완(릴리스 차단)**: 배달 없이 다수 포기 시 목록이 늘어남, 반려된 용량 망각 제거(history #421), 포화 설계 확정 대기"
    return t[:idx] + line + t[end:]

rw(ROOT + r'\docs\history.md', history)
rw(ROOT + r'\docs\구현계획.md', plan)
rw(ROOT + r'\docs\작업목록.md', tasks)
print('tombstone scope docs ok')

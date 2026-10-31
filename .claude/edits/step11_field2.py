import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

ROOT = r'D:\remote\remote'
REPORT = ROOT + r'\docs\field_test_2026-09-07_0.2.100.md'
PLAN = ROOT + r'\docs\구현계획.md'

# report: append section 9 after the appendix line
section = open(ROOT + r'\.claude\edits\field_1514_section.md', 'rb').read().decode('utf-8')
t = open(REPORT, 'rb').read().decode('utf-8')
anchor = '부속 자료(미커밋): `.claude/field-20260907/`(host_local_1413_1436.log, directory_journal.log, analyze_host.py 와 출력).'
assert anchor in t and '## 9. 15:14 추가 실기' not in t
t = t.replace(anchor, anchor + '\n' + section.rstrip('\n') + '\n\n부속 자료(미커밋, 9절): `.claude/field-20260907/viewer_1509_1519.log`, `host_local_1509_1519.log`, `client_journal_1509.log`, `analyze_viewer_1509.py`.')
open(REPORT, 'wb').write(t.encode('utf-8'))
print('report section 9 appended')

edit(PLAN, [
("""; 실기(09-07 14:15~14:33, 릴레이 경유): 호스트 도달 reason=2 요청 0건이나 뷰어 로그 미확보로 NACK 효과 미입증 (history #399)]**""",
 """; 실기(09-07 14:15~14:33, 릴레이 경유): 호스트 도달 reason=2 요청 0건이나 뷰어 로그 미확보로 NACK 효과 미입증 (history #399); 15:09~15:17 직결 세션 뷰어 로그: nackNegotiated=1 협상 확인, 청크 손실 0 이라 재전송 효과 미입증 (history #400)]**"""),
("""; 실기(09-07): 25분 세션 폭주 0, 재접속 시 reason=7 1~2회 (history #399)]**""",
 """; 실기(09-07): 25분 세션 폭주 0, 재접속 시 reason=7 1~2회 (history #399); 15:09~15:17 뷰어 로그: seq gap IDR 57건 전부 keyResync·reason=2 0건 실기 확인 (history #400)]**"""),
("""  - 완료조건: [ ] 캡처 (재)시작·기하 확정·WM_DISPLAYCHANGE 시 캡처 대상의 현재 물리 rect+원점(선택 모니터·창 모드 포함)으로 target rect 재갱신 + 로그 [ ] 실기: 단일 모니터 전체 데스크톱은 `target==virt`·`mapped==in`, 다중/선택 모니터는 해당 대상 rect 기준 `mapped` 일치 (rect=0 fallback 은 단일 모니터 한정 조건부)""",
 """  - 완료조건: [ ] 캡처 (재)시작·기하 확정·WM_DISPLAYCHANGE 시 캡처 대상의 현재 물리 rect+원점(선택 모니터·창 모드 포함)으로 target rect 재갱신 + 로그 [ ] 실기: 단일 모니터 전체 데스크톱은 `target==virt`·`mapped==in`, 다중/선택 모니터는 해당 대상 rect 기준 `mapped` 일치 (rect=0 fallback 은 단일 모니터 한정 조건부) [ ] secure_input `inject landed` 진단 예산(프로세스당 12건)을 에피소드/시간 기반으로 재충전 — 15:15 UAC 클릭은 예산 소진으로 기록 없음, 미해결 유지 (history #400)"""),
("""- [ ] **P10** 로그 업로드 401 — 재등록/재로그인·서버 재시작 뒤 업로드 영구 실패 — **2026-09-07 실기 (history #399, 보고서 5절)**""",
 """- [ ] **P11** 캡처 백엔드 전환(WGC↔DXGI) 직후 실제 프레임 capture stamp 지연 → 뷰어 가짜 decode_queue 혼잡 — **2026-09-07 15:15 실기 관측 (history #400, 보고서 9.3)**
  - 증상: UAC 전환 시 Congested 2회(lag 567/848ms), IDR 2회·burstDrops 8·present gap max 809ms. 호스트 `capture-switch first-frame` 의 `sendQpcUs−captureQpcUs` 126ms/820ms. 사용자 체감 없음.
  - 완료조건: [ ] 전환 첫 프레임 stamp 기준(WGC/DXGI 시계·flush) 원인 확인 [ ] 전환 직후 프레임이 혼잡 트리거를 내지 않도록 stamp 보정 또는 게이트 유예 [ ] 회귀 테스트
- [ ] **P10** 로그 업로드 401 — 재등록/재로그인·서버 재시작 뒤 업로드 영구 실패 — **2026-09-07 실기 (history #399, 보고서 5절)**"""),
("""  - 증상: 13:48:39 부터 host 업로드 401 연속(배치 폐기) → 실기 구간 NAS 로그 공백. 클라 Bearer 세션은 서버 재시작(in-memory)으로 소멸.""",
 """  - 증상: 13:48:39 부터 host 업로드 401 연속(배치 폐기) → 실기 구간 NAS 로그 공백. 클라 Bearer 세션은 서버 재시작(in-memory)으로 소멸. 15:09 클라 재로그인으로 뷰어 업로드는 재개됐으나 호스트 401 은 지속 — 코드 미수정 (history #400)."""),
])
append_history(ROOT + r'\docs\history.md', ROOT + r'\.claude\edits\history_400.md')

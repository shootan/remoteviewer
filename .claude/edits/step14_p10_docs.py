import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

ROOT = r'D:\remote\remote'

edit(ROOT + r'\docs\구현계획.md', [
("""- [ ] **P10** 로그 업로드 401 — 재등록/재로그인·서버 재시작 뒤 업로드 영구 실패 — **2026-09-07 실기 (history #399, 보고서 5절)**
  - 증상: 13:48:39 부터 host 업로드 401 연속(배치 폐기) → 실기 구간 NAS 로그 공백. 클라 Bearer 세션은 서버 재시작(in-memory)으로 소멸. 15:09 클라 재로그인으로 뷰어 업로드는 재개됐으나 호스트 401 은 지속 — 코드 미수정 (history #400).
  - 완료조건: [ ] 업로더 토큰 갱신(재등록 뒤) [ ] 401 시 재인증/x-host-token 폴백 [ ] 서버 세션 persist 또는 클라 자동 재로그인 [ ] 실패 배치 재시도""",
 """- [~] **P10** 로그 업로드 401 — 재등록/재로그인·서버 재시작 뒤 업로드 영구 실패 — **2026-09-07 실기 (history #399, 보고서 5절) → 코드 완료 (history #401), 설치본·서버 배포·실기 대기**
  - 증상: 13:48:39 부터 host 업로드 401 연속(배치 폐기) → 실기 구간 NAS 로그 공백. 클라 Bearer 세션은 서버 재시작(in-memory)으로 소멸. 15:09 클라 재로그인으로 뷰어 업로드는 재개됐으나 호스트 401 은 지속 (history #400).
  - 완료조건: [x] 업로더 토큰 갱신(재등록/재로그인 시 `log_upload_configure` 재포인팅, identity 변경 시 큐 폐기) [x] 401 시 일시정지+"sign in again" 표시+새 토큰으로 보관 배치 재전송 (클라는 비밀번호 미보관이라 자동 재로그인 없음; x-host-token 폴백은 호스트 전용 현행 유지) [x] 서버 세션 persist(해시+만료, 코드·테스트; **NAS 배포는 별도 지시**) [x] 실패 배치 재시도 상한(3회·60s·보관 4개, 4xx 즉시 폐기) [ ] 실기: 0.2.101 설치 후 재로그인/재등록 뒤 NAS host.log·viewer.log 연속 확인, `log_upload.diag` 에 `auth rejected`→`configured … resumes` 확인"""),
])

edit(ROOT + r'\docs\작업목록.md', [
("""1.2 [ ] **P10 로그 업로드 401** — 재등록 뒤 업로더 토큰 미갱신·서버 재시작 시 클라 세션 소멸로 NAS 로그 공백 → 토큰 갱신/재인증/세션 persist/배치 재시도 (history #399)""",
 """1.2 [~] **P10 로그 업로드 401** — 재등록 뒤 업로더 토큰 미갱신·서버 재시작 시 클라 세션 소멸로 NAS 로그 공백 → 업로더 재포인팅/401 일시정지/상한 재시도/세션 persist **코드 완료 (history #401)**, 0.2.101 설치본·서버 배포·실기 대기"""),
])

append_history(ROOT + r'\docs\history.md', ROOT + r'\.claude\edits\history_401.md')
print('P10 docs ok')

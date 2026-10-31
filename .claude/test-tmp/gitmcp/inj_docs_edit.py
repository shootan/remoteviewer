import io

ROOT = 'D:/remote/remote/.claude/worktrees/main-stability-integration/docs/'

hist_entry = """
### 2026-10-29 시험 안전 하네스 보수·독립 검증 완료 (안전 Finding 후속, 시험 코드 — 제품 출하 아님)
- 범위: 2026-10-28 안전 Finding(시험 Host 입력 주입 기본 ON·stream-start wake·전원 유지)의 **시험 하네스 보수**. 정본 `.claude/codex_test_injection_finding.md`, 시험 코드는 `spike/host-admission-r3`(ka-r3) d9a3a91 → ccaf745 (R2 통합 때 main 으로 온다).
- 제품 변경(같은 브랜치, Codex SOURCE OK): `--host-power-management on|off`(명령줄 전용, 기본 on, 첫 효과 전 bind) — off 면 실행 상태 요청·display wake·monitor-power broadcast 0. OS 입력 효과(SendInput·입력 PostMessage·SetCursorPos·IME·AttachThreadInput·UIA 쓰기·SetThreadExecutionState)를 `os_input_effects.hpp` 경계로 모음. 시험 전용 build `GNLinkStreamInputRecordTest` 는 기록만 하고 수행하지 않으며 출하 gate 가 그 marker 를 거부.
- 시험 Host 표준: `--input-injection-mode none --host-power-management off`, 해당 launch 의 두 OFF 줄 전 연결 0, 실행 중 재판정, 종료→EOF→최종 snapshot 판정, kill-on-close job, 저장소 test root 안 소유 fixture. 실제 입력이 주제인 4개 시험은 기본 REFUSED.
- 검증용 독립 실행(`.claude/verify_inj/VERIFY_RECORD_ba2eaf2.md`): 일반 8 + admission(product-client·real-directory) **9/9 ALL PASS**, 19 Host launch 전부 OFF 최종 판정 확인(r1, b8dcc93) · 기록 전용 e2e **12/0**, fatal **26/0**(r2, ccaf745 — r1 의 FAIL/INVALID 는 시험 판독·환경 결함으로 확인 후 보수). Codex 구성 단위 EVIDENCE OK(`codex_inj_general_evidence_r1.md`, `codex_inj_record_fatal_evidence_r2.md`).
- 경계 사고 기록(보수됨): 시험·출하 gate self-test 가 %TEMP% 에 만들고 지우던 경로(runtime test 9회·gate self-test 도구 호출 43회/8 세션, 실제 경로·삭제 결과 미확인) → 저장소 test root 의 새 run 폴더로 바꾸고 junction 은 들어가지 않음.
- 남은 것(이번 관문 밖): 실제 입력 4개 시험의 record-sink 전환, `viewer_prelogin_login_e2e` 재설계(사용자 재개 전 실행 금지 유지), 게시본 구 Host(--old-host) 실행 미검증, PS/LK/APK 의 안전 재확인과 PC·AVD 실기 EVIDENCE, R2 통합·출하.
- 제품/테스트/문서: 문서(이 항목·작업목록 한 줄). 제품·시험 코드는 브랜치에 있고 main 반영은 R2 통합에서.
"""

task_row = "| **INJ** | **시험 Host 입력·전원 안전 — 남은 것** (10-28 안전 Finding, 하네스 보수·독립 검증은 10-29 완료) | 실제 입력이 주제인 시험도 사용자 PC 를 건드리지 않고 자동 회귀 | 중 | ① 실제 입력 4개 시험(control_resume·viewer_mouse_xbutton·viewer_paste·viewer_release_all) — 기본 REFUSED, record-sink 로 전환(설계 `caplife/report_inj_draft.md`) ② `viewer_prelogin_login_e2e` 재설계 — **사용자 재개 전 실행 금지** ③ 게시본 구 Host(--old-host) 실기 미검증(순수 fixture 로만 보충) · 정본 `.claude/codex_test_injection_finding.md`, 기록 history 2026-10-29 |\n"

def edit(path, fn):
    raw = open(path, 'rb').read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    text = raw.decode('utf-8-sig')
    crlf = '\r\n' in text
    if crlf:
        text = text.replace('\r\n', '\n')
    text = fn(text)
    if crlf:
        text = text.replace('\n', '\r\n')
    data = text.encode('utf-8')
    if bom:
        data = b'\xef\xbb\xbf' + data
    open(path, 'wb').write(data)

def add_hist(t):
    assert '2026-10-29 시험 안전 하네스' not in t
    return t.rstrip('\n') + '\n' + hist_entry

def add_task(t):
    anchor = '| **DW** |'
    i = t.index(anchor)
    return t[:i] + task_row + t[i:]

edit(ROOT + 'history.md', add_hist)
edit(ROOT + '작업목록.md', add_task)
print('ok')

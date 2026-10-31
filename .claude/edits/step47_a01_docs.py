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
    assert '### 416)' not in t
    L = [
"### 416) 2026-09-08 A01 로그 업로더 401 일시정지 busy loop 수정 — 송신 가능할 때만 깨어나는 대기 조건, 시간 기준 idle-alive, workerCycles (A2A task t-uhqkw7h9, 검증용 remote#865h2pvp 위임, Codex 계획 t-thjz1nw2)",
"- 결함(감사 U3/프로브 Q1, 실측 재현): `log_upload.cpp` worker 의 `cv.wait_for` 조건이 `queuedBytes >= batchMaxBytes` 를 authRejected/credentials 와 무관하게 참으로 두어, 401 일시정지 중 큐가 192 KiB 를 넘으면 `next_job_locked` 가 false 를 내도 즉시 재진입 → 코어 점유 + 30 사이클마다 `idle alive` diag 기록. 이번 회귀 [10] 수정 전 실측(`.claude/a01_prefix_log_upload_test.log`): 2 s 동안 workerCycles +364,024, diag +1,001,105 B, 프로세스 CPU 1,984 ms.",
"- 수정: 대기 조건을 `stopping || wakeSeq != seen || (credentials && !authRejected && queuedBytes >= batchMaxBytes)` 로 — 보낼 수 없는 상태의 가득 찬 큐는 깨우지 않고 flushInterval 만큼 잔다(next_job false 인 사이클은 그대로 timed wait 로 복귀, 즉시 continue 없음). configure/clear 의 wakeSeq 와 stop 은 즉시 반영 유지. `idle alive` diag 는 사이클 수가 아니라 시간 기준(60 s 당 최대 1회). 불변: ownerEpoch·configGeneration 펜싱, 늦은 401/500 격리, 재시도 3회/2·4 s/60 s, 보관 4개, 큐 4 MB.",
"- `LogUploadStatus.workerCycles`(루프 반복 수, 내부 API·테스트 seam) 추가; stop 에서 0 으로.",
"- 회귀 `log_upload_test.cpp` [10]: 제품 기본값(batchMaxBytes 192 KiB, flush 2000 ms, 큐 4 MB)으로 401 pause → 99 B 줄 2,100개(≈205 KiB) 적재 → 2 s 창에서 workerCycles 증가 ≤ 5, diag 증가 ≤ 1 KiB, 프로세스 CPU < 500 ms, 요청 수 1 유지, 드롭 0 → 새 토큰 configure → 보관 배치(STALE→FRESH 헤더) 먼저, 이어 큐가 배치 단위로 배출(3번째 요청 본문 ≥ 100 KB), authRejected 해제·held 0. 수정 후(`.claude/a01_postfix_log_upload_test.log`): workerCycles +1, diag +0 B, CPU 0 ms, 전체 PASS([1]~[9] 유지). 단독 실행 3회 PASS.",
"- 변경 파일: `apps/native_poc/src/log_upload.hpp`, `log_upload.cpp`, `log_upload_test.cpp`, `docs/history.md`, `docs/구현계획.md`(A01 체크), `docs/stabilization_audit_2026-09-08.md`(A01 체크), `docs/작업목록.md`. 설치본·설치·push 없음(릴리스는 A02 뒤 한 번).",
"- 다음: V14+A04 수신 recovery(같은 task, 별도 커밋).",
    ]
    return t.rstrip('\r\n') + nl + nl + nl.join(L) + nl

def plan(t, nl):
    old = "- [ ] A01 401 이후 업로더 busy loop 수정 및 기본 배치 크기 회귀"
    assert t.count(old) == 1
    return t.replace(old, "- [x] A01 401 이후 업로더 busy loop 수정 및 기본 배치 크기 회귀 (history #416, 회귀 [10])")

def audit(t, nl):
    old = "- [ ] A01 업로더 busy loop 수정·기본 크기 회귀"
    assert t.count(old) == 1
    return t.replace(old, "- [x] A01 업로더 busy loop 수정·기본 크기 회귀 (history #416)")

def tasks(t, nl):
    anchor = "1.0.1 [~] **11:27 고화질 정지 패킷 캡처** — 1차 캡처 분석 완료·2차 캡처 방법 결정 대기 (history #415)"
    assert t.count(anchor) == 1
    return t.replace(anchor, anchor + nl + "1.4 [x] **A01 업로더 401 busy loop** — 송신 가능할 때만 깨는 대기 조건·시간 기준 idle-alive·workerCycles, 회귀 [10] 수정 전 FAIL→후 PASS (history #416). 설치본은 A02 뒤")

rw(ROOT + r'\docs\history.md', history)
rw(ROOT + r'\docs\구현계획.md', plan)
rw(ROOT + r'\docs\stabilization_audit_2026-09-08.md', audit)
rw(ROOT + r'\docs\작업목록.md', tasks)
print('A01 docs ok')

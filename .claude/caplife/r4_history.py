import json
p = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\docs\\history.md'
s = open(p, 'rb').read().decode('utf-8'); crlf = '\r\n' in s; s = s.replace('\r\n', '\n')
anchor = '- 제품/테스트/문서: 제품(재시작 owner·재구성 열림 규칙·GDI cookie·연속성 토큰·Serve sequence·상태 board, lib pointerfirst seam) / 테스트(UA rebuild-fails 재작성, PS pointer-first, owner 단위 확장, status board 단위 신규, attachment [D] 계약 반전·[E][F] 신규) / 문서(이 항목).\n'
assert s.count(anchor) == 1
entry = anchor + '''### 2026-10-26 capture-lifecycle r4 — full-frame WGC 재시작 실패의 retry 도 같은 episode 의 ask (fix/capture-lifecycle, 미게시 · R1 0.2.163 통합 대기)
- 근거: `codex_capture_lifecycle_src_r3.md`(6ffd94b: 나머지 SOURCE OK, FullFrame 실패 retry 한 곳만 잔여).
- 원인: r3 의 일반 retry(`host_stage_time_limit.cpp`)는 DesktopRebuild 실패만 제외해, full-frame WGC 재시작이 Detach 뒤 실패하면 8 s backoff 로 끝없이 다시 시작했다(fullFrameAsks 는 첫 1회만 차감, ask 는 restartPending 이면 조기 return).
- 이제 일반 retry 는 General 실패만 맡는다. FullFrame 자신의 실패는 그 episode 의 다음 ask 로 재시도한다(같은 3회·1 s 간격·source key, 재시작 wrapper 의 backoff 가 지난 뒤에만 — 보이지 않게 거절될 시도를 ask 로 세지 않음). 소진 뒤에는 일반 tick·새 kick·posted flag 어느 것으로도 추가 시작 0. 새 유효 source(세션·부착 경계 등)는 기존 N4 대로 새 budget.
- 시험 대역(시험 빌드만): `GNLINK_STREAM_TEST_WGC_STILL=1` — WGC attachment 마다 첫 프레임만 넘김(비활성 검사 뒤라 꺼진 동안의 프레임은 캐시를 지움).
- 시험: UA `--fullframe-fails`(신규) 18/18 — (C) 1회 실패 → 같은 episode 의 ask 2 가 재시작 ok → 그림, (A) 계속 실패: 재시작 3회(ask 1~3), 12 s 동안 추가 0·일반 retry 0·그림 0, (B) 새 세션 = 새 source → 새 budget 의 ask 1 로 그림 복귀. 부정 대조 M16(r3 규칙: 일반 retry 가 FullFrame 도 재시도) → report r4 절.
- 제품/테스트/문서: 제품(일반 retry 의 owner 조건, full-frame ask 의 실패 retry 결산, 재시작 조건) / 테스트(UA fullframe-fails, 시험 빌드 WGC still 대역) / 문서(이 항목).
'''
s = s.replace(anchor, entry)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

files = ["apps/native_poc/src/host_capture_lifecycle_ua_e2e_test.cpp", "apps/native_poc/src/host_capture_session.cpp",
         "apps/native_poc/src/host_capture_session.hpp", "apps/native_poc/src/host_stage_backend.cpp",
         "apps/native_poc/src/host_stage_pop_frame.cpp", "apps/native_poc/src/host_stage_time_limit.cpp", "docs/history.md"]
json.dump({"repo_path": "C:/work/gnlink-wt/fix-capture-lifecycle", "files": files}, open("D:\\remote\\remote\\.claude\\caplife\\r4_add.json", "w"))
msg = """fix(capture-lifecycle r4): a failed full-frame WGC restart is retried as that episode's next ask, never past its 3

Codex source recheck of 6ffd94b (codex_capture_lifecycle_src_r3.md): the one residual, N1-a FullFrame.
r3's generic retry (stage_time_limit) excluded only DesktopRebuild failures, so a full-frame WGC restart that
failed after its detach was restarted every 8 s without end (fullFrameAsks counted the first try only; the
ask returned early while a restart was pending).

Now the generic retry takes General failures only. A FullFrame failure is retried by the episode's next ask
-- the same 3 asks, 1 s interval and source key, and only once the restart wrapper's backoff has run (an
attempt it would refuse unseen is never counted). Spent: no further start from the generic tick, a kick or
the posted flag. A new valid source opens a new bounded budget as before (N4).

Test build only: GNLINK_STREAM_TEST_WGC_STILL=1 (each WGC attachment hands on its first frame only).
New UA --fullframe-fails: one failure then the same episode recovers; repeated failure stops at 3 restarts
(12 s: none more, no generic retry, no picture); a new session gets its picture back.

Product / test / docs: product (generic retry owner rule, full-frame ask retry accounting, restart
condition) / test (UA --fullframe-fails, WGC still seam) / docs (history).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""
json.dump({"repo_path": "C:/work/gnlink-wt/fix-capture-lifecycle", "message": msg}, open("D:\\remote\\remote\\.claude\\caplife\\r4_commit.json", "w"))
print('ok')

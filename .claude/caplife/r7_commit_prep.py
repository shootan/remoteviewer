import json
p = 'C:\\work\\gnlink-wt\\rel-0163\\docs\\history.md'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
s += '''- r7 보수(`codex_rel_0163_src_r6.md` R3 token 취득): 수신 루프가 프레임마다 **epoch 와 local 여부를 gate 잠금 안의 한 snapshot(`TakeFrameTicket`)으로 먼저** 받고, 그 epoch 로 decoder 초기화·처리, 같은 ticket 으로 최종 commit·reveal 까지 간다(두 번째 읽기 없음). ordinary ticket 의 그림도 그 뒤 시작된 local op 에 섞이지 않는다(CommitFrame 거부). 시험: gate 단위(ticket snapshot·취소된 A 거부·ordinary→local B 혼합 거부), PS E1 39/39 — (6) **ticket 직후** A 취소·B 시작 latch: 그 프레임 미기록(version 불변), B 는 자기 ticket·keyframe 으로 완료; UA E1 28/28; 부정 대조 M6(r6 모양: ticket 뒤 local 재읽기+ordinary 무검사) → (6) FAIL(version 83→85), M7(ordinary 혼합 검사 제거) → gate FAIL.
'''
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
R = 'C:/work/gnlink-wt/rel-0163'
d = 'D:\\remote\\remote\\.claude\\caplife\\'
json.dump({"repo_path": R, "files": ["apps/native_poc/src/viewer_selection_gate.hpp", "apps/native_poc/src/viewer_selection_gate.cpp",
                                     "apps/native_poc/src/viewer_video_receiver_frame.cpp", "docs/history.md"]}, open(d + 'r7p_add.json', 'w'))
json.dump({"repo_path": R, "message": """fix(viewer, rel-0163 r7): a frame takes its op and local mode in one snapshot, first, and keeps that ticket to its commit

Codex codex_rel_0163_src_r6.md, R3's token acquisition on 5ce3ce7. frameOp was the receive loop's last seen epoch and
the local mode a second, separate read (IsLiveLocal): a cancel / new op in between turned a local frame into an
ordinary one, which the final commit permitted unchecked.

Now SelectionGateState::TakeFrameTicket gives epoch + local mode in ONE snapshot under the gate's lock, taken before
the frame does anything else; the decoder is reset for that epoch, and the same ticket goes to CommitFrame and the
reveal. An ordinary frame's ticket is also not mixed into a local op begun after it (CommitFrame refuses it).

Product / test / docs: product (viewer selection gate, receive path) / test (none in this commit) / docs (history).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""}, open(d + 'r7p_commit.json', 'w'))
json.dump({"repo_path": R, "files": ["apps/native_poc/src/viewer_selection_gate_test.cpp", "apps/native_poc/src/viewer_prelogin_login_e2e_test.cpp"]},
          open(d + 'r7t_add.json', 'w'))
json.dump({"repo_path": R, "message": """test(rel-0163 r7): a latch at the frame ticket -- a cancel and new op right after a frame took A's ticket

viewer_selection_gate_test: the ticket is one snapshot; a cancelled A's ticket and an ordinary ticket taken before a
local B are both refused at the commit; B's own ticket is adopted. viewer_prelogin_login_e2e_test --render (6): A
cancelled and B begun right after a frame took A's ticket -- that frame is not written (FrameBuffer version
unchanged), B completes on its own ticket and keyframe (39/39). Mutants: M6 (the r6 shape: local mode re-read
after the ticket, ordinary frames unchecked) -> (6) FAIL, version 83 -> 85; M7 (no ordinary-mix check) -> gate FAIL.

Product / test / docs: product (none) / test (gate unit, PS E1) / docs (none).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""}, open(d + 'r7t_commit.json', 'w'))
print('ok')

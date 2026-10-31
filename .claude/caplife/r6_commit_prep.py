import json
p = 'C:\\work\\gnlink-wt\\rel-0163\\docs\\history.md'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
s += '''- r6 보수(`codex_rel_0163_src_r5.md` R3 잔여 2): ① `CommitFrame` 이 허가할 때 gate 잠금을 호출자에게 넘겨(FrameBuffer 잠금 → gate 잠금 순서) **FrameBuffer 쓰기가 끝날 때까지** 유지 — 허가와 쓰기 사이에 Begin/Clear 가 끼지 못한다. ② reveal 뒤 예외는 **실제로 reveal 이 commit 된 op**(`revealedOp`)의 그림에만 — 같은 epoch 라도 reveal 전에 취소된 op 는 거부. 시험: gate 단위(revealed R → 새 A 취소 시 A·R 거부 / 허가 잠금 동안 다른 스레드의 Clear·Begin 대기), PS E1 37/37 — (4) 최종 채택 직전 취소 → 거부·**version 불변**, (5) 허가와 쓰기 사이의 취소·새 op 시도가 **쓰기 끝까지 대기**(A 쓰기 먼저, 그 뒤 B 가 자기 keyframe 으로 완료); 부정 대조 M4(잠금 해제 뒤 쓰기) → (5) FAIL, M5(epoch·activeGen 만으로 예외) → gate FAIL.
'''
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
R = 'C:/work/gnlink-wt/rel-0163'
d = 'D:\\remote\\remote\\.claude\\caplife\\'
json.dump({"repo_path": R, "files": ["apps/native_poc/src/viewer_selection_gate.hpp", "apps/native_poc/src/viewer_selection_gate.cpp",
                                     "apps/native_poc/src/viewer_video_receiver_frame.cpp", "docs/history.md"]}, open(d + 'r6p_add.json', 'w'))
json.dump({"repo_path": R, "message": """fix(viewer, rel-0163 r6): a permitted picture is written under the gate's lock; only the op that was revealed keeps its stream

Codex codex_rel_0163_src_r5.md, R3's two residuals on 7d82c86.

1  SelectionGateState::CommitFrame hands its lock to the caller when it permits (std::unique_lock out param),
   kept until the FrameBuffer write is done (lock order: FrameBuffer, then the gate). A Begin/Clear -- which take
   only the gate's lock -- can no longer fall between "this op is live" and the picture being written.
2  After a reveal, a local op's later pictures go on only for the op whose reveal was committed (revealedOp, set
   in CommitReveal). An op of the same epoch cancelled before its own reveal is not that op: refused.

Product / test / docs: product (viewer selection gate, receive path) / test (none in this commit) / docs (history).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""}, open(d + 'r6p_commit.json', 'w'))
json.dump({"repo_path": R, "files": ["apps/native_poc/src/viewer_selection_gate_test.cpp", "apps/native_poc/src/viewer_prelogin_login_e2e_test.cpp"]},
          open(d + 'r6t_add.json', 'w'))
json.dump({"repo_path": R, "message": """test(rel-0163 r6): the permission-to-write order and a cancelled op after a reveal

viewer_selection_gate_test: an op revealed at gen 7, then a new local op cancelled before its reveal -- its picture
and the old op's are refused; while a permitted picture's lock is held, another thread's Clear/Begin waits.
viewer_prelogin_login_e2e_test --render: (4) now also asserts the FrameBuffer version is unchanged at A's refusal;
(5) a cancel and new op tried between A's permission and its write wait until the write is done (blocked, A's
write lands), then B completes on its own keyframe (37/37). Mutants: M4 (lock released before the write) -> (5)
FAIL; M5 (the exception by epoch and active generation) -> gate FAIL.

Product / test / docs: product (none) / test (gate unit, PS E1) / docs (none).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""}, open(d + 'r6t_commit.json', 'w'))
print('ok')

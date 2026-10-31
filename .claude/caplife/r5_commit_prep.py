import json
p = 'C:\\work\\gnlink-wt\\rel-0163\\docs\\history.md'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
s += '''- r5 보수(`codex_rel_0163_src_r4.md` 잔여 3): ① 보류된 선택은 **누른 순간의 control run** 을 처음에 잡아 그대로 기록하고, 즉시 재확인 실행도 그 run 이 아직 현재일 때만 — 역할 읽기와 보류 사이 재접속이 끼어도 옛 클릭이 새 연결에서 실행되지 않는다. ② 디코드된 그림의 **최종 채택**(FrameBuffer 잠금 안의 `SelectionGateState::CommitFrame`)에서 그 프레임의 op(그리고 프레임을 시작할 때 정한 local 여부 — 나중에 일반 프레임으로 다시 읽지 않음)가 아직 살아 있는 local op 인지 확인하고, 미결합이면 바로 거기서 결합한다(유효 출력 확인 뒤). 살아 있지 않으면 쓰지 않는다(localFramesRefused). reveal 은 그 채택이 대표한 generation 으로. ③ 호스트 WindowSelected 는 gate 의 한 경계(`ApplyHostAck`)에서 local 여부를 보고, local 이면 panel 도 적용하지 않는다. 시험: window proc 단독 55/55(재접속 latch·ack 경계 latch), gate 단위(CommitFrame·ApplyHostAck), PS E1 35/35(**최종 채택 직전 A 취소·B 시작 latch: A 그림 거부·version 불변, B 는 자기 keyframe 으로 완료**, 259/284 ms)·UA E1 28/28, 부정 대조 M1(R1-run)·M2(R4 panel)·M3(R3) 모두 FAIL.
'''
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
R = 'C:/work/gnlink-wt/rel-0163'
d = 'D:\\remote\\remote\\.claude\\caplife\\'
prod = ["apps/native_poc/src/viewer_selection_gate.hpp", "apps/native_poc/src/viewer_selection_gate.cpp",
        "apps/native_poc/src/viewer_video_receiver_frame.cpp", "apps/native_poc/src/viewer_picker.cpp", "docs/history.md"]
test = ["apps/native_poc/src/viewer_selection_gate_test.cpp", "apps/native_poc/src/viewer_window_proc_isolated_test.cpp",
        "apps/native_poc/src/viewer_prelogin_login_e2e_test.cpp"]
json.dump({"repo_path": R, "files": prod}, open(d + 'r5p_add.json', 'w'))
json.dump({"repo_path": R, "message": """fix(viewer, rel-0163 r5): the held pick keeps its own run; a local picture is adopted only at its final commit; a host ack and the panel are one boundary

Codex codex_rel_0163_src_r4.md, the three residuals on 501f706.

R1-run  begin_pc_target_selection takes the control run FIRST; a held pick records that run, and the immediate
        re-check runs it only while that run is still current -- a reconnect between the role read and the hold
        no longer turns the old click into the new connection's.
R3      A decoded picture's adoption is decided at ONE point: SelectionGateState::CommitFrame, inside the
        FrameBuffer lock right before the write. The frame carries the op token and whether it was taken for a
        local op (fixed when the token is taken -- never re-read as ordinary later); a local op still unbound is
        bound there, after the output is known valid; a picture of an op no longer live is not written
        (localFramesRefused). The reveal stands for the generation that adoption stood for.
R4      apply_window_selected_result goes through SelectionGateState::ApplyHostAck: the local check and the
        panel's and the gate's application are one boundary -- for a local op the panel is not applied either.

Product / test / docs: product (viewer selection gate, receive path, picker) / test (none in this commit) /
docs (history).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""}, open(d + 'r5p_commit.json', 'w'))
json.dump({"repo_path": R, "files": test}, open(d + 'r5t_add.json', 'w'))
json.dump({"repo_path": R, "message": """test(rel-0163 r5): latches at the three boundaries -- a reconnect between role read and hold, a cancel before the final commit, a local op before an ack

viewer_window_proc_isolated_test (55/55): a reconnect lands between the role read and the hold -- the old run's
pick is not run (no Begin, no stream-on), a pick on the new run runs once; an op turns local right before an old
ack's boundary -- gate and panel unchanged, an ordinary op's own ack still applies. viewer_selection_gate_test:
CommitFrame (no candidate -> no bind; an old op's picture after a new op -> refused; a revealed op goes on) and
ApplyHostAck (a local op never applies the panel). viewer_prelogin_login_e2e_test --render (4): A cancelled and B
begun right before A's first picture's FrameBuffer commit -- A refused, version unchanged; B completes on its own
keyframe. Mutants M1 (live run), M2 (panel before the boundary), M3 (no op check at commit) each FAIL.

Product / test / docs: product (none) / test (window proc isolated, gate unit, PS E1) / docs (none).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""}, open(d + 'r5t_commit.json', 'w'))
print('ok')

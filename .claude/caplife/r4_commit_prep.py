import json
p = 'C:\\work\\gnlink-wt\\rel-0163\\docs\\history.md'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
s += '''- r4 보수(`codex_rel_0163_src_r3.md` R1~R4): ① 역할 확인 전 클릭과 첫 pong 사이 — UI 는 보류를 기록한 **뒤** 역할을 다시 보고, control 은 역할을 정한 **뒤** 보류를 본다(seq_cst) → 둘 중 하나가 반드시 보고, 실행은 exchange 로 1회, 다른 연결(run)의 보류는 실행 안 함. ② 보내지 못한 WindowSelect 의 통지는 **요청에 실린 op tag**(selectionTag = 그 op 의 epoch)를 쓴다 — 옛 A 의 통지가 새 B 를 바꾸지 못한다. ③ selection gate 의 전이·판정을 한 mutex 로 묶고, 로컬 op 의 generation 은 **그 op 의 token(수신 루프가 그 프레임에 쓴 epoch)** 으로만, 그리고 frame gate 를 통과해 디코더로 간 keyframe 중 **출력이 나온 것**의 generation 으로만 결합한다(거절·디코드 실패 key 는 결합 안 함). reveal 후보와 FrameBuffer 채택도 그 프레임의 op 로 — 살아 있는 op 가 아니면 버림. ④ 로컬 op 가 진행 중일 때 온 호스트 WindowSelected 는(성공·실패·결합 전후 모두) panel·generation·pending·stream 을 바꾸지 않는다(SelectionAck::IgnoredLocal). 시험: gate 단위(ABA·옛 tag·늦은 ack 무간섭·일반 ack 대조), window proc 단독 51/51(pong 이 role 읽기와 보류 사이에 끼는 latch → 1회 실행·중복 0·옛 run 무효, 옛 op 통지 무간섭), PS E1 33/33(280/278 ms)·UA E1 28/28.
'''
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
R = 'C:/work/gnlink-wt/rel-0163'
d = 'D:\\remote\\remote\\.claude\\caplife\\'
prod = ["apps/native_poc/src/viewer_selection_gate.hpp", "apps/native_poc/src/viewer_selection_gate.cpp",
        "apps/native_poc/src/viewer_decoder_state.hpp", "apps/native_poc/src/viewer_video_receiver_frame.cpp",
        "apps/native_poc/src/viewer_picker.cpp", "apps/native_poc/src/viewer_picker.hpp",
        "apps/native_poc/src/viewer_control_client.cpp", "docs/history.md"]
test = ["apps/native_poc/src/viewer_selection_gate_test.cpp", "apps/native_poc/src/viewer_window_proc_isolated_test.cpp"]
json.dump({"repo_path": R, "files": prod}, open(d + 'r4p_add.json', 'w'))
json.dump({"repo_path": R, "message": """fix(viewer, rel-0163 r4): the local pick's wake, op tag, generation bind and late ack each belong to their own op

Codex codex_rel_0163_src_r3.md R1-R4 on 5729880.

R1  A pick held before the host's role is known: the UI holds it and THEN re-reads the role; the control thread
    sets the role and THEN looks for a held pick (both sequentially consistent) -- at least one sees the other,
    so the pick is never lost; run_deferred_pc_selection takes it once (exchange), and only for the control run
    it was held on (deferredRun).
R2  A WindowSelect the control thread could not send is resolved with the op tag the request carried
    (selectionTag = the epoch Begin gave that op), never the live epoch read at post time: an older op's notice
    cannot convert or clear a newer one.
R3  The selection gate's transitions and deciding reads take one mutex. A local op's generation is bound only
    with that op's token (the epoch the receive loop took for the frame), and only from a keyframe the frame gate
    sent to the decoder whose decode then produced output (a rejected or failed key never binds). The reveal
    candidate and the FrameBuffer adoption use the frame's op; a picture of an op that is no longer live is dropped.
R4  While a local op is in flight a host WindowSelected -- success or failure, before or after the bind --
    changes neither the panel nor the generation, pending or stream (SelectionAck::IgnoredLocal).

Product / test / docs: product (viewer selection gate, decoder state, receive path, picker, control client) /
test (none in this commit) / docs (history).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""}, open(d + 'r4p_commit.json', 'w'))
json.dump({"repo_path": R, "files": test}, open(d + 'r4t_add.json', 'w'))
json.dump({"repo_path": R, "message": """test(rel-0163 r4): the local pick's counterexamples -- the pong between read and hold, an old op's notice, ABA, a late ack

viewer_window_proc_isolated_test: the first pong lands between the UI's role read and its hold (a test hook) --
the pick runs once with no second click, a duplicate wake runs nothing, an older connection's held pick runs
nothing; an old op's unsendable notices (desktop and window) leave a newer local op untouched, the current op's
own notice converts it (desktop) or refuses it (window). viewer_selection_gate_test: an op token binds only its
own op (an old frame after cancel -> new op binds nothing, its candidate never reveals it), a late ack -- success
or failure, before or after the bind -- is IgnoredLocal and the op still completes on its own frame (corrects
9b836e2's expectation), an ordinary op's own ack still works.

Product / test / docs: product (none) / test (gate unit, window proc isolated) / docs (none).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""}, open(d + 'r4t_commit.json', 'w'))
print('ok')

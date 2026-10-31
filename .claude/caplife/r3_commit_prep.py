import json
p = 'C:\\work\\gnlink-wt\\rel-0163\\docs\\history.md'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
if not s.endswith('\n'):
    s += '\n'
s += '''### 2026-10-26 PC 0.2.163 r3 — 로그인 전 화면에서 picker 의 바탕화면 선택이 끝나지 않던 결함 (rel/0.2.163, 미게시)
- 근거: rel-0163 r2 E1(실제 제품 renderer 시험)에서 재현 3/3 + 1(`report_rel_0163_r2_e1_blocker.md`), 결정 `codex_rel_0163_e1_decision.md`.
- 원인: 셸은 뷰어를 항상 picker 로 띄운다. 로그인 전 호스트(kCaptureFlagPreloginV1)에는 뷰어가 WindowList/WindowSelect 를 보내지 않는데(allowlist), picker 의 선택은 그 select 의 ack 를 기다리는 동안 모든 프레임을 버렸다 → picker 가 닫히지 않고 영상 0. 10-22 실기의 정황과 일치(유일 원인으로 확정하지 않음).
- 수정(뷰어): 역할이 알려진 로그인 전 호스트 + 바탕화면(0) 선택만 **로컬 선택 transaction**(selection gate 의 localAckless) — 같은 pending·op epoch(decoder 초기화·keyframe 대기), ack 대신 **그 op 뒤 처음 받아들인 keyframe 의 stream generation 을 결합**, 그 뒤 다른 generation 은 ack 뒤와 같이 버림, reveal 은 UI 에서 재검증, 실제 present 뒤 restore 보고(기존 그대로). 가짜 ack·allowlist 확대·MonitorSelect 대용 없음. 역할을 알기 전(첫 pong 전) 누른 선택은 이 연결의 의도로 보류했다가 역할이 알려지면 한 번 실행. 보내지 못한 WindowSelect 는 조용히 버리지 않고 그 op 를 로컬로 잇거나(바탕화면) 명시적으로 거부(창). 로그인 전 화면에서 창 선택은 거부. UAC·LockCred 만으로는 이 경로에 들어가지 않는다(PreloginV1 일 때만).
- 시험: PS `--still-screen --render`(제품 WndProc·실제 present·화면 밖 창·WGC 창 캡처) 33/33 — 역할 확인 전 클릭 보류→1회 실행, 창 선택 거부, picker 10 s → 바탕화면 **275 ms**·다시 10 s → **279 ms**, 이후 3 s 에 5~6 present. 수정 전 같은 시험 4/4 FAIL(present 0). viewer_selection_gate_test 에 로컬 transaction 경우(bind 전 reveal 없음·gen 불일치·옛 op·cancel·전환·늦은 ack) 추가.
- 공용 세션(APK): `native_video_client_session.cpp` 도 로그인 전 호스트에 WindowSelect 를 보내지 않고 WindowSelected 를 기다린다 — 같은 종류(확인만, 수정은 APK 0.2.32 단계).
- 제품/테스트/문서: 제품(viewer selection gate·수신 경로 결합·picker·control client·window proc) / 테스트(PS E1 확장, gate 단위) / 문서(이 항목).
'''
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

R = 'C:/work/gnlink-wt/rel-0163'
prod = ["apps/native_poc/src/viewer_selection_gate.hpp", "apps/native_poc/src/viewer_selection_gate.cpp",
        "apps/native_poc/src/viewer_video_receiver_frame.cpp", "apps/native_poc/src/viewer_picker.cpp",
        "apps/native_poc/src/viewer_picker.hpp", "apps/native_poc/src/viewer_constants.hpp",
        "apps/native_poc/src/viewer_window_proc.cpp", "apps/native_poc/src/viewer_control_client.cpp", "docs/history.md"]
test = ["apps/native_poc/src/viewer_selection_gate_test.cpp", "apps/native_poc/src/viewer_prelogin_login_e2e_test.cpp"]
d = 'D:\\remote\\remote\\.claude\\caplife\\'
json.dump({"repo_path": R, "files": prod}, open(d + 'r3p_add.json', 'w'))
json.dump({"repo_path": R, "message": """fix(viewer, rel-0163 r3): a desktop pick at a logon-screen host completes -- a local selection, bound to its first keyframe

Codex codex_rel_0163_e1_decision.md. The shell opens the viewer on its picker; a host known to be at the logon
screen (kCaptureFlagPreloginV1) is sent no WindowList/WindowSelect (its allowlist), yet the pick waited for that
select's ack and dropped every frame meanwhile (DropAwaitingAck): the picker never closed, nothing was shown
(rel-0163 r2 E1, product renderer, 4/4).

Now, for that host and the desktop (0) only, the pick is a LOCAL selection transaction (SelectionGateState::
localAckless): the same pending op and epoch (decoder reset, wait for a keyframe), no ack -- its stream
generation is the one of the first keyframe admitted after it began (BindLocalGeneration, once); after that other
generations drop as after an ack; the reveal is revalidated on the UI thread; restore is reported on the real
present as before. No fake ack, no allowlist change, no MonitorSelect stand-in. A pick pressed before the host's
role is known (its first pong) is held as this connection's intent and run once when it is (kMsgRunDeferredPick);
a WindowSelect the control thread could not send is resolved, never dropped silently (kMsgUnsendableSelect: the
same op continues as local for the desktop, a window is refused). A window pick at the logon screen is refused.
A secure desktop (UAC) or LockCred alone never takes this path.

Product / test / docs: product (viewer selection gate, receive-path bind, picker, control client, window proc) /
test (none in this commit) / docs (history).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""}, open(d + 'r3p_commit.json', 'w'))
json.dump({"repo_path": R, "files": test}, open(d + 'r3t_add.json', 'w'))
json.dump({"repo_path": R, "message": """test(rel-0163 r3): the logon-screen desktop pick through the product renderer, and the local selection's gate cases

viewer_prelogin_login_e2e_test --still-screen --render: the first pong held 4 s and the desktop card pressed
before it (held, then run once as a local selection), a window pick refused at the logon screen, then picker
10 s -> the desktop card -> presented within 1 s, twice (the idle reattach each time), the still screen presented
>= 4 times in the next 3 s; screenshots of the viewer window (off every monitor, Windows.Graphics.Capture).
viewer_selection_gate_test: a local op reveals only after its keyframe binds a generation; another generation, an
older op, a cancel or a late ack do not complete it; an unsendable acked op converts once, that op only.

Product / test / docs: product (none) / test (PS E1, gate unit) / docs (none).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""}, open(d + 'r3t_commit.json', 'w'))
print('ok')

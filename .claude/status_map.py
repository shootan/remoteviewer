# -*- coding: utf-8 -*-
"""The picker's status tokens reach the screen. Give each one a sentence.

`SetStatus` carries developer tokens -- `control_disconnected`, `session_lost`,
`control_connect_failed`, `waiting_first_frame`, `window_list_request pending` -- and the picker
used to draw them verbatim, which is how `window_list_received count=3` ended up on a user's
screen. Splitting `displayStatus` off stopped the tokens appearing, but nothing filled the gap:
a host that refused the list now showed the derived "no shareable windows" line instead, which
is not missing information but wrong information.

The mapping lives where the drawing happens, because that is the only target compiled with
/utf-8 -- the shared core is built into two dozen targets that are not.
"""
import io

p = 'apps/native_poc/src/viewer_overlay_draw.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  // panelDisplayStatus, not panelStatus: the latter is the token the code matches on
  // (`window_list_received count=3`), and it used to be drawn here verbatim.
  // The list line is written here rather than in the shared core: that file is compiled into two
  // dozen targets without /utf-8, where a Korean literal does not survive the lexer.
  std::string listedLine;
  if (ctx.control.connected.load(std::memory_order_relaxed)) {
    listedLine = windowItems.empty()
                     ? std::string("공유할 수 있는 창이 없습니다. 전체 화면을 선택하세요.")
                     : "공유할 수 있는 창 " + std::to_string(windowItems.size()) + "개";
  }
  std::string statusLine = selectionLocked
                               ? std::string("호스트 설정으로 대상이 고정돼 있습니다")
                               : (panelDisplayStatus.empty() ? listedLine : panelDisplayStatus);'''

new = '''  // panelDisplayStatus, not panelStatus: the latter is the token the code matches on
  // (`window_list_received count=3`), and it used to be drawn here verbatim.
  // The list line is written here rather than in the shared core: that file is compiled into two
  // dozen targets without /utf-8, where a Korean literal does not survive the lexer.
  std::string listedLine;
  if (ctx.control.connected.load(std::memory_order_relaxed)) {
    listedLine = windowItems.empty()
                     ? std::string("공유할 수 있는 창이 없습니다. 전체 화면을 선택하세요.")
                     : "공유할 수 있는 창 " + std::to_string(windowItems.size()) + "개";
  }

  // The tokens that mean something went wrong, said in Korean.
  //
  // Without this the three failure states -- the control channel dropping, the session being
  // lost, the connect failing -- showed the ordinary list line instead, which told the user the
  // picker was fine when it was not. Unknown tokens fall through to the list line rather than
  // being printed, because a developer string on screen is what this whole split was about.
  std::string tokenLine;
  if (panelStatus == "control_disconnected") {
    tokenLine = "호스트와의 연결이 끊겼습니다.";
  } else if (panelStatus == "session_lost") {
    tokenLine = "세션이 끊겼습니다. 다시 연결해 주세요.";
  } else if (panelStatus == "control_connect_failed") {
    tokenLine = "호스트에 연결하지 못했습니다.";
  } else if (panelStatus == "waiting_control" || panelStatus == "window_list_request pending") {
    tokenLine = "화면 목록을 불러오는 중…";
  } else if (panelStatus == "waiting_first_frame") {
    tokenLine = "첫 화면을 기다리는 중…";
  }

  std::string statusLine = selectionLocked
                               ? std::string("호스트 설정으로 대상이 고정돼 있습니다")
                               : (!panelDisplayStatus.empty()
                                      ? panelDisplayStatus
                                      : (!tokenLine.empty() ? tokenLine : listedLine));'''

assert d.count(old) == 1, 'status anchor'
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(old, new, 1))
print('ok')

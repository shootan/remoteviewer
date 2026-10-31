# -*- coding: utf-8 -*-
"""Separates the sentence the user reads from the token the code matches on.

`status` is two things at once: `should_append_selected_title()` matches it with rfind, and the
picker draws it on screen. That is why `window_list_received count=3` was a user-facing line. The
token stays exactly as it is; a second field carries the sentence.
"""
import io

# ---------------------------------------------------------------- the state field
p = 'apps/native_poc/src/native_video_client_shared_core.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  std::string status = "waiting_control";'''
new = '''  /**
   * The machine-readable token. Matched by prefix (`should_append_selected_title`) and written to
   * the log, so its spelling is a contract -- do not translate it.
   */
  std::string status = "waiting_control";
  /**
   * The same moment, said to the user.
   *
   * Kept apart because one field could not be both: `window_list_received count=3` was appearing
   * on screen as the picker's subheading, which is a log line wearing a user's clothes. Empty
   * means the drawing code falls back to whatever it showed before.
   */
  std::string displayStatus;'''
assert d.count(old) == 1, 'state field'
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(old, new, 1))
print('hpp ok')

# ---------------------------------------------------------------- set it where the token is set
p = 'apps/native_poc/src/native_video_client_shared_core.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''  state_.status = std::string("window_list_received count=") + std::to_string(count);'''
new = '''  state_.status = std::string("window_list_received count=") + std::to_string(count);
  state_.displayStatus = count > 0
                             ? "공유할 수 있는 창 " + std::to_string(count) + "개"
                             : std::string("공유할 수 있는 창이 없습니다. 전체 화면을 선택하세요.");'''
assert d.count(old) == 1, 'status set'
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(old, new, 1))
print('cpp ok')

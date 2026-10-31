# -*- coding: utf-8 -*-
"""Instruments the target-selection chain so the five hypotheses can be told apart.

Nothing here changes what the chain does. Two of the branches that end the click already exist
and return silently; the point is to make them say so.
"""
import io

# ---------------------------------------------------------------- an observation channel
p = 'apps/native_poc/src/client_session_toolbar.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''struct SessionToolbarCallbacks {
  std::function<void()> onTargets;  // back to the capture-target picker
  std::function<void()> onMacro;    // show/hide the macro window
  std::function<void(uint32_t monitorId)> onMonitor;
};'''
new = '''struct SessionToolbarCallbacks {
  std::function<void()> onTargets;  // back to the capture-target picker
  std::function<void()> onMacro;    // show/hide the macro window
  std::function<void(uint32_t monitorId)> onMonitor;
  /**
   * Where this window says what it did with a click.
   *
   * "대상 선택 does nothing" has several possible meanings and the toolbar can distinguish three
   * of them on its own: the press was never recorded, the pointer left the button before release,
   * or the callback was invoked and the rest of the chain is where it stopped. Without this they
   * all look the same from outside -- two of the branches below `return 0` in silence.
   *
   * Optional. Diagnostics only; it must not decide anything.
   */
  std::function<void(const std::string& line)> onLog;
};'''
assert d.count(old) == 1, 'callbacks struct'
d = d.replace(old, new, 1)
if '#include <string>' not in d:
    d = d.replace('#include <functional>', '#include <functional>\n#include <string>', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('toolbar hpp ok')

# ---------------------------------------------------------------- say which branch ran
p = 'apps/native_poc/src/client_session_toolbar.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''    case WM_LBUTTONDOWN:
      g.pressed = hit_test(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      if (g.pressed != kButtonNone) SetCapture(hwnd);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;'''
new = '''    case WM_LBUTTONDOWN:
      g.pressed = hit_test(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      if (g.pressed != kButtonNone) SetCapture(hwnd);
      if (g.callbacks.onLog) {
        g.callbacks.onLog("[toolbar] down id=" + std::to_string(g.pressed) + " x=" +
                          std::to_string(GET_X_LPARAM(lp)) + " y=" +
                          std::to_string(GET_Y_LPARAM(lp)));
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;'''
assert d.count(old) == 1, 'lbuttondown'
d = d.replace(old, new, 1)

old = '''      if (pressed == kButtonNone) return 0;
      if (hit_test(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)) != pressed) return 0;
      if (pressed == kButtonTargets) {
        if (g.callbacks.onTargets) g.callbacks.onTargets();'''
new = '''      if (pressed == kButtonNone) {
        // No press was recorded. The button never saw the down, or something else consumed it.
        if (g.callbacks.onLog) g.callbacks.onLog("[toolbar] up ignored: no press recorded");
        return 0;
      }
      if (const int over = hit_test(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); over != pressed) {
        // Released off the button. Correct behaviour, and indistinguishable from a dead button
        // unless it is said out loud.
        if (g.callbacks.onLog) {
          g.callbacks.onLog("[toolbar] up ignored: released over " + std::to_string(over) +
                            " but pressed " + std::to_string(pressed));
        }
        return 0;
      }
      if (pressed == kButtonTargets) {
        if (g.callbacks.onLog) {
          g.callbacks.onLog(std::string("[toolbar] targets clicked, callback ") +
                            (g.callbacks.onTargets ? "present" : "MISSING"));
        }
        if (g.callbacks.onTargets) g.callbacks.onTargets();'''
assert d.count(old) == 1, 'lbuttonup'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('toolbar cpp ok')

# ---------------------------------------------------------------- the rest of the chain
p = 'apps/native_poc/src/viewer_picker.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''void set_picker_visible_and_sync_stream(ViewerState& ctx, bool visible) {
  ctx.picker.visible.store(visible, std::memory_order_relaxed);'''
new = '''void set_picker_visible_and_sync_stream(ViewerState& ctx, bool visible) {
  const bool was = ctx.picker.visible.exchange(visible, std::memory_order_relaxed);
  // The transition, and the generation that decides whether the stream is held off. Between them
  // these separate "the flag never changed" from "it changed and nothing was drawn".
  log_client_line(ctx, std::string("[picker] visible ") + (was ? "1" : "0") + "->" +
                           (visible ? "1" : "0") + " activeGen=" +
                           std::to_string(ctx.sel.activeStreamGeneration.load(
                               std::memory_order_acquire)));'''
assert d.count(old) == 1, 'picker visible'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('picker ok')

# ---------------------------------------------------------------- wire the channel
p = 'apps/native_poc/src/viewer_startup.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''    toolbarCallbacks.onTargets = [&ctx] {'''
new = '''    toolbarCallbacks.onLog = [&ctx](const std::string& line) { log_client_line(ctx, line); };
    toolbarCallbacks.onTargets = [&ctx] {'''
assert d.count(old) == 1, 'startup wire'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('startup ok')

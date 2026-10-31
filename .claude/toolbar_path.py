# -*- coding: utf-8 -*-
"""The toolbar said 직접 when it knew nothing.

`relay` is a bool that starts false, and the bar printed `relay ? 릴레이 : 직접`. Before anything
had answered -- which is exactly when a user looks at it -- that reads as a confirmed direct
connection. A default is not a measurement.
"""
import io

# ---------------------------------------------------------------- the missing third state
p = 'apps/native_poc/src/viewer_session_state.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  std::atomic<bool> relayPath{false};'''
new = '''  std::atomic<bool> relayPath{false};
  /**
   * Whether `relayPath` has been decided by anything.
   *
   * Without this the two states "direct" and "not yet known" share one value, and the toolbar
   * showed the second as the first -- a claim about the connection made before the connection
   * existed.
   */
  std::atomic<bool> relayPathKnown{false};'''
assert d.count(old) == 1, 'session state'
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(old, new, 1))
print('session state ok')

# ---------------------------------------------------------------- set it where the path is decided
p = 'apps/native_poc/src/viewer_startup.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''    ctx.session.relayPath.store(session.relay, std::memory_order_relaxed);'''
new = '''    ctx.session.relayPath.store(session.relay, std::memory_order_relaxed);
    // The directory answered, so this is a measurement now rather than a default.
    ctx.session.relayPathKnown.store(true, std::memory_order_relaxed);'''
assert d.count(old) == 1, 'directory path'
d = d.replace(old, new, 1)

old = '''    ctx.session.sock = socket(AF_INET,
                   (ctx.dec.transport == VideoTransport::Udp) ? SOCK_DGRAM : SOCK_STREAM,
                   (ctx.dec.transport == VideoTransport::Udp) ? IPPROTO_UDP : IPPROTO_TCP);'''
new = '''    ctx.session.sock = socket(AF_INET,
                   (ctx.dec.transport == VideoTransport::Udp) ? SOCK_DGRAM : SOCK_STREAM,
                   (ctx.dec.transport == VideoTransport::Udp) ? IPPROTO_UDP : IPPROTO_TCP);
    // An address given on the command line is dialled directly by construction -- there is no
    // relay in this path -- so here "direct" is known rather than assumed.
    ctx.session.relayPath.store(false, std::memory_order_relaxed);
    ctx.session.relayPathKnown.store(true, std::memory_order_relaxed);'''
assert d.count(old) == 1, 'explicit path'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('startup ok')

# ---------------------------------------------------------------- carry it to the bar
p = 'apps/native_poc/src/client_session_toolbar.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  bool relay = false;  // the billed path, so it is worth saying out loud'''
new = '''  bool relay = false;  // the billed path, so it is worth saying out loud
  /**
   * False until something has decided `relay`.
   *
   * Kept separate rather than folded into `relay` because a bool cannot hold three answers, and
   * the third one -- "nobody has said yet" -- is the one the user sees first.
   */
  bool pathKnown = false;'''
assert d.count(old) == 1, 'toolbar state'
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(old, new, 1))
print('toolbar hpp ok')

p = 'apps/native_poc/src/viewer_picker.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''  state.relay = ctx.session.relayPath.load(std::memory_order_relaxed);'''
new = '''  state.relay = ctx.session.relayPath.load(std::memory_order_relaxed);
  state.pathKnown = ctx.session.relayPathKnown.load(std::memory_order_relaxed);'''
assert d.count(old) == 1, 'picker push'
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(old, new, 1))
print('picker ok')

# ---------------------------------------------------------------- say it honestly
p = 'apps/native_poc/src/client_session_toolbar.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''std::wstring status_text() {
  std::wstring text = g.state.connected ? L"연결됨" : L"연결 중";
  text += g.state.relay ? L" · 릴레이" : L" · 직접";'''
new = '''std::wstring status_text() {
  std::wstring text = g.state.connected ? L"연결됨" : L"연결 중";
  // Three answers, not two. The symbols are there so the three do not depend on colour to be
  // told apart -- the bar is one line of text and has no colour to spare anyway.
  if (!g.state.pathKnown) {
    text += L" · ◌ 경로 확인 중";
  } else if (g.state.relay) {
    text += L" · ⇄ 릴레이";
  } else {
    text += L" · → 직접";
  }'''
assert d.count(old) == 1, 'status text'
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(old, new, 1))
print('toolbar cpp ok')

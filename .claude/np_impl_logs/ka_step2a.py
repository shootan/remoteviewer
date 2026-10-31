import os
R = r'C:\work\gnlink-wt\ka-pc\apps\native_poc'
rel_now = ''

def load(rel):
    b = open(os.path.join(R, rel), 'rb').read().decode('utf-8')
    return b.replace('\r\n', '\n'), ('\r\n' in b)

def save(rel, s, crlf):
    open(os.path.join(R, rel), 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

def rep(s, a, b, cnt=1):
    assert s.count(a) == cnt, (rel_now, s.count(a), a[:90])
    return s.replace(a, b)

# Args
rel_now = r'src\viewer_args.hpp'
s, c = load(rel_now)
s = rep(s, "  std::string cancelEventHandle;", """  std::string cancelEventHandle;
  // ka-pc-impl r1 (KA ③): started by the client shell as the managed standby viewer of one PC. The two handle
  // values are this process's ends of the shell's private pipes (managed_viewer_ipc.hpp); everything secret
  // (the session token) arrives on them, never here.
  bool managedStandby = false;
  std::string ipcReadHandle;
  std::string ipcWriteHandle;""")
save(rel_now, s, c)

rel_now = r'src\viewer_args.cpp'
s, c = load(rel_now)
s = rep(s, """    } else if (k == "--reconnect") {
      a.reconnect = true;
    }""", """    } else if (k == "--reconnect") {
      a.reconnect = true;
    } else if (k == "--managed-standby") {
      a.managedStandby = true;
    } else if (k == "--ipc-read" && i + 1 < argc) {
      a.ipcReadHandle = trim_ascii(argv[++i]);
    } else if (k == "--ipc-write" && i + 1 < argc) {
      a.ipcWriteHandle = trim_ascii(argv[++i]);
    }""")
save(rel_now, s, c)

# Session state
rel_now = r'src\viewer_session_state.hpp'
s, c = load(rel_now)
s = rep(s, """  SOCKET sock = INVALID_SOCKET;
  // cross-thread: set once by create_window""", """  SOCKET sock = INVALID_SOCKET;
  // ka-pc-impl r1: `sock` is a duplicate of a standby lease's socket (an activation). It is closed, never shut
  // down: shutdown() acts on the socket the lease keeps pinging on after the session.
  bool sockSharedWithLease = false;
  // ka-pc-impl r1: the managed standby viewer's milestones of one activation ("picker-ready", "presented"),
  // each said once; empty in an ordinary viewer. Called on the UI thread.
  std::function<void(const char*)> milestone;
  std::atomic<bool> pickerReadySaid{false};
  std::atomic<bool> presentedSaid{false};
  // cross-thread: set once by create_window""")
if '#include <functional>' not in s:
    s = rep(s, '#include "viewer_common.hpp"\n', '#include <functional>\n\n#include "viewer_common.hpp"\n')
save(rel_now, s, c)

# WM_CLOSE / shutdown: no shutdown() on a lease socket
rel_now = r'src\viewer_window_proc.cpp'
s, c = load(rel_now)
s = rep(s, """      ctx.session.running = false;
      if (ctx.session.sock != INVALID_SOCKET) shutdown(ctx.session.sock, SD_BOTH);
      DestroyWindow(hwnd);""", """      ctx.session.running = false;
      // ka-pc-impl r1: a lease's socket is not shut down -- the lease pings on it after this session.
      if (ctx.session.sock != INVALID_SOCKET && !ctx.session.sockSharedWithLease) shutdown(ctx.session.sock, SD_BOTH);
      DestroyWindow(hwnd);""")
save(rel_now, s, c)

rel_now = r'src\viewer_shutdown.cpp'
s, c = load(rel_now)
s = rep(s, """  if (ctx.session.sock != INVALID_SOCKET) {
    shutdown(ctx.session.sock, SD_BOTH);
  }""", """  // ka-pc-impl r1: a lease's socket (an activation's duplicate) is closed below, never shut down -- shutdown()
  // would end the very socket the lease keeps pinging on once this session is over. The recv thread still
  // leaves: `running` is down and its receive is bounded by the timeout armed at connect.
  if (ctx.session.sock != INVALID_SOCKET && !ctx.session.sockSharedWithLease) {
    shutdown(ctx.session.sock, SD_BOTH);
  }""")
save(rel_now, s, c)

# milestones
rel_now = r'src\viewer_overlay_draw.cpp'
s, c = load(rel_now)
s = rep(s, """      log_client_line(ctx, "[picker] first paint after show, client=" +""", """      // ka-pc-impl r1: the managed viewer's PickerReady (its activation's picker is on the screen).
      if (ctx.session.milestone && !ctx.session.pickerReadySaid.exchange(true)) ctx.session.milestone("picker-ready");
      log_client_line(ctx, "[picker] first paint after show, client=" +""")
save(rel_now, s, c)

rel_now = r'src\viewer_restore.cpp'
s, c = load(rel_now)
i = s.index('void restore_frame_presented(ViewerState& ctx, uint64_t generation) {')
j = s.index('\n', i)
s = s[:j+1] + """  // ka-pc-impl r1: the managed viewer's Presented (the first picture of its activation, after a pick).
  if (ctx.session.milestone && !ctx.session.presentedSaid.exchange(true)) ctx.session.milestone("presented");
""" + s[j+1:]
save(rel_now, s, c)
print('ok')

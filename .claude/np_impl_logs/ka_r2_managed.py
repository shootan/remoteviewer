import os
base = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src' + os.sep

def edit(name, pairs):
    p = base + name
    b = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in b
    s = b.replace('\r\n', '\n')
    for a, c in pairs:
        assert s.count(a) == 1, (name, s.count(a), a[:90])
        s = s.replace(a, c)
    open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

edit('native_video_client_main.cpp', [(
'''#include "viewer_common.hpp"
#include "viewer_context.hpp"''', '''#include "viewer_common.hpp"
#include "viewer_constants.hpp"
#include "viewer_context.hpp"''')])

edit('viewer_managed_standby.cpp', [
('''#include "standby_runner.hpp"''', '''#include "standby_runner.hpp"
#include "viewer_constants.hpp"'''),
('''// How long after an activation's session ended this viewer still says "returning", not "ready". The Host ends a
// session when its control channel has been silent for 10 s (host_startup_control.cpp, UdpControlLink 10000) --
// the viewer's leaving is not announced -- and until then it still counts this device's entry as holding the
// session: a Release is refused (Live), a new admission of this device is Busy. A Pong only says the entry is
// alive, never that the grant is back (Codex model A), so "ready" is not claimed before the Host can have seen the
// end. A tap during "returning" still works: an activation with a newer generation is the device taking it back.
constexpr uint64_t kReturningMs = 12000;''', '''// How long after an activation's session ended this viewer says "returning" before "ready".
// ka-pc-impl r2 (Codex D1): this is NOT a proof that the Host has given the grant back -- nothing on the existing wire
// says that (a Pong only says the entry is alive; SessionOver is local). It is a display interval: the Host ends a
// session after 10 s of control silence (host_startup_control.cpp, UdpControlLink 10000), so for about that long
// the previous connection may still be being cleaned up there. "ready" means: the local hand-back is done and the
// lease's entry is alive -- not "the Host has finished cleaning up". A tap during "returning" is allowed (the device
// takes the grant back with a newer generation), and so is a release.
constexpr uint64_t kReturningMs = 12000;'''),
('''    ViewerContext* live = ctx.get();
    auto closeIt = [live] {
      live->connectCancelled.store(true, std::memory_order_release);  // a connect still in progress ends
      HWND h = live->session.hwnd;
      if (h && IsWindow(h)) PostMessageW(h, WM_CLOSE, 0, 0);
    };''', '''    ViewerContext* live = ctx.get();
    // ka-pc-impl r2 (Codex N4): the owner fence first -- input and the clipboard cut on the UI thread, then the
    // window's own WM_CLOSE teardown. A connect still in progress ends; a session whose window does not exist yet
    // never starts (run_session_body checks connectCancelled before the window and after the connect).
    auto closeIt = [live] {
      live->connectCancelled.store(true, std::memory_order_release);
      HWND h = live->session.hwnd;
      if (h && IsWindow(h)) PostMessageW(h, kMsgOwnerFence, 0, 0);
    };'''),
('''    Message e = stamped(Type::SessionEnded);
    e.fields["op"] = op;
    e.fields["rc"] = std::to_string(rc);
    e.fields["reason"] = parkAsked.load() ? "park" : (stopAfter.load() ? exitReason : (rc == 0 ? "closed" : "ended"));
    e.fields["connected"] = taken ? "1" : "0";''', '''    // ka-pc-impl r2 (Codex N6): "connected" is the session's real connect (handshake + control, the line the
    // ordinary path also counts) -- not that the lease's socket was taken. A handshake that failed after the take
    // is a failed connect (rc 6), and says so.
    const bool connected = ctx->sessionConnected;
    Message e = stamped(Type::SessionEnded);
    e.fields["op"] = op;
    e.fields["rc"] = std::to_string(rc);
    e.fields["reason"] = parkAsked.load() ? "park"
                         : stopAfter.load() ? exitReason
                         : !connected ? "connect-failed"
                         : (rc == 0 ? "closed" : "ended");
    e.fields["connected"] = connected ? "1" : "0";
    e.fields["taken"] = taken ? "1" : "0";'''),
('''    log("activation op=" + op + " ended rc=" + std::to_string(rc));''', '''    log("activation op=" + op + " ended rc=" + std::to_string(rc) + " connected=" + (connected ? "1" : "0") +
        " taken=" + (taken ? "1" : "0"));'''),
])
print('ok')

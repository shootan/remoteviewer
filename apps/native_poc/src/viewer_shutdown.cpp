// The viewer's teardown, verbatim from the former main(): stop, wake the control thread, stop the
// macro engine, close the sockets, join, release the frame and the decoder, MFShutdown.
// (viewer split refactor Phase 2-11)

#include "viewer_shutdown.hpp"
#include "bounded_process_exit.hpp"
#include <cstdio>

#include <iostream>

#include "viewer_state.hpp"
#include "viewer_thread_join.hpp"

namespace remote60::native_poc::viewer {

namespace {
// The recv thread leaves within one receive timeout (25 ms on UDP) once the socket is closed and
// `running` is down; a wedged decoder / D3D call is the only thing that keeps it. Long enough to
// never cut a healthy shutdown short, short enough that a dead session does not become a hung
// process.
constexpr uint32_t kRecvThreadJoinTimeoutMs = 3000;
// The control thread is woken by the channel close / socket shutdown above it; the same bound.
constexpr uint32_t kControlThreadJoinTimeoutMs = 3000;

// A thread that did not come back is left alone -- it may be inside a driver call holding
// pointers into ctx -- and the process ends right here, before any of the shared state below is
// torn down (a detach followed by cleanup would be a use-after-free). Logged first so the field
// log names the stage; exit code 44 tells the shell this was an abnormal end. This is a bounded
// termination, not a recovery and not a reconnect. (Codex invariant 2.)
[[noreturn]] void abandon_thread_and_exit(ViewerContext& ctx, const char* which, uint32_t waitedMs) {
  char text[256];
  const int count = std::snprintf(text, sizeof(text),
      "[viewer][liveness] %s did not stop in %u ms, recv stage=%s; terminating exit44\n",
      which, waitedMs, recv_stage_name(ctx.recvLive.current_stage()));
  remote60::native_poc::terminate_with_diagnostic(44, text, static_cast<DWORD>(count > 0 ? count : 0));
}
}  // namespace

void shutdown_viewer(ViewerContext& ctx) {
  ctx.session.running = false;
  ctx.session.inputEnabled = false;
  ctx.session.hostImeSupported.store(false, std::memory_order_relaxed);  // stop host-IME on teardown
  // Before anything is joined: the control thread can be parked in a blocking receive for the
  // read timeout, and closing the channel is what wakes it. Otherwise shutdown waits it out.
  ctx.control.udpControl.Close(remote60::native_poc::ControlCloseReason::Shutdown);
  ctx.input.macro.StopPlayback();
  ctx.input.macro.StopRecording();
  remote60::native_poc::macro_window_destroy();
  if (ctx.session.sock != INVALID_SOCKET) {
    shutdown(ctx.session.sock, SD_BOTH);
  }
  if (ctx.controlSock != INVALID_SOCKET) {
    shutdown(ctx.controlSock, SD_BOTH);
  }
  // Bounded joins. A thread wedged inside a decoder / D3D / socket call never returns; blocking
  // here would turn a dead session into a hung process the shell cannot restart. Only a thread
  // that actually returned lets the teardown below touch the state it shared. (Codex condition
  // 4 / invariant 2 on history #390 item 5.)
  if (!join_with_timeout(ctx.controlThread, kControlThreadJoinTimeoutMs)) {
    abandon_thread_and_exit(ctx, "control", kControlThreadJoinTimeoutMs);
  }
  if (!join_with_timeout(ctx.recvThread, kRecvThreadJoinTimeoutMs)) {
    abandon_thread_and_exit(ctx, "recv", kRecvThreadJoinTimeoutMs);
  }
  if (ctx.session.sock != INVALID_SOCKET) { closesocket(ctx.session.sock); ctx.session.sock = INVALID_SOCKET; }
  if (ctx.controlSock != INVALID_SOCKET) { closesocket(ctx.controlSock); ctx.controlSock = INVALID_SOCKET; }

  if (ctx.dec.useH264) {
    {
      std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
      ctx.frameBuf.frame.surfaceSample.Reset();
      ctx.frameBuf.frame.surfaceTexture.Reset();
      ctx.frameBuf.frame.bytes.reset();
    }
    ctx.dec.decoder.shutdown();
    if (ctx.dec.mfStarted) MFShutdown();
  }
  ctx.uiWatchdogStop.store(true);
  if (ctx.uiWatchdog.joinable()) ctx.uiWatchdog.join();
}

}  // namespace remote60::native_poc::viewer

// The viewer's teardown, verbatim from the former main(): stop, wake the control thread, stop the
// macro engine, close the sockets, join, release the frame and the decoder, MFShutdown.
// (viewer split refactor Phase 2-11)

#include "viewer_shutdown.hpp"

#include "viewer_globals.hpp"

namespace remote60::native_poc::viewer {

void shutdown_viewer(ViewerContext& ctx) {
  gSession.running = false;
  gSession.inputEnabled = false;
  // Before anything is joined: the control thread can be parked in a blocking receive for the
  // read timeout, and closing the channel is what wakes it. Otherwise shutdown waits it out.
  gControl.udpControl.Close(remote60::native_poc::ControlCloseReason::Shutdown);
  gInput.macro.StopPlayback();
  gInput.macro.StopRecording();
  remote60::native_poc::macro_window_destroy();
  if (gSession.sock != INVALID_SOCKET) {
    shutdown(gSession.sock, SD_BOTH);
    closesocket(gSession.sock);
    gSession.sock = INVALID_SOCKET;
  }
  if (ctx.controlSock != INVALID_SOCKET) {
    shutdown(ctx.controlSock, SD_BOTH);
    closesocket(ctx.controlSock);
    ctx.controlSock = INVALID_SOCKET;
  }
  if (ctx.controlThread.joinable()) ctx.controlThread.join();
  if (ctx.recvThread.joinable()) ctx.recvThread.join();

  if (ctx.dec.useH264) {
    {
      std::lock_guard<std::mutex> lk(gFrameBuf.frame.mu);
      gFrameBuf.frame.surfaceSample.Reset();
      gFrameBuf.frame.surfaceTexture.Reset();
      gFrameBuf.frame.bytes.reset();
    }
    ctx.dec.decoder.shutdown();
    if (ctx.dec.mfStarted) MFShutdown();
  }
}

}  // namespace remote60::native_poc::viewer

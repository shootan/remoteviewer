import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

SRC = r'D:\remote\remote\apps\native_poc\src'
NL = chr(92) + 'n'  # a literal backslash-n for C++ string literals

edit(SRC + r'\viewer_state.hpp', [
("""#include "viewer_ui_resources.hpp"
#include "viewer_frame_gate_state.hpp"
""",
"""#include "viewer_ui_resources.hpp"
#include "viewer_frame_gate_state.hpp"
#include "viewer_recv_liveness.hpp"
"""),
("""  RemoteCursorState cursor;      // 1-9  remote cursor sample + overlay window
  UiResources ui;                // 1-10 fonts, dpi, brush cache, NV12 presenter
};
""",
"""  RemoteCursorState cursor;      // 1-9  remote cursor sample + overlay window
  UiResources ui;                // 1-10 fonts, dpi, brush cache, NV12 presenter
  RecvLiveness recvLive;         // 1-11 recv-thread progress heartbeat + the UI watchdog's state (history #390 item 5)
};
"""),
])

edit(SRC + r'\viewer_session_state.hpp', [
("""  uint32_t udpHelloAckFeatures = 0;
  bool hostSupportsNack = false;
""",
"""  uint32_t udpHelloAckFeatures = 0;
  bool hostSupportsNack = false;
  // Session watchdog (viewer_session_watchdog.cpp): a session whose control channel is gone for
  // good and whose video has not progressed for deadSessionUs is declared dead; deadSessionExit
  // then ends the viewer so the shell offers a reconnect (REMOTE60_NATIVE_DEAD_SESSION_EXIT=0 only
  // notifies). main sets both; the UI thread reads.
  uint32_t deadSessionUs = 5000000;
  bool deadSessionExit = true;
"""),
])

edit(SRC + r'\viewer_startup.cpp', [
("""  ctx.videoNackEnabled = env_u32_clamped("REMOTE60_NATIVE_VIDEO_NACK", 1, 0, 1) != 0;
""",
"""  ctx.videoNackEnabled = env_u32_clamped("REMOTE60_NATIVE_VIDEO_NACK", 1, 0, 1) != 0;
  ctx.session.deadSessionUs =
      env_u32_clamped("REMOTE60_NATIVE_DEAD_SESSION_MS", 5000, 1000, 600000) * 1000u;
  ctx.session.deadSessionExit = env_u32_clamped("REMOTE60_NATIVE_DEAD_SESSION_EXIT", 1, 0, 1) != 0;
"""),
])

edit(SRC + r'\viewer_window_proc.cpp', [
("""      if (wp == kCursorOverlayTimerId) {
        update_cursor_overlay(ctx, hwnd);
        // Safety net, not the primary path: if a repaint request was ever lost, this notices that
        // a newer frame is sitting unpresented and asks again. Correctness lives in
        // request_video_paint; this only bounds the damage to one tick. (Viewer ledger F-20.)
        poll_video_paint_liveness(ctx, hwnd);
        return 0;
      }
""",
"""      if (wp == kCursorOverlayTimerId) {
        update_cursor_overlay(ctx, hwnd);
        // Safety net, not the primary path: if a repaint request was ever lost, this notices that
        // a newer frame is sitting unpresented and asks again. Correctness lives in
        // request_video_paint; this only bounds the damage to one tick. (Viewer ledger F-20.)
        poll_video_paint_liveness(ctx, hwnd);
        // Once a second: is the recv thread moving, is anything arriving, is the session dead?
        poll_session_liveness(ctx, hwnd);
        return 0;
      }
"""),
])

# the include for the watchdog in the window procedure
import re
p = SRC + r'\viewer_window_proc.cpp'
t = open(p, 'rb').read().decode('utf-8')
if '#include "viewer_session_watchdog.hpp"' not in t:
    m = re.search(r'#include "viewer_present.hpp"\n', t)
    assert m, 'viewer_window_proc.cpp: no viewer_present include'
    t = t[:m.end()] + '#include "viewer_session_watchdog.hpp"\n' + t[m.end():]
    open(p, 'wb').write(t.encode('utf-8'))
    print('ok include added to viewer_window_proc.cpp')

edit(SRC + r'\viewer_video_receiver.cpp', [
("""  while (ctx.session.running.load()) {
    // At the top of the loop, so it also runs on the receive timeouts a quiet link produces --
    // previously it sat after the packet processing and a socket that stayed silent never
    // reached it, so a harness --seconds limit could only end through the UI's own timer and
    // the socket close. (F-13.)
    if (args.seconds > 0 && qpc_now_us() >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) break;
    discontinuityHandled = false;
    const int n = recv(ctx.session.sock, reinterpret_cast<char*>(datagram.data()), static_cast<int>(datagram.size()), 0);
    if (n <= 0) {
""",
"""  ctx.recvLive.Enter(RecvStage::Recv, qpc_now_us());
  while (ctx.session.running.load()) {
    // At the top of the loop, so it also runs on the receive timeouts a quiet link produces --
    // previously it sat after the packet processing and a socket that stayed silent never
    // reached it, so a harness --seconds limit could only end through the UI's own timer and
    // the socket close. (F-13.)
    if (args.seconds > 0 && qpc_now_us() >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) break;
    discontinuityHandled = false;
    {
      // Liveness heartbeat for the UI watchdog: one pass per loop, timeouts included.
      const uint64_t loopUs = qpc_now_us();
      ctx.recvLive.loopIterations.fetch_add(1, std::memory_order_relaxed);
      ctx.recvLive.lastLoopUs.store(loopUs, std::memory_order_relaxed);
      ctx.recvLive.Enter(RecvStage::Recv, loopUs);
    }
    const int n = recv(ctx.session.sock, reinterpret_cast<char*>(datagram.data()), static_cast<int>(datagram.size()), 0);
    if (n <= 0) {
"""),
("""      break;
    }
    if (ctx.control.overUdp.load(std::memory_order_acquire)) ctx.control.udpControl.Tick();
    // Control is offered the datagram BEFORE the video length guard, and the order is the
""",
"""      break;
    }
    {
      const uint64_t gotUs = qpc_now_us();
      ctx.recvLive.lastDatagramUs.store(gotUs, std::memory_order_relaxed);
      ctx.recvLive.Enter(RecvStage::Control, gotUs);
    }
    if (ctx.control.overUdp.load(std::memory_order_acquire)) ctx.control.udpControl.Tick();
    // Control is offered the datagram BEFORE the video length guard, and the order is the
"""),
("""    ++udpSimAcceptedCount;
    ++st.udpChunkRecvCount;

    auto assembleResult = assembler.PushDatagram(datagram.data(), static_cast<size_t>(n), qpc_now_us());
""",
"""    ++udpSimAcceptedCount;
    ++st.udpChunkRecvCount;

    {
      const uint64_t chunkUs = qpc_now_us();
      ctx.recvLive.lastVideoChunkUs.store(chunkUs, std::memory_order_relaxed);
      ctx.recvLive.Enter(RecvStage::Assembly, chunkUs);
    }
    auto assembleResult = assembler.PushDatagram(datagram.data(), static_cast<size_t>(n), qpc_now_us());
"""),
("""  auto deliver_completed = [&](UdpH264AssemblyStepResult& r) -> bool {
    ++st.udpAssemblyCompletedCount;
    const uint64_t packetNowUs = qpc_now_us();
""",
"""  auto deliver_completed = [&](UdpH264AssemblyStepResult& r) -> bool {
    ++st.udpAssemblyCompletedCount;
    const uint64_t packetNowUs = qpc_now_us();
    ctx.recvLive.lastAssembledUs.store(packetNowUs, std::memory_order_relaxed);
"""),
("""  ctx.session.running = false;
  if (ctx.session.hwnd) PostMessageW(ctx.session.hwnd, WM_CLOSE, 0, 0);
  return;
}

void VideoReceiver::run_tcp() {
  while (ctx.session.running.load()) {
    if (args.seconds > 0 && qpc_now_us() >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) break;
""",
"""  ctx.recvLive.Enter(RecvStage::Exited, qpc_now_us());
  ctx.session.running = false;
  if (ctx.session.hwnd) PostMessageW(ctx.session.hwnd, WM_CLOSE, 0, 0);
  return;
}

void VideoReceiver::run_tcp() {
  ctx.recvLive.Enter(RecvStage::Recv, qpc_now_us());
  while (ctx.session.running.load()) {
    if (args.seconds > 0 && qpc_now_us() >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) break;
    {
      const uint64_t loopUs = qpc_now_us();
      ctx.recvLive.loopIterations.fetch_add(1, std::memory_order_relaxed);
      ctx.recvLive.lastLoopUs.store(loopUs, std::memory_order_relaxed);
      ctx.recvLive.Enter(RecvStage::Recv, loopUs);
    }
"""),
("""    MessageHeader header{};
    if (!remote60::native_poc::recv_all(ctx.session.sock, &header, sizeof(header))) break;
    if (header.magic != remote60::native_poc::kMagic || header.size < sizeof(header)) break;
""",
"""    MessageHeader header{};
    if (!remote60::native_poc::recv_all(ctx.session.sock, &header, sizeof(header))) break;
    if (header.magic != remote60::native_poc::kMagic || header.size < sizeof(header)) break;
    ctx.recvLive.lastDatagramUs.store(qpc_now_us(), std::memory_order_relaxed);
"""),
("""  }
  ctx.session.running = false;
  if (ctx.session.hwnd) PostMessageW(ctx.session.hwnd, WM_CLOSE, 0, 0);
}

void VideoReceiver::Run() {
""",
"""  }
  ctx.recvLive.Enter(RecvStage::Exited, qpc_now_us());
  ctx.session.running = false;
  if (ctx.session.hwnd) PostMessageW(ctx.session.hwnd, WM_CLOSE, 0, 0);
}

void VideoReceiver::Run() {
"""),
])

edit(SRC + r'\viewer_video_receiver_frame.cpp', [
("""    const uint64_t decodeStartUs = qpc_now_us();
    std::vector<DecodedFrameNv12> outFrames;
    const int64_t inputSampleTimeHns = static_cast<int64_t>(h.captureQpcUs) * 10;
    bool pendingTimestampOverflow = false;
    dec.decoder.set_next_input_synthetic(synthetic);  // rides the decoder's pending-input FIFO (0.2.97)
    if (!dec.decoder.decode_access_unit(*payloadPtr, keyFrame, inputSampleTimeHns, &outFrames,
                                    &pendingTimestampOverflow)) {
      fg.note_decode_failure(in, lag);
""",
"""    const uint64_t decodeStartUs = qpc_now_us();
    std::vector<DecodedFrameNv12> outFrames;
    const int64_t inputSampleTimeHns = static_cast<int64_t>(h.captureQpcUs) * 10;
    bool pendingTimestampOverflow = false;
    dec.decoder.set_next_input_synthetic(synthetic);  // rides the decoder's pending-input FIFO (0.2.97)
    ctx.recvLive.Enter(RecvStage::Decode, decodeStartUs);
    const bool decodedOk = dec.decoder.decode_access_unit(*payloadPtr, keyFrame, inputSampleTimeHns,
                                                          &outFrames, &pendingTimestampOverflow);
    ctx.recvLive.lastDecodeReturnUs.store(qpc_now_us(), std::memory_order_relaxed);
    if (!decodedOk) {
      fg.note_decode_failure(in, lag);
"""),
("""    const uint64_t nowUs = qpc_now_us();
    const uint64_t queueSetUs = nowUs;
    const uint64_t decodeToQueueUs = (queueSetUs >= decodeEndUs) ? (queueSetUs - decodeEndUs) : 0;
    {
      std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
""",
"""    const uint64_t nowUs = qpc_now_us();
    const uint64_t queueSetUs = nowUs;
    const uint64_t decodeToQueueUs = (queueSetUs >= decodeEndUs) ? (queueSetUs - decodeEndUs) : 0;
    ctx.recvLive.Enter(RecvStage::Publish, nowUs);
    {
      std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
"""),
("""    // The reveal above and the picker-close handler invalidate on their own, so the newest
    // decoded frame still shows the moment the picker leaves.
    request_video_paint(ctx, ctx.session.hwnd);
""",
"""    // The reveal above and the picker-close handler invalidate on their own, so the newest
    // decoded frame still shows the moment the picker leaves.
    request_video_paint(ctx, ctx.session.hwnd);
    ctx.recvLive.lastPublishUs.store(qpc_now_us(), std::memory_order_relaxed);
"""),
])

CMAKE = r'D:\remote\remote\apps\native_poc\CMakeLists.txt'
edit(CMAKE, [
("""  src/viewer_video_receiver.cpp
  src/viewer_video_receiver_frame.cpp
  src/viewer_window_proc.cpp
  src/viewer_cursor_overlay.cpp
""",
"""  src/viewer_video_receiver.cpp
  src/viewer_video_receiver_frame.cpp
  src/viewer_session_watchdog.cpp
  src/viewer_window_proc.cpp
  src/viewer_cursor_overlay.cpp
"""),
("""# Windows receive-path integration test (Windows NACK wiring / recovery state machine): the real
""",
"""# The session-liveness verdict (recv-thread stall / silent link / dead session), pure logic.
add_executable(remote60_viewer_liveness_test
  src/viewer_liveness_test.cpp
)
target_include_directories(remote60_viewer_liveness_test PRIVATE src)

# Windows receive-path integration test (Windows NACK wiring / recovery state machine): the real
"""),
])

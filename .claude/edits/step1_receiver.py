import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

HPP = r'D:\remote\remote\apps\native_poc\src\viewer_video_receiver.hpp'
CPP = r'D:\remote\remote\apps\native_poc\src\viewer_video_receiver.cpp'
STATS = r'D:\remote\remote\apps\native_poc\src\viewer_recv_stats.hpp'

edit(HPP, [
(
"""class VideoReceiver {
 public:
  VideoReceiver(ViewerState& ctx, const Args& args, DecoderState& dec, FrameGateState& gate,
                uint64_t startUs, uint32_t udpSimDropPm, uint32_t udpSimDropSeed)
      : ctx(ctx), args(args), dec(dec), gate(gate), startUs(startUs), udpSimDropPm(udpSimDropPm),
        udpSimDropSeed(udpSimDropSeed) {}
""",
"""class VideoReceiver {
 public:
  // Video NACK policy of this session (Windows NACK wiring): `enabled` = the host acknowledged
  // kUdpFeatureVideoNack (and the env did not turn it off), `holdUs` = the assembler's in-order
  // hold while a retransmit may still repair an older AU (0 = legacy immediate delivery).
  struct NackOptions {
    bool enabled = false;
    uint64_t holdUs = 0;
  };
  VideoReceiver(ViewerState& ctx, const Args& args, DecoderState& dec, FrameGateState& gate,
                uint64_t startUs, uint32_t udpSimDropPm, uint32_t udpSimDropSeed,
                NackOptions nack = {})
      : ctx(ctx), args(args), dec(dec), gate(gate), startUs(startUs), udpSimDropPm(udpSimDropPm),
        udpSimDropSeed(udpSimDropSeed), nack(nack) {}
"""),
(
"""  const uint32_t udpSimDropPm;
  const uint32_t udpSimDropSeed;
  RecvStats st;
""",
"""  const uint32_t udpSimDropPm;
  const uint32_t udpSimDropSeed;
  const NackOptions nack;
  RecvStats st;
"""),
])

edit(STATS, [
(
"""  uint64_t udpAssemblyFecRecoveredCount = 0;
  uint32_t udpAssemblyDropPmLast = 0;
""",
"""  uint64_t udpAssemblyFecRecoveredCount = 0;
  uint64_t udpNackSentCount = 0;   // video NACK datagrams sent (Windows NACK wiring)
  uint64_t udpNackChunkCount = 0;  // missing chunk indices they carried
  uint32_t udpAssemblyDropPmLast = 0;
"""),
])

edit(CPP, [
(
"""#include "viewer_decoder_backend.hpp"
#include "viewer_env_util.hpp"
#include "viewer_log.hpp"
#include "viewer_overlay_draw.hpp"
#include "viewer_picker.hpp"
#include "viewer_present.hpp"
""",
"""#include "udp_video_nack.hpp"
#include "viewer_decoder_backend.hpp"
#include "viewer_env_util.hpp"
#include "viewer_log.hpp"
#include "viewer_overlay_draw.hpp"
#include "viewer_picker.hpp"
#include "viewer_present.hpp"
"""),
(
"""  UdpH264FrameAssembler assembler;
  uint64_t assemblyDropped = 0;
  uint64_t oversizePayloadDropCount = 0;
""",
"""  UdpH264FrameAssembler assembler;
  // Video NACK (Windows wiring). Against a host that acknowledged kUdpFeatureVideoNack the
  // assembler delivers in sequence order and holds a completed AU for up to nack.holdUs while an
  // older one may still be repaired; the shared scheduler (udp_video_nack.hpp) decides when to
  // ask. Both stay off otherwise, which is the pre-NACK behaviour exactly (old host, env override).
  const bool nackEnabled = nack.enabled;
  const bool holdEnabled = nackEnabled && nack.holdUs > 0;
  constexpr size_t kHoldMaxConcurrentAssemblies = 8;  // ~120 ms of 60 fps AUs in flight
  if (holdEnabled) assembler.ConfigureInOrderHold(nack.holdUs, kHoldMaxConcurrentAssemblies);
  VideoNackScheduler nackScheduler;
  uint64_t lastUdpNackSentCount = 0;
  uint64_t lastUdpNackChunkCount = 0;
  auto maybe_send_nack = [&](uint64_t nowUs) {
    if (!nackEnabled) return;
    UdpVideoNackPacket pkt{};
    if (!nackScheduler.Poll(assembler, !gate.waitForKeyFrame, nowUs, &pkt)) return;
    (void)send(ctx.session.sock, reinterpret_cast<const char*>(&pkt), sizeof(pkt), 0);
    ++st.udpNackSentCount;
    st.udpNackChunkCount += pkt.missingCount;
    if (st.udpNackSentCount <= 5 || (st.udpNackSentCount % 200) == 1) {
      std::cout << "[native-video-client] video-nack seq=" << pkt.seq
                << " gen=" << pkt.streamGeneration << " missing=" << pkt.missingCount
                << " of=" << pkt.chunkCount << " round=" << pkt.round
                << " total=" << st.udpNackSentCount
                << " waitForKey=" << (gate.waitForKeyFrame ? 1 : 0) << "\\n";
    }
  };
  uint64_t assemblyDropped = 0;
  uint64_t oversizePayloadDropCount = 0;
"""),
(
"""  uint64_t lastUdpSimDroppedCount = 0;
  uint64_t lastUdpSimAcceptedCount = 0;

  while (ctx.session.running.load()) {
    // At the top of the loop, so it also runs on the 200 ms receive timeouts a quiet link
    // produces -- previously it sat after the packet processing and a socket that stayed silent
    // never reached it, so a harness --seconds limit could only end through the UI's own timer
    // and the socket close. (F-13.)
    if (args.seconds > 0 && qpc_now_us() >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) break;
    const int n = recv(ctx.session.sock, reinterpret_cast<char*>(datagram.data()), static_cast<int>(datagram.size()), 0);
    if (n <= 0) {
      // A read timeout is not a dead socket. It is also the tunnel's heartbeat: the control
      // thread spends most of its time blocked waiting for a reply, so if retransmission
      // were driven from there it would stop exactly when a reply goes missing -- and the
      // host, hearing nothing, declares the client lost. This thread always runs.
      if (remote60::native_poc::last_socket_error_is_retryable()) {
        if (ctx.control.overUdp.load(std::memory_order_acquire)) ctx.control.udpControl.Tick();
        continue;
      }
      break;
    }
""",
"""  uint64_t lastUdpSimDroppedCount = 0;
  uint64_t lastUdpSimAcceptedCount = 0;

  // One decoder reset + one keyframe request per loss EPISODE, not per lost frame (P5). Under
  // sustained loss every frame is a discontinuity; resetting the decoder and asking for an IDR
  // each time kept the reference chain permanently broken and turned each answer into a large
  // IDR spike that tipped a marginal link further over -- a death spiral that ended in
  // peer-lost. While already waiting for a keyframe the frame gate keeps re-asking on its own
  // timeout, so suppressing the repeat only removes churn, never the eventual recovery.
  // (history #337, item P5.) The latch is per receive-loop iteration.
  bool discontinuityHandled = false;
  auto handle_udp_discontinuity = [&]() {
    if (discontinuityHandled) return;
    discontinuityHandled = true;
    const bool alreadyWaiting = gate.waitForKeyFrame;
    gate.waitForKeyFrame = true;
    ++st.udpAssemblyKeyReqCount;
    if (alreadyWaiting) return;
    dec.decoder.reset();
    request_keyframe(ctx, 2);
  };
  // The delivered AU's seq is not the previous one + 1: whatever sat between them is gone.
  auto note_sequence_gap = [&](const UdpH264AssemblyStepResult& r) {
    (void)r;
    ++assemblyDropped;
    ++st.udpAssemblyDroppedCount;
    handle_udp_discontinuity();
  };
  // Everything that used to follow a Completed disposition inline: the telemetry line, then the
  // frame. False when the receive loop must end (process_h264_frame said so).
  auto deliver_completed = [&](UdpH264AssemblyStepResult& r) -> bool {
    ++st.udpAssemblyCompletedCount;
    const uint64_t packetNowUs = qpc_now_us();
    // GNLink stream telemetry (diagnostics only): one line per assembled keyframe, plus any
    // non-key frame that needed FEC repair or showed loss/reorder, so a periodic-stutter
    // session joins the host 'wire seq=' log by seq+gen while steady play stays quiet.
    {
      const auto& fh = r.frame.header;
      const bool key = ((fh.flags & 1u) != 0);
      if (key || r.fecRecovered || r.reorderDetected || r.droppedPreviousIncomplete) {
        std::ostringstream telem;
        telem << "[native-video-client][telemetry] stage=assembly"
              << " seq=" << fh.seq
              << " gen=" << fh.streamGeneration
              << " key=" << (key ? 1 : 0)
              << " lastChunkRecvUs=" << packetNowUs
              << " bytes=" << fh.payloadSize
              << " fecRecovered=" << (r.fecRecovered ? 1 : 0)
              << " fecRecoveredChunks=" << r.fecRecoveredChunks
              << " reorder=" << (r.reorderDetected ? 1 : 0)
              << " droppedPrev=" << (r.droppedPreviousIncomplete ? 1 : 0)
              << " pending=" << assembler.PendingCount();
        log_client_line(ctx, telem.str());
      }
    }
    auto payload = std::move(r.frame.payload);
    return process_h264_frame(r.frame.header, &payload, packetNowUs);
  };
  // In-order hold: hand out everything that is ready in sequence, and whatever an expired hold
  // releases. Runs after every datagram AND on every receive timeout, because the hold expires on
  // the clock, not on arrival. False when the receive loop must end.
  auto drain_deliveries = [&]() -> bool {
    if (!holdEnabled) return true;
    UdpH264AssemblyStepResult ready{};
    while (assembler.PopDelivery(qpc_now_us(), !gate.waitForKeyFrame, &ready)) {
      if (ready.droppedPreviousIncomplete) note_sequence_gap(ready);
      if (!deliver_completed(ready)) return false;
    }
    return true;
  };

  while (ctx.session.running.load()) {
    // At the top of the loop, so it also runs on the receive timeouts a quiet link produces --
    // previously it sat after the packet processing and a socket that stayed silent never
    // reached it, so a harness --seconds limit could only end through the UI's own timer and
    // the socket close. (F-13.)
    if (args.seconds > 0 && qpc_now_us() >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) break;
    discontinuityHandled = false;
    const int n = recv(ctx.session.sock, reinterpret_cast<char*>(datagram.data()), static_cast<int>(datagram.size()), 0);
    if (n <= 0) {
      // A read timeout is not a dead socket. It is also the tunnel's heartbeat: the control
      // thread spends most of its time blocked waiting for a reply, so if retransmission
      // were driven from there it would stop exactly when a reply goes missing -- and the
      // host, hearing nothing, declares the client lost. This thread always runs. It is also
      // the clock of the in-order hold and of the NACK rounds on a quiet link: a lost chunk on
      // a static screen is noticed here, not by a next frame that may be seconds away.
      if (remote60::native_poc::last_socket_error_is_retryable()) {
        if (ctx.control.overUdp.load(std::memory_order_acquire)) ctx.control.udpControl.Tick();
        if (!drain_deliveries()) break;
        maybe_send_nack(qpc_now_us());
        continue;
      }
      break;
    }
"""),
(
"""    const auto assembleResult = assembler.PushDatagram(datagram.data(), static_cast<size_t>(n));
    if (assembleResult.fecRecovered) {
      st.udpAssemblyFecRecoveredCount += assembleResult.fecRecoveredChunks;
    }
    bool discontinuityHandled = false;
    auto handle_udp_discontinuity = [&]() {
      if (discontinuityHandled) return;
      discontinuityHandled = true;
      // P5: one decoder reset + one keyframe request per loss EPISODE, not per lost frame. Under
      // sustained loss every frame is a discontinuity; resetting the decoder and asking for an IDR
      // each time kept the reference chain permanently broken and turned each answer into a large
      // IDR spike that tipped a marginal link further over -- a death spiral that ended in
      // peer-lost. While already waiting for a keyframe the frame gate keeps re-asking on its own
      // timeout as complete frames arrive, so suppressing the repeat only removes churn, never the
      // eventual recovery. (history #337, item P5)
      const bool alreadyWaiting = gate.waitForKeyFrame;
      gate.waitForKeyFrame = true;
      ++st.udpAssemblyKeyReqCount;
      if (alreadyWaiting) return;
      dec.decoder.reset();
      request_keyframe(ctx, 2);
    };
    if (assembleResult.droppedPreviousIncomplete) {
      ++assemblyDropped;
      ++st.udpAssemblyDroppedCount;
      handle_udp_discontinuity();
    }
""",
"""    auto assembleResult = assembler.PushDatagram(datagram.data(), static_cast<size_t>(n), qpc_now_us());
    if (assembleResult.fecRecovered) {
      st.udpAssemblyFecRecoveredCount += assembleResult.fecRecoveredChunks;
    }
    // Legacy delivery reports a seq gap on the datagram that revealed it (this may be a Partial:
    // an evicted older assembly). With the in-order hold the gap belongs to the delivery that
    // carries it (drain_deliveries), so a hold that is still running is not read as loss.
    if (assembleResult.droppedPreviousIncomplete && !holdEnabled) note_sequence_gap(assembleResult);
"""),
(
"""    if (assembleResult.disposition == UdpH264AssemblyDisposition::Completed) {
      ++st.udpAssemblyCompletedCount;
      const uint64_t packetNowUs = qpc_now_us();
      // GNLink stream telemetry (diagnostics only): one line per assembled keyframe, plus any
      // non-key frame that needed FEC repair or showed loss/reorder, so a periodic-stutter
      // session joins the host 'wire seq=' log by seq+gen while steady play stays quiet.
      {
        const auto& fh = assembleResult.frame.header;
        const bool key = ((fh.flags & 1u) != 0);
        if (key || assembleResult.fecRecovered || assembleResult.reorderDetected ||
            assembleResult.droppedPreviousIncomplete) {
          std::ostringstream telem;
          telem << "[native-video-client][telemetry] stage=assembly"
                << " seq=" << fh.seq
                << " gen=" << fh.streamGeneration
                << " key=" << (key ? 1 : 0)
                << " lastChunkRecvUs=" << packetNowUs
                << " bytes=" << fh.payloadSize
                << " fecRecovered=" << (assembleResult.fecRecovered ? 1 : 0)
                << " fecRecoveredChunks=" << assembleResult.fecRecoveredChunks
                << " reorder=" << (assembleResult.reorderDetected ? 1 : 0)
                << " droppedPrev=" << (assembleResult.droppedPreviousIncomplete ? 1 : 0);
          log_client_line(ctx, telem.str());
        }
      }
      auto payload = std::move(assembleResult.frame.payload);
      if (!process_h264_frame(assembleResult.frame.header, &payload, packetNowUs)) break;
    }

    const uint64_t nowUs = qpc_now_us();
""",
"""    if (assembleResult.disposition == UdpH264AssemblyDisposition::Completed) {
      // Legacy immediate delivery (no hold): the AU that just completed.
      if (!deliver_completed(assembleResult)) break;
    }
    // In-order hold: Queued (and anything an expired hold releases) goes out here, in sequence.
    if (!drain_deliveries()) break;
    // On a busy link, also nudge the NACK for any still-stuck earlier AU (round-gated inside).
    maybe_send_nack(qpc_now_us());

    const uint64_t nowUs = qpc_now_us();
"""),
(
"""      const uint64_t simDroppedDelta = udpSimDroppedCount - lastUdpSimDroppedCount;
      const uint64_t simAcceptedDelta = udpSimAcceptedCount - lastUdpSimAcceptedCount;
""",
"""      const uint64_t nackSentDelta = st.udpNackSentCount - lastUdpNackSentCount;
      const uint64_t nackChunkDelta = st.udpNackChunkCount - lastUdpNackChunkCount;
      const uint64_t simDroppedDelta = udpSimDroppedCount - lastUdpSimDroppedCount;
      const uint64_t simAcceptedDelta = udpSimAcceptedCount - lastUdpSimAcceptedCount;
"""),
(
"""                << " simDropPm=" << simDropPermille
                << " simDropTotal=" << simDroppedDelta
                << " waitForKey=" << (gate.waitForKeyFrame ? 1 : 0)
                << " catchup=" << (gate.catchupMode ? 1 : 0)
                << "\\n";
""",
"""                << " simDropPm=" << simDropPermille
                << " simDropTotal=" << simDroppedDelta
                << " nackOn=" << (nackEnabled ? 1 : 0)
                << " nackSent=" << nackSentDelta
                << " nackChunks=" << nackChunkDelta
                << " nackExhausted=" << nackScheduler.stats().roundsExhausted
                << " pending=" << assembler.PendingCount()
                << " waitForKey=" << (gate.waitForKeyFrame ? 1 : 0)
                << " catchup=" << (gate.catchupMode ? 1 : 0)
                << "\\n";
"""),
(
"""      lastUdpAssemblyFecRecoveredCount = st.udpAssemblyFecRecoveredCount;
      lastUdpSimDroppedCount = udpSimDroppedCount;
""",
"""      lastUdpAssemblyFecRecoveredCount = st.udpAssemblyFecRecoveredCount;
      lastUdpNackSentCount = st.udpNackSentCount;
      lastUdpNackChunkCount = st.udpNackChunkCount;
      lastUdpSimDroppedCount = udpSimDroppedCount;
"""),
])

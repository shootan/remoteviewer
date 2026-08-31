// See viewer_video_receiver.hpp. Bodies are the recvThread lambda of native_video_client_main.cpp,
// verbatim (viewer split refactor Phase 2-1).

#include "viewer_video_receiver.hpp"

#include "viewer_frame_gate.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>

#include "viewer_decoder_backend.hpp"
#include "viewer_env_util.hpp"
#include "viewer_log.hpp"
#include "viewer_overlay_draw.hpp"
#include "viewer_picker.hpp"
#include "viewer_present.hpp"

namespace remote60::native_poc::viewer {

void VideoReceiver::DecoderSink::reset_decoder() { dec.decoder.reset(); }

bool VideoReceiver::DecoderSink::rebuild_decoder() {
  return dec.decoder.initialize(dec.decoderW, dec.decoderH, args.fpsHint);
}

void VideoReceiver::DecoderSink::request_keyframe(uint16_t reason) { viewer::request_keyframe(reason); }

namespace {

// Wait until the socket is readable or the timeout passes. Lets a loop that would otherwise sit in
// a blocking recv come up for air on a quiet link -- which is exactly when a --seconds limit or a
// running=false has to be noticed. True on readable OR error (the recv reports the error).
bool wait_readable(SOCKET s, int timeoutMs) {
  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(s, &readSet);
  timeval tv{};
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;
  const int ready = select(0, &readSet, nullptr, nullptr, &tv);
  return ready != 0;  // 0 = timeout; >0 readable; <0 error -> let recv see it
}

}  // namespace

void VideoReceiver::run_udp() {
  std::array<uint8_t, 1600> datagram{};
  const uint32_t effectiveUdpSimDropSeed = (udpSimDropSeed > 0)
                                               ? udpSimDropSeed
                                               : static_cast<uint32_t>(qpc_now_us() & 0x7fffffffu);
  std::minstd_rand udpSimRng(effectiveUdpSimDropSeed);
  std::uniform_int_distribution<uint32_t> udpSimDropDist(0, 999);
  UdpH264FrameAssembler assembler;
  uint64_t assemblyDropped = 0;
  uint64_t oversizePayloadDropCount = 0;
  uint64_t udpSimDroppedCount = 0;
  uint64_t udpSimAcceptedCount = 0;
  uint64_t udpAssemblyStatAtUs = qpc_now_us() + 1000000ULL;
  uint64_t lastUdpChunkRecvCount = 0;
  uint64_t lastUdpAssemblyCompletedCount = 0;
  uint64_t lastUdpAssemblyDroppedCount = 0;
  uint64_t lastUdpAssemblyMalformedCount = 0;
  uint64_t lastUdpAssemblyReorderCount = 0;
  uint64_t lastUdpAssemblyKeyReqCount = 0;
  uint64_t lastUdpAssemblyFecRecoveredCount = 0;
  uint64_t lastUdpSimDroppedCount = 0;
  uint64_t lastUdpSimAcceptedCount = 0;

  while (gSession.running.load()) {
    // At the top of the loop, so it also runs on the 200 ms receive timeouts a quiet link
    // produces -- previously it sat after the packet processing and a socket that stayed silent
    // never reached it, so a harness --seconds limit could only end through the UI's own timer
    // and the socket close. (F-13.)
    if (args.seconds > 0 && qpc_now_us() >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) break;
    const int n = recv(gSession.sock, reinterpret_cast<char*>(datagram.data()), static_cast<int>(datagram.size()), 0);
    if (n <= 0) {
      // A read timeout is not a dead socket. It is also the tunnel's heartbeat: the control
      // thread spends most of its time blocked waiting for a reply, so if retransmission
      // were driven from there it would stop exactly when a reply goes missing -- and the
      // host, hearing nothing, declares the client lost. This thread always runs.
      if (remote60::native_poc::last_socket_error_is_retryable()) {
        if (gControl.overUdp.load(std::memory_order_acquire)) gControl.udpControl.Tick();
        continue;
      }
      break;
    }
    if (gControl.overUdp.load(std::memory_order_acquire)) gControl.udpControl.Tick();
    // Control is offered the datagram BEFORE the video length guard, and the order is the
    // whole point. A control message is not bounded below by the video header: a
    // single-fragment input ack is 32 + 28 = 60 bytes against an 88-byte video header, so
    // checking the video size first silently ate every small reply -- input acks and window
    // selections -- while the larger ones (pong, window lists) came through and made the
    // channel look healthy. OnPacket claims only its own kinds, so video cannot be stolen.
    if (gControl.overUdp.load(std::memory_order_acquire) &&
        gControl.udpControl.OnPacket(datagram.data(), static_cast<size_t>(n))) {
      continue;
    }
    // Remote hardware-cursor sample: smaller than the video header, so it must be claimed
    // before the size guard below silently eats it. Latest-wins into atomics; the UI timer
    // does the mapping and drawing.
    if (n == static_cast<int>(sizeof(remote60::native_poc::UdpCursorPosPacket))) {
      remote60::native_poc::UdpCursorPosPacket cp{};
      std::memcpy(&cp, datagram.data(), sizeof(cp));
      if (cp.magic == remote60::native_poc::kMagic &&
          cp.kind == static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::CursorPos) &&
          cp.size == sizeof(cp)) {
        // Bounds sanity before the values reach mapping math: a malformed peer packet must
        // not be able to feed the clamp arithmetic absurd dimensions. Claimed either way.
        if (cp.captureW >= 2 && cp.captureW <= 16384 && cp.captureH >= 2 &&
            cp.captureH <= 16384) {
          gCursor.x.store(cp.x, std::memory_order_relaxed);
          gCursor.y.store(cp.y, std::memory_order_relaxed);
          gCursor.capW.store(cp.captureW, std::memory_order_relaxed);
          gCursor.capH.store(cp.captureH, std::memory_order_relaxed);
          gCursor.generation.store(cp.streamGeneration, std::memory_order_relaxed);
          gCursor.visible.store((cp.flags & 0x1u) != 0, std::memory_order_relaxed);
          gCursor.updateUs.store(qpc_now_us(), std::memory_order_release);
        }
        continue;
      }
    }
    if (n < static_cast<int>(sizeof(UdpVideoChunkHeader))) continue;

    UdpVideoChunkHeader u{};
    std::memcpy(&u, datagram.data(), sizeof(u));
    if (u.magic != remote60::native_poc::kMagic ||
        u.kind != static_cast<uint16_t>(UdpPacketKind::VideoChunk) ||
        u.size != sizeof(UdpVideoChunkHeader)) {
      continue;
    }
    if (u.codec != static_cast<uint16_t>(UdpCodec::H264)) {
      ++st.skippedQueued;
      continue;
    }
    if (udpSimDropPm > 0) {
      const uint32_t samplePm = udpSimDropDist(udpSimRng);
      if (samplePm < udpSimDropPm) {
        ++udpSimDroppedCount;
        ++st.skippedQueued;
        continue;
      }
    }
    ++udpSimAcceptedCount;
    ++st.udpChunkRecvCount;

    const auto assembleResult = assembler.PushDatagram(datagram.data(), static_cast<size_t>(n));
    if (assembleResult.fecRecovered) {
      st.udpAssemblyFecRecoveredCount += assembleResult.fecRecoveredChunks;
    }
    bool discontinuityHandled = false;
    auto handle_udp_discontinuity = [&]() {
      if (discontinuityHandled) return;
      discontinuityHandled = true;
      gate.waitForKeyFrame = true;
      dec.decoder.reset();
      request_keyframe(2);
      ++st.udpAssemblyKeyReqCount;
    };
    if (assembleResult.droppedPreviousIncomplete) {
      ++assemblyDropped;
      ++st.udpAssemblyDroppedCount;
      handle_udp_discontinuity();
    }

    if (assembleResult.disposition == UdpH264AssemblyDisposition::Malformed) {
      ++st.skippedQueued;
      ++st.udpAssemblyMalformedCount;
      handle_udp_discontinuity();
      if (assembleResult.oversizePayload && ((++oversizePayloadDropCount % 30ULL) == 1ULL)) {
        std::cout << "[native-video-client] dropped oversized udp payload bytes="
                  << assembleResult.rejectedPayloadSize
                  << " count=" << oversizePayloadDropCount << "\n";
      }
      continue;
    }

    if (assembleResult.disposition == UdpH264AssemblyDisposition::Dropped) {
      ++st.skippedQueued;
      ++assemblyDropped;
      ++st.udpAssemblyDroppedCount;
      if (assembleResult.reorderDetected) ++st.udpAssemblyReorderCount;
      handle_udp_discontinuity();
      if ((assemblyDropped % 120) == 1) {
        std::cout << "[native-video-client] udp assembly drop count=" << assemblyDropped
                  << " seq=" << u.seq
                  << " expectedSeq=" << assembleResult.expectedSeq
                  << " chunkOffset=" << u.chunkOffset
                  << " nextOffset=" << assembleResult.expectedNextOffset
                  << "\n";
      }
      continue;
    }

    if (assembleResult.disposition == UdpH264AssemblyDisposition::Completed) {
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
          log_client_line(telem.str());
        }
      }
      auto payload = std::move(assembleResult.frame.payload);
      if (!process_h264_frame(assembleResult.frame.header, &payload, packetNowUs)) break;
    }

    const uint64_t nowUs = qpc_now_us();
    if (nowUs >= udpAssemblyStatAtUs) {
      const uint64_t chunksDelta = st.udpChunkRecvCount - lastUdpChunkRecvCount;
      const uint64_t completedDelta = st.udpAssemblyCompletedCount - lastUdpAssemblyCompletedCount;
      const uint64_t droppedDelta = st.udpAssemblyDroppedCount - lastUdpAssemblyDroppedCount;
      const uint64_t malformedDelta = st.udpAssemblyMalformedCount - lastUdpAssemblyMalformedCount;
      const uint64_t reorderDelta = st.udpAssemblyReorderCount - lastUdpAssemblyReorderCount;
      const uint64_t keyReqDelta = st.udpAssemblyKeyReqCount - lastUdpAssemblyKeyReqCount;
      const uint64_t fecRecoveredDelta =
          st.udpAssemblyFecRecoveredCount - lastUdpAssemblyFecRecoveredCount;
      const uint64_t simDroppedDelta = udpSimDroppedCount - lastUdpSimDroppedCount;
      const uint64_t simAcceptedDelta = udpSimAcceptedCount - lastUdpSimAcceptedCount;
      const uint64_t simTotalDelta = simDroppedDelta + simAcceptedDelta;
      const uint64_t simDropPermille = (simTotalDelta > 0)
          ? ((simDroppedDelta * 1000ULL) / simTotalDelta)
          : 0;
      const uint64_t totalFramesDelta = completedDelta + droppedDelta;
      const uint64_t dropPermille = (totalFramesDelta > 0)
          ? ((droppedDelta * 1000ULL) / totalFramesDelta)
          : 0;
      st.udpAssemblyDropPmLast = static_cast<uint32_t>(std::min<uint64_t>(dropPermille, 1000ULL));
      std::cout << "[native-video-client] udp-assembly chunks=" << chunksDelta
                << " completed=" << completedDelta
                << " dropped=" << droppedDelta
                << " dropPm=" << dropPermille
                << " malformed=" << malformedDelta
                << " reorder=" << reorderDelta
                << " keyReq=" << keyReqDelta
                << " fecRecovered=" << fecRecoveredDelta
                << " simDropPm=" << simDropPermille
                << " simDropTotal=" << simDroppedDelta
                << " waitForKey=" << (gate.waitForKeyFrame ? 1 : 0)
                << " catchup=" << (gate.catchupMode ? 1 : 0)
                << "\n";
      lastUdpChunkRecvCount = st.udpChunkRecvCount;
      lastUdpAssemblyCompletedCount = st.udpAssemblyCompletedCount;
      lastUdpAssemblyDroppedCount = st.udpAssemblyDroppedCount;
      lastUdpAssemblyMalformedCount = st.udpAssemblyMalformedCount;
      lastUdpAssemblyReorderCount = st.udpAssemblyReorderCount;
      lastUdpAssemblyKeyReqCount = st.udpAssemblyKeyReqCount;
      lastUdpAssemblyFecRecoveredCount = st.udpAssemblyFecRecoveredCount;
      lastUdpSimDroppedCount = udpSimDroppedCount;
      lastUdpSimAcceptedCount = udpSimAcceptedCount;
      udpAssemblyStatAtUs += 1000000ULL;
    }
  }

  gSession.running = false;
  if (gSession.hwnd) PostMessageW(gSession.hwnd, WM_CLOSE, 0, 0);
  return;
}

void VideoReceiver::run_tcp() {
  while (gSession.running.load()) {
    if (args.seconds > 0 && qpc_now_us() >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) break;
    // The TCP socket has no receive timeout, so recv_all would block for as long as the host
    // stays quiet; a bounded select in front of it keeps the checks above alive. (F-13.)
    if (!wait_readable(gSession.sock, 200)) continue;
    MessageHeader header{};
    if (!remote60::native_poc::recv_all(gSession.sock, &header, sizeof(header))) break;
    if (header.magic != remote60::native_poc::kMagic || header.size < sizeof(header)) break;
    const auto msgType = static_cast<MessageType>(header.type);

    if (msgType == MessageType::RawFrameBgra && header.size == sizeof(RawFrameHeader)) {
      RawFrameHeader h{};
      h.header = header;
      if (!remote60::native_poc::recv_all(gSession.sock, &h.seq, sizeof(h) - sizeof(MessageHeader))) break;
      std::vector<uint8_t> payload(h.payloadSize);
      if (!remote60::native_poc::recv_all(gSession.sock, payload.data(), payload.size())) break;

      if (!dec.useRaw) {
        ++st.skippedQueued;
        continue;
      }

      const uint64_t nowUs = qpc_now_us();
      const uint64_t queueSetUs = nowUs;
      auto frameBgra = std::make_shared<std::vector<uint8_t>>(std::move(payload));
      if (!frameBgra || frameBgra->empty()) {
        ++st.skippedQueued;
        continue;
      }
      {
        std::lock_guard<std::mutex> lk(gFrameBuf.frame.mu);
        const uint64_t prevVersion = gFrameBuf.frame.version;
        const uint64_t lastPresentedVersion = gFrameBuf.lastPresentedVersion.load(std::memory_order_relaxed);
        if (prevVersion > lastPresentedVersion) {
          ++gFrameBuf.overwriteBeforePresentCount;
        }
        gFrameBuf.frame.format = SharedFrame::PixelFormat::Bgra32;
        gFrameBuf.frame.width = h.width;
        gFrameBuf.frame.height = h.height;
        gFrameBuf.frame.codedWidth = h.width;
        gFrameBuf.frame.codedHeight = h.height;
        gFrameBuf.frame.visibleLeft = 0;
        gFrameBuf.frame.visibleTop = 0;
        gFrameBuf.frame.stride = h.stride;
        gFrameBuf.frame.seq = h.seq;
        gFrameBuf.frame.captureUs = h.captureQpcUs;
        gFrameBuf.frame.encodeStartUs = h.encodeStartQpcUs;
        gFrameBuf.frame.encodeEndUs = h.encodeEndQpcUs;
        gFrameBuf.frame.sendUs = h.sendQpcUs;
        gFrameBuf.frame.recvUs = nowUs;
        gFrameBuf.frame.decodeStartUs = nowUs;
        gFrameBuf.frame.decodeEndUs = nowUs;
        gFrameBuf.frame.queueSetUs = queueSetUs;
        gFrameBuf.frame.decodeToQueueUs = 0;
        gFrameBuf.frame.streamGeneration = h.streamGeneration;
        gFrameBuf.frame.key = false;  // raw BGRA has no keyframe concept; keeps present telemetry quiet.
        gFrameBuf.frame.version = prevVersion + 1;
        gFrameBuf.frame.bytes = std::move(frameBgra);
        gFrameBuf.frame.surfaceSample.Reset();
        gFrameBuf.frame.surfaceTexture.Reset();
        gFrameBuf.frame.surfaceSubresource = 0;
      }
      request_video_paint(gSession.hwnd);

      if (args.traceEvery > 0 && (h.seq % args.traceEvery) == 0 &&
          (args.traceMax == 0 || gPresent.traceRecvPrinted.load() < args.traceMax)) {
        const auto nowPrinted = gPresent.traceRecvPrinted.fetch_add(1) + 1;
        if (args.traceMax == 0 || nowPrinted <= args.traceMax) {
          std::ostringstream oss;
          oss << "[native-video-client][trace_recv] seq=" << h.seq
              << " captureUs=" << h.captureQpcUs
              << " encodeStartUs=" << h.encodeStartQpcUs
              << " encodeEndUs=" << h.encodeEndQpcUs
              << " sendUs=" << h.sendQpcUs
              << " recvUs=" << nowUs
              << " decodeStartUs=" << nowUs
              << " decodeEndUs=" << nowUs
              << " c2eUs=" << ((h.encodeStartQpcUs >= h.captureQpcUs) ? (h.encodeStartQpcUs - h.captureQpcUs) : 0)
              << " encUs=" << ((h.encodeEndQpcUs >= h.encodeStartQpcUs) ? (h.encodeEndQpcUs - h.encodeStartQpcUs) : 0)
              << " e2sUs=" << ((h.sendQpcUs >= h.encodeEndQpcUs) ? (h.sendQpcUs - h.encodeEndQpcUs) : 0)
              << " netUs=" << ((nowUs >= h.sendQpcUs) ? (nowUs - h.sendQpcUs) : 0)
              << " r2dUs=0"
              << " decUs=0"
              << " bytes=" << h.payloadSize;
          log_client_line(oss.str());
        }
      }

      ++st.recvFrames;
      ++st.decodedFrames;
      st.recvBytes += h.payloadSize;
      st.decodedBytes += static_cast<uint64_t>(h.payloadSize);
      const uint64_t latencyUs = (nowUs >= h.captureQpcUs) ? (nowUs - h.captureQpcUs) : 0;
      const uint64_t decodeTailUs = (nowUs >= h.sendQpcUs) ? (nowUs - h.sendQpcUs) : 0;
      st.sumLatencyUs += latencyUs;
      st.sumDecodeTailUs += decodeTailUs;
      st.maxLatencyUs = std::max(st.maxLatencyUs, latencyUs);
      st.maxDecodeTailUs = std::max(st.maxDecodeTailUs, decodeTailUs);

      flush_stats_if_due(nowUs, h.width, h.height, false, 0, 0);
    } else if (msgType == MessageType::EncodedFrameH264 && header.size == sizeof(EncodedFrameHeader)) {
      EncodedFrameHeader h{};
      h.header = header;
      if (!remote60::native_poc::recv_all(gSession.sock, &h.seq, sizeof(h) - sizeof(MessageHeader))) break;
      std::vector<uint8_t> payload(h.payloadSize);
      if (!remote60::native_poc::recv_all(gSession.sock, payload.data(), payload.size())) break;
      const uint64_t packetNowUs = qpc_now_us();
      if (!process_h264_frame(h, &payload, packetNowUs)) break;
    } else {
      const size_t bodySize = static_cast<size_t>(header.size - sizeof(header));
      if (bodySize > 0 && !remote60::native_poc::recv_discard(gSession.sock, bodySize)) break;
      ++st.skippedQueued;
    }

  }
  gSession.running = false;
  if (gSession.hwnd) PostMessageW(gSession.hwnd, WM_CLOSE, 0, 0);
}

void VideoReceiver::Run() {
  dec.recvSelectionEpoch = gSel.epoch.load(std::memory_order_acquire);
  st.statAtUs = qpc_now_us() + 1000000ULL;
  gate.frameIntervalUs = std::max<uint64_t>(
      1ULL, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, args.fpsHint)));
  st.lastPresentCounters = load_present_counters();
  if (dec.transport == VideoTransport::Udp) {
    run_udp();
    return;
  }
  run_tcp();
}

}  // namespace remote60::native_poc::viewer

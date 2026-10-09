// fec-single-chunk-stride, end to end: a real, isolated host (GNLinkStream.exe) capturing a window
// of this process, once with the padded parity layout (REMOTE60_NATIVE_FEC_SINGLE_CHUNK_STRIDE=0,
// what every host in the field sends today) and once with the tight one (the default after this
// change), for three kinds of content -- a still page of text, the same page with a blinking caret
// and one creeping block, and busy video-like motion. MEASUREMENT of the wire, plus the checks
// that make the measurement meaningful:
//
//   - the host was started isolated (e2e_isolation.hpp) with input injection off, capturing only
//     this test's own composed, invisible, click-through window -- never the user's screen;
//   - the host's own startup line says which layout it runs (fecSingleChunkStride=tight|padded);
//   - the receiver is the product's shared session policy (SessionVideoPipeline: the real
//     assembler, in-order hold, NACK scheduler -- NACKs go to the real host and its real replay
//     answers them) and the product decoder on every delivered AU;
//   - lossless: every frame the host sent after the first arrives in order -- no gap, no
//     discontinuity, no keyframe request, nothing malformed, everything decodes -- under BOTH
//     layouts; then the wire: the host's own per-flow byte account (udpTxParityBytes and the
//     rest of its stats line) and this end's count of what arrived, tight against padded.
//   - --loss-permille P --loss-seed S: this end drops that share of video datagrams (by datagram
//     identity, so a replay of a lost chunk is a new draw) before the pipeline sees them, and the
//     NACK/FEC path of the real host is exercised; reported, compared, not gated (two live
//     captures are never the same frames).
//
//   remote60_fec_single_chunk_host_e2e_test [--host <GNLinkStream.exe>] [--seconds N]
//       [--content static|lowmotion|video|all] [--loss-permille P] [--loss-seed S] [--keep-dir]
//   (REMOTE60_ALLOW_HOST_E2E=1; exit 77 = skipped)
// display-capture (a composed window on the interactive desktop), gpu-or-software (encoder in the
// host, decoder here), network (loopback).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "e2e_isolation.hpp"
#include "mf_h264_codec.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"
#include "host_e2e_marker_fixture.hpp"  // r8: shared Target/EventLog/decode_event_marker
#include "session_video_pipeline.hpp"
#include "viewer_constants.hpp"       // r7 V2: FrameGate env defaults
#include "viewer_frame_gate.hpp"      // r7 V2: the real PC receive gate
#include "viewer_frame_gate_state.hpp"
#include "viewer_recv_stats.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "control_resume_e2e_support.hpp"

#include <mfapi.h>
#include <mmsystem.h>

#pragma comment(lib, "mfplat.lib")

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;

namespace {

int gFails = 0;
int gCheckCount = 0;
void check(bool ok, const std::string& what, const std::string& detail = std::string()) {
  ++gCheckCount;
  if (ok) {
    std::printf("PASS %s\n", what.c_str());
  } else {
    std::printf("FAIL %s%s%s\n", what.c_str(), detail.empty() ? "" : " -- ", detail.c_str());
    ++gFails;
  }
}
std::string u(uint64_t v) { return std::to_string(v); }

// The host port is picked at run time per run (e2e_pick_free_udp_port), so runs and tests can overlap.
constexpr uint32_t kFps = 30;
constexpr uint32_t kBitrate = 1500000;  // the user's 1500 setting, where parity was 25% of payload
  using namespace remote60::native_poc::e2emarker;

// ------------------------------------------------------------------------------ host log

std::string value_of(const std::string& line, const std::string& key) {
  const std::string needle = " " + key + "=";
  const size_t at = line.find(needle);
  if (at == std::string::npos) return {};
  const size_t start = at + needle.size();
  const size_t end = line.find(' ', start);
  return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
}
uint64_t num_of(const std::string& line, const std::string& key) {
  const std::string v = value_of(line, key);
  return v.empty() ? 0 : std::strtoull(v.c_str(), nullptr, 10);
}

struct HostAccount {
  std::string layoutToken;  // fecSingleChunkStride=... from the host's own startup line
  size_t statsLines = 0;
  // Cumulative counters of the last stats line (udpTxFrames, not sentFrames: that one is per interval).
  uint64_t txFrames = 0, txBytes = 0, parity = 0, chunkHdr = 0, videoDatagrams = 0, nackBytes = 0,
           nackDatagrams = 0, wireEst = 0;
  size_t keyframeLines = 0;
  // Isolation matrix (r2): max sender queue wait/depth from the host's own [wire] lines, and the
  // wire cap it logged at startup (wireCapBps=...), and how many capture frames the input gate skipped.
  uint64_t maxQueueWaitUs = 0, maxQueueDepth = 0, wireCapBps = 0, wireOverloadSkips = 0;
  // r5 G4: the host's same-clock wire-cap apply marker (qpc) and the bitrate it applied. The LAST one
  // seen (a run has one downshift). 0 = none logged.
  uint64_t wireCapAppliedQpcUs = 0;
  uint64_t wireCapAppliedBitrate = 0;
};

HostAccount read_host_log(const std::wstring& path) {
  HostAccount a;
  std::ifstream in(path);
  std::string line;
  std::string last;
  while (std::getline(in, line)) {
    if (line.find(" fecSingleChunkStride=") != std::string::npos) a.layoutToken = value_of(line, "fecSingleChunkStride");
    if (line.find(" udpTxWireEstBytes=") != std::string::npos) {
      last = line;
      ++a.statsLines;
    }
    if (line.find("[keyframe]") != std::string::npos) ++a.keyframeLines;
    if (line.find(" wire seq=") != std::string::npos) {
      a.maxQueueWaitUs = std::max(a.maxQueueWaitUs, num_of(line, "queueWaitUs"));
      a.maxQueueDepth = std::max(a.maxQueueDepth, num_of(line, "queueDepth"));
    }
    if (line.find(" wireCapBps=") != std::string::npos && a.wireCapBps == 0) a.wireCapBps = num_of(line, "wireCapBps");
    if (line.find(" wireOverloadSkips=") != std::string::npos) a.wireOverloadSkips = num_of(line, "wireOverloadSkips");
    if (line.find("wirecap applied ") != std::string::npos) {  // r5 G4 same-clock apply marker
      a.wireCapAppliedQpcUs = num_of(line, "appliedQpcUs");
      a.wireCapAppliedBitrate = num_of(line, "bitrate");
    }
  }
  if (!last.empty()) {
    a.txFrames = num_of(last, "udpTxFrames");
    a.txBytes = num_of(last, "udpTxBytes");
    a.parity = num_of(last, "udpTxParityBytes");
    a.chunkHdr = num_of(last, "udpTxChunkHdrBytes");
    a.videoDatagrams = num_of(last, "udpTxVideoDatagrams");
    a.nackBytes = num_of(last, "udpTxNackBytes");
    a.nackDatagrams = num_of(last, "udpTxNackDatagrams");
    a.wireEst = num_of(last, "udpTxWireEstBytes");
  }
  return a;
}

// ------------------------------------------------------------------------------- one run

struct RunResult {
  bool launched = false;
  bool hello = false;
  bool nackNegotiated = false;
  HostAccount host;
  // This end.
  uint64_t rxData = 0, rxParity = 0, rxHeader = 0, rxReplay = 0, rxVideoDatagrams = 0, rxDropped = 0;
  uint32_t delivered = 0, firstSeq = 0, lastSeq = 0, gaps = 0, maxGap = 0, keyReq = 0, disc = 0, nacks = 0,
           malformed = 0, fec = 0, giveUps = 0, singleChunkFrames = 0, keyFrames = 0;
  // Sequence gaps the pipeline saw closed by a complete keyframe (the host's own doing: an IDR
  // supersedes the sender's backlog, whose seqs are never sent -- respond_to_sequence_gap).
  uint32_t gapsBehindKey = 0;
  uint32_t decoded = 0, decodeErrors = 0;
  std::string decoder;
  // Isolation-matrix instrumentation (bitrate-hard-cap r2, completion criterion 3).
  std::vector<std::pair<uint64_t, uint32_t>> wireEvents;  // (arrival qpc us, bytes incl. +28)
  std::vector<uint64_t> deliverUs;        // when each AU was delivered (qpc)
  std::vector<uint64_t> captureStampUs;   // its header captureQpcUs (latency, same clock domain)
  std::vector<uint8_t> deliverWasKey;     // 1 if the delivered AU was a keyframe
  uint32_t appliedBitrate = 0, appliedFps = 0;
  uint64_t switchUs = 0;  // when the runtime bitrate downshift was sent (0 = none)
  uint64_t recoveryMaxUs = 0;  // max time from a discontinuity to the next delivered keyframe
  uint32_t recoveryCount = 0;
  // Realtime decode (r4 addendum): a decoder thread consumes AUs AS THEY ARE DELIVERED (not post-hoc),
  // so these are present-ready timings -- when each frame actually finished decoding, and the span
  // from its capture to that moment. This is what a viewer would actually see keep up (or not).
  std::vector<uint64_t> decodeOutUs;      // qpc when each frame finished decoding, in order
  std::vector<uint64_t> decodeLatencyUs;  // decodeOut - captureStamp per frame
  uint32_t realtimeDecoded = 0;           // frames the realtime decoder produced
  uint64_t liveEndUs = 0;                 // qpc when the receive loop was stopped; decodes AFTER this
                                          // are the post-stop DRAIN, excluded from the present-latency
                                          // stat so a frame decoded during teardown does not inflate it.
  // Event-matched change->decoded-picture latency (r6 H3): per event, decodeOut - paint QPC, only for
  // the FIRST decoded frame carrying that event-ID marker. eventsExpected = ids painted; eventsMatched
  // = ids whose marker was decoded. This is the real single-change / full-transition response.
  std::vector<uint64_t> eventLatencyUs;
  uint32_t eventsExpected = 0, eventsMatched = 0;
  // Per-event detail for the V1 paired table: id, paint QPC, first decoded-marker QPC, latency.
  struct EventRec { uint32_t id = 0; uint64_t paintUs = 0, decodedUs = 0, latencyUs = 0; };
  std::vector<EventRec> eventRecs;
  // r7 V2: the real PC VideoReceiver/FrameGate smoke path -- keyframe requests FrameGate made (the
  // "no unnecessary repeated keyframe" check) and the IDRs that actually decoded through the gate.
  uint32_t v2KeyframeRequests = 0;
  uint32_t v2IdrDecoded = 0;
};

uint64_t fnv1a_u64(uint64_t h, uint64_t v) {
  const uint8_t* b = reinterpret_cast<const uint8_t*>(&v);
  for (size_t i = 0; i < sizeof(v); ++i) {
    h ^= b[i];
    h *= 1099511628211ull;
  }
  return h;
}

RunResult run_host(const std::wstring& hostExe, const std::wstring& runDir, Content content, bool tight,
                   uint16_t port, int seconds, uint32_t lossPermille, uint32_t lossSeed, bool wireCapOn,
                   uint32_t bitrate = kBitrate, uint32_t fps = kFps, uint32_t downshiftBitrate = 0,
                   uint32_t downshiftAtSec = 0, bool v2FrameGate = false) {
  RunResult r;
  CreateDirectoryW(runDir.c_str(), nullptr);
  const std::wstring me = self_path();
  const bool staged = CopyFileW(hostExe.c_str(), (runDir + L"GNLinkStream.exe").c_str(), FALSE) &&
                      CopyFileW(me.c_str(), (runDir + L"GNLinkCapture.exe").c_str(), FALSE);
  check(staged, "the host could be staged into the run directory", std::string(hostExe.begin(), hostExe.end()));
  if (!staged) return r;

  EventLog eventLog;  // r6 H3: paint-event ids/QPCs, shared with the decode thread
  Target target;
  target.events = &eventLog;
  target.content = content;
  target.title = L"remote60 fec single-chunk target " + std::to_wstring(GetCurrentProcessId()) + L" " +
                 std::wstring(tight ? L"tight" : L"padded");
  check(target.Start(), "an on-screen, invisible, click-through window is up for the host to capture");
  const HWND targetHwnd = target.hwnd();

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  const std::wstring hostLogPath = runDir + L"host.log";
  HANDLE hostLog = INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION hostPi{};
  {
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_STATS_PRINT_EVERY_SEC", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_FEC_SINGLE_CHUNK_STRIDE", tight ? L"1" : L"0");
    // The FEC-layout runs pin the wire cap OFF (this test's original purpose is the parity layout,
    // and the cap paces a large AU over its send time, which is orthogonal). A separate cap-ON
    // lossless run below asserts "lossless -> premature NACK 0" so the pin does not erase the cap-ON
    // evidence that the r2 progress-based tail fix holds on real hardware. (bitrate-hard-cap r2.)
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_WIRE_CAP", wireCapOn ? L"1" : L"0");
    std::wstring cmd = L"\"" + runDir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                       L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(port) + L" --fps " +
                       std::to_wstring(fps) + L" --bitrate " + std::to_wstring(bitrate) + L" --seconds " +
                       std::to_wstring(seconds + 30) + L" --input-injection-mode none" +
                       remote60::native_poc::e2e::e2e_capture_window_args(target.title);
    const bool noInjection = cmd.find(L"--input-injection-mode none") != std::wstring::npos;
    check(noInjection, "the host is started with input injection off");
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    hostLog = CreateFileW(hostLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hostLog != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = hostLog;
      si.hStdError = hostLog;
    }
    const std::wstring isoAppData = runDir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> isoEnv = e2e_isolated_environment(isoAppData);
    std::string isoWhy;
    const bool isoOk = e2e_command_is_isolated(cmd, runDir, &isoWhy) &&
                       e2e_path_is_under(e2e_block_localappdata(isoEnv), runDir);
    check(isoOk, "the launched host is isolated from the user's files", isoWhy);
    r.launched = noInjection && isoOk &&
                 CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, hostLog != INVALID_HANDLE_VALUE,
                                CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, isoEnv.data(),
                                runDir.c_str(), &si, &hostPi) != 0;
    if (r.launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  check(r.launched, "the host started");

  if (r.launched) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_port = htons(port);
    InetPtonW(AF_INET, L"127.0.0.1", &hostAddr.sin_addr);
    connect(sock, reinterpret_cast<const sockaddr*>(&hostAddr), sizeof(hostAddr));
    DWORD rcvTimeout = 10;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));
    int rcvBuf = 4 << 20;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));

    UdpHelloOptions hello;
    hello.budgetMs = 20000;
    hello.sliceMaxMs = 250;
    hello.retrySleepMs = 50;
    hello.requestNack = true;  // the Windows viewer and the session both ask for NACK
    std::string helloError;
    uint32_t ackFeatures = 0;
    r.hello = udp_hello_handshake(sock, hello, nullptr, &helloError, &ackFeatures, nullptr);
    check(r.hello, "the viewer's Hello is answered", helloError);
    r.nackNegotiated = (ackFeatures & kUdpFeatureVideoNack) != 0;

    UdpControlChannel control;
    control.Configure(
        [&sock](const void* data, size_t len) {
          return send(sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, 1200);
    UdpControlLink link(&control, 12000);

    // The product's receive policy, fed from this socket.
    std::mutex pmu;
    std::atomic<bool> pendingKeyRequest{false};
    uint32_t lastDelivered = 0;
    uint64_t lastDiscUs = 0;  // for recovery-time: a discontinuity awaiting its next delivered key
    // Realtime decode (r4 addendum): a decoder thread consumes AUs as they are delivered, so the
    // fps/latency/freeze reflect what a viewer would actually see keep up -- not a post-hoc batch.
    std::mutex dmu;
    std::condition_variable dcv;
    bool dstop = false;
    std::deque<std::pair<std::vector<uint8_t>, std::pair<bool, uint64_t>>> dq;  // payload, (key, captureStamp)

    // r7 V2: the real PC receive gate. When v2FrameGate, each assembled AU goes through the real
    // FrameGate (admit -> gate verdict), and on Decode is decoded INLINE on the ingress thread (as the
    // viewer's recv thread does), then note_decode_ok/note_reference_sync/clear_empty_streak; fg.tick()
    // runs after every datagram and timeout to drive the recovery (keyframe re-ask) timer. The sink
    // counts keyframe requests and routes them to the same control path. (SessionVideoPipeline still
    // does the assembly + NACK below; FrameGate is the pre-decode gate layered on its delivery.)
    using viewer::FrameGate;
    using viewer::FrameGateInputs;
    using viewer::FrameGateLag;
    using viewer::FrameGateState;
    using viewer::FrameGateVerdict;
    using viewer::RecvStats;
    FrameGateState v2gate;
    RecvStats v2st;
    std::unique_ptr<H264Decoder> v2dec;
    uint64_t v2PresentedCapUs = 0;
    int64_t v2stamp = 10'000'000;
    std::set<uint32_t> v2seen;
    struct V2Sink : viewer::FrameGateSink {
      RunResult* r = nullptr;
      std::atomic<bool>* pending = nullptr;
      void reset_decoder() override {}
      bool rebuild_decoder() override { return true; }
      void request_keyframe(uint16_t reason) override {
        (void)reason;
        ++r->v2KeyframeRequests;
        pending->store(true);
      }
    } v2sink;
    v2sink.r = &r;
    v2sink.pending = &pendingKeyRequest;
    FrameGate v2fg(v2gate, v2st, v2sink);
    if (v2FrameGate) {
      v2gate.catchupReenterMinIntervalUs = viewer::kCatchupReenterMinIntervalUsDefault;
      v2gate.staleCaptureDropUs = viewer::kStaleCaptureDropUs;
      v2gate.staleReferenceRecoveryMinIntervalUs = viewer::kStaleRecoveryMinIntervalUsDefault;
      v2gate.congestionRecoverMinUs = viewer::kCongestionRecoverMinUsDefault;
      v2gate.congestionRecoveryTimeoutUs = viewer::kCongestionRecoveryTimeoutUsDefault;
      v2gate.decodeQueueLagDropUs = viewer::kDecodeQueueLagDropUs;
      v2gate.catchupLagDropUs = viewer::kCatchupLagDropUs;
      v2gate.denseArrivalMaxGapUs = viewer::kDenseArrivalMaxGapUsDefault;
      v2gate.lagTriggerStreakMin = viewer::kLagTriggerStreakMinDefault;
      v2gate.recoveryRetryIntervalUs = viewer::kKeyRecoveryRetryUsDefault;
      v2gate.recoveryRetryMaxIntervalUs = viewer::kKeyRecoveryRetryMaxUsDefault;
      v2gate.recoveryRetryDeferMaxUs = viewer::kKeyRecoveryDeferMaxUsDefault;
      v2gate.frameIntervalUs = 1'000'000ULL / (fps ? fps : 60);
      v2gate.waitForKeyFrame = true;  // an H.264 session starts waiting for its first IDR
    }

    SessionVideoPipelineConfig cfg;
    cfg.nackEnabled = r.nackNegotiated;
    cfg.holdUs = r.nackNegotiated ? 120000 : 0;
    cfg.maxConcurrent = 8;
    SessionVideoPipeline::Callbacks cb;
    cb.deliver = [&](UdpH264AssembledFrame&& f) {
      ++r.delivered;
      if (r.firstSeq == 0) r.firstSeq = f.header.seq;
      if (lastDelivered != 0 && f.header.seq > lastDelivered + 1) {
        ++r.gaps;
        r.maxGap = std::max(r.maxGap, f.header.seq - lastDelivered - 1);
      }
      lastDelivered = f.header.seq;
      r.lastSeq = f.header.seq;
      const bool key = (f.header.flags & kEncodedFrameFlagKeyFrame) != 0;
      if (key) ++r.keyFrames;
      const uint64_t deliverNowUs = qpc_now_us();
      r.deliverUs.push_back(deliverNowUs);
      r.captureStampUs.push_back(f.header.captureQpcUs);
      r.deliverWasKey.push_back(key ? 1 : 0);
      if (key && lastDiscUs != 0 && deliverNowUs >= lastDiscUs) {
        r.recoveryMaxUs = std::max(r.recoveryMaxUs, deliverNowUs - lastDiscUs);
        ++r.recoveryCount;
        lastDiscUs = 0;  // recovered
      }
      if (v2FrameGate) {
        // The real PC pre-decode gate, inline on this (ingress) thread.
        FrameGateInputs in{};
        in.captureQpcUs = f.header.captureQpcUs;
        in.sendQpcUs = f.header.sendQpcUs;
        in.seq = f.header.seq;
        in.keyFrame = key;
        in.packetNowUs = deliverNowUs;
        in.recvGapUs = v2fg.note_packet(deliverNowUs, false);
        in.presentedCapUs = v2PresentedCapUs;
        in.decodedCapUs = v2PresentedCapUs;
        FrameGateLag lag{};
        const FrameGateVerdict v = v2fg.admit(in, &lag);
        if (v == FrameGateVerdict::Decode && v2dec) {
          std::vector<DecodedFrameNv12> dout;
          bool ov = false;
          v2stamp += 333333;
          if (v2dec->decode_access_unit(f.payload, key, v2stamp, &dout, &ov)) {
            v2fg.note_decode_ok();
            v2fg.note_reference_sync(in);
            v2fg.clear_empty_streak();
            if (key) ++r.v2IdrDecoded;
            const uint64_t outUs = qpc_now_us();
            v2PresentedCapUs = f.header.captureQpcUs;
            for (const auto& df : dout) {
              if (df.bytes.empty()) continue;
              const uint32_t mid = decode_event_marker(df.bytes.data(), df.width, df.height, df.visibleLeft, df.visibleTop);
              if (mid != 0 && v2seen.insert(mid).second) {
                const uint64_t paintQpc = eventLog.PaintQpc(mid);
                if (paintQpc != 0 && outUs >= paintQpc) {
                  r.eventLatencyUs.push_back(outUs - paintQpc);
                  r.eventRecs.push_back({mid, paintQpc, outUs, outUs - paintQpc});
                }
              }
            }
          } else {
            v2fg.note_decode_failure(in, lag);
          }
        }
        return;  // v2 decodes inline; nothing goes to the realtime decode queue
      }
      {
        std::lock_guard<std::mutex> dlk(dmu);
        dq.emplace_back(std::move(f.payload), std::make_pair(key, f.header.captureQpcUs));
      }
      dcv.notify_one();
    };
    cb.requestKeyframe = [&] {
      ++r.keyReq;
      pendingKeyRequest.store(true);
    };
    cb.discontinuity = [&] { ++r.disc; if (lastDiscUs == 0) lastDiscUs = qpc_now_us(); };
    cb.sendNack = [&](const UdpVideoNackPacket& p) {
      ++r.nacks;
      (void)send(sock, reinterpret_cast<const char*>(&p), sizeof(p), 0);  // to the real host
    };
    SessionVideoPipeline pipeline(cfg, cb);

    // The realtime decode thread is used only by the measurement path; V2 decodes inline on the
    // ingress thread (its own v2dec), so it is not started under v2FrameGate.
    std::thread decodeThread;
    if (!v2FrameGate) decodeThread = std::thread([&] {
      H264Decoder dec;
      if (!dec.initialize(kWinW, kWinH, fps)) {
        r.decoder = "init-failed";
        return;
      }
      r.decoder = dec.backend_name();
      int64_t stamp = 10000000;
      std::set<uint32_t> seenEvents;  // event-ids whose marker was already matched (first-decode only)
      for (;;) {
        std::pair<std::vector<uint8_t>, std::pair<bool, uint64_t>> au;
        {
          std::unique_lock<std::mutex> dlk(dmu);
          dcv.wait(dlk, [&] { return dstop || !dq.empty(); });
          if (dq.empty()) {
            if (dstop) break;
            continue;
          }
          au = std::move(dq.front());
          dq.pop_front();
        }
        std::vector<DecodedFrameNv12> out;
        bool overflow = false;
        stamp += 333333;
        if (!dec.decode_access_unit(au.first, au.second.first, stamp, &out, &overflow)) ++r.decodeErrors;
        const uint64_t outUs = qpc_now_us();
        for (size_t k = 0; k < out.size(); ++k) {
          ++r.realtimeDecoded;
          r.decodeOutUs.push_back(outUs);
          r.decodeLatencyUs.push_back(outUs >= au.second.second ? outUs - au.second.second : 0);
          // r6 H3: read the event-ID marker out of the decoded Y plane and match the FIRST decoded
          // frame of each event to its paint QPC -> a true change->decoded-picture latency.
          const DecodedFrameNv12& f = out[k];
          if (!f.bytes.empty()) {
            const uint32_t id = decode_event_marker(f.bytes.data(), f.width, f.height, f.visibleLeft, f.visibleTop);
            if (id != 0 && seenEvents.insert(id).second) {
              const uint64_t paintQpc = eventLog.PaintQpc(id);
              if (paintQpc != 0 && outUs >= paintQpc) {
                r.eventLatencyUs.push_back(outUs - paintQpc);
                r.eventRecs.push_back({id, paintQpc, outUs, outUs - paintQpc});
              }
            }
          }
        }
      }
      dec.shutdown();
    });

    std::atomic<bool> stop{false};
    std::map<uint64_t, uint32_t> occurrences;
    std::thread ingress([&] {
      if (v2FrameGate) {  // the V2 decoder lives on this (recv) thread, like the real viewer
        v2dec = std::make_unique<H264Decoder>();
        if (v2dec->initialize(kWinW, kWinH, fps)) r.decoder = v2dec->backend_name();
        else r.decoder = "init-failed";
      }
      std::vector<uint8_t> buf(2048);
      while (!stop.load()) {
        control.Tick();
        const int n = recv(sock, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
        const uint64_t now = qpc_now_us();
        if (n > 0 && !control.OnPacket(buf.data(), static_cast<size_t>(n)) &&
            n >= static_cast<int>(sizeof(UdpVideoChunkHeader))) {
          UdpVideoChunkHeader h{};
          std::memcpy(&h, buf.data(), sizeof(h));
          if (h.magic == kMagic && h.kind == static_cast<uint16_t>(UdpPacketKind::VideoChunk) &&
              h.size == sizeof(UdpVideoChunkHeader)) {
            const bool parity = (h.flags & 0x10u) != 0;
            const uint64_t id = (static_cast<uint64_t>(h.streamGeneration) << 48) ^ (static_cast<uint64_t>(h.seq) << 24) ^
                                (static_cast<uint64_t>(h.chunkIndex) << 1) ^ (parity ? 1u : 0u);
            const uint32_t occ = ++occurrences[id];
            const uint32_t body = static_cast<uint32_t>(n) - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
            ++r.rxVideoDatagrams;
            r.rxHeader += sizeof(UdpVideoChunkHeader);
            r.wireEvents.emplace_back(now, static_cast<uint32_t>(n) + 28u);  // IP+UDP for the window math
            if (occ > 1) r.rxReplay += static_cast<uint64_t>(n);
            else if (parity) r.rxParity += body;
            else r.rxData += body;
            if (!parity && h.chunkCount == 1 && occ == 1) ++r.singleChunkFrames;
            bool drop = false;
            if (lossPermille > 0) {
              uint64_t x = fnv1a_u64(0xcbf29ce484222325ull ^ lossSeed, id);
              x = fnv1a_u64(x, occ);
              drop = (x % 1000u) < lossPermille;
            }
            if (drop) {
              ++r.rxDropped;
            } else {
              std::lock_guard<std::mutex> lk(pmu);
              pipeline.OnDatagram(buf.data(), static_cast<size_t>(n), now);
              if (v2FrameGate) v2fg.tick(now);  // recovery timer after every datagram (real recv loop)
            }
            continue;
          }
        }
        std::lock_guard<std::mutex> lk(pmu);
        pipeline.OnTick(now);
        if (v2FrameGate) v2fg.tick(now);  // and on every receive timeout
      }
    });

    // The viewer's control thread: pings keep the session served; keyframe requests go out the
    // way the product sends them.
    std::atomic<bool> controlStop{false};
    std::thread controlThread([&] {
      ClientControlScheduler scheduler;
      WindowPanelStateModel windowPanel;
      StreamStateControl streamState;
      CaptureModeRequestState captureMode;
      KeyframeRequestState keyframe(120000, 300000, 3);
      RuntimeTuneState runtimeTune(300000, 30000000, 250000, 1, 240);
      ClientInputQueue inputQueue;
      scheduler.Reset(kClientControlIntervalMsDefault, qpc_now_us());
      const uint64_t downshiftSendUs = downshiftBitrate > 0 ? qpc_now_us() + downshiftAtSec * 1'000'000ull : 0;
      bool downsent = false;
      uint32_t dseq = 50000;
      while (!controlStop.load()) {
        if (downshiftSendUs && !downsent && qpc_now_us() >= downshiftSendUs) {
          ControlRuntimeEncoderConfigMessage m{};
          m.header.magic = kMagic;
          m.header.type = static_cast<uint16_t>(MessageType::ControlRuntimeEncoderConfig);
          m.header.size = static_cast<uint16_t>(sizeof(m));
          m.seq = ++dseq;
          m.bitrate = downshiftBitrate;
          m.flags = 0x1u;  // bitrate valid
          m.clientSendQpcUs = qpc_now_us();
          if (link.Write(&m, sizeof(m)) && link.EndMessage()) {
            r.switchUs = qpc_now_us();
            downsent = true;
          }
        }
        if (pendingKeyRequest.exchange(false)) (void)keyframe.Request(2, qpc_now_us());
        ClientControlMetricsSnapshot metrics{};
        ControlOutboundAction action{};
        const uint64_t now = qpc_now_us();
        if (scheduler.NextAction(now, metrics, &windowPanel, &streamState, &captureMode, &keyframe, &runtimeTune,
                                 &inputQueue, &action, nullptr, nullptr)) {
          TcpControlResponse response;
          if (execute_control_action(link, action, &response) && action.kind == ControlOutboundActionKind::Ping) {
            scheduler.OnPingCompleted(qpc_now_us());
          }
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
      }
    });

    std::this_thread::sleep_for(std::chrono::seconds(seconds));

    controlStop.store(true);
    if (controlThread.joinable()) controlThread.join();
    r.liveEndUs = qpc_now_us();  // anything decoded after this is drain, not a live present
    stop.store(true);
    if (ingress.joinable()) ingress.join();
    // Drain and stop the realtime decoder (it finishes any AUs still queued, then exits).
    {
      std::lock_guard<std::mutex> dlk(dmu);
      dstop = true;
    }
    dcv.notify_all();
    if (decodeThread.joinable()) decodeThread.join();
    r.decoded = r.realtimeDecoded;  // the matrix's correctness check now reflects the realtime decode
    r.eventsExpected = eventLog.Count();            // ids painted
    r.eventsMatched = static_cast<uint32_t>(r.eventLatencyUs.size());  // ids decoded + matched
    {
      std::lock_guard<std::mutex> lk(pmu);
      r.malformed = static_cast<uint32_t>(pipeline.stats().malformed);
      r.fec = static_cast<uint32_t>(pipeline.stats().fecRecoveredChunks);
      r.giveUps = static_cast<uint32_t>(pipeline.stats().stuckHeadGiveUps);
      r.gapsBehindKey = static_cast<uint32_t>(pipeline.stats().gapsBehindCompleteKey);
    }
    closesocket(sock);
  }

  target.Stop();
  if (hostPi.hProcess) {
    TerminateProcess(hostPi.hProcess, 0);
    WaitForSingleObject(hostPi.hProcess, 5000);
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  CloseHandle(job);
  r.host = read_host_log(hostLogPath);
  {
    std::string captureLine;
    const bool own = remote60::native_poc::e2e::e2e_host_captured_window(hostLogPath, targetHwnd, &captureLine);
    std::printf("capture: %s (this pid %lu)\n", captureLine.c_str(), static_cast<unsigned long>(GetCurrentProcessId()));
    check(own, std::string(tight ? "tight" : "padded") +
                   ": the host captured the window this test paints (its hwnd, this pid), not another test's",
          captureLine);
  }

  // Decode is realtime now (the decodeThread above), so there is no post-hoc batch pass here.
  r.appliedBitrate = bitrate;
  r.appliedFps = fps;
  return r;
}

void print_run(Content content, bool tight, uint32_t lossPermille, uint32_t lossSeed, const RunResult& r) {
  std::printf("HOSTRUN content=%s layout=%s hostSays=%s loss=%u seed=%u nack=%d hostTxFrames=%llu hostKeyLines=%zu"
              " hostTxBytes=%llu hostParity=%llu hostChunkHdr=%llu hostVideoDg=%llu hostNackBytes=%llu hostWireEst=%llu"
              " rxDelivered=%u rxFirstSeq=%u rxLastSeq=%u rxSingleChunk=%u rxKeyFrames=%u rxGaps=%u rxGapsBehindKey=%u rxMaxGap=%u"
              " rxKeyReq=%u rxDisc=%u rxNacks=%u rxFec=%u rxGiveUps=%u rxMalformed=%u rxDropped=%llu"
              " rxData=%llu rxParity=%llu rxHdr=%llu rxReplay=%llu rxVideoDg=%llu decoded=%u decErr=%u decoder=%s\n",
              content_name(content), tight ? "tight" : "padded", r.host.layoutToken.c_str(), lossPermille, lossSeed,
              r.nackNegotiated ? 1 : 0, static_cast<unsigned long long>(r.host.txFrames), r.host.keyframeLines,
              static_cast<unsigned long long>(r.host.txBytes), static_cast<unsigned long long>(r.host.parity),
              static_cast<unsigned long long>(r.host.chunkHdr), static_cast<unsigned long long>(r.host.videoDatagrams),
              static_cast<unsigned long long>(r.host.nackBytes), static_cast<unsigned long long>(r.host.wireEst),
              r.delivered, r.firstSeq, r.lastSeq, r.singleChunkFrames, r.keyFrames, r.gaps, r.gapsBehindKey, r.maxGap,
              r.keyReq, r.disc,
              r.nacks, r.fec, r.giveUps, r.malformed, static_cast<unsigned long long>(r.rxDropped),
              static_cast<unsigned long long>(r.rxData), static_cast<unsigned long long>(r.rxParity),
              static_cast<unsigned long long>(r.rxHeader), static_cast<unsigned long long>(r.rxReplay),
              static_cast<unsigned long long>(r.rxVideoDatagrams), r.decoded, r.decodeErrors, r.decoder.c_str());
}

// Peak bytes in any sliding window, as bits/s (two-pointer; wireEvents are in arrival order).
double window_peak_bps(const std::vector<std::pair<uint64_t, uint32_t>>& ev, uint64_t windowUs) {
  double peak = 0;
  size_t lo = 0;
  uint64_t sum = 0;
  for (size_t hi = 0; hi < ev.size(); ++hi) {
    sum += ev[hi].second;
    while (ev[hi].first - ev[lo].first >= windowUs) sum -= ev[lo++].second;
    const double bps = static_cast<double>(sum) * 8.0 * 1e6 / static_cast<double>(windowUs);
    if (bps > peak) peak = bps;
  }
  return peak;
}

// The isolation-matrix metrics for one run + the cap-window / responsiveness assertions.
//
// Two layers are reported, kept distinct (r4 R4 + addendum):
//   delivery* -- measured at ASSEMBLER DELIVERY (deliveredFps / deliverLat / deliverGapMax).
//   decode*   -- measured at the REALTIME DECODER output (decodeFps / decodeLat / decodeGapMax): a
//                decoder thread consumed the AUs as they arrived, so this is present-ready timing --
//                what a viewer would actually see keep up. `realtimeDecoded`/decErr is the correctness
//                check. The pre-fixed responsiveness ceiling `maxDecodeGapMs` (0 = no gate) is applied
//                to the DECODE gap, so a candidate that holds the cap by freezing the picture FAILS.
void matrix_metrics(const char* label, uint64_t capBps, const RunResult& r, double maxDecodeGapMs,
                    double maxDecodeLatMs = 0.0) {
  const double p1s = window_peak_bps(r.wireEvents, 1'000'000);
  const double p250 = window_peak_bps(r.wireEvents, 250'000);
  // r4 R5 (Codex e1bc633): the burst contract is the 2s rolling average, so the 2s window of the EXTERNAL
  // received packet trace (original + FEC + NACK, all in wireEvents) is the real pass/fail. In burst mode
  // the 250ms/1s windows INTENTIONALLY exceed the cap (that is Option 1); only p2s must stay within it.
  const double p2s = window_peak_bps(r.wireEvents, 2'000'000);
  char burstEnv[8] = {0};
  const bool burstMode = GetEnvironmentVariableA("REMOTE60_NATIVE_FORCE_CLAMP_BURST", burstEnv,
                                                 sizeof(burstEnv)) > 0 &&
                         (burstEnv[0] == '1' || burstEnv[0] == 't' || burstEnv[0] == 'T');
  double deliveredFps = 0, firstFrameMs = 0, deliverGapMaxMs = 0, idrPerSec = 0;
  if (r.deliverUs.size() >= 2) {
    const uint64_t span = r.deliverUs.back() - r.deliverUs.front();
    if (span > 0) {
      deliveredFps = static_cast<double>(r.delivered) * 1e6 / static_cast<double>(span);
      idrPerSec = static_cast<double>(r.keyFrames) * 1e6 / static_cast<double>(span);
    }
    const uint64_t t0 = r.wireEvents.empty() ? r.deliverUs.front() : r.wireEvents.front().first;
    firstFrameMs = (r.deliverUs.front() - t0) / 1000.0;
    uint64_t gap = 0;
    for (size_t i = 1; i < r.deliverUs.size(); ++i) gap = std::max(gap, r.deliverUs[i] - r.deliverUs[i - 1]);
    deliverGapMaxMs = gap / 1000.0;
  }
  // stutter-keyframe r3 C: the COUNT of >250ms output gaps (the periodic-IDR stutter metric the
  // contract asks for), not just the max. Computed from the retained per-frame deliver/decode times.
  uint32_t deliverGap250 = 0, decodeGap250 = 0;
  for (size_t i = 1; i < r.deliverUs.size(); ++i)
    if (r.deliverUs[i] - r.deliverUs[i - 1] > 250'000ULL) ++deliverGap250;
  for (size_t i = 1; i < r.decodeOutUs.size(); ++i)
    if (r.decodeOutUs[i] - r.decodeOutUs[i - 1] > 250'000ULL) ++decodeGap250;
  // Realtime decode layer.
  double decodeFps = 0, decodeGapMaxMs = 0, decodeLatP95Ms = 0, decodeLatMaxMs = 0, firstDecodeMs = 0;
  if (r.decodeOutUs.size() >= 2) {
    const uint64_t span = r.decodeOutUs.back() - r.decodeOutUs.front();
    if (span > 0) decodeFps = static_cast<double>(r.realtimeDecoded) * 1e6 / static_cast<double>(span);
    const uint64_t t0 = r.wireEvents.empty() ? r.decodeOutUs.front() : r.wireEvents.front().first;
    firstDecodeMs = (r.decodeOutUs.front() - t0) / 1000.0;
    uint64_t gap = 0;
    for (size_t i = 1; i < r.decodeOutUs.size(); ++i) gap = std::max(gap, r.decodeOutUs[i] - r.decodeOutUs[i - 1]);
    decodeGapMaxMs = gap / 1000.0;
    // Present latency over frames decoded DURING the live run (exclude the post-stop drain, which would
    // inflate the max with a frame finished during teardown).
    std::vector<uint64_t> lat;
    for (size_t i = 0; i < r.decodeLatencyUs.size() && i < r.decodeOutUs.size(); ++i)
      if (r.liveEndUs == 0 || r.decodeOutUs[i] <= r.liveEndUs) lat.push_back(r.decodeLatencyUs[i]);
    if (!lat.empty()) {
      std::sort(lat.begin(), lat.end());
      decodeLatP95Ms = lat[(lat.size() * 95) / 100] / 1000.0;
      decodeLatMaxMs = lat.back() / 1000.0;
    }
  }
  std::printf("MATRIX %s: cap=%llu applied=%u/%ufps | win1s=%.0f (%.1f%%) win250=%.0f (%.1f%%) | "
              "deliveredFps=%.1f deliverGapMax=%.0fms firstFrame=%.0fms | realtimeDecoded=%u decodeFps=%.1f "
              "firstDecode=%.0fms decodeGapMax=%.0fms decodeLatP95=%.0fms decodeLatMax=%.0fms | idr/s=%.2f keyReq=%u "
              "disc=%u nacks=%u giveUps=%u decErr=%u decoder=%s rxDropped=%llu gap250(deliver/decode)=%u/%u\n",
              label, (unsigned long long)capBps, r.appliedBitrate, r.appliedFps, p1s, 100.0 * p1s / capBps, p250,
              100.0 * p250 / capBps, deliveredFps, deliverGapMaxMs, firstFrameMs, r.realtimeDecoded, decodeFps,
              firstDecodeMs, decodeGapMaxMs, decodeLatP95Ms, decodeLatMaxMs, idrPerSec, r.keyReq, r.disc, r.nacks,
              r.giveUps, r.decodeErrors, r.decoder.c_str(), (unsigned long long)r.rxDropped, deliverGap250, decodeGap250);
  std::printf("       %s wire-window: win2s=%.0f (%.1f%% of cap) mode=%s\n", label, p2s, 100.0 * p2s / capBps,
              burstMode ? "burst(2s gate)" : "strict(250ms/1s+2s gates)");
  std::printf("       %s host: queueWaitMax=%llums queueDepthMax=%llu wireCapBps=%llu | recoveryMax=%.0fms recoveries=%u\n",
              label, (unsigned long long)(r.host.maxQueueWaitUs / 1000), (unsigned long long)r.host.maxQueueDepth,
              (unsigned long long)r.host.wireCapBps, r.recoveryMaxUs / 1000.0, r.recoveryCount);
  const std::string tag = std::string("matrix ") + label;
  // The 2s window is the burst contract and must hold in BOTH modes (A(2s) <= 2r <=> 2s-avg bitrate <=
  // cap). Allow +15% headroom here: p2s is the client-RECEIVED trace, but a single large IDR can land its
  // bytes in a tighter-than-2s cluster and the two-pointer peak over a short run magnifies that edge; the
  // authoritative sent-side figure is the host's burstWin2s (host.log), asserted separately.
  check(p2s <= capBps * 1.15, tag + ": every 2 s window <= cap +15% (the burst contract)");
  // The 250ms/1s strict windows are the pass/fail ONLY in strict mode; in burst mode they intentionally
  // exceed the cap (Option 1) and are reported for information, with the 2s gate above governing.
  if (!burstMode) {
    check(p1s <= capBps * 1.10, tag + ": every 1 s window <= cap +10%");
    check(p250 <= capBps * 1.10, tag + ": every 250 ms window <= cap +10%");
  } else {
    std::printf("       %s [burst] 250ms=%.1f%% 1s=%.1f%% of cap intentionally exceed; 2s gate governs\n",
                label, 100.0 * p250 / capBps, 100.0 * p1s / capBps);
  }
  check(r.realtimeDecoded >= 2 && r.decodeErrors == 0, tag + ": the realtime decoder produced frames (no decode error)");
  check(firstDecodeMs >= 0 && firstDecodeMs <= 3000, tag + ": the first frame decodes within a bounded time");
  // r4 R4-2 / addendum: a pre-fixed responsiveness ceiling on the REALTIME DECODE gap, so a candidate
  // that keeps the cap by freezing the decoded picture FAILS. Only where a long freeze is a defect
  // (motion-bearing content); 0 disables it (a static page legitimately has long gaps).
  if (maxDecodeGapMs > 0)
    check(decodeGapMaxMs <= maxDecodeGapMs,
          tag + ": max realtime-decode gap within the responsiveness ceiling (" + std::to_string((int)maxDecodeGapMs) + "ms)");
  // r5 measurement: input->screen present latency. For the single-change case this is the capture->
  // decoded-present time of the change frame AFTER idle (no backlog to inflate it), the real "a change
  // reaches the screen" figure Codex asked for -- gated so a laggy present FAILS. deliverGap is NOT used.
  if (maxDecodeLatMs > 0)
    check(decodeLatMaxMs <= maxDecodeLatMs,
          tag + ": input->screen present latency within the ceiling (" + std::to_string((int)maxDecodeLatMs) + "ms)");
}

// A runtime 6 -> 1.5 Mbps downshift. r5 G4: the apply point is the host's OWN same-clock apply marker
// (`wirecap applied ... appliedQpcUs=`, logged where UpdateWireCap changes the cap) -- an INDEPENDENT
// event, not a rate observed later from the result. EVERY complete window from that instant must
// respect the new cap (the credit is preserved across the change, never refilled). The request->apply
// latency is reported separately. A step-down observed on the wire is kept only as an aux cross-check.
void matrix_metrics_downshift(const RunResult& r) {
  const uint64_t applyUs = r.host.wireCapAppliedQpcUs;  // host same-clock apply instant (G4)
  // Aux cross-check (NOT the judged point): where the wire rate actually stepped down.
  uint64_t stepDownUs = 0;
  if (r.switchUs) {
    for (size_t i = 0; i < r.wireEvents.size(); ++i) {
      const uint64_t t = r.wireEvents[i].first;
      if (t < r.switchUs) continue;
      uint64_t bytes = 0;
      for (size_t j = i + 1; j-- > 0;) {
        if (r.wireEvents[j].first + 250'000 < t) break;
        bytes += r.wireEvents[j].second;
      }
      if (static_cast<double>(bytes) * 8.0 * 1e6 / 250'000.0 <= 3'000'000.0) { stepDownUs = t; break; }
    }
  }
  std::vector<std::pair<uint64_t, uint32_t>> before, after;
  for (const auto& e : r.wireEvents) {
    if (r.switchUs && e.first < r.switchUs) before.push_back(e);
    if (applyUs && e.first >= applyUs) after.push_back(e);  // EVERY complete window from the host apply instant
  }
  const double b1 = window_peak_bps(before, 1'000'000);
  const double a1 = window_peak_bps(after, 1'000'000);
  const double a250 = window_peak_bps(after, 250'000);
  std::printf("MATRIX 6->1.5-downshift: switched=%d appliedBitrate=%llu requestToApplyMs=%.0f stepDownVsApplyMs=%.0f "
              "before1s=%.0f (%.1f%% of 6M) after1s=%.0f (%.1f%% of 1.5M) after250=%.0f (%.1f%%) decoded=%u decErr=%u "
              "beforeEv=%zu afterEv=%zu\n",
              r.switchUs != 0 ? 1 : 0, (unsigned long long)r.host.wireCapAppliedBitrate,
              (applyUs && r.switchUs) ? (double)(applyUs - r.switchUs) / 1000.0 : -1.0,
              (applyUs && stepDownUs) ? (double)((int64_t)stepDownUs - (int64_t)applyUs) / 1000.0 : 0.0,
              b1, 100.0 * b1 / 6'000'000.0, a1, 100.0 * a1 / 1'500'000.0, a250, 100.0 * a250 / 1'500'000.0, r.decoded,
              r.decodeErrors, before.size(), after.size());
  check(r.switchUs != 0, "matrix downshift: the runtime bitrate change was sent");
  check(applyUs != 0 && r.host.wireCapAppliedBitrate == 1'500'000,
        "matrix downshift: the host logged a same-clock wire-cap apply at the new rate (independent apply point)");
  check(before.empty() || b1 <= 6'000'000 * 1.10, "matrix downshift: before the switch, 1 s window <= 6 Mbps +10%");
  check(!after.empty() && a1 <= 1'500'000 * 1.10,
        "matrix downshift: EVERY 1 s window from the host apply instant <= 1.5 Mbps +10%");
  check(after.empty() || a250 <= 1'500'000 * 1.10,
        "matrix downshift: EVERY 250 ms window from the host apply instant <= 1.5 Mbps +10%");
}

// r6 H3 paired comparison: candidate cap ON / candidate cap OFF / feature-pre baseline (a95215e), on
// the two discrete-change scenarios x two rates, with EVENT-MATCHED change->decoded-picture latency.
// The judgement (Codex): if the candidate ON is not worse than the baseline and loses no events, the
// (pre-existing) ~1 s idle latency is a separate follow-up and the cap may ship; a candidate-only
// increase or new loss/stall is fixed here.
struct EvStats { double p50 = 0, p95 = 0, max = 0; uint32_t matched = 0, expected = 0; };
EvStats event_stats(const RunResult& r) {
  EvStats s;
  s.matched = r.eventsMatched;
  s.expected = r.eventsExpected;
  std::vector<uint64_t> v = r.eventLatencyUs;
  if (!v.empty()) {
    std::sort(v.begin(), v.end());
    s.p50 = v[v.size() / 2] / 1000.0;
    s.p95 = v[(v.size() * 95) / 100] / 1000.0;
    s.max = v.back() / 1000.0;
  }
  return s;
}

int run_paired(const std::wstring& candidateHost, const std::wstring& baselineHost, const std::wstring& dir,
               int seconds) {
  struct Scn { const char* label; Content content; uint32_t bitrate, fps; };
  const Scn scns[] = {
      {"singlechange-6M60", Content::SingleChange, 6'000'000, 60},
      {"singlechange-1.5M30", Content::SingleChange, 1'500'000, 30},
      {"fulltransition-6M60", Content::FullTransition, 6'000'000, 60},
      {"fulltransition-1.5M30", Content::FullTransition, 1'500'000, 30},
  };
  const bool haveBaseline = !baselineHost.empty();
  std::printf("\n=== r6 H3 paired event-ID latency (candidate ON/OFF%s) ===\n",
              haveBaseline ? " / a95215e baseline" : " -- NO baseline host given");
  int idx = 0;
  for (const Scn& s : scns) {
    auto one = [&](const std::wstring& host, bool capOn, const char* tag) -> EvStats {
      const uint16_t port = remote60::native_poc::e2e::e2e_pick_free_udp_port();
      if (port == 0) return {};
      std::wstring safe(s.label, s.label + std::strlen(s.label));
      for (wchar_t& c : safe) if (c == L'.' || c == L'/') c = L'_';
      const std::wstring runDir = dir + L"p_" + safe + L"_" + std::wstring(tag, tag + std::strlen(tag)) +
                                  L"_" + std::to_wstring(idx++) + L"\\";
      const RunResult r = run_host(host, runDir, s.content, /*tight=*/true, port, seconds, 0, 1, capOn, s.bitrate, s.fps);
      // V1: per-event table (event 1 = first screen, includes connect+host init+first IDR; separated
      // from the later idle->change events) + the slowest event's byte-time trace.
      size_t slow = 0;
      for (size_t i = 1; i < r.eventRecs.size(); ++i)
        if (r.eventRecs[i].latencyUs > r.eventRecs[slow].latencyUs) slow = i;
      std::printf("   [%s/%s] events(id:lat ms): ", s.label, tag);
      for (size_t i = 0; i < r.eventRecs.size(); ++i)
        std::printf("%s%u:%.0f%s", i == 1 ? "| runtime: " : (i == 0 ? "ev1(init): " : ""),
                    r.eventRecs[i].id, r.eventRecs[i].latencyUs / 1000.0, (i + 1 < r.eventRecs.size()) ? " " : "\n");
      if (r.eventRecs.empty()) std::printf("(none)\n");
      if (!r.eventRecs.empty()) {
        const auto& e = r.eventRecs[slow];
        const double latMs = e.latencyUs / 1000.0;
        // A CONVERSION only (latency x rate / 8) for scale -- NOT the measured AU size. It is the wire
        // budget that fits in this latency at the cap; do not read it as "the IDR was this big" or as a
        // proof of cause (the same formula applies to OFF/baseline). The host's wire log (AU bytes /
        // send times) would be needed to attribute the latency; run-max queueWait is not yet tied to
        // this AU. (r8 V1 correction.)
        const double wireBudgetBytes = latMs / 1000.0 * s.bitrate / 8.0;
        std::printf("        slowest ev%u lat=%.0fms: (scale only) latency*rate/8 @%.1fMbps = %.0f bytes -- a "
                    "conversion, NOT a measured AU size; run-max host queueWaitMax=%llums queueDepthMax=%llu "
                    "(not yet tied to this AU)\n",
                    e.id, latMs, s.bitrate / 1e6, wireBudgetBytes, (unsigned long long)(r.host.maxQueueWaitUs / 1000),
                    (unsigned long long)r.host.maxQueueDepth);
      }
      return event_stats(r);
    };
    const EvStats on = one(candidateHost, true, "on");
    const EvStats off = one(candidateHost, false, "off");
    EvStats base{};
    if (haveBaseline) base = one(baselineHost, true, "base");
    std::printf("PAIRED %-22s | ON p50/p95/max=%.0f/%.0f/%.0fms m=%u/%u | OFF %.0f/%.0f/%.0fms m=%u/%u%s\n",
                s.label, on.p50, on.p95, on.max, on.matched, on.expected, off.p50, off.p95, off.max, off.matched,
                off.expected,
                haveBaseline ? ("" ) : " | (baseline missing)");
    if (haveBaseline)
      std::printf("        %-22s | a95215e p50/p95/max=%.0f/%.0f/%.0fms m=%u/%u\n", "", base.p50, base.p95, base.max,
                  base.matched, base.expected);
    const std::string t = std::string("paired ") + s.label;
    // No loss / no gross stall, every config (the real failure modes Codex named).
    check(on.matched == on.expected && on.expected > 0, t + ": candidate ON decoded every painted event (no loss)");
    check(off.matched == off.expected && off.expected > 0, t + ": candidate OFF decoded every painted event (no loss)");
    check(on.max <= 2500.0, t + ": candidate ON no multi-second stall on a change (max=" + std::to_string((int)on.max) + "ms)");
    if (haveBaseline) {
      check(base.matched == base.expected && base.expected > 0, t + ": baseline decoded every painted event");
      // The NEW-LOGIC regression test: with the cap OFF the gate/staging/replay changes are present but
      // the limiter is not, so OFF must match the feature-pre baseline. A real code regression shows
      // HERE. (5 events/run -> p95 is noisy; a generous tolerance.)
      const double tol = std::max(base.p95 * 0.60, 250.0);
      check(off.p95 <= base.p95 + tol,
            t + ": NEW LOGIC (cap OFF) not regressed vs a95215e (off=" + std::to_string((int)off.p95) +
                " base=" + std::to_string((int)base.p95) + "ms)");
      // ON vs baseline is REPORTED, not failed: at a low cap the limiter paces a burst (a change frame)
      // to the configured rate, which inevitably costs latency vs the uncapped baseline -- the hard
      // cap's contractual trade-off, not a code defect (ON>OFF, OFF==baseline isolates it to the cap).
      std::printf("        %-22s | cap rate-bound cost (ON vs a95215e): p95 %+.0fms  [reported, not a gate]\n", "",
                  on.p95 - base.p95);
      check(on.matched >= base.matched, t + ": candidate ON loses no event the baseline delivered");
    }
  }
  return 0;
}

// r7 V2: the real PC VideoReceiver/FrameGate receive path (admit gates decode, tick drives recovery)
// on big-IDR + idle->change content at 6M/60 and 1.5M/30 -- a local smoke (not 2-PC/WAN). Confirms
// the event marker reaches the decoded picture through the gate, recovery is finite, and there is NO
// unnecessary repeated keyframe request (the IDR-storm defect FrameGate exists to avoid).
int run_v2(const std::wstring& hostExe, const std::wstring& dir, int seconds) {
  struct Scn { const char* label; Content content; uint32_t bitrate, fps; };
  const Scn scns[] = {
      {"singlechange-6M60", Content::SingleChange, 6'000'000, 60},
      {"singlechange-1.5M30", Content::SingleChange, 1'500'000, 30},
      {"fulltransition-6M60", Content::FullTransition, 6'000'000, 60},
      {"fulltransition-1.5M30", Content::FullTransition, 1'500'000, 30},
  };
  std::printf("\n=== r7 V2 PC VideoReceiver/FrameGate smoke (cap ON, real gate+decoder) ===\n");
  int idx = 0;
  for (const Scn& s : scns) {
    const uint16_t port = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    if (port == 0) continue;
    std::wstring safe(s.label, s.label + std::strlen(s.label));
    for (wchar_t& c : safe) if (c == L'.' || c == L'/') c = L'_';
    const std::wstring runDir = dir + L"v2_" + safe + L"_" + std::to_wstring(idx++) + L"\\";
    const RunResult r = run_host(hostExe, runDir, s.content, /*tight=*/true, port, seconds, 0, 1, /*wireCapOn=*/true,
                                 s.bitrate, s.fps, 0, 0, /*v2FrameGate=*/true);
    std::printf("V2 %-22s | marker events matched=%u/%u | IDR decoded=%u | FrameGate keyframe reqs=%u | "
                "disc=%u nacks=%u decoder=%s\n",
                s.label, r.eventsMatched, r.eventsExpected, r.v2IdrDecoded, r.v2KeyframeRequests, r.disc, r.nacks,
                r.decoder.c_str());
    const std::string t = std::string("v2 ") + s.label;
    check(r.eventsExpected > 0 && r.eventsMatched == r.eventsExpected,
          t + ": every change marker reached the decoded picture through the real FrameGate");
    check(r.v2IdrDecoded >= 1, t + ": at least one IDR decoded through the gate (recovery works)");
    // No IDR storm: FrameGate must not re-ask every frame. ~5 change events over `seconds`; a healthy
    // run needs only the first-IDR wait + the occasional recovery. A storm is many requests per event.
    check(r.v2KeyframeRequests <= 3u + r.eventsExpected,
          t + ": no unnecessary repeated keyframe requests (reqs=" + std::to_string(r.v2KeyframeRequests) +
              " events=" + std::to_string(r.eventsExpected) + ")");
  }
  return 0;
}

// completion criterion 3: the product-equivalent host + real MFT + real decoder, across the scenario
// matrix, each with the cap ON and every 1 s / 250 ms window measured on the real UDP receive.
int run_matrix(const std::wstring& hostExe, const std::wstring& dir, int seconds) {
  struct Scn {
    const char* label;
    Content content;
    uint32_t bitrate, fps, lossPermille;
    double maxDecodeGapMs;  // realtime-decode responsiveness ceiling; 0 = no gate (static/loss lenient)
    double maxDecodeLatMs;  // input->screen present-latency ceiling (single-change); 0 = no gate
  };
  const Scn scns[] = {
      {"6M/60-motion", Content::Video, 6'000'000, 60, 0, 500.0, 0.0},
      {"6M/60-static", Content::StaticText, 6'000'000, 60, 0, 0.0, 0.0},
      {"1.5M/30-motion", Content::Video, 1'500'000, 30, 0, 900.0, 0.0},
      {"6M/60-loss5%-nack", Content::Video, 6'000'000, 60, 50, 0.0, 0.0},
      {"6M/60-windowdrag", Content::WindowDrag, 6'000'000, 60, 0, 600.0, 0.0},
      {"3M/60-partialvideo", Content::PartialVideo, 3'000'000, 60, 0, 700.0, 0.0},
      // r5 required pre-release measurement: the 3 desktop-like types x {6M/60, 1.5M/30}, realtime
      // decode. Text scroll is continuous motion -> gate the decode GAP. Single change and full
      // transition are DISCRETE (deliberate ~2 s idle between events), so the decode gap is dominated
      // by that legitimate idle, not a freeze -- they gate the input->screen PRESENT LATENCY of the
      // change/transition frame instead (decodeLatMax), never the gap.
      {"6M/60-textscroll", Content::TextScroll, 6'000'000, 60, 0, 600.0, 0.0},
      {"1.5M/30-textscroll", Content::TextScroll, 1'500'000, 30, 0, 1200.0, 0.0},
      // NOTE: the single-change present latency measures the host's idle->change capture/kick
      // surfacing path (the cap is far from binding here -- win1s ~18%, queueDepth 0), NOT the wire
      // cap. The ceiling catches a gross multi-second freeze; the measured value (6M/60 ~1 s, 1.5M/30
      // ~0.47 s, from the host kick latency on an idle screen) is reported for judgement, not hidden.
      {"6M/60-singlechange", Content::SingleChange, 6'000'000, 60, 0, 0.0, 1800.0},
      {"1.5M/30-singlechange", Content::SingleChange, 1'500'000, 30, 0, 0.0, 1800.0},
      {"6M/60-fulltransition", Content::FullTransition, 6'000'000, 60, 0, 0.0, 1200.0},
      {"1.5M/30-fulltransition", Content::FullTransition, 1'500'000, 30, 0, 0.0, 1200.0},
  };
  for (const Scn& s : scns) {
    const uint16_t port = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    if (port == 0) continue;
    std::wstring safe(s.label, s.label + std::strlen(s.label));
    for (wchar_t& c : safe)
      if (c == L'/' || c == L'%' || c == L'.') c = L'_';  // a run-dir name, not a path
    const std::wstring runDir = dir + L"m_" + safe + L"\\";
    const RunResult r = run_host(hostExe, runDir, s.content, /*tight=*/true, port, seconds, s.lossPermille, 1,
                                 /*wireCapOn=*/true, s.bitrate, s.fps);
    matrix_metrics(s.label, s.bitrate, r, s.maxDecodeGapMs, s.maxDecodeLatMs);
  }
  // Runtime 6 -> 1.5 Mbps downshift mid-run.
  const uint16_t dport = remote60::native_poc::e2e::e2e_pick_free_udp_port();
  if (dport != 0) {
    const RunResult r = run_host(hostExe, dir + L"m_downshift\\", Content::Video, /*tight=*/true, dport,
                                 std::max(seconds, 10), 0, 1, /*wireCapOn=*/true, 6'000'000, 60,
                                 /*downshiftBitrate=*/1'500'000, /*downshiftAtSec=*/static_cast<uint32_t>(std::max(seconds, 10) / 2));
    matrix_metrics_downshift(r);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::wstring hostExe;
  int seconds = 12;
  std::string contentArg = "all";
  uint32_t lossPermille = 0;
  uint32_t lossSeed = 1;
  bool keepDir = false;
  bool matrix = false;
  bool paired = false;
  bool v2 = false;
  std::wstring baselineHost;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--host" && i + 1 < argc) {
      const std::string v = argv[++i];
      hostExe.assign(v.begin(), v.end());
    } else if (a == "--baseline" && i + 1 < argc) {
      const std::string v = argv[++i];
      baselineHost.assign(v.begin(), v.end());
    } else if (a == "--paired") {
      paired = true;
    } else if (a == "--v2") {
      v2 = true;
    } else if (a == "--seconds" && i + 1 < argc) {
      seconds = std::max(4, std::atoi(argv[++i]));
    } else if (a == "--content" && i + 1 < argc) {
      contentArg = argv[++i];
    } else if (a == "--loss-permille" && i + 1 < argc) {
      lossPermille = static_cast<uint32_t>(std::atoi(argv[++i]));
    } else if (a == "--loss-seed" && i + 1 < argc) {
      lossSeed = static_cast<uint32_t>(std::atoi(argv[++i]));
    } else if (a == "--keep-dir") {
      keepDir = true;
    } else if (a == "--matrix") {
      matrix = true;
    }
  }
  char allow[8]{};
  if (GetEnvironmentVariableA("REMOTE60_ALLOW_HOST_E2E", allow, sizeof(allow)) == 0 || std::string(allow) != "1") {
    std::printf("SKIP fec_single_chunk_host_e2e_test: starts a real (isolated) GNLinkStream.exe capturing a window of this"
                " process.\n      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return kE2eSkippedExit;
  }
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
  if (FAILED(MFStartup(MF_VERSION))) {
    std::printf("FAIL MFStartup\n");
    return 1;
  }
  // Staged inside the repository's test scratch root, never %TEMP%; each run's host gets its own
  // subdirectory beneath it. Removed when main returns, whichever way it returns, unless --keep-dir.
  remote60::native_poc::e2e::StagingDir staging;
  if (!staging.Create(L"fec_e2e")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  staging.set_keep(keepDir, keepDir ? "--keep-dir" : "");
  const std::wstring dir = staging.path();
  const std::wstring me = self_path();
  if (hostExe.empty()) hostExe = directory_of(me) + L"GNLinkStream.exe";
  std::printf("fec_single_chunk_host_e2e_test: host=%s seconds=%d content=%s loss=%u/1000 seed=%u\n",
              std::string(hostExe.begin(), hostExe.end()).c_str(), seconds, contentArg.c_str(), lossPermille, lossSeed);

  std::vector<Content> contents;
  if (contentArg == "all") contents = {Content::StaticText, Content::LowMotion, Content::Video};
  else if (contentArg == "static") contents = {Content::StaticText};
  else if (contentArg == "lowmotion") contents = {Content::LowMotion};
  else if (contentArg == "video") contents = {Content::Video};
  else {
    std::printf("usage: --content static|lowmotion|video|all\n");
    return 1;
  }

  if (v2) {
    run_v2(hostExe, dir, seconds);
  } else if (paired) {
    run_paired(hostExe, baselineHost, dir, seconds);
  } else if (matrix) {
    run_matrix(hostExe, dir, seconds);
  } else
  for (Content content : contents) {
    const char* cls = content_name(content);
    RunResult res[2];
    for (int layout = 0; layout < 2; ++layout) {
      const bool tight = layout == 1;  // padded first: what the field sends today
      const std::wstring runDir = dir + std::wstring(cls, cls + std::strlen(cls)) + L"_" +
                                  (tight ? L"tight" : L"padded") + L"\\";
      const uint16_t port = remote60::native_poc::e2e::e2e_pick_free_udp_port();
      check(port != 0, std::string(cls) + (tight ? " tight" : " padded") + ": a free UDP port for the host", u(port));
      if (port == 0) {
        // --bind-port 0 would send the host to the product's default port: nothing is started.
        std::printf("SKIP-LAUNCH %s %s: no free port, the host is not started\n", cls, tight ? "tight" : "padded");
        continue;
      }
      res[layout] = run_host(hostExe, runDir, content, tight, port, seconds, lossPermille, lossSeed,
                             /*wireCapOn=*/false);
      print_run(content, tight, lossPermille, lossSeed, res[layout]);
      const RunResult& r = res[layout];
      const std::string tag = std::string(cls) + " " + (tight ? "tight" : "padded");
      check(r.host.layoutToken == (tight ? "tight" : "padded"), tag + ": the host's own startup line names this layout",
            r.host.layoutToken);
      check(r.host.statsLines >= static_cast<size_t>(seconds / 2), tag + ": the host wrote its stats lines",
            u(r.host.statsLines));
      check(r.delivered >= 2, tag + ": frames were received and delivered", u(r.delivered));
      check(r.malformed == 0, tag + ": nothing the host sent was malformed to the receiver", u(r.malformed));
      check(r.decodeErrors == 0 && r.decoded + 4 >= r.delivered, tag + ": every delivered AU decodes",
            "decoded " + u(r.decoded) + " of " + u(r.delivered) + " errors " + u(r.decodeErrors) + " " + r.decoder);
      if (lossPermille == 0) {
        // A seq gap on a lossless loopback is the host's: an IDR (encoder_gop) supersedes the
        // sender's two-deep backlog and those seqs are never sent. The receiver sees it as a gap
        // closed by a complete keyframe -- no request, no discontinuity (respond_to_sequence_gap).
        // Anything else -- a gap not closed by a key, a discontinuity, a request -- would be a loss.
        check(r.disc == 0 && r.keyReq == 0 && r.gaps == r.gapsBehindKey,
              tag + ": lossless -> in order, no discontinuity, no keyframe request, every seq gap is the"
                    " host's keyframe supersede (closed by a complete IDR)",
              "gaps " + u(r.gaps) + " behindKey " + u(r.gapsBehindKey) + " maxGap " + u(r.maxGap) + " disc " +
                  u(r.disc) + " keyReq " + u(r.keyReq));
        check(r.rxDropped == 0 && r.nacks == 0 && r.fec == 0, tag + ": lossless -> nothing dropped, no NACK, no repair");
        // What the host says it sent and what arrived here agree on the parity bytes (both count
        // datagram payload; the stats line is at most a second behind the receiver, so the
        // allowance is a second and a half of this run's parity rate, not a share of the total --
        // a share is too tight for a short run, where one second is a large share).
        const uint64_t hp = r.host.parity;
        const uint64_t rp = r.rxParity;
        const uint64_t allowance = (std::max(hp, rp) * 3) / (static_cast<uint64_t>(seconds) * 2) + 4096;
        check(hp > 0 && rp > 0 && (hp <= rp + allowance) && (rp <= hp + allowance),
              tag + ": the host's parity account and the received parity agree (within a second of stats)",
              "host " + u(hp) + " received " + u(rp));
      }
      if (tight && content != Content::Video) {
        check(r.singleChunkFrames > 0, tag + ": single-chunk frames occurred (the case this work is about)",
              u(r.singleChunkFrames));
      }
    }
    const RunResult& padded = res[0];
    const RunResult& tight = res[1];
    const std::string tag = std::string(cls);
    // Per frame the host sent (its cumulative udpTxFrames), because two live captures never send
    // the same number of frames in the same seconds.
    const double padParityPerFrame = padded.host.txFrames ? double(padded.host.parity) / padded.host.txFrames : 0;
    const double tightParityPerFrame = tight.host.txFrames ? double(tight.host.parity) / tight.host.txFrames : 0;
    const double padWirePerFrame = padded.host.txFrames ? double(padded.host.wireEst) / padded.host.txFrames : 0;
    const double tightWirePerFrame = tight.host.txFrames ? double(tight.host.wireEst) / tight.host.txFrames : 0;
    // The wire as a multiple of the payload it carried: on a still screen one 64 KB IDR is most
    // of the 12 seconds' bytes and the frame count is ten or eleven, so "per frame" swings 10%
    // on whether the last refresh frame made the last stats line. Numerator and denominator
    // here cover the same frames.
    const double padOverhead = padded.host.txBytes ? double(padded.host.wireEst) / padded.host.txBytes : 0;
    const double tightOverhead = tight.host.txBytes ? double(tight.host.wireEst) / tight.host.txBytes : 0;
    std::printf("COMPARE content=%s padded: frames=%llu payload=%llu parity=%llu wireEst=%llu parity/frame=%.0f wire/frame=%.0f"
                " wire/payload=%.3f | tight: frames=%llu payload=%llu parity=%llu wireEst=%llu parity/frame=%.0f wire/frame=%.0f"
                " wire/payload=%.3f | parity/frame ratio=%.2f wire/frame ratio=%.2f wire/payload ratio=%.3f\n",
                cls, static_cast<unsigned long long>(padded.host.txFrames), static_cast<unsigned long long>(padded.host.txBytes),
                static_cast<unsigned long long>(padded.host.parity), static_cast<unsigned long long>(padded.host.wireEst),
                padParityPerFrame, padWirePerFrame, padOverhead, static_cast<unsigned long long>(tight.host.txFrames),
                static_cast<unsigned long long>(tight.host.txBytes), static_cast<unsigned long long>(tight.host.parity),
                static_cast<unsigned long long>(tight.host.wireEst), tightParityPerFrame, tightWirePerFrame, tightOverhead,
                padParityPerFrame > 0 ? tightParityPerFrame / padParityPerFrame : 0,
                padWirePerFrame > 0 ? tightWirePerFrame / padWirePerFrame : 0,
                padOverhead > 0 ? tightOverhead / padOverhead : 0);
    if (lossPermille == 0 && padded.launched && tight.launched) {
      check(padded.host.txFrames > 0 && tight.host.txFrames > 0, tag + ": both hosts counted the frames they sent",
            u(padded.host.txFrames) + " / " + u(tight.host.txFrames));
      if (content != Content::Video) {
        check(tightParityPerFrame < padParityPerFrame, tag + ": tight sends fewer parity bytes per frame than padded",
              "tight " + std::to_string(tightParityPerFrame) + " padded " + std::to_string(padParityPerFrame));
        check(tightOverhead < padOverhead, tag + ": tight puts fewer wire bytes per payload byte than padded",
              "tight " + std::to_string(tightOverhead) + " padded " + std::to_string(padOverhead));
      } else {
        // Busy motion is multi-chunk frames: the layouts are the same bytes there, so the two live
        // captures differ only by what the encoder produced. Reported, not gated.
        std::printf("INFO video: parity/frame tight %.0f vs padded %.0f (multi-chunk frames are identical under both;"
                    " two live captures)\n", tightParityPerFrame, padParityPerFrame);
      }
    }
  }

  // Cap-ON evidence (bitrate-hard-cap r2): the same lossless scenario with the hard wire cap ON must,
  // after the progress-based tail fix, still make NO premature NACK -- the pinned cap-OFF runs above
  // keep this test's FEC-layout purpose, this run keeps the cap-ON proof. Only on a lossless path.
  if (!matrix && lossPermille == 0) {
    const uint16_t capPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    if (capPort != 0) {
      const std::wstring capDir = dir + L"capon_lossless\\";
      const RunResult cr = run_host(hostExe, capDir, Content::Video, /*tight=*/true, capPort, seconds, 0, lossSeed,
                                    /*wireCapOn=*/true);
      print_run(Content::Video, true, 0, lossSeed, cr);
      check(cr.delivered >= 2 && cr.decodeErrors == 0,
            "cap ON lossless: frames delivered and decoded", "delivered " + u(cr.delivered) + " decErr " + u(cr.decodeErrors));
      check(cr.nacks == 0,
            "cap ON lossless: NO premature NACK (the r2 progress-based tail holds under the cap on real hardware)",
            "rxNacks " + u(cr.nacks));
    }
  }

  MFShutdown();
  WSACleanup();
  if (keepDir) std::printf("kept %s\n", std::string(dir.begin(), dir.end()).c_str());
  else check(staging.Remove(), "the staging directory is removed", staging.why());
  if (gFails != 0) {
    std::printf("fec_single_chunk_host_e2e_test: FAIL (%d of %d checks)\n", gFails, gCheckCount);
    return 1;
  }
  std::printf("fec_single_chunk_host_e2e_test: PASS (%d checks)\n", gCheckCount);
  return 0;
}

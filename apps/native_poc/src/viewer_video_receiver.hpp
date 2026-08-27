#pragma once

// The viewer's video receive thread: reads the media socket, reassembles / gates / decodes frames
// and publishes them to the FrameBuffer.
//
// Role:    Run() is the former recvThread lambda of main(): the UDP datagram loop (control tunnel
//          tick, cursor packets, FEC assembly, sim drop) or the TCP message loop (raw BGRA / H.264),
//          process_h264_frame (selection gate, stale / congestion / keyframe-wait gating, decode,
//          publish, trace) and the once-a-second stats line.
// Thread:  recv only. Owns RecvStats; writes FrameBuffer under its mutex, the remote cursor sample,
//          the client metrics; reads the selection gate, picker visibility and present counters.
// Input:   the media socket (gSession.sock), DecoderState, FrameGateState, Args.
// Output:  FrameBuffer frames, metrics, stats/telemetry lines, keyframe requests.
// Callers: main() (std::thread recvThread([&]{ receiver.Run(); })).
//
// Bodies are the lambda bodies of native_video_client_main.cpp, verbatim (viewer split refactor
// Phase 2-1); the captured state became members with the same names so the code reads unchanged.

#include "viewer_args.hpp"
#include "viewer_common.hpp"
#include "viewer_decoder_state.hpp"
#include "viewer_frame_gate_state.hpp"
#include "viewer_globals.hpp"
#include "viewer_recv_stats.hpp"

namespace remote60::native_poc::viewer {

class VideoReceiver {
 public:
  VideoReceiver(const Args& args, DecoderState& dec, FrameGateState& gate, uint64_t startUs,
                uint32_t udpSimDropPm, uint32_t udpSimDropSeed)
      : args(args), dec(dec), gate(gate), startUs(startUs), udpSimDropPm(udpSimDropPm),
        udpSimDropSeed(udpSimDropSeed) {}
  // The thread body (formerly the recvThread lambda).
  void Run();

 private:
  // captured by reference in the monolith; same names so the bodies read unchanged
  const Args& args;
  DecoderState& dec;
  FrameGateState& gate;
  const uint64_t startUs;
  const uint32_t udpSimDropPm;
  const uint32_t udpSimDropSeed;
  RecvStats st;

  // the helper lambdas, now members (verbatim bodies)
  uint32_t queue_depth_frames(uint64_t lagUs);
  void sample_queue_depth(uint64_t lagUs);
  void transition_congestion_state(ClientCongestionState nextState, uint64_t nowUs, const char* reason, uint64_t streamLagUs, uint64_t decodeQueueLagEstimateUs, uint32_t seq);
  PresentCounterSnapshot load_present_counters();
  void append_present_counter_fields(std::ostream& os);
  void append_congestion_fields(std::ostream& os);
  uint64_t aligned_lag_us(uint64_t remoteTsUs, uint64_t localNowUs, bool& timelineReady, uint64_t& remoteBaseUs, uint64_t& localBaseUs);
  void publish_metrics(uint32_t metricW, uint32_t metricH, uint64_t nowUs, uint64_t avgLatencyUs, uint64_t maxLatencyUsLocal, uint64_t avgDecodeTailUs, uint64_t maxDecodeTailUsLocal, double mbpsLocal);
  bool process_h264_frame(const EncodedFrameHeader& h, std::vector<uint8_t>* payloadPtr, uint64_t packetNowUs);
  void run_udp();
  void run_tcp();
};

}  // namespace remote60::native_poc::viewer

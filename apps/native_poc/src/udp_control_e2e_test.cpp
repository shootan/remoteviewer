// Connects a real client session to a running host with control tunnelled through the video
// socket, which is the only path a host behind NAT can offer. Verifies the parts of the
// session that would be dead if the tunnel were broken: window list, selection, and video.
//
// Usage: remote60_udp_control_e2e_test <host> <videoPort>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "native_video_client_session.hpp"

using namespace remote60::native_poc;

namespace {

int gFailures = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!cond) ++gFailures;
}

class CountingSink : public ClientEncodedFrameSink {
 public:
  void OnEncodedH264Frame(UdpH264AssembledFrame&& frame) override {
    bytes_.fetch_add(frame.payload.size(), std::memory_order_relaxed);
    frames_.fetch_add(1, std::memory_order_relaxed);
    // bit0 of the encoded header flags is "this AU is a keyframe". Counting them is how a
    // keyframe REQUEST is verified end to end: the request only matters if an IDR comes back.
    if ((frame.header.flags & 0x1u) != 0) keyFrames_.fetch_add(1, std::memory_order_relaxed);
  }
  void OnVideoStreamReset() override {}
  // Lets the test ask the controller for one keyframe, which is the only way to drive
  // ControlRequestKeyFrame from here. One-shot: the controller polls this every loop tick.
  bool ConsumeDecoderKeyframeRequest() override {
    return wantKeyframe_.exchange(false, std::memory_order_acq_rel);
  }
  void AskForKeyframe() { wantKeyframe_.store(true, std::memory_order_release); }
  uint64_t frames() const { return frames_.load(std::memory_order_relaxed); }
  uint64_t bytes() const { return bytes_.load(std::memory_order_relaxed); }
  uint64_t keyFrames() const { return keyFrames_.load(std::memory_order_relaxed); }

 private:
  std::atomic<uint64_t> frames_{0};
  std::atomic<uint64_t> bytes_{0};
  std::atomic<uint64_t> keyFrames_{0};
  std::atomic<bool> wantKeyframe_{false};
};

template <typename Fn>
bool wait_until(Fn&& fn, int timeoutMs) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    if (fn()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return fn();
}

}  // namespace

int main(int argc, char** argv) {
  const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
  const int videoPort = argc > 2 ? std::atoi(argv[2]) : 43000;

  CountingSink sink;
  ClientSessionController controller;

  ClientSessionConnectArgs args;
  args.host = host;
  args.videoPort = videoPort;
  // Deliberately the same port: it is never dialled, and the session rejects port 0 outright.
  args.controlPort = videoPort;
  args.requireUdpHello = true;
  args.requireTcpControl = false;
  args.controlOverUdp = true;
  args.controlIntervalMs = 200;
  args.encodedFrameSink = &sink;

  check("session connects", controller.Connect(args));

  const bool connected =
      wait_until([&] { return controller.Snapshot().state == ClientSessionState::Connected; }, 6000);
  check("reaches connected state", connected,
        controller.Snapshot().status + " / " + controller.Snapshot().lastError);

  check("control loop runs over udp", controller.Snapshot().controlLoopActive);

  // The window list only arrives if a request went out and a multi-kilobyte response came back
  // intact, so this single check covers both directions of the tunnel.
  const bool listArrived =
      wait_until([&] { return controller.Snapshot().latestWindowListCount > 0; }, 10000);
  check("window list arrives over the tunnel", listArrived,
        "status=" + controller.Snapshot().status);

  const bool selected = controller.RequestDesktopMode() &&
                        wait_until([&] {
                          return controller.Snapshot().status.find("window_selected") !=
                                 std::string::npos;
                        }, 8000);
  check("desktop selection round-trips", selected, "status=" + controller.Snapshot().status);

  // Streaming is opt-in, and the request itself is a control message, so this doubles as a
  // check that client-initiated control reaches the host.
  check("stream start request accepted", controller.RequestStreamActive(true));

  // An idle desktop with frame gating on emits very few frames, so this asserts that video
  // arrives at all rather than at any particular rate. Evaluated before the detail string is
  // built: argument evaluation order is unspecified, and reading the counters first reported
  // zero frames next to a passing check.
  const bool videoFlowed = wait_until([&] { return sink.frames() >= 1; }, 20000);
  check("video still flows while control shares the socket", videoFlowed,
        "frames=" + std::to_string(sink.frames()) + " bytes=" + std::to_string(sink.bytes()));

  // Runtime bitrate changes used to leave the encoder with a hardcoded 110%/130% peak and a
  // stale pacing budget. The host applies these asynchronously; the assertion here is that
  // both requests go out and the session survives them, and the host log carries the
  // "rate-control reason=reconfigure" and "pacing update" lines a harness can grep.
  // The control loop ticks every 200ms; disconnecting immediately after the request would
  // tear the session down before the tune message ever left, which is exactly the false-pass
  // this block exists to avoid. Two requests with a real gap also prove the second change
  // does not coalesce away the first.
  check("runtime bitrate down request accepted", controller.RequestRuntimeConfig(4000000, 30));
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  check("session survives bitrate downshift",
        controller.Snapshot().state == ClientSessionState::Connected);
  check("runtime bitrate up request accepted", controller.RequestRuntimeConfig(10000000, 30));
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  check("session survives bitrate upshift",
        controller.Snapshot().state == ClientSessionState::Connected);

  // The remaining control requests the host turns into main-loop work. Each one used to be a
  // hand-rolled `*Pending` atomic plus a scattered payload; they are MainLoopMailbox posts now
  // (Phase 4), and nothing else in this suite exercised them. The assertion here is that the
  // request goes out and the session survives it -- the host log carries the matching
  // "keyframe-request-consumed" / "monitor-select applied" / "desktop-backend-" lines a harness
  // greps for.
  // A keyframe REQUEST is only meaningful if an IDR comes back, so assert on the arriving key
  // count rather than on "the session is still connected" -- which was already true before the
  // request and therefore asserted nothing.
  //
  // This leg proves the IDR reaches the client. It does NOT by itself prove this particular IDR
  // was caused by the request: the keyint schedule can emit one too (unlikely inside the window
  // on an idle desktop, where publishes are ~1/s against a 60-frame keyint, but not impossible).
  // Causation is pinned by the harness grepping the host log for "keyframe-request-consumed"
  // (automation/host_udp_e2e.sh). The two together cover request -> loop -> wire.
  const uint64_t keyFramesBefore = sink.keyFrames();
  sink.AskForKeyframe();
  const bool keyframeArrived =
      wait_until([&] { return sink.keyFrames() > keyFramesBefore; }, 10000);
  check("keyframe request produces an IDR", keyframeArrived,
        "before=" + std::to_string(keyFramesBefore) + " after=" + std::to_string(sink.keyFrames()));

  // The host applies these on the main loop and logs the outcome; the shell harness greps
  // host.log for "monitor-select applied" / "desktop-backend-(applied|stored)", so the assertion
  // here is that the request goes out and the session survives it.
  check("monitor select request accepted", controller.RequestMonitorSelect(0));
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  check("session survives monitor select",
        controller.Snapshot().state == ClientSessionState::Connected);

  // 2 = WGC. Requesting the backend the host may already be on is fine: the point is that the
  // request reaches the loop, and the host logs either "-applied" or "-stored".
  check("desktop backend request accepted", controller.RequestDesktopCaptureBackend(2));
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  check("session survives backend request",
        controller.Snapshot().state == ClientSessionState::Connected);

  // Input is the latency-sensitive traffic; it must survive the same path.
  bool inputOk = true;
  for (int i = 0; i < 20; ++i) {
    inputOk = inputOk && controller.QueueInputEvent(1, 100 + i, 100 + i, 0, 0, 0);
  }
  check("input events queue and drain", inputOk && wait_until([&] {
          return controller.Snapshot().state == ClientSessionState::Connected;
        }, 3000));

  const auto finalSnapshot = controller.Snapshot();
  check("session healthy at the end", finalSnapshot.state == ClientSessionState::Connected,
        finalSnapshot.status + " / " + finalSnapshot.lastError);

  controller.Disconnect();

  std::printf(gFailures == 0 ? "\nRESULT: ALL PASS\n" : "\nRESULT: %d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}

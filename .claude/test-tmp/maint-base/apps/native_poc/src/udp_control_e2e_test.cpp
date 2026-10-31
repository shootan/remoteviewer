// Connects a real client session to a running host with control tunnelled through the video
// socket, which is the only path a host behind NAT can offer. Verifies the parts of the
// session that would be dead if the tunnel were broken: window list, selection, and video.
//
// Usage: remote60_udp_control_e2e_test                     starts its own isolated loopback host
//                                                          (REMOTE60_ALLOW_HOST_E2E=1; else exit 77)
//        remote60_udp_control_e2e_test <host> <videoPort>  against a host you started

#include "e2e_isolation.hpp"

#include <vector>

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

int kSelfHostPort = 0;  // picked at run time (e2e_pick_free_udp_port) when this test starts its own host

/** The host this test starts for itself when run without arguments. */
struct SelfHost {
  std::wstring dir;
  HANDLE job = nullptr;
  PROCESS_INFORMATION pi{};
  HANDLE log = INVALID_HANDLE_VALUE;
  bool launched = false;

  bool Start(std::string* why) {
    using namespace remote60::native_poc::e2e;
    // Inside the repository's test scratch root, never %TEMP% (RV-20 r2).
    if (!staging.Create(L"udpctl")) {
      *why = staging.why();
      return false;
    }
    dir = staging.path();
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring myDir(self);
    myDir = myDir.substr(0, myDir.find_last_of(L"\\/") + 1);
    if (!CopyFileW((myDir + L"GNLinkStream.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) ||
        !CopyFileW(self, (dir + L"GNLinkCapture.exe").c_str(), FALSE)) {
      *why = "could not stage the host";
      return false;
    }
    job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_FRAME_GATING_DISABLE", L"1");
    std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                       L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(kSelfHostPort) +
                       L" --seconds 180 --input-injection-mode none";
    // Refused, not launched, unless injection is off: the checks below send mouse events.
    if (cmd.find(L" --input-injection-mode none") == std::wstring::npos) {
      *why = "refusing: the command line does not turn input injection off";
      return false;
    }
    const std::wstring isoAppData = dir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> env = e2e_isolated_environment(isoAppData);
    std::string isoWhy;
    if (!e2e_command_is_isolated(cmd, dir, &isoWhy) ||
        !e2e_path_is_under(e2e_block_localappdata(env), dir)) {
      *why = "refusing: not isolated from the user's files -- " + isoWhy;
      return false;
    }
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    log = CreateFileW((dir + L"host.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                      &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (log != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = log;
      si.hStdError = log;
    }
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    launched = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, log != INVALID_HANDLE_VALUE,
                              CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                              env.data(), dir.c_str(), &si, &pi) != 0;
    if (!launched) {
      *why = "CreateProcessW failed " + std::to_string(GetLastError());
      return false;
    }
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    Sleep(1500);  // the listener comes up before the first Hello is worth sending
    return true;
  }

  /**
   * Ends the host through the job it was put in, and removes the staging directory -- only once
   * the host has actually exited; a host still running keeps its directory, and this says so.
   */
  bool Stop() {
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    log = INVALID_HANDLE_VALUE;
    if (job) CloseHandle(job);  // kill-on-close: the host and anything it started
    job = nullptr;
    bool exited = true;
    if (pi.hProcess) {
      const DWORD w = WaitForSingleObject(pi.hProcess, 20000);
      exited = (w == WAIT_OBJECT_0);
      CloseHandle(pi.hProcess);
    }
    if (pi.hThread) CloseHandle(pi.hThread);
    pi = PROCESS_INFORMATION{};
    if (!exited) {
      staging.set_keep(true, "the host did not exit within 20 s; its staging directory is kept");
      return false;
    }
    return staging.Remove();
  }
  remote60::native_poc::e2e::StagingDir staging;
};

int main(int argc, char** argv) {
  // Both required, deliberately, and this used to default to 127.0.0.1:43000.
  //
  // That is the port the installed GNLink listens on. A bare run -- which is exactly what a
  // regression sweep does -- therefore sent a UDP hello to the user's own running host. It was
  // rejected and nothing came of it, but a test that reaches into the live product unless told
  // otherwise is wrong in the way that only shows up once. It still has no default address --
  // without arguments it now starts a host of its own instead (below).
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--thumbnail") {  // staged as GNLinkCapture.exe; answers nothing
      Sleep(120000);
      return 0;
    }
  }
  // Two ways to run. With <host> <port> it is the tool it always was, against a host somebody
  // started. With no arguments it is a TEST (C14, ledger 0.0.5): it no longer depends on whatever
  // host happens to be running, but starts its own -- loopback, staged in its own directory,
  // isolated from the user's files like every host e2e (RV-00), and with input injection OFF,
  // because this test sends mouse events and a host in desktop mode would deliver them to the
  // user's real desktop. Without REMOTE60_ALLOW_HOST_E2E=1 it skips (exit 77).
  SelfHost self;
  std::string host;
  int videoPort = 0;
  if (argc >= 3) {
    host = argv[1];
    videoPort = std::atoi(argv[2]);
    if (videoPort <= 0 || videoPort > 65535) {
      std::puts("usage: udp_control_e2e_test [<host> <video-port>]  (port out of range)");
      return 2;
    }
  } else {
    wchar_t allow[8]{};
    if (GetEnvironmentVariableW(L"REMOTE60_ALLOW_HOST_E2E", allow, 8) == 0 || allow[0] != L'1') {
      std::puts("SKIP  udp_control_e2e_test starts its own listening host when given no arguments.");
      std::puts("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it, or pass <host> <video-port> to use one");
      std::puts("      you started (never the installed product's 43000).\n\nRESULT: SKIPPED");
      return remote60::native_poc::e2e::kE2eSkippedExit;
    }
    kSelfHostPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    if (kSelfHostPort == 0) {
      std::puts("FAIL  no free UDP port for the self-started host");
      return 1;
    }
    std::printf("self host port %d (picked at run time)\n", kSelfHostPort);
    std::string why;
    check("an isolated host of its own is started (loopback, no input injection)", self.Start(&why), why);
    if (!self.launched) {
      std::printf("\nRESULT: %d FAILED\n", gFailures);
      return 1;
    }
    host = "127.0.0.1";
    videoPort = kSelfHostPort;
  }

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
  if (self.launched) check("the scratch directory is cleaned up", self.Stop());

  std::printf(gFailures == 0 ? "\nRESULT: ALL PASS\n" : "\nRESULT: %d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}

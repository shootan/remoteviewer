// A real host, a real control channel, and a break that only the control traffic feels. (item 8, C3)
//
// Everything before this measured the two halves separately: the host's decision without a viewer,
// the viewer's state machine without a host. What neither could show is the thing the feature is:
// a session whose control channel dies while its video keeps arriving, repaired in place, with the
// same session, the same NAT mapping, and no reconnect.
//
// So this runs the real GNLinkStream and puts a proxy between it and a viewer built out of the
// product's own pieces -- udp_hello_handshake, UdpControlChannel, ViewerControlResume, and the
// recovery sequence itself (ViewerControlResume::Pump). The proxy drops CONTROL datagrams only:
// video, Hello and the resume exchange itself go through untouched, so the liveness that starts a
// recovery is the real one (the channel's retransmits running out) rather than a flag this test
// set. Nothing is forced.
//
// How the result is judged. Not by "the host said ackSent=1" -- that is the host agreeing with
// itself. The host is started with input injection pointed at a window THIS PROCESS owns
// (--input-target-pid), which restricts inject_background_input_event to PostMessage into that
// window and excludes every SendInput path by construction. So a mouse event sent by the viewer
// after the repair has to arrive in this test's own message queue to count. Nothing is injected
// into the user's desktop, and nothing could be: the explicit-target branch never reaches
// SendInput (host_input_inject.cpp:586-598).
//
// Isolation, as in the sibling host e2e tests: a scratch directory, ports nobody else is on, a
// GNLinkCapture.exe of this test's choosing, a job object that takes the host when this process
// goes, and off entirely unless REMOTE60_ALLOW_HOST_E2E=1.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "e2e_isolation.hpp"
#include "control_resume.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "viewer_control_resume.hpp"
#include "viewer_recv_liveness.hpp"
#include "control_resume_e2e_support.hpp"

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;

namespace {

// -------------------------------------------------------------------------------- the viewer side

/** The viewer's own pieces, wired the way viewer_startup wires them. */
struct ViewerSide {
  SOCKET sock = INVALID_SOCKET;
  UdpControlChannel control;
  ViewerControlResume resume;
  std::unique_ptr<UdpControlLink> link;
  std::thread ingress;
  std::atomic<bool> stop{false};
  std::atomic<uint64_t> videoDatagrams{0};
  std::atomic<uint64_t> lastVideoUs{0};
  std::atomic<uint64_t> unsolicited{0};  // control messages nobody asked for
  uint32_t nextId = 0;

  bool video_alive() const {
    const uint64_t last = lastVideoUs.load();
    if (last == 0) return false;
    const uint64_t now = qpc_now_us();
    return now < last + 5000000ull;  // the same 5 s the session watchdog uses
  }

  void StartIngress() {
    ingress = std::thread([this] {
      std::vector<uint8_t> buf(2048);
      while (!stop.load()) {
        control.Tick();
        const int n = recv(sock, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
        if (n <= 0) continue;
        // Exactly the viewer's order: the resume answer is about the channel, so it is looked at
        // before the channel is fed.
        if (resume.OnDatagram(buf.data(), static_cast<size_t>(n))) continue;
        if (control.OnPacket(buf.data(), static_cast<size_t>(n))) continue;
        videoDatagrams.fetch_add(1);
        lastVideoUs.store(qpc_now_us());
      }
    });
  }

  void StopIngress() {
    stop.store(true);
    if (ingress.joinable()) ingress.join();
  }
};

bool ping(ControlLink& link, uint64_t* elapsedUs = nullptr) {
  ControlOutboundAction action;
  action.kind = ControlOutboundActionKind::Ping;
  action.ping.header.magic = kMagic;
  action.ping.header.type = static_cast<uint16_t>(MessageType::ControlPing);
  action.ping.header.size = static_cast<uint16_t>(sizeof(action.ping));
  action.ping.clientSendQpcUs = qpc_now_us();
  action.expectedResponseType = MessageType::ControlPong;
  action.expectedResponseSize = static_cast<uint16_t>(sizeof(TcpControlResponse{}.pong));
  TcpControlResponse response;
  const uint64_t start = qpc_now_us();
  const bool ok = execute_control_action(link, action, &response) &&
                  response.kind == TcpControlResponseKind::Pong;
  if (elapsedUs) *elapsedUs = qpc_now_us() - start;
  return ok;
}

bool send_input(ControlLink& link, uint16_t kind, uint32_t seq, int32_t x, int32_t y,
                uint32_t keyCode) {
  ControlOutboundAction action;
  action.kind = ControlOutboundActionKind::InputEvent;
  action.inputEvent.header.magic = kMagic;
  action.inputEvent.header.type = static_cast<uint16_t>(MessageType::ControlInputEvent);
  action.inputEvent.header.size = static_cast<uint16_t>(sizeof(action.inputEvent));
  action.inputEvent.seq = seq;
  action.inputEvent.kind = kind;
  action.inputEvent.x = x;
  action.inputEvent.y = y;
  action.inputEvent.keyCode = keyCode;
  action.inputEvent.clientSendQpcUs = qpc_now_us();
  action.expectedResponseType = MessageType::ControlInputAck;
  action.expectedResponseSize = static_cast<uint16_t>(sizeof(TcpControlResponse{}.inputAck));
  TcpControlResponse response;
  return execute_control_action(link, action, &response) &&
         response.kind == TcpControlResponseKind::InputAck;
}

ControlProxy* gProxy = nullptr;  // for the progress line only: which side of the seam went quiet
uint32_t gCase5ResumeId = 0;     // checked against the host's own log at the end

/** One recovery, driven the way the viewer's worker drives it, with the timings kept apart. */
struct RecoveryResult {
  bool resumed = false;
  bool gaveUp = false;
  bool cancelled = false;
  uint64_t breakToFirstSendUs = 0;
  uint64_t breakToRunningUs = 0;
  uint32_t attempts = 0;
  // The picture, while the repair is going on. The host used to turn the stream off the
  // moment its control session ended, which is also the moment a resume became acceptable --
  // so "did video keep arriving" is not a nicety here, it is the condition the viewer uses to
  // decide the session is worth repairing at all.
  uint64_t mediaDuring = 0;
  uint64_t proxyMediaDuring = 0;  // what the seam forwarded over the same stretch
  uint64_t proxyMediaSettled = 0;  // the seam again, after the viewer's reading: bounds the surplus
  bool videoWentQuiet = false;
  uint64_t longestVideoGapUs = 0;
};

RecoveryResult run_recovery(ViewerSide& v, int budgetMs,
                            const std::function<bool()>& cancelled = {}) {
  RecoveryResult out;
  ResumePumpHooks hooks;
  hooks.cancelled = cancelled ? cancelled : std::function<bool()>([] { return false; });
  hooks.nowUs = [] { return qpc_now_us(); };
  hooks.videoAlive = [&v] { return v.video_alive(); };
  hooks.proveChannel = [&v] {
    v.link = std::make_unique<UdpControlLink>(&v.control, 12000);
    return ping(*v.link);
  };
  hooks.onResumed = [&out](uint64_t toFirstSend, uint64_t toRunning) {
    out.breakToFirstSendUs = toFirstSend;
    out.breakToRunningUs = toRunning;
  };
  hooks.idle = [] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); };

  const uint64_t mediaAtStart = v.videoDatagrams.load();
  const uint64_t proxyAtStart = gProxy ? gProxy->mediaPassed.load() : 0;
  const DWORD deadline = GetTickCount() + static_cast<DWORD>(budgetMs);
  DWORD nextReport = GetTickCount();
  while (GetTickCount() < deadline) {
    {
      const uint64_t last = v.lastVideoUs.load();
      const uint64_t gap = last ? (qpc_now_us() - last) : 0;
      if (gap > out.longestVideoGapUs) out.longestVideoGapUs = gap;
      if (!v.video_alive()) out.videoWentQuiet = true;
    }
    if (GetTickCount() >= nextReport) {
      // Printed while it runs, because a recovery that stalls and one that is merely slow
      // look identical in a final verdict. videoAge is the one that decides whether it asks
      // at all.
      nextReport = GetTickCount() + 2000;
      const uint64_t last = v.lastVideoUs.load();
      const uint64_t age = last ? (qpc_now_us() - last) : 0;
      std::cout << "    ...attempts=" << v.resume.attempts()
                << " media=" << v.videoDatagrams.load()
                << " proxyMedia=" << (gProxy ? gProxy->mediaPassed.load() : 0)
                << " videoAgeMs=" << (last ? age / 1000 : 0)
                << " videoAlive=" << (v.video_alive() ? 1 : 0)
                << " refused=" << v.resume.refused_acks()
                << " ignored=" << v.resume.ignored_acks()
                << " rekeyed=" << (v.resume.rekeyed() ? 1 : 0) << "\n";
    }
    const ResumePumpResult r = v.resume.Pump(hooks);
    if (r == ResumePumpResult::Resumed) {
      out.resumed = true;
      break;
    }
    if (r == ResumePumpResult::GaveUp) {
      out.gaveUp = true;
      break;
    }
    if (r == ResumePumpResult::Cancelled) {
      out.cancelled = true;
      break;
    }
  }
  out.attempts = v.resume.attempts();
  // The two counters are read at different moments, so the order and the wait are the whole
  // point. (RV-16) The seam's count is taken FIRST, then datagrams still between the seam and the
  // viewer are given time to land, and only then is the viewer's count read. Everything the seam
  // had forwarded by its reading has therefore had its chance to arrive, and the comparison is
  // "received >= forwarded": the viewer may also hold a few forwarded after the seam's reading
  // (and, at the start, a few in flight before it), which is surplus, never loss.
  out.proxyMediaDuring = gProxy ? gProxy->mediaPassed.load() - proxyAtStart : 0;
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  out.mediaDuring = v.videoDatagrams.load() - mediaAtStart;
  out.proxyMediaSettled = gProxy ? gProxy->mediaPassed.load() - proxyAtStart : 0;
  return out;
}

/**
 * C4 (RV-02): how far the repair reaches. One direction is cut COMPLETELY -- video, control,
 * resume asks and answers -- for a fixed time, and the viewer side does what the product's worker
 * does: keep exchanging until one exchange fails, then ask for ONE resume, and only if the picture
 * is alive at that moment (viewer_control_client.cpp begin_control_resume). This is a
 * measurement, not a judgement: it prints one RANGE line and changes nothing in the product.
 *
 * What it does not reproduce, said so the table is read correctly: the product's watchdog is not
 * running here. Its verdict is computed from the product's own thresholds (SessionLivenessConfig)
 * against what was measured -- "control gone and no picture for deadSessionUs" ends a session,
 * and so does "control gone for controlGoneWithVideoUs" even with a picture.
 */
void run_range_trial(ViewerSide& v, ControlProxy& proxy, bool up, int seconds) {
  std::cout << "\n--- range: the " << (up ? "UPLINK (viewer -> host)" : "DOWNLINK (host -> viewer)")
            << " fully cut for " << seconds << " s ---\n";
  const viewer::SessionLivenessConfig live{};
  std::atomic<bool>& cut = up ? proxy.dropAllUp : proxy.dropAllDown;
  const uint64_t cutStart = qpc_now_us();
  const uint64_t cutEndUs = static_cast<uint64_t>(seconds) * 1000000ull;
  cut.store(true);
  std::thread restorer([&cut, seconds] {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    cut.store(false);
  });
  const auto since_cut = [cutStart] { return qpc_now_us() - cutStart; };

  // The worker: one exchange after another until one fails, or until the cut is long over.
  bool failed = false;
  uint64_t failedAtUs = 0;
  while (since_cut() < cutEndUs + 8000000ull) {
    if (!ping(*v.link)) {
      failed = true;
      failedAtUs = since_cut();
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  std::string outcome;
  std::string watchdog = "not applicable (control never went down)";
  uint64_t resumedAtUs = 0;
  uint64_t longestGapUs = 0;
  bool aliveAtFailure = false;
  if (failed) {
    aliveAtFailure = v.video_alive();
    if (!v.resume.BeginBreakIfPossible(qpc_now_us(), aliveAtFailure, false)) {
      outcome = "NOT RESUMABLE -- the picture was not alive when the exchange failed; the product "
                "ends control here and does not ask again";
      // How long the picture stays away while control is gone, to say which watchdog rule ends it.
      while (since_cut() < cutEndUs + 8000000ull) {
        const uint64_t last = v.lastVideoUs.load();
        const uint64_t gap = last ? qpc_now_us() - last : 0;
        if (gap > longestGapUs) longestGapUs = gap;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      watchdog = longestGapUs > live.deadSessionUs
                     ? "CLOSES the session (no picture for " + std::to_string(longestGapUs / 1000) +
                           "ms > deadSession " + std::to_string(live.deadSessionUs / 1000) + "ms)"
                     : "closes the session at the latest after controlGoneWithVideo " +
                           std::to_string(live.controlGoneWithVideoUs / 1000) +
                           "ms -- control never comes back";
    } else {
      const RecoveryResult r = run_recovery(v, 45000);
      longestGapUs = r.longestVideoGapUs;
      if (r.resumed) {
        resumedAtUs = since_cut();
        outcome = "RESUMED";
      } else {
        outcome = r.gaveUp ? "GAVE UP (the viewer's own ceiling)" : "NOT RESUMED within 45 s";
      }
      watchdog = longestGapUs > live.deadSessionUs
                     ? "would CLOSE the session first (picture gap " +
                           std::to_string(longestGapUs / 1000) + "ms > deadSession " +
                           std::to_string(live.deadSessionUs / 1000) + "ms)"
                     : "does not intervene (longest picture gap " +
                           std::to_string(longestGapUs / 1000) + "ms)";
    }
  } else {
    outcome = "SURVIVED -- no exchange failed, so nothing needed repairing";
  }
  restorer.join();
  std::cout << "RANGE  dir=" << (up ? "up" : "down") << " cut=" << seconds << "s"
            << "  exchangeFailedAtMs=" << (failed ? std::to_string(failedAtUs / 1000) : "-")
            << "  pictureAliveThen=" << (failed ? (aliveAtFailure ? "yes" : "no") : "-")
            << "  controlBackAtMs=" << (resumedAtUs ? std::to_string(resumedAtUs / 1000) : "-")
            << "  dropped=" << proxy.allDropped.load() << "\n"
            << "       outcome: " << outcome << "\n"
            << "       watchdog (by the product's thresholds): " << watchdog << "\n";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::cout.setf(std::ios::unitbuf);

  if (!host_e2e_allowed()) {
    std::printf("SKIP  control_resume_e2e_test (starts a listening host)\n");
    std::printf("      This test starts a real GNLinkStream, which listens, which makes Windows\n");
    std::printf("      ask about the firewall -- and an unanswered prompt becomes a Block rule on\n");
    std::printf("      the user's machine. Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n");
    std::printf("\nRESULT: SKIPPED\n");
    return remote60::native_poc::e2e::kE2eSkippedExit;
  }
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--thumbnail") {  // staged as GNLinkCapture.exe; answers nothing
      Sleep(300000);
      return 0;
    }
  }

  // C4 (RV-02): `--range up|down <seconds>` runs the same setup and then ONE range trial instead
  // of the cases -- one fresh host per trial, so no trial inherits another's broken session.
  bool rangeMode = false;
  bool rangeUp = true;
  int rangeSeconds = 0;
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--range" && i + 2 < argc) {
      rangeMode = true;
      rangeUp = std::wstring(argv[i + 1]) == L"up";
      rangeSeconds = _wtoi(argv[i + 2]);
    }
  }
  if (rangeMode && (rangeSeconds <= 0 || rangeSeconds > 120)) {
    std::printf("usage: --range up|down <seconds 1..120>\n");
    return 2;
  }

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

  // Picked at run time so this test and its neighbours can run side by side (RV-19).
  const std::vector<uint16_t> ports = remote60::native_poc::e2e::e2e_pick_free_ports(SOCK_DGRAM, 2);
  if (ports.size() != 2) {
    std::printf("FAIL  no free UDP ports for the host and the proxy\n");
    return 1;
  }
  const uint16_t hostPort = ports[0];
  const uint16_t proxyPort = ports[1];
  std::printf("ports host %u proxy %u (picked at run time)\n", hostPort, proxyPort);

  // Staged inside the repository's test scratch root, never %TEMP%; removed when main returns,
  // whichever way it returns (RV-20 r2). Only the directory this process created is ever removed.
  remote60::native_poc::e2e::StagingDir staging;
  if (!staging.Create(L"c3_resume")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  const std::wstring dir = staging.path();

  const std::wstring me = self_path();
  const std::wstring myDir = directory_of(me);
  const bool staged =
      CopyFileW((myDir + L"GNLinkStream.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) &&
      CopyFileW(me.c_str(), (dir + L"GNLinkCapture.exe").c_str(), FALSE);
  check("a host and a never-answering helper could be staged", staged);

  InjectTarget target;
  check("a window of this process is up for the host to inject into", target.Start());

  ControlProxy proxy;
  gProxy = &proxy;
  check("the control seam is listening between the viewer and the host",
        proxy.Start(proxyPort, hostPort));

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  const std::wstring hostLogPath = dir + L"host.log";
  HANDLE hostLog = INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION hostPi{};
  bool launched = false;
  if (staged) {
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
    // Keep frames coming on a desktop nobody is touching.
    //
    // This is not papering over a failure -- it is the difference between the situation
    // the feature exists for and a different one. A resume is asked for only while video
    // is still arriving, because control gone AND video gone is a session that is ending,
    // and the host quite correctly sends nothing at all when the screen has not changed.
    // An unattended run has a perfectly static screen, so without this the test would be
    // measuring the no-video case and calling it the video case. Two env knobs the host
    // already has, set on the child only; neither touches the resume path, and nothing is
    // drawn on the user's screen to produce the change artificially.
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_FRAME_GATING_DISABLE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_STATIC_SCENE_FPS", L"15");
    // --input-target-pid is what keeps this safe as well as observable: with an explicit target,
    // inject_background_input_event resolves a window of THIS process and PostMessages into it,
    // and every SendInput path is behind the desktop-mode branch it no longer takes.
    std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                       L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(hostPort) +
                       L" --seconds 300 --enable-input-injection" +
                       L" --input-injection-mode background_message" + L" --input-target-pid " +
                       std::to_wstring(GetCurrentProcessId()) +
                       // Capture the window this process owns and keeps repainting, rather
                       // than a monitor nobody is touching. A still monitor produces roughly
                       // one frame every two seconds, and such a gap can span an entire
                       // repair -- which is what made the continuity assertion fail once in
                       // nine runs, for a reason that was never about the product. Nothing is
                       // drawn on the user's screen to achieve this. (C3 r5)
                       remote60::native_poc::e2e::e2e_capture_window_args(target.title);
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    // The host's own account of what it did, kept so the timeline can be read rather than
    // guessed at. When a repair does not complete, the question is always whether the host
    // refused, answered, or had already stopped serving -- and only its log says which.
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    hostLog = CreateFileW(hostLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hostLog != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = hostLog;
      si.hStdError = hostLog;
      si.hStdInput = nullptr;
    }
    // Isolated from the user's machine (RV-00): the child gets a LOCALAPPDATA inside this
    // test's staging directory, so the diagnostic mirror and anything else that follows the
    // variable writes there. Refused outright if the command line could reach the real files.
    const std::wstring isoAppData = dir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> isoEnv = remote60::native_poc::e2e::e2e_isolated_environment(isoAppData);
    std::string isoWhy;
    const bool isoOk =
        remote60::native_poc::e2e::e2e_command_is_isolated(cmd, dir, &isoWhy) &&
        remote60::native_poc::e2e::e2e_path_is_under(
            remote60::native_poc::e2e::e2e_block_localappdata(isoEnv), dir);
    check("the launched process is isolated from the user's files", isoOk, isoWhy);
    launched = isoOk &&
               CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr,
                              hostLog != INVALID_HANDLE_VALUE,
                              CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                              isoEnv.data(), dir.c_str(), &si, &hostPi) != 0;
    if (launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  check("the host started", launched);

  // The continuity checks below depend on the host capturing the window this process paints --
  // asked for with --capture-window-title, but a title that matches nothing makes the host fall
  // back to the monitor without failing. So the host's own log has to name THIS window: the hwnd
  // and pid it chose, not just the absence of the fallback line. (RV-16) Judged from the log once
  // the host has exited: its stdout into a file is block-buffered, so reading it while the host
  // runs found nothing on the first try even though the line had been written at startup.
  std::string captureWant;
  {
    char want[64]{};
    std::snprintf(want, sizeof(want), "capture-window target hwnd=0x%llx pid=%lu ",
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(target.hwnd())),
                  static_cast<unsigned long>(GetCurrentProcessId()));
    if (target.hwnd()) captureWant = want;
  }

  ViewerSide viewer;
  bool connected = false;
  bool repaired = false;  // case 1 got the channel back; the later cases need that
  // At this scope on purpose: the resume component holds a reference to it for the whole
  // run, and the first version declared it inside the connect block -- a dangling
  // reference the moment the block ended.
  uint64_t generation = 1;

  if (launched) {
    viewer.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in proxyAddr{};
    proxyAddr.sin_family = AF_INET;
    proxyAddr.sin_port = htons(proxyPort);
    InetPtonW(AF_INET, L"127.0.0.1", &proxyAddr.sin_addr);
    connect(viewer.sock, reinterpret_cast<const sockaddr*>(&proxyAddr), sizeof(proxyAddr));
    DWORD rcvTimeout = 50;
    setsockopt(viewer.sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout),
               sizeof(rcvTimeout));
    int rcvBuf = 1 << 20;
    setsockopt(viewer.sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf),
               sizeof(rcvBuf));

    UdpHelloOptions hello;
    hello.budgetMs = 20000;
    hello.sliceMaxMs = 250;
    hello.retrySleepMs = 50;
    hello.requestControlResume = true;  // the viewer's own option, as viewer_udp_session sets it
    std::string helloError;
    uint32_t ackFeatures = 0;
    const bool handshake =
        udp_hello_handshake(viewer.sock, hello, nullptr, &helloError, &ackFeatures, nullptr);
    check("the viewer's Hello is answered through the seam", handshake, helloError);
    check("...and the host offers control resume",
          (ackFeatures & kUdpFeatureControlResume) != 0,
          "features=" + std::to_string(ackFeatures) + " resumeBit=" +
              std::to_string(kUdpFeatureControlResume));

    viewer.control.Configure(
        [&viewer](const void* data, size_t len) {
          return send(viewer.sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, 1200);
    viewer.StartIngress();

    sockaddr_in peer{};
    int peerLen = sizeof(peer);
    getpeername(viewer.sock, reinterpret_cast<sockaddr*>(&peer), &peerLen);
    ViewerControlResume::Config cfg;
    cfg.decide.giveUpAfterUs = viewer::SessionLivenessConfig{}.controlGoneWithVideoUs;
    viewer.nextId = 0x5100;
    viewer.resume.Configure(
        &viewer.control,
        [&viewer](const void* data, size_t len) {
          return send(viewer.sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        [](const std::string& line) { std::cout << "    " << line << "\n"; },
        [&viewer]() { return ++viewer.nextId; }, [&generation]() { return generation; }, cfg,
        (ackFeatures & kUdpFeatureControlResume) != 0, peer.sin_addr.s_addr, peer.sin_port);

    // The product's own read timeout (viewer_constants.hpp kUdpControlReadTimeoutMs). It
    // has to be LONGER than the channel's retransmit budget, or a failed exchange means
    // "the link was slow" rather than "the channel gave up" -- and the first run of this
    // test used 5 s, which is under the ~6 s budget, so the break it measured was its own
    // impatience.
    viewer.link = std::make_unique<UdpControlLink>(&viewer.control, 12000);
    connected = wait_until([&] { return ping(*viewer.link); }, 20000);
    check("control answers before anything is broken", connected);
    const bool videoUp = wait_until([&] { return viewer.video_alive(); }, 20000);
    check("...and video is arriving", videoUp,
          std::to_string(viewer.videoDatagrams.load()) + " media datagrams");

    if (connected) {
      // The baseline for the judgement at the end: an input sent now reaches this process.
      const uint64_t movesBefore = target.mouseMoves.load();
      const bool acked = send_input(*viewer.link, /*mouse_move=*/1, 1, 40, 30, 0);
      const bool landed = wait_until([&] { return target.mouseMoves.load() > movesBefore; }, 5000);
      check("an input sent before the break is acknowledged", acked);
      check("...AND ARRIVES IN THIS PROCESS'S WINDOW", landed,
            "the host injected it; ackSent alone would not have shown that");
      if (!landed) {
        std::cout << "    (no injection observed -- the remaining input judgements will say so)\n";
      }
    }
  }

  if (rangeMode && connected) {
    run_range_trial(viewer, proxy, rangeUp, rangeSeconds);
    connected = false;  // the cases below are not part of a range run
  }

  // C5: a Hello the host REFUSES must not change the running session. Before the fix the host
  // stored "did this client ask for resume" from every Hello before deciding whether to accept it,
  // so a refused one without the resume bit switched resume off for the session in progress --
  // and case 1 below would then fail. This one carries a capability the host does not hold, from
  // another socket, straight to the host; the host log is checked at the end for the refusal.
  bool strangerHelloSent = false;
  if (connected) {
    SOCKET stranger = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_port = htons(hostPort);
    InetPtonW(AF_INET, L"127.0.0.1", &hostAddr.sin_addr);
    UdpHelloPacket refused{};  // default features: no resume bit
    std::memcpy(refused.authToken, "c5-not-a-capability-this-host-has", 32);
    for (int i = 0; i < 3; ++i) {
      if (sendto(stranger, reinterpret_cast<const char*>(&refused), sizeof(refused), 0,
                 reinterpret_cast<const sockaddr*>(&hostAddr), sizeof(hostAddr)) > 0) {
        strangerHelloSent = true;
      }
      Sleep(50);
    }
    closesocket(stranger);
    Sleep(200);
    check("[C5] a Hello the host will refuse is sent before the break", strangerHelloSent);
  }

  // ----------------------------------------------------------------- the break, and the repair
  if (connected) {
    std::cout << "\n--- case 1: control is cut in both directions, video keeps flowing ---\n";
    proxy.capturing.store(true);
    const uint64_t mediaBefore = proxy.mediaPassed.load();
    proxy.dropControlUp.store(true);
    proxy.dropControlDown.store(true);

    // The real liveness: keep asking until the channel's own retransmits run out. Nothing here
    // tells the viewer the link is dead -- it finds out.
    const uint64_t breakStartUs = qpc_now_us();
    bool noticed = false;
    while (qpc_now_us() - breakStartUs < 20000000ull) {
      if (!ping(*viewer.link)) {
        noticed = true;
        break;
      }
    }
    const uint64_t noticedUs = qpc_now_us() - breakStartUs;
    check("the viewer's control channel declares itself dead on its own",
          noticed && viewer.control.IsClosed(),
          std::to_string(noticedUs / 1000) + "ms, reason=" +
              std::string(to_string(viewer.control.CloseReason())));
    check("...while video kept arriving through the same socket",
          proxy.mediaPassed.load() > mediaBefore && viewer.video_alive(),
          std::to_string(proxy.mediaPassed.load() - mediaBefore) + " media datagrams during it");

    const uint64_t asksBefore = proxy.resumeAsksPassed.load();
    check("a recovery starts", viewer.resume.BeginBreakIfPossible(qpc_now_us(),
                                                                  viewer.video_alive(), false));
    // The uplink comes back. Control datagrams flow again; the session never went away.
    proxy.dropControlUp.store(false);
    proxy.dropControlDown.store(false);
    const RecoveryResult r = run_recovery(viewer, 28000);
    repaired = r.resumed;
    check("THE CONTROL CHANNEL COMES BACK WITHOUT A RECONNECT", r.resumed,
          r.resumed ? (std::to_string(r.attempts) + " asks")
                    : (r.gaveUp ? "gave up" : (r.cancelled ? "cancelled" : "timed out")));
    check("...and the asks went out over the media socket",
          proxy.resumeAsksPassed.load() > asksBefore,
          std::to_string(proxy.resumeAsksPassed.load() - asksBefore) + " resume datagrams");
    std::cout << "    timing: breakToFirstSend=" << (r.breakToFirstSendUs / 1000)
              << "ms  breakToRunning=" << (r.breakToRunningUs / 1000) << "ms  attempts="
              << r.attempts << "\n";
    check("...inside the thirty second budget, which did not move",
          r.resumed && r.breakToRunningUs < 30000000ull,
          std::to_string(r.breakToRunningUs / 1000) + "ms of 30000ms");
    check("...and the first ask went out long before the repair finished",
          r.resumed && r.breakToFirstSendUs <= r.breakToRunningUs,
          "the ask is immediate; whatever follows is the host deciding");
    // The condition the whole feature stands on, now that a resume can interrupt a serve:
    // the picture does not blink while control is being rebuilt.
    check("THE PICTURE KEPT ARRIVING THROUGHOUT THE REPAIR", r.resumed && !r.videoWentQuiet,
          std::to_string(r.mediaDuring) + " media datagrams during it, longest gap " +
              std::to_string(r.longestVideoGapUs / 1000) + "ms");
    // The question C3 can actually answer is not "did the host send video" -- that depends on
    // whether anything on the captured screen changed -- but "did the repair swallow any of
    // what it did send". Asked as a comparison across the seam, which is the only place both
    // numbers exist. When the host sent nothing at all there is nothing to judge, and this
    // says so instead of passing or failing on an accident of screen activity.
    if (r.proxyMediaDuring == 0) {
      skip("NOTHING THE HOST SENT DURING THE REPAIR WAS SWALLOWED",
           "the host sent nothing during the repair (a still capture source)");
    } else {
      check("NOTHING THE HOST SENT DURING THE REPAIR WAS SWALLOWED",
            r.mediaDuring >= r.proxyMediaDuring,
            std::to_string(r.mediaDuring) + " received; seam " +
                std::to_string(r.proxyMediaDuring) + " before the viewer's reading, " +
                std::to_string(r.proxyMediaSettled) + " after it");
    }

    if (r.resumed) {
      // Three real round trips, on the channel that was just re-keyed.
      int ok = 0;
      for (int i = 0; i < 3; ++i) {
        uint64_t us = 0;
        if (ping(*viewer.link, &us)) ++ok;
      }
      check("THREE REAL ROUND TRIPS ON THE RESUMED CHANNEL", ok == 3,
            std::to_string(ok) + "/3");

      // And the judgement that matters: a remote input, after the repair, observed arriving.
      const uint64_t movesBefore = target.mouseMoves.load();
      const bool acked = send_input(*viewer.link, 1, 2, 60, 45, 0);
      const bool landed = wait_until([&] { return target.mouseMoves.load() > movesBefore; }, 5000);
      check("input works again after the repair (host acknowledged)", acked);
      check("...AND THE HOST ACTUALLY INJECTED IT", landed,
            "judged by this process's own message queue, not by ackSent");

      // A key-up for a key the host believes is held: the release-all contract's payload, over
      // the resumed channel. (The viewer-side trigger for release-all is the window message the
      // worker posts; that wiring is not exercised here -- see the report.)
      const uint64_t upsBefore = target.keyUps.load();
      (void)send_input(*viewer.link, /*key_up=*/6, 3, 0, 0, 'A');
      check("a key-up sent after the repair reaches the host's injection",
            wait_until([&] { return target.keyUps.load() > upsBefore; }, 5000),
            "this is what stops a modifier stranding when the break swallowed the real one");

      // Nothing that was in flight when control broke comes back to haunt the new stream.
      proxy.ReplayCapturedDown(viewer.sock);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      uint64_t us = 0;
      check("A REPLAY OF PRE-BREAK CONTROL DATAGRAMS DOES NOT DISTURB THE RESUMED CHANNEL",
            ping(*viewer.link, &us),
            "the re-key gave it stream ids those datagrams do not carry");
    }
    proxy.capturing.store(false);
  }

  // ------------------------------------- a resume that arrives at a channel which is FINE
  //
  // Now that an ask can interrupt a serve, the question "on what evidence" has teeth. These
  // two send asks at a healthy session -- one invented, one an old one replayed -- and the
  // requirement is that nothing happens to it. This is the case a silence test would fail.
  if (connected && repaired) {
    std::cout << "\n--- counter-examples: asks at a healthy channel ---\n";
    uint64_t before = 0;
    check("the channel is healthy to begin with", ping(*viewer.link, &before));
    const uint64_t mediaBefore = viewer.videoDatagrams.load();
    // The stream ids in force, to compare after the asks: a re-key on either side changes them.
    const UdpControlChannel::StreamPair idsBefore = viewer.control.StreamIds();

    // ⑴ An ask with an id nobody has used, from the bound endpoint, while the host serves.
    UdpControlResumePacket forged{};
    forged.kind = static_cast<uint16_t>(UdpPacketKind::ControlResume);
    forged.streamId = kUdpControlStreamClientToHost;
    forged.resumeId = 0xC0FFEEu;
    for (int i = 0; i < 8; ++i) {
      (void)send(viewer.sock, reinterpret_cast<const char*>(&forged), sizeof(forged), 0);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    int ok = 0;
    for (int i = 0; i < 3; ++i) {
      if (ping(*viewer.link)) ++ok;
    }
    check("[counter-example 1] A HEALTHY CHANNEL SURVIVES EIGHT UNINVITED ASKS", ok == 3,
          std::to_string(ok) + "/3 round trips after them");
    // Was `!viewer.resume.rekeyed()`, which cannot fail here: with no recovery in progress there
    // is no episode to be re-keyed, so it read false whatever happened. (RV-16) The ids the channel
    // is actually using can change, and a re-key is exactly the thing that changes them.
    const UdpControlChannel::StreamPair idsAfter = viewer.control.StreamIds();
    check("...and the viewer's channel is on the same stream ids -- nothing re-keyed it",
          idsAfter.tx == idsBefore.tx && idsAfter.rx == idsBefore.rx,
          "tx " + std::to_string(idsBefore.tx) + "->" + std::to_string(idsAfter.tx) + " rx " +
              std::to_string(idsBefore.rx) + "->" + std::to_string(idsAfter.rx));
    check("...and video never stopped", viewer.videoDatagrams.load() > mediaBefore,
          std::to_string(viewer.videoDatagrams.load() - mediaBefore) + " media datagrams");

    // ⑵ The real ask from the previous break, replayed. Its id is one the host has answered.
    if (proxy.ReplayLastResumeAsk()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      int again = 0;
      for (int i = 0; i < 3; ++i) {
        if (ping(*viewer.link)) ++again;
      }
      check("[counter-example 2] A REPLAYED OLD ASK DOES NOT DISTURB THE CHANNEL", again == 3,
            std::to_string(again) + "/3 round trips after it");
    } else {
      check("[counter-example 2] an old ask was available to replay", false,
            "the proxy saw no resume ask to keep");
    }
  }

  // ------------------------- the probe said yes, and then the link went anyway (C3 r3)
  //
  // r2 asked the channel once per resumeId and re-read that answer for every retry. So a
  // channel that happened to answer the first ask and died a moment later was refused for
  // the whole thirty second ceiling -- and the viewer cannot route around it, because one
  // break is one resumeId by design. This drives exactly that order: a recovery begun while
  // the channel is healthy (so the first probe is answered and the ask is refused), and then
  // a real cut, with the SAME recovery still running.
  if (connected && repaired) {
    std::cout << "\n--- case 5: alive on the first probe, then the link goes ---\n";
    const uint64_t refusedBefore = viewer.resume.refused_acks();
    check("a recovery begins while the channel is still fine",
          viewer.resume.BeginBreakIfPossible(qpc_now_us(), viewer.video_alive(), false));
    const uint32_t id = viewer.resume.resume_id();
    // At the viewer's own cadence. Some of these land inside the freshness window and reuse
    // the verdict; the rest expire it and ask again. Both paths are meant to happen here.
    for (int i = 0; i < 6; ++i) {
      viewer.resume.Poll(true, qpc_now_us());
      std::this_thread::sleep_for(std::chrono::milliseconds(520));
    }
    check("THE HOST REFUSES WHILE ITS CHANNEL IS ANSWERING",
          viewer.resume.refused_acks() > refusedBefore,
          std::to_string(viewer.resume.refused_acks() - refusedBefore) + " refusals");
    check("...and nothing was re-keyed for it", !viewer.resume.rekeyed());

    // Now the link really goes, with that same recovery still in flight.
    proxy.dropControlUp.store(true);
    proxy.dropControlDown.store(true);
    // Control comes back the moment the repair has been agreed, so the round trip that
    // confirms it has something to travel over. Anything later would make the viewer give
    // up on the id and start another, which is not the case under test.
    std::atomic<bool> watchStop{false};
    std::thread watcher([&] {
      while (!watchStop.load()) {
        if (viewer.resume.rekeyed()) {
          proxy.dropControlUp.store(false);
          proxy.dropControlDown.store(false);
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    });
    const RecoveryResult r = run_recovery(viewer, 28000);
    watchStop.store(true);
    watcher.join();
    proxy.dropControlUp.store(false);
    proxy.dropControlDown.store(false);

    check("AND THE SAME RECOVERY STILL GETS THROUGH", r.resumed,
          r.resumed ? (std::to_string(r.attempts) + " asks in total")
                    : (r.gaveUp ? "gave up -- this is the r2 stall" : "timed out"));
    std::cout << "    timing: breakToRunning=" << (r.breakToRunningUs / 1000)
              << "ms  attempts=" << r.attempts << "  resumeId=" << id << "\n";
    if (r.resumed) {
      int ok = 0;
      for (int i = 0; i < 3; ++i) {
        if (ping(*viewer.link)) ++ok;
      }
      check("...and the channel round trips three times afterwards", ok == 3,
            std::to_string(ok) + "/3");
    }
    gCase5ResumeId = id;
  }

  // ------------------------------------------------------------------ the answer itself is lost
  //
  // Each of the remaining cases needs a working channel to break again, so they are skipped
  // rather than run when case 1 did not get one. A stall reported once is a finding; the same
  // stall reported eight more times under other names is noise that hides it.
  if (connected && !repaired) {
    std::cout << "\n";
    skip("cases 2-4", "the channel was not repaired in case 1, so there is nothing to break again");
  }
  if (connected && repaired) {
    std::cout << "\n--- case 2: the host's answer is lost, then duplicated ---\n";
    proxy.dropControlUp.store(true);
    proxy.dropControlDown.store(true);
    const uint64_t breakStartUs = qpc_now_us();
    while (qpc_now_us() - breakStartUs < 20000000ull) {
      if (!ping(*viewer.link)) break;
    }
    const uint32_t idBefore = viewer.resume.resume_id();
    check("a second recovery starts",
          viewer.resume.BeginBreakIfPossible(qpc_now_us(), viewer.video_alive(), false));
    check("...with a DIFFERENT id from the first",
          viewer.resume.resume_id() != idBefore,
          std::to_string(idBefore) + " then " + std::to_string(viewer.resume.resume_id()));
    proxy.dropControlUp.store(false);
    proxy.dropControlDown.store(false);
    proxy.dropNextResumeAcks.store(2);   // the first two answers never arrive
    proxy.duplicateResumeAcks.store(2);  // and the next ones arrive twice

    const RecoveryResult r = run_recovery(viewer, 28000);
    check("THE REPAIR SURVIVES ITS ANSWER BEING LOST TWICE", r.resumed,
          r.resumed ? (std::to_string(r.attempts) + " asks")
                    : (r.gaveUp ? "gave up" : (r.cancelled ? "cancelled" : "timed out")));
    std::cout << "    timing: breakToFirstSend=" << (r.breakToFirstSendUs / 1000)
              << "ms  breakToRunning=" << (r.breakToRunningUs / 1000) << "ms  attempts="
              << r.attempts << "\n";
    check("...it took more than one ask to get there", r.attempts > 1,
          std::to_string(r.attempts) + " asks");
    check("...and both of the lost answers really were dropped, not merely scheduled to be",
          proxy.dropNextResumeAcks.load() == 0,
          std::to_string(proxy.dropNextResumeAcks.load()) + " drops left unused");
    if (r.resumed) {
      // The duplicate that matters: the host's answer arriving again after the viewer has
      // already acted on it. Inline duplication cannot produce this -- both copies land before
      // the worker applies either -- so it is delivered here, late, on purpose.
      const uint64_t ignoredBefore = viewer.resume.ignored_acks();
      check("the host's answer can be delivered again, late", proxy.ReplayLastResumeAck());
      // It arrives after the recovery has been declared over, so the reason it is refused is
      // "no attempt in flight" rather than "duplicate" -- the duplicate-during-recovery path
      // is the pure test's. Either way the requirement is the same and it is checked here on
      // the real channel: nothing is applied, and the link is untouched.
      const bool refused =
          wait_until([&] { return viewer.resume.ignored_acks() > ignoredBefore; }, 3000);
      check("A LATE DUPLICATE OF THE ANSWER IS REFUSED, NOT REAPPLIED", refused,
            std::string("last reason=") + viewer.resume.last_reason());
      check("...and it did not start anything", !viewer.resume.attempt_in_flight());
      int ok = 0;
      for (int i = 0; i < 3; ++i) {
        if (ping(*viewer.link)) ++ok;
      }
      check("...and the channel round trips three times afterwards, undisturbed", ok == 3,
            std::to_string(ok) + "/3");
    }
    proxy.dropNextResumeAcks.store(0);
    proxy.duplicateResumeAcks.store(0);
  }

  // ------------------------------------------------------------ one direction only, and a cancel
  if (connected && repaired) {
    std::cout << "\n--- case 3: only the viewer's uplink control is cut, then the session is "
                 "called off mid-recovery ---\n";
    proxy.dropControlUp.store(true);  // the host can still be heard; it cannot hear the viewer
    const uint64_t breakStartUs = qpc_now_us();
    while (qpc_now_us() - breakStartUs < 20000000ull) {
      if (!ping(*viewer.link)) break;
    }
    check("a one-way control cut is noticed too", viewer.control.IsClosed(),
          std::string("reason=") + to_string(viewer.control.CloseReason()));
    check("...with video still arriving the whole time", viewer.video_alive());
    check("a recovery starts",
          viewer.resume.BeginBreakIfPossible(qpc_now_us(), viewer.video_alive(), false));

    // Cancel after the first ask has gone out. The uplink is still cut, so nothing can succeed;
    // what is being checked is that cancellation ends it rather than the ceiling.
    std::atomic<bool> cancel{false};
    std::thread canceller([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));
      cancel.store(true);
    });
    const uint64_t startUs = qpc_now_us();
    const RecoveryResult r = run_recovery(viewer, 28000, [&] { return cancel.load(); });
    const uint64_t tookUs = qpc_now_us() - startUs;
    canceller.join();
    check("CANCELLATION ENDS THE RECOVERY, not the ceiling", r.cancelled && !r.gaveUp,
          std::to_string(tookUs / 1000) + "ms (the ceiling is 30000ms)");
    check("...promptly", r.cancelled && tookUs < 10000000ull,
          std::to_string(tookUs / 1000) + "ms");
    proxy.dropControlUp.store(false);
  }

  // ------------------------------------------------- an answer for a session that has moved on
  if (connected && repaired) {
    std::cout << "\n--- case 4: the shell replaces this viewer while a recovery is running ---\n";
    // The generation is the viewer's own, read live by the component through the same hook the
    // product installs. Moving it is what the shell does when it calls this viewer off and
    // starts another -- and an answer that crosses that boundary is answering about a session
    // that no longer exists.
    proxy.dropControlUp.store(true);
    proxy.dropControlDown.store(true);
    const uint64_t breakStartUs = qpc_now_us();
    while (qpc_now_us() - breakStartUs < 20000000ull) {
      if (!ping(*viewer.link)) break;
    }
    const bool started =
        viewer.resume.BeginBreakIfPossible(qpc_now_us(), viewer.video_alive(), false);
    check("a recovery starts", started);
    proxy.dropControlUp.store(false);
    proxy.dropControlDown.store(false);
    ++generation;  // the shell started a different session
    const uint64_t ignoredBefore = viewer.resume.ignored_acks();
    const RecoveryResult r = run_recovery(viewer, 16000);
    check("A SESSION THAT HAS BEEN SUPERSEDED IS NOT REPAIRED", !r.resumed,
          "the host answers; the viewer does not act on it");
    check("...nothing was re-keyed", !viewer.resume.rekeyed());
    check("...and the answers that did arrive were refused for the generation",
          viewer.resume.ignored_acks() > ignoredBefore,
          std::to_string(viewer.resume.ignored_acks() - ignoredBefore) +
              " answers ignored, last reason=" + viewer.resume.last_reason());
    viewer.resume.EndSession();
  }
  viewer.StopIngress();
  if (viewer.sock != INVALID_SOCKET) closesocket(viewer.sock);
  proxy.Stop();
  target.Stop();

  // What the host said about its own control session, which is the other half of every
  // timing above. Printed unconditionally: a run that passed and a run that did not have to
  // be comparable afterwards.
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  CloseHandle(job);
  bool hostGone = true;
  if (hostPi.hProcess) {
    hostGone = WaitForSingleObject(hostPi.hProcess, 20000) == WAIT_OBJECT_0;
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);
  check("the host is gone when the job closes", hostGone);
  std::cout << "    seam: media=" << proxy.mediaPassed.load()
            << " controlDropped=" << proxy.controlDropped.load()
            << " resumeAsks=" << proxy.resumeAsksPassed.load()
            << " resumeAcks=" << proxy.resumeAcksPassed.load()
            << " fromOtherHostPorts=" << proxy.otherHostPorts.load() << "\n";

  {
    std::cout << "\n--- what the host said about its control session ---\n";
    // One claim can only be settled here: that the SAME id was first refused because the
    // channel answered, and later served because it stopped. The viewer sees two refusals and
    // a success; only the host says why each happened.
    const std::string idText = "id=" + std::to_string(gCase5ResumeId) + " ";
    bool refusedOnAlive = false;
    bool servedLater = false;
    bool strangerRefused = false;
    std::string captureLine;
    FILE* f = nullptr;
    if (_wfopen_s(&f, hostLogPath.c_str(), L"rb") == 0 && f) {
      char line[1024];
      while (std::fgets(line, sizeof(line), f)) {
        const std::string text(line);
        if (text.find("rejected reconnect hello with invalid directory capability") != std::string::npos) {
          strangerRefused = true;
        }
        if (captureLine.empty() && (text.find("capture-window target") != std::string::npos)) {
          captureLine = text;
          while (!captureLine.empty() && (captureLine.back() == '\n' || captureLine.back() == '\r')) {
            captureLine.pop_back();
          }
        }
        if (gCase5ResumeId != 0 && text.find(idText) != std::string::npos) {
          if (text.find("verdict=refuse") != std::string::npos &&
              text.find("evidence=alive") != std::string::npos) {
            refusedOnAlive = true;
          }
          if (text.find("verdict=serve") != std::string::npos) servedLater = true;
        }
        if (text.find("[control]") != std::string::npos ||
            text.find("resume") != std::string::npos ||
            text.find("stream") != std::string::npos) {
          std::cout << "    " << text;
        }
      }
      std::fclose(f);
    } else {
      std::cout << "    (the host log could not be read)\n";
    }
    if (strangerHelloSent) {
      // What makes case 1 the counter-example: the Hello really was refused, and resume still
      // worked for the session that was running.
      check("[C5] THE HOST REFUSED THAT HELLO -- and case 1's resume above still went through",
            strangerRefused && repaired,
            std::string("refused=") + (strangerRefused ? "1" : "0") + " repaired=" +
                (repaired ? "1" : "0"));
    }
    if (launched) {
      std::printf("capture: %s (this pid %lu)\n", captureLine.c_str(), static_cast<unsigned long>(GetCurrentProcessId()));
      check("the host captured the window this test paints, not a monitor",
            !captureWant.empty() && captureLine.find(captureWant) != std::string::npos,
            captureLine.empty() ? std::string("the host never said what it captured")
                                : "host: \"" + captureLine + "\"  want: \"" + captureWant + "\"");
    }
    if (gCase5ResumeId != 0) {
      check("[case 5] the host refused that id because its channel answered", refusedOnAlive);
      check("[case 5] AND LATER SERVED THE SAME ID, once it stopped answering", servedLater,
            "one verdict per instant, not one per recovery");
    }
  }

  check("the scratch directory is cleaned up", staging.Remove(), staging.why());
  WSACleanup();

  // A run with unjudged checks is not ALL PASS, and its summary says so. (RV-16)
  const char* verdict = gFailures != 0 ? "RESULT: FAILED"
                        : gSkips != 0  ? "RESULT: PASS WITH UNJUDGED CHECKS"
                                       : "RESULT: ALL PASS";
  std::cout << "\n" << verdict << "  (" << gChecks << " checks, " << gFailures << " failed, "
            << gSkips << " not judged)\n";
  return gFailures == 0 ? 0 : 1;
}

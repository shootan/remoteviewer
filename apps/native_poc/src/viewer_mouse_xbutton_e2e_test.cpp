// The back and forward mouse buttons, end to end. (mouse-xbutton r1)
//
// The user's report: "the 4th mouse button (back) does nothing on the PC client, nor does
// forward". The viewer's window procedure handled left, right and middle only; WM_XBUTTONDOWN fell
// to DefWindowProc. Worse, the host's mapping defaulted any unknown button key to LEFT, so simply
// sending VK_XBUTTON1 to an old host would have made "back" a left click. The fix is therefore two
// things that must be shown together: the buttons arrive at the far side as the right buttons, and
// they are NOT sent to a host that has not said it takes them.
//
// This drives the actual chain, with nothing in it written by the test but the first message:
//
//   WM_XBUTTONDOWN (SendMessage, so the return value can be read) -> the product's window
//   procedure -> on_secondary_button -> ctx.input.mouseButtons (X1 = 0x8) -> enqueue_input_event
//   and its host fence -> the product's control worker -> the wire -> a real GNLinkStream started
//   here -> inject_background_input_event -> PostMessage into a window THIS PROCESS owns
//   -> WM_XBUTTONDOWN naming XBUTTON1 with MK_XBUTTON1 held            (the press lands)
//
// then the up, then X2, then a left click to show nothing else changed, then a held X1 and a REAL
// loss of mouse capture (ReleaseCapture on the window's own thread, which makes Windows deliver
// WM_CAPTURECHANGED) to show the product's release-all lets go of it.
//
// Run again with --legacy-host, the host is started with REMOTE60_NATIVE_MOUSE_XBUTTONS=0 -- the
// product's own switch that makes it behave as a host from before this feature: no capability bit
// in the pong, X edges unsupported. Then the same X press must produce NOTHING at the far side
// while a left click that is sent AFTER it still lands (which is how "not sent" is told apart from
// "not yet arrived"), and the viewer must say so once.
//
// Why the observation is a message queue: the host is started with --input-target-pid and
// --input-target-title naming a window of this process, which confines inject_background_input_event
// to PostMessage into that window and excludes every SendInput path by construction. Nothing can
// reach the user's desktop. The SendInput mapping (mouseData / flags) is checked in
// mouse_button_map_test instead.
//
// Isolation as in the sibling host e2e tests: scratch directory, LOCALAPPDATA pointed into it, ports
// nobody else is on, a helper that never answers, a job object, and off unless
// REMOTE60_ALLOW_HOST_E2E=1.

#include "e2e_isolation.hpp"
#include "control_resume_e2e_support.hpp"

#include <fstream>
#include <memory>
#include <sstream>

#include "control_resume.hpp"
#include "mouse_button_map.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "viewer_control_client.hpp"
#include "viewer_control_resume.hpp"
#include "viewer_state.hpp"
#include "viewer_window_proc.hpp"

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;
using remote60::native_poc::viewer::ViewerState;

namespace {

uint16_t kHostPort = 0;  // picked at run time (e2e_pick_free_udp_port): tests run side by side

// How many of the host's own input-log lines name a given key with a given result word.
int count_host_log_lines(const std::wstring& path, const std::string& result, const std::string& key) {
  std::ifstream in(path, std::ios::binary);
  std::string line;
  int n = 0;
  const std::string needle = " key=" + key;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line.find("[native-video-host][input] " + result) == std::string::npos) continue;
    const size_t at = line.find(needle);
    if (at == std::string::npos) continue;
    const size_t end = at + needle.size();  // "key=5" must not match "key=50"
    if (end == line.size() || line[end] == ' ') ++n;
  }
  return n;
}

std::string hex16(uint32_t v) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "0x%x", v);
  return buf;
}

}  // namespace

// The one stand-in, and it is not in the chain: the macro window is a WebView2 window with its own
// loader, and nothing here opens it.
namespace remote60::native_poc {
bool macro_window_visible() { return false; }
void macro_window_toggle(HINSTANCE, HWND, const MacroWindowHooks&) {}
}  // namespace remote60::native_poc

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::cout.setf(std::ios::unitbuf);

  bool legacyHost = false;
  for (int i = 1; i < argc; ++i) {
    const std::wstring a = argv[i];
    if (a == L"--thumbnail") {  // staged as GNLinkCapture.exe
      Sleep(300000);
      return 0;
    }
    if (a == L"--legacy-host") legacyHost = true;
  }
  if (!host_e2e_allowed()) {
    std::printf("SKIP  viewer_mouse_xbutton_e2e_test (starts a listening host)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return remote60::native_poc::e2e::kE2eSkippedExit;
  }
  std::cout << "mode: " << (legacyHost ? "LEGACY host (REMOTE60_NATIVE_MOUSE_XBUTTONS=0: no capability bit)"
                                       : "host that advertises X buttons")
            << "\n";

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
  kHostPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
  if (kHostPort == 0) {
    std::printf("FAIL  no free UDP port for the host\n");
    return 1;
  }
  std::printf("host port %u (picked at run time)\n", kHostPort);

  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const std::wstring dir = std::wstring(temp) + L"remote60_xbtn_" + std::to_wstring(GetCurrentProcessId()) + L"\\";
  CreateDirectoryW(dir.substr(0, dir.size() - 1).c_str(), nullptr);
  // Removed when main returns, whichever way it returns (the loop at the end is the tidy path).
  remote60::native_poc::e2e::StagingDirCleanup stagingCleanup{dir, std::wstring(temp)};
  const std::wstring me = self_path();
  const std::wstring myDir = directory_of(me);
  const bool staged =
      CopyFileW((myDir + L"GNLinkStream.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) &&
      CopyFileW(me.c_str(), (dir + L"GNLinkCapture.exe").c_str(), FALSE);
  check("a host and a never-answering helper could be staged", staged);

  InjectTarget target;
  check("a window of this process is up for the host to inject into", target.Start());

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
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_FRAME_GATING_DISABLE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_STATIC_SCENE_FPS", L"15");
    // The host's own switch. The child's environment is built from ours below, so this reaches
    // it; the viewer side of this process never reads it.
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_MOUSE_XBUTTONS", legacyHost ? L"0" : nullptr);
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    hostLog = CreateFileW(hostLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                       L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(kHostPort) +
                       L" --seconds 300 --enable-input-injection --input-log-every 1" +
                       L" --input-injection-mode background_message --input-target-pid " +
                       std::to_wstring(GetCurrentProcessId()) +
                       // Both pid and title: this process owns two windows and the resolver must
                       // not be free to pick the viewer's, which would feed the host's injection
                       // straight back into the window procedure that produced it.
                       L" --input-target-title \"" + target.title + L"\"";
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hostLog != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = hostLog;
      si.hStdError = hostLog;
    }
    const std::wstring isoAppData = dir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> isoEnv = e2e_isolated_environment(isoAppData);
    std::string isoWhy;
    const bool isoOk = e2e_command_is_isolated(cmd, dir, &isoWhy) &&
                       e2e_path_is_under(e2e_block_localappdata(isoEnv), dir);
    check("the launched process is isolated from the user's files", isoOk, isoWhy);
    launched = isoOk && CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, hostLog != INVALID_HANDLE_VALUE,
                                       CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                                       isoEnv.data(), dir.c_str(), &si, &hostPi) != 0;
    if (launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  check("the host started", launched);

  // ------------------------------------------------------------------ the viewer, for real
  ViewerState ctx;
  SOCKET sock = INVALID_SOCKET;
  std::thread worker;
  std::thread ingress;
  std::atomic<bool> ingressStop{false};
  std::atomic<uint64_t> media{0};
  bool connected = false;
  uint32_t nextResumeId = 0x7300;
  uint64_t generation = 1;

  HWND viewerWindow = nullptr;
  if (launched) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = remote60::native_poc::viewer::WndProc;  // the product's, not a stand-in
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Remote60XButtonViewer";
    RegisterClassExW(&wc);
    viewerWindow = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"remote60 xbutton viewer",
                                   WS_OVERLAPPEDWINDOW, -4200, -4200, 640, 480, nullptr, nullptr, wc.hInstance, &ctx);
    check("the product's window procedure owns a window", viewerWindow != nullptr);
    if (viewerWindow) ShowWindow(viewerWindow, SW_SHOWNOACTIVATE);
    ctx.session.hwnd = viewerWindow;
    ctx.session.running.store(true);
    ctx.session.inputEnabled.store(true);
    ctx.session.controlRequired = true;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_port = htons(kHostPort);
    InetPtonW(AF_INET, L"127.0.0.1", &hostAddr.sin_addr);
    connect(sock, reinterpret_cast<const sockaddr*>(&hostAddr), sizeof(hostAddr));
    DWORD rcvTimeout = 50;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));
    ctx.session.sock = sock;

    UdpHelloOptions hello;
    hello.budgetMs = 20000;
    hello.sliceMaxMs = 250;
    hello.retrySleepMs = 50;
    std::string helloError;
    uint32_t ackFeatures = 0;
    check("the viewer's Hello is answered", udp_hello_handshake(sock, hello, nullptr, &helloError, &ackFeatures, nullptr),
          helloError);

    ctx.control.udpControl.Configure(
        [sock](const void* data, size_t len) {
          return send(sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, 1200);
    ctx.control.overUdp.store(true, std::memory_order_release);

    sockaddr_in peer{};
    int peerLen = sizeof(peer);
    getpeername(sock, reinterpret_cast<sockaddr*>(&peer), &peerLen);
    ViewerControlResume::Config cfg;
    cfg.decide.giveUpAfterUs = remote60::native_poc::viewer::SessionLivenessConfig{}.controlGoneWithVideoUs;
    ctx.control.resume.Configure(
        &ctx.control.udpControl,
        [sock](const void* data, size_t len) {
          return send(sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        [](const std::string& line) { std::cout << "    " << line << "\n"; }, [&nextResumeId]() { return ++nextResumeId; },
        [&generation]() { return generation; }, cfg, (ackFeatures & kUdpFeatureControlResume) != 0,
        peer.sin_addr.s_addr, peer.sin_port);

    ingress = std::thread([&] {
      std::vector<uint8_t> buf(2048);
      while (!ingressStop.load()) {
        ctx.control.udpControl.Tick();
        const int n = recv(sock, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
        if (n <= 0) continue;
        if (ctx.control.resume.OnDatagram(buf.data(), static_cast<size_t>(n))) continue;
        if (ctx.control.udpControl.OnPacket(buf.data(), static_cast<size_t>(n))) continue;
        media.fetch_add(1);
        ctx.recvLive.lastPublishUs.store(qpc_now_us(), std::memory_order_relaxed);
        ctx.recvLive.lastDatagramUs.store(qpc_now_us(), std::memory_order_relaxed);
      }
    });

    connected = wait_until([&] { return media.load() > 0; }, 20000);
    check("video is arriving", connected, std::to_string(media.load()) + " media datagrams");

    static remote60::native_poc::viewer::Args args;
    args.inputLogEvery = 0;
    static remote60::native_poc::viewer::ControlClient control(ctx, args, /*startInPicker=*/false);
    ctx.control.scheduler.Reset(kClientControlIntervalMsDefault, qpc_now_us());
    ctx.control.connected.store(true, std::memory_order_relaxed);
    worker = std::thread([&] { control.Run(); });
  }

  const auto pump_until = [&](const std::function<bool()>& done, int budgetMs) {
    const DWORD deadline = GetTickCount() + static_cast<DWORD>(budgetMs);
    for (;;) {
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      if (done()) return true;
      if (GetTickCount() >= deadline) return done();
      Sleep(10);
    }
  };
  // Everything that is going to arrive has arrived once this passes with nothing new.
  const auto settle = [&](const std::function<uint32_t()>& count) {
    uint32_t last = count();
    for (int quiet = 0; quiet < 15;) {
      pump_until([] { return false; }, 100);
      const uint32_t now = count();
      quiet = (now == last) ? quiet + 1 : 0;
      last = now;
    }
  };
  const auto xTotal = [&] {
    return target.xDowns[0].load() + target.xDowns[1].load() + target.xDowns[2].load() + target.xUps[0].load() +
           target.xUps[1].load() + target.xUps[2].load();
  };

  if (connected) {
    const bool roundTripped = pump_until([&] { return ctx.control.lastRttUs.load() > 0; }, 25000);
    check("the worker completes a round trip with the host", roundTripped,
          "rttUs=" + std::to_string(ctx.control.lastRttUs.load()));
    check("...and control is up", ctx.control.connected.load());
  }

  const bool ready = connected && ctx.control.connected.load();
  if (ready) {
    // What the product would have reached by now: a decoded frame size (clicks map through it)
    // and the picker dismissed (until then a click is a picker press, not input).
    {
      std::lock_guard<std::mutex> lock(ctx.frameBuf.frame.mu);
      ctx.frameBuf.frame.width = 320;
      ctx.frameBuf.frame.height = 240;
    }
    ctx.picker.visible.store(false, std::memory_order_relaxed);
  }
  const LPARAM at = MAKELPARAM(320, 240);

  if (ready && !legacyHost) {
    std::cout << "\n--- the host says it takes X buttons ---\n";
    check("THE PONG ADVERTISES kCaptureFlagMouseXButtonsV1", ctx.session.hostMouseXButtons.load());

    std::cout << "\n--- X1 (back) goes down and up, through the product's own path ---\n";
    const LRESULT downRet = SendMessageW(viewerWindow, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON1, XBUTTON1), at);
    check("the window procedure answers TRUE to WM_XBUTTONDOWN (the documented contract)", downRet == TRUE,
          "returned " + std::to_string(downRet));
    check("...and holds X1 (wire bit 0x8)", (ctx.input.mouseButtons.load() & kMouseWireX1) != 0,
          "mouseButtons=" + hex16(ctx.input.mouseButtons.load()));
    const bool x1Down = pump_until([&] { return target.xDowns[1].load() > 0; }, 10000);
    check("X1 DOWN REACHES THE HOST AND ARRIVES AS WM_XBUTTONDOWN NAMING XBUTTON1", x1Down,
          std::to_string(target.xDowns[1].load()) + " WM_XBUTTONDOWN(XBUTTON1)");
    if (x1Down) {
      const uint32_t wp = target.lastXDownWparam.load();
      check("...with MK_XBUTTON1 (0x20) held and no MK_LBUTTON",
            (LOWORD(wp) & MK_XBUTTON1) != 0 && (LOWORD(wp) & MK_LBUTTON) == 0, "wParam=" + hex16(wp));
      const int32_t x = target.lastXDownX.load();
      const int32_t y = target.lastXDownY.load();
      // The viewer maps (320,240) of its 640x480 client onto its 320x240 frame; the host maps that
      // onto the target's 320x240 client. Inside the client either way.
      check("...inside the target's client area", x >= 0 && x < 320 && y >= 0 && y < 240,
            std::to_string(x) + "," + std::to_string(y));
      check("...and no LEFT press was made of it", target.leftDowns.load() == 0,
            std::to_string(target.leftDowns.load()) + " WM_LBUTTONDOWN");
    }
    const LRESULT upRet = SendMessageW(viewerWindow, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), at);
    check("the window procedure answers TRUE to WM_XBUTTONUP", upRet == TRUE);
    check("...and lets go of X1", (ctx.input.mouseButtons.load() & kMouseWireX1) == 0);
    const bool x1Up = pump_until([&] { return target.xUps[1].load() > 0; }, 10000);
    check("X1 UP ARRIVES AS WM_XBUTTONUP NAMING XBUTTON1", x1Up,
          std::to_string(target.xUps[1].load()) + " WM_XBUTTONUP(XBUTTON1)");
    if (x1Up) {
      const uint32_t wp = target.lastXUpWparam.load();
      check("...with MK_XBUTTON1 no longer held", (LOWORD(wp) & MK_XBUTTON1) == 0, "wParam=" + hex16(wp));
    }

    std::cout << "\n--- X2 (forward) ---\n";
    SendMessageW(viewerWindow, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON2, XBUTTON2), at);
    const bool x2Down = pump_until([&] { return target.xDowns[2].load() > 0; }, 10000);
    check("X2 DOWN ARRIVES AS WM_XBUTTONDOWN NAMING XBUTTON2", x2Down,
          std::to_string(target.xDowns[2].load()) + " WM_XBUTTONDOWN(XBUTTON2)");
    if (x2Down) {
      const uint32_t wp = target.lastXDownWparam.load();
      check("...with MK_XBUTTON2 (0x40) held", (LOWORD(wp) & MK_XBUTTON2) != 0, "wParam=" + hex16(wp));
    }
    SendMessageW(viewerWindow, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), at);
    const bool x2Up = pump_until([&] { return target.xUps[2].load() > 0; }, 10000);
    check("X2 UP ARRIVES AS WM_XBUTTONUP NAMING XBUTTON2", x2Up,
          std::to_string(target.xUps[2].load()) + " WM_XBUTTONUP(XBUTTON2)");
    check("no X message named anything but XBUTTON1 / XBUTTON2",
          target.xDowns[0].load() == 0 && target.xUps[0].load() == 0);

    std::cout << "\n--- a left click still is one (regression) ---\n";
    const uint32_t xBefore = xTotal();
    PostMessageW(viewerWindow, WM_LBUTTONDOWN, MK_LBUTTON, at);
    const bool lDown = pump_until([&] { return target.leftDowns.load() > 0; }, 10000);
    check("THE LEFT DOWN ARRIVES AS WM_LBUTTONDOWN", lDown, std::to_string(target.leftDowns.load()) + " WM_LBUTTONDOWN");
    PostMessageW(viewerWindow, WM_LBUTTONUP, 0, at);
    const bool lUp = pump_until([&] { return target.leftUps.load() > 0; }, 10000);
    check("...and the up as WM_LBUTTONUP", lUp, std::to_string(target.leftUps.load()) + " WM_LBUTTONUP");
    settle(xTotal);
    check("...with no X message made of either", xTotal() == xBefore);

    std::cout << "\n--- X1 held, then mouse capture is really lost: the release-all lets go ---\n";
    const uint32_t x1DownsBefore = target.xDowns[1].load();
    const uint32_t x1UpsBefore = target.xUps[1].load();
    SendMessageW(viewerWindow, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON1, XBUTTON1), at);
    const bool held = pump_until([&] { return target.xDowns[1].load() > x1DownsBefore; }, 10000);
    check("[held] the X1 down landed", held);
    check("[held] the window procedure took mouse capture for it", GetCapture() == viewerWindow);
    check("[held] the viewer is holding X1", (ctx.input.mouseButtons.load() & kMouseWireX1) != 0);
    // The real trigger: releasing capture on the window's own thread makes Windows send it
    // WM_CAPTURECHANGED, the case the product's release-all runs from. Not a posted stand-in.
    ReleaseCapture();
    pump_until([] { return false; }, 100);
    check("[held] ...and after WM_CAPTURECHANGED no X bit is held",
          (ctx.input.mouseButtons.load() & kMouseWireXMask) == 0,
          "mouseButtons=" + hex16(ctx.input.mouseButtons.load()));
    const bool released = pump_until([&] { return target.xUps[1].load() > x1UpsBefore; }, 10000);
    check("THE X1 UP ARRIVES AT THE HOST WITHOUT ANYONE PRESSING UP", released,
          std::to_string(target.xUps[1].load() - x1UpsBefore) + " WM_XBUTTONUP(XBUTTON1)");
    settle([&] { return target.xUps[1].load(); });
    check("...EXACTLY ONCE", target.xUps[1].load() - x1UpsBefore == 1,
          std::to_string(target.xUps[1].load() - x1UpsBefore) + " WM_XBUTTONUP(XBUTTON1)");
    check("...and the down was not repeated", target.xDowns[1].load() - x1DownsBefore == 1);
  }

  if (ready && legacyHost) {
    std::cout << "\n--- the host does NOT say it takes X buttons ---\n";
    check("THE PONG DOES NOT ADVERTISE kCaptureFlagMouseXButtonsV1", !ctx.session.hostMouseXButtons.load());

    std::cout << "\n--- X1 is pressed; a left click follows; only the left click may arrive ---\n";
    // The viewer's own count of input events that left it, so "not sent" is shown at this end
    // too and does not rest on the host refusing what it was sent.
    const uint64_t sentBefore = ctx.session.inputEventsSent.load();
    const LRESULT downRet = SendMessageW(viewerWindow, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON1, XBUTTON1), at);
    check("the window procedure still answers TRUE (handled, by refusing)", downRet == TRUE);
    check("...and holds nothing for it", (ctx.input.mouseButtons.load() & kMouseWireXMask) == 0,
          "mouseButtons=" + hex16(ctx.input.mouseButtons.load()));
    check("...and said so once", ctx.input.xButtonRefusalReported.load());
    SendMessageW(viewerWindow, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), at);
    SendMessageW(viewerWindow, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON2, XBUTTON2), at);
    SendMessageW(viewerWindow, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), at);
    // Sent AFTER the X presses on the same ordered channel: when this lands, anything sent
    // before it would have landed too.
    PostMessageW(viewerWindow, WM_LBUTTONDOWN, MK_LBUTTON, at);
    const bool lDown = pump_until([&] { return target.leftDowns.load() > 0; }, 10000);
    check("the left click sent after them arrives", lDown, std::to_string(target.leftDowns.load()) + " WM_LBUTTONDOWN");
    PostMessageW(viewerWindow, WM_LBUTTONUP, 0, at);
    pump_until([&] { return target.leftUps.load() > 0; }, 10000);
    settle(xTotal);
    check("NO X PRESS OR RELEASE REACHED THE HOST", xTotal() == 0, std::to_string(xTotal()) + " WM_XBUTTON*");
    check("...and no LEFT press was made of them either (exactly the one click)",
          target.leftDowns.load() == 1 && target.leftUps.load() == 1,
          std::to_string(target.leftDowns.load()) + " down / " + std::to_string(target.leftUps.load()) + " up");
    const uint64_t sentDelta = ctx.session.inputEventsSent.load() - sentBefore;
    check("...AND THE VIEWER SENT EXACTLY THE LEFT CLICK'S TWO EVENTS, NOT THE FOUR X EDGES", sentDelta == 2,
          std::to_string(sentDelta) + " input events left the viewer");

    std::cout << "\n--- capture loss with nothing X held sends no X up ---\n";
    PostMessageW(viewerWindow, WM_LBUTTONDOWN, MK_LBUTTON, at);
    pump_until([&] { return target.leftDowns.load() > 1; }, 10000);
    ReleaseCapture();
    pump_until([&] { return target.leftUps.load() > 1; }, 10000);
    settle(xTotal);
    check("the release-all let go of the left button", target.leftUps.load() == 2);
    check("...and sent no X up", xTotal() == 0);
  }

  // ------------------------------------------------------------------------------- teardown
  ctx.session.running.store(false);
  if (worker.joinable()) worker.join();
  ingressStop.store(true);
  if (ingress.joinable()) ingress.join();
  if (sock != INVALID_SOCKET) closesocket(sock);
  if (viewerWindow) DestroyWindow(viewerWindow);
  target.Stop();
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  CloseHandle(job);
  bool hostGone = true;
  if (hostPi.hProcess) {
    hostGone = WaitForSingleObject(hostPi.hProcess, 20000) == WAIT_OBJECT_0;
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);
  check("the host is gone when the job closes", hostGone);

  // The host's own account, as receipt evidence beside the arrival evidence above (it is not a
  // substitute for it): what it injected, and what it refused.
  if (launched && ready) {
    const int injX1 = count_host_log_lines(hostLogPath, "injected", "5");
    const int injX2 = count_host_log_lines(hostLogPath, "injected", "6");
    const int unsupX = count_host_log_lines(hostLogPath, "unsupported", "5") +
                       count_host_log_lines(hostLogPath, "unsupported", "6");
    if (!legacyHost) {
      check("host log: X1 edges injected (down, up, held down, released up)", injX1 == 4, std::to_string(injX1));
      check("host log: X2 edges injected (down, up)", injX2 == 2, std::to_string(injX2));
    } else {
      check("host log: no X edge was injected", injX1 == 0 && injX2 == 0,
            std::to_string(injX1) + " / " + std::to_string(injX2));
    }
    check("host log: no X edge was refused as unsupported (none reached a host that lacks them)", unsupX == 0,
          std::to_string(unsupX));
  }

  for (int i = 0; i < 40; ++i) {
    DeleteFileW((dir + L"host.log").c_str());
    DeleteFileW((dir + L"GNLinkStream.exe").c_str());
    DeleteFileW((dir + L"GNLinkCapture.exe").c_str());
    remote60::native_poc::e2e::remove_tree_under(dir + L"localappdata", dir);
    if (RemoveDirectoryW(dir.substr(0, dir.size() - 1).c_str())) break;
    Sleep(100);
  }
  check("the scratch directory is cleaned up",
        GetFileAttributesW(dir.substr(0, dir.size() - 1).c_str()) == INVALID_FILE_ATTRIBUTES);
  WSACleanup();

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  (" << gChecks << " checks, "
            << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}

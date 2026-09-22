// A key that goes down, a key-up that never gets sent, and who releases it. (item 8, C3 r3)
//
// The viewer's repair is not finished when the channel comes back. A key pressed before the break
// is still down on the host: its up was generated while the tunnel was dead and is simply gone,
// and the host has no reason to believe anything changed. That is a modifier latched on somebody
// else's machine, and it outlives the session.
//
// The product answers it by running the EXISTING release-all contract when control returns -- the
// same two calls WM_KILLFOCUS makes. Up to r2 the end-to-end evidence for that was "a key-up we
// sent ourselves reached the host's injection", which shows the wire works and says nothing about
// whether anything would have sent one. This drives the actual chain instead:
//
//   PostMessage WM_KEYDOWN -> the product's window procedure -> forward_key_down ->
//   ctx.input.forwardedKeyDown -> the input queue -> the product's control worker -> the real
//   host -> PostMessage into a window THIS PROCESS owns          (the down lands)
//   ...control is cut, and the key-up is never generated...      (the up is lost)
//   ...the worker repairs the channel and posts kMsgControlResumed...
//   -> the window procedure -> enqueue_release_for_pressed_keys -> ...the same path again
//                                                                  (the up arrives by itself)
//
// Nothing in that sequence is written by this test except the first PostMessage and the cut.
//
// Why the observation is a message queue rather than a host counter: the host is started with
// --input-target-pid pointing at a window of this process, which confines
// inject_background_input_event to PostMessage into that window and excludes every SendInput path
// by construction (host_input_inject.cpp:586-598). Nothing can reach the user's desktop.
//
// ⚠️ LIMIT, stated because it bounds what this proves. Two release paths run on resume:
// enqueue_release_for_pressed_keys (the legacy key path, observed here) and
// release_all_physical (the host-IME scan-code path). The second cannot be observed in this
// environment at all: the host injects physical scan codes with SendInput, and its gate requires
// the CAPTURE target window to hold focus (host_control_session.cpp:441-451). This host captures a
// monitor, so the gate is shut and no physical key is injected -- which is also why driving it
// would be unsafe here, since SendInput types wherever focus happens to be.

#include "control_resume_e2e_support.hpp"

#include <memory>

#include "control_resume.hpp"
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

constexpr uint16_t kHostPort = 44794;
constexpr uint16_t kProxyPort = 44795;
constexpr uint32_t kTestVk = 'K';

/** The scan-code shaped lParam a real WM_KEYDOWN carries; the product reads bits out of it. */
LPARAM key_lparam(uint32_t vk, bool up) {
  const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
  LPARAM lp = 1 | (static_cast<LPARAM>(scan & 0xff) << 16);
  if (up) lp |= (1ll << 30) | (1ll << 31);
  return lp;
}

}  // namespace

// The one stand-in, and it is not in the chain: the macro window is a WebView2 window with its
// own loader, and nothing here opens it. Everything the release-all path touches -- the window
// procedure, the input forwarding, the queue, the control worker, the channel -- is linked for
// real.
namespace remote60::native_poc {
bool macro_window_visible() { return false; }
void macro_window_toggle(HINSTANCE, HWND, const MacroWindowHooks&) {}
}  // namespace remote60::native_poc

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::cout.setf(std::ios::unitbuf);

  if (!host_e2e_allowed()) {
    std::printf("SKIP  viewer_release_all_e2e_test (starts a listening host)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return 0;
  }
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--thumbnail") {  // staged as GNLinkCapture.exe
      Sleep(300000);
      return 0;
    }
  }

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const std::wstring dir = std::wstring(temp) + L"remote60_c3_relall_" +
                           std::to_wstring(GetCurrentProcessId()) + L"\\";
  CreateDirectoryW(dir.substr(0, dir.size() - 1).c_str(), nullptr);
  const std::wstring me = self_path();
  const std::wstring myDir = directory_of(me);
  const bool staged =
      CopyFileW((myDir + L"GNLinkStream.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) &&
      CopyFileW(me.c_str(), (dir + L"GNLinkCapture.exe").c_str(), FALSE);
  check("a host and a never-answering helper could be staged", staged);

  InjectTarget target;
  check("a window of this process is up for the host to inject into", target.Start());

  ControlProxy proxy;
  check("the control seam is listening", proxy.Start(kProxyPort, kHostPort));

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
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    hostLog = CreateFileW(hostLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                       L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(kHostPort) +
                       L" --seconds 300 --enable-input-injection" +
                       L" --input-injection-mode background_message --input-target-pid " +
                       std::to_wstring(GetCurrentProcessId()) +
                       // The title as well as the pid, because this process owns TWO windows and
                       // the resolver would otherwise be free to pick the viewer's -- which would
                       // feed the host's injection straight back into the window procedure that
                       // produced it. Both criteria must match, so there is exactly one answer.
                       L" --input-target-title \"c3 inject target\"";
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hostLog != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = hostLog;
      si.hStdError = hostLog;
    }
    launched = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr,
                              hostLog != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                              nullptr, dir.c_str(), &si, &hostPi) != 0;
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
  uint32_t nextResumeId = 0x7100;
  uint64_t generation = 1;

  HWND viewerWindow = nullptr;
  if (launched) {
    // The product's own window procedure. Not a stand-in: the release-all this test is about is a
    // case inside it, and the key state it releases is built by the same procedure on the way in.
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = remote60::native_poc::viewer::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Remote60C3ReleaseAllViewer";
    RegisterClassExW(&wc);
    // The state travels in the creation parameters, which is how the product does it:
    // WM_NCCREATE pins it before CreateWindowExW returns, so the messages creation itself
    // generates already see it.
    viewerWindow = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName,
                                   L"remote60 c3 viewer", WS_OVERLAPPEDWINDOW, -4200, -4200, 640,
                                   480, nullptr, nullptr, wc.hInstance, &ctx);
    check("the product's window procedure owns a window", viewerWindow != nullptr);
    if (viewerWindow) ShowWindow(viewerWindow, SW_SHOWNOACTIVATE);
    ctx.session.hwnd = viewerWindow;
    ctx.session.running.store(true);
    ctx.session.inputEnabled.store(true);
    ctx.session.controlRequired = true;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in proxyAddr{};
    proxyAddr.sin_family = AF_INET;
    proxyAddr.sin_port = htons(kProxyPort);
    InetPtonW(AF_INET, L"127.0.0.1", &proxyAddr.sin_addr);
    connect(sock, reinterpret_cast<const sockaddr*>(&proxyAddr), sizeof(proxyAddr));
    DWORD rcvTimeout = 50;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout),
               sizeof(rcvTimeout));
    ctx.session.sock = sock;

    UdpHelloOptions hello;
    hello.budgetMs = 20000;
    hello.sliceMaxMs = 250;
    hello.retrySleepMs = 50;
    hello.requestControlResume = true;
    std::string helloError;
    uint32_t ackFeatures = 0;
    check("the viewer's Hello is answered",
          udp_hello_handshake(sock, hello, nullptr, &helloError, &ackFeatures, nullptr), helloError);

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
    cfg.decide.giveUpAfterUs =
        remote60::native_poc::viewer::SessionLivenessConfig{}.controlGoneWithVideoUs;
    ctx.control.resume.Configure(
        &ctx.control.udpControl,
        [sock](const void* data, size_t len) {
          return send(sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        [](const std::string& line) { std::cout << "    " << line << "\n"; },
        [&nextResumeId]() { return ++nextResumeId; }, [&generation]() { return generation; }, cfg,
        (ackFeatures & kUdpFeatureControlResume) != 0, peer.sin_addr.s_addr, peer.sin_port);

    // The viewer's ingress, in the order viewer_video_receiver.cpp uses.
    ingress = std::thread([&] {
      std::vector<uint8_t> buf(2048);
      while (!ingressStop.load()) {
        ctx.control.udpControl.Tick();
        const int n = recv(sock, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
        if (n <= 0) continue;
        if (ctx.control.resume.OnDatagram(buf.data(), static_cast<size_t>(n))) continue;
        if (ctx.control.udpControl.OnPacket(buf.data(), static_cast<size_t>(n))) continue;
        media.fetch_add(1);
        // The liveness the worker reads to decide a session is worth repairing.
        ctx.recvLive.lastPublishUs.store(qpc_now_us(), std::memory_order_relaxed);
        ctx.recvLive.lastDatagramUs.store(qpc_now_us(), std::memory_order_relaxed);
      }
    });

    connected = wait_until([&] { return media.load() > 0; }, 20000);
    check("video is arriving", connected, std::to_string(media.load()) + " media datagrams");

    // The product's control worker. From here on the viewer's behaviour is its own.
    static remote60::native_poc::viewer::Args args;
    args.inputLogEvery = 0;
    static remote60::native_poc::viewer::ControlClient control(ctx, args, /*startInPicker=*/false);
    // Two lines from viewer_startup.cpp:825-831, reproduced because this harness does not run
    // startup. The scheduler needs its interval, and "connected" is published BEFORE the thread
    // starts -- Run() stores false on its first failed exchange, so storing true afterwards
    // could land on top of that. Everything the test is about happens after this point and is
    // the product's own.
    ctx.control.scheduler.Reset(kClientControlIntervalMsDefault, qpc_now_us());
    ctx.control.connected.store(true, std::memory_order_relaxed);
    worker = std::thread([&] { control.Run(); });
  }

  // The window procedure runs on this thread; every wait below pumps it.
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

  if (connected) {
    // Not the flag this test set: a round trip the worker actually completed, which is what
    // the RTT is recorded from (viewer_control_client.cpp:195).
    const bool roundTripped = pump_until([&] { return ctx.control.lastRttUs.load() > 0; }, 25000);
    check("the worker completes a round trip with the host", roundTripped,
          "rttUs=" + std::to_string(ctx.control.lastRttUs.load()));
    check("...and control is up", ctx.control.connected.load());
  }

  bool downLanded = false;
  if (connected && ctx.control.connected.load()) {
    std::cout << "\n--- a key goes down, through the product's own path ---\n";
    const uint64_t downsBefore = target.keyDowns.load();
    PostMessageW(viewerWindow, WM_KEYDOWN, kTestVk, key_lparam(kTestVk, false));
    downLanded = pump_until([&] { return target.keyDowns.load() > downsBefore; }, 10000);
    check("THE KEY DOWN REACHES THE HOST AND IS INJECTED INTO THIS PROCESS", downLanded,
          std::to_string(target.keyDowns.load() - downsBefore) + " WM_KEYDOWN");
    check("...and the viewer is holding it", ctx.input.forwardedKeyDown[kTestVk].load(),
          "this is the state that would strand on the host");
    check("...and no up has been sent", target.keyUps.load() == 0,
          std::to_string(target.keyUps.load()) + " WM_KEYUP so far");
  }

  if (downLanded) {
    std::cout << "\n--- control is cut; the key-up is never generated ---\n";
    const uint64_t downsAtCut = target.keyDowns.load();
    proxy.dropControlUp.store(true);
    proxy.dropControlDown.store(true);

    // The worker finds out on its own: its channel runs out of retransmits and the action fails.
    const bool resuming =
        pump_until([&] { return !ctx.control.connected.load(); }, 25000);
    check("the worker notices the control channel is gone", resuming);

    // The uplink comes back. Nothing else is done to it.
    proxy.dropControlUp.store(false);
    proxy.dropControlDown.store(false);

    const bool back = pump_until([&] { return ctx.control.connected.load(); }, 40000);
    check("THE CHANNEL IS REPAIRED WITHOUT A RECONNECT", back);

    const bool upArrived = pump_until([&] { return target.keyUps.load() > 0; }, 10000);
    check("AND THE KEY-UP ARRIVES AT THE HOST WITHOUT ANYONE SENDING ONE", upArrived,
          std::to_string(target.keyUps.load()) +
              " WM_KEYUP -- posted by the release-all the repair triggered");
    check("...so the viewer is no longer holding it",
          !ctx.input.forwardedKeyDown[kTestVk].load());
    check("NO DUPLICATE RE-EXECUTION: the down happened exactly once",
          target.keyDowns.load() == downsAtCut,
          std::to_string(target.keyDowns.load() - downsAtCut) + " further WM_KEYDOWN after the cut");
  }

  // ------------------------------------------------------------------------------- teardown
  ctx.session.running.store(false);
  if (worker.joinable()) worker.join();
  ingressStop.store(true);
  if (ingress.joinable()) ingress.join();
  if (sock != INVALID_SOCKET) closesocket(sock);
  if (viewerWindow) DestroyWindow(viewerWindow);
  proxy.Stop();
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

  for (int i = 0; i < 40; ++i) {
    DeleteFileW((dir + L"host.log").c_str());
    DeleteFileW((dir + L"GNLinkStream.exe").c_str());
    DeleteFileW((dir + L"GNLinkCapture.exe").c_str());
    if (RemoveDirectoryW(dir.substr(0, dir.size() - 1).c_str())) break;
    Sleep(100);
  }
  check("the scratch directory is cleaned up",
        GetFileAttributesW(dir.substr(0, dir.size() - 1).c_str()) == INVALID_FILE_ATTRIBUTES);
  WSACleanup();

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  (" << gChecks
            << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}

// Two control dispatchers, one stuck preview, and whether either of them notices the other.
//
// The host runs two independent control loops when a control port is configured: a TCP one
// (host_startup_control.cpp:127, serving at :163) and a UDP one tunnelled over the media socket
// (:397, serving at :431). Both call Serve on the SAME ControlSessionServer, so both reach the
// same preview budget and the same helper. A LAN client dialling a second port lands on the first;
// a client reached through the directory lands on the second; a machine can have both at once.
//
// That is why the budget is under a lock and why the helper has an explicit cap of one. This test
// is the part that was missing: those two guards checked against two REAL dispatchers rather than
// two threads calling the same function.
//
// The question is the same as the single-dispatcher case, asked twice: while one session is inside
// a preview that will never arrive, does the OTHER session's control still answer?
//
// Isolation is unchanged -- scratch directory, ports nobody else uses, a GNLinkCapture of this
// test's choosing that never answers, and a job object that takes the host when this process goes.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "native_video_client_session.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"

namespace {

using remote60::native_poc::ClientSessionConnectArgs;
using remote60::native_poc::ClientSessionController;
using remote60::native_poc::ClientSessionState;
using remote60::native_poc::fetch_window_thumbnail;
using remote60::native_poc::qpc_now_us;
using remote60::native_poc::TcpControlLink;
using remote60::native_poc::WindowThumbnailReply;

/**
 * A sink that does nothing with the frames.
 *
 * Required, not optional: a session connected without one does not stay connected. The first
 * version of this test left it null and the UDP session went to "error" before it ever received a
 * window list -- which for a while looked like the two dispatchers interfering with each other.
 * They were not; running the same session with no TCP dispatcher at all failed identically.
 */
class NullSink : public remote60::native_poc::ClientEncodedFrameSink {
 public:
  void OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& frame) override {
    frames_.fetch_add(1, std::memory_order_relaxed);
    (void)frame;
  }
  void OnVideoStreamReset() override {}
  uint64_t frames() const { return frames_.load(std::memory_order_relaxed); }

 private:
  std::atomic<uint64_t> frames_{0};
};

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

constexpr uint64_t kHostDeadlineUs = 1000ull * 1000;

bool wait_until(const std::function<bool()>& done, int budgetMs) {
  const DWORD deadline = GetTickCount() + static_cast<DWORD>(budgetMs);
  while (GetTickCount() < deadline) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return done();
}

std::wstring self_path() {
  std::wstring path(32768, L'\0');
  const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  path.resize(n);
  return path;
}

std::wstring directory_of(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
}

SOCKET connect_local(uint16_t port, int attempts) {
  for (int i = 0; i < attempts; ++i) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET) {
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);
      InetPtonW(AF_INET, L"127.0.0.1", &addr.sin_addr);
      if (connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0) {
        int noDelay = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay),
                   sizeof(noDelay));
        return s;
      }
      closesocket(s);
    }
    Sleep(200);
  }
  return INVALID_SOCKET;
}

}  // namespace


namespace {

/**
 * Whether this run may start a host that listens.
 *
 * Off by default, and that is the whole point. Starting GNLinkStream opens listening sockets, and
 * on Windows that raises a firewall prompt for an executable the user has never seen -- a scratch
 * copy under %TEMP% with a fresh path every run. Left unanswered the prompt becomes a Block rule,
 * and a full regression sweep left eight rules behind on this machine before anyone noticed.
 *
 * So the default is to skip, loudly, with exit 0: a regression run should not quietly lose
 * coverage, and it should not quietly reconfigure somebody's firewall either.
 */
bool host_e2e_allowed() {
  wchar_t value[8]{};
  const DWORD n = GetEnvironmentVariableW(L"REMOTE60_ALLOW_HOST_E2E", value, 8);
  return n > 0 && value[0] == L'1';
}

void print_skip(const char* what) {
  std::printf("SKIP  %s\n", what);
  std::printf("      This test starts a real GNLinkStream, which listens, which makes Windows\n");
  std::printf("      ask about the firewall -- and an unanswered prompt becomes a Block rule on\n");
  std::printf("      the user's machine. Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n");
  std::printf("\nRESULT: SKIPPED\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  if (!host_e2e_allowed()) {
    print_skip("host_two_dispatcher_e2e_test (starts a listening host)");
    return 0;
  }

  // Helper mode: copied over GNLinkCapture.exe, this answers nothing.
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--thumbnail") {
      Sleep(120000);
      return 0;
    }
  }

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

  const uint16_t mediaPort = 44780;   // UDP: media, and the control tunnel rides on it
  const uint16_t controlPort = 44781; // TCP: the second dispatcher

  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const std::wstring dir = std::wstring(temp) + L"remote60_two_disp_" +
                           std::to_wstring(GetCurrentProcessId()) + L"\\";
  CreateDirectoryW(dir.substr(0, dir.size() - 1).c_str(), nullptr);

  const std::wstring me = self_path();
  const std::wstring myDir = directory_of(me);
  const bool staged =
      CopyFileW((myDir + L"GNLinkStream.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) &&
      CopyFileW(me.c_str(), (dir + L"GNLinkCapture.exe").c_str(), FALSE);
  check("a host and a never-answering helper could be staged", staged);

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  PROCESS_INFORMATION hostPi{};
  bool launched = false;
  if (staged) {
    // h264 over UDP is gated behind this. Set on ourselves so the child inherits it; nothing else
    // in this process cares.
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
    std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                       L" --bind-port " + std::to_wstring(mediaPort) + L" --control-port " +
                       std::to_wstring(controlPort) + L" --seconds 90";
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    launched = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                              CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, dir.c_str(), &si,
                              &hostPi) != 0;
    if (launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  check("the host started", launched);

  NullSink sink;
  ClientSessionController udpClient;
  SOCKET tcpControl = INVALID_SOCKET;

  if (launched) {
    // Dispatcher one. This also unblocks the host's startup: the control threads are not created
    // until a media client has arrived, which for UDP means the hello handshake.
    ClientSessionConnectArgs args;
    args.host = "127.0.0.1";
    args.videoPort = mediaPort;
    args.controlPort = mediaPort;  // never dialled; the session rejects 0
    args.requireUdpHello = true;
    args.requireTcpControl = false;
    args.controlOverUdp = true;
    args.controlIntervalMs = 200;
    args.encodedFrameSink = &sink;
    check("a UDP session connects", udpClient.Connect(args));
    const bool up = wait_until(
        [&] { return udpClient.Snapshot().state == ClientSessionState::Connected; }, 15000);
    check("...and reaches Connected", up, udpClient.Snapshot().status);
    check("...with its control loop running over the media socket",
          udpClient.Snapshot().controlLoopActive);

    // Dispatcher two, on its own port. Only reachable because the UDP client arrived first.
    //
    // --no-tcp leaves it out, which is how the UDP side's behaviour with and without a second
    // dispatcher can be compared in the same harness.
    bool skipTcp = false;
    for (int i = 1; i < argc; ++i) {
      if (std::wstring(argv[i]) == L"--no-tcp") skipTcp = true;
    }
    if (!skipTcp) tcpControl = connect_local(controlPort, 40);
    if (skipTcp) {
      wait_until([&] { return udpClient.Snapshot().latestWindowListCount > 0; }, 15000);
      const auto snap = udpClient.Snapshot();
      check("[--no-tcp] the UDP session alone gets a window list",
            snap.latestWindowListCount > 0,
            std::to_string(snap.latestWindowListCount) + " windows, status=" + snap.status);
      check("[--no-tcp] ...and stays connected",
            snap.state == ClientSessionState::Connected, snap.status);
    }
    check("a TCP control session connects to the second dispatcher",
          tcpControl != INVALID_SOCKET,
          "both dispatchers are live at this point -- the premise this test exists to check");
  }

  if (tcpControl != INVALID_SOCKET) {
    // Let the UDP side settle so its own periodic traffic is running.
    wait_until([&] { return udpClient.Snapshot().latestWindowListCount > 0; }, 15000);
    check("the UDP session has a window list", udpClient.Snapshot().latestWindowListCount > 0,
          std::to_string(udpClient.Snapshot().latestWindowListCount) + " windows");

    TcpControlLink link(tcpControl);

    // The race. The TCP dispatcher goes into a preview that will never arrive; while it is in
    // there, the UDP dispatcher is asked for something and timed.
    std::atomic<uint64_t> tcpUs{0};
    std::atomic<bool> tcpAnswered{false};
    std::atomic<bool> tcpPresent{true};
    std::thread tcpSide([&] {
      WindowThumbnailReply reply;
      const uint64_t start = qpc_now_us();
      const bool ok = fetch_window_thumbnail(link, 0, 256, 160, qpc_now_us(), &reply);
      tcpUs.store(qpc_now_us() - start);
      tcpAnswered.store(ok);
      tcpPresent.store(reply.present);
    });

    // Give the TCP request time to be inside the host's capture, then ask the other session for a
    // round trip and see how long it waits.
    Sleep(150);
    // A selection is the round trip with an unambiguous reply: the host answers
    // ControlWindowSelected, and the status carries it. There is no version counter on the
    // snapshot, so the observable is that string appearing after the request goes out.
    const uint64_t beforeUdp = qpc_now_us();
    const bool udpAnswered =
        udpClient.RequestDesktopMode() &&
        wait_until([&] {
          return udpClient.Snapshot().status.find("window_selected") != std::string::npos;
        }, 5000);
    const uint64_t udpUs = qpc_now_us() - beforeUdp;

    tcpSide.join();

    std::printf("  tcp preview %llums / udp round trip %llums\n",
                (unsigned long long)(tcpUs.load() / 1000),
                (unsigned long long)(udpUs / 1000));

    check("the TCP session's preview request is answered", tcpAnswered.load());
    check("...with no preview, because the helper never answers", !tcpPresent.load());

    // Deliberately not "it took a deadline". Which of the two pays one depends on who asked
    // first: the UDP session fetches previews on its own, so by the time the TCP request arrives
    // the window is often already in cooldown and the answer is immediate. That is the budget
    // being SHARED between the dispatchers, which is the behaviour, not a missed measurement.
    check("...within one deadline", tcpUs.load() < kHostDeadlineUs * 2,
          std::to_string(tcpUs.load() / 1000) + "ms");

    check("THE UDP SESSION ANSWERED WHILE THE TCP ONE WAS ASKING", udpAnswered,
          "two dispatchers, one helper that never answers");
    check("...within one deadline too", udpAnswered && udpUs < kHostDeadlineUs * 2,
          std::to_string(udpUs / 1000) + "ms");

    // The property the incident was the absence of: neither session waits out the OTHER's
    // deadline on top of its own. Serialised, this pair would cost two.
    check("...and neither waited behind the other",
          (tcpUs.load() + udpUs) < kHostDeadlineUs * 2,
          std::to_string((tcpUs.load() + udpUs) / 1000) + "ms for both, " +
              std::to_string(kHostDeadlineUs * 2 / 1000) + "ms if they had serialised");

    // And the UDP session is still healthy afterwards -- the budget was written from two threads
    // by now, since both dispatchers have asked for previews.
    check("...and the UDP session is still connected at the end",
          udpClient.Snapshot().state == ClientSessionState::Connected,
          udpClient.Snapshot().status);
  }

  udpClient.Disconnect();
  if (tcpControl != INVALID_SOCKET) closesocket(tcpControl);

  CloseHandle(job);
  bool hostGone = true;
  if (hostPi.hProcess) {
    hostGone = WaitForSingleObject(hostPi.hProcess, 15000) == WAIT_OBJECT_0;
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);
  check("the host is gone when the job closes", hostGone);

  for (int i = 0; i < 30; ++i) {
    DeleteFileW((dir + L"GNLinkStream.exe").c_str());
    DeleteFileW((dir + L"GNLinkCapture.exe").c_str());
    if (RemoveDirectoryW(dir.substr(0, dir.size() - 1).c_str())) break;
    Sleep(100);
  }
  check("the scratch directory is cleaned up",
        GetFileAttributesW(dir.substr(0, dir.size() - 1).c_str()) == INVALID_FILE_ATTRIBUTES);
  WSACleanup();

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  ("
            << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}

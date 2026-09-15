// A real control session, with a preview that will never arrive.
//
// Everything before this measured pieces: the budget alone, the helper alone, the two composed by
// a test rather than by the product. What none of them touched is the thing that actually broke
// on 2026-09-15 -- the dispatcher. One window's preview held it for 1h50m and every other request
// on that host waited behind it, which is why sessions connected, listed windows, showed a picker
// and then died at the fifteen second mark.
//
// So this runs the real GNLinkStream, connects to its real control port, and asks the question
// directly: while a preview is timing out, does the control channel still answer anything else?
//
// Isolation. A scratch directory, ports nobody else is on, and a GNLinkCapture.exe of this test's
// choosing -- one that never answers, so every preview times out and the dispatcher is under the
// worst load the fix is meant to survive. The installed product is not touched, not stopped, not
// read. The host process lives in a job object and dies with this one.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
// Before any project header: windows.h defines min/max as macros and native_socket.hpp calls
// std::min. Same undef the capture worker does.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"

namespace {

using remote60::native_poc::ControlOutboundAction;
using remote60::native_poc::ControlOutboundActionKind;
using remote60::native_poc::execute_control_action;
using remote60::native_poc::fetch_window_thumbnail;
using remote60::native_poc::kMagic;
using remote60::native_poc::MessageType;
using remote60::native_poc::qpc_now_us;
using remote60::native_poc::TcpControlLink;
using remote60::native_poc::TcpControlResponse;
using remote60::native_poc::WindowThumbnailReply;

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

// The host's own deadline, from host_thumbnail_helper.hpp. Not included directly: this test links
// the client side, and the number is what the host is expected to honour rather than something
// shared with it.
constexpr uint64_t kHostDeadlineUs = 1000ull * 1000;
// What "the channel is still answering" has to mean. Two orders of magnitude under the deadline;
// anything near a second would be the dispatcher having waited.
constexpr uint64_t kPromptUs = 250ull * 1000;

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

/** One ping, and how long the channel took to answer it. */
bool ping(TcpControlLink& link, uint64_t* elapsedUs) {
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
  const bool ok = execute_control_action(link, action, &response);
  *elapsedUs = qpc_now_us() - start;
  return ok;
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
    print_skip("host_control_deadline_e2e_test (starts a listening host)");
    return 0;
  }

  // Helper mode: this executable is also the GNLinkCapture that never answers.
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--thumbnail") {
      Sleep(120000);
      return 0;
    }
  }

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("WSAStartup failed\n");
    return 1;
  }

  // Ports chosen well away from the product's 43000/43001. A host already running on this machine
  // is somebody else's and is not to be disturbed.
  const uint16_t mediaPort = 44720;
  const uint16_t controlPort = 44721;

  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const std::wstring dir = std::wstring(temp) + L"remote60_ctl_e2e_" +
                           std::to_wstring(GetCurrentProcessId()) + L"\\";
  CreateDirectoryW(dir.substr(0, dir.size() - 1).c_str(), nullptr);

  const std::wstring me = self_path();
  const std::wstring myDir = directory_of(me);
  const bool staged =
      CopyFileW((myDir + L"GNLinkStream.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) &&
      CopyFileW(me.c_str(), (dir + L"GNLinkCapture.exe").c_str(), FALSE);
  check("a host and a never-answering helper could be staged", staged,
        staged ? std::string() : "is remote60_native_video_host_poc built?");
  if (!staged) {
    std::printf("\nRESULT: FAILED  (%d checks, %d failed)\n", gChecks, gFailures + 1);
    return 1;
  }

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  // --bind-address is not optional here even though it reads that way. It defaults to empty,
  // which resolve_bind_address turns into INADDR_ANY (host_net_io.cpp:33), so every listener this
  // host opens binds the wildcard -- and a wildcard listener on an executable Windows has never
  // seen is what raises the firewall prompt that left Block rules on the user's machine. Loopback
  // is all this test ever dials.
  std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --bind-address 127.0.0.1 --bind-port " +
                     std::to_wstring(mediaPort) + L" --control-port " +
                     std::to_wstring(controlPort) + L" --seconds 120";
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION hostPi{};
  const bool launched =
      CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, dir.c_str(), &si, &hostPi) != 0;
  if (launched) {
    AssignProcessToJobObject(job, hostPi.hProcess);
    ResumeThread(hostPi.hThread);
  }
  check("the host started", launched);

  SOCKET media = INVALID_SOCKET;
  SOCKET control = INVALID_SOCKET;
  if (launched) {
    // The control listener does not open until a media client connects: startup_connect_client
    // accepts before startup_start_control_threads runs. Nothing is sent on it -- the connection
    // itself is what the host is waiting for.
    media = connect_local(mediaPort, 40);
    check("a media client could connect, which is what opens the control port",
          media != INVALID_SOCKET);
    if (media != INVALID_SOCKET) {
      control = connect_local(controlPort, 40);
      check("the control port accepted a connection", control != INVALID_SOCKET);
    }
  }

  if (control != INVALID_SOCKET) {
    TcpControlLink link(control);

    uint64_t pingUs = 0;
    check("the control channel answers a ping before anything else happens", ping(link, &pingUs),
          std::to_string(pingUs / 1000) + "ms");
    const uint64_t baselinePing = pingUs;

    // The desktop preview goes through the same helper as any window, so it is enough to make the
    // dispatcher spend a deadline -- and it needs no window to exist.
    WindowThumbnailReply reply;
    const uint64_t beforeThumb = qpc_now_us();
    const bool exchanged = fetch_window_thumbnail(link, 0, 256, 160, qpc_now_us(), &reply);
    const uint64_t thumbUs = qpc_now_us() - beforeThumb;

    check("the thumbnail request is answered rather than dropped", exchanged,
          "a dropped request would desync the stream and end the session");
    check("...with no preview, because the helper never answers", exchanged && !reply.present);
    check("...after about the host's deadline, not forever", exchanged && thumbUs < kHostDeadlineUs * 4,
          std::to_string(thumbUs / 1000) + "ms against a " + std::to_string(kHostDeadlineUs / 1000) +
              "ms deadline");
    check("...and it did take a deadline, so the timeout is what ended it",
          thumbUs > kHostDeadlineUs / 2, std::to_string(thumbUs / 1000) + "ms");

    // The question this file exists to answer.
    uint64_t afterUs = 0;
    const bool stillAlive = ping(link, &afterUs);
    check("THE CONTROL CHANNEL STILL ANSWERS after a preview timed out", stillAlive,
          "this is what failed on 2026-09-15");
    check("...promptly, not behind the next deadline", stillAlive && afterUs < kPromptUs,
          std::to_string(afterUs / 1000) + "ms, baseline " + std::to_string(baselinePing / 1000) +
              "ms");

    // And the budget, from the other side of the wire: keep asking and the host stops paying.
    uint64_t worstLater = 0;
    bool allAnswered = true;
    for (int i = 0; i < 4; ++i) {
      WindowThumbnailReply again;
      const uint64_t a = qpc_now_us();
      if (!fetch_window_thumbnail(link, 0, 256, 160, qpc_now_us(), &again)) allAnswered = false;
      const uint64_t took = qpc_now_us() - a;
      if (took > worstLater) worstLater = took;
    }
    check("repeated requests are all answered", allAnswered);
    check("...and the session survives a window that never produces a preview",
          ping(link, &afterUs) && afterUs < kPromptUs, std::to_string(afterUs / 1000) + "ms");
    std::printf("  worst repeat request: %llums\n", (unsigned long long)(worstLater / 1000));
  }

  if (control != INVALID_SOCKET) closesocket(control);
  if (media != INVALID_SOCKET) closesocket(media);

  // Order matters, and the first version of this got it wrong in a way that leaves a process
  // behind. Closing the job kills the host, but killing is asynchronous: deleting the executable
  // immediately afterwards races a process that still has it open. So the job goes first, then the
  // host is WAITED for, and only then is anything deleted.
  //
  // Waiting needs the handle, which is why it is closed after the wait rather than before.
  CloseHandle(job);
  bool hostGone = true;
  if (hostPi.hProcess) {
    hostGone = WaitForSingleObject(hostPi.hProcess, 10000) == WAIT_OBJECT_0;
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);
  check("the host is gone when the job closes, leaving nothing behind", hostGone);

  // Retried: a file can stay locked for a moment after the process holding it exits.
  for (int i = 0; i < 20; ++i) {
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

// Item 6 and 7 of prelogin-service (prelogin-ui r2), run as processes:
//
//   * a viewer's restart request on a DIRECTORY-AUTHENTICATED session: the phone's session controller
//     (ClientSessionController) -> ControlRebootRequest (84) -> the host's gate (directory auth + the
//     served epoch, worker 1's) -> the admin pipe -> the service's decision (dedup / result cache) ->
//     the OS seam, called ONCE; the same request on an UNauthenticated session: NotAuthorized, seam 0;
//   * the user Host's ready: the host, once its first directory heartbeat succeeded, tells the service
//     over the pipe -- which records the session and start time it established from the pipe.
//
// The host is GNLinkStreamMigrationTest (the product's sources; REMOTE60_STREAM_TEST_SEAM replaces the
// server address with a loopback fixture and lets it name a \\.\pipe\GNLinkTest- pipe), staged as
// GNLinkStream.exe and started with SeShutdownPrivilege DELETED from its token. The service end is
// this process: a test pipe served by the PRODUCT's server and decision (RunPreloginAdminServer /
// HandleAdminRequest), with the product's host check (the client's image is the staged GNLinkStream.exe)
// and an OS seam that only counts. What differs from the product: the admin check is stood in for (the
// test host is not elevated), and the OS seam does not restart anything.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <sddl.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "directory_fake_server.hpp"
#include "e2e_isolation.hpp"
#include "native_video_client_session.hpp"
#include "poc_protocol.hpp"
#include "prelogin_admin_pipe.hpp"
#include "reboot_wire.hpp"

using remote60::native_poc::test_support::FakeDirectory;
using remote60::native_poc::test_support::Reply;
namespace pl = remote60::native_poc::prelogin;

namespace {

class NullSink : public remote60::native_poc::ClientEncodedFrameSink {
 public:
  void OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& frame) override { (void)frame; }
  void OnVideoStreamReset() override {}
};

int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}

bool wait_until(const std::function<bool()>& done, uint32_t budgetMs) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
  while (std::chrono::steady_clock::now() < deadline) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return done();
}

std::wstring self_path() {
  wchar_t path[MAX_PATH * 2]{};
  GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
  return path;
}
std::wstring directory_of(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring(L".") : path.substr(0, slash + 1);
}

HANDLE token_without_shutdown_privilege() {
  HANDLE self = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, &self)) return nullptr;
  LUID_AND_ATTRIBUTES del{};
  LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &del.Luid);
  HANDLE restricted = nullptr;
  const BOOL ok = CreateRestrictedToken(self, 0, 0, nullptr, 1, &del, 0, nullptr, &restricted);
  CloseHandle(self);
  return ok ? restricted : nullptr;
}

int process_has_shutdown_privilege(HANDLE process) {
  HANDLE tok = nullptr;
  if (!OpenProcessToken(process, TOKEN_QUERY, &tok)) return -1;
  DWORD need = 0;
  GetTokenInformation(tok, TokenPrivileges, nullptr, 0, &need);
  std::vector<uint8_t> buf(need ? need : 1);
  int result = -1;
  if (GetTokenInformation(tok, TokenPrivileges, buf.data(), need, &need)) {
    LUID shutdown{};
    LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &shutdown);
    const auto* tp = reinterpret_cast<const TOKEN_PRIVILEGES*>(buf.data());
    result = 0;
    for (DWORD i = 0; i < tp->PrivilegeCount; ++i) {
      if (tp->Privileges[i].Luid.LowPart == shutdown.LowPart && tp->Privileges[i].Luid.HighPart == shutdown.HighPart) result = 1;
    }
  }
  CloseHandle(tok);
  return result;
}

/** The host, its stdout readable. */
struct Host {
  PROCESS_INFORMATION pi{};
  HANDLE job = nullptr;
  HANDLE readEnd = nullptr;
  std::thread reader;
  std::atomic<bool> reading{false};
  std::mutex mu;
  std::string output;
  std::wstring dir;
  remote60::native_poc::e2e::StagingDir staging;
  std::string log() {
    std::lock_guard<std::mutex> l(mu);
    return output;
  }
  bool WaitFor(const char* marker, uint32_t ms) {
    return wait_until([&] { return log().find(marker) != std::string::npos; }, ms);
  }
  int Count(const std::string& marker) {
    const std::string all = log();
    int n = 0;
    for (size_t at = all.find(marker); at != std::string::npos; at = all.find(marker, at + marker.size())) ++n;
    return n;
  }
  void Stop() {
    reading = false;
    if (job) {
      CloseHandle(job);
      job = nullptr;
    }
    if (readEnd) {
      CloseHandle(readEnd);
      readEnd = nullptr;
    }
    if (reader.joinable()) reader.join();
    if (pi.hProcess) {
      WaitForSingleObject(pi.hProcess, 20000);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      pi = PROCESS_INFORMATION{};
    }
    if (staging.created()) staging.Remove();
  }
  ~Host() { Stop(); }
};

bool StartHost(Host* host, const std::string& url, uint16_t mediaPort, const std::wstring& pipe, HANDLE restricted) {
  if (!host->staging.Create(L"reboot_delegate")) return false;
  host->dir = host->staging.path();
  const std::wstring myDir = directory_of(self_path());
  if (!CopyFileW((myDir + L"GNLinkStreamMigrationTest.exe").c_str(), (host->dir + L"GNLinkStream.exe").c_str(), FALSE) ||
      !CopyFileW(self_path().c_str(), (host->dir + L"GNLinkCapture.exe").c_str(), FALSE)) {
    return false;
  }
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE writeEnd = nullptr;
  if (!CreatePipe(&host->readEnd, &writeEnd, &sa, 1 << 20)) return false;
  SetHandleInformation(host->readEnd, HANDLE_FLAG_INHERIT, 0);
  host->job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(host->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
  SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
  const std::wstring wurl(url.begin(), url.end());
  std::wstring cmd = L"\"" + host->dir + L"GNLinkStream.exe\" --transport udp --codec h264 --bind-address 127.0.0.1" +
                     L" --bind-port " + std::to_wstring(mediaPort) + L" --directory-url " + wurl +
                     L" --directory-id tester --directory-pw e2e-pass-1234 --seconds 90" + L" --directory-cache \"" + host->dir +
                     L"host_cache.json\"";
  std::string why;
  if (!remote60::native_poc::e2e::e2e_command_is_isolated(cmd, host->dir, &why)) {
    std::printf("FAIL  the host command is not isolated: %s\n", why.c_str());
    return false;
  }
  const std::wstring appData = host->dir + L"localappdata";
  CreateDirectoryW(appData.c_str(), nullptr);
  std::vector<wchar_t> env = remote60::native_poc::e2e::e2e_isolated_environment(appData);
  env.pop_back();  // re-terminated below
  for (const std::wstring& kv : {L"GNLINK_STREAM_TEST_DIRECTORY=" + wurl, L"GNLINK_STREAM_TEST_ADMIN_PIPE=" + pipe,
                                 L"GNLINK_STREAM_TEST_ADMIN_SERVER_PID=" + std::to_wstring(GetCurrentProcessId())}) {
    env.insert(env.end(), kv.begin(), kv.end());
    env.push_back(L'\0');
  }
  env.push_back(L'\0');
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = writeEnd;
  si.hStdError = writeEnd;
  const bool launched = CreateProcessAsUserW(restricted, nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                                             CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, env.data(),
                                             host->dir.c_str(), &si, &host->pi) != FALSE;
  CloseHandle(writeEnd);
  if (!launched) return false;
  AssignProcessToJobObject(host->job, host->pi.hProcess);
  ResumeThread(host->pi.hThread);
  host->reading = true;
  host->reader = std::thread([host] {
    char buf[4096];
    DWORD got = 0;
    while (host->reading.load() && host->readEnd && ReadFile(host->readEnd, buf, sizeof(buf), &got, nullptr) && got > 0) {
      std::lock_guard<std::mutex> l(host->mu);
      host->output.append(buf, got);
    }
  });
  return true;
}

// The service's world: the OS seam only counts; a ready is recorded.
struct Service {
  std::mutex mu;
  int osReboots = 0;
  int readies = 0;
  uint32_t readySession = 0;
  uint64_t readyStart = 0;
  bool readyAfterHeartbeat = false;
  std::function<bool()> heartbeatSeen;
  pl::RebootLedger ledger;
  std::atomic<bool> run{false};
  std::thread t;
  void start(const std::wstring& pipe, const std::wstring& hostImage) {
    run = true;
    t = std::thread([this, pipe, hostImage] {
      pl::AdminPrimitives p;
      p.recordPath = [] { return std::wstring(); };
      p.machineId = [] { return std::string(); };
      p.readFloor = [] { return uint64_t{0}; };
      p.raiseFloor = [](uint64_t) { return true; };
      p.readRecord = [](const std::wstring&, pl::MachineRecord*) { return false; };
      p.writeRecord = [](const std::wstring&, const pl::MachineRecord&) -> uint32_t { return 5; };
      p.removeRecord = [](const std::wstring&) -> uint32_t { return 0; };
      p.featureEnabled = [] { return true; };  // r5 G11: pre-login access is on, as the service says
      p.queryStartType = [](uint32_t*) { return false; };
      p.setStartType = [](uint32_t) -> uint32_t { return 5; };
      p.loadRememberedStartType = [](uint32_t*) { return false; };
      p.rememberStartType = [](uint32_t) { return false; };
      p.forgetRememberedStartType = [] {};
      p.requestOsReboot = [this]() -> uint32_t {
        std::lock_guard<std::mutex> l(mu);
        ++osReboots;
        return 0;  // "the OS accepted it" -- nothing is restarted
      };
      p.noteUserHostReady = [this](uint32_t s, uint64_t st) {
        std::lock_guard<std::mutex> l(mu);
        ++readies;
        readySession = s;
        readyStart = st;
        readyAfterHeartbeat = heartbeatSeen && heartbeatSeen();
      };
      // The user's own DACL (the test host is not elevated); the product's host check by image.
      HANDLE tok = nullptr;
      OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
      DWORD need = 0;
      GetTokenInformation(tok, TokenUser, nullptr, 0, &need);
      std::string buf(need, '\0');
      GetTokenInformation(tok, TokenUser, buf.data(), need, &need);
      CloseHandle(tok);
      LPWSTR sid = nullptr;
      ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &sid);
      const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid) + L")";
      LocalFree(sid);
      pl::RunPreloginAdminServer(p, [this] { return run.load(); }, pipe.c_str(), sddl.c_str(), [](void*) { return true; },
                                 [hostImage](void* h) { return pl::ClientImageIs(h, hostImage); }, &ledger);
    });
    Sleep(200);
  }
  void stop() {
    run = false;
    if (t.joinable()) t.join();
  }
  int reboots() {
    std::lock_guard<std::mutex> l(mu);
    return osReboots;
  }
};

bool host_e2e_allowed() {
  wchar_t value[8]{};
  const DWORD n = GetEnvironmentVariableW(L"REMOTE60_ALLOW_HOST_E2E", value, 8);
  return n > 0 && value[0] == L'1';
}

bool wait_outcome(remote60::native_poc::ClientSessionController& c, uint32_t* id, int* outcome, uint32_t* win32) {
  return wait_until([&] { return c.TakeRebootOutcome(id, outcome, win32); }, 10000);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--thumbnail") {
      Sleep(120000);
      return 0;
    }
  }
  if (!host_e2e_allowed()) {
    std::printf("SKIP  host_reboot_delegate_e2e_test (starts a listening host)\n      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return remote60::native_poc::e2e::kE2eSkippedExit;
  }
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

  // The authenticated client's own socket: the capability is issued for exactly this tuple.
  SOCKET clientSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in local{};
  local.sin_family = AF_INET;
  InetPtonW(AF_INET, L"127.0.0.1", &local.sin_addr);
  bind(clientSock, reinterpret_cast<sockaddr*>(&local), sizeof(local));
  int len = sizeof(local);
  getsockname(clientSock, reinterpret_cast<sockaddr*>(&local), &len);
  const uint16_t clientPort = ntohs(local.sin_port);
  const std::string token(32, 'r');

  FakeDirectory dir;
  check("the fake directory starts", dir.Start());
  dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" + std::to_string(dir.udpPort()) + "}}"}});
  dir.Script("/api/host/register", {Reply{200, "{\"ok\":true,\"hostId\":\"h-reboot\",\"hostToken\":\"" + std::string(32, 'e') + "\"}"}});
  dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true,\"pendingPunch\":[{\"ip\":\"127.0.0.1\",\"port\":" +
                                                    std::to_string(clientPort) + ",\"punchToken\":\"" + token + "\"}]}"}});
  SetEnvironmentVariableW(L"REMOTE60_DIRECTORY_HEARTBEAT_SEC", L"120");

  const std::wstring pipe = L"\\\\.\\pipe\\GNLinkTest-reboot-" + std::to_wstring(GetCurrentProcessId());
  HANDLE restricted = token_without_shutdown_privilege();
  Host host;
  const uint16_t mediaPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
  const bool started = restricted && mediaPort != 0 && StartHost(&host, dir.url(), mediaPort, pipe, restricted);
  check("the host (test build, staged as GNLinkStream.exe) started without SeShutdownPrivilege", started);
  const int priv = started ? process_has_shutdown_privilege(host.pi.hProcess) : -1;
  const bool safe = priv == 0;
  check("SAFETY: the host's token holds no SeShutdownPrivilege at all", safe, std::to_string(priv));
  Service service;
  if (started && safe) {
    service.heartbeatSeen = [&host] { return host.log().find("step=heartbeat ok=1") != std::string::npos; };
    service.start(pipe, host.dir + L"GNLinkStream.exe");

    // ---- item 7: the ready, once the first heartbeat succeeded
    check("the host collects the capability from its first heartbeat", host.WaitFor("directory punch ->", 30000));
    const bool ready = wait_until([&] { std::lock_guard<std::mutex> l(service.mu); return service.readies >= 1; }, 15000);
    FILETIME c{}, e{}, k{}, u{};
    GetProcessTimes(host.pi.hProcess, &c, &e, &k, &u);
    const uint64_t hostStart = (static_cast<uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
    DWORD hostSession = 0;
    ProcessIdToSessionId(host.pi.dwProcessId, &hostSession);
    {
      std::lock_guard<std::mutex> l(service.mu);
      check("ITEM 7: the host's ready reached the service after it registered, with ITS session and start time",
            ready && service.readySession == hostSession && service.readyStart == hostStart,
            std::to_string(service.readies) + " readies");
      check("...and only after the host's first successful heartbeat (registered), not at start",
            service.readyAfterHeartbeat);
    }
    pl::UserHostReadyTracker tracker;
    {
      std::lock_guard<std::mutex> l(service.mu);
      tracker.Note(service.readySession, service.readyStart);
    }
    check("...which completes a handoff launched before this host started (same session)", tracker.Completes(hostSession, hostStart - 1));
    check("...and not one launched after it (a later LOGON: this host's ready is stale)", !tracker.Completes(hostSession, hostStart + 10000000));

    // ---- item 6, unauthenticated: the gate stops it
    {
      NullSink sink;
      remote60::native_poc::ClientSessionController plain;
      remote60::native_poc::ClientSessionConnectArgs args;
      args.host = "127.0.0.1";
      args.videoPort = mediaPort;
      args.controlPort = mediaPort;
      args.requireUdpHello = true;
      args.requireTcpControl = false;
      args.controlOverUdp = true;
      args.controlIntervalMs = 200;
      args.encodedFrameSink = &sink;
      check("an unauthenticated session connects", plain.Connect(args) &&
                wait_until([&] { return plain.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000));
      wait_until([&] { return (plain.HostCaptureFlags() & remote60::native_poc::kCaptureFlagRemoteRebootV1) != 0; }, 8000);
      const uint32_t id = plain.RequestReboot();
      uint32_t gotId = 0, win32 = 0;
      int outcome = -1;
      const bool answered = id != 0 && wait_outcome(plain, &gotId, &outcome, &win32);
      check("ITEM 6, unauthenticated: NotAuthorized, and the service's OS seam was NOT called",
            answered && gotId == id && outcome == 2 && service.reboots() == 0, std::to_string(outcome));
      plain.Disconnect();
      Sleep(500);
    }

    // ---- item 6, authenticated: the whole chain, the OS seam once
    {
      // Whatever the host's punch left on the socket is not the session's.
      u_long nb = 1;
      ioctlsocket(clientSock, FIONBIO, &nb);
      char drain[2048];
      while (recv(clientSock, drain, sizeof(drain), 0) > 0) {
      }
      nb = 0;
      ioctlsocket(clientSock, FIONBIO, &nb);
      NullSink sink;
      remote60::native_poc::ClientSessionController authed;
      remote60::native_poc::ClientSessionConnectArgs args;
      args.host = "127.0.0.1";
      args.videoPort = mediaPort;
      args.controlPort = mediaPort;
      args.requireUdpHello = true;
      args.requireTcpControl = false;
      args.controlOverUdp = true;
      args.controlIntervalMs = 200;
      args.peerAuthToken = token;
      args.udpHandshakeTimeoutMs = 4000;
      args.preparedUdpSocket = static_cast<remote60::native_poc::SocketHandle>(clientSock);
      args.encodedFrameSink = &sink;
      clientSock = INVALID_SOCKET;  // the session owns it now
      const bool up = authed.Connect(args) &&
                      wait_until([&] { return authed.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 20000);
      check("a session with the directory's capability connects (directory-authenticated)", up, authed.Snapshot().status);
      wait_until([&] { return (authed.HostCaptureFlags() & remote60::native_poc::kCaptureFlagRemoteRebootV1) != 0; }, 8000);
      // r5 G11: a LOGGED-IN host (not --prelogin) whose service has pre-login access on says so, so the
      // restart popup does not warn "no connect until someone logs in".
      const bool durable =
          wait_until([&] { return (authed.HostCaptureFlags() & remote60::native_poc::kCaptureFlagPreloginEnabledV1) != 0; }, 8000);
      check("G11: the logged-in host advertises durable pre-login (0x8000) because its service says it is on", durable,
            std::to_string(authed.HostCaptureFlags()));
      check("...and not \"at the logon screen now\" (0x2000)", (authed.HostCaptureFlags() & remote60::native_poc::kCaptureFlagPreloginV1) == 0);
      const uint32_t id = authed.RequestReboot();
      uint32_t gotId = 0, win32 = 0;
      int outcome = -1;
      const bool answered = id != 0 && wait_outcome(authed, &gotId, &outcome, &win32);
      check("ITEM 6: 84 -> the host's gate -> the pipe -> the service: Accepted for that request",
            answered && gotId == id && outcome == 0, std::to_string(outcome) + " win32 " + std::to_string(win32));
      check("...and the service's OS seam was called exactly ONCE", service.reboots() == 1, std::to_string(service.reboots()));
      const uint32_t id2 = authed.RequestReboot();
      uint32_t gotId2 = 0;
      int outcome2 = -1;
      const bool answered2 = id2 != 0 && wait_outcome(authed, &gotId2, &outcome2, &win32);
      check("a second request while the restart is under way: not Accepted, the OS seam still ONCE",
            answered2 && outcome2 != 0 && service.reboots() == 1, std::to_string(outcome2));
      check("the host logged the authorized request (auth=1 result=0) and did not call the OS itself",
            host.Count("[native-video-host][control][reboot] requestId=" + std::to_string(id) + " auth=1 result=0") == 1);
      authed.Disconnect();
    }
  }
  service.stop();
  host.Stop();
  if (clientSock != INVALID_SOCKET) closesocket(clientSock);
  if (restricted) CloseHandle(restricted);
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

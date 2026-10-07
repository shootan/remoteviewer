// prelogin-ui r9 (W3 + the W1 facts): the handoff's ready, end to end -- GNLinkHost -> GNLinkStream -> the
// service -- and what a second GNLinkHost does, which the service's start policy rests on.
//
// The service starts the user's GNLinkHost "--tray" with REMOTE60_PRELOGIN_READY_NONCE in its environment
// (prelogin_user_host_launch.cpp). GNLinkHost passes its environment to its streaming child
// (stream_child_env.hpp); the child, once registered with the directory, sends Ready over the admin pipe;
// the service completes the handoff only for THAT launch's nonce, session and start time
// (UserHostReadyTracker). No test ran that chain: each link was tested on its own (the verifier's W3).
//
// Here: GNLinkHost's TEST build (GNLinkHostUiTest) started "--tray" with a launch nonce, the way the service
// starts it; its child is the streaming TEST build (GNLinkStreamMigrationTest, staged in the fixture root);
// a fake directory; this process serves the admin pipe with the product's server and decision. Then a
// second GNLinkHost "--tray" (what the c196833 service started every second): it must leave with exit 0,
// and that exit code, fed to the product start policy, must not start a third.
//
// What differs from the product (product-equivalent test build, the rest is "G only"): the two TEST builds
// (fixture directory, fixture admin pipe, no elevation -- the pipe's admin check is stood in for), this
// process's token (with no SeShutdownPrivilege) instead of the WTSQueryUserToken token, and no SYSTEM
// service: the service's CreateProcessAsUser and the ProgramData record chain are covered elsewhere
// (prelogin_user_host_launch_test, prelogin_store_chain_test) and on the real machine (G).

#include <winsock2.h>
#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "directory_client.hpp"
#include "directory_fake_server.hpp"
#include "e2e_isolation.hpp"
#include "prelogin_admin_pipe.hpp"
#include "prelogin_user_host_policy.hpp"

using remote60::native_poc::test_support::FakeDirectory;
using remote60::native_poc::test_support::Reply;
namespace pl = remote60::native_poc::prelogin;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
bool wait_until(const std::function<bool()>& done, uint32_t ms) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return done();
}
std::string narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }

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

std::wstring user_sddl() {
  HANDLE tok = nullptr;
  OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
  DWORD need = 0;
  GetTokenInformation(tok, TokenUser, nullptr, 0, &need);
  std::string buf(need, '\0');
  GetTokenInformation(tok, TokenUser, buf.data(), need, &need);
  CloseHandle(tok);
  LPWSTR sid = nullptr;
  ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &sid);
  std::wstring s = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid) + L")";
  LocalFree(sid);
  return s;
}

uint64_t filetime_now() {
  FILETIME f{};
  GetSystemTimeAsFileTime(&f);
  return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}

// The service's world for this chain: readies are recorded; nothing else is asked of it.
struct Service {
  std::mutex mu;
  std::vector<std::pair<std::string, uint64_t>> readies;  // nonce, process start
  uint32_t readyPid = 0;
  uint32_t readySession = 0;
  pl::UserHostReadyTracker tracker;
  pl::RebootLedger ledger;
  std::atomic<bool> run{false};
  std::thread t;
  void start(const std::wstring& pipe, const std::wstring& streamImage) {
    run = true;
    t = std::thread([this, pipe, streamImage] {
      pl::AdminPrimitives p;
      p.recordPath = [] { return std::wstring(); };
      p.machineId = [] { return std::string(); };
      p.readFloor = [](uint64_t* out) { *out = 0; return true; };
      p.raiseFloor = [](uint64_t) { return true; };
      p.readRecord = [](const std::wstring&, pl::MachineRecord*) { return false; };
      p.writeRecord = [](const std::wstring&, const pl::MachineRecord&) -> uint32_t { return 5; };
      p.removeRecord = [](const std::wstring&) -> uint32_t { return 0; };
      p.featureEnabled = [] { return true; };  // pre-login access is on
      p.queryStartType = [](uint32_t*) { return false; };
      p.setStartType = [](uint32_t) -> uint32_t { return 5; };
      p.loadRememberedStartType = [](uint32_t*) { return false; };
      p.rememberStartType = [](uint32_t) { return false; };
      p.forgetRememberedStartType = [] {};
      p.noteUserHostReady = [this](uint32_t s, uint64_t started, const std::string& n, uint32_t pid) {
        std::lock_guard<std::mutex> l(mu);
        readies.emplace_back(n, started);
        readySession = s;
        readyPid = pid;
        tracker.Note(s, started, n, pid);
      };
      const std::wstring sddl = user_sddl();
      pl::RunPreloginAdminServer(p, [this] { return run.load(); }, pipe.c_str(), sddl.c_str(), [](void*) { return true; },
                                 [streamImage](void* h) { return pl::ClientImageIs(h, streamImage); }, &ledger);
    });
    Sleep(200);
  }
  void stop() {
    run = false;
    if (t.joinable()) t.join();
  }
};

struct Proc {
  PROCESS_INFORMATION pi{};
  bool started = false;
};

Proc start_host_tray(const std::filesystem::path& hostExe, const std::wstring& env, HANDLE token, HANDLE job) {
  Proc p;
  std::wstring cmd = L"\"" + hostExe.wstring() + L"\" --tray";
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  std::vector<wchar_t> block(env.begin(), env.end());
  p.started = CreateProcessAsUserW(token, nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                                   CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, block.data(), nullptr, &si, &p.pi) != FALSE;
  if (p.started) {
    AssignProcessToJobObject(job, p.pi.hProcess);
    ResumeThread(p.pi.hThread);
  }
  return p;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--thumbnail") {  // staged as GNLinkCapture.exe: a helper that never answers
      Sleep(120000);
      return 0;
    }
  }
  if (GetEnvironmentVariableW(L"REMOTE60_ALLOW_UI_TEST", nullptr, 0) == 0 ||
      GetEnvironmentVariableW(L"REMOTE60_ALLOW_HOST_E2E", nullptr, 0) == 0) {
    std::printf("SKIP  prelogin_ready_chain_e2e_test (starts GNLinkHost's test build and a streaming host)\n"
                "      Set REMOTE60_ALLOW_UI_TEST=1 and REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return remote60::native_poc::e2e::kE2eSkippedExit;
  }
  std::printf("the handoff's ready, GNLinkHost -> GNLinkStream -> service (prelogin-ui r9, W3/W1)\n\n");
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);

  wchar_t self[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  const std::filesystem::path exeDir = std::filesystem::path(self).parent_path();
  const std::filesystem::path hostExe = exeDir / L"GNLinkHostUiTest.exe";
  check("the GNLinkHost and GNLinkStream test builds are beside this test",
        std::filesystem::exists(hostExe) && std::filesystem::exists(exeDir / L"GNLinkStreamMigrationTest.exe"));

  // The fixture: every file the two programs may write lives here.
  std::filesystem::path repo = exeDir;
  while (!repo.empty() && !std::filesystem::exists(repo / L"AGENTS.md")) repo = repo.parent_path();
  const std::filesystem::path root =
      repo / L".claude" / L"test-tmp" / (L"prelogin-ready-chain-" + std::to_wstring(GetCurrentProcessId()));
  std::filesystem::create_directories(root / L"appdata" / L"remote60");
  std::ofstream(root / L".fixture") << "owned test profile";
  const std::filesystem::path stream = root / L"GNLinkStream.exe";
  std::filesystem::copy_file(exeDir / L"GNLinkStreamMigrationTest.exe", stream,
                             std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(self, root / L"GNLinkCapture.exe", std::filesystem::copy_options::overwrite_existing);

  FakeDirectory dir;
  check("the fake directory starts", dir.Start());
  const std::string url = dir.url();
  dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" + std::to_string(dir.udpPort()) + "}}"}});
  dir.Script("/api/host/register", {Reply{200, "{\"ok\":true,\"hostId\":\"h-chain\",\"hostToken\":\"" + std::string(32, 'c') + "\"}"}});
  dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true}"}});
  // Signed in already (the window and its child read the same host.json): this machine, this server.
  const std::filesystem::path cache = root / L"appdata" / L"remote60" / L"host.json";
  {
    std::ofstream hj(cache);
    hj << "{\n  \"directoryUrl\": \"" << url << "\",\n  \"accountId\": \"chain-user\",\n  \"machineId\": \""
       << remote60::native_poc::directory::machine_id() << "\",\n  \"hostName\": \"CHAIN-PC\",\n  \"hostId\": \"h-chain\",\n"
       << "  \"hostToken\": \"" << std::string(32, 'c') << "\"\n}\n";
  }

  const std::wstring pipe = L"\\\\.\\pipe\\GNLinkTest-ready-chain-" + std::to_wstring(GetCurrentProcessId());
  Service service;
  service.start(pipe, stream.wstring());

  const uint16_t mediaPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
  const uint16_t controlPort = remote60::native_poc::e2e::e2e_pick_free_tcp_port();
  auto env_with_nonce = [&](const std::wstring& nonce) {
    std::wstring env;
    LPWCH block = GetEnvironmentStringsW();
    for (LPWCH p = block; *p; p += wcslen(p) + 1) {
      const std::wstring kv = p;
      if (kv.rfind(L"LOCALAPPDATA=", 0) == 0 || kv.rfind(L"GNLINK_", 0) == 0 || kv.rfind(L"REMOTE60_", 0) == 0) continue;
      env += kv;
      env.push_back(L'\0');
    }
    FreeEnvironmentStringsW(block);
    const std::wstring wurl(url.begin(), url.end());
    for (const std::wstring& kv :
         {L"GNLINK_HOST_TEST_ROOT=" + root.wstring(), L"LOCALAPPDATA=" + (root / L"appdata").wstring(),
          L"GNLINK_HOST_TEST_DIRECTORY=" + wurl, L"GNLINK_HOST_TEST_PRELOGIN_PIPE=" + pipe,
          L"GNLINK_HOST_TEST_PRELOGIN_SERVER_PID=" + std::to_wstring(GetCurrentProcessId()),
          L"GNLINK_HOST_TEST_STREAM_EXE=" + stream.wstring(),
          // the streaming test build's fixtures (it inherits GNLinkHost's environment)
          L"GNLINK_STREAM_TEST_DIRECTORY=" + wurl, L"GNLINK_STREAM_TEST_CACHE=" + cache.wstring(),
          L"GNLINK_STREAM_TEST_ADMIN_PIPE=" + pipe,
          L"GNLINK_STREAM_TEST_ADMIN_SERVER_PID=" + std::to_wstring(GetCurrentProcessId()),
          std::wstring(L"GNLINK_STREAM_TEST_BIND_ADDRESS=127.0.0.1"),
          L"GNLINK_STREAM_TEST_BIND_PORT=" + std::to_wstring(mediaPort),
          L"GNLINK_STREAM_TEST_CONTROL_PORT=" + std::to_wstring(controlPort),
          std::wstring(L"REMOTE60_DIRECTORY_HEARTBEAT_SEC=120"),
          // what the service puts in (prelogin_user_host_launch.cpp BuildUserHostEnvironment)
          L"REMOTE60_PRELOGIN_READY_NONCE=" + nonce}) {
      env += kv;
      env.push_back(L'\0');
    }
    env.push_back(L'\0');
    return env;
  };

  HANDLE token = token_without_shutdown_privilege();
  check("SAFETY: the programs run with a token holding no SeShutdownPrivilege", token != nullptr);
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  DWORD mySession = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &mySession);
  const uint64_t launchA = filetime_now();
  Proc a = token ? start_host_tray(hostExe, env_with_nonce(L"chain-launch-A"), token, job) : Proc{};
  check("GNLinkHost (test build) started \"--tray\" with a launch nonce, as the service starts it", a.started);

  const bool readyArrived = a.started && wait_until([&] {
    std::lock_guard<std::mutex> l(service.mu);
    return !service.readies.empty();
  }, 45000);
  {
    std::lock_guard<std::mutex> l(service.mu);
    const std::string nonce = readyArrived ? service.readies.front().first : std::string();
    check("W3: a Ready reached the service -- from GNLinkHost's streaming child, after it registered", readyArrived,
          std::to_string(dir.Count("/api/host/heartbeat")) + " heartbeats");
    check("W3: ...carrying the nonce the service launched GNLinkHost with (passed through to its child)",
          nonce == "chain-launch-A", nonce);
    check("W3: ...from this session", service.readySession == mySession, std::to_string(service.readySession));
    check("W3: ...which completes the handoff for that launch (session, launch time, nonce)",
          service.tracker.Completes(mySession, launchA, "chain-launch-A"));
    check("...but not for another launch's nonce", !service.tracker.Completes(mySession, launchA, "chain-launch-B"));
  }
  // r10: had the service ADOPTED this GNLinkHost (a Host it did not start: no nonce of its own), the same
  // ready completes the handoff through who sent it -- the real parent of the real sender is that Host.
  {
    const auto parentOf = [](uint32_t pid, uint64_t start) { return pl::ParentProcessOf(pid, start); };
    check("r10: the ready's sender is GNLinkHost's own streaming child (its real parent)",
          service.tracker.CompletesAdopted(mySession, a.pi.dwProcessId, parentOf), std::to_string(service.readyPid));
    check("...and it does not complete it for another process taken for the adopted Host",
          !service.tracker.CompletesAdopted(mySession, GetCurrentProcessId(), parentOf));
  }

  // W1: what the c196833 service did next -- start another GNLinkHost "--tray" while A runs.
  const size_t readiesBefore = [&] {
    std::lock_guard<std::mutex> l(service.mu);
    return service.readies.size();
  }();
  Proc b = start_host_tray(hostExe, env_with_nonce(L"chain-launch-B"), token, job);
  DWORD bCode = STILL_ACTIVE;
  const bool bEnded = b.started && WaitForSingleObject(b.pi.hProcess, 10000) == WAIT_OBJECT_0 &&
                      GetExitCodeProcess(b.pi.hProcess, &bCode);
  check("W1: a second GNLinkHost \"--tray\" while the user's runs leaves at once, exit 0 (single instance)",
        bEnded && bCode == 0, std::to_string(bCode));
  Sleep(3000);
  {
    std::lock_guard<std::mutex> l(service.mu);
    check("...and no ready of its launch ever comes (why the old service started one every second)",
          service.readies.size() == readiesBefore &&
              !service.tracker.Completes(mySession, launchA, "chain-launch-B"));
  }
  // The product start policy, fed what the service would observe: B (started) ended with that exit code,
  // A runs in the session.
  pl::UserHostLaunchPolicy policy;
  pl::UserHostObservation o;
  o.logon = 1;
  o.nowMs = 1000;
  (void)policy.Decide(o);  // the first tick: B is started
  o.nowMs = 1500;
  o.watchedExited = bEnded;
  o.watchedExitCode = bCode;
  o.otherRunning = WaitForSingleObject(a.pi.hProcess, 0) == WAIT_TIMEOUT;
  const pl::UserHostDecision next = policy.Decide(o);
  int launches = 0;
  for (int t = 0; t < 30; ++t) {
    pl::UserHostObservation w;
    w.logon = 1;
    w.nowMs = 2000 + t * 1000;
    w.watchedAlive = true;  // A, adopted
    if (policy.Decide(w) == pl::UserHostDecision::Launch) ++launches;
  }
  check("W1: with B's real exit code and A running, the start policy adopts A and starts nothing more",
        next == pl::UserHostDecision::Adopt && launches == 0, std::to_string(launches));

  if (b.started) {
    CloseHandle(b.pi.hProcess);
    CloseHandle(b.pi.hThread);
  }
  if (a.started) {
    CloseHandle(a.pi.hProcess);
    CloseHandle(a.pi.hThread);
  }
  CloseHandle(job);  // GNLinkHost and its streaming child with it
  if (token) CloseHandle(token);
  service.stop();
  Sleep(500);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  check("the fixture directory is removed", !std::filesystem::exists(root), ec.message());
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

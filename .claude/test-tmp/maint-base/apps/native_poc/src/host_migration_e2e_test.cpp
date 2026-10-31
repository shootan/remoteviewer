#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// A real streaming host, started the way GNLinkHost starts it, with a sign-in cached under
// another address.
//
// directory_migration_test drives the HostAgent in-process and hands it the list of former names
// itself. What that leaves unrun is the line in host_startup_connect.cpp that hands the list to
// the agent in the product -- and that line is what decides whether an unattended PC comes up
// signed out after an update. So this starts the process.
//
// The process is GNLinkStreamMigrationTest.exe: every source GNLinkStream has, compiled with
// REMOTE60_STREAM_TEST_SEAM, which replaces exactly two constants -- the product's server and
// its former names -- with loopback fixtures read from the environment. host_startup_connect.cpp,
// the agent, the heartbeat and the cache handling are the product's own objects.
//
// What is judged is what each fixture directory RECEIVED and the bytes of the cache file, read
// after the host has exited. Not shown by this: TLS, the real server, the real former name, or
// an installed host going through an update.
//
// Off unless REMOTE60_ALLOW_HOST_E2E=1, like every test that starts a host: it opens listening
// sockets. Skipping is loud and exits 77.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "directory_client.hpp"
#include "directory_fake_server.hpp"
#include "e2e_isolation.hpp"

using remote60::native_poc::test_support::FakeDirectory;
using remote60::native_poc::test_support::Reply;
namespace directory = remote60::native_poc::directory;
namespace e2e = remote60::native_poc::e2e;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ",
              detail.c_str());
}

// Not a real token: nothing issued it. Distinctive, so it can be looked for in what arrived.
const char kToken[] = "fixture-stream-host-token-91be";

bool host_e2e_allowed() {
  wchar_t value[8]{};
  const DWORD n = GetEnvironmentVariableW(L"REMOTE60_ALLOW_HOST_E2E", value, 8);
  return n > 0 && value[0] == L'1';
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

std::wstring widen(const std::string& text) { return std::wstring(text.begin(), text.end()); }
std::string narrow(const std::wstring& text) { return std::string(text.begin(), text.end()); }

std::string read_file(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool wait_until(const std::function<bool()>& done, uint32_t budgetMs) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
  while (std::chrono::steady_clock::now() < deadline) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return done();
}

/** A directory that accepts observations and answers heartbeats with `heartbeat`. */
void script_directory(FakeDirectory& dir, const Reply& heartbeat) {
  dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                         std::to_string(dir.udpPort()) + "}}"}});
  dir.Script("/api/host/heartbeat", {heartbeat});
  // Registering would be a way round the question. The host is given no password, so this is
  // never expected; it is answered so that a host which did try is counted rather than hung.
  dir.Script("/api/host/register", {Reply{401, "{\"error\":\"not in this test\"}"}});
}

/** A spawned streaming host whose output this test can read. */
struct SpawnedHost {
  PROCESS_INFORMATION pi{};
  HANDLE job = nullptr;
  HANDLE readEnd = nullptr;
  std::thread reader;
  std::atomic<bool> reading{false};
  std::mutex mu;
  std::string output;
  std::wstring dir;
  std::wstring cachePath;
  e2e::StagingDir staging;
  std::string cacheBefore;

  std::string log() {
    std::lock_guard<std::mutex> lock(mu);
    return output;
  }
  std::string tail() {
    const std::string all = log();
    return all.size() > 500 ? all.substr(all.size() - 500) : all;
  }
  bool Said(const std::string& marker) { return log().find(marker) != std::string::npos; }
  bool WaitFor(const std::string& marker, uint32_t budgetMs) {
    return wait_until([&] { return Said(marker); }, budgetMs);
  }

  /** Ends the host and waits until it has really gone: the cache is read after this. */
  bool Stop() {
    reading = false;
    if (job) {
      CloseHandle(job);  // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE takes the host with it
      job = nullptr;
    }
    bool exited = true;
    if (pi.hProcess) {
      exited = WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0;
      CloseHandle(pi.hProcess);
    }
    if (readEnd) {
      CloseHandle(readEnd);
      readEnd = nullptr;
    }
    if (reader.joinable()) reader.join();
    if (pi.hThread) CloseHandle(pi.hThread);
    pi = PROCESS_INFORMATION{};
    return exited;
  }

  bool Cleanup() {
    if (!staging.created()) return true;
    return staging.Remove();
  }

  ~SpawnedHost() {
    Stop();
    Cleanup();
  }
};

/** The arguments that keep a test host off the user's files. (RV-00) */
std::wstring isolation_args(const std::wstring& stagingDir) {
  return L" --directory-cache \"" + stagingDir + L"host_cache.json\"";
}

/**
 * Stages and starts the host with a cache that names `cachedUrl`.
 *
 * The command line is the one GNLinkHost builds (host_app_main.cpp, Supervise): a url, an
 * account and a host name, and NO password -- the window never has one to give. The bind
 * address, the ports and the cache path are what make it a test.
 */
bool StartHost(SpawnedHost* host, const char* tag, const std::string& serverUrl,
               const std::string& formerName, const std::string& cachedUrl) {
  const uint16_t mediaPort = e2e::e2e_pick_free_udp_port();
  const uint16_t controlPort = e2e::e2e_pick_free_tcp_port();
  if (mediaPort == 0 || controlPort == 0) {
    std::printf("SKIP-LAUNCH %s: no free port, the host is not started\n", tag);
    return false;
  }
  if (!host->staging.Create(L"migration_" + std::wstring(tag, tag + strlen(tag)))) {
    std::printf("FAIL  %s\n", host->staging.why().c_str());
    return false;
  }
  host->dir = host->staging.path();
  host->cachePath = host->dir + L"host_cache.json";

  const std::wstring myDir = directory_of(self_path());
  const std::wstring exe = host->dir + L"GNLinkStreamMigrationTest.exe";
  if (!CopyFileW((myDir + L"GNLinkStreamMigrationTest.exe").c_str(), exe.c_str(), FALSE) ||
      !CopyFileW(self_path().c_str(), (host->dir + L"GNLinkCapture.exe").c_str(), FALSE)) {
    std::printf("FAIL  could not stage the host (%lu)\n", GetLastError());
    return false;
  }

  // What an install from before this build has on disk.
  directory::HostCache seed;
  seed.directoryUrl = cachedUrl;
  seed.accountId = "tester";
  seed.machineId = directory::machine_id();
  seed.hostName = "Fixture PC";
  seed.hostId = "h-cached";
  seed.hostToken = kToken;
  if (!directory::save_host_cache(narrow(host->cachePath), seed)) {
    std::printf("FAIL  could not write the fixture cache\n");
    return false;
  }
  host->cacheBefore = read_file(host->cachePath);

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

  // Carried into the child's environment block below. The heartbeat interval is the product's
  // floor, so the "tried again" cases do not have to sit through 25 seconds.
  SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
  SetEnvironmentVariableW(L"REMOTE60_DIRECTORY_HEARTBEAT_SEC", L"5");
  SetEnvironmentVariableW(L"REMOTE60_DIRECTORY_URL", nullptr);
  SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_DIRECTORY", widen(serverUrl).c_str());
  SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_FORMER_NAME",
                          formerName.empty() ? nullptr : widen(formerName).c_str());

  // Loopback on purpose: a wildcard listener is what raises the firewall prompt.
  std::wstring cmd = L"\"" + exe + L"\" --transport udp --codec h264" +
                     L" --bind-address 127.0.0.1" +
                     L" --bind-port " + std::to_wstring(mediaPort) +
                     L" --control-port " + std::to_wstring(controlPort) +
                     L" --directory-url " + widen(serverUrl) +
                     L" --directory-id tester --host-name \"Fixture PC\"" +
                     L" --seconds 60" + isolation_args(host->dir);

  std::string why;
  const bool cmdIsolated = e2e::e2e_command_is_isolated(cmd, host->dir, &why);
  const std::wstring appData = host->dir + L"localappdata";
  CreateDirectoryW(appData.c_str(), nullptr);
  std::vector<wchar_t> env = e2e::e2e_isolated_environment(appData);
  const bool envIsolated = e2e::e2e_path_is_under(e2e::e2e_block_localappdata(env), host->dir);
  check((std::string("[") + tag + "] the host's cache and LOCALAPPDATA are inside the staging directory").c_str(),
        cmdIsolated && envIsolated, why);
  if (!cmdIsolated || !envIsolated) return false;  // never start a host that could reach the user's files

  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = writeEnd;
  si.hStdError = writeEnd;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  const bool launched =
      CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                     CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, env.data(),
                     host->dir.c_str(), &si, &host->pi) != FALSE;
  CloseHandle(writeEnd);
  if (!launched) {
    std::printf("FAIL  the host did not start (%lu)\n", GetLastError());
    return false;
  }
  AssignProcessToJobObject(host->job, host->pi.hProcess);
  ResumeThread(host->pi.hThread);

  host->reading = true;
  host->reader = std::thread([host] {
    char buf[4096];
    DWORD got = 0;
    while (host->reading.load() && host->readEnd &&
           ReadFile(host->readEnd, buf, sizeof(buf), &got, nullptr) && got > 0) {
      std::lock_guard<std::mutex> lock(host->mu);
      host->output.append(buf, got);
    }
  });

  // From the host's own words, not from the arguments this test passed.
  const bool said = host->WaitFor("directory cache path=", 20000);
  const bool usedIt = said && host->Said("directory cache path=" + narrow(host->cachePath) +
                                         " (--directory-cache)");
  const bool seam = host->WaitFor("[stream-test-seam] server=" + serverUrl, 10000);
  check((std::string("[") + tag + "] a real streaming host started, on the staging cache, in the test-seam build").c_str(),
        usedIt && seam, said ? host->tail() : "the host never said which cache it used");
  return usedIt && seam;
}

struct Outcome {
  std::string cacheAfter;
  directory::HostCache after;
  std::string log;
  bool exited = false;
};

Outcome finish(SpawnedHost* host) {
  Outcome out;
  out.exited = host->Stop();
  out.log = host->log();
  out.cacheAfter = read_file(host->cachePath);
  directory::load_host_cache(narrow(host->cachePath), &out.after);
  return out;
}

// ------------------------------------------------------------------------------ the cases

void case_former_name_is_carried_over() {
  FakeDirectory server, former;
  if (!server.Start() || !former.Start()) { check("[carried] fixtures start", false); return; }
  script_directory(server, Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"});
  script_directory(former, Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"});

  SpawnedHost host;
  if (!StartHost(&host, "carried", server.url(), former.url(), former.url())) return;
  const bool beat = wait_until([&] { return server.Count("/api/host/heartbeat") >= 1; }, 30000);
  const bool rewrote = host.WaitFor("the cache now names it", 10000);
  const std::string cacheBefore = host.cacheBefore;
  const Outcome out = finish(&host);

  check("[carried] THE HOST SENT A HEARTBEAT WITH THE CACHED TOKEN, WITHOUT REGISTERING",
        beat && server.RequestsContaining(kToken) >= 1 && server.Count("/api/host/register") == 0,
        "heartbeats: " + std::to_string(server.Count("/api/host/heartbeat")) +
            ", registrations: " + std::to_string(server.Count("/api/host/register")) +
            (beat ? "" : "  " + host.tail()));
  check("[carried] it says it is online", out.log.find("directory online") != std::string::npos);
  check("[carried] the cache now names the server",
        rewrote && directory::directory_origin_key(out.after.directoryUrl) ==
                       directory::directory_origin_key(server.url()),
        out.after.directoryUrl);
  check("[carried] ...with the same token, account and host id",
        out.after.hostToken == kToken && out.after.accountId == "tester" &&
            out.after.hostId == "h-cached");
  check("[carried] ...and it did change: it named the former address before",
        out.cacheAfter != cacheBefore);
  check("[carried] the former name received nothing", former.Requests() == 0,
        "requests: " + std::to_string(former.Requests()));
  check("[carried] the host exited when stopped, and its staging directory is removed",
        out.exited && host.Cleanup(), host.staging.why());
}

void case_unlisted_is_neither_sent_nor_erased() {
  FakeDirectory server, former, elsewhere;
  if (!server.Start() || !former.Start() || !elsewhere.Start()) {
    check("[unlisted] fixtures start", false);
    return;
  }
  for (FakeDirectory* dir : {&server, &former, &elsewhere}) {
    script_directory(*dir, Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"});
  }

  SpawnedHost host;
  if (!StartHost(&host, "unlisted", server.url(), former.url(), elsewhere.url())) return;
  const bool refused = host.WaitFor("no cached host token", 20000);
  // Long enough for a whole cycle: a host that was going to send would have by then.
  std::this_thread::sleep_for(std::chrono::seconds(7));
  const std::string cacheBefore = host.cacheBefore;
  const Outcome out = finish(&host);

  check("[unlisted] NO REQUEST CARRYING THE TOKEN REACHED THE SERVER",
        server.RequestsContaining(kToken) == 0,
        "requests carrying it: " + std::to_string(server.RequestsContaining(kToken)) + " of " +
            std::to_string(server.Requests()));
  check("[unlisted] no heartbeat and no registration were sent",
        server.Count("/api/host/heartbeat") == 0 && server.Count("/api/host/register") == 0);
  check("[unlisted] the address it was cached under, and the former name, received nothing",
        elsewhere.Requests() == 0 && former.Requests() == 0);
  check("[unlisted] the cache is byte for byte what it was", out.cacheAfter == cacheBefore);
  check("[unlisted] the host says a sign-in is needed", refused, host.tail());
  check("[unlisted] the host exited when stopped, and its staging directory is removed",
        out.exited && host.Cleanup(), host.staging.why());
}

void case_rejected_needs_signing_in() {
  FakeDirectory server, former;
  if (!server.Start() || !former.Start()) { check("[401] fixtures start", false); return; }
  script_directory(server, Reply{401, "{\"error\":\"unknown host token\"}"});
  script_directory(former, Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"});

  SpawnedHost host;
  if (!StartHost(&host, "rejected", server.url(), former.url(), former.url())) return;
  const bool beat = wait_until([&] { return server.Count("/api/host/heartbeat") >= 1; }, 30000);
  // One more cycle, so "needs id/pw" -- what the window turns into SIGN IN AGAIN -- is reached.
  // The refusal itself, then one more cycle for the state the host settles in.
  const bool needs = host.WaitFor("[native-video-host] directory host token rejected", 5000) &&
                     host.WaitFor("[native-video-host] directory registration needs id/pw", 12000);
  const std::string cacheBefore = host.cacheBefore;
  const Outcome out = finish(&host);

  check("[401] the token was presented to the server", beat && server.RequestsContaining(kToken) >= 1);
  check("[401] it was presented once, not again after the refusal",
        server.RequestsContaining(kToken) == 1,
        "requests carrying it: " + std::to_string(server.RequestsContaining(kToken)));
  check("[401] the host is not online", out.log.find("directory online") == std::string::npos);
  check("[401] the cache was not rewritten to name the server", out.cacheAfter == cacheBefore,
        out.after.directoryUrl);
  check("[401] the former name received nothing", former.Requests() == 0);
  // What GNLinkHost's window reads to show SIGN IN AGAIN (needs_sign_in_again).
  check("[401] the host says so on the line the window reads: rejected, then sign-in needed",
        needs, host.tail());
  check("[401] the host exited when stopped, and its staging directory is removed",
        out.exited && host.Cleanup(), host.staging.why());
}

void case_server_is_down() {
  FakeDirectory former;
  if (!former.Start()) { check("[down] fixtures start", false); return; }
  script_directory(former, Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"});
  std::string deadUrl;
  {
    FakeDirectory gone;  // an address that was just in use and is not any more
    if (!gone.Start()) { check("[down] fixtures start", false); return; }
    deadUrl = gone.url();
  }

  SpawnedHost host;
  if (!StartHost(&host, "down", deadUrl, former.url(), former.url())) return;
  // Two cycles: the second is the evidence that it kept the token and came back.
  const bool again = host.WaitFor("[dir-cycle] n=2 start", 30000);
  const std::string cacheBefore = host.cacheBefore;
  const Outcome out = finish(&host);

  check("[down] the host tries again on the next cycle", again, host.tail());
  check("[down] the cache keeps the token and the address it was found with",
        out.cacheAfter == cacheBefore, out.after.directoryUrl);
  check("[down] it is not reported as a sign-out",
        out.log.find("registration needs id/pw") == std::string::npos &&
            out.log.find("host token rejected") == std::string::npos);
  check("[down] the former name was not tried instead", former.Requests() == 0,
        "requests: " + std::to_string(former.Requests()));
  check("[down] the host exited when stopped, and its staging directory is removed",
        out.exited && host.Cleanup(), host.staging.why());
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  // Staged over GNLinkCapture.exe by the harness: answer nothing and wait to be killed.
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--thumbnail") {
      Sleep(120000);
      return 0;
    }
  }

  if (!host_e2e_allowed()) {
    std::printf("SKIP  host_migration_e2e_test (starts a listening host)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n");
    std::printf("\nRESULT: SKIPPED\n");
    return e2e::kE2eSkippedExit;
  }

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("FAIL  winsock did not start\n");
    return 1;
  }

  // Each case starts its own host: what a host does with its cache happens once, at start.
  case_former_name_is_carried_over();
  case_unlisted_is_neither_sent_nor_erased();
  case_rejected_needs_signing_in();
  case_server_is_down();

  WSACleanup();
  std::printf("\n%s  (%d checks, %d failed)\n",
              gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}

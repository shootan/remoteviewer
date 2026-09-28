#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// A real host, a real client handshake, and the candidate the client actually ends up talking to.
//
// C1's earlier coverage stopped at the choice: the product's PunchAny picked the private candidate
// because the host answered its punch. What it did not show is that the choice is worth anything --
// that the Hello sent to that private address, carrying a capability the directory issued against
// the host's PUBLIC tuple, is accepted and a session opens. That gap is what this closes.
//
// Everything here is the product: a spawned GNLinkStream.exe talking to a scripted fake directory,
// the product's DirectoryRendezvous for the client side, and the product's udp_hello_handshake on
// the very socket that won the punch race.
//
// Off unless REMOTE60_ALLOW_HOST_E2E=1. Starting a host opens listening sockets, and on Windows an
// unanswered firewall prompt becomes a Block rule on the user's machine -- a regression sweep once
// left eight behind. Skipping is loud and exits 0.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "e2e_isolation.hpp"
#include "directory_fake_server.hpp"
#include "directory_rendezvous.hpp"
#include "native_video_client_tcp_control.hpp"
#include "native_video_client_session.hpp"
#include "poc_protocol.hpp"

using remote60::native_poc::test_support::FakeDirectory;
using remote60::native_poc::test_support::Reply;

namespace {

/** The session wants somewhere to put frames. Nothing here looks at them. */
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

bool wait_until(const std::function<bool()>& done, uint32_t budgetMs) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
  while (std::chrono::steady_clock::now() < deadline) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return done();
}

int gChecks = 0;
int gFailures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ",
              detail.c_str());
}

void note(const std::string& text) { std::printf("      %s\n", text.c_str()); }

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

/** A spawned GNLinkStream whose stdout this test can read. */
struct SpawnedHost {
  PROCESS_INFORMATION pi{};
  HANDLE job = nullptr;
  HANDLE readEnd = nullptr;
  std::thread reader;
  std::atomic<bool> reading{false};
  std::mutex mu;
  std::string output;
  std::wstring dir;
  std::wstring cachePath;  // where this host was told to keep its directory cache

  std::string log() {
    std::lock_guard<std::mutex> lock(mu);
    return output;
  }

  /** The last of it, for a failure message: the whole log is thousands of lines. */
  std::string tail() {
    const std::string all = log();
    return all.size() > 400 ? all.substr(all.size() - 400) : all;
  }

  /** How many times a marker appears. Bounded budgets are counts, not presence. */
  int Count(const std::string& marker) {
    const std::string all = log();
    int n = 0;
    for (size_t at = all.find(marker); at != std::string::npos;
         at = all.find(marker, at + marker.size())) {
      ++n;
    }
    return n;
  }

  /** Every line carrying a marker, joined. What a window failure needs to be readable. */
  std::string Lines(const std::string& marker) {
    const std::string all = log();
    std::string out;
    size_t at = 0;
    while ((at = all.find(marker, at)) != std::string::npos) {
      const size_t begin = all.rfind('\n', at);
      const size_t end = all.find('\n', at);
      out += (out.empty() ? "" : " | ");
      out += all.substr(begin == std::string::npos ? 0 : begin + 1,
                        (end == std::string::npos ? all.size() : end) -
                            (begin == std::string::npos ? 0 : begin + 1));
      at = (end == std::string::npos) ? all.size() : end;
    }
    return out;
  }

  /** Waits for a line the host prints, so the test follows the host rather than a sleep. */
  bool WaitFor(const char* marker, uint32_t budgetMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
    while (std::chrono::steady_clock::now() < deadline) {
      if (log().find(marker) != std::string::npos) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
  }

  void Stop() {
    reading = false;
    if (job) {
      CloseHandle(job);  // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE takes the host with it
      job = nullptr;
    }
    if (readEnd) {
      CloseHandle(readEnd);
      readEnd = nullptr;
    }
    if (reader.joinable()) reader.join();
    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) {
      // The kill is asynchronous: until the host has actually exited its image file is in use
      // and GNLinkStream.exe cannot be deleted -- which is how every case used to leave its
      // staging directory behind (132 of them on one machine). Wait for it first.
      WaitForSingleObject(pi.hProcess, 20000);
      CloseHandle(pi.hProcess);
    }
    pi = PROCESS_INFORMATION{};
    if (!dir.empty()) {
      // Everything the staging directory holds -- the exe copies, the token cache and the
      // diagnostic mirror the isolation arguments caught -- and only inside this one directory.
      wchar_t temp[MAX_PATH]{};
      GetTempPathW(MAX_PATH, temp);
      if (!remote60::native_poc::e2e::e2e_remove_staging_dir(dir, temp)) {
        std::printf("WARN  staging directory not removed: %ls\n", dir.c_str());
      }
      dir.clear();
    }
  }
  ~SpawnedHost() { Stop(); }
};

/**
 * The arguments that keep a test host off the user's files. (RV-00)
 *
 * One function so the mutation test has exactly one thing to take away -- and taking it away
 * makes the pre-launch check refuse, so the host that would have written the real cache is
 * never started.
 */
std::wstring isolation_args(const std::wstring& stagingDir) {
  return L" --directory-cache \"" + stagingDir + L"host_cache.json\"";
}

bool StartHost(SpawnedHost* host, const std::string& directoryUrl, uint16_t mediaPort,
               uint16_t controlPort, const char* tag) {
  if (mediaPort == 0 || controlPort == 0) {
    // A port the picker could not provide must not reach the host: --bind-port 0 reads as "no
    // candidates" and the host would fall back to the product's default port. Nothing is staged,
    // nothing is started; the case fails at its "starts" check.
    std::printf("SKIP-LAUNCH %s: no free port (media udp %u, control tcp %u), the host is not started\n", tag,
                mediaPort, controlPort);
    return false;
  }
  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  host->dir = std::wstring(temp) + L"remote60_punch_e2e_" +
              std::wstring(tag, tag + strlen(tag)) + L"_" +
              std::to_wstring(GetCurrentProcessId()) + L"\\";
  CreateDirectoryW(host->dir.substr(0, host->dir.size() - 1).c_str(), nullptr);

  const std::wstring myDir = directory_of(self_path());
  // GNLinkCapture is staged as a copy of this binary in --thumbnail mode: the host starts one and
  // it must not answer, exactly as the sibling e2e does.
  if (!CopyFileW((myDir + L"GNLinkStream.exe").c_str(),
                 (host->dir + L"GNLinkStream.exe").c_str(), FALSE) ||
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
  const std::wstring url(directoryUrl.begin(), directoryUrl.end());
  // Loopback on purpose: a wildcard listener is what raises the firewall prompt.
  std::wstring cmd = L"\"" + host->dir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                     L" --bind-address 127.0.0.1" +
                     L" --bind-port " + std::to_wstring(mediaPort) +
                     L" --control-port " + std::to_wstring(controlPort) +
                     L" --directory-url " + url +
                     L" --directory-id tester --directory-pw e2e-pass-1234" +
                     L" --seconds 90" + isolation_args(host->dir);

  // Checked BEFORE anything starts. A host that talks to a directory writes a token cache, and
  // without --directory-cache that is the user's own %LOCALAPPDATA%\remote60\host.json --
  // which this test overwrote on 2026-09-23. It refuses rather than launches.
  std::string why;
  const bool cmdIsolated = remote60::native_poc::e2e::e2e_command_is_isolated(cmd, host->dir, &why);
  check("the host command line keeps the directory cache inside the staging directory",
        cmdIsolated, why);
  const std::wstring appData = host->dir + L"localappdata";
  CreateDirectoryW(appData.c_str(), nullptr);
  std::vector<wchar_t> env = remote60::native_poc::e2e::e2e_isolated_environment(appData);
  const bool envIsolated = remote60::native_poc::e2e::e2e_path_is_under(
      remote60::native_poc::e2e::e2e_block_localappdata(env), host->dir);
  check("the host gets a LOCALAPPDATA inside the staging directory", envIsolated,
        "the diagnostic mirror writes under whatever LOCALAPPDATA it is handed");
  if (!cmdIsolated || !envIsolated) return false;  // never start a host that could reach the user's files
  host->cachePath = host->dir + L"host_cache.json";

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
  if (!launched) return false;
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
  // The host says which cache it will write, before it registers. Confirmed from its own words
  // rather than from the argument this test passed: the question is what the process DID.
  const std::string wantPath(host->cachePath.begin(), host->cachePath.end());
  const bool said = host->WaitFor("directory cache path=", 20000);
  const std::string out = host->log();
  const bool usedIt = said && out.find("directory cache path=" + wantPath + " (--directory-cache)") !=
                                  std::string::npos;
  check("the host reports using the staging cache, not the user's", usedIt,
        said ? "" : "the host never said which cache it used");
  return usedIt;
}

/** A relay that answers late, and -- when asked to -- forwards for the session that follows. */
struct LateRelay {
  SOCKET sock = INVALID_SOCKET;
  uint16_t port = 0;
  std::thread thread;
  std::atomic<bool> running{false};
  std::atomic<int> answered{0};
  std::atomic<bool> forward{false};
  uint16_t hostPort = 0;

  bool Start(uint32_t graceMs, uint16_t forwardTo = 0) {
    hostPort = forwardTo;
    forward = forwardTo != 0;
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    sockaddr_in bound{};
    int boundLen = sizeof(bound);
    if (getsockname(sock, reinterpret_cast<sockaddr*>(&bound), &boundLen) != 0) return false;
    port = ntohs(bound.sin_port);
    DWORD timeout = 100;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));
    running = true;
    thread = std::thread([this, graceMs] {
      char buf[4096];
      bool scheduled = false;
      std::chrono::steady_clock::time_point answerAt;
      sockaddr_in client{};
      bool haveClient = false;
      sockaddr_in hostAddr{};
      hostAddr.sin_family = AF_INET;
      hostAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      hostAddr.sin_port = htons(hostPort);
      while (running.load()) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        const int n = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from),
                               &fromLen);
        if (n > 0) {
          const bool fromHost = forward.load() && from.sin_port == htons(hostPort);
          if (fromHost && haveClient) {
            sendto(sock, buf, n, 0, reinterpret_cast<const sockaddr*>(&client), sizeof(client));
          } else {
            client = from;
            haveClient = true;
            if (!scheduled) {
              scheduled = true;
              answerAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(graceMs);
            }
            // Once the relay has answered, it carries whatever the client sends onward. Before
            // that it only counts: the grace period is the whole contest.
            if (forward.load() && answered.load() > 0) {
              sendto(sock, buf, n, 0, reinterpret_cast<const sockaddr*>(&hostAddr),
                     sizeof(hostAddr));
            }
          }
        }
        if (scheduled && std::chrono::steady_clock::now() >= answerAt) {
          remote60::native_poc::UdpHelloPacket reply{};
          reply.kind = static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::Punch);
          sendto(sock, reinterpret_cast<const char*>(&reply), sizeof(reply), 0,
                 reinterpret_cast<const sockaddr*>(&client), sizeof(client));
          ++answered;
          scheduled = false;
        }
      }
    });
    return true;
  }
  void Stop() {
    running = false;
    if (sock != INVALID_SOCKET) {
      closesocket(sock);
      sock = INVALID_SOCKET;
    }
    if (thread.joinable()) thread.join();
  }
  ~LateRelay() { Stop(); }
};

std::string capabilityBody(const std::string& token, const char* ip, uint16_t port) {
  return "{\"ok\":true,\"pendingPunch\":[{\"ip\":\"" + std::string(ip) + "\",\"port\":" +
         std::to_string(port) + ",\"punchToken\":\"" + token + "\"}]}";
}

void ScriptDirectory(FakeDirectory& dir, const std::string& hostId, const std::string& heartbeat) {
  dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                         std::to_string(dir.udpPort()) + "}}"}});
  dir.Script("/api/host/register",
             {Reply{200, "{\"ok\":true,\"hostId\":\"" + hostId + "\",\"hostToken\":\"" +
                             std::string(32, 'e') + "\"}"}});
  dir.Script("/api/host/heartbeat", {Reply{200, heartbeat}});
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
    std::printf("SKIP  host_punch_reply_e2e_test (starts a listening host)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n");
    std::printf("\nRESULT: SKIPPED\n");
    return remote60::native_poc::e2e::kE2eSkippedExit;
  }

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("FAIL  winsock did not start\n");
    return 1;
  }

  const std::string token(32, 'c');
  const char* kIssuedIp = "211.218.222.1";  // the PUBLIC tuple the capability is issued against
  const uint16_t kIssuedPort = 60420;

  // Every case below spawns its own host. A capability is single-use and the reply window is
  // real time, so sharing one host between cases would make each case depend on the last.

  // ============================================================ armed: the session actually opens
  {
    FakeDirectory dir;
    check("the fake directory starts", dir.Start());
    ScriptDirectory(dir, "h-e2e", capabilityBody(token, kIssuedIp, kIssuedPort));

    SpawnedHost host;
    const uint16_t mediaPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();  // picked at run time (RV-19)
    SetEnvironmentVariableW(L"REMOTE60_DIRECTORY_HEARTBEAT_SEC", L"120");
    check("a real GNLinkStream starts against it",
          StartHost(&host, dir.url(), mediaPort, remote60::native_poc::e2e::e2e_pick_free_tcp_port(), "armed"));
    // The outbound punch is the host saying it collected the capability -- which is also what
    // opened its reply window. Following the host's own line beats sleeping.
    check("...and collects the capability from the heartbeat",
          host.WaitFor("directory punch ->", 30000), host.tail());

    LateRelay relay;
    check("a relay that answers on the 2500ms grace starts", relay.Start(2500));

    remote60::native_poc::DirectoryRendezvous rv;
    std::string observed, error;
    check("the client observes itself through the directory",
          rv.Observe("127.0.0.1", dir.udpPort(), "e2e-token", &observed, &error), error);

    std::vector<remote60::native_poc::RendezvousCandidate> candidates;
    candidates.push_back({"127.0.0.1", mediaPort, "private"});
    candidates.push_back({"127.0.0.1", relay.port, "relay"});

    remote60::native_poc::RendezvousCandidate chosen;
    const auto began = std::chrono::steady_clock::now();
    const bool picked = rv.PunchAny(candidates, 4000, &chosen, &error);
    const auto punchMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - began).count();
    check("the client picks a candidate", picked, error);
    check("...and it is the private one", chosen.kind == "private",
          chosen.kind + " at " + std::to_string(punchMs) + "ms");
    note("punch race: " + std::to_string(punchMs) + "ms (the relay answers at 2500ms)");

    // THE point of this file. The socket that won the race is handed to the product session,
    // which connects it and runs the product hello -- carrying a capability the directory issued
    // against the host's PUBLIC tuple, to a PRIVATE address.
    NullSink sink;
    remote60::native_poc::ClientSessionController client;
    remote60::native_poc::ClientSessionConnectArgs args;
    args.host = chosen.ip;
    args.videoPort = chosen.port;
    args.controlPort = chosen.port;  // tunnelled over the media socket; the session rejects 0
    args.requireUdpHello = true;
    args.requireTcpControl = false;
    args.controlOverUdp = true;
    args.controlIntervalMs = 200;
    args.peerAuthToken = token;
    args.udpHandshakeTimeoutMs = 4000;
    args.preparedUdpSocket = rv.Release();
    args.encodedFrameSink = &sink;
    check("the punched socket is handed to the product session", client.Connect(args));
    const bool up = wait_until(
        [&] {
          return client.Snapshot().state == remote60::native_poc::ClientSessionState::Connected;
        }, 20000);
    check("a capability issued for the public tuple opens a session from a private source", up,
          client.Snapshot().status);
    check("...with the control loop running over that same socket",
          client.Snapshot().controlLoopActive);

    // The host says which endpoint it bound the session to. This is what proves the translation
    // happened, rather than inferring it from the handshake having succeeded.
    const std::string expected =
        "capability endpoint translated expected=" + std::string(kIssuedIp) + ":" +
        std::to_string(kIssuedPort);
    check("...and the host reports the endpoint it translated to",
          host.WaitFor(expected.c_str(), 5000), expected);
    check("...and reports a client connected", host.WaitFor("client connected", 5000));

    // One control round trip, end to end. A desktop selection is the request with an
    // unambiguous answer: the host replies and the status carries it.
    const bool roundTrip =
        client.RequestDesktopMode() &&
        wait_until([&] {
          return client.Snapshot().status.find("window_selected") != std::string::npos;
        }, 8000);
    check("a control request makes the round trip over the tunnel", roundTrip,
          client.Snapshot().status);

    // PunchAny re-sends while it waits, so the host saw more than one punch from one source.
    // They are all answered, and the per-source budget is 25 -- duplicates are bounded.
    const int replied = host.Count("replied=1");
    check("duplicate punches from one source are all answered, within budget",
          replied >= 1 && replied <= 25, std::to_string(replied) + " replies");

    // The relay answers at 2500ms whatever happens. A late answer must not disturb a session
    // that is already up.
    const auto before = client.Snapshot();
    std::this_thread::sleep_for(std::chrono::milliseconds(2800));
    const auto after = client.Snapshot();
    check("the relay's late answer does not disturb the established session",
          after.state == remote60::native_poc::ClientSessionState::Connected &&
              after.host == before.host && after.videoPort == before.videoPort,
          "relay answered " + std::to_string(relay.answered.load()) + " time(s), endpoint " +
              after.host + ":" + std::to_string(after.videoPort));

    client.Disconnect();
    host.Stop();
    relay.Stop();
  }

  // ================================================================== a token nobody issued
  {
    FakeDirectory dir;
    check("the fake directory starts (bad token)", dir.Start());
    ScriptDirectory(dir, "h-e2e-bad", capabilityBody(token, kIssuedIp, kIssuedPort));

    SpawnedHost host;
    const uint16_t mediaPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    SetEnvironmentVariableW(L"REMOTE60_DIRECTORY_HEARTBEAT_SEC", L"120");
    check("a real GNLinkStream starts (bad token)",
          StartHost(&host, dir.url(), mediaPort, remote60::native_poc::e2e::e2e_pick_free_tcp_port(), "badtoken"));
    check("...and collects the capability", host.WaitFor("directory punch ->", 30000),
          host.tail());

    remote60::native_poc::DirectoryRendezvous rv;
    std::string observed, error;
    check("the client observes itself (bad token)",
          rv.Observe("127.0.0.1", dir.udpPort(), "e2e-token-bad", &observed, &error), error);
    std::vector<remote60::native_poc::RendezvousCandidate> candidates;
    candidates.push_back({"127.0.0.1", mediaPort, "private"});
    remote60::native_poc::RendezvousCandidate chosen;
    check("the host answers its punch all the same",
          rv.PunchAny(candidates, 4000, &chosen, &error), error);

    // Answering a punch is not authorizing anyone, and this is the check that says so.
    NullSink sink;
    remote60::native_poc::ClientSessionController client;
    remote60::native_poc::ClientSessionConnectArgs args;
    args.host = chosen.ip;
    args.videoPort = chosen.port;
    args.controlPort = chosen.port;
    args.requireUdpHello = true;
    args.requireTcpControl = false;
    args.controlOverUdp = true;
    args.peerAuthToken = std::string(32, 'z');  // never issued
    args.udpHandshakeTimeoutMs = 2500;
    args.preparedUdpSocket = rv.Release();
    args.encodedFrameSink = &sink;
    client.Connect(args);
    const bool up = wait_until(
        [&] {
          return client.Snapshot().state == remote60::native_poc::ClientSessionState::Connected;
        }, 6000);
    check("a capability nobody issued does not open a session", !up, client.Snapshot().status);
    check("...and the host does not report a client connected",
          host.log().find("client connected") == std::string::npos);
    client.Disconnect();
    host.Stop();
  }

  // ============================ the window closes on its own, and the relay path still works
  {
    // Two things at once, and they are the same thing: the reply window is ten seconds of real
    // time, and once it has passed the host goes back to the state the field was in -- silent,
    // relay wins. What must NOT change is authorization: the same capability, presented from the
    // relay's address instead of the client's, still opens the session.
    FakeDirectory dir;
    check("the fake directory starts (window expiry)", dir.Start());
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                           std::to_string(dir.udpPort()) + "}}"}});
    dir.Script("/api/host/register",
               {Reply{200, "{\"ok\":true,\"hostId\":\"h-e2e-expiry\",\"hostToken\":\"" +
                               std::string(32, 'e') + "\"}"}});
    // The capability ONCE, then nothing -- which is what a real directory does, because it
    // dequeues a pending punch when it hands it over. It matters here: a punch arriving makes
    // the host refresh, and a directory that re-served the same capability would re-open the
    // window a hundred and fifty milliseconds later. The first run of this case found exactly
    // that, and the host was right -- the fake was the thing modelling a directory that never
    // forgets.
    dir.Script("/api/host/heartbeat", {Reply{200, capabilityBody(token, kIssuedIp, kIssuedPort)},
                                       Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"}});

    SpawnedHost host;
    const uint16_t mediaPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    // Far enough out that no second heartbeat can re-open the window mid-case.
    SetEnvironmentVariableW(L"REMOTE60_DIRECTORY_HEARTBEAT_SEC", L"120");
    check("a real GNLinkStream starts (window expiry)",
          StartHost(&host, dir.url(), mediaPort, remote60::native_poc::e2e::e2e_pick_free_tcp_port(), "expiry"));
    check("...and collects the capability, opening the window",
          host.WaitFor("directory punch ->", 30000), host.tail());

    note("waiting out the 10s reply window");
    std::this_thread::sleep_for(std::chrono::milliseconds(11500));

    LateRelay relay;
    check("a forwarding relay starts", relay.Start(2500, mediaPort));

    remote60::native_poc::DirectoryRendezvous rv;
    std::string observed, error;
    check("the client observes itself (window expiry)",
          rv.Observe("127.0.0.1", dir.udpPort(), "e2e-token-exp", &observed, &error), error);
    std::vector<remote60::native_poc::RendezvousCandidate> candidates;
    candidates.push_back({"127.0.0.1", mediaPort, "private"});
    candidates.push_back({"127.0.0.1", relay.port, "relay"});

    remote60::native_poc::RendezvousCandidate chosen;
    const auto began = std::chrono::steady_clock::now();
    const bool picked = rv.PunchAny(candidates, 6000, &chosen, &error);
    const auto punchMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - began).count();
    check("once the window has closed the host is silent again and the relay wins",
          picked && chosen.kind == "relay",
          chosen.kind + " at " + std::to_string(punchMs) + "ms");
    check("...and it takes the relay's grace period to get there", punchMs >= 2400,
          std::to_string(punchMs) + "ms");
    check("...and the host logged the refusal as a closed window", host.Count("reason=closed") > 0,
          std::to_string(host.Count("reason=closed")) + " closed-window refusals");
    // Printed either way: when the window misbehaves, the host's own decisions are the
    // only thing that says which of the two openers fired.
    note("window: " + host.Lines("[punch-reply]"));
    note("punches: " + host.Lines("[dir-punch]"));

    NullSink sink;
    remote60::native_poc::ClientSessionController client;
    remote60::native_poc::ClientSessionConnectArgs args;
    args.host = chosen.ip;
    args.videoPort = chosen.port;
    args.controlPort = chosen.port;
    args.requireUdpHello = true;
    args.requireTcpControl = false;
    args.controlOverUdp = true;
    args.controlIntervalMs = 200;
    args.peerAuthToken = token;
    args.udpHandshakeTimeoutMs = 6000;
    args.preparedUdpSocket = rv.Release();
    args.encodedFrameSink = &sink;
    check("the relay-chosen socket is handed to the product session", client.Connect(args));
    const bool up = wait_until(
        [&] {
          return client.Snapshot().state == remote60::native_poc::ClientSessionState::Connected;
        }, 20000);
    check("the same capability opens the session through the relay too", up,
          client.Snapshot().status);
    const std::string expected =
        "capability endpoint translated expected=" + std::string(kIssuedIp) + ":" +
        std::to_string(kIssuedPort);
    check("...and the host translated to the relay's address, not the client's",
          host.WaitFor(expected.c_str(), 5000) &&
              host.log().find("actual=127.0.0.1:" + std::to_string(relay.port)) !=
                  std::string::npos,
          "relay port " + std::to_string(relay.port));
    client.Disconnect();
    host.Stop();
    relay.Stop();
  }

  // ==================================== a capability that arrives after the client has asked
  {
    // The client is first. Its hello is refused until the host's next heartbeat hands it the
    // capability, and then the retry -- still inside the token's life -- succeeds. Nothing here
    // touches the punch: this is the handshake's own retry budget.
    FakeDirectory dir;
    check("the fake directory starts (late capability)", dir.Start());
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                           std::to_string(dir.udpPort()) + "}}"}});
    dir.Script("/api/host/register",
               {Reply{200, "{\"ok\":true,\"hostId\":\"h-e2e-late\",\"hostToken\":\"" +
                               std::string(32, 'e') + "\"}"}});
    // First heartbeat: nothing. Every one after that: the capability. The last scripted reply
    // repeats, which is what makes "from here on" expressible.
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"},
                                       Reply{200, capabilityBody(token, kIssuedIp, kIssuedPort)}});

    SpawnedHost host;
    const uint16_t mediaPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    SetEnvironmentVariableW(L"REMOTE60_DIRECTORY_HEARTBEAT_SEC", L"5");
    check("a real GNLinkStream starts (late capability)",
          StartHost(&host, dir.url(), mediaPort, remote60::native_poc::e2e::e2e_pick_free_tcp_port(), "late"));
    check("...and reaches its directory with nothing pending",
          host.WaitFor("directory agent started", 30000), host.tail());
    check("...and has no capability yet",
          host.log().find("directory punch ->") == std::string::npos);

    NullSink sink;
    remote60::native_poc::ClientSessionController client;
    remote60::native_poc::ClientSessionConnectArgs args;
    args.host = "127.0.0.1";
    args.videoPort = mediaPort;
    args.controlPort = mediaPort;
    args.requireUdpHello = true;
    args.requireTcpControl = false;
    args.controlOverUdp = true;
    args.controlIntervalMs = 200;
    args.peerAuthToken = token;
    // Long enough to outlast a 5s heartbeat gap. The hello re-sends inside this budget.
    args.udpHandshakeTimeoutMs = 20000;
    args.encodedFrameSink = &sink;
    const auto began = std::chrono::steady_clock::now();
    check("the client asks before the host can answer", client.Connect(args));
    const bool up = wait_until(
        [&] {
          return client.Snapshot().state == remote60::native_poc::ClientSessionState::Connected;
        }, 30000);
    const auto waitedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - began).count();
    check("a capability that arrives late still opens the session", up,
          client.Snapshot().status + " after " + std::to_string(waitedMs) + "ms");
    check("...and the host did collect it in the meantime",
          host.log().find("directory punch ->") != std::string::npos);
    note("hello retried for " + std::to_string(waitedMs) + "ms before the capability landed");
    client.Disconnect();
    host.Stop();
  }

  // Punch LOSS is not covered in this round. Not "cannot be covered" -- the earlier note said
  // loopback leaves nothing to drop a datagram with, which reads as a property of the problem
  // when it is a property of this fixture. A drop can be injected at the transport boundary,
  // and C2 is where that is being built. Until then this file exercises duplicates and
  // re-sends, and says so rather than letting the gap look closed.

  std::printf("\n%s  (%d checks, %d failed)\n",
              gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks, gFailures);
  WSACleanup();
  return gFailures == 0 ? 0 : 1;
}

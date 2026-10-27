#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// keepalive-r2 A (spike/host-admission): control-only STANDBY entries at a real host.
//
// A spawned GNLinkStream (the freshly built one; --old-host <exe> adds an older host without standby) and
// raw-socket clients speaking UdpStandbyPacket. What is shown: several authenticated clients keep a place at
// the host WITHOUT being its session; the session belongs to one of them only through an explicit Activate,
// which goes through the same admission an authenticated Hello does; a parked client's pings, re-admissions,
// control datagrams and late activations do not touch the active one; bounds and lease hold; through a relay
// stand-in (a host-facing socket per client lease -- Codex decision 1 (a)) entries are told apart, and a parked
// client's session packets (r3 S4) never reach the active session; the grant ends with its session (r3 S3); an older
// host does not answer StandbyHello and keeps its session. The harness (spawn, directory script, raw control
// primitives) is host_punch_reply_e2e_test.cpp's.
//
// Off unless REMOTE60_ALLOW_HOST_E2E=1 (it starts a listening host on loopback). Tags: network(loopback).

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>

#include "e2e_isolation.hpp"
#include "directory_fake_server.hpp"
#include "directory_rendezvous.hpp"
#include "native_video_client_tcp_control.hpp"
#include "native_video_client_session.hpp"
#include "poc_protocol.hpp"
#include "host_standby_table.hpp"
#include "standby_lease.hpp"
#include "standby_runner.hpp"
#include "directory_session_client.hpp"

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
  remote60::native_poc::e2e::StagingDir staging;  // the directory above, inside the repository
  bool stopClean = false;   // Stop(): the host exited and its staging directory is gone
  std::string stopWhy;      // ...or why not

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
    // The host's whole log, appended to REMOTE60_E2E_DUMP_HOST_LOG when set (the evidence of a failing case).
    {
      char dump[MAX_PATH] = {};
      if (GetEnvironmentVariableA("REMOTE60_E2E_DUMP_HOST_LOG", dump, MAX_PATH) > 0) {
        if (FILE* f = std::fopen(dump, "ab")) {
          const std::string all = log();
          std::fprintf(f, "\n===== host log (%zu bytes) =====\n", all.size());
          std::fwrite(all.data(), 1, all.size(), f);
          std::fclose(f);
        }
      }
    }
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
    bool exited = true;
    DWORD wait = WAIT_OBJECT_0;
    if (pi.hProcess) {
      // The kill is asynchronous: until the host has actually exited its image file is in use
      // and GNLinkStream.exe cannot be deleted -- which is how every case used to leave its
      // staging directory behind (132 of them on one machine). Only WAIT_OBJECT_0 says it has.
      wait = WaitForSingleObject(pi.hProcess, 20000);
      exited = (wait == WAIT_OBJECT_0);
      CloseHandle(pi.hProcess);
    }
    pi = PROCESS_INFORMATION{};
    if (!dir.empty()) {
      if (!exited) {
        // Kept, not removed under a process that may still be using it; the case reports it.
        stopWhy = "the host did not exit within 20 s (wait=" + std::to_string(wait) + "); staging kept";
        staging.set_keep(true, stopWhy);
        stopClean = false;
      } else {
        stopClean = staging.Remove();
        stopWhy = stopClean ? std::string() : staging.why();
      }
      dir.clear();
    } else if (!staging.created()) {
      stopClean = true;  // never staged, nothing to remove
      stopWhy = "nothing was staged";
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
// The host's lifetime: 90 s (the spike cases), or REMOTE60_E2E_HOST_SECONDS -- the product-client run is longer (two
// leases, a takeover, a re-activation and the runner on ONE host) and set it (a host that exits mid-run fails it).
std::wstring e2e_host_seconds() {
  wchar_t v[16] = {};
  if (GetEnvironmentVariableW(L"REMOTE60_E2E_HOST_SECONDS", v, 16) > 0 && _wtoi(v) > 0) return v;
  return L"90";
}

std::wstring isolation_args(const std::wstring& stagingDir) {
  return L" --directory-cache \"" + stagingDir + L"host_cache.json\"";
}

bool StartHost(SpawnedHost* host, const std::string& directoryUrl, uint16_t mediaPort,
               uint16_t controlPort, const char* tag, bool prelogin = false) {
  if (mediaPort == 0 || controlPort == 0) {
    // A port the picker could not provide must not reach the host: --bind-port 0 reads as "no
    // candidates" and the host would fall back to the product's default port. Nothing is staged,
    // nothing is started; the case fails at its "starts" check.
    std::printf("SKIP-LAUNCH %s: no free port (media udp %u, control tcp %u), the host is not started\n", tag,
                mediaPort, controlPort);
    return false;
  }
  // Inside the repository's test scratch root, never %TEMP% (RV-20 r2): a directory this
  // process created, one per case, removed once the host has exited.
  if (!host->staging.Create(L"punch_" + std::wstring(tag, tag + strlen(tag)))) {
    std::printf("FAIL  %s\n", host->staging.why().c_str());
    return false;
  }
  host->dir = host->staging.path();

  const std::wstring myDir = directory_of(self_path());
  // GNLinkCapture is staged as a copy of this binary in --thumbnail mode: the host starts one and
  // it must not answer, exactly as the sibling e2e does.
  // udp-control-peer r2 negative control: REMOTE60_E2E_OLD_BIN points at a GNLinkStream.exe built
  // WITHOUT the peer gate (host_startup_control.cpp at b6f4b18). The injection case then dispatches B's
  // control and fails -- proving the gate is load-bearing. Unset -> the freshly built (fixed) host.
  std::wstring streamSrc = myDir + L"GNLinkStream.exe";
  {
    wchar_t oldBin[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"REMOTE60_E2E_OLD_BIN", oldBin, MAX_PATH) != 0 && oldBin[0] != L'\0') {
      streamSrc = oldBin;
    }
  }
  if (!CopyFileW(streamSrc.c_str(), (host->dir + L"GNLinkStream.exe").c_str(), FALSE) ||
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
  // r6 G1: in prelogin mode the role flag is the ONLY transport/codec input -- `--prelogin` must itself
  // pin udp+h264. r6 V5 (updated r9 W4): the directory arguments given here are NOT a way in -- a
  // --prelogin worker ignores them and takes its credential from the machine record only, so this run is
  // the negative: the shipped binary, handed a directory to register with, never registers. (The
  // positive -- the worker registering from a sealed machine record -- is viewer_prelogin_login_e2e's,
  // on the test build.)
  const std::wstring roleArgs = prelogin ? L" --prelogin" : L" --transport udp --codec h264";
  std::wstring cmd = L"\"" + host->dir + L"GNLinkStream.exe\"" + roleArgs +
                     L" --bind-address 127.0.0.1" +
                     L" --bind-port " + std::to_wstring(mediaPort) +
                     L" --control-port " + std::to_wstring(controlPort) +
                     L" --directory-url " + url +
                     L" --directory-id tester --directory-pw e2e-pass-1234" +
                     L" --seconds " + e2e_host_seconds() + isolation_args(host->dir);

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
  if (prelogin) {
    // A --prelogin worker reads the machine record under %ProgramData%: this one gets an empty folder of
    // its own, so a record on this PC (if there ever is one) can never be read or registered with.
    const std::wstring programData = host->dir + L"programdata";
    CreateDirectoryW(programData.c_str(), nullptr);
    const std::wstring kv = L"ProgramData=" + programData;
    std::vector<wchar_t> rebuilt;
    for (const wchar_t* p = env.data(); *p; p += wcslen(p) + 1) {
      if (_wcsnicmp(p, L"ProgramData=", 12) == 0) continue;
      rebuilt.insert(rebuilt.end(), p, p + wcslen(p) + 1);
    }
    rebuilt.insert(rebuilt.end(), kv.begin(), kv.end());
    rebuilt.push_back(L'\0');
    rebuilt.push_back(L'\0');
    env.swap(rebuilt);
  }
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
  // r6 V5: a --prelogin worker given directory arguments ignores them and never starts its directory
  // agent, so it never names a cache -- it has nothing to write. Its isolation is the empty ProgramData
  // above; the case checks the offline result itself.
  if (prelogin) return host->WaitFor("[native-video-host][prelogin]", 20000);
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

// --- raw-socket viewer primitives (udp-control-peer r3): bypass ClientSessionController so the test
// controls the source port/address directly (A->A' move, foreign endpoints). -----------------------
void raw_send_hello(SOCKET s, const sockaddr_in& host, const std::string& token) {
  remote60::native_poc::UdpHelloPacket h{};  // magic/kind/size/version/features default (FEC set)
  const size_t n = token.size() < sizeof(h.authToken) - 1 ? token.size() : sizeof(h.authToken) - 1;
  std::memcpy(h.authToken, token.data(), n);
  sendto(s, reinterpret_cast<const char*>(&h), sizeof(h), 0, reinterpret_cast<const sockaddr*>(&host),
         sizeof(host));
}

void raw_send_window_select(SOCKET s, const sockaddr_in& host, uint32_t messageSeq, uint64_t windowId) {
  remote60::native_poc::ControlWindowSelectMessage msg{};
  msg.header.magic = remote60::native_poc::kMagic;
  msg.header.type = static_cast<uint16_t>(remote60::native_poc::MessageType::ControlWindowSelect);
  msg.header.size = static_cast<uint16_t>(sizeof(msg));
  msg.seq = 7;
  msg.windowId = windowId;
  remote60::native_poc::UdpControlChunkHeader head{};
  head.magic = remote60::native_poc::kMagic;
  head.kind = static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::ControlData);
  head.size = static_cast<uint16_t>(sizeof(head));
  head.streamId = remote60::native_poc::kUdpControlStreamClientToHost;  // the host's rx stream
  head.messageSeq = messageSeq;
  head.totalSize = sizeof(msg);
  head.fragIndex = 0;
  head.fragCount = 1;
  head.fragOffset = 0;
  head.fragSize = sizeof(msg);
  uint8_t dg[sizeof(head) + sizeof(msg)];
  std::memcpy(dg, &head, sizeof(head));
  std::memcpy(dg + sizeof(head), &msg, sizeof(msg));
  sendto(s, reinterpret_cast<const char*>(dg), sizeof(dg), 0, reinterpret_cast<const sockaddr*>(&host),
         sizeof(host));
}

// Reads host->client datagrams until one is a ControlData on `streamId` (matching `wantSeq`, or any seq
// when wantSeq==0) within timeoutMs. Returns true on match and reports the seq in *outSeq.
bool raw_wait_ctrl_data(SOCKET s, uint32_t streamId, uint32_t wantSeq, int timeoutMs, uint32_t* outSeq) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    uint8_t buf[2048];
    sockaddr_in from{};
    int fl = sizeof(from);
    const int n = recvfrom(s, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                           reinterpret_cast<sockaddr*>(&from), &fl);
    if (n < static_cast<int>(sizeof(remote60::native_poc::UdpControlChunkHeader))) continue;
    remote60::native_poc::UdpControlChunkHeader h{};
    std::memcpy(&h, buf, sizeof(h));
    if (h.magic != remote60::native_poc::kMagic) continue;
    if (h.kind != static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::ControlData)) continue;
    if (h.streamId != streamId) continue;
    if (wantSeq != 0 && h.messageSeq != wantSeq) continue;
    if (outSeq) *outSeq = h.messageSeq;
    return true;
  }
  return false;
}

void raw_send_ctrl_ack(SOCKET s, const sockaddr_in& host, uint32_t streamId, uint32_t seq) {
  remote60::native_poc::UdpControlAckPacket a{};
  a.streamId = streamId;
  a.messageSeq = seq;
  a.missingCount = 0;
  sendto(s, reinterpret_cast<const char*>(&a), sizeof(a), 0, reinterpret_cast<const sockaddr*>(&host),
         sizeof(host));
}

// r3 S4: the rest of a session's packets, raw -- what a parked client behind a relay could send.
void raw_send_ctrl_nack(SOCKET s, const sockaddr_in& host, uint32_t streamId, uint32_t seq) {
  remote60::native_poc::UdpControlAckPacket a{};
  a.kind = static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::ControlNack);
  a.streamId = streamId;
  a.messageSeq = seq;
  a.missingCount = 1;
  a.missing[0] = 0;
  sendto(s, reinterpret_cast<const char*>(&a), sizeof(a), 0, reinterpret_cast<const sockaddr*>(&host), sizeof(host));
}
void raw_send_video_nack(SOCKET s, const sockaddr_in& host, uint32_t seq) {
  remote60::native_poc::UdpVideoNackPacket n{};
  n.seq = seq;
  n.missingCount = 1;
  n.missing[0] = 0;
  sendto(s, reinterpret_cast<const char*>(&n), sizeof(n), 0, reinterpret_cast<const sockaddr*>(&host), sizeof(host));
}
void raw_send_resume(SOCKET s, const sockaddr_in& host, uint32_t resumeId) {
  remote60::native_poc::UdpControlResumePacket r{};
  r.streamId = remote60::native_poc::kUdpControlStreamClientToHost;
  r.resumeId = resumeId;
  sendto(s, reinterpret_cast<const char*>(&r), sizeof(r), 0, reinterpret_cast<const sockaddr*>(&host), sizeof(host));
}
// A bulk-channel-shaped datagram (control chunk header, a bulk stream id): the host's bulk receivers sit behind
// the same adopted-peer gate as control.
void raw_send_bulk(SOCKET s, const sockaddr_in& host, uint32_t messageSeq) {
  remote60::native_poc::UdpControlChunkHeader head{};
  head.magic = remote60::native_poc::kMagic;
  head.kind = static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::ControlData);
  head.size = static_cast<uint16_t>(sizeof(head));
  head.streamId = 0x42554C4Bu;  // not a control stream
  head.messageSeq = messageSeq;
  head.totalSize = 16;
  head.fragCount = 1;
  head.fragSize = 16;
  uint8_t dg[sizeof(head) + 16] = {};
  std::memcpy(dg, &head, sizeof(head));
  sendto(s, reinterpret_cast<const char*>(dg), sizeof(dg), 0, reinterpret_cast<const sockaddr*>(&host), sizeof(host));
}
// Drains `s` for `ms` and counts what the host sent it that is NOT a Standby* reply.
int count_non_standby_rx(SOCKET s, int ms) {
  int n = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
    uint8_t buf[2048];
    const int m = recv(s, reinterpret_cast<char*>(buf), sizeof(buf), 0);
    if (m < 6) continue;
    uint16_t k = 0;
    std::memcpy(&k, buf + 4, 2);
    if (k < static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::StandbyHello) || k > static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::StandbyRelease)) ++n;
  }
  return n;
}

// A loopback UDP socket bound to `bindIp` (an ephemeral port), with a short receive timeout.
SOCKET raw_open(const char* bindIp) {
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) return s;
  sockaddr_in local{};
  local.sin_family = AF_INET;
  inet_pton(AF_INET, bindIp, &local.sin_addr);
  local.sin_port = 0;
  if (bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
    closesocket(s);
    return INVALID_SOCKET;
  }
  DWORD rcvTo = 1000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTo), sizeof(rcvTo));
  return s;
}


// ----------------------------------------------------------------- standby client primitives
std::string capabilitiesBody(const std::vector<std::string>& tokens, const char* ip, uint16_t port) {
  std::string out = "{\"ok\":true,\"pendingPunch\":[";
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (i) out += ",";
    out += "{\"ip\":\"" + std::string(ip) + "\",\"port\":" + std::to_string(port) + ",\"punchToken\":\"" + tokens[i] +
           "\"}";
  }
  return out + "]}";
}

using remote60::native_poc::UdpPacketKind;
using remote60::native_poc::UdpStandbyPacket;

struct Entry {
  uint64_t id = 0;
  uint8_t secret[16] = {};
  bool ok = false;
  uint32_t status = 99;
};

void sb_send(SOCKET s, const sockaddr_in& to, UdpPacketKind kind, const uint8_t device, uint32_t gen,
             const std::string& token, const Entry* e) {
  UdpStandbyPacket p{};
  p.kind = static_cast<uint16_t>(kind);
  p.size = static_cast<uint16_t>(sizeof(p));
  for (uint8_t& b : p.deviceId) b = device;
  p.clientGen = gen;
  p.features = remote60::native_poc::kUdpFeatureVideoFec | remote60::native_poc::kUdpFeatureVideoFecInterleaved;
  if (!token.empty()) std::memcpy(p.authToken, token.data(), (std::min)(token.size(), sizeof(p.authToken) - 1));
  if (e) {
    p.entryId = e->id;
    std::memcpy(p.entrySecret, e->secret, 16);
  }
  sendto(s, reinterpret_cast<const char*>(&p), sizeof(p), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
}

// Reads until a datagram of `kind` (a Standby packet, or a HelloAck when kind == HelloAck) or the time runs out.
bool sb_wait(SOCKET s, UdpPacketKind kind, int timeoutMs, UdpStandbyPacket* out, remote60::native_poc::UdpHelloPacket* ack) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (std::chrono::steady_clock::now() < deadline) {
    uint8_t buf[2048];
    sockaddr_in from{};
    int fl = sizeof(from);
    const int n = recvfrom(s, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
    if (n < 8) continue;
    uint32_t magic = 0;
    uint16_t k = 0;
    std::memcpy(&magic, buf, 4);
    std::memcpy(&k, buf + 4, 2);
    if (magic != remote60::native_poc::kMagic || k != static_cast<uint16_t>(kind)) continue;
    if (kind == UdpPacketKind::HelloAck) {
      if (n < static_cast<int>(sizeof(remote60::native_poc::UdpHelloPacket))) continue;
      if (ack) std::memcpy(ack, buf, sizeof(*ack));
      return true;
    }
    if (n != static_cast<int>(sizeof(UdpStandbyPacket))) continue;
    if (out) std::memcpy(out, buf, sizeof(*out));
    return true;
  }
  return false;
}

// StandbyHello until an answer (the capability lands on a heartbeat), or `tries` run out.
// The first capability reaches the host with a heartbeat (observed: up to ~50 s after registering), so the
// default keeps trying for a minute; every later token arrives in the same heartbeat.
Entry admit(SOCKET s, const sockaddr_in& host, uint8_t device, uint32_t gen, const std::string& token, int tries = 150) {
  Entry e;
  for (int i = 0; i < tries; ++i) {
    sb_send(s, host, UdpPacketKind::StandbyHello, device, gen, token, nullptr);
    UdpStandbyPacket r{};
    if (sb_wait(s, UdpPacketKind::StandbyAck, 400, &r, nullptr)) {
      e.status = r.status;
      e.ok = r.status == remote60::native_poc::kUdpStandbyStatusOk;
      e.id = r.entryId;
      std::memcpy(e.secret, r.entrySecret, 16);
      return e;
    }
  }
  return e;
}

uint32_t ping(SOCKET s, const sockaddr_in& host, const Entry& e) {
  sb_send(s, host, UdpPacketKind::StandbyPing, 0, 0, std::string(), &e);
  UdpStandbyPacket r{};
  return sb_wait(s, UdpPacketKind::StandbyPong, 800, &r, nullptr) ? r.status : 99;
}

// StandbyActivate until its result; or a refusal -- reported in *refusal. r3 (Codex decision 2): the result names
// the operation -- a StandbyPong of THIS entry and THIS generation (granted, or refused with a status) -- and a
// granted one is followed by the HelloAck. A Pong of another generation (a late answer to an earlier
// activation) is not this one's result and is skipped.
bool activate(SOCKET s, const sockaddr_in& host, const Entry& e, uint32_t gen, uint32_t* refusal, int tries = 10) {
  for (int i = 0; i < tries; ++i) {
    sb_send(s, host, UdpPacketKind::StandbyActivate, 0, gen, std::string(), &e);
    bool granted = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    while (std::chrono::steady_clock::now() < deadline) {
      uint8_t buf[2048];
      sockaddr_in from{};
      int fl = sizeof(from);
      const int n = recvfrom(s, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
      if (n < 8) continue;
      uint16_t k = 0;
      std::memcpy(&k, buf + 4, 2);
      if (k == static_cast<uint16_t>(UdpPacketKind::HelloAck) && granted) {
        remote60::native_poc::UdpHelloPacket ack{};
        std::memcpy(&ack, buf, sizeof(ack));
        return (ack.features & remote60::native_poc::kUdpFeatureDirectoryAuth) != 0;
      }
      if (k == static_cast<uint16_t>(UdpPacketKind::StandbyPong) && n == static_cast<int>(sizeof(UdpStandbyPacket))) {
        UdpStandbyPacket r{};
        std::memcpy(&r, buf, sizeof(r));
        if (r.entryId != e.id || r.clientGen != gen) continue;  // not this activation's result
        if (r.status == remote60::native_poc::kUdpStandbyStatusOk &&
            (r.features & remote60::native_poc::kUdpStandbyPongActivationGranted) != 0) {
          granted = true;
          continue;
        }
        if (refusal) *refusal = r.status;
        return false;
      }
    }
  }
  return false;
}

size_t count_of(const std::string& text, const std::string& needle) {
  size_t n = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++n;
  return n;
}

// A window-select through the host's control channel; true when the host logged that it processed it.
bool select_seen(SpawnedHost& host, SOCKET s, const sockaddr_in& to, uint32_t seq, uint64_t id, int waitMs = 3000) {
  raw_send_window_select(s, to, seq, id);
  return host.WaitFor(("requestedId=" + std::to_string(id)).c_str(), static_cast<uint32_t>(waitMs));
}

// A relay stand-in for decision (a): each client lease gets its OWN host-facing socket, so the host tells the
// clients apart by address as well as by entry. It routes host->client by the socket a datagram arrived on and
// reads nothing in it (r3: the shared-socket mode, whose replies were routed by the last StandbyActivate SEEN --
// before the host had accepted it -- is gone with option (b), Codex S4).
struct RelayMock {
  SOCKET front = INVALID_SOCKET;  // client-facing
  sockaddr_in frontAddr{};
  sockaddr_in hostAddr{};
  std::atomic<bool> running{false};
  std::thread thread;
  struct Leg {
    sockaddr_in client{};
    SOCKET up = INVALID_SOCKET;
  };
  std::vector<Leg> legs;
  std::mutex mu;

  bool Start(const sockaddr_in& host) {
    hostAddr = host;
    front = raw_open("127.0.0.1");
    if (front == INVALID_SOCKET) return false;
    int len = sizeof(frontAddr);
    getsockname(front, reinterpret_cast<sockaddr*>(&frontAddr), &len);
    DWORD to = 5;
    setsockopt(front, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));
    running = true;
    thread = std::thread([this] { Loop(); });
    return true;
  }
  void Loop() {
    uint8_t buf[2048];
    while (running.load()) {
      sockaddr_in from{};
      int fl = sizeof(from);
      int n = recvfrom(front, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
      if (n > 0) {
        std::lock_guard<std::mutex> lock(mu);
        Leg* leg = nullptr;
        for (Leg& l : legs)
          if (l.client.sin_port == from.sin_port && l.client.sin_addr.s_addr == from.sin_addr.s_addr) leg = &l;
        if (!leg) {
          legs.push_back(Leg{});
          leg = &legs.back();
          leg->client = from;
          leg->up = raw_open("127.0.0.1");
          DWORD to = 5;
          setsockopt(leg->up, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));
        }
        sendto(leg->up, reinterpret_cast<const char*>(buf), n, 0, reinterpret_cast<const sockaddr*>(&hostAddr),
               sizeof(hostAddr));
      }
      // host -> clients: by the leg's own socket, nothing read from the datagram
      std::vector<std::pair<SOCKET, sockaddr_in>> ups;
      {
        std::lock_guard<std::mutex> lock(mu);
        for (Leg& l : legs) ups.push_back({l.up, l.client});
      }
      for (auto& up : ups) {
        for (int i = 0; i < 64; ++i) {
          const int m = recv(up.first, reinterpret_cast<char*>(buf), sizeof(buf), 0);
          if (m <= 0) break;
          sendto(front, reinterpret_cast<const char*>(buf), m, 0, reinterpret_cast<const sockaddr*>(&up.second),
                 sizeof(up.second));
        }
      }
    }
  }
  void Stop() {
    running = false;
    if (thread.joinable()) thread.join();
    for (Leg& l : legs)
      if (l.up != INVALID_SOCKET) closesocket(l.up);
    if (front != INVALID_SOCKET) closesocket(front);
  }
  ~RelayMock() { Stop(); }
};

// ------------------------------------------------------------------ the real directory server (r3, relay (a))
// apps/directory/server.js under node, on loopback ports this test picked, with its data and logs in a staging
// directory of its own. Its stdout is kept for the relay's own lines (reserved / bound / pinned / ready / active).
struct NodeServer {
  PROCESS_INFORMATION pi{};
  HANDLE job = nullptr;
  HANDLE readEnd = nullptr;
  std::thread reader;
  std::mutex mu;
  std::string output;
  remote60::native_poc::e2e::StagingDir staging;
  uint16_t httpPort = 0, udpPort = 0, relayPort = 0;
  std::wstring node, script;

  std::string log() {
    std::lock_guard<std::mutex> lock(mu);
    return output;
  }
  bool WaitFor(const char* marker, uint32_t budgetMs) {
    return wait_until([&] { return log().find(marker) != std::string::npos; }, budgetMs);
  }
  std::vector<wchar_t> Env() const {
    // The test's own environment plus the server's settings; nothing of the server reaches outside staging.
    std::vector<std::pair<std::wstring, std::wstring>> set = {
        {L"REMOTE60_DIR_DATA", staging.path() + L"directory.json"},
        {L"REMOTE60_LOG_DIR", staging.path() + L"logs"},
        {L"REMOTE60_DIR_PORT", std::to_wstring(httpPort)},
        {L"REMOTE60_DIR_UDP_PORT", std::to_wstring(udpPort)},
        {L"REMOTE60_RELAY_ENABLED", L"1"},
        {L"REMOTE60_RELAY_IP", L"127.0.0.1"},
        {L"REMOTE60_RELAY_PORT", std::to_wstring(relayPort)},
        {L"REMOTE60_RELAY_GRACE_MS", L"400"},
        {L"REMOTE60_RELAY_ALLOW_IPS", L"127.0.0.1"},
        {L"REMOTE60_RELAY_ALLOW_ACCOUNTS", L"tester"},
        {L"REMOTE60_RELAY_HOST_FACING_IP", L"127.0.0.1"},
        {L"REMOTE60_RELAY_HOST_FACING_BIND", L"127.0.0.1"},
    };
    std::vector<wchar_t> env;
    if (wchar_t* block = GetEnvironmentStringsW()) {
      for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
        bool overridden = false;
        for (const auto& kv : set) {
          if (_wcsnicmp(p, (kv.first + L"=").c_str(), kv.first.size() + 1) == 0) overridden = true;
        }
        if (!overridden) env.insert(env.end(), p, p + wcslen(p) + 1);
      }
      FreeEnvironmentStringsW(block);
    }
    for (const auto& kv : set) {
      const std::wstring line = kv.first + L"=" + kv.second;
      env.insert(env.end(), line.begin(), line.end());
      env.push_back(L'\0');
    }
    env.push_back(L'\0');
    return env;
  }
  // Runs node with `args`; when `wait`, to its end (the --add-account step), else kept running with its output read.
  bool Run(const std::wstring& args, bool wait) {
    std::wstring cmd = L"\"" + node + L"\" \"" + script + L"\" " + args;
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    std::vector<wchar_t> env = Env();
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 1 << 20)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION p{};
    const bool ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, env.data(),
                                   staging.path().c_str(), &si, &p) != FALSE;
    CloseHandle(wr);
    if (!ok) {
      CloseHandle(rd);
      return false;
    }
    if (!job) {
      job = CreateJobObjectW(nullptr, nullptr);
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
      limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    AssignProcessToJobObject(job, p.hProcess);
    ResumeThread(p.hThread);
    CloseHandle(p.hThread);
    if (wait) {
      const DWORD w = WaitForSingleObject(p.hProcess, 20000);
      char buf[4096];
      DWORD got = 0;
      while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) {
        std::lock_guard<std::mutex> lock(mu);
        output.append(buf, got);
      }
      DWORD code = 1;
      GetExitCodeProcess(p.hProcess, &code);
      CloseHandle(p.hProcess);
      CloseHandle(rd);
      return w == WAIT_OBJECT_0 && code == 0;
    }
    pi = p;
    readEnd = rd;
    reader = std::thread([this] {
      char buf[4096];
      DWORD got = 0;
      while (readEnd && ReadFile(readEnd, buf, sizeof(buf), &got, nullptr) && got > 0) {
        std::lock_guard<std::mutex> lock(mu);
        output.append(buf, got);
      }
    });
    return true;
  }
  bool Start(const std::wstring& nodeExe, const std::wstring& serverJs) {
    node = nodeExe;
    script = serverJs;
    if (!staging.Create(L"realdir")) return false;
    CreateDirectoryW((staging.path() + L"logs").c_str(), nullptr);
    httpPort = remote60::native_poc::e2e::e2e_pick_free_tcp_port();
    udpPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    relayPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    if (!httpPort || !udpPort || !relayPort || udpPort == relayPort) return false;
    if (!Run(L"--add-account tester e2e-pass-1234", true)) return false;
    if (!Run(L"", false)) return false;
    return WaitFor("[relay] listening on udp", 15000);
  }
  void Stop() {
    if (job) {
      CloseHandle(job);  // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
      job = nullptr;
    }
    if (pi.hProcess) {
      WaitForSingleObject(pi.hProcess, 20000);
      CloseHandle(pi.hProcess);
      pi = PROCESS_INFORMATION{};
    }
    if (readEnd) {
      CloseHandle(readEnd);
      readEnd = nullptr;
    }
    if (reader.joinable()) reader.join();
    if (staging.created()) staging.Remove();
  }
  ~NodeServer() { Stop(); }
};

// One HTTP/1.0 request to 127.0.0.1:port (no chunking, the server closes): the status and the body.
int http_call(uint16_t port, const char* method, const std::string& path, const std::string& body,
              const std::string& bearer, std::string* out) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return 0;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = htons(port);
  DWORD to = 5000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));
  if (connect(s, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0) {
    closesocket(s);
    return 0;
  }
  std::string req = std::string(method) + " " + path + " HTTP/1.0\r\nHost: 127.0.0.1\r\n";
  if (!bearer.empty()) req += "Authorization: Bearer " + bearer + "\r\n";
  if (!body.empty()) req += "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
  req += "\r\n" + body;
  send(s, req.data(), static_cast<int>(req.size()), 0);
  std::string resp;
  char buf[4096];
  for (int n; (n = recv(s, buf, sizeof(buf), 0)) > 0;) resp.append(buf, n);
  closesocket(s);
  const size_t sp = resp.find(' ');
  const size_t split = resp.find("\r\n\r\n");
  if (out) *out = split == std::string::npos ? std::string() : resp.substr(split + 4);
  return sp == std::string::npos ? 0 : std::atoi(resp.c_str() + sp + 1);
}

// The string value of "key" in a flat JSON text (enough for these replies), or "".
std::string json_str(const std::string& json, const std::string& key) {
  const std::string k = "\"" + key + "\":\"";
  const size_t at = json.find(k);
  if (at == std::string::npos) return std::string();
  const size_t end = json.find('"', at + k.size());
  return end == std::string::npos ? std::string() : json.substr(at + k.size(), end - at - k.size());
}

// OBSERVE from this socket, so the server knows the client's wire tuple (what /api/connect binds the relay to).
bool observe_from(SOCKET s, uint16_t udpPort, const std::string& token) {
  sockaddr_in d{};
  d.sin_family = AF_INET;
  d.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  d.sin_port = htons(udpPort);
  const std::string msg = "OBSERVE " + token;
  for (int i = 0; i < 10; ++i) {
    sendto(s, msg.data(), static_cast<int>(msg.size()), 0, reinterpret_cast<const sockaddr*>(&d), sizeof(d));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < deadline) {
      char buf[512];
      const int n = recv(s, buf, sizeof(buf) - 1, 0);
      if (n > 0 && buf[0] == '{') return true;
    }
  }
  return false;
}

}  // namespace

// ===================================================================== keepalive-r2 (2) d: the PRODUCT client
struct MemGenStore : remote60::native_poc::StandbyGenerationStore {
  std::array<uint8_t, 16> id{};
  uint32_t last = 0;
  bool have = false;
  bool Load(std::array<uint8_t, 16>* d, uint32_t* l) override {
    if (!have) return false;
    *d = id;
    *l = last;
    return true;
  }
  bool Save(const std::array<uint8_t, 16>& d, uint32_t l) override {
    id = d;
    last = l;
    have = true;
    return true;
  }
};

uint64_t mono_ms() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Pumps the lease until `done` or the time runs out.
bool pump_until(remote60::native_poc::StandbyLease& lease, const std::function<bool()>& done, uint32_t budgetMs) {
  const uint64_t until = mono_ms() + budgetMs;
  while (mono_ms() < until) {
    lease.Pump(100);
    if (done()) return true;
  }
  return done();
}

std::string last_events(const remote60::native_poc::StandbyLease& lease) {
  std::string out;
  for (const auto& e : lease.events()) out += e + "; ";
  return out;
}

// Two product clients (StandbyLease + ClientSessionController -- the code the apps will run) at a real host: both
// parked without a session; A activates (a session through the lease's own socket, the activation's Pong + HelloAck);
// B's Pings leave it alone; B takes over explicitly; A, parked again on a new socket, takes it back with a larger
// generation. An older host: the lease is Unsupported and never sends a Hello.
int run_product_client(const std::vector<std::string>& tokens, const std::wstring& oldHost, bool realDirectory) {
  SetEnvironmentVariableW(L"REMOTE60_E2E_HOST_SECONDS", L"420");
  using remote60::native_poc::StandbyLease;
  using remote60::native_poc::GenerationAllocator;
  const char* kIssuedIp = "211.218.222.1";
  const uint16_t kIssuedPort = 60420;
  std::printf("[product client: StandbyLease + ClientSessionController at a real host]\n");
  FakeDirectory dir;
  check("the fake directory starts", dir.Start());
  ScriptDirectory(dir, "h-standby", capabilitiesBody(tokens, kIssuedIp, kIssuedPort));
  SpawnedHost host;
  const uint16_t mediaPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
  SetEnvironmentVariableW(L"REMOTE60_DIRECTORY_HEARTBEAT_SEC", L"1");
  check("a real GNLinkStream starts",
        StartHost(&host, dir.url(), mediaPort, remote60::native_poc::e2e::e2e_pick_free_tcp_port(), "standby"));
  check("the host registers", host.WaitFor("directory registered", 20000) || host.WaitFor("directory online", 20000),
        host.tail());
  size_t nextToken = 0;
  auto cap = [&](remote60::native_poc::SocketHandle, remote60::native_poc::StandbyCapability* out) {
    if (nextToken >= tokens.size()) return false;
    out->token = tokens[nextToken++];
    out->ip = "127.0.0.1";
    out->port = mediaPort;
    return true;
  };
  MemGenStore storeA, storeB;
  auto randA = [](uint8_t* b, size_t n) { for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(0xA1); return true; };
  auto randB = [](uint8_t* b, size_t n) { for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(0xB1); return true; };
  GenerationAllocator gensA(storeA, randA), gensB(storeB, randB);
  const uint32_t features = remote60::native_poc::kUdpFeatureVideoFec | remote60::native_poc::kUdpFeatureVideoFecInterleaved;
  StandbyLease a(gensA, features, cap, mono_ms), b(gensB, features, cap, mono_ms);
  std::string err;
  check("A's and B's leases open (one socket each)", a.Open(&err) && b.Open(&err), err);
  a.Start();
  b.Start();
  // the first capability lands with a heartbeat (seen up to ~50 s after registering)
  const bool parkedA = pump_until(a, [&] { return a.state() == StandbyLease::State::Parked; }, 70000);
  const bool parkedB = pump_until(b, [&] { return b.state() == StandbyLease::State::Parked; }, 20000);
  check("A parks (its StandbyHello admitted)", parkedA, last_events(a));
  check("B parks, its own entry", parkedB && b.entryId() != a.entryId(), last_events(b));
  check("...and NEITHER started a session", count_of(host.log(), "first session") == 0 &&
        count_of(host.log(), "session epoch=") == 0, host.tail());

  NullSink sinkA, sinkB;
  remote60::native_poc::ClientSessionController ca, cb;
  remote60::native_poc::ClientSessionConnectArgs argsA;
  argsA.encodedFrameSink = &sinkA;
  argsA.controlIntervalMs = 200;
  check("A's activation arguments: its lease's socket and the activation handshake", a.ActivationArgs(&argsA));
  const bool upA = ca.Connect(argsA) &&
                   wait_until([&] { return ca.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000);
  check("A's ClientSessionController connects THROUGH the activation (no Hello)", upA, ca.Snapshot().lastError + " | " + last_events(a));
  check("...the host admitted it as the first session by activation", host.WaitFor("first session by activation", 3000), host.tail());
  check("...video reaches A's sink", wait_until([&] { return sinkA.frames() > 0; }, 10000));
  ca.RequestWindowList();
  check("...and A's control works (its window list arrives)", wait_until([&] { return ca.Snapshot().latestWindowListCount > 0; }, 8000));

  std::printf("  -- B, parked, pings for 12 s; A must not notice --\n");
  const size_t epochs = count_of(host.log(), "session epoch=");
  const size_t ended = count_of(host.log(), "control session ended");
  pump_until(b, [] { return false; }, 12000);
  check("B stays parked (its Pings answered), no entry lost", b.state() == StandbyLease::State::Parked && b.lostCount() == 0,
        last_events(b));
  check("NO new session, NO control-session end while B pinged", count_of(host.log(), "session epoch=") == epochs &&
        count_of(host.log(), "control session ended") == ended, host.tail());
  check("A is still connected", ca.Snapshot().state == remote60::native_poc::ClientSessionState::Connected);

  std::printf("  -- B takes over explicitly; A's session ends; A parks again; A takes it back --\n");
  remote60::native_poc::ClientSessionConnectArgs argsB;
  argsB.encodedFrameSink = &sinkB;
  argsB.controlIntervalMs = 200;
  check("B's activation arguments", b.ActivationArgs(&argsB));
  const bool upB = cb.Connect(argsB) &&
                   wait_until([&] { return cb.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000);
  check("B connects through ITS activation", upB, cb.Snapshot().lastError + " | " + last_events(b));
  check("...a takeover: a new epoch for B", host.WaitFor("(standby activation", 3000) &&
        count_of(host.log(), "session epoch=") == epochs + 1, host.tail());
  check("...video reaches B", wait_until([&] { return sinkB.frames() > 0; }, 10000));
  const bool aEnded =
      wait_until([&] { return ca.Snapshot().state != remote60::native_poc::ClientSessionState::Connected; }, 25000);
  check("A's session ends (the host moved on)", aEnded, ca.Snapshot().status);
  ca.Disconnect();
  a.SessionOver();
  pump_until(a, [] { return false; }, 7000);
  check("A parks again on its lease's own socket: its entry answers its Pings", a.state() == StandbyLease::State::Parked &&
        a.lostCount() == 0, last_events(a));
  remote60::native_poc::ClientSessionConnectArgs argsA2;
  argsA2.encodedFrameSink = &sinkA;
  argsA2.controlIntervalMs = 200;
  const uint64_t framesBefore = sinkA.frames();
  const bool upA2 = a.ActivationArgs(&argsA2) && ca.Connect(argsA2) &&
                    wait_until([&] { return ca.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000);
  check("A takes it back (a larger generation)", upA2, ca.Snapshot().lastError + " | " + last_events(a));
  check("...video reaches A again", wait_until([&] { return sinkA.frames() > framesBefore; }, 10000));
  // every session the host made came from an activation: the first one logged as such, and every rollover tagged
  // "(standby activation" -- a Hello would make a first session without that line or an untagged "session epoch="
  {
    const std::string log = host.log();
    const size_t epochsAll = count_of(log, "session epoch=");
    const size_t epochsByActivation = count_of(log, "(standby activation");
    check("no ordinary Hello made a session: the first by activation, every rollover by activation",
          count_of(log, "client connected transport") == 1 && count_of(log, "first session by activation") == 1 &&
              epochsAll == epochsByActivation && epochsAll == 2,
          "first=" + std::to_string(count_of(log, "first session by activation")) + " epochs=" + std::to_string(epochsAll) +
              " byActivation=" + std::to_string(epochsByActivation));
  }
  ca.Disconnect();
  cb.Disconnect();

  std::printf("  -- the app's StandbyRunner (a pump thread per PC): start, activate, session over, release --\n");
  {
    MemGenStore storeR;
    auto randR = [](uint8_t* b, size_t n) { for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(0xD1); return true; };
    GenerationAllocator gensR(storeR, randR);
    remote60::native_poc::StandbyRunner runner(gensR, features, [&](const std::string&) { return cap; }, mono_ms);
    check("the runner starts a lease for the PC (its own thread)", runner.Start("h-standby") && runner.size() == 1);
    check("...it parks without anyone pumping it here",
          wait_until([&] { return runner.StatusOf("h-standby").state == StandbyLease::State::Parked; }, 20000));
    NullSink sinkR;
    remote60::native_poc::ClientSessionController cr;
    remote60::native_poc::ClientSessionConnectArgs argsR;
    argsR.encodedFrameSink = &sinkR;
    argsR.controlIntervalMs = 200;
    const size_t epochsR = count_of(host.log(), "(standby activation");
    const bool upR = runner.TakeActivation("h-standby", &argsR) && cr.Connect(argsR) &&
                     wait_until([&] { return cr.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000);
    check("TakeActivation + Connect: the runner's PC takes the session by activation (video)", upR &&
          count_of(host.log(), "(standby activation") == epochsR + 1 && wait_until([&] { return sinkR.frames() > 0; }, 10000),
          cr.Snapshot().lastError);
    check("...while handed off the runner reports it so (the pump leaves the socket alone)",
          runner.StatusOf("h-standby").handedOff);
    const size_t endedR = count_of(host.log(), "control session ended");
    cr.Disconnect();
    runner.SessionOver("h-standby");
    // the host ends that grant with the session's real end (contract §2.3 S3) -- until then the entry holds the session
    // and refuses a Release (Live): a release sent then is simply refused and the entry lapses on its lease later
    check("the host sees the session end (the grant ends with it)",
          wait_until([&] { return count_of(host.log(), "control session ended") > endedR; }, 30000), host.tail());
    check("session over: parked again by the runner's own pings",
          wait_until([&] { const auto st = runner.StatusOf("h-standby");
                           return st.state == StandbyLease::State::Parked && !st.handedOff; }, 15000));
    const size_t releasedBefore = count_of(host.log(), "[standby] released");
    runner.Release("h-standby");
    check("Release: the lease is gone here and released at the host (one StandbyRelease)",
          runner.size() == 0 && host.WaitFor("[standby] released", 3000) && count_of(host.log(), "[standby] released") == releasedBefore + 1,
          host.tail());
  }
  host.Stop();

  if (realDirectory) {
    std::printf("\n[product client through the REAL server.js relay: support from /api/hosts, standby connects, a lease each]\n");
    wchar_t nodeExe[MAX_PATH] = {};
    const bool haveNode = SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, nodeExe, nullptr) > 0;
    const std::string repo = REMOTE60_TEST_REPO_ROOT;
    const std::wstring serverJs = std::wstring(repo.begin(), repo.end()) + L"/apps/directory/server.js";
    NodeServer srv;
    check("the real directory server starts (relay on, loopback)", haveNode && srv.Start(nodeExe, serverJs));
    SpawnedHost rh;
    const uint16_t rPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    const std::string url = "http://127.0.0.1:" + std::to_string(srv.httpPort);
    check("a real GNLinkStream starts against it", StartHost(&rh, url, rPort, remote60::native_poc::e2e::e2e_pick_free_tcp_port(), "realdir"));
    check("the host is online", rh.WaitFor("directory online", 30000), rh.tail());
    std::string session, lerr;
    check("a client signs in (the product's directory_login)",
          remote60::native_poc::directory_login(url, "tester", "e2e-pass-1234", &session, &lerr), lerr);
    std::vector<remote60::native_poc::DirectoryHostEntry> hosts;
    remote60::native_poc::directory_list_hosts(url, session, &hosts, &lerr);
    const std::string hostId = hosts.empty() ? std::string() : hosts.front().hostId;
    bool supported = false, relay = false;
    check("standby support is read from /api/hosts (no side effect): supported, with a relay",
          remote60::native_poc::directory_standby_support(url, session, &supported, &relay, &lerr) && supported && relay, lerr);
    int observeSeq = 0;
    std::string lastRelayState;
    // The capability through the lease's OWN socket: observe from it, then a standby connect -- the relay binds the
    // lease to this endpoint. Aim at the relay only when THIS connect reserved one.
    auto relayCap = [&](remote60::native_poc::SocketHandle s, remote60::native_poc::StandbyCapability* out) {
      const std::string obs = "prod-standby-" + std::to_string(++observeSeq);
      if (!observe_from(s, srv.udpPort, obs)) return false;
      remote60::native_poc::DirectoryConnectTarget t;
      std::string e;
      if (!remote60::native_poc::directory_connect(url, session, hostId, obs, &t, &e, nullptr, /*standby=*/true)) return false;
      lastRelayState = t.standbyRelay;
      if (t.standbyRelay != "reserved" || t.relayPort == 0) return false;
      out->token = t.punchToken;
      out->ip = t.relayIp;
      out->port = t.relayPort;
      return true;
    };
    MemGenStore rsA, rsB;
    GenerationAllocator rgA(rsA, randA), rgB(rsB, randB);
    StandbyLease ra(rgA, features, relayCap, mono_ms), rb(rgB, features, relayCap, mono_ms);
    check("two leases open", ra.Open(&err) && rb.Open(&err), err);
    ra.Start();
    rb.Start();
    check("each standby connect reserved its own relay lease", lastRelayState == "reserved", lastRelayState);
    const bool rpa = pump_until(ra, [&] { return ra.state() == StandbyLease::State::Parked; }, 30000);
    const bool rpb = pump_until(rb, [&] { return rb.state() == StandbyLease::State::Parked; }, 30000);
    check("A and B park THROUGH the relay, each its own entry", rpa && rpb && ra.entryId() != rb.entryId(),
          last_events(ra) + " | " + last_events(rb) + " target=" + ra.target());
    check("...two relay leases bound and ready, no session", count_of(srv.log(), "standby ready") >= 2 &&
          count_of(rh.log(), "first session") == 0, rh.tail());
    NullSink rsinkA, rsinkB;
    remote60::native_poc::ClientSessionController rca, rcb;
    remote60::native_poc::ClientSessionConnectArgs rargsA;
    rargsA.encodedFrameSink = &rsinkA;
    rargsA.controlIntervalMs = 200;
    const bool rupA = ra.ActivationArgs(&rargsA) && rca.Connect(rargsA) &&
                      wait_until([&] { return rca.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000);
    check("A's session through its relay lease, by activation", rupA && rh.WaitFor("first session by activation", 3000),
          rca.Snapshot().lastError + " | " + last_events(ra));
    check("...video through the relay", wait_until([&] { return rsinkA.frames() > 0; }, 10000));
    check("...the relay marks A's lease active", srv.WaitFor("standby active", 3000));
    pump_until(rb, [] { return false; }, 8000);
    check("B pings through its own lease: parked, A untouched", rb.state() == StandbyLease::State::Parked &&
          rca.Snapshot().state == remote60::native_poc::ClientSessionState::Connected, last_events(rb));
    remote60::native_poc::ClientSessionConnectArgs rargsB;
    rargsB.encodedFrameSink = &rsinkB;
    rargsB.controlIntervalMs = 200;
    const bool rupB = rb.ActivationArgs(&rargsB) && rcb.Connect(rargsB) &&
                      wait_until([&] { return rcb.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000);
    check("B takes over through ITS relay lease (a new epoch, video)", rupB && rh.WaitFor("(standby activation", 3000) &&
          wait_until([&] { return rsinkB.frames() > 0; }, 10000), rcb.Snapshot().lastError + " | " + last_events(rb));
    wait_until([&] { return rca.Snapshot().state != remote60::native_poc::ClientSessionState::Connected; }, 25000);
    rca.Disconnect();
    ra.SessionOver();
    // the lease kept its socket (the session had a duplicate handle): the SAME endpoint its relay lease is bound to --
    // the entry goes on by Ping through the relay, no loss, no new standby connect
    const int connectsBefore = observeSeq;
    pump_until(ra, [] { return false; }, 20000);
    check("through the relay, A's lease after its session: still parked on the same socket -- no loss, no new connect",
          ra.state() == StandbyLease::State::Parked && ra.lostCount() == 0 && observeSeq == connectsBefore, last_events(ra));
    remote60::native_poc::ClientSessionConnectArgs rargsA2;
    rargsA2.encodedFrameSink = &rsinkA;
    rargsA2.controlIntervalMs = 200;
    const uint64_t rframes = rsinkA.frames();
    const bool rupA2 = ra.ActivationArgs(&rargsA2) && rca.Connect(rargsA2) &&
                       wait_until([&] { return rca.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000);
    check("...and takes the session back through the same relay lease (video)", rupA2 &&
          wait_until([&] { return rsinkA.frames() > rframes; }, 10000), rca.Snapshot().lastError + " | " + last_events(ra));
    rca.Disconnect();
    rcb.Disconnect();
    rh.Stop();
    srv.Stop();
  }

  if (!oldHost.empty()) {
    std::printf("\n[product client at an OLDER host: Unsupported, no Hello]\n");
    FakeDirectory dir2;
    check("the fake directory starts", dir2.Start());
    ScriptDirectory(dir2, "h-old", capabilitiesBody(tokens, kIssuedIp, kIssuedPort));
    SpawnedHost old;
    const uint16_t port2 = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    SetEnvironmentVariableW(L"REMOTE60_E2E_OLD_BIN", oldHost.c_str());
    check("the older GNLinkStream starts",
          StartHost(&old, dir2.url(), port2, remote60::native_poc::e2e::e2e_pick_free_tcp_port(), "old"));
    SetEnvironmentVariableW(L"REMOTE60_E2E_OLD_BIN", L"");
    old.WaitFor("directory registered", 20000) || old.WaitFor("directory online", 20000);
    MemGenStore storeC;
    GenerationAllocator gensC(storeC, randA);
    auto capOld = [&](remote60::native_poc::SocketHandle, remote60::native_poc::StandbyCapability* out) {
      out->token = tokens[tokens.size() - 1];
      out->ip = "127.0.0.1";
      out->port = port2;
      return true;
    };
    StandbyLease c(gensC, features, capOld, mono_ms);
    check("a lease opens at the older host", c.Open(&err), err);
    c.Start();
    const bool unsupported = pump_until(c, [&] { return c.state() == StandbyLease::State::Unsupported; }, 10000);
    check("no answer: Unsupported (no automatic connect to it)", unsupported, last_events(c));
    check("...and its session table untouched: no session, no Hello admission",
          count_of(old.log(), "first session") == 0 && count_of(old.log(), "client connected") == 0, old.tail());
    old.Stop();
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  WSACleanup();
  return gFailures ? 1 : 0;
}

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--thumbnail") {
      Sleep(120000);
      return 0;
    }
  }
  if (!host_e2e_allowed()) {
    std::printf("SKIP  host_standby_admission_e2e_test (starts a listening host)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return remote60::native_poc::e2e::kE2eSkippedExit;
  }
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("FAIL  winsock did not start\n");
    return 1;
  }
  std::wstring oldHost;
  bool realDirectory = false;
  for (int i = 1; i < argc; ++i) {
    if (i + 1 < argc && std::wstring(argv[i]) == L"--old-host") oldHost = argv[i + 1];
    if (std::wstring(argv[i]) == L"--real-directory") realDirectory = true;
  }
  const char* kIssuedIp = "211.218.222.1";
  const uint16_t kIssuedPort = 60420;
  std::vector<std::string> tokens;
  for (char c = 'a'; c <= 'p'; ++c) tokens.push_back(std::string(32, c));

  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--product-client") return run_product_client(tokens, oldHost, realDirectory);
  }
  std::printf("host_standby_admission_e2e_test (keepalive-r2 A spike)\n\n");
  // ================================================================== direct
  {
    std::printf("[direct: two standby entries, one active, no interference, explicit takeover]\n");
    FakeDirectory dir;
    check("the fake directory starts", dir.Start());
    ScriptDirectory(dir, "h-standby", capabilitiesBody(tokens, kIssuedIp, kIssuedPort));
    SpawnedHost host;
    const uint16_t mediaPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    SetEnvironmentVariableW(L"REMOTE60_DIRECTORY_HEARTBEAT_SEC", L"1");
    check("a real GNLinkStream starts",
          StartHost(&host, dir.url(), mediaPort, remote60::native_poc::e2e::e2e_pick_free_tcp_port(), "standby"));
    check("the host registers", host.WaitFor("directory registered", 20000) || host.WaitFor("directory online", 20000),
          host.tail());
    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    hostAddr.sin_port = htons(mediaPort);

    SOCKET a = raw_open("127.0.0.1"), b = raw_open("127.0.0.1");
    const Entry ea = admit(a, hostAddr, 0xA1, 1, tokens[0]);
    const Entry eb = admit(b, hostAddr, 0xB1, 1, tokens[1]);
    check("A (device A1) is admitted as a standby entry", ea.ok, "status=" + std::to_string(ea.status));
    check("B (device B1) is admitted as a standby entry, its own", eb.ok && eb.id != ea.id);
    check("...and NEITHER admission started a session (no epoch, no stream)",
          count_of(host.log(), "session epoch=") == 0, host.tail());

    uint32_t refusal = 0;
    check("A's explicit activation is answered with a HelloAck (DirectoryAuth)", activate(a, hostAddr, ea, 1, &refusal),
          "refusal=" + std::to_string(refusal));
    // The FIRST session is admitted in the handshake loop ("first session by activation"); only a later takeover
    // rolls the session over (`session epoch=N peer=new (standby activation ...)`).
    check("...through the session admission: the first session, logged as an activation (no rollover yet)",
          host.WaitFor("first session by activation", 3000) && count_of(host.log(), "session epoch=") == 0, host.tail());
    check("A's control works (its window-select is processed)", select_seen(host, a, hostAddr, 1, 0xA0A0000000000001ull));

    std::printf("  -- B, parked, keeps itself alive and retries; A must not notice --\n");
    const size_t epochsBefore = count_of(host.log(), "session epoch=");
    const size_t endedBefore = count_of(host.log(), "control session ended");
    bool pingsOk = true;
    for (int i = 0; i < 10; ++i) pingsOk = pingsOk && ping(b, hostAddr, eb) == remote60::native_poc::kUdpStandbyStatusOk;
    check("B's pings are answered Ok (10)", pingsOk);
    Entry replay = admit(b, hostAddr, 0xB1, 1, tokens[1], 3);
    check("B's StandbyHello retransmit (same gen) replays the SAME entry (capability not needed again)",
          replay.ok && replay.id == eb.id);
    for (int i = 0; i < 5; ++i) raw_send_window_select(b, hostAddr, 50 + i, 0xBADB0000000000B1ull);
    Entry eb2 = admit(b, hostAddr, 0xB1, 2, tokens[2]);
    check("B 'reconnects' (new gen, new capability): a new entry replaces its old one", eb2.ok && eb2.id != eb.id);
    check("...the old one is gone", ping(b, hostAddr, eb) == remote60::native_poc::kUdpStandbyStatusUnknown);
    check("A's control still works after all that (fence)", select_seen(host, a, hostAddr, 2, 0xA0A0000000000002ull));
    check("NO new session, NO control-session end while B pinged / re-admitted / sent control",
          count_of(host.log(), "session epoch=") == epochsBefore && count_of(host.log(), "control session ended") == endedBefore,
          host.tail());
    check("B's control datagrams were dropped (never processed)",
          host.log().find("requestedId=" + std::to_string(0xBADB0000000000B1ull)) == std::string::npos);

    std::printf("  -- explicit takeover by B; A parked; A's late packets --\n");
    check("B's explicit activation takes the session", activate(b, hostAddr, eb2, 1, &refusal), host.tail());
    check("...a new epoch (3) and a new keyframe for B", host.WaitFor("(standby activation", 3000) &&
                                                         count_of(host.log(), "session epoch=") == epochsBefore + 1);
    check("B's control works", select_seen(host, b, hostAddr, 1, 0xB0B0000000000001ull));
    raw_send_window_select(a, hostAddr, 3, 0xDEADA0000000000Aull);
    check("A's late control (old session) is refused",
          !host.WaitFor(("requestedId=" + std::to_string(0xDEADA0000000000Aull)).c_str(), 1500));
    check("A is parked, not dropped: its entry still answers", ping(a, hostAddr, ea) == remote60::native_poc::kUdpStandbyStatusOk);
    refusal = 0;
    const size_t epochsAfterB = count_of(host.log(), "session epoch=");
    check("A's DELAYED Activate (old gen 1) is refused as superseded -- no re-takeover",
          !activate(a, hostAddr, ea, 1, &refusal, 1) && refusal == remote60::native_poc::kUdpStandbyStatusSuperseded &&
              count_of(host.log(), "session epoch=") == epochsBefore + 1 && epochsAfterB == epochsBefore + 1,
          "refusal=" + std::to_string(refusal));
    check("B still has the session (fence)", select_seen(host, b, hostAddr, 2, 0xB0B0000000000002ull));
    check("A's NEW explicit activation (gen 2) takes it back", activate(a, hostAddr, ea, 2, &refusal) &&
                                                               select_seen(host, a, hostAddr, 1, 0xA0A0000000000003ull));

    std::printf("  -- bounds --\n");
    std::vector<SOCKET> extra;
    int admitted = 0;
    Entry last;
    for (int d = 0; d < 6; ++d) {
      extra.push_back(raw_open("127.0.0.1"));
      last = admit(extra.back(), hostAddr, static_cast<uint8_t>(0xC0 + d), 1, tokens[3 + d]);
      admitted += last.ok ? 1 : 0;
    }
    check("six more devices fill the host to eight entries", admitted == 6, std::to_string(admitted));
    extra.push_back(raw_open("127.0.0.1"));
    const Entry ninth = admit(extra.back(), hostAddr, 0xD9, 1, tokens[9]);
    check("a ninth device is answered BUSY", !ninth.ok && ninth.status == remote60::native_poc::kUdpStandbyStatusBusy,
          "status=" + std::to_string(ninth.status));
    sb_send(extra[5], hostAddr, UdpPacketKind::StandbyRelease, 0, 0, std::string(), &last);
    Sleep(200);
    const Entry tenth = admit(extra.back(), hostAddr, 0xDA, 1, tokens[10]);
    check("after one Release, a new device gets a place", tenth.ok, "status=" + std::to_string(tenth.status));
    check("A's session went through all of it untouched", select_seen(host, a, hostAddr, 2, 0xA0A0000000000004ull));
    for (SOCKET s : extra) closesocket(s);

    std::printf("  -- lease --\n");
    const Entry lapsing = admit(b, hostAddr, 0xB1, 3, tokens[11]);
    Sleep(remote60::native_poc::kStandbyLeaseMs + 1500);
    check("an entry that stops pinging lapses after its lease (20 s)",
          lapsing.ok && ping(b, hostAddr, lapsing) == remote60::native_poc::kUdpStandbyStatusUnknown);

    std::printf("  -- r3 S3: the active grant ends with its session --\n");
    // A's raw client acknowledges none of the host's control data: its session ends as peer-lost (a real end,
    // not a resume) within seconds -- long before now.
    check("A's session has really ended (its control session ended)",
          host.WaitFor("control session ended", 15000), host.tail());
    refusal = 0;
    const size_t epochsAtEnd = count_of(host.log(), "session epoch=");
    check("S3: A's SAME activation again (gen 2) is refused -- the grant ended with its session, no Ack of it",
          !activate(a, hostAddr, ea, 2, &refusal, 1) && refusal == remote60::native_poc::kUdpStandbyStatusSuperseded &&
              count_of(host.log(), "session epoch=") == epochsAtEnd,
          "refusal=" + std::to_string(refusal));
    check("S3: ...A, parked again, still answers its pings", ping(a, hostAddr, ea) == remote60::native_poc::kUdpStandbyStatusOk);
    check("S3: a NEW activation (gen 3) is a new session",
          activate(a, hostAddr, ea, 3, &refusal) && count_of(host.log(), "session epoch=") == epochsAtEnd + 1);
    closesocket(a);
    closesocket(b);
    host.Stop();
    check("the host exited cleanly", host.stopClean, host.stopWhy);
  }

  // ================================================================== relay stand-in
  // r3 (Codex decision 1 / S4): the relay keeps ONE host-facing socket per client lease -- (a). A shared
  // host-facing address (b) is not part of this R2: behind one, the host's control/ACK/NACK/resume/bulk gate is
  // the address, and a parked client's raw packets would pass it. It is no longer exercised here.
  {
    std::printf("\n[relay stand-in, per-client host-facing socket: A direct active, B and C through the relay]\n");
    FakeDirectory dir;
    check("the fake directory starts", dir.Start());
    ScriptDirectory(dir, "h-relay", capabilitiesBody(tokens, kIssuedIp, kIssuedPort));
    SpawnedHost host;
    const uint16_t mediaPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    check("a real GNLinkStream starts",
          StartHost(&host, dir.url(), mediaPort, remote60::native_poc::e2e::e2e_pick_free_tcp_port(), "relay"));
    check("the host registers", host.WaitFor("directory registered", 20000) || host.WaitFor("directory online", 20000));
    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    hostAddr.sin_port = htons(mediaPort);
    RelayMock relay;
    check("the relay stand-in starts", relay.Start(hostAddr));
    SOCKET a = raw_open("127.0.0.1"), b = raw_open("127.0.0.1"), c = raw_open("127.0.0.1");
    const Entry ea = admit(a, hostAddr, 0xA2, 1, tokens[0]);
    uint32_t refusal = 0;
    check("A (direct) is admitted and activated", ea.ok && activate(a, hostAddr, ea, 1, &refusal) &&
                                                      select_seen(host, a, hostAddr, 1, 0xA1A1000000000001ull));
    const Entry eb = admit(b, relay.frontAddr, 0xB2, 1, tokens[1]);
    const Entry ec = admit(c, relay.frontAddr, 0xC2, 1, tokens[2]);
    check("B and C are admitted THROUGH the relay, each its own entry", eb.ok && ec.ok && eb.id != ec.id,
          "b=" + std::to_string(eb.status) + " c=" + std::to_string(ec.status));
    bool pings = true;
    for (int i = 0; i < 5; ++i)
      pings = pings && ping(b, relay.frontAddr, eb) == 0 && ping(c, relay.frontAddr, ec) == 0;
    check("their pings are told apart by entry, not by address", pings);
    check("A's direct session is untouched by both", select_seen(host, a, hostAddr, 2, 0xA1A1000000000002ull) &&
                                                         count_of(host.log(), "session epoch=") == 0);
    check("B activates through the relay: takes the session", activate(b, relay.frontAddr, eb, 1, &refusal) &&
                                                                  host.WaitFor("(standby activation", 3000) &&
                                                                  count_of(host.log(), "session epoch=") == 1);
    check("B's control (through the relay) works", select_seen(host, b, relay.frontAddr, 1, 0xB2B2000000000001ull));
    check("C, parked behind the same relay, still answers", ping(c, relay.frontAddr, ec) == 0);
    check("...and A, parked, too", ping(a, hostAddr, ea) == 0);
    std::printf("  -- r3 S4: C, parked behind the relay, sends session packets while B is active --\n");
    const size_t epochsB = count_of(host.log(), "session epoch=");
    const size_t endedB = count_of(host.log(), "control session ended");
    count_non_standby_rx(c, 200);  // whatever was in flight before
    for (int i = 0; i < 5; ++i) raw_send_window_select(c, relay.frontAddr, 70 + i, 0xC4C4000000000000ull + i);
    for (int i = 0; i < 5; ++i) raw_send_ctrl_ack(c, relay.frontAddr, 1, 100 + i);
    for (int i = 0; i < 5; ++i) raw_send_ctrl_nack(c, relay.frontAddr, 2, 1 + i);
    for (int i = 0; i < 5; ++i) raw_send_video_nack(c, relay.frontAddr, 1 + i);
    for (int i = 0; i < 3; ++i) raw_send_resume(c, relay.frontAddr, 0xC5C50000u + i);
    for (int i = 0; i < 5; ++i) raw_send_bulk(c, relay.frontAddr, 1 + i);
    Entry cForged = ec;
    std::memset(cForged.secret, 0x5A, 16);
    uint32_t cRefusal = 0;
    const bool forgedTook = activate(c, relay.frontAddr, cForged, 1, &cRefusal, 1);
    uint32_t cStale = 0;
    const bool staleTook = activate(c, relay.frontAddr, ec, 0, &cStale, 1);
    check("S4: C's control data / ACKs through its own relay leg are never processed",
          host.log().find("requestedId=" + std::to_string(0xC4C4000000000000ull)) == std::string::npos);
    check("S4: C's activation with a wrong secret and with gen 0 are refused",
          !forgedTook && cRefusal == remote60::native_poc::kUdpStandbyStatusUnknown && !staleTook &&
              cStale == remote60::native_poc::kUdpStandbyStatusSuperseded,
          "wrong-secret=" + std::to_string(cRefusal) + " gen0=" + std::to_string(cStale));
    check("S4: C's control-NACK / video-NACK / resume / bulk get NOTHING back on C's leg (no retransmit, no ResumeAck)",
          count_non_standby_rx(c, 1500) == 0);
    check("S4: ...C's resume is ignored as from another peer (no reply, no state)",
          host.log().find("resume ignored id=" + std::to_string(0xC5C50000u)) != std::string::npos, host.tail());
    count_non_standby_rx(b, 300);  // B's leg drained: what follows is the host's answer to B's next select
    check("S4: ...B keeps the session: no new epoch, no control-session end, its control still works",
          count_of(host.log(), "session epoch=") == epochsB && count_of(host.log(), "control session ended") == endedB &&
              select_seen(host, b, relay.frontAddr, 2, 0xB2B2000000000002ull));
    uint32_t bSeq = 0;
    check("S4: ...and the host still sends B's control data back down B's own leg",
          raw_wait_ctrl_data(b, remote60::native_poc::kUdpControlStreamHostToClient, 0, 3000, &bSeq));
    closesocket(a);
    closesocket(b);
    closesocket(c);
    relay.Stop();
    host.Stop();
  }

  // ================================================================== the REAL directory server's relay
  // r3 (Codex decision 1 (a), "제한된 로컬 실제 server.js 다중 lease 시험"): apps/directory/server.js under node, a real
  // GNLinkStream registered and heartbeating to it, and two raw clients each taking a STANDBY relay lease through
  // it (/api/connect {standby:true}). The relay gives each lease its own host-facing socket; the host's existing
  // punch to the extra pendingPunch entry pins it. Loopback: no NAT is crossed here -- the NAT path is shown
  // only as far as "the host punched the reserved port and the lease pinned it".
  if (realDirectory) {
    std::printf("\n[the real directory server.js relay: two standby leases through it, one active]\n");
    wchar_t nodeExe[MAX_PATH] = {};
    const bool haveNode = SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, nodeExe, nullptr) > 0;
    const std::string repo = REMOTE60_TEST_REPO_ROOT;
    const std::wstring serverJs = std::wstring(repo.begin(), repo.end()) + L"/apps/directory/server.js";
    NodeServer srv;
    check("node and the repository's server.js are found", haveNode && GetFileAttributesW(serverJs.c_str()) != INVALID_FILE_ATTRIBUTES);
    check("the real directory server starts (relay on, loopback, its own staging)", haveNode && srv.Start(nodeExe, serverJs),
          srv.log().size() > 300 ? srv.log().substr(srv.log().size() - 300) : srv.log());
    SpawnedHost host;
    const uint16_t mediaPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    check("a real GNLinkStream starts against it",
          StartHost(&host, "http://127.0.0.1:" + std::to_string(srv.httpPort), mediaPort,
                    remote60::native_poc::e2e::e2e_pick_free_tcp_port(), "realdir"));
    // Online = a heartbeat with an observation went through (registered alone is not: /api/connect says offline).
    check("the host registers with the real server and is online", host.WaitFor("directory online", 30000), host.tail());
    std::string body;
    const int loginStatus = http_call(srv.httpPort, "POST", "/api/login", "{\"id\":\"tester\",\"pw\":\"e2e-pass-1234\"}", "", &body);
    const std::string session = json_str(body, "sessionToken");
    http_call(srv.httpPort, "GET", "/api/hosts", "", session, &body);
    const std::string hostId = json_str(body, "hostId");
    check("a client signs in and sees the host", loginStatus == 200 && !session.empty() && !hostId.empty(),
          "login=" + std::to_string(loginStatus));
    sockaddr_in relayAddr{};
    relayAddr.sin_family = AF_INET;
    relayAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    relayAddr.sin_port = htons(srv.relayPort);
    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    hostAddr.sin_port = htons(mediaPort);
    SOCKET b = raw_open("127.0.0.1"), c = raw_open("127.0.0.1");
    const auto standbyConnect = [&](SOCKET s, const char* obsToken, std::string* reply) {
      if (!observe_from(s, srv.udpPort, obsToken)) return std::string();
      http_call(srv.httpPort, "POST", "/api/connect",
                std::string("{\"hostId\":\"") + hostId + "\",\"observeToken\":\"" + obsToken + "\",\"standby\":true}",
                session, reply);
      return json_str(*reply, "punchToken");
    };
    std::string rb, rc;
    const std::string tb = standbyConnect(b, "realdir-b-observe", &rb);
    const std::string tc = standbyConnect(c, "realdir-c-observe", &rc);
    check("two standby connects: each reserved its own relay lease",
          !tb.empty() && !tc.empty() && json_str(rb, "standbyRelay") == "reserved" && json_str(rc, "standbyRelay") == "reserved",
          rb + " | " + rc);
    // The host collects both capabilities with a heartbeat and punches every entry -- the two relay-facing ones too.
    check("the host collects them and punches both reserved host-facing ports; both leases pin the host",
          wait_until([&] { return count_of(srv.log(), "standby host pinned") >= 2; }, 15000),
          host.Lines("directory punch ->"));
    const Entry eb = admit(b, relayAddr, 0xB7, 1, tb, 40);
    const Entry ec = admit(c, relayAddr, 0xC7, 1, tc, 40);
    check("B and C are admitted as standby entries THROUGH the real relay, each its own",
          eb.ok && ec.ok && eb.id != ec.id, "b=" + std::to_string(eb.status) + " c=" + std::to_string(ec.status));
    check("...the relay saw both leases bound and ready (two host-facing sockets)",
          count_of(srv.log(), "standby bound client=") == 2 && count_of(srv.log(), "standby ready") == 2);
    check("...and no session started", count_of(host.log(), "session epoch=") == 0 &&
                                           host.log().find("first session by activation") == std::string::npos);
    uint32_t refusal = 0;
    check("B activates over its lease: HelloAck through the relay, the relay marks the lease active",
          activate(b, relayAddr, eb, 1, &refusal) && srv.WaitFor("standby active", 3000), "refusal=" + std::to_string(refusal));
    check("B's control (through the real relay) works", select_seen(host, b, relayAddr, 1, 0xB7B7000000000001ull));
    bool pings = true;
    for (int i = 0; i < 5; ++i) pings = pings && ping(c, relayAddr, ec) == remote60::native_poc::kUdpStandbyStatusOk;
    check("C, parked on its own lease, keeps pinging", pings);
    const size_t epochs = count_of(host.log(), "session epoch=");
    for (int i = 0; i < 5; ++i) raw_send_window_select(c, relayAddr, 80 + i, 0xC7C7000000000000ull + i);
    for (int i = 0; i < 3; ++i) raw_send_resume(c, relayAddr, 0xC7C70000u + i);
    check("C's session packets through its lease are never processed (the host tells the leases apart by address)",
          host.log().find("requestedId=" + std::to_string(0xC7C7000000000000ull)) == std::string::npos);
    check("...B keeps the session, its control still works",
          count_of(host.log(), "session epoch=") == epochs && select_seen(host, b, relayAddr, 2, 0xB7B7000000000002ull));
    check("C's explicit activation takes it (a new epoch), its control works",
          activate(c, relayAddr, ec, 1, &refusal) && host.WaitFor("(standby activation", 3000) &&
              select_seen(host, c, relayAddr, 1, 0xC7C7000000000101ull));
    raw_send_window_select(b, relayAddr, 3, 0xB7B70000000000FFull);
    check("B's late control (its old session) is refused",
          !host.WaitFor(("requestedId=" + std::to_string(0xB7B70000000000FFull)).c_str(), 1500));
    check("B, parked again, still pings Ok through its lease", ping(b, relayAddr, eb) == remote60::native_poc::kUdpStandbyStatusOk);
    closesocket(b);
    closesocket(c);
    host.Stop();
    srv.Stop();
    check("the host exited cleanly", host.stopClean, host.stopWhy);
  } else {
    std::printf("\n[the real directory server.js relay] not run (pass --real-directory; needs node on PATH)\n");
  }

  // ================================================================== an older host
  if (!oldHost.empty()) {
    std::printf("\n[an OLDER host (no standby): StandbyHello is not answered and takes nothing]\n");
    SetEnvironmentVariableW(L"REMOTE60_E2E_OLD_BIN", oldHost.c_str());
    FakeDirectory dir;
    check("the fake directory starts", dir.Start());
    ScriptDirectory(dir, "h-old", capabilitiesBody(tokens, kIssuedIp, kIssuedPort));
    SpawnedHost host;
    const uint16_t mediaPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    check("the older GNLinkStream starts",
          StartHost(&host, dir.url(), mediaPort, remote60::native_poc::e2e::e2e_pick_free_tcp_port(), "old"));
    check("it registers", host.WaitFor("directory registered", 20000) || host.WaitFor("directory online", 20000));
    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    hostAddr.sin_port = htons(mediaPort);
    SOCKET a = raw_open("127.0.0.1"), b = raw_open("127.0.0.1");
    bool aUp = false;
    for (int i = 0; i < 60 && !aUp; ++i) {
      raw_send_hello(a, hostAddr, tokens[0]);
      Sleep(200);
      aUp = select_seen(host, a, hostAddr, 1, 0x01D0000000000001ull, 1200);
    }
    check("A holds a session on the older host (ordinary Hello)", aUp);
    const size_t epochs = count_of(host.log(), "session epoch=");
    // 3 tries (1.2 s): a host that has standby answers within milliseconds. Kept short because this raw client
    // does not acknowledge the host's control data, and an older host ends such a peer as lost after ~6 s
    // (10-26: 8 tries let A's session lapse before its second select -- the harness, not the probe).
    const Entry eb = admit(b, hostAddr, 0xB3, 1, tokens[1], 3);
    check("B's StandbyHello gets NO answer (= 'not supported'; a client stops here, sends no Hello)",
          !eb.ok && eb.status == 99);
    check("A's session on the older host is untouched", select_seen(host, a, hostAddr, 2, 0x01D0000000000002ull) &&
                                                         count_of(host.log(), "session epoch=") == epochs);
    closesocket(a);
    closesocket(b);
    host.Stop();
    SetEnvironmentVariableW(L"REMOTE60_E2E_OLD_BIN", nullptr);
  } else {
    std::printf("\n[an OLDER host] not run (pass --old-host <GNLinkStream.exe without standby>)\n");
  }

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  WSACleanup();
  return gFailures ? 1 : 0;
}

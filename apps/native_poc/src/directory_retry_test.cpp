// What the clients do when the directory says it has no address observation for them.
//
// The server answers 409 for that now, and it is a state either end can repair: send another
// observation and ask again. The dangerous shapes are the two either side of doing it right --
// treating it as an authentication failure (which signs the host out and makes it re-register,
// fixing nothing and losing the cached token) and retrying in a loop (which spins against a
// server refusing for a reason the client cannot fix, with a user waiting on it).
//
// Neither shape shows up in a passing suite unless something counts the requests. So this file
// stands up a directory -- http and udp, in this process, on ports the OS picks -- scripts what it
// answers, and counts what arrives. The assertions are about the number of requests as much as the
// outcome: "it worked" is also true of a client that tried forty times.
//
// The same fixture covers directory_observe_from_health(), which had no test at all: it is the
// route a viewer uses when it resumed from a stored session and so never saw a login response.
//
// THIS SUITE TAKES ABOUT 140 SECONDS, AND THAT IS NOT A HANG.
//
// Every wait here is bounded (the polls are 150 x 100ms and then fail), but the waits are real:
// the product floors the heartbeat interval at five seconds -- `if (cfg_.heartbeatSeconds < 5)
// cfg_.heartbeatSeconds = 5` -- so a test cannot ask for a faster cycle, and each HostAgent
// scenario has to sit through one. The only way to make this quicker is to let the interval be
// injected, the way the update tests inject their lock name.
//
// Recorded here because the tempting fix is to delete scenarios, and the scenarios are the point:
// what they assert is the number of requests, which is the only thing that separates "repaired it
// once" from "retried in a loop" or "signed out on the way to succeeding".

#ifndef NOMINMAX
#define NOMINMAX  // or windows.h's min/max macros eat the (std::min) in native_socket.hpp
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "directory_client.hpp"
#include "directory_session_bootstrap.hpp"
#include "directory_session_client.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace {

int gFailures = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!cond) ++gFailures;
}

struct Reply {
  int status = 200;
  std::string body;
};

/**
 * A directory that answers what the test tells it to, and remembers what it was asked.
 *
 * Both halves, because the flow needs both: the UDP probe is what makes an observation, and the
 * HTTP call is where the server's opinion of that observation comes back.
 */
class FakeDirectory {
 public:
  bool Start() {
    http_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    udp_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (http_ == INVALID_SOCKET || udp_ == INVALID_SOCKET) return false;
    if (!Bind(http_, &httpPort_) || !Bind(udp_, &udpPort_)) return false;
    if (::listen(http_, 8) != 0) return false;
    httpThread_ = std::thread([this] { ServeHttp(); });
    udpThread_ = std::thread([this] { ServeUdp(); });
    return true;
  }

  void Stop() {
    stopping_ = true;
    if (http_ != INVALID_SOCKET) { closesocket(http_); http_ = INVALID_SOCKET; }
    if (udp_ != INVALID_SOCKET) { closesocket(udp_); udp_ = INVALID_SOCKET; }
    if (httpThread_.joinable()) httpThread_.join();
    if (udpThread_.joinable()) udpThread_.join();
  }

  ~FakeDirectory() { Stop(); }

  /** Queued answers for a path. The last one repeats once the queue runs out. */
  void Script(const std::string& path, std::vector<Reply> replies) {
    std::lock_guard<std::mutex> lk(mu_);
    scripts_[path] = std::move(replies);
    served_[path] = 0;
  }

  int Count(const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    return counts_[path];
  }

  int ObserveProbes() const { return probes_.load(); }

  /** What to answer an OBSERVE probe with. Empty = the real thing (the sender's own address). */
  void ObserveReply(const std::string& json) {
    std::lock_guard<std::mutex> lk(mu_);
    observeReply_ = json;
  }

  std::string url() const { return "http://127.0.0.1:" + std::to_string(httpPort_); }
  uint16_t udpPort() const { return udpPort_; }

 private:
  static bool Bind(SOCKET s, uint16_t* outPort) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // loopback only
    addr.sin_port = 0;                              // the OS picks
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    int len = sizeof(addr);
    if (getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
    *outPort = ntohs(addr.sin_port);
    return true;
  }

  Reply Next(const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    ++counts_[path];
    auto it = scripts_.find(path);
    if (it == scripts_.end() || it->second.empty()) return Reply{404, "{\"error\":\"no script\"}"};
    size_t& at = served_[path];
    const Reply reply = it->second[at < it->second.size() ? at : it->second.size() - 1];
    ++at;
    return reply;
  }

  void ServeHttp() {
    while (!stopping_) {
      SOCKET c = accept(http_, nullptr, nullptr);
      if (c == INVALID_SOCKET) return;
      std::string raw;
      char buf[4096];
      size_t headEnd = std::string::npos;
      size_t want = 0;
      for (;;) {
        if (headEnd == std::string::npos) {
          headEnd = raw.find("\r\n\r\n");
          if (headEnd != std::string::npos) {
            const size_t at = raw.find("Content-Length:");
            want = at == std::string::npos ? 0 : strtoul(raw.c_str() + at + 15, nullptr, 10);
            if (raw.size() >= headEnd + 4 + want) break;
          }
        } else if (raw.size() >= headEnd + 4 + want) {
          break;
        }
        const int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, static_cast<size_t>(n));
      }

      // "POST /api/connect HTTP/1.1" -> "/api/connect"
      std::string path;
      const size_t firstSpace = raw.find(' ');
      const size_t secondSpace = firstSpace == std::string::npos
                                     ? std::string::npos
                                     : raw.find(' ', firstSpace + 1);
      if (firstSpace != std::string::npos && secondSpace != std::string::npos) {
        path = raw.substr(firstSpace + 1, secondSpace - firstSpace - 1);
      }
      const Reply reply = Next(path);
      const std::string out = "HTTP/1.1 " + std::to_string(reply.status) + " X\r\nContent-Length: " +
                              std::to_string(reply.body.size()) +
                              "\r\nConnection: close\r\n\r\n" + reply.body;
      size_t sent = 0;
      while (sent < out.size()) {
        const int n = send(c, out.data() + sent, static_cast<int>(out.size() - sent), 0);
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
      }
      shutdown(c, SD_SEND);
      closesocket(c);
    }
  }

  void ServeUdp() {
    char buf[512];
    while (!stopping_) {
      sockaddr_in from{};
      int fromLen = sizeof(from);
      const int n = recvfrom(udp_, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from),
                            &fromLen);
      if (n <= 0) return;
      buf[n] = '\0';
      if (std::string(buf, static_cast<size_t>(n)).rfind("OBSERVE ", 0) != 0) continue;
      ++probes_;
      char ip[INET_ADDRSTRLEN] = {};
      inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
      std::string scripted;
      {
        std::lock_guard<std::mutex> lk(mu_);
        scripted = observeReply_;
      }
      const std::string reply =
          scripted.empty() ? std::string("{\"ip\":\"") + ip + "\",\"port\":" +
                                 std::to_string(ntohs(from.sin_port)) + "}"
                           : scripted;
      sendto(udp_, reply.data(), static_cast<int>(reply.size()), 0,
             reinterpret_cast<sockaddr*>(&from), fromLen);
    }
  }

  SOCKET http_ = INVALID_SOCKET;
  SOCKET udp_ = INVALID_SOCKET;
  uint16_t httpPort_ = 0;
  uint16_t udpPort_ = 0;
  std::map<std::string, std::vector<Reply>> scripts_;
  std::map<std::string, size_t> served_;
  std::map<std::string, int> counts_;
  std::atomic<int> probes_{0};
  std::string observeReply_;
  std::mutex mu_;
  std::thread httpThread_;
  std::thread udpThread_;
  std::atomic<bool> stopping_{false};
};

std::string candidateBody() {
  // One candidate that resolves and answers nothing. PunchAny falls back to the first candidate
  // rather than giving up, so the session still opens -- which is what lets this test end at the
  // directory exchange instead of needing a peer.
  return "{\"punchToken\":\"" + std::string(32, 'a') +
         "\",\"hostPublicIp\":\"127.0.0.1\",\"hostPublicUdpPort\":9,"
         "\"candidates\":[{\"ip\":\"127.0.0.1\",\"port\":9,\"kind\":\"public\"}]}";
}

/**
 * Runs a HostAgent against the fake directory for a few seconds.
 *
 * The agent never owns a socket -- the address the directory observes has to be the one media
 * arrives on -- so the test supplies one, forwards what the agent sends, and feeds the replies
 * back in through ConsumeUdpPacket. That is the same wiring the real host has.
 */
std::string exe_directory() {
  char path[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, path, MAX_PATH);
  std::string text(path);
  const size_t slash = text.find_last_of("\/");
  return slash == std::string::npos ? std::string(".") : text.substr(0, slash);
}

/**
 * Writes a host cache so the agent starts as a machine that has registered before.
 *
 * That is the state the field defect lived in: a cached token means EnsureRegistered() returns
 * immediately, so nothing ever tells the host where observations go.
 */
void SeedHostCache(const std::string& path, const std::string& url) {
  remote60::native_poc::directory::HostCache cache;
  cache.directoryUrl = url;
  cache.accountId = "tester";
  cache.machineId = remote60::native_poc::directory::machine_id();
  cache.hostName = "Cached PC";
  cache.hostId = "h-cached";
  cache.hostToken = std::string(32, 'e');
  remote60::native_poc::directory::save_host_cache(path, cache);
}

std::string RunHost(FakeDirectory& dir, const char* label, int wantHeartbeats,
                    int wantRegisters, bool seedCache = false, uint16_t explicitPort = 0) {
  SOCKET media = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in bindAddr{};
  bindAddr.sin_family = AF_INET;
  bindAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(media, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr));
  DWORD timeout = 200;
  setsockopt(media, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
             sizeof(timeout));

  remote60::native_poc::directory::HostAgent agent;
  std::atomic<bool> pumping{true};
  std::thread pump([&] {
    char buf[2048];
    while (pumping.load()) {
      sockaddr_in from{};
      int fromLen = sizeof(from);
      const int n = recvfrom(media, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from),
                             &fromLen);
      if (n > 0) agent.ConsumeUdpPacket(buf, static_cast<size_t>(n), from);
    }
  });

  remote60::native_poc::directory::HostAgentConfig cfg;
  cfg.url = dir.url();
  cfg.accountId = "tester";
  cfg.password = "test-pass-1234";
  cfg.hostName = "Fixture PC";
  // Beside this executable and unique per case, so no run reads another's cached token and
  // nothing outside the build tree is written.
  cfg.cachePath = exe_directory() + "\\retry-fixture-" + std::string(label) + ".json";
  // Nothing pinned by default: the point of most of these cases is what the agent works out for
  // itself. A case that pins one says so.
  cfg.observeUdpPort = explicitPort;
  cfg.heartbeatSeconds = 5;
  if (seedCache) {
    SeedHostCache(cfg.cachePath, cfg.url);
    // No password either. Registration is then impossible, so a heartbeat can only happen if the
    // cached token was used -- which is what puts the agent in the state under test.
    cfg.password.clear();
  }

  std::string error;
  const bool started = agent.Start(cfg, [&](const void* data, size_t len, const sockaddr_in& to) {
    sendto(media, static_cast<const char*>(data), static_cast<int>(len), 0,
           reinterpret_cast<const sockaddr*>(&to), sizeof(to));
  }, &error);
  check((std::string("the host agent starts (") + label + ")").c_str(), started, error);

  // Waits for what this case is about rather than for a fixed span: the repaired 409 retries
  // immediately, while a cleared token waits out a heartbeat cycle before registering again.
  // Bounded, so a client that never gets there fails rather than hangs.
  for (int i = 0; i < 150; ++i) {
    if (dir.Count("/api/host/heartbeat") >= wantHeartbeats &&
        dir.Count("/api/host/register") >= wantRegisters) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  const std::string status = agent.StatusLine();
  agent.Stop();
  pumping = false;
  closesocket(media);
  if (pump.joinable()) pump.join();
  DeleteFileA(cfg.cachePath.c_str());
  return status;
}

}  // namespace

int main() {
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("FAIL  winsock did not start\n");
    return 1;
  }

  using namespace remote60::native_poc;

  // ------------------------------------------------- directory_observe_from_health(), untested
  //
  // The route a viewer takes when it resumed from a stored session: it never saw a login
  // response, so this is the only place the observe endpoint can come from. On an https
  // directory, getting nothing here means refusing to observe -- so a reconnect would fail where
  // a fresh sign-in works, and the difference would look like nothing at all.
  {
    FakeDirectory dir;
    check("the fake directory starts", dir.Start());

    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":29181}}"}});
    directory::ObserveEndpoint got;
    std::string error;
    check("the health route carries the observe endpoint",
          directory_observe_from_health(dir.url(), &got, &error), error);
    check("...with the port the server named", got.known && got.port == 29181,
          std::to_string(got.port));

    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":29181,\"host\":\"obs.example\"}}"}});
    got = directory::ObserveEndpoint{};
    check("...and the host when there is one",
          directory_observe_from_health(dir.url(), &got, &error) && got.host == "obs.example",
          got.host);

    // An older directory says nothing. That is a documented state, not a failure of this call --
    // the port rule has an answer for absence, and reporting it as an error would turn every
    // older server into a broken one.
    dir.Script("/healthz", {Reply{200, "{\"ok\":true}"}});
    got = directory::ObserveEndpoint{};
    error.clear();
    const bool silent = directory_observe_from_health(dir.url(), &got, &error);
    check("a directory that says nothing leaves the endpoint unknown", !silent && !got.known);
    check("...and does not invent a port", got.port == 0, std::to_string(got.port));

    dir.Script("/healthz", {Reply{500, "{\"error\":\"broken\"}"}});
    got = directory::ObserveEndpoint{};
    error.clear();
    check("a server error is an error", !directory_observe_from_health(dir.url(), &got, &error));
    check("...and it says something", !error.empty(), error);

    const std::string deadUrl = dir.url();
    dir.Stop();
    got = directory::ObserveEndpoint{};
    error.clear();
    check("a directory that is not there is reported as unreachable",
          !directory_observe_from_health(deadUrl, &got, &error) &&
              error.find("cannot reach") != std::string::npos,
          error);
  }

  // ------------------------------------------------------- the viewer's retry, counted
  {
    FakeDirectory dir;
    dir.Start();
    dir.Script("/api/connect", {Reply{409, "{\"error\":\"observation_required\"}"},
                                Reply{200, candidateBody()}});

    DirectorySessionRequest request{};
    request.url = dir.url();
    request.sessionToken = "session";
    request.hostId = "host";
    request.directoryUdpPort = dir.udpPort();
    request.punchBudgetMs = 300;
    DirectorySessionResult session{};
    std::string error;
    const bool opened = directory_session_open(request, &session, &error);
    check("a 409 on connect is repaired rather than reported", opened, error);
    check("...by asking exactly twice", dir.Count("/api/connect") == 2,
          std::to_string(dir.Count("/api/connect")));
    check("...and by observing again first", dir.ObserveProbes() >= 2,
          std::to_string(dir.ObserveProbes()));
    check("...and without signing in again", dir.Count("/api/login") == 0,
          std::to_string(dir.Count("/api/login")));
    if (session.socket != kInvalidSocket) closesocket(session.socket);
    dir.Stop();
  }

  {
    // Refused twice: the client must give up, not keep going. This is the assertion that a loop
    // would fail -- the outcome is the same either way, only the count differs.
    FakeDirectory dir;
    dir.Start();
    dir.Script("/api/connect", {Reply{409, "{\"error\":\"observation_required\"}"}});

    DirectorySessionRequest request{};
    request.url = dir.url();
    request.sessionToken = "session";
    request.hostId = "host";
    request.directoryUdpPort = dir.udpPort();
    request.punchBudgetMs = 300;
    DirectorySessionResult session{};
    std::string error;
    check("a directory that keeps refusing is not retried forever",
          !directory_session_open(request, &session, &error), error);
    check("...and it stopped at two attempts", dir.Count("/api/connect") == 2,
          std::to_string(dir.Count("/api/connect")));
    dir.Stop();
  }

  {
    // Every other refusal is not repairable from here, so it must not cost a second round trip.
    FakeDirectory dir;
    dir.Start();
    dir.Script("/api/connect", {Reply{404, "{\"error\":\"host not found\"}"}});

    DirectorySessionRequest request{};
    request.url = dir.url();
    request.sessionToken = "session";
    request.hostId = "host";
    request.directoryUdpPort = dir.udpPort();
    request.punchBudgetMs = 300;
    DirectorySessionResult session{};
    std::string error;
    check("a 404 is not treated as a missing observation",
          !directory_session_open(request, &session, &error), error);
    check("...and is asked once", dir.Count("/api/connect") == 1,
          std::to_string(dir.Count("/api/connect")));
    dir.Stop();
  }

  // ------------------------------------------------------------- the host's retry, end to end
  //
  // Through HostAgent itself, not a piece of it: register, observe, heartbeat, and the 409 that
  // arrives in the middle. Two things must be true and only one of them is about the outcome.
  //
  // The heartbeat has to be sent twice -- once refused, once accepted -- and the host must NOT
  // register again. A 401 clears the cached token and re-registers, which is right for a token
  // the server has forgotten and wrong for this: nothing is wrong with the token, and dropping it
  // would lose the one thing that lets an unattended PC come back without someone walking to it.
  {
    FakeDirectory dir;
    dir.Start();
    dir.Script("/api/host/register",
               {Reply{200, "{\"hostId\":\"h1\",\"hostToken\":\"" + std::string(32, 'b') + "\"}"}});
    dir.Script("/api/host/heartbeat", {Reply{409, "{\"error\":\"observation_required\"}"},
                                       Reply{200, "{\"ok\":true}"}});
    RunHost(dir, "409-then-200", 2, 1, false, dir.udpPort());

    check("the host heartbeats again after a 409", dir.Count("/api/host/heartbeat") == 2,
          std::to_string(dir.Count("/api/host/heartbeat")));
    check("...and does not register again, so the cached token survives",
          dir.Count("/api/host/register") == 1,
          std::to_string(dir.Count("/api/host/register")));
    check("...and it observed more than once", dir.ObserveProbes() >= 2,
          std::to_string(dir.ObserveProbes()));
    dir.Stop();
  }

  {
    // The contrast that gives the assertion above its meaning: 401 IS the token being gone, and
    // it does re-register. If both statuses took the same path, the test above would pass for a
    // client that treated every refusal as a lost token.
    FakeDirectory dir;
    dir.Start();
    dir.Script("/api/host/register",
               {Reply{200, "{\"hostId\":\"h1\",\"hostToken\":\"" + std::string(32, 'b') + "\"}"}});
    dir.Script("/api/host/heartbeat", {Reply{401, "{\"error\":\"unknown host token\"}"},
                                       Reply{200, "{\"ok\":true}"}});
    RunHost(dir, "401", 1, 2, false, dir.udpPort());

    check("a 401 does register again", dir.Count("/api/host/register") >= 2,
          std::to_string(dir.Count("/api/host/register")));
    dir.Stop();
  }

  // ---------------------------------------- a port that is not a port must not become one
  //
  // The reply to the address probe is the host's own public port: it is what gets published, and
  // it decides whether anyone can reach this machine. The reader here took the digits in front of
  // a '.' and had no upper bound -- only zero was refused -- so 65537 became **1** on the way
  // through uint16_t. That is not a value anything downstream rejects. It is a plausible port,
  // and a host nobody can reach looks like a network fault rather than a parse.
  //
  // Asserted through the status line, which is where the observation surfaces: no "public=" means
  // nothing was published, which is the correct outcome for every one of these.
  {
    const char* bad[] = {
        "{\"ip\":\"1.2.3.4\",\"port\":65537}",    // truncated to 1 before
        "{\"ip\":\"1.2.3.4\",\"port\":65536}",    // one past the top
        "{\"ip\":\"1.2.3.4\",\"port\":0}",        // refused before too
        "{\"ip\":\"1.2.3.4\",\"port\":29181.5}",  // read as 29181 before
        "{\"ip\":\"1.2.3.4\",\"port\":-1}",
        "{\"ip\":\"1.2.3.4\",\"port\":\"29181\"}",
        "{\"ip\":\"1.2.3.4\",\"port\":99999999999999}",
        "{\"ip\":\"\",\"port\":29181}",           // an address that is not one
    };
    for (const char* reply : bad) {
      FakeDirectory dir;
      dir.Start();
      dir.ObserveReply(reply);
      dir.Script("/api/host/register",
                 {Reply{200, "{\"hostId\":\"h1\",\"hostToken\":\"" + std::string(32, 'c') + "\"}"}});
      dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true}"}});
      const std::string status = RunHost(dir, "bad-port", 1, 1, false, dir.udpPort());
      check((std::string("nothing is published for ") + reply).c_str(),
            status.find("public=") == std::string::npos, status);
      dir.Stop();
    }
  }

  {
    // The boundary that must still work. A rule that refuses 65535 as well would be a different
    // defect wearing the same fix.
    FakeDirectory dir;
    dir.Start();
    dir.ObserveReply("{\"ip\":\"1.2.3.4\",\"port\":65535}");
    dir.Script("/api/host/register",
               {Reply{200, "{\"hostId\":\"h1\",\"hostToken\":\"" + std::string(32, 'd') + "\"}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true}"}});
    const std::string status = RunHost(dir, "port-65535", 1, 1, false, dir.udpPort());
    check("65535 is a port and is published", status.find("public=1.2.3.4:65535") != std::string::npos,
          status);
    dir.Stop();
  }

  // ---------------------------------------- and the same reply on the viewer's path
  //
  // The viewer and the phone share one parser (directory_rendezvous.cpp), so this covers both.
  // A refused reply must leave the probe unanswered rather than dialling a number it invented.
  {
    FakeDirectory dir;
    dir.Start();
    dir.ObserveReply("{\"ip\":\"1.2.3.4\",\"port\":65537}");
    dir.Script("/api/connect", {Reply{200, candidateBody()}});

    DirectorySessionRequest request{};
    request.url = dir.url();
    request.sessionToken = "session";
    request.hostId = "host";
    request.directoryUdpPort = dir.udpPort();
    request.punchBudgetMs = 300;
    DirectorySessionResult session{};
    std::string error;
    const bool opened = directory_session_open(request, &session, &error);
    check("the viewer refuses an out-of-range observed port", !opened, error);
    check("...and never asks to connect on it", dir.Count("/api/connect") == 0,
          std::to_string(dir.Count("/api/connect")));
    dir.Stop();
  }

  {
    FakeDirectory dir;
    dir.Start();
    dir.ObserveReply("{\"ip\":\"1.2.3.4\",\"port\":29181.5}");
    dir.Script("/api/connect", {Reply{200, candidateBody()}});

    DirectorySessionRequest request{};
    request.url = dir.url();
    request.sessionToken = "session";
    request.hostId = "host";
    request.directoryUdpPort = dir.udpPort();
    request.punchBudgetMs = 300;
    DirectorySessionResult session{};
    std::string error;
    check("a fractional observed port is not a port on the viewer's side either",
          !directory_session_open(request, &session, &error), error);
    dir.Stop();
  }

  // ------------------------------------ a host that resumed from a cache still has to be told
  //
  // The field defect, reproduced without TLS. EnsureRegistered() returns immediately when a token
  // is cached, and the advertisement only ever arrived on the registration response -- so a host
  // that had registered successfully once never learned where observations go again. On https
  // there is no default to fall back to, so it could not observe, could not heartbeat, and never
  // appeared in anyone's list. The better the last run went, the more certainly the next one was
  // stuck.
  //
  // Here the fake directory's observe port is an ephemeral one the OS picked, which is NOT the
  // http port plus one. So the legacy default cannot reach it: a heartbeat proves the agent asked
  // the health route and used the answer.
  {
    FakeDirectory dir;
    dir.Start();
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                           std::to_string(dir.udpPort()) + "}}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true}"}});
    dir.Script("/api/host/register", {Reply{500, "{\"error\":\"must not be called\"}"}});

    const std::string status = RunHost(dir, "cached-advertised", 1, 0, true);
    check("a cached host asks the health route", dir.Count("/healthz") >= 1,
          std::to_string(dir.Count("/healthz")));
    check("...and heartbeats using the advertised port",
          dir.Count("/api/host/heartbeat") >= 1,
          std::to_string(dir.Count("/api/host/heartbeat")));
    check("...without registering again", dir.Count("/api/host/register") == 0,
          std::to_string(dir.Count("/api/host/register")));
    check("...and reports itself online", status.find("public=") != std::string::npos, status);
    dir.Stop();
  }

  {
    // The same, with a directory that says nothing. It must refuse rather than dial something it
    // made up -- and it must not sit in a loop asking. The count is the assertion: a poll would
    // show one request per cycle.
    FakeDirectory dir;
    dir.Start();
    dir.Script("/healthz", {Reply{200, "{\"ok\":true}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true}"}});
    dir.Script("/api/host/register", {Reply{500, "{\"error\":\"must not be called\"}"}});

    const std::string status = RunHost(dir, "cached-silent", 99, 99, true);
    check("a silent directory means nothing is published",
          status.find("public=") == std::string::npos, status);
    check("...and no heartbeat is sent", dir.Count("/api/host/heartbeat") == 0,
          std::to_string(dir.Count("/api/host/heartbeat")));
    check("...and the health route is asked, but not on a loop",
          dir.Count("/healthz") >= 1 && dir.Count("/healthz") <= 6,
          std::to_string(dir.Count("/healthz")));
    check("...and the reason is about the server, not a timeout",
          status.find("observations") != std::string::npos, status);
    dir.Stop();
  }

  {
    // A pinned port is the operator's decision and outranks the advertisement -- and the agent
    // must not ask the health route at all when it already has an answer.
    FakeDirectory dir;
    dir.Start();
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":9}}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true}"}});
    dir.Script("/api/host/register", {Reply{500, "{\"error\":\"must not be called\"}"}});

    const std::string status = RunHost(dir, "cached-pinned", 1, 0, true, dir.udpPort());
    check("a pinned port is used and the advertisement is not needed",
          dir.Count("/api/host/heartbeat") >= 1 && status.find("public=") != std::string::npos,
          status);
    check("...so the health route is not asked at all", dir.Count("/healthz") == 0,
          std::to_string(dir.Count("/healthz")));
    dir.Stop();
  }

  WSACleanup();
  std::printf(gFailures == 0 ? "\nall retry checks passed\n" : "\n%d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}

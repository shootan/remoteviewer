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
#include "directory_fake_server.hpp"
#include "poc_protocol.hpp"
#include "directory_rendezvous.hpp"
#include "directory_session_bootstrap.hpp"
#include "directory_session_client.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace {

int gFailures = 0;
// Counted here and printed in the summary: counting "^PASS" lines undercounts when the agent's
// own log output lands on the same line as a check (seen by the reviewer). (RV-18)
int gChecks = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  ++gChecks;
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!cond) ++gFailures;
}

// The fake directory lives in directory_fake_server.hpp now: a second test needs the same one.
using remote60::native_poc::test_support::FakeDirectory;
using remote60::native_poc::test_support::Reply;

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
  // Both separators. This was "\/" -- an unknown escape that is just "/", so on a Windows path
  // nothing was found and the fixture cache landed in the working directory instead. (RV-17)
  const size_t slash = text.find_last_of("\\/");
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

/**
 * A running HostAgent whose sends are recorded instead of reaching a socket.
 *
 * c1-lan-relay needs the real ConsumeUdpPacket and the real send_ seam -- the punch reply goes out
 * through the same function the outbound punches use, because it has to leave from the candidate
 * tuple the client is watching. Everything the agent does with the directory is real; only the
 * datagram's destination is a vector.
 */
struct RecordedSend {
  std::vector<uint8_t> bytes;
  sockaddr_in to{};
};

struct PunchHarness {
  remote60::native_poc::directory::HostAgent agent;
  std::mutex mu;
  std::vector<RecordedSend> sent;
  std::string cachePath;

  // A real socket, and a thread feeding what arrives back in. The recorder alone was not enough:
  // the agent's OBSERVE probe goes through the same seam, and with nowhere to go the observation
  // never came back, the cycle never reached the heartbeat, and no capability was ever collected.
  // Every punch then read reason=closed -- correctly, which is how the harness's own gap showed.
  SOCKET media = INVALID_SOCKET;
  uint16_t mediaPort = 0;  // what the client will use as the "private" candidate
  std::thread pump;
  std::atomic<bool> pumping{false};
  // Datagrams the agent addressed off this machine: recorded above, never put on the wire. (RV-17)
  std::atomic<uint64_t> offMachineSuppressed{0};

  std::vector<RecordedSend> take() {
    std::lock_guard<std::mutex> lock(mu);
    std::vector<RecordedSend> out;
    out.swap(sent);
    return out;
  }
  size_t count() {
    std::lock_guard<std::mutex> lock(mu);
    return sent.size();
  }
  void Stop() {
    agent.Stop();
    pumping = false;
    if (media != INVALID_SOCKET) {
      closesocket(media);
      media = INVALID_SOCKET;
    }
    if (pump.joinable()) pump.join();
  }
  ~PunchHarness() {
    Stop();
    std::printf("  (harness: %llu datagram(s) addressed off this machine, recorded but not sent)\n",
                static_cast<unsigned long long>(offMachineSuppressed.load()));
    if (!cachePath.empty()) DeleteFileA(cachePath.c_str());
  }
};


/**
 * A stand-in for the relay: it answers, but only after the grace period.
 *
 * The server waits RELAY_GRACE_MS (2500) before answering a punch, which is what made it lose to
 * a host that answers immediately -- and win against one that never answers at all. That timing
 * is the whole contest, so the fake keeps it.
 */
struct LateRelay {
  SOCKET sock = INVALID_SOCKET;
  uint16_t port = 0;
  std::thread thread;
  std::atomic<bool> running{false};
  std::atomic<int> answered{0};

  bool Start(uint32_t graceMs) {
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
    DWORD timeout = 200;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));
    running = true;
    thread = std::thread([this, graceMs] {
      char buf[2048];
      bool scheduled = false;
      std::chrono::steady_clock::time_point answerAt;
      sockaddr_in peer{};
      while (running.load()) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        const int n = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from),
                               &fromLen);
        if (n > 0 && !scheduled) {
          scheduled = true;
          peer = from;
          answerAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(graceMs);
        }
        if (scheduled && std::chrono::steady_clock::now() >= answerAt) {
          remote60::native_poc::UdpHelloPacket reply{};
          reply.kind = static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::Punch);
          sendto(sock, reinterpret_cast<const char*>(&reply), sizeof(reply), 0,
                 reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
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

/** A heartbeat body that hands the host one capability for a PUBLIC tuple. */
std::string heartbeatWithCapability(const std::string& token, const char* ip, uint16_t port) {
  return "{\"ok\":true,\"pendingPunch\":[{\"ip\":\"" + std::string(ip) + "\",\"port\":" +
         std::to_string(port) + ",\"punchToken\":\"" + token + "\"}]}";
}

sockaddr_in addrOf(const char* ip, uint16_t port) {
  sockaddr_in out{};
  out.sin_family = AF_INET;
  out.sin_port = htons(port);
  inet_pton(AF_INET, ip, &out.sin_addr);
  return out;
}

remote60::native_poc::UdpHelloPacket punchPacket() {
  remote60::native_poc::UdpHelloPacket packet{};
  packet.kind = static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::Punch);
  return packet;
}

/** Did anything get sent to this address? The outbound punch is the only thing that would. */
bool SentTo(PunchHarness* harness, const sockaddr_in& want) {
  std::lock_guard<std::mutex> lock(harness->mu);
  for (const RecordedSend& record : harness->sent) {
    if (record.to.sin_addr.s_addr == want.sin_addr.s_addr &&
        record.to.sin_port == want.sin_port) {
      return true;
    }
  }
  return false;
}

/**
 * Starts an agent against `dir` and waits for the state the case needs.
 *
 * `capabilityTarget`, when set, is the tuple the heartbeat hands out: the wait ends when a
 * datagram has actually gone there, which is the only thing that proves Punch() ran and so that
 * the reply window is open. Waiting for "any datagram" is not enough -- the observe probe goes
 * through the same seam, and waiting on it let the first version of this test run its punches
 * against a host that had collected nothing.
 */
// `heartbeatSeconds` matters for the refresh cases: with the ordinary five, the agent polls
// on its own often enough to drown out the thing being counted. Set it long and every
// heartbeat in the count is one a punch asked for.
bool StartPunchHarness(FakeDirectory& dir, PunchHarness* harness, const char* label,
                       const sockaddr_in* capabilityTarget, uint32_t heartbeatSeconds = 5) {
  harness->cachePath = exe_directory() + "\\punch-fixture-" + std::string(label) + ".json";

  harness->media = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in bindAddr{};
  bindAddr.sin_family = AF_INET;
  bindAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(harness->media, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr));
  sockaddr_in bound{};
  int boundLen = sizeof(bound);
  if (getsockname(harness->media, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0) {
    harness->mediaPort = ntohs(bound.sin_port);
  }
  DWORD timeout = 200;
  setsockopt(harness->media, SOL_SOCKET, SO_RCVTIMEO,
             reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  harness->pumping = true;
  harness->pump = std::thread([harness] {
    char buf[2048];
    while (harness->pumping.load()) {
      sockaddr_in from{};
      int fromLen = sizeof(from);
      const int n = recvfrom(harness->media, buf, sizeof(buf), 0,
                             reinterpret_cast<sockaddr*>(&from), &fromLen);
      if (n > 0) harness->agent.ConsumeUdpPacket(buf, static_cast<size_t>(n), from);
    }
  });
  remote60::native_poc::directory::HostAgentConfig cfg;
  cfg.url = dir.url();
  cfg.accountId = "tester";
  cfg.password = "test-pass-1234";
  cfg.hostName = "Punch PC";
  cfg.cachePath = harness->cachePath;
  cfg.heartbeatSeconds = heartbeatSeconds;

  std::string error;
  if (!harness->agent.Start(cfg, [harness](const void* data, size_t len, const sockaddr_in& to) {
        RecordedSend record;
        record.bytes.assign(static_cast<const uint8_t*>(data),
                            static_cast<const uint8_t*>(data) + len);
        record.to = to;
        {
          std::lock_guard<std::mutex> lock(harness->mu);
          harness->sent.push_back(record);
        }
        // Recorded AND sent: the directory has to see the observe probe for the cycle to get as
        // far as a heartbeat, and the reply has to leave from this socket for the same reason the
        // product needs it to -- the candidate tuple is this socket's address.
        // ...but only to loopback. Punch candidates the fixture hands out can be real-looking
        // outside addresses, and a test has no business sending anything off this machine;
        // those are recorded and asserted on, never transmitted. (RV-17)
        if ((ntohl(to.sin_addr.s_addr) >> 24) == 127) {
          sendto(harness->media, static_cast<const char*>(data), static_cast<int>(len), 0,
                 reinterpret_cast<const sockaddr*>(&to), sizeof(to));
        } else {
          harness->offMachineSuppressed.fetch_add(1);
        }
      }, &error)) {
    return false;
  }
  for (int i = 0; i < 300; ++i) {
    const bool ready = capabilityTarget ? SentTo(harness, *capabilityTarget)
                                        : dir.Count("/api/host/heartbeat") >= 1;
    if (ready) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return capabilityTarget ? SentTo(harness, *capabilityTarget)
                          : dir.Count("/api/host/heartbeat") >= 1;
}

}  // namespace

int main() {
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("FAIL  winsock did not start\n");
    return 1;
  }

  using namespace remote60::native_poc;


  // ============================================ c1-lan-relay: the host answers a punch, bounded
  //
  // The field defect: a viewer on the same LAN punched the host's private address twenty times,
  // every punch arrived, and the host sent nothing back -- so the viewer's PunchAny never saw the
  // private candidate answer and the relay's 2500 ms grace won. These run the REAL path:
  // ConsumeUdpPacket deciding, and the real send_ seam carrying the reply out of the same socket
  // the candidate tuple names.
  {
    FakeDirectory dir;
    check("the fake directory starts (punch reply)", dir.Start());
    // The observe endpoint has to come from somewhere or the cycle never reaches a heartbeat:
    // step=health ok=0 then step=observe ok=0, forever, and no capability is ever collected.
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                        std::to_string(dir.udpPort()) + "}}"}});
    const std::string token(32, 'c');
    dir.Script("/api/host/register", {Reply{200, "{\"ok\":true,\"hostId\":\"h-1\",\"hostToken\":\"" +
                                             std::string(32, 'e') + "\"}"}});
    // Script's last reply repeats, so every heartbeat keeps handing out the capability.
    dir.Script("/api/host/heartbeat",
               {Reply{200, heartbeatWithCapability(token, "211.218.222.1", 60420)}});

    PunchHarness harness;
    const sockaddr_in capabilityTarget = addrOf("211.218.222.1", 60420);
    check("the host agent starts and collects a capability",
          StartPunchHarness(dir, &harness, "reply", &capabilityTarget));

    // Everything sent while starting up is the outbound punch to the public tuple. The reply is
    // what happens next, so the record starts clean here.
    harness.take();

    const sockaddr_in client = addrOf("192.168.20.16", 60420);
    const remote60::native_poc::UdpHelloPacket punch = punchPacket();
    harness.agent.ConsumeUdpPacket(&punch, sizeof(punch), client);

    std::vector<RecordedSend> replies = harness.take();
    check("a punch from a LAN client is answered", replies.size() == 1,
          std::to_string(replies.size()) + " datagrams");
    if (replies.size() == 1) {
      check("...with one datagram of exactly the same size",
            replies[0].bytes.size() == sizeof(remote60::native_poc::UdpHelloPacket),
            std::to_string(replies[0].bytes.size()) + " bytes");
      check("...addressed back to the source that punched",
            replies[0].to.sin_addr.s_addr == client.sin_addr.s_addr &&
                replies[0].to.sin_port == client.sin_port);
      const auto* asHello =
          reinterpret_cast<const remote60::native_poc::UdpHelloPacket*>(replies[0].bytes.data());
      check("...and it is a punch, carrying nothing",
            asHello->kind == static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::Punch) &&
                asHello->authToken[0] == '\0');
    }

    // The budget, through the product path. A client sends 25 punches in an attempt; the 26th
    // and everything after it gets nothing, and the host does not start a conversation.
    for (int i = 0; i < 40; ++i) harness.agent.ConsumeUdpPacket(&punch, sizeof(punch), client);
    const size_t afterFlood = harness.take().size();
    check("one source is answered at most its allowance",
          afterFlood + 1 <= remote60::native_poc::kPunchReplyPerSource,
          std::to_string(afterFlood + 1) + " of " +
              std::to_string(remote60::native_poc::kPunchReplyPerSource));

    // A flood must not turn into a flood of HTTP either: the refresh flag is one per cycle and
    // punches do not get to re-arm it while it is up.
    const int heartbeatsBefore = dir.Count("/api/host/heartbeat");
    for (int i = 0; i < 200; ++i) harness.agent.ConsumeUdpPacket(&punch, sizeof(punch), client);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const int heartbeatsAfter = dir.Count("/api/host/heartbeat");
    check("two hundred punches do not become two hundred heartbeats",
          heartbeatsAfter - heartbeatsBefore <= 2,
          std::to_string(heartbeatsAfter - heartbeatsBefore) + " heartbeats");

    // AuthorizePeer, unchanged: the capability was issued against the PUBLIC tuple and the Hello
    // arrives from the PRIVATE one, which is exactly what a direct LAN connection looks like.
    // The rule is not being modified here -- it is being pinned, because the whole fix depends
    // on it staying true.
    remote60::native_poc::directory::HostAgent::PeerAuthDiag diag;
    const bool authorized = harness.agent.AuthorizePeer(token, client, &diag);
    check("a capability issued for the public tuple is accepted from the private one", authorized);
    check("...and is reported as an endpoint that moved", diag.endpointMoved);
    check("...and the capability is single use", !harness.agent.AuthorizePeer(token, client, &diag));
    check("...and a token nobody issued is refused",
          !harness.agent.AuthorizePeer(std::string(32, 'z'), client, &diag));
  }

  {
    // No capability, no window. A host nobody has asked about answers nothing at all -- which is
    // what keeps this from being an open reflector for anyone who finds the port.
    FakeDirectory dir;
    check("the fake directory starts (no window)", dir.Start());
    dir.Script("/api/host/register", {Reply{200, "{\"ok\":true,\"hostId\":\"h-2\",\"hostToken\":\"" +
                                             std::string(32, 'e') + "\"}"}});
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                        std::to_string(dir.udpPort()) + "}}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"}});

    PunchHarness harness;
    check("the host agent starts without a capability",
          StartPunchHarness(dir, &harness, "nowindow", nullptr));
    harness.take();

    const sockaddr_in client = addrOf("192.168.20.16", 60420);
    const remote60::native_poc::UdpHelloPacket punch = punchPacket();
    for (int i = 0; i < 20; ++i) harness.agent.ConsumeUdpPacket(&punch, sizeof(punch), client);
    check("a host with no connection in progress answers nothing", harness.take().empty());

    // And the punches themselves must not open the window. Twenty arrived above; if receiving
    // one were enough to arm the host, the twenty-first would be answered.
    harness.agent.ConsumeUdpPacket(&punch, sizeof(punch), client);
    check("...and receiving punches does not arm it", harness.take().empty());
  }


  // ================= c1-lan-relay, end to end: which candidate the real client actually picks
  //
  // The two halves joined. The host side is the product path -- ConsumeUdpPacket deciding and
  // send_ carrying the reply out of the very socket the private candidate names. The client side
  // is the product's own DirectoryRendezvous::PunchAny, given the same shape of candidate list
  // the directory hands out: private first, then a relay that answers on the 2500 ms grace.
  //
  // What this does NOT cover, and is not claimed: the Hello/HelloAck that follows. AuthorizePeer
  // accepting a private-source Hello against a public-tuple capability is pinned above, at unit
  // level, but the session handshake itself is not driven here.
  {
    FakeDirectory dir;
    check("the fake directory starts (e2e)", dir.Start());
    const std::string token(32, 'd');
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                        std::to_string(dir.udpPort()) + "}}"}});
    dir.Script("/api/host/register", {Reply{200, "{\"ok\":true,\"hostId\":\"h-e2e\",\"hostToken\":\"" +
                                             std::string(32, 'e') + "\"}"}});
    dir.Script("/api/host/heartbeat",
               {Reply{200, heartbeatWithCapability(token, "211.218.222.1", 60420)}});

    PunchHarness harness;
    const sockaddr_in capabilityTarget = addrOf("211.218.222.1", 60420);
    check("the host is up with a capability (e2e)",
          StartPunchHarness(dir, &harness, "e2e", &capabilityTarget));
    check("...and the host socket has a port for the client to punch", harness.mediaPort != 0);

    LateRelay relay;
    check("the late-answering relay starts", relay.Start(2500));

    remote60::native_poc::DirectoryRendezvous rv;
    std::string observed, error;
    check("the client observes itself through the directory",
          rv.Observe("127.0.0.1", dir.udpPort(), "e2e-token", &observed, &error), error);

    std::vector<remote60::native_poc::RendezvousCandidate> candidates;
    candidates.push_back({"127.0.0.1", harness.mediaPort, "private"});
    candidates.push_back({"127.0.0.1", relay.port, "relay"});

    remote60::native_poc::RendezvousCandidate chosen;
    const auto began = std::chrono::steady_clock::now();
    const bool picked = rv.PunchAny(candidates, 4000, &chosen, &error);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - began).count();

    check("the client picks a candidate", picked, error);
    check("...and it is the private one, not the relay", chosen.kind == "private",
          chosen.kind + " at " + std::to_string(elapsedMs) + "ms");
    // The number that matters: the relay answers at 2500ms, so anything under that means the
    // host's own reply is what was seen. Recorded rather than asserted tightly -- the claim is
    // the ORDER, not a latency figure from a loopback test.
    check("...well before the relay's grace period", elapsedMs < 2500,
          std::to_string(elapsedMs) + "ms of the relay's 2500ms");
    rv.Close();
    harness.Stop();
    relay.Stop();
  }

  {
    // The same wiring with the host NOT armed -- no capability, so no window, so no reply. This
    // is the state the field was in, and the relay wins exactly as it did there. It is also the
    // control for the case above: without it, "private won" could be an artefact of the harness.
    FakeDirectory dir;
    check("the fake directory starts (e2e control)", dir.Start());
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                        std::to_string(dir.udpPort()) + "}}"}});
    dir.Script("/api/host/register", {Reply{200, "{\"ok\":true,\"hostId\":\"h-e2e2\",\"hostToken\":\"" +
                                             std::string(32, 'e') + "\"}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"}});

    PunchHarness harness;
    check("the host is up without a capability (e2e control)",
          StartPunchHarness(dir, &harness, "e2e-silent", nullptr));
    check("...and its socket has a port", harness.mediaPort != 0);

    LateRelay relay;
    check("the late-answering relay starts (control)", relay.Start(2500));

    remote60::native_poc::DirectoryRendezvous rv;
    std::string observed, error;
    check("the client observes itself (control)",
          rv.Observe("127.0.0.1", dir.udpPort(), "e2e-token-2", &observed, &error), error);

    std::vector<remote60::native_poc::RendezvousCandidate> candidates;
    candidates.push_back({"127.0.0.1", harness.mediaPort, "private"});
    candidates.push_back({"127.0.0.1", relay.port, "relay"});

    remote60::native_poc::RendezvousCandidate chosen;
    const auto began = std::chrono::steady_clock::now();
    const bool picked = rv.PunchAny(candidates, 5000, &chosen, &error);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - began).count();

    check("a silent host loses to the relay", picked && chosen.kind == "relay",
          chosen.kind + " at " + std::to_string(elapsedMs) + "ms");
    check("...and it takes the relay's grace period to get there", elapsedMs >= 2400,
          std::to_string(elapsedMs) + "ms");
    rv.Close();
    harness.Stop();
    relay.Stop();
  }

  // ============ what a stream of punches costs the directory, measured across several cycles
  //
  // The punch branch used to set the refresh flag as its very first act -- before the packet had
  // been checked and before the source had been looked at -- and the agent clears that flag when
  // it polls. So a client punching steadily re-armed it every cycle, and steady punching meant
  // steady outbound HTTP for as long as it went on.
  //
  // The earlier evidence for "this is bounded" was a burst of 200 causing no extra heartbeat,
  // which does not answer the question: a burst lands inside one cycle. This punches for ten
  // seconds, across many cycles, and counts what the directory was actually asked.
  {
    FakeDirectory dir;
    check("the fake directory starts (refresh rate)", dir.Start());
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                        std::to_string(dir.udpPort()) + "}}"}});
    dir.Script("/api/host/register", {Reply{200, "{\"ok\":true,\"hostId\":\"h-rate\",\"hostToken\":\"" +
                                             std::string(32, 'e') + "\"}"}});
    // Deliberately NO capability. A heartbeat that hands one over is followed by the outbound
    // punch phase, which takes five seconds -- and then the cycle length, not the cooldown, is
    // what limits the count, and the test would pass whether the cooldown existed or not. The
    // refresh path does not care about capabilities, so this measures what it says it measures.
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"}});

    PunchHarness harness;
    // Five minutes: nothing in this case is on the ordinary schedule, so every heartbeat counted
    // below is one a punch asked for.
    check("the host is up (refresh rate)",
          StartPunchHarness(dir, &harness, "rate", nullptr, 300));

    const sockaddr_in client = addrOf("192.168.20.16", 60420);
    const remote60::native_poc::UdpHelloPacket punch = punchPacket();

    const int before = dir.Count("/api/host/heartbeat");
    const auto began = std::chrono::steady_clock::now();
    int sentPunches = 0;
    while (std::chrono::steady_clock::now() - began < std::chrono::milliseconds(10000)) {
      harness.agent.ConsumeUdpPacket(&punch, sizeof(punch), client);
      ++sentPunches;
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    // A moment for the last refresh to be consumed, so the count is not short by one by luck.
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const int after = dir.Count("/api/host/heartbeat");
    const int caused = after - before;

    // Ten seconds at one per two seconds, plus the one that may be in flight when the clock
    // starts. Stated as the bound rather than an exact number: the agent polls on a 200ms slice
    // and a cycle takes time, so which side of a boundary the last one lands on is not fixed.
    // Ten seconds divided by the floor, plus two: one for a refresh already pending when the
    // clock started, one for where the last one falls. Without the floor this is about twenty --
    // a cycle here is a quarter of a second -- so the two cases are not close.
    const int bound = static_cast<int>(10000 / remote60::native_poc::kPunchRefreshCooldownMs) + 2;
    check("ten seconds of punches cost at most one directory cycle every two seconds",
          caused <= bound,
          std::to_string(caused) + " heartbeats from " + std::to_string(sentPunches) +
              " punches, bound " + std::to_string(bound));
    check("...and at least one, so the interrupt still works at all", caused >= 1,
          std::to_string(caused) + " heartbeats");
    harness.Stop();
  }

  // ==================== a punch we would never answer does not get to ask the directory either
  {
    FakeDirectory dir;
    check("the fake directory starts (malformed refresh)", dir.Start());
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                        std::to_string(dir.udpPort()) + "}}"}});
    dir.Script("/api/host/register", {Reply{200, "{\"ok\":true,\"hostId\":\"h-bad\",\"hostToken\":\"" +
                                             std::string(32, 'e') + "\"}"}});
    // No capability, so a cycle is a quarter of a second rather than five seconds. With the
    // slow cycle this case passed even when the packet checks were removed: the heartbeat a
    // bad punch caused simply had not finished before the count was read.
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"}});

    PunchHarness harness;
    check("the host is up (malformed refresh)",
          StartPunchHarness(dir, &harness, "badrefresh", nullptr, 300));

    const int before = dir.Count("/api/host/heartbeat");

    // Right magic and kind, so it reaches the branch; wrong protocol version, so nothing in it
    // should be acted on. This used to schedule an HTTP request regardless.
    remote60::native_poc::UdpHelloPacket wrongVersion = punchPacket();
    wrongVersion.version = remote60::native_poc::kUdpProtocolVersion + 7;
    const sockaddr_in client = addrOf("192.168.20.16", 60420);
    for (int i = 0; i < 20; ++i) {
      harness.agent.ConsumeUdpPacket(&wrongVersion, sizeof(wrongVersion), client);
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    // And an address we would never answer, well formed. Same rule, different reason.
    const remote60::native_poc::UdpHelloPacket good = punchPacket();
    const sockaddr_in multicast = addrOf("239.1.2.3", 60420);
    for (int i = 0; i < 10; ++i) {
      harness.agent.ConsumeUdpPacket(&good, sizeof(good), multicast);
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    const int caused = dir.Count("/api/host/heartbeat") - before;
    check("a malformed punch, or one from an address we would not answer, asks the directory nothing",
          caused == 0, std::to_string(caused) + " heartbeats");
    harness.Stop();
  }

  // ========================= the directory's own wake keeps its old behaviour, cooldown or not
  {
    // The cooldown is for punches from clients. A wake from the directory is rare, it is the
    // address the directory answers from, and it is the case the interrupt was built for -- so
    // it must still be immediate, including right after a client punch has just used the floor.
    FakeDirectory dir;
    check("the fake directory starts (wake not throttled)", dir.Start());
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                        std::to_string(dir.udpPort()) + "}}"}});
    dir.Script("/api/host/register", {Reply{200, "{\"ok\":true,\"hostId\":\"h-wake\",\"hostToken\":\"" +
                                             std::string(32, 'e') + "\"}"}});
    // No capability here either, and for the same reason as the case above: a heartbeat that
    // hands one over is followed by five seconds of outbound punching, and then every timing
    // in this case is dominated by that rather than by what it is measuring.
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"}});

    PunchHarness harness;
    check("the host is up (wake not throttled)",
          StartPunchHarness(dir, &harness, "wake", nullptr, 300));

    const remote60::native_poc::UdpHelloPacket punch = punchPacket();
    const sockaddr_in client = addrOf("192.168.20.16", 60420);

    // Spend the client floor, and then WAIT for the refresh it asked for to be consumed. The
    // first cut of this case did not wait: the flag was still pending when the wake arrived, so
    // the wake changed nothing observable and the case failed for a reason that had nothing to
    // do with throttling. `refreshWasPending=1` in the host's own log is what said so.
    const int beforeClient = dir.Count("/api/host/heartbeat");
    const auto punchedAt = std::chrono::steady_clock::now();
    harness.agent.ConsumeUdpPacket(&punch, sizeof(punch), client);
    bool clientRefreshLanded = false;
    while (std::chrono::steady_clock::now() - punchedAt < std::chrono::milliseconds(1500)) {
      if (dir.Count("/api/host/heartbeat") > beforeClient) {
        clientRefreshLanded = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    check("a client punch asks the directory once", clientRefreshLanded);

    const auto sinceClientPunch = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - punchedAt).count();
    check("...and the wake below is sent while that floor is still in force",
          sinceClientPunch < static_cast<long long>(remote60::native_poc::kPunchRefreshCooldownMs),
          std::to_string(sinceClientPunch) + "ms of " +
              std::to_string(remote60::native_poc::kPunchRefreshCooldownMs) + "ms");

    const int before = dir.Count("/api/host/heartbeat");
    sockaddr_in hostMedia{};
    hostMedia.sin_family = AF_INET;
    hostMedia.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    hostMedia.sin_port = htons(harness.mediaPort);
    check("the directory can send from the socket the host observes through",
          dir.SendFromUdp(&punch, sizeof(punch), hostMedia));

    bool sawHeartbeat = false;
    const auto wokeAt = std::chrono::steady_clock::now();
    long long wakeMs = 0;
    while (std::chrono::steady_clock::now() - wokeAt < std::chrono::milliseconds(5000)) {
      if (dir.Count("/api/host/heartbeat") > before) {
        sawHeartbeat = true;
        wakeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - wokeAt).count();
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    // No further punches are sent, so if the wake were subject to the client floor it would be
    // dropped and this heartbeat would never arrive at all -- the agent's own schedule is five
    // minutes away. The elapsed time is recorded, not asserted on.
    check("a directory wake is not made to wait for the client cooldown", sawHeartbeat,
          sawHeartbeat ? std::to_string(wakeMs) + "ms after the wake"
                       : "no heartbeat in 5s, and nothing else was going to cause one");
    harness.Stop();
  }

  // ================== the connect loops can be called off, which they could not be before
  //
  // Observe waits six times for 300ms and PunchAny punches for four seconds, and neither took a
  // stop -- so a viewer the shell had already replaced spent all of it before it could notice.
  // Nearly six seconds of a connect was uninterruptible.
  {
    std::atomic<bool> cancelled{true};

    remote60::native_poc::DirectoryRendezvous rv;
    std::string observed, error;
    const auto began = std::chrono::steady_clock::now();
    // Nothing is listening on that port. Without the stop this is six attempts of 300ms; with it
    // the first check turns it round before any of them.
    const bool observeOk = rv.Observe("127.0.0.1", 9, "cancel-token", &observed, &error,
                                      &cancelled);
    const auto observeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - began).count();
    check("an already-cancelled Observe gives up", !observeOk, error);
    check("...saying it was cancelled rather than that it timed out", error == "cancelled", error);
    check("...without spending its attempts", observeMs < 900,
          std::to_string(observeMs) + "ms of the ~1800ms it would otherwise take");
    rv.Close();
  }

  {
    // The same for the punch, which is the longer of the two.
    FakeDirectory dir;
    check("the fake directory starts (cancel)", dir.Start());
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                        std::to_string(dir.udpPort()) + "}}"}});

    remote60::native_poc::DirectoryRendezvous rv;
    std::string observed, error;
    check("the client observes itself (cancel)",
          rv.Observe("127.0.0.1", dir.udpPort(), "cancel-token-2", &observed, &error), error);

    std::vector<remote60::native_poc::RendezvousCandidate> candidates;
    // A port nothing answers on, so only the budget or the stop can end this.
    candidates.push_back({"127.0.0.1", 9, "private"});

    std::atomic<bool> cancelled{false};
    // Set from another thread while the punch is running, which is how it actually arrives.
    std::thread canceller([&cancelled] {
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      cancelled.store(true, std::memory_order_release);
    });

    remote60::native_poc::RendezvousCandidate chosen;
    const auto began = std::chrono::steady_clock::now();
    const bool picked = rv.PunchAny(candidates, 4000, &chosen, &error, &cancelled);
    const auto punchMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - began).count();
    canceller.join();

    check("a punch in flight stops when it is cancelled", !picked, error);
    check("...saying so", error == "cancelled", error);
    // It checks once per 150ms pass, so the wait after the flag is set is that, not the budget.
    check("...within a pass of being told, not at the end of the budget", punchMs < 1200,
          std::to_string(punchMs) + "ms of a 4000ms budget");
    rv.Close();
  }

  {
    // And the default is the old behaviour exactly: no stop, no early exit.
    FakeDirectory dir;
    check("the fake directory starts (no stop)", dir.Start());
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                        std::to_string(dir.udpPort()) + "}}"}});
    remote60::native_poc::DirectoryRendezvous rv;
    std::string observed, error;
    check("the client observes itself (no stop)",
          rv.Observe("127.0.0.1", dir.udpPort(), "no-stop-token", &observed, &error), error);
    std::vector<remote60::native_poc::RendezvousCandidate> candidates;
    candidates.push_back({"127.0.0.1", 9, "private"});
    remote60::native_poc::RendezvousCandidate chosen;
    const auto began = std::chrono::steady_clock::now();
    rv.PunchAny(candidates, 700, &chosen, &error);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - began).count();
    check("a caller that passes no stop still runs its whole budget", ms >= 650,
          std::to_string(ms) + "ms of 700ms");
    rv.Close();
  }

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

  // ---------------------------------------- an account that is not active
  //
  // The directory answers a host of a stopped account 401 with code account_inactive, and a
  // registration by an account waiting for approval 403 with a sentence. Neither is fixed by
  // asking again every heartbeat, and the first must not cost the token: re-enabled, the account
  // takes this same token back with nothing to re-register.
  {
    FakeDirectory dir;
    dir.Start();
    dir.Script("/api/host/register",
               {Reply{200, "{\"hostId\":\"h1\",\"hostToken\":\"" + std::string(32, 'd') + "\"}"}});
    dir.Script("/api/host/heartbeat",
               {Reply{401, "{\"error\":\"unknown host token\",\"code\":\"account_inactive\"}"},
                Reply{200, "{\"ok\":true}"}});
    const std::string status = RunHost(dir, "inactive", 3, 1, false, dir.udpPort());
    check("[inactive] a 401 for an account that is not active keeps the token: no registration",
          dir.Count("/api/host/register") == 1, std::to_string(dir.Count("/api/host/register")));
    check("[inactive] ...and the host is not asked again every heartbeat (it waits cycles out)",
          dir.Count("/api/host/heartbeat") <= 2, std::to_string(dir.Count("/api/host/heartbeat")));
    check("[inactive] re-enabled, the same token is taken back: online, nothing re-registered",
          status.rfind("online", 0) == 0, status);
    dir.Stop();
  }
  {
    // Still stopped: what the host says meanwhile.
    FakeDirectory dir;
    dir.Start();
    dir.Script("/api/host/register",
               {Reply{200, "{\"hostId\":\"h1\",\"hostToken\":\"" + std::string(32, 'e') + "\"}"}});
    const Reply inactive{401, "{\"error\":\"unknown host token\",\"code\":\"account_inactive\"}"};
    dir.Script("/api/host/heartbeat", {inactive, inactive, inactive, inactive});
    const std::string status = RunHost(dir, "inactive-stays", 1, 1, false, dir.udpPort());
    check("[inactive] ...while stopped it says so in the words the window reads as SIGN IN AGAIN",
          status.find("token rejected") != std::string::npos &&
              status.find("not active") != std::string::npos,
          status);
    check("[inactive] ...and still has not registered again", dir.Count("/api/host/register") == 1);
    dir.Stop();
  }
  {
    FakeDirectory dir;
    dir.Start();
    dir.Script("/api/host/register",
               {Reply{403, "{\"error\":\"fixture: waiting for approval\",\"code\":\"pending\"}"},
                Reply{403, "{\"error\":\"fixture: waiting for approval\",\"code\":\"pending\"}"},
                Reply{403, "{\"error\":\"fixture: waiting for approval\",\"code\":\"pending\"}"},
                Reply{403, "{\"error\":\"fixture: waiting for approval\",\"code\":\"pending\"}"}});
    const std::string status = RunHost(dir, "register-403", 0, 4, false, dir.udpPort());
    check("[403] a refused registration is not retried every heartbeat",
          dir.Count("/api/host/register") <= 2, std::to_string(dir.Count("/api/host/register")));
    check("[403] ...the directory's own sentence is what the host says",
          status.find("registration refused: fixture: waiting for approval") != std::string::npos,
          status);
    check("[403] ...and nothing was heartbeated", dir.Count("/api/host/heartbeat") == 0);
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

  // ------------------------------------ asking must not cost anything where the old rule worked
  //
  // The health request is new, and new questions break old answers. On http the observe port has
  // always been derivable (httpPort + 1), and every currently deployed directory is laid out that
  // way -- so a health route that 500s, or that answers without saying anything, must leave that
  // path exactly as it was. If this pair ever goes red, the fix has made the common case worse
  // than the bug it repaired.
  {
    const struct {
      const char* label;
      const char* what;
      Reply health;
    } rows[] = {
        {"legacy-health-500", "a health route that fails",
         Reply{500, "{\"error\":\"no health route here\"}"}},
        {"legacy-health-silent", "a health route that says nothing",
         Reply{200, "{\"ok\":true}"}},
    };
    for (const auto& row : rows) {
      FakeDirectory dir;
      if (!dir.StartLegacyAdjacent()) {
        // Never silently skipped: an unavailable adjacent port would otherwise turn this into a
        // case that passes by not running.
        check((std::string(row.what) + ": could not lay out the fixture").c_str(), false,
              "the adjacent udp port was not free after 32 tries");
        continue;
      }
      dir.Script("/healthz", {row.health});
      dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true}"}});
      dir.Script("/api/host/register", {Reply{500, "{\"error\":\"must not be called\"}"}});

      const std::string status = RunHost(dir, row.label, 1, 0, true);
      check((std::string(row.what) + " leaves the http fallback working").c_str(),
            dir.Count("/api/host/heartbeat") >= 1,
            std::to_string(dir.Count("/api/host/heartbeat")));
      check("...and the observation really went to httpPort+1", dir.ObserveProbes() >= 1,
            std::to_string(dir.ObserveProbes()));
      check("...without registering again", dir.Count("/api/host/register") == 0,
            std::to_string(dir.Count("/api/host/register")));
      check("...and the address is published", status.find("public=") != std::string::npos,
            status);
      dir.Stop();
    }
  }

  WSACleanup();
  if (gFailures == 0) {
    std::printf("\nall retry checks passed  (%d checks, 0 failed)\n", gChecks);
  } else {
    std::printf("\n%d FAILED  (%d checks, %d failed)\n", gFailures, gChecks, gFailures);
  }
  return gFailures == 0 ? 0 : 1;
}

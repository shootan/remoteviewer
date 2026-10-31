// prelogin r21 (Codex codex_prelogin_r20_integ.md, E2): RequestWindowList as the integration left it -- prelogin's
// host-role gate first, then keepalive's park-checked enqueue -- run through the PRODUCT session
// (ClientSessionController and its control loop, native_video_client_session.cpp) against a fake control host
// on loopback. No UI, no SendInput, no desktop, no real host.
//
// What is the product here: the controller, its control loop, scheduler, role gate, park fence and TCP control
// client -- all linked from the product sources. What is the test: the fake host below (it answers Ping, the
// window list and the monitor list, and records every message it receives, in order, with the time), and the
// calls the app makes (RequestWindowList / RequestMonitorList / BeginPark / ResumeFromPark).
//
// Scenarios (each its own connection):
//   S1 deferred   the host holds every Pong: a list asked for before the first Pong is not on the wire; the
//                 first Pong (an ordinary host) sends it.
//   S2 prelogin   the host's Pong carries kCaptureFlagPreloginV1: no window-list / monitor-list request ever
//                 reaches the wire, before or after the first Pong.
//   S3 park       an ordinary host that honours the pause (thumbnail flag) and lists monitors: while parked a
//                 list / monitor request is refused and none reaches the wire between the pause and the
//                 resume; after ResumeFromPark a new request does.
//   S4 race       requests hammered from another thread while BeginPark runs, repeatedly: none reaches the
//                 wire between the pause and the resume, and every round ends (no hang).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "native_socket.hpp"
#include "native_video_client_session.hpp"
#include "poc_protocol.hpp"

namespace n = remote60::native_poc;
using n::MessageHeader;
using n::MessageType;
using n::SocketHandle;
using n::kInvalidSocket;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}
bool wait_until(const std::function<bool()>& done, int ms) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return done();
}
using Clock = std::chrono::steady_clock;

struct WireEntry {
  uint16_t type;
  uint32_t flags;  // StreamState flags (bit0 active); 0 otherwise
  Clock::time_point at;
};

// The fake control host: UDP HelloAck, then a TCP control stream that answers Ping / WindowListRequest /
// MonitorListRequest and records everything it is sent.
struct FakeHost {
  std::atomic<uint32_t> pongFlags{0};
  std::atomic<bool> holdPongs{false};
  uint32_t listFlags = 0;
  int controlPort = 0, videoPort = 0;
  std::atomic<bool> stop{false};
  SocketHandle tcpListen = kInvalidSocket, accepted = kInvalidSocket, udp = kInvalidSocket;
  std::thread tcpThread, udpThread;
  mutable std::mutex mu;
  std::vector<WireEntry> wire;

  bool Start() {
    std::string err;
    if (!n::initialize_sockets(&err)) return false;
    tcpListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(tcpListen, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(tcpListen, 1) != 0) return false;
    if (bind(udp, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) return false;
    sockaddr_in b{};
    int bl = sizeof(b);
    getsockname(tcpListen, reinterpret_cast<sockaddr*>(&b), &bl);
    controlPort = ntohs(b.sin_port);
    bl = sizeof(b);
    getsockname(udp, reinterpret_cast<sockaddr*>(&b), &bl);
    videoPort = ntohs(b.sin_port);
    udpThread = std::thread([this] { RunUdp(); });
    tcpThread = std::thread([this] { RunTcp(); });
    return true;
  }
  void Stop() {
    stop = true;
    holdPongs = false;
    n::shutdown_socket(&accepted);
    n::shutdown_socket(&tcpListen);
    n::shutdown_socket(&udp);
    if (tcpThread.joinable()) tcpThread.join();
    if (udpThread.joinable()) udpThread.join();
  }
  ~FakeHost() { Stop(); }

  void RunUdp() {
    sockaddr_in peer{};
    int pl = sizeof(peer);
    n::UdpHelloPacket hello{};
    const int got = recvfrom(udp, reinterpret_cast<char*>(&hello), sizeof(hello), 0, reinterpret_cast<sockaddr*>(&peer), &pl);
    if (got >= static_cast<int>(sizeof(hello)) && hello.magic == n::kMagic &&
        hello.kind == static_cast<uint16_t>(n::UdpPacketKind::Hello)) {
      n::UdpHelloPacket ack{};
      ack.kind = static_cast<uint16_t>(n::UdpPacketKind::HelloAck);
      sendto(udp, reinterpret_cast<const char*>(&ack), sizeof(ack), 0, reinterpret_cast<sockaddr*>(&peer), pl);
    }
  }

  void Record(uint16_t type, uint32_t flags) {
    std::lock_guard<std::mutex> l(mu);
    wire.push_back(WireEntry{type, flags, Clock::now()});
  }

  void RunTcp() {
    sockaddr_in peer{};
    int pl = sizeof(peer);
    accepted = accept(tcpListen, reinterpret_cast<sockaddr*>(&peer), &pl);
    if (accepted == kInvalidSocket) return;
    n::set_recv_timeout(accepted, 500);
    n::set_tcp_nodelay(accepted);
    std::vector<uint8_t> body;
    while (!stop.load()) {
      MessageHeader h{};
      if (!n::recv_all(accepted, &h, sizeof(h))) {
        if (stop.load()) break;
        continue;  // a recv timeout with nothing sent -- keep listening
      }
      if (h.magic != n::kMagic || h.size < sizeof(h)) break;
      body.assign(h.size - sizeof(h), 0);
      if (!body.empty() && !n::recv_all(accepted, body.data(), body.size())) break;
      const auto type = static_cast<MessageType>(h.type);
      uint32_t flags = 0;
      if (type == MessageType::ControlStreamState && h.size == sizeof(n::ControlStreamStateMessage)) {
        n::ControlStreamStateMessage m{};
        std::memcpy(&m, &h, sizeof(h));
        std::memcpy(reinterpret_cast<uint8_t*>(&m) + sizeof(h), body.data(), body.size());
        flags = m.flags;
      }
      Record(h.type, flags);
      if (type == MessageType::ControlPing && h.size == sizeof(n::ControlPingMessage)) {
        n::ControlPingMessage ping{};
        std::memcpy(&ping, &h, sizeof(h));
        std::memcpy(reinterpret_cast<uint8_t*>(&ping) + sizeof(h), body.data(), body.size());
        while (holdPongs.load() && !stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        n::ControlPongMessage pong{};
        pong.header.magic = n::kMagic;
        pong.header.type = static_cast<uint16_t>(MessageType::ControlPong);
        pong.header.size = static_cast<uint16_t>(sizeof(pong));
        pong.seq = ping.seq;
        pong.clientSendQpcUs = ping.clientSendQpcUs;
        pong.captureTargetFlags = pongFlags.load();
        if (!n::send_all(accepted, &pong, sizeof(pong))) break;
      } else if (type == MessageType::ControlWindowListRequest &&
                 h.size == sizeof(n::ControlWindowListRequestMessage)) {
        n::ControlWindowListRequestMessage req{};
        std::memcpy(&req, &h, sizeof(h));
        std::memcpy(reinterpret_cast<uint8_t*>(&req) + sizeof(h), body.data(), body.size());
        n::ControlWindowListMessage list{};
        list.header.magic = n::kMagic;
        list.header.type = static_cast<uint16_t>(MessageType::ControlWindowList);
        list.header.size = static_cast<uint16_t>(sizeof(list));
        list.seq = req.seq;
        list.flags = listFlags;
        list.itemCount = 2;
        list.items[0].id = 1001;
        std::snprintf(list.items[0].title, sizeof(list.items[0].title), "%s", "Editor");
        list.items[1].id = 1002;
        std::snprintf(list.items[1].title, sizeof(list.items[1].title), "%s", "Browser");
        if (!n::send_all(accepted, &list, sizeof(list))) break;
      } else if (type == MessageType::ControlMonitorListRequest &&
                 h.size == sizeof(n::ControlMonitorListRequestMessage)) {
        n::ControlMonitorListRequestMessage req{};
        std::memcpy(&req, &h, sizeof(h));
        std::memcpy(reinterpret_cast<uint8_t*>(&req) + sizeof(h), body.data(), body.size());
        n::ControlMonitorListMessage ml{};
        ml.header.magic = n::kMagic;
        ml.header.type = static_cast<uint16_t>(MessageType::ControlMonitorList);
        ml.header.size = static_cast<uint16_t>(sizeof(ml));
        ml.seq = req.seq;
        ml.itemCount = 1;
        ml.items[0].id = 0;
        ml.items[0].width = 1920;
        ml.items[0].height = 1080;
        if (!n::send_all(accepted, &ml, sizeof(ml))) break;
      } else if (type == MessageType::ControlWindowThumbnailRequest &&
                 h.size == sizeof(n::ControlWindowThumbnailRequestMessage)) {
        // The session fetches previews on its own once the host has the thumbnail flag: answered "none".
        n::ControlWindowThumbnailRequestMessage req{};
        std::memcpy(&req, &h, sizeof(h));
        std::memcpy(reinterpret_cast<uint8_t*>(&req) + sizeof(h), body.data(), body.size());
        n::ControlWindowThumbnailHeader th{};
        th.header.magic = n::kMagic;
        th.header.type = static_cast<uint16_t>(MessageType::ControlWindowThumbnail);
        th.header.size = static_cast<uint16_t>(sizeof(th));
        th.seq = req.seq;
        th.windowId = req.windowId;
        if (!n::send_all(accepted, &th, sizeof(th))) break;
      }
    }
  }

  size_t Count(MessageType t) const {
    std::lock_guard<std::mutex> l(mu);
    size_t c = 0;
    for (const auto& e : wire) c += e.type == static_cast<uint16_t>(t) ? 1 : 0;
    return c;
  }
  std::string Types() const {
    std::lock_guard<std::mutex> l(mu);
    std::string out;
    for (const auto& e : wire) out += std::to_string(e.type) + (e.type == static_cast<uint16_t>(MessageType::ControlStreamState) ? (e.flags & 1u ? "+" : "-") : "") + " ";
    return out;
  }
  size_t Lists() const { return Count(MessageType::ControlWindowListRequest) + Count(MessageType::ControlMonitorListRequest); }
  // List / monitor requests received after the LAST pause (StreamState without bit0) that came before `until`,
  // and before `until`.
  size_t ListsWhileParked(Clock::time_point until, bool* sawPause) const {
    std::lock_guard<std::mutex> l(mu);
    size_t pauseAt = wire.size();
    for (size_t i = 0; i < wire.size(); ++i) {
      if (wire[i].at > until) break;
      if (wire[i].type == static_cast<uint16_t>(MessageType::ControlStreamState) && (wire[i].flags & 1u) == 0) pauseAt = i;
    }
    *sawPause = pauseAt < wire.size();
    if (!*sawPause) return 0;
    size_t c = 0;
    for (size_t i = pauseAt + 1; i < wire.size() && wire[i].at <= until; ++i) {
      c += (wire[i].type == static_cast<uint16_t>(MessageType::ControlWindowListRequest) ||
            wire[i].type == static_cast<uint16_t>(MessageType::ControlMonitorListRequest))
               ? 1
               : 0;
    }
    return c;
  }
};

bool connect_to(n::ClientSessionController& c, const FakeHost& h) {
  n::ClientSessionConnectArgs a{};
  a.host = "127.0.0.1";
  a.videoPort = h.videoPort;
  a.controlPort = h.controlPort;
  a.controlIntervalMs = 50;
  return c.Connect(a) && wait_until([&] {
           const auto s = c.Snapshot();
           return s.state == n::ClientSessionState::Connected && s.controlLoopActive;
         }, 5000);
}

constexpr uint32_t kOrdinaryListFlags = n::kControlWindowListFlagThumbnails | n::kControlWindowListFlagMonitors |
                                        n::kControlWindowListFlagMonitorSelectTransactionV1;

void s1_deferred() {
  std::printf("\n--- S1: a list asked for before the first Pong waits for it ---\n");
  FakeHost h;
  h.listFlags = kOrdinaryListFlags;
  h.holdPongs = true;
  if (!h.Start()) return check("S1: fake host starts", false);
  n::ClientSessionController c;
  check("S1: the session connects (Pongs held)", connect_to(c, h));
  const bool asked = c.RequestWindowList();  // the app's call right after connecting
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  const size_t beforePong = h.Count(MessageType::ControlWindowListRequest);
  const bool pingSeen = h.Count(MessageType::ControlPing) > 0;
  const std::string status = c.WindowPanelSnapshotCopy().status;
  check("S1: before the first Pong no window-list request is on the wire (the request is held, not refused)",
        asked && pingSeen && beforePong == 0, "listReq=" + std::to_string(beforePong) + " status=" + status);
  h.holdPongs = false;
  const bool sent = wait_until([&] { return h.Count(MessageType::ControlWindowListRequest) >= 1; }, 3000);
  const bool applied = wait_until([&] { return c.Snapshot().latestWindowListCount == 2; }, 3000);
  check("S1: the first Pong (an ordinary host) sends the held request and the list arrives", sent && applied,
        "listReq=" + std::to_string(h.Count(MessageType::ControlWindowListRequest)));
  c.Disconnect();
}

void s2_prelogin() {
  std::printf("\n--- S2: a host at the logon screen gets no list / monitor request ---\n");
  FakeHost h;
  h.listFlags = kOrdinaryListFlags;
  h.pongFlags = n::kCaptureFlagPreloginV1;
  h.holdPongs = true;
  if (!h.Start()) return check("S2: fake host starts", false);
  n::ClientSessionController c;
  check("S2: the session connects", connect_to(c, h));
  (void)c.RequestWindowList();  // before the first Pong: held ...
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  h.holdPongs = false;  // ... and the first Pong says "logon screen"
  const bool firstPong = wait_until([&] { return (c.HostCaptureFlags() & n::kCaptureFlagPreloginV1) != 0; }, 3000);
  const bool after = c.RequestWindowList();
  const bool mon = c.RequestMonitorList();
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  const std::string status = c.WindowPanelSnapshotCopy().status;
  // (RequestMonitorList has no role gate of its own: it is only queued, and the monitor request goes out only
  // to a host whose window list said it lists monitors -- which a logon-screen host never sent. The wire says.)
  check("S2: list / monitor wire 0 (before and after the first Pong); the app is told 'not here'",
        firstPong && h.Lists() == 0 && !after && status.find("logon screen") != std::string::npos,
        "lists=" + std::to_string(h.Lists()) + " pings=" + std::to_string(h.Count(MessageType::ControlPing)) +
            " monitorQueued=" + std::to_string(mon ? 1 : 0) + " status=" + status);
  c.Disconnect();
}

bool park_now(n::ClientSessionController& c) {
  if (!c.BeginPark()) return false;
  return wait_until([&] { return c.ParkPhase() == n::ClientParkPhase::Parked; }, 3000);
}

void s3_park() {
  std::printf("\n--- S3: parked -> no list / monitor request on the wire; resumed -> they go again ---\n");
  FakeHost h;
  h.listFlags = kOrdinaryListFlags;
  if (!h.Start()) return check("S3: fake host starts", false);
  n::ClientSessionController c;
  check("S3: the session connects", connect_to(c, h));
  (void)c.RequestWindowList();
  const bool ready = wait_until([&] {
    return c.Snapshot().latestWindowListCount == 2 && c.HostHonorsStreamPause() &&
           h.Count(MessageType::ControlMonitorListRequest) >= 1;
  }, 3000);
  check("S3: an ordinary host: list, pause support and monitor list established", ready,
        "lists=" + std::to_string(h.Lists()));
  const bool parked = park_now(c);
  const bool w = c.RequestWindowList();
  const bool m = c.RequestMonitorList();
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  bool sawPause = false;
  const size_t during = h.ListsWhileParked(Clock::now(), &sawPause);
  if (!parked) std::printf("INFO  S3 wire types: %s\n", h.Types().c_str());
  check("S3: parked: the requests are refused and none reaches the wire after the pause", parked && sawPause && !w &&
        !m && during == 0, "during=" + std::to_string(during) + " phase=" + std::to_string(static_cast<int>(c.ParkPhase())));
  const size_t before = h.Count(MessageType::ControlWindowListRequest);
  const bool resumed = c.ResumeFromPark();
  (void)c.RequestStreamActive(true);  // what the app does on resume
  const bool again = c.RequestWindowList();
  const bool arrived = wait_until([&] { return h.Count(MessageType::ControlWindowListRequest) > before; }, 3000);
  check("S3: after ResumeFromPark a new window-list request reaches the host", resumed && again && arrived);
  c.Disconnect();
}

void s4_race() {
  std::printf("\n--- S4: requests racing BeginPark ---\n");
  FakeHost h;
  h.listFlags = kOrdinaryListFlags;
  if (!h.Start()) return check("S4: fake host starts", false);
  n::ClientSessionController c;
  check("S4: the session connects", connect_to(c, h));
  (void)c.RequestWindowList();
  wait_until([&] { return c.HostHonorsStreamPause(); }, 3000);
  constexpr int kRounds = 25;
  int parkedRounds = 0, violations = 0, refusedWhileParked = 0;
  const auto t0 = Clock::now();
  for (int round = 0; round < kRounds; ++round) {
    std::atomic<bool> go{true};
    std::thread hammer([&] {
      while (go.load()) {
        (void)c.RequestWindowList();
        (void)c.RequestMonitorList();
        std::this_thread::yield();
      }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(round % 5));
    const bool parked = park_now(c);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));  // the hammer keeps asking while parked
    refusedWhileParked += (parked && !c.RequestWindowList()) ? 1 : 0;
    go = false;
    hammer.join();
    bool sawPause = false;
    const size_t during = h.ListsWhileParked(Clock::now(), &sawPause);
    if (parked && sawPause) {
      ++parkedRounds;
      if (during != 0) ++violations;
    }
    if (!c.ResumeFromPark()) break;
    (void)c.RequestStreamActive(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
  check("S4: every round parked with the requests racing, none reached the wire while parked, all ended",
        parkedRounds == kRounds && violations == 0 && refusedWhileParked == kRounds,
        "parked " + std::to_string(parkedRounds) + "/" + std::to_string(kRounds) + ", violations " +
            std::to_string(violations) + ", " + std::to_string(ms) + " ms, lists " + std::to_string(h.Lists()));
  // Positive control: the same hammer without a park does put requests on the wire (so 0 above is the park).
  {
    const size_t before = h.Lists();
    std::atomic<bool> go{true};
    std::thread hammer([&] {
      while (go.load()) {
        (void)c.RequestWindowList();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    });
    const bool flowed = wait_until([&] { return h.Lists() >= before + 3; }, 3000);
    go = false;
    hammer.join();
    check("S4: positive control -- unparked, the same requests reach the wire", flowed,
          "lists " + std::to_string(before) + " -> " + std::to_string(h.Lists()));
  }
  const auto s = c.Snapshot();
  check("S4: the session is still connected after the rounds", s.state == n::ClientSessionState::Connected);
  c.Disconnect();
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("RequestWindowList: role gate + park through the product ClientSessionController (prelogin r21 E2)\n");
  s1_deferred();
  s2_prelogin();
  s3_park();
  s4_race();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

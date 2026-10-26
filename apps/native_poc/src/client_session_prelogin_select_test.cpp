// apk-0232 r1 (Codex codex_rel_0163_e1_decision.md, "shared session / APK"): a DESKTOP pick at a host known to be
// at the logon screen (kCaptureFlagPreloginV1), through the PRODUCT shared session (ClientSessionController, its
// control loop, scheduler, role gate and window panel -- native_video_client_session.cpp, linked from the product
// sources) and the selection gate the APK's decoder sink uses (selection_ack_gate.hpp), against a fake control host
// on loopback. Before this, the session queued a WindowSelect(0) that the logon-screen host does not take, dropped
// it unsent at the role check, and the APK waited in REQUESTING for an answer that never came (6 s, back to the list).
//
// What is the product: the controller and everything it links; SelectionAckGate. What is the test: the fake host
// (answers Ping, WindowSelect for an ordinary host; records what it is sent), and GateSink -- the Android decoder
// sink's use of the gate reduced to what a selection needs (Admit; an admitted key frame is handed to the decoder:
// KeyDelivered; an admitted frame produces output: the selection's first output = ready), driven with frames by the
// test instead of the UDP receive loop. The panel status is read with the APK's own rule (SelectionStatusPolicy.kt:
// "window_selected" = ACKED, "window_select_failed" = FAILED).
//
// Scenarios (each its own connection):
//   P1 local      prelogin host known, desktop pick: nothing selected on the wire, an IDR asked for; a delta before
//                 the IDR binds nothing; the first IDR binds the selection's generation; the panel says selected with
//                 that generation; the first output is the selection's.
//   P2 deferred   picked before the first Pong (Pongs held), then the Pong says prelogin: the SAME op turns local (not
//                 dropped, not sent).
//   P3 window     a window pick at a known prelogin host is refused at once (nothing queued); one queued before the
//                 Pong is refused after it (panel FAILED, the gate opens nothing).
//   P4 ordinary   an ordinary host, and one with UAC / LockCred flags but not PreloginV1: the ack path as before
//                 (WindowSelect on the wire, the host's answer opens the gate), never local.
//   P5 stale ops  local A then B before any IDR: the IDR binds B only; A never completes. A local op abandoned
//                 (AbortWindowSelection) completes nothing afterwards.
//   P6 again      a second desktop pick on the same connection is a new local op, bound by a NEW IDR.
//   P7 reconnect  a new connection to a prelogin host: a pick works again (nothing carried from the old one).
// Tags: network(loopback), pure-logic.

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
#include "selection_ack_gate.hpp"

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

// The APK's reading of the panel status (SelectionStatusPolicy.kt).
enum class Verdict { None, Acked, Failed };
Verdict verdict(const std::string& status) {
  if (status.rfind("window_select_failed", 0) == 0) return Verdict::Failed;
  if (status.rfind("window_selected", 0) == 0) return Verdict::Acked;
  return Verdict::None;
}

// The fake control host: UDP HelloAck, then TCP control answering Ping and (an ordinary host) WindowSelect.
struct FakeHost {
  std::atomic<uint32_t> pongFlags{0};
  std::atomic<bool> holdPongs{false};
  std::atomic<uint64_t> nextGeneration{7001};
  int controlPort = 0, videoPort = 0;
  std::atomic<bool> stop{false};
  SocketHandle tcpListen = kInvalidSocket, accepted = kInvalidSocket, udp = kInvalidSocket;
  std::thread tcpThread, udpThread;
  mutable std::mutex mu;
  std::vector<uint16_t> wire;
  std::vector<uint64_t> selectedIds;

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
        continue;
      }
      if (h.magic != n::kMagic || h.size < sizeof(h)) break;
      body.assign(h.size - sizeof(h), 0);
      if (!body.empty() && !n::recv_all(accepted, body.data(), body.size())) break;
      const auto type = static_cast<MessageType>(h.type);
      {
        std::lock_guard<std::mutex> l(mu);
        wire.push_back(h.type);
      }
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
        // An ordinary host's list (the session asks for it at the first Pong; the control loop waits for it).
        n::ControlWindowListRequestMessage req{};
        std::memcpy(&req, &h, sizeof(h));
        std::memcpy(reinterpret_cast<uint8_t*>(&req) + sizeof(h), body.data(), body.size());
        n::ControlWindowListMessage list{};
        list.header.magic = n::kMagic;
        list.header.type = static_cast<uint16_t>(MessageType::ControlWindowList);
        list.header.size = static_cast<uint16_t>(sizeof(list));
        list.seq = req.seq;
        list.itemCount = 1;
        list.items[0].id = 1001;
        std::snprintf(list.items[0].title, sizeof(list.items[0].title), "%s", "Editor");
        if (!n::send_all(accepted, &list, sizeof(list))) break;
      } else if (type == MessageType::ControlWindowSelect && h.size == sizeof(n::ControlWindowSelectMessage)) {
        // An ordinary host's answer: ok, a new stream generation. (A logon-screen host would close the link.)
        n::ControlWindowSelectMessage req{};
        std::memcpy(&req, &h, sizeof(h));
        std::memcpy(reinterpret_cast<uint8_t*>(&req) + sizeof(h), body.data(), body.size());
        {
          std::lock_guard<std::mutex> l(mu);
          selectedIds.push_back(req.windowId);
        }
        n::ControlWindowSelectedMessage ans{};
        ans.header.magic = n::kMagic;
        ans.header.type = static_cast<uint16_t>(MessageType::ControlWindowSelected);
        ans.header.size = static_cast<uint16_t>(sizeof(ans));
        ans.seq = req.seq;
        ans.flags = 0x1u;
        ans.windowId = req.windowId;
        ans.streamGeneration = nextGeneration.fetch_add(1);
        std::snprintf(ans.title, sizeof(ans.title), "%s", "desktop");
        if (!n::send_all(accepted, &ans, sizeof(ans))) break;
      }
    }
  }

  size_t Count(MessageType t) const {
    std::lock_guard<std::mutex> l(mu);
    size_t c = 0;
    for (uint16_t e : wire) c += e == static_cast<uint16_t>(t) ? 1 : 0;
    return c;
  }
};

// The Android decoder sink's selection handling, reduced: the same gate object and the same calls in the same
// places (android_video_decoder.cpp: Prepare at nativePrepareVideoSwitch, Admit per frame, KeyDelivered when an
// IDR is queued into the codec, the selection's first output at hand-over, Reset at nativeAbortVideoSwitch).
class GateSink : public n::ClientEncodedFrameSink {
 public:
  void OnEncodedH264Frame(n::UdpH264AssembledFrame&& /* frame */) override {}
  void OnVideoStreamReset() override {}
  uint64_t CurrentSelectionTag() override {
    std::lock_guard<std::mutex> l(mu_);
    return gate_.pending();
  }
  uint64_t KeyframeOwedFor() override {
    std::lock_guard<std::mutex> l(mu_);
    return gate_.OwedToken();
  }
  void OnWindowSelectionControlResultFor(const n::ControlWindowSelectedMessage& msg, uint64_t requestTag) override {
    std::lock_guard<std::mutex> l(mu_);
    gate_.OnAck((msg.flags & 0x1u) != 0, msg.streamGeneration, requestTag);
  }
  bool BeginLocalSelectionFor(uint64_t tag) override {
    std::lock_guard<std::mutex> l(mu_);
    return gate_.BeginLocal(tag);
  }
  uint64_t LocalSelectionBoundFor(uint64_t tag) override {
    std::lock_guard<std::mutex> l(mu_);
    return gate_.LocalBoundFor(tag);
  }
  void RefuseSelectionFor(uint64_t tag) override {
    std::lock_guard<std::mutex> l(mu_);
    gate_.Refuse(tag);
  }
  // r3 (N1): as android_video_decoder.cpp -- judged and recorded under the state Prepare/Abort take.
  LocalCommit CommitLocalSelectionFor(uint64_t tag, const std::function<void(uint64_t)>& commit) override {
    std::lock_guard<std::mutex> l(mu_);
    if (tag == 0 || gate_.pending() != tag) return LocalCommit::Abandoned;
    const uint64_t generation = gate_.LocalBoundFor(tag);
    if (generation == 0) return LocalCommit::Unbound;
    commit(generation);
    return LocalCommit::Committed;
  }

  void Prepare(uint64_t selection) {
    std::lock_guard<std::mutex> l(mu_);
    gate_.Prepare(selection);
    ready_ = 0;
    outputs_ = 0;
  }
  void Abort() {
    std::lock_guard<std::mutex> l(mu_);
    gate_.Reset();
    ready_ = 0;
  }
  // One frame from the host. Returns whether it reached the decoder.
  // decoderAble=false: the decoder cannot take it (no surface yet, codec not up): an IDR is lost (KeyLost).
  bool Feed(uint64_t generation, bool key, bool decoderAble = true) {
    std::lock_guard<std::mutex> l(mu_);
    if (!gate_.Admit(generation, key)) return false;
    if (!decoderAble) {
      if (key) gate_.KeyLost(generation);
      return true;
    }
    if (key) gate_.KeyDelivered(generation);
    // An admitted frame produces output (a delta needs a delivered IDR before it; the decoder emulation is that).
    if (!key && !keyDelivered_) return true;
    keyDelivered_ = keyDelivered_ || key;
    ++outputs_;
    if (gate_.pending() != 0 && ready_ != gate_.pending()) ready_ = gate_.pending();
    return true;
  }
  void NewDecoder() {
    std::lock_guard<std::mutex> l(mu_);
    keyDelivered_ = false;
  }
  uint64_t ready() {
    std::lock_guard<std::mutex> l(mu_);
    return ready_;
  }
  uint64_t expected() {
    std::lock_guard<std::mutex> l(mu_);
    return gate_.expected();
  }

 private:
  std::mutex mu_;
  n::SelectionAckGate gate_;
  uint64_t ready_ = 0;
  uint64_t outputs_ = 0;
  bool keyDelivered_ = false;
};

bool connect_to(n::ClientSessionController& c, const FakeHost& h, GateSink* sink) {
  n::ClientSessionConnectArgs a{};
  a.host = "127.0.0.1";
  a.videoPort = h.videoPort;
  a.controlPort = h.controlPort;
  a.controlIntervalMs = 50;
  a.encodedFrameSink = sink;
  return c.Connect(a) && wait_until([&] {
           const auto s = c.Snapshot();
           return s.state == n::ClientSessionState::Connected && s.controlLoopActive;
         }, 5000);
}
bool role_known(n::ClientSessionController& c, FakeHost& h) {
  return wait_until([&] { return h.Count(MessageType::ControlPing) >= 2; }, 3000);
}
std::string status_of(n::ClientSessionController& c) { return c.WindowPanelSnapshotCopy().status; }

constexpr uint32_t kPrelogin = n::kCaptureFlagPreloginV1;

// P1 + P5 + P6: local selections at a known prelogin host.
void p1_local() {
  std::printf("\n--- P1/P5/P6: desktop picks at a host known to be at the logon screen ---\n");
  FakeHost h;
  h.pongFlags = kPrelogin;
  if (!h.Start()) return check("P1: fake host starts", false);
  n::ClientSessionController c;
  GateSink sink;
  check("P1: the session connects", connect_to(c, h, &sink));
  role_known(c, h);
  sink.Prepare(11);
  const bool asked = c.RequestDesktopMode();
  check("P1: the desktop pick is accepted", asked);
  const bool keyAsked = wait_until([&] { return h.Count(MessageType::ControlRequestKeyFrame) >= 1; }, 2500);
  check("P1: an IDR is asked for (allowed at the logon screen); no WindowSelect on the wire",
        keyAsked && h.Count(MessageType::ControlWindowSelect) == 0, "status=" + status_of(c));
  check("P1: not selected yet (the APK stays REQUESTING): no IDR has come", verdict(status_of(c)) == Verdict::None,
        status_of(c));
  check("P1: a delta before the IDR does not reach the decoder and binds nothing", !sink.Feed(500, false) && sink.ready() == 0);
  const size_t keysBefore = h.Count(MessageType::ControlRequestKeyFrame);
  sink.Feed(500, true, /*decoderAble=*/false);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  check("P1: an IDR the decoder could not take (no surface) binds nothing: not selected, no output",
        sink.LocalSelectionBoundFor(11) == 0 && verdict(status_of(c)) == Verdict::None && sink.ready() == 0, status_of(c));
  check("P1: ...and an IDR is asked for again", wait_until([&] { return h.Count(MessageType::ControlRequestKeyFrame) > keysBefore; }, 2500));
  check("P1: the first IDR reaches the decoder", sink.Feed(500, true));
  const bool acked = wait_until([&] { return verdict(status_of(c)) == Verdict::Acked; }, 2000);
  const auto snap = c.WindowPanelSnapshotCopy();
  check("P1: the panel says selected (APK: ACKED) with the generation that IDR bound",
        acked && snap.lastSelectStreamGeneration == 500 && snap.lastSelectOk, snap.status + " gen=" + std::to_string(snap.lastSelectStreamGeneration));
  check("P1: the selection's first output is ready (APK: select_ready)", sink.ready() == 11);
  check("P1: after it, only that generation passes (a straggler of another is dropped)",
        sink.Feed(500, false) && !sink.Feed(499, false) && !sink.Feed(499, true));

  // P6: again on the same connection.
  sink.Prepare(12);
  sink.NewDecoder();
  check("P6: a second desktop pick is accepted (a new local op)", c.RequestDesktopMode());
  wait_until([&] { return verdict(status_of(c)) == Verdict::None; }, 1000);
  check("P6: ...not selected until a NEW IDR (the panel is no longer ACKED)", verdict(status_of(c)) == Verdict::None, status_of(c));
  check("P6: deltas of the old generation do not complete it", !sink.Feed(500, false) && sink.ready() == 0);
  sink.Feed(500, true);
  check("P6: the new IDR completes it", wait_until([&] { return verdict(status_of(c)) == Verdict::Acked; }, 2000) && sink.ready() == 12);

  // P5: A then B before any IDR.
  sink.Prepare(13);
  sink.NewDecoder();
  c.RequestDesktopMode();
  sink.Prepare(14);
  c.RequestDesktopMode();
  sink.Feed(500, true);
  check("P5: A then B, then the IDR: B completes, A never does",
        wait_until([&] { return verdict(status_of(c)) == Verdict::Acked; }, 2000) && sink.ready() == 14 &&
            sink.LocalSelectionBoundFor(13) == 0 && sink.LocalSelectionBoundFor(14) == 500);
  // ...and a local op abandoned before its IDR completes nothing afterwards.
  sink.Prepare(15);
  sink.NewDecoder();
  c.RequestDesktopMode();
  sink.Abort();
  const bool fed = sink.Feed(500, true);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  check("P5: an abandoned local op: the IDR after it completes nothing, the panel does not say selected",
        sink.ready() == 0 && verdict(status_of(c)) != Verdict::Acked && sink.LocalSelectionBoundFor(15) == 0,
        std::string("fed=") + (fed ? "1" : "0") + " status=" + status_of(c));
  check("P1: no WindowSelect ever reached the logon-screen host", h.Count(MessageType::ControlWindowSelect) == 0);
  c.Disconnect();
  h.Stop();
}

// P2: picked before the first Pong.
void p2_deferred() {
  std::printf("\n--- P2: a desktop pick before the first Pong, the Pong says logon screen ---\n");
  FakeHost h;
  h.pongFlags = kPrelogin;
  h.holdPongs = true;
  if (!h.Start()) return check("P2: fake host starts", false);
  n::ClientSessionController c;
  GateSink sink;
  check("P2: the session connects (Pongs held)", connect_to(c, h, &sink));
  sink.Prepare(21);
  check("P2: the pick is accepted before the role is known", c.RequestDesktopMode());
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  h.holdPongs = false;
  const bool keyAsked = wait_until([&] { return h.Count(MessageType::ControlRequestKeyFrame) >= 1; }, 3000);
  check("P2: after the Pong the SAME op went local: an IDR asked for, no WindowSelect sent",
        keyAsked && h.Count(MessageType::ControlWindowSelect) == 0, "status=" + status_of(c));
  sink.Feed(800, true);
  const bool acked = wait_until([&] { return verdict(status_of(c)) == Verdict::Acked; }, 2000);
  check("P2: the IDR completes it (APK: ACKED, then ready)", acked && sink.ready() == 21, status_of(c));
  c.Disconnect();
  h.Stop();
}

// P3: window picks at a logon-screen host.
void p3_window() {
  std::printf("\n--- P3: window picks at the logon screen are refused, not left waiting ---\n");
  {
    FakeHost h;
    h.pongFlags = kPrelogin;
    if (!h.Start()) return check("P3: fake host starts", false);
    n::ClientSessionController c;
    GateSink sink;
    check("P3: the session connects", connect_to(c, h, &sink));
    role_known(c, h);
    sink.Prepare(31);
    const bool asked = c.RequestWindowSelect(1001);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    check("P3: known logon screen: the window pick is refused at once, nothing on the wire",
          !asked && h.Count(MessageType::ControlWindowSelect) == 0, "status=" + status_of(c));
    c.Disconnect();
    h.Stop();
  }
  {
    FakeHost h;
    h.pongFlags = kPrelogin;
    h.holdPongs = true;
    if (!h.Start()) return check("P3: fake host starts (2)", false);
    n::ClientSessionController c;
    GateSink sink;
    check("P3: the session connects (Pongs held)", connect_to(c, h, &sink));
    sink.Prepare(32);
    check("P3: a window pick before the role is known is queued", c.RequestWindowSelect(1001));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    h.holdPongs = false;
    const bool failed = wait_until([&] { return verdict(status_of(c)) == Verdict::Failed; }, 3000);
    check("P3: after the Pong it is refused (APK: FAILED, back to the list), never sent",
          failed && h.Count(MessageType::ControlWindowSelect) == 0, status_of(c));
    check("P3: ...and the gate opens nothing for it", !sink.Feed(900, true) && sink.ready() == 0);
    c.Disconnect();
    h.Stop();
  }
}

// P4: ordinary hosts keep the ack path.
void p4_ordinary() {
  std::printf("\n--- P4: an ordinary host, and UAC / LockCred without PreloginV1: the ack path ---\n");
  for (const uint32_t flags : {0u, n::kCaptureFlagSecureDesktopActive | n::kCaptureFlagLockCredV1}) {
    FakeHost h;
    h.pongFlags = flags;
    if (!h.Start()) return check("P4: fake host starts", false);
    n::ClientSessionController c;
    GateSink sink;
    const std::string tag = flags ? "P4 (UAC+LockCred)" : "P4 (ordinary)";
    check((tag + ": the session connects").c_str(), connect_to(c, h, &sink));
    role_known(c, h);
    sink.Prepare(41);
    c.RequestDesktopMode();
    const bool sent = wait_until([&] { return h.Count(MessageType::ControlWindowSelect) == 1; }, 2500);
    const bool acked = wait_until([&] { return verdict(status_of(c)) == Verdict::Acked; }, 2500);
    const uint64_t gen = c.WindowPanelSnapshotCopy().lastSelectStreamGeneration;
    check((tag + ": WindowSelect(0) on the wire, the host's answer selects (its generation)").c_str(),
          sent && acked && gen >= 7001, status_of(c) + " gen=" + std::to_string(gen));
    check((tag + ": the gate follows the host's answer, not a local bind").c_str(),
          sink.expected() == gen && !sink.Feed(gen + 100, true) && sink.Feed(gen, true) && sink.ready() == 41);
    c.Disconnect();
    h.Stop();
  }
}

// P8 (apk-0232 r2, Codex N1): a queued pick A turned local in the sink and held there (test hook) -- meanwhile the
// user cancels it and picks B -- then A goes on. B, completed by its own IDR, must show selected with ITS generation;
// A's late turn must not take B's tracking. And: A cancelled with no new pick completes nothing afterwards.
void p8_stale_turn() {
  std::printf("\n--- P8: a queued pick turned local late does not take a newer pick's tracking ---\n");
  for (const bool pickB : {true, false}) {
    FakeHost h;
    h.pongFlags = kPrelogin;
    h.holdPongs = true;
    if (!h.Start()) return check("P8: fake host starts", false);
    n::ClientSessionController c;
    GateSink sink;
    check(pickB ? "P8: the session connects (Pongs held)" : "P8 (cancel only): the session connects", connect_to(c, h, &sink));
    std::atomic<bool> entered{false}, release{false};
    c.SetTestAfterLocalSinkBeginHook([&](uint64_t tag) {
      if (tag != 81) return;
      entered = true;
      const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
      while (!release.load() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    });
    sink.Prepare(81);
    c.RequestDesktopMode();  // queued: the role is not known yet
    h.holdPongs = false;
    const bool held = wait_until([&] { return entered.load(); }, 4000);
    check(pickB ? "P8: A was turned local in the sink and is held there" : "P8 (cancel only): A held after its sink turn", held);
    std::thread ui;
    if (pickB) {
      // the user: back out of A, pick B (prepare + desktop pick) -- on the UI's own thread
      sink.Prepare(82);
      ui = std::thread([&] { c.RequestDesktopMode(); });
    } else {
      sink.Abort();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    release = true;
    if (ui.joinable()) ui.join();
    c.SetTestAfterLocalSinkBeginHook(nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    sink.NewDecoder();
    sink.Feed(910, true);
    if (pickB) {
      const bool acked = wait_until([&] { return verdict(status_of(c)) == Verdict::Acked; }, 2500);
      check("P8: B, completed by its own IDR, shows selected with ITS generation (A's late turn took nothing of B's)",
            acked && c.WindowPanelSnapshotCopy().lastSelectStreamGeneration == 910 && sink.ready() == 82,
            status_of(c) + " gen=" + std::to_string(c.WindowPanelSnapshotCopy().lastSelectStreamGeneration));
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      check("P8 (cancel only): A, cancelled, completes nothing afterwards", verdict(status_of(c)) != Verdict::Acked && sink.ready() == 0,
            status_of(c));
    }
    check(pickB ? "P8: no WindowSelect reached the logon-screen host" : "P8 (cancel only): no WindowSelect on the wire",
          h.Count(MessageType::ControlWindowSelect) == 0);
    c.Disconnect();
    h.Stop();
  }
}

// P9 (apk-0232 r3, Codex N1 last boundary): the poll has found the local pick A bound to its IDR's generation and is
// about to record it (test hook) -- there the user cancels A, or prepares a newer pick B. The cancel / Prepare must
// not finish in that gap with A recorded selected after it: either A's record comes first (then the cancel follows
// it), or nothing of A is recorded. With B: B then completes with its OWN IDR and generation.
void p9_commit_vs_cancel() {
  std::printf("\n--- P9: the poll's completion and a cancel / new Prepare are one boundary ---\n");
  for (const bool pickB : {false, true}) {
    const char* tagName = pickB ? "P9 (new Prepare B)" : "P9 (cancel)";
    FakeHost h;
    h.pongFlags = kPrelogin;
    if (!h.Start()) return check("P9: fake host starts", false);
    n::ClientSessionController c;
    GateSink sink;
    check((std::string(tagName) + ": the session connects, logon-screen role known").c_str(),
          connect_to(c, h, &sink) && role_known(c, h));
    std::atomic<bool> entered{false}, uiDone{false};
    std::atomic<int> verdictAtUiDone{-1};
    std::thread ui;
    c.SetTestBeforeLocalCommitHook([&](uint64_t tag, uint64_t generation) {
      if (tag != 91 || entered.exchange(true)) return;
      // the user, on the UI thread, in the gap: back out of A (or prepare B)
      ui = std::thread([&] {
        if (pickB) sink.Prepare(92); else sink.Abort();
        verdictAtUiDone = static_cast<int>(verdict(status_of(c)));
        uiDone = true;
      });
      // give the UI's call every chance to finish inside the gap (it cannot, if the gap is one boundary)
      const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
      while (!uiDone.load() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(5));
      (void)generation;
    });
    sink.Prepare(91);
    sink.NewDecoder();
    c.RequestDesktopMode();  // the role is known: a local selection at once
    wait_until([&] { return h.Count(MessageType::ControlRequestKeyFrame) >= 1; }, 2500);
    sink.Feed(700, true);    // A's IDR binds it
    check((std::string(tagName) + ": the poll reached A bound, before its record").c_str(),
          wait_until([&] { return entered.load(); }, 3000));
    wait_until([&] { return uiDone.load(); }, 3000);
    if (ui.joinable()) ui.join();
    c.SetTestBeforeLocalCommitHook(nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const Verdict finalV = verdict(status_of(c));
    const bool recordedAfterUi = finalV == Verdict::Acked && verdictAtUiDone.load() != static_cast<int>(Verdict::Acked);
    check((std::string(tagName) + ": A is not recorded selected AFTER the user's call finished (A first, or nothing)").c_str(),
          uiDone.load() && !recordedAfterUi,
          "at ui done=" + std::to_string(verdictAtUiDone.load()) + " final=" + status_of(c));
    if (!pickB) {
      check("P9 (cancel): after the cancel the sink holds no selection", sink.CurrentSelectionTag() == 0 && sink.ready() == 0);
    } else {
      c.RequestDesktopMode();
      sink.NewDecoder();
      wait_until([&] { return h.Count(MessageType::ControlRequestKeyFrame) >= 2; }, 2500);
      sink.Feed(701, true);
      const bool acked = wait_until([&] {
        return verdict(status_of(c)) == Verdict::Acked && c.WindowPanelSnapshotCopy().lastSelectStreamGeneration == 701;
      }, 2500);
      check("P9 (new Prepare B): B completes with its OWN IDR and generation", acked && sink.ready() == 92,
            status_of(c) + " gen=" + std::to_string(c.WindowPanelSnapshotCopy().lastSelectStreamGeneration));
    }
    check((std::string(tagName) + ": no WindowSelect on the wire").c_str(), h.Count(MessageType::ControlWindowSelect) == 0);
    c.Disconnect();
    h.Stop();
  }
}

// P7: a new connection.
void p7_reconnect() {
  std::printf("\n--- P7: a new connection to a logon-screen host ---\n");
  n::ClientSessionController c;
  GateSink sink;
  for (int round = 0; round < 2; ++round) {
    FakeHost h;
    h.pongFlags = kPrelogin;
    if (!h.Start()) return check("P7: fake host starts", false);
    check("P7: the session connects", connect_to(c, h, &sink));
    role_known(c, h);
    sink.Prepare(70 + round);
    sink.NewDecoder();
    c.RequestDesktopMode();
    wait_until([&] { return h.Count(MessageType::ControlRequestKeyFrame) >= 1; }, 2500);
    sink.Feed(600 + round, true);
    check(round == 0 ? "P7: first connection: the pick completes" : "P7: after a reconnect: the pick completes again",
          wait_until([&] { return verdict(status_of(c)) == Verdict::Acked; }, 2000) && sink.ready() == 70u + round &&
              c.WindowPanelSnapshotCopy().lastSelectStreamGeneration == 600u + round && h.Count(MessageType::ControlWindowSelect) == 0,
          status_of(c));
    c.Disconnect();
    h.Stop();
  }
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("client_session_prelogin_select_test (apk-0232 r1)\n");
  p1_local();
  p2_deferred();
  p3_window();
  p4_ordinary();
  p7_reconnect();
  p8_stale_turn();
  p9_commit_vs_cancel();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

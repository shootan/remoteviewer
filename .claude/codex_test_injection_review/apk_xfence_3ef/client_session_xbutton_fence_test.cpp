// keepalive-r2 e (codex_ka_pc_src_r7.md "APK 영향"): the mouse X buttons through the PRODUCT shared session the
// Android app drives (ClientSessionController and its control loop, native_video_client_session.cpp) against a
// fake control host on loopback. No UI, no SendInput, no desktop, no real host, no AVD.
//
// The contract (client_mouse_xbutton_fence.hpp; the PC: fence_mouse_for_host): a host whose Pong lacks
// kCaptureFlagMouseXButtonsV1 receives no X edge (ControlInputEvent kind 2 / 3, keyCode 5 / 6) and no X bit in
// `buttons` (0x8 / 0x10) -- on every path into the session; a host that has it receives them unchanged.
//
// What is the product: the controller, its control loop, scheduler, role gate, X fence, park, held-input releases,
// the TCP control client, and InputMacro (input_macro.cpp, its parser and playback). What is the test: the fake host
// (answers Ping / the window list / InputEvent, records every input event it is sent), and the app's calls:
//   direct  -- QueueInputEvent exactly as nativeQueueInputEvent (native_bridge.cpp) calls it: buttons & 0x7.
//   macro   -- InputMacro::LoadSerialized + StartPlayback, pumped with the loop nativeMacroPump uses
//              (PollDueStep -> QueueInputEvent). The JNI glue itself is Android-only and not compiled here.
//   park    -- a button held on the host, BeginPark (its release is made by ClientHeldInput and queued by the
//              park's collection), ResumeFromPark, input again.
//   action  -- the send-time fence on a hand-built ControlOutboundAction (a role not known yet, a host without X):
//              the backstop for whatever reaches the queue without QueueInputEvent.
// Every "0 X" is read next to a positive control in the same run (the non-X events arrive; the supporting host
// gets the X events), so a 0 cannot come from nothing being sent at all.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "client_mouse_xbutton_fence.hpp"
#include "input_macro.hpp"
#include "native_socket.hpp"
#include "native_video_client_session.hpp"
#include "native_video_client_shared_core.hpp"
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

struct WireInput {
  uint16_t kind;
  uint32_t keyCode;
  uint16_t buttons;
};

constexpr uint16_t kXBits = n::kMouseWireX1 | n::kMouseWireX2;
constexpr uint32_t kOrdinaryListFlags = n::kControlWindowListFlagThumbnails | n::kControlWindowListFlagMonitors |
                                        n::kControlWindowListFlagMonitorSelectTransactionV1;

// The fake control host: UDP HelloAck, then a TCP control stream that answers Ping / WindowListRequest /
// MonitorListRequest / InputEvent and records every input event it is sent.
struct FakeHost {
  std::atomic<uint32_t> pongFlags{0};
  int controlPort = 0, videoPort = 0;
  std::atomic<bool> stop{false};
  SocketHandle tcpListen = kInvalidSocket, accepted = kInvalidSocket, udp = kInvalidSocket;
  std::thread tcpThread, udpThread;
  mutable std::mutex mu;
  std::vector<WireInput> inputs;
  std::vector<uint16_t> streamStates;  // flags of every StreamState (bit0 active)

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

  template <typename T>
  static T Decode(const MessageHeader& h, const std::vector<uint8_t>& body) {
    T m{};
    std::memcpy(&m, &h, sizeof(h));
    std::memcpy(reinterpret_cast<uint8_t*>(&m) + sizeof(h), body.data(), body.size());
    return m;
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
      if (type == MessageType::ControlPing && h.size == sizeof(n::ControlPingMessage)) {
        const auto ping = Decode<n::ControlPingMessage>(h, body);
        n::ControlPongMessage pong{};
        pong.header.magic = n::kMagic;
        pong.header.type = static_cast<uint16_t>(MessageType::ControlPong);
        pong.header.size = static_cast<uint16_t>(sizeof(pong));
        pong.seq = ping.seq;
        pong.clientSendQpcUs = ping.clientSendQpcUs;
        pong.captureTargetFlags = pongFlags.load();
        if (!n::send_all(accepted, &pong, sizeof(pong))) break;
      } else if (type == MessageType::ControlInputEvent && h.size == sizeof(n::ControlInputEventMessage)) {
        const auto e = Decode<n::ControlInputEventMessage>(h, body);
        {
          std::lock_guard<std::mutex> l(mu);
          inputs.push_back(WireInput{e.kind, e.keyCode, e.buttons});
        }
        n::ControlInputAckMessage ack{};
        ack.header.magic = n::kMagic;
        ack.header.type = static_cast<uint16_t>(MessageType::ControlInputAck);
        ack.header.size = static_cast<uint16_t>(sizeof(ack));
        ack.seq = e.seq;
        if (!n::send_all(accepted, &ack, sizeof(ack))) break;
      } else if (type == MessageType::ControlStreamState && h.size == sizeof(n::ControlStreamStateMessage)) {
        const auto m = Decode<n::ControlStreamStateMessage>(h, body);
        std::lock_guard<std::mutex> l(mu);
        streamStates.push_back(static_cast<uint16_t>(m.flags));
      } else if (type == MessageType::ControlWindowListRequest && h.size == sizeof(n::ControlWindowListRequestMessage)) {
        const auto req = Decode<n::ControlWindowListRequestMessage>(h, body);
        n::ControlWindowListMessage list{};
        list.header.magic = n::kMagic;
        list.header.type = static_cast<uint16_t>(MessageType::ControlWindowList);
        list.header.size = static_cast<uint16_t>(sizeof(list));
        list.seq = req.seq;
        list.flags = kOrdinaryListFlags;
        list.itemCount = 1;
        list.items[0].id = 1001;
        std::snprintf(list.items[0].title, sizeof(list.items[0].title), "%s", "Editor");
        if (!n::send_all(accepted, &list, sizeof(list))) break;
      } else if (type == MessageType::ControlMonitorListRequest && h.size == sizeof(n::ControlMonitorListRequestMessage)) {
        const auto req = Decode<n::ControlMonitorListRequestMessage>(h, body);
        n::ControlMonitorListMessage ml{};
        ml.header.magic = n::kMagic;
        ml.header.type = static_cast<uint16_t>(MessageType::ControlMonitorList);
        ml.header.size = static_cast<uint16_t>(sizeof(ml));
        ml.seq = req.seq;
        ml.itemCount = 1;
        ml.items[0].width = 1920;
        ml.items[0].height = 1080;
        if (!n::send_all(accepted, &ml, sizeof(ml))) break;
      } else if (type == MessageType::ControlWindowThumbnailRequest &&
                 h.size == sizeof(n::ControlWindowThumbnailRequestMessage)) {
        const auto req = Decode<n::ControlWindowThumbnailRequestMessage>(h, body);
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

  std::vector<WireInput> Inputs() const {
    std::lock_guard<std::mutex> l(mu);
    return inputs;
  }
  size_t InputCount() const {
    std::lock_guard<std::mutex> l(mu);
    return inputs.size();
  }
};

struct Tally {
  size_t total = 0, xEdges = 0, xBits = 0;
  std::vector<WireInput> all;
  size_t count(uint16_t kind, uint32_t key) const {
    size_t c = 0;
    for (const auto& e : all) c += (e.kind == kind && e.keyCode == key) ? 1 : 0;
    return c;
  }
  std::string str() const {
    return "wire=" + std::to_string(total) + " Xedges=" + std::to_string(xEdges) + " Xbits=" + std::to_string(xBits);
  }
};
Tally tally(const std::vector<WireInput>& v, size_t from = 0) {
  Tally t;
  for (size_t i = from; i < v.size(); ++i) {
    const auto& e = v[i];
    t.all.push_back(e);
    ++t.total;
    t.xEdges += n::IsMouseXButtonEdge(e.kind, e.keyCode) ? 1 : 0;
    t.xBits += (e.buttons & kXBits) != 0 ? 1 : 0;
  }
  return t;
}

bool connect_to(n::ClientSessionController& c, const FakeHost& h) {
  n::ClientSessionConnectArgs a{};
  a.host = "127.0.0.1";
  a.videoPort = h.videoPort;
  a.controlPort = h.controlPort;
  a.controlIntervalMs = 50;
  if (!c.Connect(a)) return false;
  return wait_until([&] {
    const auto s = c.Snapshot();
    return s.state == n::ClientSessionState::Connected && s.controlLoopActive;
  }, 5000);
}

// Connected, and the first Pong's role is in (so the queue-time fence judges a known host).
bool connect_and_know_role(n::ClientSessionController& c, FakeHost& h, uint32_t flags) {
  h.pongFlags = flags;
  if (!connect_to(c, h)) return false;
  (void)c.RequestWindowList();  // the window list says the host honours the pause (park needs it)
  return wait_until([&] { return c.HostCaptureFlags() == flags && c.HostHonorsStreamPause(); }, 3000);
}

// nativeQueueInputEvent (native_bridge.cpp): kind clamped, keyCode as given, buttons & 0x7.
bool jni_queue(n::ClientSessionController& c, uint16_t kind, uint32_t keyCode, uint16_t buttons) {
  return c.QueueInputEvent(kind, 100, 100, 0, keyCode, static_cast<uint16_t>(buttons & 0x7));
}

const char* hostName(bool takesX) { return takesX ? "X host" : "old host"; }

// ---- direct (JNI) -----------------------------------------------------------------------------------------
void direct(bool takesX) {
  std::printf("\n--- direct (JNI) input, %s ---\n", hostName(takesX));
  FakeHost h;
  if (!h.Start()) return check("direct: fake host starts", false);
  n::ClientSessionController c;
  const uint32_t flags = takesX ? n::kCaptureFlagMouseXButtonsV1 : 0u;
  check("direct: connected, role known", connect_and_know_role(c, h, flags));
  const bool xDown = jni_queue(c, 2, 5, 0x1);
  const bool xUp = jni_queue(c, 3, 5, 0x1);
  const bool x2Down = jni_queue(c, 2, 6, 0);
  const bool x2Up = jni_queue(c, 3, 6, 0);
  const bool lDown = jni_queue(c, 2, 1, 0x1);
  const bool lUp = jni_queue(c, 3, 1, 0);
  const bool move = jni_queue(c, 1, 0, 0x1);
  const bool sent = wait_until([&] { return h.InputCount() >= (takesX ? 7u : 3u); }, 3000);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const Tally t = tally(h.Inputs());
  if (takesX) {
    check("direct X host: every edge accepted and on the wire, X1/X2 down+up included (unaffected)",
          sent && xDown && xUp && x2Down && x2Up && lDown && lUp && move && t.total == 7 && t.xEdges == 4 &&
              t.count(2, 5) == 1 && t.count(3, 6) == 1,
          t.str());
  } else {
    check("direct old host: the X edges are refused at the call (QueueInputEvent false)",
          !xDown && !xUp && !x2Down && !x2Up && c.MouseXButtonsHeldBack() == 4,
          "heldBack=" + std::to_string(c.MouseXButtonsHeldBack()));
    check("direct old host: wire X edges 0, X bits 0 -- the left down/up and the move DID arrive (positive control)",
          sent && lDown && lUp && move && t.xEdges == 0 && t.xBits == 0 && t.total == 3 && t.count(2, 1) == 1 &&
              t.count(3, 1) == 1,
          t.str());
  }
  c.Disconnect();
}

// ---- macro -----------------------------------------------------------------------------------------------
// A macro file as the app stores it: an X1 click, an X2 click, a move with every button bit (0x1F, the parser
// keeps all five), a left click, a left down whose move carries X bits too.
const char* kMacroText =
    "gnlink-macro-v1\n"
    "2 10 10 0 5 8 0\n"
    "3 10 10 0 5 0 0\n"
    "2 10 10 0 6 16 0\n"
    "3 10 10 0 6 0 0\n"
    "1 20 20 0 0 31 0\n"
    "2 30 30 0 1 1 0\n"
    "1 31 31 0 0 25 0\n"
    "3 30 30 0 1 0 0\n";

void macro(bool takesX) {
  std::printf("\n--- macro playback, %s ---\n", hostName(takesX));
  n::InputMacro m;
  check("macro: the stored macro loads, X steps and X bits kept by the parser", m.LoadSerialized(kMacroText));
  FakeHost h;
  if (!h.Start()) return check("macro: fake host starts", false);
  n::ClientSessionController c;
  const uint32_t flags = takesX ? n::kCaptureFlagMouseXButtonsV1 : 0u;
  check("macro: connected, role known", connect_and_know_role(c, h, flags));
  n::MacroPlaybackOptions o{};
  const auto t0 = std::chrono::steady_clock::now();
  auto nowMs = [&] {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() + 1000);
  };
  check("macro: playback starts", m.StartPlayback(o, nowMs(), 7));
  // nativeMacroPump: while (PollDueStep) QueueInputEvent(step.kind, x, y, wheel, keyCode, buttons) -- no mask.
  int accepted = 0, refused = 0, steps = 0;
  for (int tick = 0; tick < 200 && m.state() == n::InputMacro::State::Playing; ++tick) {
    n::MacroStep step;
    while (m.PollDueStep(nowMs(), &step)) {
      ++steps;
      (c.QueueInputEvent(step.kind, step.x, step.y, step.wheelDelta, step.keyCode, step.buttons) ? accepted : refused)++;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const size_t want = takesX ? 8u : 4u;
  const bool sent = wait_until([&] { return h.InputCount() >= want; }, 3000);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const Tally t = tally(h.Inputs());
  const std::string d = t.str() + " steps=" + std::to_string(steps) + " accepted=" + std::to_string(accepted) +
                        " refused=" + std::to_string(refused);
  if (takesX) {
    check("macro X host: all 8 steps on the wire, X edges 4 and X bits as stored (unaffected)",
          sent && steps == 8 && accepted == 8 && t.total == 8 && t.xEdges == 4 && t.xBits == 4, d);
  } else {
    check("macro old host: the 4 X edges refused, the 4 others accepted", steps == 8 && refused == 4 && accepted == 4,
          d);
    bool masked = true;
    for (const auto& e : t.all) masked = masked && (e.buttons & ~n::kMouseWireLegacyMask) == 0;
    check("macro old host: wire X edges 0, X bits 0; the moves' left bit kept, the left click arrived (positive)",
          sent && t.xEdges == 0 && t.xBits == 0 && t.total == 4 && masked && t.count(2, 1) == 1 && t.count(3, 1) == 1 &&
              t.all.size() == 4 && t.all[0].kind == 1 && t.all[0].buttons == 0x7 && t.all[2].buttons == 0x1,
          d);
  }
  c.Disconnect();
}

// ---- park / resume ---------------------------------------------------------------------------------------
bool park_now(n::ClientSessionController& c) {
  if (!c.BeginPark()) return false;
  return wait_until([&] { return c.ParkPhase() == n::ClientParkPhase::Parked; }, 3000);
}

void park(bool takesX) {
  std::printf("\n--- park / resume with a button held, %s ---\n", hostName(takesX));
  FakeHost h;
  if (!h.Start()) return check("park: fake host starts", false);
  n::ClientSessionController c;
  const uint32_t flags = takesX ? n::kCaptureFlagMouseXButtonsV1 : 0u;
  check("park: connected, role known, pause honoured", connect_and_know_role(c, h, flags));
  // held on the host: X1 (if it may go) and left
  const bool xDown = jni_queue(c, 2, 5, 0);
  const bool lDown = jni_queue(c, 2, 1, 0x1);
  const size_t downs = takesX ? 2u : 1u;
  const bool downsSent = wait_until([&] { return h.InputCount() >= downs; }, 3000);
  const size_t mark = h.InputCount();
  const bool parked = park_now(c);
  const bool releasedSent = wait_until([&] { return h.InputCount() >= mark + downs; }, 3000);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const Tally rel = tally(h.Inputs(), mark);
  const std::string d = rel.str() + " parkReleased=" + std::to_string(c.ParkReleasedCount()) +
                        " xDown=" + std::to_string(xDown) + " lDown=" + std::to_string(lDown);
  if (takesX) {
    check("park X host: the held X1 and left are released by the park (X up on the wire -- no stuck X)",
          downsSent && parked && releasedSent && xDown && lDown && rel.count(3, 5) == 1 && rel.count(3, 1) == 1 &&
              rel.total == 2,
          d);
  } else {
    check("park old host: the X down was refused; only the left is held and released -- park's releases carry "
          "no X edge, no X bit",
          downsSent && parked && releasedSent && !xDown && lDown && rel.count(3, 1) == 1 && rel.xEdges == 0 &&
              rel.xBits == 0 && rel.total == 1,
          d);
  }
  const bool resumed = c.ResumeFromPark();
  (void)c.RequestStreamActive(true);
  const size_t mark2 = h.InputCount();
  const bool x2 = jni_queue(c, 2, 6, 0);
  const bool x2u = jni_queue(c, 3, 6, 0);
  const bool l = jni_queue(c, 2, 1, 0x1);
  const bool lu = jni_queue(c, 3, 1, 0);
  const size_t after = takesX ? 4u : 2u;
  const bool flowed = wait_until([&] { return h.InputCount() >= mark2 + after; }, 3000);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const Tally t2 = tally(h.Inputs(), mark2);
  if (takesX) {
    check("park X host: after resume X2 down/up and left down/up go again", resumed && flowed && x2 && x2u && l &&
          lu && t2.total == 4 && t2.xEdges == 2, t2.str());
  } else {
    check("park old host: after resume the X2 edges are refused again, the left click goes (positive)",
          resumed && flowed && !x2 && !x2u && l && lu && t2.total == 2 && t2.xEdges == 0 && t2.xBits == 0, t2.str());
  }
  c.Disconnect();
}

// ---- the send-time fence on an action --------------------------------------------------------------------
n::ControlOutboundAction input_action(uint16_t kind, uint32_t keyCode, uint16_t buttons) {
  n::ControlOutboundAction a{};
  a.kind = n::ControlOutboundActionKind::InputEvent;
  a.inputEvent.kind = kind;
  a.inputEvent.keyCode = keyCode;
  a.inputEvent.buttons = buttons;
  return a;
}

void action_fence() {
  std::printf("\n--- the send-time fence on an outbound action ---\n");
  struct Role {
    const char* name;
    bool known;
    uint32_t flags;
    bool takes;
  };
  const Role roles[] = {
      {"role not known yet", false, n::kCaptureFlagMouseXButtonsV1, false},
      {"known, no X flag", true, n::kCaptureFlagPreloginV1, false},
      {"known, X flag", true, n::kCaptureFlagMouseXButtonsV1 | n::kCaptureFlagPreloginV1, true},
  };
  for (const Role& r : roles) {
    const bool takes = n::HostTakesMouseXButtons(r.known, r.flags);
    int refused = 0, masked = 0, untouched = 0;
    for (uint16_t kind : {uint16_t{2}, uint16_t{3}}) {
      for (uint32_t key : {5u, 6u}) {
        auto a = input_action(kind, key, 0x1F);
        refused += n::FenceMouseXButtonsAction(takes, &a) ? 0 : 1;
      }
    }
    auto mv = input_action(1, 0, 0x1F);
    const bool mvOk = n::FenceMouseXButtonsAction(takes, &mv);
    masked += (mvOk && mv.inputEvent.buttons == (takes ? 0x1F : 0x7)) ? 1 : 0;
    auto left = input_action(3, 1, 0x19);
    const bool leftOk = n::FenceMouseXButtonsAction(takes, &left);
    masked += (leftOk && left.inputEvent.buttons == (takes ? 0x19 : 0x1)) ? 1 : 0;
    auto key5 = input_action(5, 5, 0);  // a KEY event with code 5 is not a mouse X edge (PC fence the same)
    untouched += n::FenceMouseXButtonsAction(takes, &key5) ? 1 : 0;
    n::ControlOutboundAction ping{};
    ping.kind = n::ControlOutboundActionKind::Ping;
    untouched += n::FenceMouseXButtonsAction(takes, &ping) ? 1 : 0;
    const bool ok = takes == r.takes && refused == (r.takes ? 0 : 4) && masked == 2 && untouched == 2;
    check((std::string("action fence, ") + r.name + (r.takes ? ": X edges and bits pass" : ": 4/4 X edges refused, X bits masked, the rest passes")).c_str(),
          ok, "takes=" + std::to_string(takes) + " refused=" + std::to_string(refused) + " masked=" +
                  std::to_string(masked) + " untouched=" + std::to_string(untouched));
  }
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("mouse X buttons through the PRODUCT ClientSessionController (APK shared session, keepalive-r2 e)\n");
  action_fence();
  for (bool takesX : {false, true}) {
    direct(takesX);
    macro(takesX);
    park(takesX);
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

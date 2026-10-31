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
//   r2 (codex_apk_xfence_src_r1.md X1 / X2) -- the capability CHANGING under one press, by the fake host's Pong
//   phases (a Pong held back, its flags switched):
//     unknown -> X       a down refused before the first Pong: after the X Pong its up and its held bit stay off
//                        the wire; the next press goes whole.
//     X -> no X (send)   a press queued while the host took X, sent after a Pong without X: the down, its moves'
//                        bit and its up are all dropped at the send fence; back to X, the next press goes.
//     X -> no X (owed)   a down SENT to an X host, then no X: its owed release is not sent (no exception), the
//                        park cannot finish and ends by its own bound (not confirmed), the session ends.
//     reconnect          a press refused on one connection is not remembered on the next.
//   r3 (codex_apk_xfence_src_r2.md N1) -- a down SENT to an X host owes its release, whatever becomes of a later down
//   of the same button:
//     owed / entry       a later down refused at the entry (no X): back to X, the up goes -- exactly one X up; while
//                        still no X, none.
//     owed / send        a later down admitted, then dropped at sending (no X): back to X, the up goes -- exactly one;
//                        while still no X, none.
//     In each, the debt ends with that answered up: a park afterwards confirms (nothing held), X2 and left unaffected.
//   r4 (codex_apk_xfence_src_r3.md N1) -- a down QUEUED while the host took X, not sent yet; a no-X Pong; its up
//   refused at the entry; an X Pong; then the sender reaches the down: it must not go alone (no orphan down). The
//   loop is held on two Pings in a row (a Ping goes before queued input once it is due), so the down stays queued
//   through both Pongs.
//   ledger  -- ClientMouseXPressLedger itself, both sides, the same transitions without a session.
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
  std::atomic<bool> holdPongs{false};  // r2: a Ping is answered only once released (the client's loop waits on it)
  std::atomic<int> heldPings{0};
  std::atomic<int> releaseOne{0};  // r4: answers ONE held Ping (the next stays held while holdPongs)
  std::atomic<int> ackDelayMs{0};  // r4: each InputAck this late (the sender stays busy on earlier input)
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
        if (holdPongs.load()) {
          ++heldPings;
          while (holdPongs.load() && !stop.load()) {
            int one = releaseOne.load();
            if (one > 0 && releaseOne.compare_exchange_strong(one, one - 1)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
          }
        }
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
        if (ackDelayMs.load() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ackDelayMs.load()));
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

// ---- r2: the capability changing under one press ----------------------------------------------------------
constexpr uint32_t kX = n::kCaptureFlagMouseXButtonsV1;

// The client's control loop is waiting on a Ping the fake host holds: nothing is sent until the Pong is released.
bool hold_next_pong(FakeHost& h) {
  const int before = h.heldPings.load();
  h.holdPongs = true;
  return wait_until([&] { return h.heldPings.load() > before; }, 3000);
}
bool release_pong_as(FakeHost& h, n::ClientSessionController& c, uint32_t flags) {
  h.pongFlags = flags;
  h.holdPongs = false;
  return wait_until([&] { return c.HostCaptureFlags() == flags; }, 3000);
}

void unknown_to_x() {
  std::printf("\n--- r2: a down refused before the first Pong, then the X Pong ---\n");
  FakeHost h;
  h.holdPongs = true;  // the first Pong held: the role is unknown
  if (!h.Start()) return check("unknown->X: fake host starts", false);
  n::ClientSessionController c;
  n::ClientSessionConnectArgs a{};
  a.host = "127.0.0.1";
  a.videoPort = h.videoPort;
  a.controlPort = h.controlPort;
  a.controlIntervalMs = 50;
  const bool up = c.Connect(a) && wait_until([&] {
    const auto s = c.Snapshot();
    return s.state == n::ClientSessionState::Connected && s.controlLoopActive && h.heldPings.load() > 0;
  }, 5000);
  check("unknown->X: connected, the first Pong held (role unknown)", up && c.HostCaptureFlags() == 0);
  // a macro press as nativeMacroPump hands it (no 0x7 mask): X1 down, then its moves carry the X1 bit
  const bool down = c.QueueInputEvent(2, 10, 10, 0, 5, n::kMouseWireX1);
  const bool known = release_pong_as(h, c, kX);
  // one move (consecutive moves coalesce in the queue): X1 still held, left too
  const bool mv1 = c.QueueInputEvent(1, 11, 11, 0, 0, n::kMouseWireX1 | n::kMouseWireLeft);
  const bool mv2 = mv1;
  const bool upX = c.QueueInputEvent(3, 12, 12, 0, 5, 0);
  // the next press, after that up: judged afresh -- the host takes X now
  const bool down2 = c.QueueInputEvent(2, 20, 20, 0, 5, n::kMouseWireX1);
  const bool mv3 = c.QueueInputEvent(1, 21, 21, 0, 0, n::kMouseWireX1);
  const bool up2 = c.QueueInputEvent(3, 21, 21, 0, 5, 0);
  const bool sent = wait_until([&] { return h.InputCount() >= 4; }, 3000);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const auto all = h.Inputs();
  const std::string d = "down=" + std::to_string(down) + " up=" + std::to_string(upX) + " mv=" + std::to_string(mv1) +
                        std::to_string(mv2) + " next=" + std::to_string(down2) + std::to_string(mv3) + std::to_string(up2) +
                        " wire=" + std::to_string(all.size());
  check("unknown->X: the refused press stays refused after the X Pong -- its up refused, its moves sent without X1",
        up && known && !down && !upX && mv1 && mv2 && all.size() >= 1 && all[0].kind == 1 &&
            all[0].buttons == n::kMouseWireLeft,
        d);
  check("unknown->X: the next press goes whole (X1 down, its X1 bit, X1 up) -- and nothing else X on the wire",
        sent && down2 && mv3 && up2 && all.size() == 4 && all[1].kind == 2 && all[1].keyCode == 5 &&
            all[2].buttons == n::kMouseWireX1 && all[3].kind == 3 && all[3].keyCode == 5,
        d);
  c.Disconnect();
}

void x_to_nox_at_send() {
  std::printf("\n--- r2: a down queued while the host took X, dropped at sending (no X), then X again ---\n");
  FakeHost h;
  if (!h.Start()) return check("X->noX send: fake host starts", false);
  n::ClientSessionController c;
  check("X->noX send: connected to an X host", connect_and_know_role(c, h, kX));
  const bool held = hold_next_pong(h);  // the loop now waits on this Ping: what is queued next is sent after its Pong
  const bool down = c.QueueInputEvent(2, 10, 10, 0, 6, n::kMouseWireX2);
  const bool mv = c.QueueInputEvent(1, 11, 11, 0, 0, n::kMouseWireX2 | n::kMouseWireLeft);
  const bool lclick = c.QueueInputEvent(2, 12, 12, 0, 1, n::kMouseWireLeft) && c.QueueInputEvent(3, 12, 12, 0, 1, 0);
  const uint64_t heldBack0 = c.MouseXButtonsHeldBack();
  const bool noX = release_pong_as(h, c, 0);
  const bool sent = wait_until([&] { return h.InputCount() >= 3; }, 3000);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const Tally t = tally(h.Inputs());
  check("X->noX send: queued while X (accepted); after the no-X Pong the X2 down is dropped at sending, the move's "
        "X2 bit masked; the left click goes",
        held && down && mv && lclick && noX && sent && t.total == 3 && t.xEdges == 0 && t.xBits == 0 &&
            t.all[0].kind == 1 && t.all[0].buttons == n::kMouseWireLeft && t.count(2, 1) == 1 && t.count(3, 1) == 1 &&
            c.MouseXButtonsHeldBack() == heldBack0 + 1,
        t.str() + " heldBack+" + std::to_string(c.MouseXButtonsHeldBack() - heldBack0));
  // the capability comes back while that press is still down: its moves and its up stay off the wire
  const bool held2 = hold_next_pong(h);
  const bool xAgain = release_pong_as(h, c, kX);
  const size_t mark = h.InputCount();
  const bool mv2 = c.QueueInputEvent(1, 13, 13, 0, 0, n::kMouseWireX2);
  const bool upX = c.QueueInputEvent(3, 13, 13, 0, 6, 0);
  const bool d2 = c.QueueInputEvent(2, 20, 20, 0, 6, n::kMouseWireX2);
  const bool u2 = c.QueueInputEvent(3, 20, 20, 0, 6, 0);
  const bool sent2 = wait_until([&] { return h.InputCount() >= mark + 3; }, 3000);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const Tally t2 = tally(h.Inputs(), mark);
  check("X->noX send: X again -- the dropped press's move goes without X2 and its up is dropped at sending; the next "
        "X2 press goes (down and up)",
        held2 && xAgain && mv2 && upX && d2 && u2 && sent2 && t2.total == 3 && t2.all[0].kind == 1 &&
            t2.all[0].buttons == 0 && t2.count(3, 6) == 1 && t2.count(2, 6) == 1 && t2.all[1].kind == 2,
        t2.str());
  c.Disconnect();
}

void x_to_nox_owed() {
  std::printf("\n--- r2: a down sent to an X host, then a Pong without X: its owed release ---\n");
  FakeHost h;
  if (!h.Start()) return check("X->noX owed: fake host starts", false);
  n::ClientSessionController c;
  check("X->noX owed: connected to an X host, pause honoured", connect_and_know_role(c, h, kX));
  const bool down = c.QueueInputEvent(2, 10, 10, 0, 5, n::kMouseWireX1);
  const bool sent = wait_until([&] { return h.InputCount() >= 1; }, 3000);
  const bool held = hold_next_pong(h);
  const bool noX = release_pong_as(h, c, 0);
  const size_t mark = h.InputCount();
  const bool upNow = c.QueueInputEvent(3, 10, 10, 0, 5, 0);  // the user lets go: a host without X gets no X up
  const bool began = c.BeginPark();
  const bool ended = wait_until([&] {
    const auto s = c.Snapshot();
    return s.state != n::ClientSessionState::Connected || !s.controlLoopActive;
  }, 5000);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const Tally t = tally(h.Inputs(), mark);
  const std::string err = c.Snapshot().lastError;
  check("X->noX owed: the X1 down went to the X host",
        down && sent && h.Inputs().size() >= 1 && h.Inputs()[0].kind == 2 && h.Inputs()[0].keyCode == 5);
  check("X->noX owed: after the no-X Pong no X up is sent (the user's up refused, the park's release dropped -- no "
        "exception to a host without X)",
        held && noX && !upNow && t.xEdges == 0 && t.xBits == 0, t.str() + " parkReleased=" + std::to_string(c.ParkReleasedCount()));
  check("X->noX owed: the park does not call itself confirmed -- it ends by its bound and the session ends",
        began && ended && err.find("park not confirmed") != std::string::npos && c.ParkPhase() != n::ClientParkPhase::Parked,
        "lastError=" + err + " phase=" + std::to_string(static_cast<int>(c.ParkPhase())));
  c.Disconnect();
}

void reconnect_forgets() {
  std::printf("\n--- r2: a press refused on one connection is not remembered on the next ---\n");
  n::ClientSessionController c;
  {
    FakeHost h;
    if (!h.Start()) return check("reconnect: fake host 1 starts", false);
    check("reconnect: connection 1 to a host without X", connect_and_know_role(c, h, 0));
    check("reconnect: an X1 down refused there (its up never comes on that connection)",
          !c.QueueInputEvent(2, 10, 10, 0, 5, 0));
    c.Disconnect();
  }
  FakeHost h2;
  if (!h2.Start()) return check("reconnect: fake host 2 starts", false);
  check("reconnect: connection 2 to an X host", connect_and_know_role(c, h2, kX));
  // judged by connection 2 alone: a lone X1 up (what connection 1 would have refused as its refused press's up)
  const bool loneUp = c.QueueInputEvent(3, 10, 10, 0, 5, 0);
  const bool loneSent = wait_until([&] { return h2.InputCount() >= 1; }, 3000);
  check("reconnect: connection 1's refused press is not carried -- an X1 up on connection 2 is judged afresh (X host: "
        "goes)",
        loneUp && loneSent && h2.Inputs()[0].kind == 3 && h2.Inputs()[0].keyCode == 5);
  const size_t mark = h2.InputCount();
  const bool d = c.QueueInputEvent(2, 10, 10, 0, 5, n::kMouseWireX1);
  const bool mv = c.QueueInputEvent(1, 11, 11, 0, 0, n::kMouseWireX1);
  const bool u = c.QueueInputEvent(3, 11, 11, 0, 5, 0);
  const bool sent = wait_until([&] { return h2.InputCount() >= mark + 3; }, 3000);
  const Tally t = tally(h2.Inputs(), mark);
  check("reconnect: on connection 2 a fresh X1 press goes whole (no refusal carried over)",
        d && mv && u && sent && t.total == 3 && t.xEdges == 2 && t.xBits == 2, t.str());
  c.Disconnect();
}

// r3: a down the host received, then a later down of the same button refused / dropped.
// `atEntry`: that later down refused at the entry (queued while the Pong said no X); else admitted (X) and dropped at
// sending (a no-X Pong in between). `xBack`: the host takes X again before the user's up.
void owed_kept(bool atEntry, bool xBack) {
  const char* path = atEntry ? "entry" : "send";
  std::printf("\n--- r3: owed release / a later down refused at the %s / up %s ---\n", path, xBack ? "after X is back" : "while no X");
  FakeHost h;
  if (!h.Start()) return check("owed: fake host starts", false);
  n::ClientSessionController c;
  const std::string tag = std::string("owed/") + path + (xBack ? " X back" : " no X") + ": ";
  check((tag + "connected to an X host, pause honoured").c_str(), connect_and_know_role(c, h, kX));
  const bool d1 = c.QueueInputEvent(2, 10, 10, 0, 5, n::kMouseWireX1);
  const bool sent1 = wait_until([&] { return h.InputCount() >= 1; }, 3000);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));  // its answer (ACK) in: the debt is recorded
  const size_t mark = h.InputCount();
  bool d2 = false;
  bool phase = true;
  if (atEntry) {
    phase = hold_next_pong(h) && release_pong_as(h, c, 0);
    d2 = c.QueueInputEvent(2, 11, 11, 0, 5, n::kMouseWireX1);  // refused at the entry
  } else {
    phase = hold_next_pong(h);
    d2 = c.QueueInputEvent(2, 11, 11, 0, 5, n::kMouseWireX1);  // admitted (X) ...
    phase = phase && release_pong_as(h, c, 0);                 // ... dropped at sending (no X)
  }
  const bool left = c.QueueInputEvent(2, 12, 12, 0, 1, n::kMouseWireLeft) && c.QueueInputEvent(3, 12, 12, 0, 1, 0);
  const bool leftSent = wait_until([&] { return h.InputCount() >= mark + 2; }, 3000);
  if (xBack) phase = phase && hold_next_pong(h) && release_pong_as(h, c, kX);
  const bool up = c.QueueInputEvent(3, 13, 13, 0, 5, 0);
  if (xBack) wait_until([&] { return h.InputCount() >= mark + 3; }, 3000);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const Tally t = tally(h.Inputs(), mark);
  const std::string d = t.str() + " d2=" + std::to_string(d2) + " up=" + std::to_string(up) + " X1up=" +
                        std::to_string(t.count(3, 5)) + " X1down=" + std::to_string(t.count(2, 5));
  check((tag + "the first X1 down reached the X host").c_str(), d1 && sent1 && phase);
  check((tag + (atEntry ? "the later X1 down refused at the entry" : "the later X1 down admitted, then dropped at sending (not on the wire)")).c_str(),
        (atEntry ? !d2 : d2) && t.count(2, 5) == 0 && left && leftSent && t.count(2, 1) == 1 && t.count(3, 1) == 1, d);
  if (xBack) {
    check((tag + "X back: the up goes -- exactly ONE X1 up on the wire (the owed release, not hidden by the later down)").c_str(),
          up && t.count(3, 5) == 1 && t.xEdges == 1, d);
    // the debt ended with that answered up: a park now finds nothing held -- it confirms, and sends no further X up
    const size_t mark2 = h.InputCount();
    const bool parked = c.BeginPark() && wait_until([&] { return c.ParkPhase() == n::ClientParkPhase::Parked; }, 3000);
    const Tally t2 = tally(h.Inputs(), mark2);
    check((tag + "the debt is paid: a park confirms with nothing to release (no further X up)").c_str(),
          parked && t2.count(3, 5) == 0 && c.ParkReleasedCount() == 0,
          t2.str() + " parkReleased=" + std::to_string(c.ParkReleasedCount()));
    if (parked) {
      (void)c.ResumeFromPark();
      (void)c.RequestStreamActive(true);
    }
    const size_t mark3 = h.InputCount();
    const bool x2 = c.QueueInputEvent(2, 20, 20, 0, 6, n::kMouseWireX2) && c.QueueInputEvent(3, 20, 20, 0, 6, 0);
    const bool x2sent = wait_until([&] { return h.InputCount() >= mark3 + 2; }, 3000);
    const Tally t3 = tally(h.Inputs(), mark3);
    check((tag + "X2 unaffected: its press goes (down and up)").c_str(),
          x2 && x2sent && t3.count(2, 6) == 1 && t3.count(3, 6) == 1, t3.str());
  } else {
    check((tag + "still no X: the up is NOT sent (0 X up, no exception to a host without X)").c_str(),
          !up && t.count(3, 5) == 0 && t.xEdges == 0 && t.xBits == 0, d);
  }
  c.Disconnect();
}

// r4: a queued (unsent) down whose up is refused at the entry, then X again before the sender reaches the down.
void queued_down_refused_up() {
  std::printf("\n--- r4: a queued X1 down, its up refused (no X), X again before the sender takes the down ---\n");
  FakeHost h;
  if (!h.Start()) return check("r4 orphan: fake host starts", false);
  n::ClientSessionController c;
  check("r4 orphan: connected to an X host, pause honoured", connect_and_know_role(c, h, kX));
  // The sender kept busy ahead of the down: 8 key events, each answered 20 ms late. A Ping is due again 50 ms after
  // the last Pong and goes before the next queued input -- so Ping B is sent (and held) while the down still waits.
  h.ackDelayMs = 20;
  const size_t mark = h.InputCount();
  const bool heldA = hold_next_pong(h);  // the loop waits on Ping A
  bool keys = true;
  for (int i = 0; i < 4; ++i) keys = keys && c.QueueInputEvent(5, 0, 0, 0, 0x10, 0) && c.QueueInputEvent(6, 0, 0, 0, 0x10, 0);
  const bool down = c.QueueInputEvent(2, 10, 10, 0, 5, n::kMouseWireX1);   // admitted (X), queued, not sent
  const bool mv = c.QueueInputEvent(1, 11, 11, 0, 0, n::kMouseWireX1 | n::kMouseWireLeft);  // the press held
  const int beforeB = h.heldPings.load();
  h.pongFlags = 0;
  h.releaseOne = 1;  // Pong A (no X); Ping B is held next
  const bool noX = wait_until([&] { return c.HostCaptureFlags() == 0; }, 3000);
  const bool heldB = wait_until([&] { return h.heldPings.load() > beforeB; }, 3000);
  const size_t downsSoFar = tally(h.Inputs(), mark).count(2, 5);  // the down still queued
  const size_t keysSoFar = h.InputCount() - mark;
  const bool up = c.QueueInputEvent(3, 11, 11, 0, 5, 0);  // no X now: refused at the entry
  h.pongFlags = kX;
  h.holdPongs = false;  // Pong B (X) and every later one
  const bool xAgain = wait_until([&] { return c.HostCaptureFlags() == kX; }, 3000);
  const bool flowed = wait_until([&] {
    const Tally t = tally(h.Inputs(), mark);
    return t.count(6, 0x10) == 4 && t.count(1, 0) >= 1;
  }, 5000);  // every key and the move
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  h.ackDelayMs = 0;
  const Tally t = tally(h.Inputs(), mark);
  const std::string d = t.str() + " down=" + std::to_string(down) + " up=" + std::to_string(up) + " keysBeforeB=" +
                        std::to_string(keysSoFar) + " X1down=" + std::to_string(t.count(2, 5)) +
                        " X1up=" + std::to_string(t.count(3, 5));
  check("r4 orphan: the phases held -- the down queued under X, Pong A no X, Ping B held with the down still "
        "queued, the up refused, Pong B X",
        heldA && keys && down && mv && noX && heldB && downsSoFar == 0 && keysSoFar < 8 && !up && xAgain, d);
  size_t moveBits = 0xFFFF;
  for (const auto& e : t.all) {
    if (e.kind == 1) moveBits = e.buttons;
  }
  check("r4 orphan: no orphan down -- the cancelled X1 down is not sent, no X1 up either, the move goes without X1",
        flowed && t.count(2, 5) == 0 && t.count(3, 5) == 0 && t.xBits == 0 && moveBits == n::kMouseWireLeft, d);
  const size_t mark2 = h.InputCount();
  const bool parked = c.BeginPark() && wait_until([&] { return c.ParkPhase() == n::ClientParkPhase::Parked; }, 3000);
  const Tally t2 = tally(h.Inputs(), mark2);
  check("r4 orphan: nothing held or owed -- a park confirms with nothing to release", parked && t2.count(3, 5) == 0 &&
        c.ParkReleasedCount() == 0, t2.str() + " parkReleased=" + std::to_string(c.ParkReleasedCount()));
  if (parked) {
    (void)c.ResumeFromPark();
    (void)c.RequestStreamActive(true);
  }
  const size_t mark3 = h.InputCount();
  const bool d2 = c.QueueInputEvent(2, 20, 20, 0, 5, n::kMouseWireX1);
  const bool u2 = c.QueueInputEvent(3, 20, 20, 0, 5, 0);  // a quick pair, both queued: whole
  const bool sent2 = wait_until([&] { return h.InputCount() >= mark3 + 2; }, 3000);
  const Tally t3 = tally(h.Inputs(), mark3);
  check("r4 orphan: the next X1 press (a quick pair, both queued) goes whole", d2 && u2 && sent2 &&
        t3.count(2, 5) == 1 && t3.count(3, 5) == 1, t3.str());
  c.Disconnect();
}

// The ledger alone: the same transitions, both sides, no session.
void ledger_pure() {
  std::printf("\n--- r2: ClientMouseXPressLedger ---\n");
  using L = n::ClientMouseXPressLedger;
  n::ControlOutboundAction a{};
  a.kind = n::ControlOutboundActionKind::InputEvent;
  auto ev = [&](uint16_t kind, uint32_t key, uint16_t buttons) {
    a.inputEvent.kind = kind;
    a.inputEvent.keyCode = key;
    a.inputEvent.buttons = buttons;
    return &a;
  };
  {
    L l;
    uint16_t b = 0x8;
    const bool d = l.AdmitQueued(false, 2, 5, &b);
    uint16_t m = 0x9;
    const bool mv = l.AdmitQueued(true, 1, 0, &m);
    uint16_t u = 0;
    const bool up = l.AdmitQueued(true, 3, 5, &u);
    uint16_t d2b = 0x8;
    const bool d2 = l.AdmitQueued(true, 2, 5, &d2b);
    uint16_t u2b = 0;
    const bool u2 = l.AdmitQueued(true, 3, 5, &u2b);
    check("ledger entry: refused down (no X) -> X: move without X1, up refused; the next press goes",
          !d && mv && m == 0x1 && !up && d2 && d2b == 0x8 && u2 && !l.EntryRefused(5));
  }
  {
    L l;
    const bool d = l.AdmitSendAction(false, ev(2, 6, 0x10));
    const bool mv = l.AdmitSendAction(true, ev(1, 0, 0x11));
    const uint16_t mvb = a.inputEvent.buttons;
    const bool up = l.AdmitSendAction(true, ev(3, 6, 0));
    const bool d2 = l.AdmitSendAction(true, ev(2, 6, 0x10));
    l.OnSentAction(a);
    const bool owed = l.SendOwed(6);
    const bool u2 = l.AdmitSendAction(true, ev(3, 6, 0));
    l.OnSentAction(a);
    check("ledger send: a down dropped (no X) -> X: move without X2, up dropped; the next press goes and is owed until its up",
          !d && mv && mvb == 0x1 && !up && d2 && owed && u2 && !l.SendOwed(6));
  }
  {
    L l;
    (void)l.AdmitSendAction(true, ev(2, 5, 0x8));
    l.OnSentAction(a);
    const bool upNoX = l.AdmitSendAction(false, ev(3, 5, 0));
    const bool stillOwed = l.SendOwed(5);
    const bool upX = l.AdmitSendAction(true, ev(3, 5, 0));
    check("ledger send: a SENT down's release -- refused to a host without X (still owed, no exception), sent once X is back",
          !upNoX && stillOwed && upX);
  }
  {
    L l;
    uint16_t b = 0;
    (void)l.AdmitQueued(false, 2, 5, &b);
    (void)l.AdmitSendAction(false, ev(2, 6, 0));
    l.Reset();
    uint16_t u = 0;
    check("ledger reset: nothing refused or dropped remains", !l.EntryRefused(5) && !l.SendDropped(6) &&
          l.AdmitQueued(true, 3, 5, &u));
  }
  // r3 N1: a SENT down's debt is not hidden by a later down of the button refused at the entry / dropped at sending
  for (bool atEntry : {true, false}) {
    L l;
    uint16_t b = 0x8;
    (void)l.AdmitQueued(true, 2, 5, &b);
    (void)l.AdmitSendAction(true, ev(2, 5, 0x8));
    l.OnSentAction(a);  // sent + answered: owed
    bool later = false;
    if (atEntry) {
      uint16_t b2 = 0x8;
      later = l.AdmitQueued(false, 2, 5, &b2);  // refused at the entry (no X)
    } else {
      uint16_t b2 = 0x8;
      later = l.AdmitQueued(true, 2, 5, &b2) && !l.AdmitSendAction(false, ev(2, 5, 0x8));  // admitted, dropped
    }
    const bool owedKept = l.SendOwed(5);
    uint16_t mv = 0x9;
    const bool mvOk = l.AdmitQueued(true, 1, 0, &mv);  // the host holds X1: its bit is true, kept
    uint16_t u = 0;
    const bool upEntry = l.AdmitQueued(true, 3, 5, &u);
    const bool upSend = l.AdmitSendAction(true, ev(3, 5, 0));
    l.OnSentAction(a);
    const bool paid = !l.SendOwed(5);
    check(atEntry ? "ledger r3: owed X1, a later down refused at the entry -- the owed up still goes once X is back, then paid"
                  : "ledger r3: owed X1, a later down dropped at sending -- the owed up still goes once X is back, then paid",
          (atEntry ? !later : later) && owedKept && mvOk && mv == 0x9 && upEntry && upSend && paid);
    L k;  // the same, the up while still no X: refused at both fences, the debt kept (no exception)
    uint16_t c0 = 0x8;
    (void)k.AdmitQueued(true, 2, 5, &c0);
    (void)k.AdmitSendAction(true, ev(2, 5, 0x8));
    k.OnSentAction(a);
    uint16_t c1 = 0x8;
    if (atEntry) {
      (void)k.AdmitQueued(false, 2, 5, &c1);
    } else {
      (void)k.AdmitQueued(true, 2, 5, &c1);
      (void)k.AdmitSendAction(false, ev(2, 5, 0x8));
    }
    uint16_t c2 = 0;
    const bool upNoXEntry = k.AdmitQueued(false, 3, 5, &c2);
    const bool upNoXSend = k.AdmitSendAction(false, ev(3, 5, 0));
    check(atEntry ? "ledger r3: ...the up while still no X: refused at the entry and at sending, the debt kept"
                  : "ledger r3: ...(send path) the up while still no X: refused at both fences, the debt kept",
          !upNoXEntry && !upNoXSend && k.SendOwed(5));
  }
  // r4 N1: queued down, its up refused at the entry, X again before the sender: the down is cancelled
  {
    L l;
    uint16_t b = 0x8;
    const bool d = l.AdmitQueued(true, 2, 5, &b);
    l.NoteQueued(2, 5, 41);
    uint16_t u = 0;
    const bool up = l.AdmitQueued(false, 3, 5, &u);  // no X: refused, the queued down goes with it
    const bool cancelled = l.CancelledDowns(5) == 1 && l.PendingDowns(5) == 0;
    n::ControlOutboundAction& x = *ev(2, 5, 0x8);
    x.inputEvent.seq = 41;
    const bool sent = l.AdmitSendAction(true, &x);  // X again: the sender reaches it
    n::ControlOutboundAction& m = *ev(1, 0, 0x9);
    m.inputEvent.seq = 42;
    const bool mvOk = l.AdmitSendAction(true, &m);
    check("ledger r4: a queued down whose up was refused at the entry is cancelled at sending (X again or not), its "
          "bit hidden; not owed",
          d && !up && cancelled && !sent && mvOk && m.inputEvent.buttons == 0x1 && !l.SendOwed(5) && l.CancelledDowns(5) == 0);
  }
  {
    L l;  // a quick pair queued: its up admitted -- the down is NOT cancelled
    uint16_t b = 0x8, u = 0;
    (void)l.AdmitQueued(true, 2, 5, &b);
    l.NoteQueued(2, 5, 7);
    (void)l.AdmitQueued(true, 3, 5, &u);
    l.NoteQueued(3, 5, 8);
    n::ControlOutboundAction& x = *ev(2, 5, 0x8);
    x.inputEvent.seq = 7;
    const bool dn = l.AdmitSendAction(true, &x);
    l.OnSentAction(x);
    n::ControlOutboundAction& y = *ev(3, 5, 0);
    y.inputEvent.seq = 8;
    const bool upS = l.AdmitSendAction(true, &y);
    l.OnSentAction(y);
    check("ledger r4: a quick pair (down and up both queued) goes whole, nothing cancelled",
          dn && upS && !l.SendOwed(5) && l.CancelledDowns(5) == 0 && l.PendingDowns(5) == 0);
  }
  {
    L l;  // a down already TAKEN by the sender (in flight), its up refused: not cancellable -- owed once answered
    uint16_t b = 0x8, u = 0;
    (void)l.AdmitQueued(true, 2, 5, &b);
    l.NoteQueued(2, 5, 9);
    n::ControlOutboundAction& x = *ev(2, 5, 0x8);
    x.inputEvent.seq = 9;
    const bool dn = l.AdmitSendAction(true, &x);  // taken
    const bool up = l.AdmitQueued(false, 3, 5, &u);  // no X now: refused
    l.OnSentAction(x);  // the down's answer
    check("ledger r4: a down in flight when its up is refused stays a debt (owed), nothing cancelled",
          dn && !up && l.SendOwed(5) && l.CancelledDowns(5) == 0);
  }
  {
    L l;
    uint16_t b = 0;
    (void)l.AdmitQueued(false, 2, 5, &b);  // X1 refused
    uint16_t x2 = 0x10;
    const bool d2 = l.AdmitQueued(true, 2, 6, &x2);
    uint16_t lk = 0x1;
    const bool left = l.AdmitQueued(true, 2, 1, &lk);
    check("ledger: a refused X1 press does not touch X2 or the left button", d2 && x2 == 0x10 && left && lk == 0x1);
  }
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
  ledger_pure();
  unknown_to_x();
  x_to_nox_at_send();
  x_to_nox_owed();
  reconnect_forgets();
  for (bool atEntry : {true, false}) {
    for (bool xBack : {true, false}) owed_kept(atEntry, xBack);
  }
  queued_down_refused_up();
  for (bool takesX : {false, true}) {
    direct(takesX);
    macro(takesX);
    park(takesX);
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

// apk-decoder-liveness r2 (Codex codex_apk_decoder_liveness_design_r2.md, regression axis 2): an IDR lost while the
// decoder was NOT ready, and the recovery once it is -- through the PRODUCT shared session (ClientSessionController:
// its control loop with the owed-key retry and the keyframe limiter, its UDP receive loop, control over the video
// socket as on the relay path: controlOverUdp) delivering into the APK's decoder sink (android_video_decoder.cpp)
// against the stand-in NDK. The host is a fake on loopback: UDP HelloAck, video, and a UdpControlChannel (product
// timings) answering Ping, the window list, WindowSelect (a new stream generation; its first IDR sent at once) and
// RequestKeyFrame (an IDR of the CURRENT generation).
//
// The selection's IDR arrives before the decoder is ready. Ready comes LATER than the session's initial owed retries
// cover (8 asks, 1 s apart):
//   U1  no surface until --late-ms (the IDR is lost: KeyLost, owed)
//   U2  the surface is there, the codec's create is held until --late-ms
//   U4  no surface at the pick (lost, owed); the surface comes at 2 s; the create is held until --late-ms
//   U3  as U4, and a NEWER selection at half-time: nothing of the old generation is queued; the new one recovers
// each with a host that keeps sending deltas and an idle one (only the IDRs asked for).
// r4 (Codex codex_apk_decoder_liveness_src_r1.md N3): the codec's create FAILS (not slow) until ready, the host idle:
//   U5   failing until --late-ms: every IDR the 8 asks bring is spent on a failed create and the budget is gone; once
//        a create can succeed, the decoder's own bounded retry makes the codec and re-arms the owed IDR (a new token:
//        the same 8 asks) -- judged from ready like the others;
//   U5b  failing for 26 s, past the decoder's retries (20 x 1 s): the failure is TERMINAL (codecState failed_terminal,
//        logged) -- nothing recovers by itself and nothing is asked for 3 s after the create could succeed; then a
//        new surface (the explicit path) brings the codec back -- judged from that re-attach.
// The contract, measured from "ready": the current generation's IDR is the codec's first input (< 3.5 s) and a
// picture of it is presented (the selection becomes ready); control stays open on both ends throughout (no
// peer-lost). The keyframe requests before / after ready are recorded (the owed budget against the late decoder).
//
//   remote60_android_decoder_idr_recovery_test [--late-ms N]      (default 12000: beyond 8 x 1 s)
// Tag: network (loopback) + threads. No device, no real Host.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "android_video_decoder.hpp"
#include "fake_ndk.hpp"
#include "native_socket.hpp"
#include "native_video_client_session.hpp"
#include "native_video_client_shared_core.hpp"
#include "poc_protocol.hpp"
#include "udp_control_channel.hpp"

namespace n = remote60::native_poc;
using remote60::android_direct::AndroidVideoDecoderSink;
using n::MessageHeader;
using n::MessageType;
using n::SocketHandle;
using n::kInvalidSocket;
using Clock = std::chrono::steady_clock;

namespace {
int gChecks = 0, gFailures = 0;
void check(const std::string& what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}
bool wait_until(const std::function<bool()>& done, int ms) {
  const auto end = Clock::now() + std::chrono::milliseconds(ms);
  while (Clock::now() < end) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return done();
}
int64_t ms_between(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
}

uint8_t idr_tag(uint64_t gen) { return static_cast<uint8_t>(0x80u | (gen & 0x7fu)); }

std::vector<uint8_t> video_datagram(uint32_t seq, uint64_t gen, bool key) {
  std::vector<uint8_t> au;
  const uint8_t sc[] = {0, 0, 0, 1};
  auto put = [&](std::initializer_list<uint8_t> nal) {
    au.insert(au.end(), sc, sc + 4);
    au.insert(au.end(), nal.begin(), nal.end());
  };
  const uint8_t tag = key ? idr_tag(gen) : 0x01;
  if (key) {
    put({0x67, 0x42, 0x00, 0x28, 0xAB});
    put({0x68, 0xCE, 0x3C, 0x80});
    put({0x65, 0x88, tag, tag});
  } else {
    put({0x41, 0x9A, tag, tag});
  }
  n::UdpVideoChunkHeader h{};
  h.seq = seq;
  h.flags = static_cast<uint16_t>((key ? 0x1u : 0u) | 0x2u | 0x4u);
  h.width = 1280;
  h.height = 720;
  h.stride = 1280;
  h.payloadSize = static_cast<uint32_t>(au.size());
  h.chunkSize = static_cast<uint32_t>(au.size());
  h.chunkCount = 1;
  h.chunkStride = static_cast<uint32_t>(au.size());
  h.streamGeneration = gen;
  h.captureQpcUs = 1000000ull + seq * 33333ull;
  std::vector<uint8_t> d(sizeof(h) + au.size());
  std::memcpy(d.data(), &h, sizeof(h));
  std::memcpy(d.data() + sizeof(h), au.data(), au.size());
  return d;
}

template <typename T>
T decode_msg(const uint8_t* p) {
  T m{};
  std::memcpy(&m, p, sizeof(T));
  return m;
}

// The fake host: one UDP socket for the Hello, video and its control channel (as the relay path carries them).
struct FakeHost {
  bool sendDeltas = true;
  int videoPort = 0;
  std::atomic<bool> stop{false};
  std::atomic<uint64_t> generation{0};
  std::atomic<uint64_t> nextGeneration{7001};
  std::atomic<int> idrWanted{0};
  SocketHandle udp = kInvalidSocket;
  sockaddr_in peer{};
  std::atomic<bool> havePeer{false};
  std::thread thread;
  n::UdpControlChannel control;  // product timings
  std::mutex mu;
  std::vector<Clock::time_point> keyRequests;
  std::vector<Clock::time_point> pings;  // the client's keepalive, as it reaches the host
  std::atomic<bool> peerLost{false};

  bool Start() {
    std::string err;
    if (!n::initialize_sockets(&err)) return false;
    udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(udp, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) return false;
    sockaddr_in b{};
    int bl = sizeof(b);
    getsockname(udp, reinterpret_cast<sockaddr*>(&b), &bl);
    videoPort = ntohs(b.sin_port);
    DWORD t = 2;
    setsockopt(udp, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&t), sizeof(t));
    control.Configure(
        [this](const void* p, size_t len) {
          return sendto(udp, static_cast<const char*>(p), static_cast<int>(len), 0, reinterpret_cast<sockaddr*>(&peer),
                        sizeof(peer)) >= 0;
        },
        n::kUdpControlStreamHostToClient, n::kUdpControlStreamClientToHost, 1200);
    thread = std::thread([this] { Run(); });
    return true;
  }
  void Stop() {
    stop = true;
    if (thread.joinable()) thread.join();
    n::shutdown_socket(&udp);
  }
  ~FakeHost() { Stop(); }

  void SendMsg(const void* m, size_t len) { control.Send(m, len); }

  void Handle(const std::vector<uint8_t>& in) {
    size_t at = 0;
    while (at + sizeof(MessageHeader) <= in.size()) {
      MessageHeader h{};
      std::memcpy(&h, in.data() + at, sizeof(h));
      if (h.magic != n::kMagic || h.size < sizeof(h) || at + h.size > in.size()) return;
      const uint8_t* p = in.data() + at;
      const auto type = static_cast<MessageType>(h.type);
      if (type == MessageType::ControlPing && h.size == sizeof(n::ControlPingMessage)) {
        {
          std::lock_guard<std::mutex> l(mu);
          pings.push_back(Clock::now());
        }
        const auto ping = decode_msg<n::ControlPingMessage>(p);
        n::ControlPongMessage pong{};
        pong.header.magic = n::kMagic;
        pong.header.type = static_cast<uint16_t>(MessageType::ControlPong);
        pong.header.size = static_cast<uint16_t>(sizeof(pong));
        pong.seq = ping.seq;
        pong.clientSendQpcUs = ping.clientSendQpcUs;
        SendMsg(&pong, sizeof(pong));
      } else if (type == MessageType::ControlWindowListRequest && h.size == sizeof(n::ControlWindowListRequestMessage)) {
        const auto req = decode_msg<n::ControlWindowListRequestMessage>(p);
        n::ControlWindowListMessage list{};
        list.header.magic = n::kMagic;
        list.header.type = static_cast<uint16_t>(MessageType::ControlWindowList);
        list.header.size = static_cast<uint16_t>(sizeof(list));
        list.seq = req.seq;
        list.itemCount = 2;
        list.items[0].id = 1001;
        std::snprintf(list.items[0].title, sizeof(list.items[0].title), "%s", "Editor");
        list.items[1].id = 1002;
        std::snprintf(list.items[1].title, sizeof(list.items[1].title), "%s", "Terminal");
        SendMsg(&list, sizeof(list));
      } else if (type == MessageType::ControlWindowSelect && h.size == sizeof(n::ControlWindowSelectMessage)) {
        const auto req = decode_msg<n::ControlWindowSelectMessage>(p);
        n::ControlWindowSelectedMessage ans{};
        ans.header.magic = n::kMagic;
        ans.header.type = static_cast<uint16_t>(MessageType::ControlWindowSelected);
        ans.header.size = static_cast<uint16_t>(sizeof(ans));
        ans.seq = req.seq;
        ans.flags = 0x1u;
        ans.windowId = req.windowId;
        ans.streamGeneration = nextGeneration.fetch_add(1);
        std::snprintf(ans.title, sizeof(ans.title), "%s", "window");
        generation = ans.streamGeneration;
        idrWanted = 1;  // the selection's first IDR (a host sends it at once; it may beat the answer)
        SendMsg(&ans, sizeof(ans));
      } else if (type == MessageType::ControlRequestKeyFrame && h.size == sizeof(n::ControlRequestKeyFrameMessage)) {
        {
          std::lock_guard<std::mutex> l(mu);
          keyRequests.push_back(Clock::now());
        }
        idrWanted.fetch_add(1);
      }
      at += h.size;
    }
  }

  void Run() {
    std::vector<uint8_t> buf(2048), in;
    uint32_t seq = 0;
    auto nextVideo = Clock::now();
    while (!stop.load()) {
      sockaddr_in from{};
      int fl = sizeof(from);
      const int got = recvfrom(udp, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0,
                               reinterpret_cast<sockaddr*>(&from), &fl);
      if (got > 0) {
        if (n::UdpControlChannel::IsControlDatagram(buf.data(), static_cast<size_t>(got))) {
          control.OnPacket(buf.data(), static_cast<size_t>(got));
        } else if (got >= static_cast<int>(sizeof(n::UdpHelloPacket))) {
          n::UdpHelloPacket hello{};
          std::memcpy(&hello, buf.data(), sizeof(hello));
          if (hello.magic == n::kMagic && hello.kind == static_cast<uint16_t>(n::UdpPacketKind::Hello)) {
            peer = from;
            havePeer = true;
            n::UdpHelloPacket ack{};
            ack.kind = static_cast<uint16_t>(n::UdpPacketKind::HelloAck);
            sendto(udp, reinterpret_cast<const char*>(&ack), sizeof(ack), 0, reinterpret_cast<sockaddr*>(&peer),
                   sizeof(peer));
          }
        }
      }
      if (!havePeer.load()) continue;
      control.Tick();
      while (control.Receive(&in, 0)) Handle(in);
      if (control.IsClosed() && control.CloseReason() == n::ControlCloseReason::PeerLost) peerLost = true;
      const uint64_t gen = generation.load();
      if (gen != 0 && Clock::now() >= nextVideo) {
        nextVideo = Clock::now() + std::chrono::milliseconds(33);
        int want = idrWanted.load();
        const bool key = want > 0 && idrWanted.compare_exchange_strong(want, want - 1);
        if (key || sendDeltas) {
          const auto d = video_datagram(seq++, gen, key);
          sendto(udp, reinterpret_cast<const char*>(d.data()), static_cast<int>(d.size()), 0,
                 reinterpret_cast<sockaddr*>(&peer), sizeof(peer));
        }
      }
    }
  }

  // The longest the client's keepalive was silent at the host within [a, b].
  int64_t PingGapMs(Clock::time_point a, Clock::time_point b) {
    std::lock_guard<std::mutex> l(mu);
    Clock::time_point last = a;
    int64_t gap = 0;
    for (auto t : pings) {
      if (t < a || t > b) continue;
      gap = std::max<int64_t>(gap, ms_between(last, t));
      last = t;
    }
    return std::max<int64_t>(gap, ms_between(last, b));
  }
  size_t RequestsBetween(Clock::time_point a, Clock::time_point b) {
    std::lock_guard<std::mutex> l(mu);
    size_t c = 0;
    for (auto t : keyRequests) c += (t >= a && t <= b) ? 1 : 0;
    return c;
  }
};

size_t queued_count() {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  return fake_ndk::controls().queued.size();
}
std::vector<uint8_t> queued_tags_from(size_t from) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  const auto& q = fake_ndk::controls().queued;
  std::vector<uint8_t> out;
  for (size_t i = from; i < q.size(); ++i) out.push_back(q[i].empty() ? 0 : q[i].back());
  return out;
}

struct Case {
  const char* name;
  int surfaceAtMs;   // -1: before the pick; else ms after the pick
  bool holdCreate;   // the codec's create held until lateMs
  bool hostDeltas;
  bool newer;        // a newer selection at lateMs / 2
  bool failCreate = false;  // r4 N3: the codec's create FAILS until ready (instead of being held)
  bool terminal = false;    // r4 N3: ...for longer than the decoder's own retries; then a re-attach
  int lateOverrideMs = 0;   // this case's own "ready" (0: --late-ms)
};

struct Outcome {
  bool connected = false, selected = false, controlLost = false;
  size_t asksBefore = 0, asksAfter = 0;
  int64_t pingGapMs = -1;  // the longest keepalive silence at the host while the decoder was not ready
  int64_t queuedMs = -1, presentMs = -1;
  bool firstIsCurrentIdr = false, oldGenQueued = false;
  // r4 N3 (U5b): the state before ready, and what happened in the 3 s after the create could succeed again
  bool terminalBefore = false, selfRecovered = false;
  size_t asksIdle = 0;
  std::string detail;
};

Outcome run_case(const Case& k, int lateMs) {
  Outcome o;
  if (k.lateOverrideMs > 0) lateMs = k.lateOverrideMs;
  FakeHost host;
  host.sendDeltas = k.hostDeltas;
  if (!host.Start()) return o;
  n::ClientSessionController c;
  AndroidVideoDecoderSink sink;
  JNIEnv* env = nullptr;
  int surface = 1, surface2 = 2;
  if (k.failCreate) {
    std::lock_guard<std::mutex> lk(fake_ndk::mu());
    fake_ndk::controls().createFails = true;
  }
  if (k.holdCreate) {
    fake_ndk::barrier().which.store(fake_ndk::kHoldCreate);
    fake_ndk::barrier().held.store(true);
  }
  std::atomic<bool> letGo{false};
  std::thread safety([&] {  // whatever blocks (the old sink holds the receive loop), the hold ends
    const auto t0 = Clock::now();
    while (!letGo.load() && ms_between(t0, Clock::now()) < lateMs + 20000)
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    fake_ndk::barrier().held.store(false);
  });
  if (k.surfaceAtMs < 0) sink.SetSurface(env, &surface);
  n::ClientSessionConnectArgs a{};
  a.host = "127.0.0.1";
  a.videoPort = host.videoPort;
  a.controlPort = host.videoPort;  // must be a valid port; not used (no TCP control, as on the relay path)
  a.requireTcpControl = false;
  a.controlOverUdp = true;
  a.controlIntervalMs = 50;
  a.encodedFrameSink = &sink;
  o.connected = c.Connect(a) && wait_until([&] {
    const auto s = c.Snapshot();
    return s.state == n::ClientSessionState::Connected && s.controlLoopActive;
  }, 5000);
  const uint64_t firstGen = host.nextGeneration.load();
  uint64_t currentGen = firstGen;
  uint64_t localGen = 1;
  if (o.connected) {
    wait_until([&] { return !c.WindowPanelSnapshotCopy().items.empty(); }, 3000);
    sink.PrepareForWindowSelection(1);  // the APK's order: the sink prepared for the pick, then the session asks
    o.selected = c.RequestWindowSelect(1001);
  }
  const auto t0 = Clock::now();
  bool surfaceSet = k.surfaceAtMs < 0, newerDone = false;
  // The decoder is not ready until lateMs (no surface and / or a held create).
  while (o.connected && ms_between(t0, Clock::now()) < lateMs) {
    const int64_t el = ms_between(t0, Clock::now());
    if (!surfaceSet && k.surfaceAtMs >= 0 && k.holdCreate && el >= k.surfaceAtMs) {
      sink.SetSurface(env, &surface);  // U4: the surface comes; its codec is held
      surfaceSet = true;
    }
    if (k.newer && !newerDone && el >= lateMs / 2) {
      sink.PrepareForWindowSelection(2);
      c.RequestWindowSelect(1002);
      currentGen = firstGen + 1;
      localGen = 2;
      newerDone = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  const size_t q0 = queued_count();
  auto ready = Clock::now();
  o.asksBefore = host.RequestsBetween(t0, ready);
  o.pingGapMs = host.PingGapMs(t0, ready);
  o.terminalBefore = sink.DebugStatus().find(" codecState=failed_terminal") != std::string::npos;
  if (k.failCreate) {
    std::lock_guard<std::mutex> lk(fake_ndk::mu());
    fake_ndk::controls().createFails = false;  // a create can succeed from here
  }
  if (!surfaceSet) sink.SetSurface(env, &surface);  // U1: the surface comes now
  letGo = true;
  fake_ndk::barrier().held.store(false);
  if (k.terminal) {
    // U5b: terminal -- nothing by itself for 3 s (no codec, no IDR in, no ask); then the explicit path: a new surface
    const auto idle0 = Clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    for (uint8_t t : queued_tags_from(q0)) o.selfRecovered = o.selfRecovered || t == idr_tag(currentGen);
    o.asksIdle = host.RequestsBetween(idle0, Clock::now());
    sink.SetSurface(env, &surface2);
    ready = Clock::now();
  }
  // From ready: the first time the current IDR is queued, then (one picture decoded) the selection ready.
  bool pushed = false;
  while (o.connected && ms_between(ready, Clock::now()) < 5000 && o.presentMs < 0) {
    const auto tags = queued_tags_from(q0);
    if (o.queuedMs < 0) {
      for (uint8_t t : tags) {
        if (t == idr_tag(currentGen)) {
          o.queuedMs = ms_between(ready, Clock::now());
          break;
        }
      }
    }
    if (o.queuedMs >= 0 && !pushed) {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::Controls::Output out;
      out.presentationTimeUs = 1;
      fake_ndk::controls().outputs.push_back(out);
      pushed = true;
    }
    if (pushed && sink.ReadySelectionGeneration() == localGen) o.presentMs = ms_between(ready, Clock::now());
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  const auto tags = queued_tags_from(q0);
  o.firstIsCurrentIdr = !tags.empty() && tags.front() == idr_tag(currentGen);
  for (uint8_t t : tags) o.oldGenQueued = o.oldGenQueued || (k.newer && t == idr_tag(firstGen));
  o.asksAfter = host.RequestsBetween(ready, Clock::now());
  const auto snap = c.Snapshot();
  o.controlLost = host.peerLost.load() || snap.state != n::ClientSessionState::Connected;
  // The test-build progress trace (GNLINK_LIVENESS_TRACE is defined for this target): one line at the end.
  std::printf("  trace: %s\n", c.LivenessTraceLine().c_str());  // the session's (the sink under test may be an old one)
  o.detail = "pingGap=" + std::to_string(o.pingGapMs) + "ms asks before ready=" + std::to_string(o.asksBefore) + " after=" + std::to_string(o.asksAfter) +
             (k.terminal ? " terminalBefore=" + std::to_string(o.terminalBefore ? 1 : 0) + " idleAsks=" +
                               std::to_string(o.asksIdle) + " selfRecovered=" + std::to_string(o.selfRecovered ? 1 : 0)
                         : std::string()) +
             " queued+" + std::to_string(o.queuedMs) + "ms present+" + std::to_string(o.presentMs) +
             "ms first=" + (tags.empty() ? std::string("none") : std::to_string(tags.front())) +
             " hostPeerLost=" + std::to_string(host.peerLost.load() ? 1 : 0) +
             " clientConnected=" + std::to_string(snap.state == n::ClientSessionState::Connected ? 1 : 0);
  letGo = true;
  safety.join();
  c.Disconnect();
  sink.SetSurface(env, nullptr);
  {
    std::lock_guard<std::mutex> lk(fake_ndk::mu());
    fake_ndk::controls().outputs.clear();
  }
  return o;
}

void judge(const Case& k, const Outcome& o) {
  const std::string name = k.name;
  check(name + ": connected (control over the video socket) and the pick sent", o.connected && o.selected);
  check(name + ": control stayed open on both ends (no peer-lost)", !o.controlLost, o.detail);
  check(name + ": the client's keepalive kept reaching the host while the decoder was not ready (gap < 1000 ms)",
        o.pingGapMs >= 0 && o.pingGapMs < 1000, o.detail);
  check(name + ": the CURRENT generation's IDR is the codec's first input (< 3.5 s after ready)",
        o.queuedMs >= 0 && o.queuedMs < 3500 && o.firstIsCurrentIdr, o.detail);
  check(name + ": a picture of it is presented -- the selection is ready", o.presentMs >= 0 && o.presentMs < 4000, o.detail);
  if (k.newer) check(name + ": nothing of the superseded generation was queued", !o.oldGenQueued, o.detail);
  if (k.failCreate && !k.terminal) {
    check(name + ": the owed budget was spent on failed creates before ready (>= 8 asks)", o.asksBefore >= 8, o.detail);
  }
  if (k.terminal) {
    check(name + ": past the decoder's retries the failure is TERMINAL (codecState=failed_terminal)", o.terminalBefore,
          o.detail);
    check(name + ": terminal means terminal: nothing recovered by itself, nothing asked, for 3 s",
          !o.selfRecovered && o.asksIdle == 0, o.detail);
  }
}
}  // namespace

int main(int argc, char** argv) {
  int lateMs = 12000;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--late-ms") == 0) lateMs = std::atoi(argv[i + 1]);
  }
  std::printf("android_decoder_idr_recovery_test  late=%d ms (owed retries: 8 x 1 s; control budget 24 x 250 ms)\n",
              lateMs);
  const Case cases[] = {
      {"U1 deltas", lateMs, false, true, false},  {"U1 idle", lateMs, false, false, false},
      {"U2 deltas", -1, true, true, false},       {"U2 idle", -1, true, false, false},
      {"U4 deltas", 2000, true, true, false},     {"U4 idle", 2000, true, false, false},
      {"U3 newer idle", 2000, true, false, true},
      // r4 N3
      {"U5 create fails idle", -1, false, false, false, true, false, 0},
      {"U5b create fails past the retries idle, then a new surface", -1, false, false, false, true, true, 26000},
  };
  for (const auto& k : cases) {
    std::printf("\n--- %s\n", k.name);
    judge(k, run_case(k, lateMs));
  }
  {
    std::lock_guard<std::mutex> lk(fake_ndk::mu());
    fake_ndk::controls().createFails = false;
  }
  std::printf("\nandroid_decoder_idr_recovery_test: %d checks, %d failed\nRESULT: %s\n", gChecks, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}

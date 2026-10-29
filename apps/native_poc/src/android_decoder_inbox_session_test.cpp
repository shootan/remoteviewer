// apk-inbox C (design caplife/design_apk_decoder_inbox_r2.md r3 §3-§4; Codex codex_apk_inbox_c_impl_go_r1.md): the
// recovery episode and the D8 key-request descriptors through the PRODUCT shared session (ClientSessionController:
// its UDP receive loop, its control loop with the limiter, control over the video socket as on the relay path) into
// the APK's decoder sink (android_video_decoder.cpp) through its binding adapter (entry_sink_adapter.hpp), against the
// stand-in NDK. The host is a fake on loopback (as android_decoder_idr_recovery_test's): video at 30 fps, a
// UdpControlChannel with product timings; it records every RequestKeyFrame it receives.
//   [S1] a chain loss while the host answers no key request: the HOST receives exactly 4 at ~0 / 1 / 3 / 7 s, control
//        stays open, the episode ends exhausted; the host's next IDR still decodes and ends it
//   [S2] D8, the ACCEPTED point: the limiter took the request; right before the exchange another PC is bound (this one
//        parked) -- the request is NOT sent (the host receives nothing), counted as dropped stale
//   [S3] D8, the HELD point: the limiter refuses the retry (it is held); before it goes to the limiter again another PC
//        is bound -- dropped; the host receives only the first request, even after the limiter would take it
// The race is put exactly at the point by the session's test hook (REMOTE60_CLIENT_SESSION_TEST_HOOKS, test builds).
//
//   remote60_android_decoder_inbox_session_test
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
#include "entry_sink_adapter.hpp"
#include "fake_ndk.hpp"
#include "native_socket.hpp"
#include "native_video_client_session.hpp"
#include "native_video_client_shared_core.hpp"
#include "poc_protocol.hpp"
#include "udp_control_channel.hpp"

namespace n = remote60::native_poc;
using remote60::android_direct::AndroidVideoDecoderSink;
using remote60::android_direct::EntrySinkAdapter;
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
uint64_t field(const std::string& status, const char* key) {
  const std::string k = std::string(" ") + key + "=";
  const size_t at = status.find(k);
  return at == std::string::npos ? ~0ull : std::strtoull(status.c_str() + at + k.size(), nullptr, 10);
}
std::string sfield(const std::string& status, const char* key) {
  const std::string k = std::string(" ") + key + "=";
  const size_t at = status.find(k);
  if (at == std::string::npos) return "?";
  const size_t from = at + k.size();
  return status.substr(from, status.find(' ', from) - from);
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

// The fake host (as android_decoder_idr_recovery_test's), plus: it may ignore key requests (records them only).
struct FakeHost {
  int videoPort = 0;
  std::atomic<bool> stop{false};
  std::atomic<uint64_t> generation{0};
  std::atomic<uint64_t> nextGeneration{7001};
  std::atomic<int> idrWanted{0};
  std::atomic<bool> ignoreKeyRequests{false};
  SocketHandle udp = kInvalidSocket;
  sockaddr_in peer{};
  std::atomic<bool> havePeer{false};
  std::thread thread;
  n::UdpControlChannel control;
  std::mutex mu;
  std::vector<Clock::time_point> keyRequests;
  std::vector<Clock::time_point> pings;
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
        list.itemCount = 1;
        list.items[0].id = 1001;
        std::snprintf(list.items[0].title, sizeof(list.items[0].title), "%s", "Editor");
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
        SendMsg(&ans, sizeof(ans));  // the answer first, then the selection's IDR on the next video tick
        generation = ans.streamGeneration;
        idrWanted = 1;
      } else if (type == MessageType::ControlRequestKeyFrame && h.size == sizeof(n::ControlRequestKeyFrameMessage)) {
        {
          std::lock_guard<std::mutex> l(mu);
          keyRequests.push_back(Clock::now());
        }
        if (!ignoreKeyRequests.load()) idrWanted.fetch_add(1);
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
        const auto d = video_datagram(seq++, gen, key);
        sendto(udp, reinterpret_cast<const char*>(d.data()), static_cast<int>(d.size()), 0,
               reinterpret_cast<sockaddr*>(&peer), sizeof(peer));
      }
    }
  }
  std::vector<int64_t> RequestsSinceMs(Clock::time_point t0) {
    std::lock_guard<std::mutex> l(mu);
    std::vector<int64_t> out;
    for (auto t : keyRequests)
      if (t >= t0) out.push_back(ms_between(t0, t));
    return out;
  }
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

// One PC (binding token `token`) connected, its window picked and its first IDR in the codec.
struct Rig {
  FakeHost host;
  n::ClientSessionController c;
  AndroidVideoDecoderSink sink;
  EntrySinkAdapter a{&sink};
  JNIEnv* env = nullptr;
  int surface = 1;
  uint64_t token = 0;
  bool ready = false;

  bool Up(uint64_t t) {
    token = t;
    if (!host.Start()) return false;
    sink.Bind(token);
    a.SetToken(token);
    sink.SetSurface(env, &surface);
    n::ClientSessionConnectArgs args{};
    args.host = "127.0.0.1";
    args.videoPort = host.videoPort;
    args.controlPort = host.videoPort;
    args.requireTcpControl = false;
    args.controlOverUdp = true;
    args.controlIntervalMs = 50;
    args.encodedFrameSink = &a;
    if (!c.Connect(args)) return false;
    if (!wait_until([&] {
          const auto s = c.Snapshot();
          return s.state == n::ClientSessionState::Connected && s.controlLoopActive;
        }, 5000))
      return false;
    wait_until([&] { return !c.WindowPanelSnapshotCopy().items.empty(); }, 3000);
    const size_t q0 = queued_count();
    sink.PrepareForWindowSelectionAs(token, 1);
    if (!c.RequestWindowSelect(1001)) return false;
    ready = wait_until([&] {
      for (uint8_t t : queued_tags_from(q0))
        if (t == idr_tag(host.nextGeneration.load() - 1)) return true;
      return false;
    }, 5000) && wait_until([&] { return a.KeyframeOwedFor() == 0; }, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // startup's own asks settled
    return ready;
  }
  // A chain loss: the codec's input dequeue held while the host's deltas keep coming (~21 > 8 + 1).
  void Loss() {
    fake_ndk::barrier().which.store(fake_ndk::kHoldDequeueInput);
    fake_ndk::barrier().held.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    fake_ndk::barrier().held.store(false);
  }
  void Down() {
    c.SetTestKeyRequestHook(nullptr);
    c.Disconnect();
    sink.SetSurface(env, nullptr);
    host.Stop();
  }
};

std::string list(const std::vector<int64_t>& v) {
  std::string s;
  for (int64_t x : v) s += (s.empty() ? "" : ",") + std::to_string(x);
  return s.empty() ? "none" : s;
}
}  // namespace

int main() {
  std::printf("android_decoder_inbox_session_test (apk-inbox C: the episode and D8 through the product session)\n");

  std::puts("\n--- [S1] a chain loss, the host answers no key request: 4 requests at ~0/1/3/7 s, then exhausted");
  {
    Rig r;
    const bool up = r.Up(11);
    check("[S1] connected, the window picked, its first IDR in the codec", up);
    if (up) {
      r.host.ignoreKeyRequests = true;
      const auto t0 = Clock::now();
      r.Loss();
      std::this_thread::sleep_for(std::chrono::milliseconds(12800));
      const auto reqs = r.host.RequestsSinceMs(t0);
      const std::string st = r.sink.DebugStatus();
      std::vector<int64_t> rel;
      for (int64_t x : reqs) rel.push_back(reqs.empty() ? 0 : x - reqs.front());
      auto close_to = [&](size_t i, int64_t ms) { return i < rel.size() && std::llabs(rel[i] - ms) <= 400; };
      check("[S1] the HOST received exactly 4 key requests, ~0 / 1 / 3 / 7 s apart from the first",
            reqs.size() == 4 && close_to(1, 1000) && close_to(2, 3000) && close_to(3, 7000),
            "at ms after the loss=" + list(reqs) + " relative=" + list(rel));
      check("[S1] the episode ended exhausted; one episode", sfield(st, "recovery") == "exhausted" &&
            field(st, "episodes") == 1 && field(st, "recSent") == 4, "recovery=" + sfield(st, "recovery") + " sent=" +
            std::to_string(field(st, "recSent")));
      check("[S1] control stayed open; the keepalive kept reaching the host (gap < 1000 ms)",
            !r.host.peerLost.load() && r.host.PingGapMs(t0, Clock::now()) < 1000,
            "pingGap=" + std::to_string(r.host.PingGapMs(t0, Clock::now())) + "ms");
      const size_t q0 = queued_count();
      r.host.idrWanted = 1;  // the host's periodic IDR
      const bool decoded = wait_until([&] {
        for (uint8_t t : queued_tags_from(q0))
          if (t == idr_tag(r.host.generation.load())) return true;
        return false;
      }, 2000);
      wait_until([&] { return sfield(r.sink.DebugStatus(), "recovery") == "idle"; }, 1000);
      check("[S1] exhausted blocks nothing: the host's next IDR is decoded and ends the state", decoded &&
            sfield(r.sink.DebugStatus(), "recovery") == "idle");
    }
    r.Down();
  }

  std::puts("\n--- [S2] D8 at the ACCEPTED point: another PC bound right before the exchange -- not sent");
  {
    Rig r;
    const bool up = r.Up(21);
    check("[S2] connected and ready", up);
    if (up) {
      r.host.ignoreKeyRequests = true;
      std::atomic<int> fired{0};
      r.c.SetTestKeyRequestHook([&](const char* point) {
        if (std::strcmp(point, "accepted") == 0 && fired.fetch_add(1) == 0) r.sink.Bind(22);  // this PC parked
      });
      const uint64_t dropped0 = r.c.KeyRequestsDroppedStale();
      const auto t0 = Clock::now();
      r.Loss();
      std::this_thread::sleep_for(std::chrono::milliseconds(2500));
      const auto reqs = r.host.RequestsSinceMs(t0);
      check("[S2] the accepted request was judged at the send point and NOT sent (the host received none)",
            fired.load() >= 1 && reqs.empty() && r.c.KeyRequestsDroppedStale() > dropped0,
            "hook=" + std::to_string(fired.load()) + " host requests=" + list(reqs) + " droppedStale=" +
                std::to_string(r.c.KeyRequestsDroppedStale() - dropped0));
    }
    r.Down();
  }

  std::puts("\n--- [S3] D8 at the HELD point: the retry refused by the limiter (min interval), another PC bound before it goes again");
  {
    Rig r;
    const bool up = r.Up(31);
    check("[S3] connected and ready", up);
    if (up) {
      r.host.ignoreKeyRequests = true;
      // the limiter's minimum interval: 3 s between taken requests (tokens plenty) -- the +1 s retry is refused (held),
      // the one after it would be taken at +3 s. (r1 used one token per 1.5 s: 2 of 3 runs took the +1 s retry.)
      r.c.TestConfigureKeyframeLimiter(3000000, 1, 100);
      // past one whole interval: a request of the setup (the pick's own asks) must not refuse the loss's FIRST one
      std::this_thread::sleep_for(std::chrono::milliseconds(3500));
      std::atomic<int> fired{0}, acceptedAfter{0};
      r.c.SetTestKeyRequestHook([&](const char* point) {
        if (std::strcmp(point, "held") == 0 && fired.fetch_add(1) == 0) r.sink.Bind(32);
        // after the held drop nothing of this PC may reach the send point: dropped at the HELD point, not later at the
        // accepted one (that would be the limiter charged for it -- the held check missing, the send check catching it)
        if (std::strcmp(point, "accepted") == 0 && fired.load() > 0) acceptedAfter.fetch_add(1);
      });
      const uint64_t dropped0 = r.c.KeyRequestsDroppedStale();
      const auto t0 = Clock::now();
      r.Loss();
      std::this_thread::sleep_for(std::chrono::milliseconds(4500));  // past the limiter's next token
      const auto reqs = r.host.RequestsSinceMs(t0);
      check("[S3] the first request went; the held retry was dropped AT the held point (never re-entered the limiter, "
            "never reached the send point) and never sent",
            fired.load() >= 1 && reqs.size() == 1 && r.c.KeyRequestsDroppedStale() > dropped0 && acceptedAfter.load() == 0,
            "hook=" + std::to_string(fired.load()) + " host requests=" + list(reqs) + " droppedStale=" +
                std::to_string(r.c.KeyRequestsDroppedStale() - dropped0) + " accepted after the held drop=" +
                std::to_string(acceptedAfter.load()));
    }
    r.Down();
  }

  std::printf("\nandroid_decoder_inbox_session_test: %d checks, %d failed\nRESULT: %s\n", gChecks, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}

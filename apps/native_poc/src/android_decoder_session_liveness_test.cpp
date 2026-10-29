// apk-decoder-liveness r1 (Codex codex_apk_decoder_liveness_design_r1.md (3), contrast 2): the PRODUCT receive path
// with the PRODUCT decoder sink, while the codec is slow -- over a real loopback socket.
//
//   client: ClientSessionController::VideoReceiveMain (RunVideoReceiveForTest) -- the real receive loop that also
//           carries the UDP control channel (OnPacket / Tick) -- delivering into the APK's AndroidVideoDecoderSink
//           (android_video_decoder.cpp) built against the stand-in NDK; its UdpControlChannel with the product's
//           default timings (250 ms x 24 = a 6 s budget). A "control" thread stands in for the session's worker: it
//           reads the control messages, sends one every 250 ms, and calls the sink's hooks as the worker does.
//   host:   a fake -- its own UdpControlChannel (default timings), a message every 250 ms, and H.264 single-chunk
//           frames at 30 fps (an IDR every second) on the same socket.
//
// Phases: [A] 3 s healthy (no surface yet: the codec cannot exist); [B] the UI sets the surface while the codec's
// CREATE is held for --hold-ms (8000, longer than the budget); [C] after it lets go.
// The contract: control keeps flowing both ways through B -- every message ACKed, no peer-lost on either end; after
// B the codec's first input is an IDR. NEGATIVE: built against the sink of 9f9a91c (REMOTE60_LIVENESS_SINK_DIR) the
// receive loop is held inside the sink, the host's messages go unACKed, and the host declares peer-lost.
// Tag: network (loopback) + threads. No device, no real Host.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "android_video_decoder.hpp"
#include "fake_ndk.hpp"
#include "native_video_client_session.hpp"
#include "native_video_client_shared_core.hpp"
#include "poc_protocol.hpp"
#include "udp_control_channel.hpp"

using remote60::android_direct::AndroidVideoDecoderSink;
using namespace remote60::native_poc;
using Clock = std::chrono::steady_clock;

namespace {
int gFailures = 0, gChecks = 0;
void check(const std::string& name, bool cond, const std::string& detail = {}) {
  ++gChecks;
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
  if (!cond) ++gFailures;
}
int64_t ms_since(Clock::time_point t) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

SOCKET make_loopback_udp(sockaddr_in* bound) {
  SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
  int len = sizeof(*bound);
  getsockname(s, reinterpret_cast<sockaddr*>(bound), &len);
  return s;
}

// One H.264 AU in one chunk. IDR: SPS + PPS + IDR slice; else one non-IDR slice. Its last byte is `tag`.
std::vector<uint8_t> video_datagram(uint32_t seq, bool key, uint8_t tag) {
  std::vector<uint8_t> au;
  const uint8_t sc[] = {0, 0, 0, 1};
  auto put = [&](std::initializer_list<uint8_t> nal) {
    au.insert(au.end(), sc, sc + 4);
    au.insert(au.end(), nal.begin(), nal.end());
  };
  if (key) {
    put({0x67, 0x42, 0x00, 0x28, 0xAB});
    put({0x68, 0xCE, 0x3C, 0x80});
    put({0x65, 0x88, tag, tag});
  } else {
    put({0x41, 0x9A, tag, tag});
  }
  UdpVideoChunkHeader h{};
  h.seq = seq;
  h.flags = static_cast<uint16_t>((key ? 0x1u : 0u) | 0x2u | 0x4u);
  h.width = 1280;
  h.height = 720;
  h.stride = 1280;
  h.payloadSize = static_cast<uint32_t>(au.size());
  h.chunkSize = static_cast<uint32_t>(au.size());
  h.chunkCount = 1;
  h.chunkStride = static_cast<uint32_t>(au.size());
  h.captureQpcUs = 1000000ull + seq * 33333ull;
  std::vector<uint8_t> d(sizeof(h) + au.size());
  std::memcpy(d.data(), &h, sizeof(h));
  std::memcpy(d.data() + sizeof(h), au.data(), au.size());
  return d;
}

// Arrival times of one direction's messages: the longest silence, and whether it was ever closed.
struct Arrivals {
  std::mutex m;
  std::vector<Clock::time_point> at;
  void add() {
    std::lock_guard<std::mutex> lk(m);
    at.push_back(Clock::now());
  }
  int64_t max_gap_ms(Clock::time_point from, Clock::time_point to) {
    std::lock_guard<std::mutex> lk(m);
    Clock::time_point last = from;
    int64_t gap = 0;
    for (auto t : at) {
      if (t < from || t > to) continue;
      gap = std::max<int64_t>(gap, std::chrono::duration_cast<std::chrono::milliseconds>(t - last).count());
      last = t;
    }
    return std::max<int64_t>(gap, std::chrono::duration_cast<std::chrono::milliseconds>(to - last).count());
  }
  size_t count(Clock::time_point from, Clock::time_point to) {
    std::lock_guard<std::mutex> lk(m);
    size_t n = 0;
    for (auto t : at) n += (t >= from && t <= to) ? 1 : 0;
    return n;
  }
};

std::vector<uint8_t> queued_tags_from(size_t from) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  const auto& q = fake_ndk::controls().queued;
  std::vector<uint8_t> out;
  for (size_t i = from; i < q.size(); ++i) out.push_back(q[i].empty() ? 0 : q[i].back());
  return out;
}
size_t queued_count() {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  return fake_ndk::controls().queued.size();
}
}  // namespace

int main(int argc, char** argv) {
  int holdMs = 8000;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--hold-ms") == 0) holdMs = std::atoi(argv[i + 1]);
  }
  const UdpControlChannel::Timings product;
  std::printf("android_decoder_session_liveness_test  hold=%d ms  control budget=%u x %llu ms = %llu ms\n", holdMs,
              product.maxAttempts, static_cast<unsigned long long>(product.retransmitIntervalUs / 1000),
              static_cast<unsigned long long>(product.maxAttempts * product.retransmitIntervalUs / 1000));
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa)) return 1;

  sockaddr_in clientAddr{}, hostAddr{};
  SOCKET cs = make_loopback_udp(&clientAddr);
  SOCKET hs = make_loopback_udp(&hostAddr);
  connect(cs, reinterpret_cast<sockaddr*>(&hostAddr), sizeof(hostAddr));
  connect(hs, reinterpret_cast<sockaddr*>(&clientAddr), sizeof(clientAddr));
  DWORD hostTimeout = 2;
  setsockopt(hs, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&hostTimeout), sizeof(hostTimeout));

  // ---- the client: the product receive loop + the APK sink
  ClientSessionController c;
  AndroidVideoDecoderSink sink;
  c.SetControlOverUdpForTest(true);
  UdpControlChannel& ctl = c.ControlChannelForTest();
  ctl.Configure([&](const void* p, size_t n) { return send(cs, static_cast<const char*>(p), static_cast<int>(n), 0) >= 0; },
                /*tx=*/1, /*rx=*/2, /*mtu=*/1200);

  // ---- the host
  UdpControlChannel host;
  host.Configure([&](const void* p, size_t n) { return send(hs, static_cast<const char*>(p), static_cast<int>(n), 0) >= 0; },
                 /*tx=*/2, /*rx=*/1, /*mtu=*/1200);
  std::mutex hostSendMu;  // video and control share the host socket

  Arrivals h2c, c2h;  // host -> client control messages read by the client; client -> host read by the host
  std::atomic<bool> stop{false};
  std::atomic<bool> hostClosedPeerLost{false}, clientClosedPeerLost{false};

  std::thread rx([&] { c.RunVideoReceiveForTest(static_cast<SocketHandle>(cs), &sink); });

  std::thread hostThread([&] {
    uint32_t seq = 0, msg = 0;
    auto nextVideo = Clock::now(), nextMsg = Clock::now();
    std::vector<uint8_t> buf(2048), in;
    while (!stop.load()) {
      const int n = recv(hs, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
      if (n > 0) host.OnPacket(buf.data(), static_cast<size_t>(n));
      host.Tick();
      while (host.Receive(&in, 0)) c2h.add();
      if (host.IsClosed() && host.CloseReason() == ControlCloseReason::PeerLost) hostClosedPeerLost.store(true);
      const auto now = Clock::now();
      if (now >= nextMsg && !host.IsClosed()) {
        const std::string m = "h2c-" + std::to_string(msg++);
        host.Send(m.data(), m.size());
        nextMsg = now + std::chrono::milliseconds(250);
      }
      if (now >= nextVideo) {
        const bool key = (seq % 30) == 0;
        const auto d = video_datagram(seq, key, key ? static_cast<uint8_t>(0x80 | ((seq / 30) & 0x7f)) : 0x01);
        ++seq;
        send(hs, reinterpret_cast<const char*>(d.data()), static_cast<int>(d.size()), 0);
        nextVideo = now + std::chrono::milliseconds(33);
      }
    }
  });

  // The session worker's part: reads control, sends its own, calls the sink's hooks.
  std::thread controlThread([&] {
    uint32_t msg = 0;
    auto nextMsg = Clock::now();
    std::vector<uint8_t> in;
    while (!stop.load()) {
      if (ctl.Receive(&in, 20)) h2c.add();
      if (ctl.IsClosed() && ctl.CloseReason() == ControlCloseReason::PeerLost) clientClosedPeerLost.store(true);
      if (Clock::now() >= nextMsg && !ctl.IsClosed()) {
        const std::string m = "c2h-" + std::to_string(msg++);
        ctl.Send(m.data(), m.size());
        nextMsg = Clock::now() + std::chrono::milliseconds(250);
      }
      (void)sink.KeyframeOwedFor();
      (void)sink.ConsumeDecoderKeyframeRequest();
      ClientDecodeStats d{};
      (void)sink.DrainDecodeStats(&d);
    }
  });

  JNIEnv* env = nullptr;
  int surfaceA = 1;

  // [A] healthy
  const auto a0 = Clock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(3000));
  const auto a1 = Clock::now();
  check("[A] healthy: host -> client control flows (longest silence < 600 ms)", h2c.max_gap_ms(a0, a1) < 600,
        "gap=" + std::to_string(h2c.max_gap_ms(a0, a1)) + "ms n=" + std::to_string(h2c.count(a0, a1)));
  check("[A] healthy: client -> host control flows (longest silence < 600 ms)", c2h.max_gap_ms(a0, a1) < 600,
        "gap=" + std::to_string(c2h.max_gap_ms(a0, a1)) + "ms n=" + std::to_string(c2h.count(a0, a1)));

  // [B] the UI sets the surface; the codec's create is held
  fake_ndk::barrier().which.store(fake_ndk::kHoldCreate);
  fake_ndk::barrier().held.store(true);
  std::atomic<bool> letGo{false};
  std::thread safety([&] {  // whatever blocks, the hold ends
    const auto t0 = Clock::now();
    while (!letGo.load() && ms_since(t0) < holdMs + 6000) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    fake_ndk::barrier().held.store(false);
  });
  std::thread ui([&] { sink.SetSurface(env, &surfaceA); });
  const auto b0 = Clock::now();
  bool blocked = false;
  while (ms_since(b0) < 3000 && !(blocked = fake_ndk::barrier().waiting.load() > 0)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  const auto heldAt = Clock::now();
  while (ms_since(heldAt) < holdMs) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  const auto b1 = Clock::now();
  const size_t qAtRelease = queued_count();
  letGo.store(true);
  safety.join();
  ui.join();
  check("[B] the codec's create was really held, longer than the control budget",
        blocked && ms_since(heldAt) >= holdMs, "held " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(b1 - heldAt).count()) + " ms");
  check("[B] host -> client control kept flowing while the codec was held (longest silence < 1000 ms)",
        h2c.max_gap_ms(heldAt, b1) < 1000,
        "gap=" + std::to_string(h2c.max_gap_ms(heldAt, b1)) + "ms n=" + std::to_string(h2c.count(heldAt, b1)));
  check("[B] client -> host control kept flowing (its ACKs came back: longest silence < 1000 ms)",
        c2h.max_gap_ms(heldAt, b1) < 1000,
        "gap=" + std::to_string(c2h.max_gap_ms(heldAt, b1)) + "ms n=" + std::to_string(c2h.count(heldAt, b1)));
  check("[B] no peer-lost on the host", !hostClosedPeerLost.load() && !host.IsClosed());
  check("[B] no peer-lost on the client", !clientClosedPeerLost.load() && !ctl.IsClosed());

  // [C] after the hold
  const auto c0 = Clock::now();
  const bool fresh = [&] {
    while (ms_since(c0) < 3000) {
      if (queued_count() > qAtRelease) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }();
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  const auto tags = queued_tags_from(qAtRelease);
  check("[C] after the hold the codec takes frames, and its FIRST input is an IDR",
        fresh && !tags.empty() && (tags.front() & 0x80u) != 0,
        "queued=" + std::to_string(tags.size()) + (tags.empty() ? "" : " first=" + std::to_string(tags.front())));
  check("[C] control still open on both ends", !host.IsClosed() && !ctl.IsClosed());

  stop.store(true);
  c.RequestStopForTest();
  controlThread.join();
  hostThread.join();
  rx.join();
  sink.SetSurface(env, nullptr);
  closesocket(hs);  // cs is the controller's (closed by ~c)
  WSACleanup();
  std::printf("\nandroid_decoder_session_liveness_test: %d checks, %d failed\nRESULT: %s\n", gChecks, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}

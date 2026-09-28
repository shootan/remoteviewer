// clip-image direction A, two-sided, in one process (plan r1 ⑧ + r2 §2 §6, 8-2..8-4).
//
// What is the product here: ClipImageClient (package worker with the real WIC encode and SHA-256,
// the Offer / Cancel / Status exchange, the bulk channel, the pacer, the rate controller) and
// HostClipImageService (offer judgement, pulls, chunk acceptance, SHA-256 + PNG header + WIC decode
// into CF_DIBV5), talking over real UDP sockets on loopback, through an optional impairment proxy
// (one-way delay, random loss, a rate cap).
// What is not: the OS clipboard on both ends -- one logon session has one clipboard
// (clip_image_winsta_test), so the snapshot is handed to the client and the host publishes into an
// injected publisher that checks every pixel and alpha of the CF_DIBV5 it is given; and the control
// link, which is an in-memory ControlLink that dispatches to the service's handlers the way
// host_control_session.cpp does (the product parse/dispatch lines themselves are not run).
//
//   remote60_clip_image_e2e_test [--quick]
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "clip_image_client.hpp"
#include "clip_image_core.hpp"
#include "host_clip_image.hpp"
#include "udp_impair_proxy.hpp"

#pragma comment(lib, "winmm.lib")

using namespace remote60::native_poc;

namespace {
int g_failed = 0;
int g_checks = 0;
void check(const std::string& what, bool ok) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
  std::fflush(stdout);
}

uint64_t now_us() { return BulkPacer::NowUs(); }

// ---------------------------------------------------------------- the image
struct Image {
  uint32_t w = 0, h = 0;
  std::vector<uint32_t> px;  // top-down BGRA, straight alpha
};

Image make_noise(uint32_t w, uint32_t h, uint32_t seed) {
  Image im;
  im.w = w;
  im.h = h;
  im.px.resize(static_cast<size_t>(w) * h);
  std::mt19937 rng(seed);
  for (auto& p : im.px) p = rng();  // incompressible: the PNG is about w*h*4 bytes
  // Alpha 0 and 255 must both occur, and a pixel with alpha 0 keeps its colour in a straight-alpha
  // DIBV5 -- which is what "every pixel and alpha survives" means below.
  im.px[0] = 0x00123456u;
  im.px[1] = 0xFF654321u;
  return im;
}

ClipSnapshot snapshot_of(const Image& im, uint64_t seq, const std::u16string& text = u"") {
  ClipSnapshot s;
  s.kind = ClipSnapshotKind::Dib;
  s.sequence = seq;
  s.text = text;
  s.bytes.resize(sizeof(BITMAPV5HEADER) + im.px.size() * 4);
  BITMAPV5HEADER h{};
  h.bV5Size = sizeof(h);
  h.bV5Width = static_cast<LONG>(im.w);
  h.bV5Height = static_cast<LONG>(im.h);  // bottom-up, the common clipboard layout
  h.bV5Planes = 1;
  h.bV5BitCount = 32;
  h.bV5Compression = BI_BITFIELDS;
  h.bV5RedMask = 0x00FF0000;
  h.bV5GreenMask = 0x0000FF00;
  h.bV5BlueMask = 0x000000FF;
  h.bV5AlphaMask = 0xFF000000;
  h.bV5CSType = LCS_sRGB;
  std::memcpy(s.bytes.data(), &h, sizeof(h));
  auto* rows = reinterpret_cast<uint32_t*>(s.bytes.data() + sizeof(h));
  for (uint32_t y = 0; y < im.h; ++y)
    std::memcpy(rows + static_cast<size_t>(im.h - 1 - y) * im.w, im.px.data() + static_cast<size_t>(y) * im.w, im.w * 4);
  return s;
}

// ---------------------------------------------------------------- the OS boundary (host clipboard)
class FakePublisher : public HostClipImagePublisher {
 public:
  bool Enabled() const override { return true; }
  uint64_t Sequence() const override { return seq.load(); }
  ClipPublishResult Publish(uint64_t expect, HGLOBAL png, HGLOBAL dib, const std::u16string& text) override {
    ClipPublishResult r = ClipPublishResult::Superseded;
    if (expect == seq.load()) {
      r = ClipPublishResult::Published;
      std::lock_guard<std::mutex> l(mu);
      ++published;
      publishedAtUs = BulkPacer::NowUs();
      lastText = text;
      lastPngBytes = png ? GlobalSize(png) : 0;
      mismatches = UINT64_MAX;
      if (dib && expected) {
        const auto* base = static_cast<const uint8_t*>(GlobalLock(dib));
        const DibInfo info = validate_dib(base, GlobalSize(dib));
        if (info.ok() && info.width == expected->w && info.height == expected->h && info.topDown) {
          const auto* rows = reinterpret_cast<const uint32_t*>(base + info.pixelOffset);
          uint64_t bad = 0;
          for (size_t i = 0; i < expected->px.size(); ++i) bad += rows[i] != expected->px[i];
          mismatches = bad;
        }
        GlobalUnlock(dib);
      }
      seq.fetch_add(1);  // the write moves the clipboard, as the real one does
    }
    if (png) GlobalFree(png);
    if (dib) GlobalFree(dib);
    return r;
  }
  std::atomic<uint64_t> seq{100};
  std::mutex mu;
  const Image* expected = nullptr;
  uint64_t published = 0;
  uint64_t publishedAtUs = 0;  // when the host side published (the gate's end point)
  uint64_t mismatches = UINT64_MAX;
  size_t lastPngBytes = 0;
  std::u16string lastText;
};

// ---------------------------------------------------------------- the control link
// Requests are dispatched to the service's handlers exactly as host_control_session.cpp does:
// fixed size, one answer each (Cancel answered as CancelReply).
class LoopbackLink : public ControlLink {
 public:
  LoopbackLink(HostClipImageService* svc, uint64_t epoch) : svc_(svc), epoch_(epoch) {}
  bool Write(const void* d, size_t n) override {
    auto* b = static_cast<const uint8_t*>(d);
    tx_.insert(tx_.end(), b, b + n);
    return true;
  }
  bool EndMessage() override {
    MessageHeader h{};
    if (tx_.size() < sizeof(h)) return false;
    std::memcpy(&h, tx_.data(), sizeof(h));
    const auto type = static_cast<MessageType>(h.type);
    if (type == MessageType::ControlClipImageOffer && tx_.size() == sizeof(ControlClipImageOfferMessage)) {
      ControlClipImageOfferMessage m{};
      std::memcpy(&m, tx_.data(), sizeof(m));
      auto r = svc_->HandleOffer(m, epoch_);
      put(&r, sizeof(r));
    } else if (type == MessageType::ControlClipImageCancel && tx_.size() == sizeof(ControlClipImageCancelMessage)) {
      ControlClipImageCancelMessage m{};
      std::memcpy(&m, tx_.data(), sizeof(m));
      auto r = svc_->HandleCancel(m);
      r.header.type = static_cast<uint16_t>(MessageType::ControlClipImageCancelReply);
      put(&r, sizeof(r));
    } else if (type == MessageType::ControlClipImageStatus && tx_.size() == sizeof(ControlClipImageStatusMessage)) {
      ControlClipImageStatusMessage m{};
      std::memcpy(&m, tx_.data(), sizeof(m));
      auto r = svc_->HandleStatus(m);
      put(&r, sizeof(r));
    } else {
      return false;
    }
    tx_.clear();
    ++exchanges;
    return true;
  }
  bool Read(void* out, size_t n) override {
    if (rx_.size() < n) return false;
    std::memcpy(out, rx_.data(), n);
    rx_.erase(rx_.begin(), rx_.begin() + static_cast<std::ptrdiff_t>(n));
    return true;
  }
  bool Alive() const override { return true; }
  uint64_t exchanges = 0;

 private:
  void put(const void* d, size_t n) {
    auto* b = static_cast<const uint8_t*>(d);
    rx_.insert(rx_.end(), b, b + n);
  }
  HostClipImageService* svc_;
  uint64_t epoch_;
  std::vector<uint8_t> tx_, rx_;
};

// The impairment proxy lives in udp_impair_proxy.hpp (shared with the ABR harness).
using remote60::native_poc::test::Impair;
using remote60::native_poc::test::Proxy;

// ---------------------------------------------------------------- one rig: host + client (+ proxy)
struct Rig {
  FakePublisher pub;
  HostClipImageService svc{&pub};
  ClipImageClient client;
  SOCKET hostSock = INVALID_SOCKET, clientSock = INVALID_SOCKET;
  sockaddr_in hostAddr{}, peerOfHost{};
  Proxy proxy;
  bool useProxy = false;
  std::atomic<bool> running{true};
  std::thread hostReader, clientReader;
  std::atomic<uint64_t> bulkDatagramsToHost{0};

  bool Start(const Impair* im, BulkRateConfig rate, bool bulkNegotiated = true) {
    hostSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(hostSock, reinterpret_cast<sockaddr*>(&hostAddr), sizeof(hostAddr));
    int len = sizeof(hostAddr);
    getsockname(hostSock, reinterpret_cast<sockaddr*>(&hostAddr), &len);
    DWORD to = 50;
    setsockopt(hostSock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));
    int buf = 8 << 20;
    setsockopt(hostSock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buf), sizeof(buf));
    clientSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in any{};
    any.sin_family = AF_INET;
    any.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(clientSock, reinterpret_cast<sockaddr*>(&any), sizeof(any));
    setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));
    setsockopt(clientSock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buf), sizeof(buf));
    sockaddr_in clientAddr{};
    len = sizeof(clientAddr);
    getsockname(clientSock, reinterpret_cast<sockaddr*>(&clientAddr), &len);
    useProxy = im != nullptr;
    if (useProxy) {
      if (!proxy.Start(hostAddr, *im)) return false;
      connect(clientSock, reinterpret_cast<const sockaddr*>(&proxy.frontAddr), sizeof(proxy.frontAddr));
      peerOfHost = proxy.backAddr;
      // The session's Hello, in miniature: the proxy learns where the viewer is (a real viewer has
      // spoken long before it copies anything).
      const char hello[4] = {'h', 'i', '!', 0};
      for (int i = 0; i < 3; ++i) send(clientSock, hello, sizeof(hello), 0);
    } else {
      connect(clientSock, reinterpret_cast<const sockaddr*>(&hostAddr), sizeof(hostAddr));
      peerOfHost = clientAddr;
    }
    static const bool tracePulls = [] {
      const char* e = std::getenv("REMOTE60_CLIP_BULK_TRACE");
      return e && e[0] == '2';
    }();
    svc.Start(
        [this](const void* d, size_t n) {
          if (tracePulls && n >= sizeof(UdpControlChunkHeader) + sizeof(ClipBulkPullMessage)) {
            UdpControlChunkHeader h{};
            std::memcpy(&h, d, sizeof(h));
            ClipBulkPullMessage p{};
            std::memcpy(&p, static_cast<const uint8_t*>(d) + sizeof(h), sizeof(p));
            if (h.kind == static_cast<uint16_t>(UdpPacketKind::ControlData) &&
                p.header.type == static_cast<uint16_t>(MessageType::ClipBulkPull)) {
              std::printf("PULLTX off=%u trig=%u seq=%u at=%llu\n", p.offset, p.triggerOffset, h.messageSeq,
                          static_cast<unsigned long long>(now_us()));
            }
          }
          return sendto(hostSock, static_cast<const char*>(d), static_cast<int>(n), 0,
                        reinterpret_cast<const sockaddr*>(&peerOfHost), sizeof(peerOfHost)) > 0;
        },
        1200);
    svc.SetBulkNegotiated(bulkNegotiated);
    client.Start([this](const void* d, size_t n) { return send(clientSock, static_cast<const char*>(d), static_cast<int>(n), 0) > 0; },
                 [] { return uint64_t{0}; }, [] { return false; }, 1200, rate, [](const std::string& line) {
                   std::printf("    [client %.3f] %s\n", BulkPacer::NowUs() / 1e6, line.c_str());
                 });
    client.SetBulkNegotiated(bulkNegotiated);
    client.SetHostSupports(true);
    hostReader = std::thread([this] {
      uint8_t b[2048];
      while (running) {
        const int n = recv(hostSock, reinterpret_cast<char*>(b), sizeof(b), 0);
        if (n <= 0) continue;
        if (bulk_stream_claims(b, n)) ++bulkDatagramsToHost;
        svc.OnDatagram(b, n);
      }
    });
    clientReader = std::thread([this] {
      uint8_t b[2048];
      while (running) {
        const int n = recv(clientSock, reinterpret_cast<char*>(b), sizeof(b), 0);
        if (n <= 0) continue;
        client.OnDatagram(b, n);
      }
    });
    return true;
  }
  void Stop() {
    client.Stop();
    svc.Stop();
    running = false;
    if (hostReader.joinable()) hostReader.join();
    if (clientReader.joinable()) clientReader.join();
    if (useProxy) proxy.Stop();
    closesocket(hostSock);
    closesocket(clientSock);
  }
  // The control thread: Pump on its idle turns (every 20 ms here) until `done` or the deadline.
  template <typename Done>
  bool Drive(LoopbackLink& link, uint64_t deadlineUs, Done done) {
    while (now_us() < deadlineUs) {
      if (client.Pump(link) < 0) return false;
      if (done()) return true;
      Sleep(20);
    }
    return false;
  }
};

struct Run {
  bool published = false;
  double seconds = 0;
  uint64_t mismatches = UINT64_MAX;
  size_t pngBytes = 0;
  uint32_t lastRate = 0;
  uint64_t resends = 0, dropped = 0;
};

Run transfer(const Image& im, const Impair* impair, BulkRateConfig rate, const char* label, uint64_t limitS) {
  Rig rig;
  rig.pub.expected = &im;
  Run out;
  if (!rig.Start(impair, rate)) return out;
  LoopbackLink link(&rig.svc, 3);
  const uint64_t t0 = now_us();
  std::printf("    [t0 %.3f] submit\n", t0 / 1e6);
  rig.client.SubmitSnapshot(snapshot_of(im, 500));
  const bool ok = rig.Drive(link, t0 + limitS * 1000000ull, [&] {
    const auto c = rig.client.GetCounters();
    return c.published + c.failed + c.cancelled + c.superseded > 0;
  });
  out.seconds = (now_us() - t0) / 1e6;
  {
    std::lock_guard<std::mutex> l(rig.pub.mu);
    // The gate runs from the copy to the PEER's publish; the viewer learns of it up to one status
    // poll (500 ms) later, which is not part of the user's wait.
    if (rig.pub.publishedAtUs > t0) out.seconds = (rig.pub.publishedAtUs - t0) / 1e6;
  }
  const auto c = rig.client.GetCounters();
  std::printf("    decisions: evaluations=%llu raises=%llu lowers=%llu pauses=%llu lossEvents=%llu "
              "sameEventHolds=%llu channelFragRetransmits=%llu srttMs=%.1f\n",
              static_cast<unsigned long long>(c.evaluations), static_cast<unsigned long long>(c.raises),
              static_cast<unsigned long long>(c.lowers), static_cast<unsigned long long>(c.pauses),
              static_cast<unsigned long long>(c.lossEvents), static_cast<unsigned long long>(c.recoveryHolds),
              static_cast<unsigned long long>(c.channelFragmentRetransmits), c.srttUs / 1000.0);
  out.published = ok && c.published == 1;
  out.lastRate = c.lastRateBps;
  out.resends = rig.client.PacerStats().resendsAfterTransmit;
  {
    std::lock_guard<std::mutex> l(rig.pub.mu);
    out.mismatches = rig.pub.mismatches;
    out.pngBytes = rig.pub.lastPngBytes;
  }
  if (rig.useProxy) out.dropped = rig.proxy.dropped.load();
  rig.Stop();
  std::printf("  %-44s %s in %.1f s  png=%zu  lastRate=%u bps  resends=%llu  proxyDropped=%llu\n", label,
              out.published ? "published" : "NOT published", out.seconds, out.pngBytes, out.lastRate,
              static_cast<unsigned long long>(out.resends), static_cast<unsigned long long>(out.dropped));
  return out;
}


}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::strcmp(argv[1], "--quick") == 0;
  const bool perfOnly = argc > 1 && std::strcmp(argv[1], "--perf-only") == 0;
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  timeBeginPeriod(1);  // the proxy's delivery loop only; the pacer does not rely on it
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);

  const Image img1m = make_noise(512, 512, 1);      // PNG ~1 MiB
  const Image img5m = make_noise(1280, 1024, 2);   // PNG ~5 MiB
  const Impair wan{20000, 10, 8000000};             // RTT 40 ms, 1 % loss, 8 Mbps
  const BulkRateConfig r2;                          // the agreed defaults (name kept for the scenarios below)

  // ---- integrity on every rate configuration, and the plan r2 §6 arrival gates, measured.
  // The gates are the plan's; which values must meet them is still being decided (the reviewer with
  // Codex), so each run prints MEASURE with its verdict against them, and only the integrity of what
  // arrived is PASS/FAIL. A configuration that cannot finish on a lossy path is reported as such.
  {
    struct Cfg {
      const char* name;
      BulkRateConfig rate;
    };
    // The agreed values are the defaults; the loss-tolerance variant is measured for comparison only.
    // The agreed values (2nd agreement) are the defaults; nothing else is measured here any more.
    const Cfg cfgs[] = {{"agreed-3 (agreed-2 + a lost pull's head-of-line follower is not a queue)", BulkRateConfig{}}};
    struct Case {
      const Image* im;
      const char* size;
      const Impair* impair;
      const char* path;
      double gateS;
      uint64_t limitS;
    };
    // Gates (agreed, fixed test conditions, not an SLA): 5 MiB on an idle LAN <= 5 s, on RTT 40 ms /
    // 1 % loss <= 25 s. 1 MiB is measured alongside with the same bounds for reference.
    // 2nd agreement ⑤: the lossy-path gate is 30 s, over several fixed loss seeds (every seed is
    // reported and judged -- none is picked). The earlier 25 s failures stay in the older logs.
    // 3rd agreement ③: a PROVISIONAL cap of 45 s (not final), over the earlier seeds 1-5 and five
    // independent seeds chosen in advance (11-15). The 30 s target is still printed for every run
    // (target30=met/missed) -- its failures stay on record, the threshold is not raised again.
    static Impair wanSeeds[10];
    static const char* wanNames[10] = {"40ms/1%/8M seed1",  "40ms/1%/8M seed2",  "40ms/1%/8M seed3",  "40ms/1%/8M seed4",
                                       "40ms/1%/8M seed5",  "40ms/1%/8M seed11", "40ms/1%/8M seed12", "40ms/1%/8M seed13",
                                       "40ms/1%/8M seed14", "40ms/1%/8M seed15"};
    for (uint32_t s = 0; s < 10; ++s) {
      wanSeeds[s] = wan;
      wanSeeds[s].seed = s < 5 ? s + 1 : s + 6;
    }
    std::vector<Case> cases = {{&img5m, "5 MiB", nullptr, "LAN", 5.0, 60}};
    for (uint32_t s = 0; s < (quick ? 1u : 10u); ++s) cases.push_back({&img5m, "5 MiB", &wanSeeds[s], wanNames[s], 45.0, 120});
    if (!quick) {
      cases.push_back({&img1m, "1 MiB", nullptr, "LAN", 5.0, 40});
      cases.push_back({&img1m, "1 MiB", &wanSeeds[0], wanNames[0], 45.0, 60});
    }
    for (const Cfg& c : cfgs) {
      for (const Case& k : cases) {
        char label[160];
        std::snprintf(label, sizeof(label), "%s, %s, %s", k.size, k.path, c.name);
        const Run r = transfer(*k.im, k.impair, c.rate, label, k.limitS);
        std::printf("MEASURE  cfg=\"%s\" size=%s path=%s published=%d seconds=%.1f gate=%.0f verdict=%s target30=%s lastRateBps=%u resends=%llu\n",
                    c.name, k.size, k.path, r.published ? 1 : 0, r.seconds, k.gateS,
                    (r.published && r.seconds <= k.gateS) ? "within" : "OVER",
                    k.impair ? ((r.published && r.seconds <= 30.0) ? "met" : "missed") : "n/a", r.lastRate,
                    static_cast<unsigned long long>(r.resends));
        if (&c == &cfgs[0]) {
          char g[200];
          std::snprintf(g, sizeof(g), "agreed gate: %s <= %.0f s (measured %.1f s)", label, k.gateS, r.seconds);
          check(g, r.published && r.seconds <= k.gateS);
        }
        if (r.published) {
          check(std::string("intact (every pixel and alpha): ") + label, r.mismatches == 0);
        } else if (k.impair == nullptr) {
          check(std::string("a LAN transfer must finish: ") + label, false);
        }
      }
    }
  }
  if (perfOnly) {
    CoUninitialize();
    timeEndPeriod(1);
    WSACleanup();
    std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
    return g_failed ? 1 : 0;
  }
  // ---- text of the same copy travels in the package
  {
    Rig rig;
    const Image tiny = make_noise(40, 30, 3);
    rig.pub.expected = &tiny;
    rig.Start(nullptr, r2);
    LoopbackLink link(&rig.svc, 3);
    rig.client.SubmitSnapshot(snapshot_of(tiny, 501, u"caption \xD55C\xAE00"));
    const bool ok = rig.Drive(link, now_us() + 10000000, [&] { return rig.client.GetCounters().published > 0; });
    bool same = false;
    {
      std::lock_guard<std::mutex> l(rig.pub.mu);
      same = rig.pub.lastText == u"caption \xD55C\xAE00" && rig.pub.mismatches == 0;
    }
    check("image + same-copy text: both published together", ok && same);
    rig.Stop();
  }
  // ---- a newer copy mid-transfer supersedes: no late publish of the old one (r2 8-2)
  {
    Rig rig;
    const Image second = make_noise(64, 48, 9);
    rig.pub.expected = &second;
    rig.Start(nullptr, BulkRateConfig{});  // slow (256 kbps) so the first is still running
    LoopbackLink link(&rig.svc, 3);
    rig.client.SubmitSnapshot(snapshot_of(img5m, 600));
    rig.Drive(link, now_us() + 15000000, [&] { return rig.client.Active(); });
    Sleep(500);
    rig.client.SubmitSnapshot(snapshot_of(second, 601));
    const bool ok = rig.Drive(link, now_us() + 20000000, [&] { return rig.client.GetCounters().published > 0; });
    const auto c = rig.client.GetCounters();
    bool only = false;
    {
      std::lock_guard<std::mutex> l(rig.pub.mu);
      only = rig.pub.published == 1 && rig.pub.mismatches == 0;
    }
    check("the newer copy is published, the older one cancelled and never published", ok && only && c.cancelled == 1);
    rig.Stop();
  }
  // ---- the host clipboard changed after accept: superseded, nothing published (r2 8-4)
  {
    Rig rig;
    rig.pub.expected = &img1m;
    rig.Start(nullptr, r2);
    LoopbackLink link(&rig.svc, 3);
    rig.client.SubmitSnapshot(snapshot_of(img1m, 700));
    rig.Drive(link, now_us() + 15000000, [&] { return rig.client.Active(); });
    rig.pub.seq.fetch_add(1);  // someone copied on the host meanwhile
    const bool ok = rig.Drive(link, now_us() + 30000000, [&] {
      const auto c = rig.client.GetCounters();
      return c.published + c.superseded + c.failed > 0;
    });
    const auto c = rig.client.GetCounters();
    check("host copy after accept: superseded, and the host clipboard untouched",
          ok && c.superseded == 1 && rig.pub.published == 0);
    rig.Stop();
  }
  // ---- session end mid-transfer: both sides idle within 1 s, nothing published
  {
    Rig rig;
    rig.pub.expected = &img5m;
    rig.Start(nullptr, BulkRateConfig{});
    LoopbackLink link(&rig.svc, 3);
    rig.client.SubmitSnapshot(snapshot_of(img5m, 800));
    rig.Drive(link, now_us() + 15000000, [&] { return rig.client.Active(); });
    Sleep(1000);
    const uint64_t t0 = now_us();
    rig.svc.OnSessionEnd(4);
    rig.client.EndSession();
    const double ms = (now_us() - t0) / 1000.0;
    ControlClipImageStatusMessage s{};
    const auto st = rig.svc.HandleStatus(s);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "session end: both sides idle in %.1f ms (<= 1000), nothing published", ms);
    check(buf, ms <= 1000.0 && !rig.client.Active() && rig.pub.published == 0 &&
                   st.state == static_cast<uint8_t>(ClipImageState::Unknown));
    rig.Stop();
  }
  // ---- counterexample: the budget shrinks mid-transfer (video starts) -- the rate follows at once
  {
    Rig rig;
    rig.pub.expected = &img5m;
    rig.Start(nullptr, r2);
    LoopbackLink link(&rig.svc, 3);
    rig.client.SubmitSnapshot(snapshot_of(img5m, 950));
    rig.Drive(link, now_us() + 15000000, [&] { return rig.client.GetCounters().lastRateBps >= 4000000; });
    const uint32_t before = rig.client.GetCounters().lastRateBps;
    rig.client.SetUplinkBudgetBps(1000000);
    const uint64_t t0 = now_us();
    rig.Drive(link, t0 + 2000000, [&] { return rig.client.GetCounters().lastRateBps <= 1000000; });
    const double ms = (now_us() - t0) / 1000.0;
    const uint32_t after = rig.client.GetCounters().lastRateBps;
    char buf[200];
    std::snprintf(buf, sizeof(buf), "a budget drop mid-transfer applies at once: %u -> %u bps in %.0f ms (<= 100)", before, after, ms);
    check(buf, before >= 4000000 && after <= 1000000 && ms <= 100.0);
    // ④ a budget of 0 sends nothing -- not the 64 kbps floor -- and its end resumes the transfer
    rig.client.SetUplinkBudgetBps(0);
    Sleep(300);  // let the pacer drain what was already admitted
    const uint64_t dg0 = rig.bulkDatagramsToHost.load();
    rig.Drive(link, now_us() + 1500000, [] { return false; });
    const uint64_t dg1 = rig.bulkDatagramsToHost.load();
    std::snprintf(buf, sizeof(buf), "④ a budget of 0: %llu bulk datagrams reached the host in 1.5 s (acknowledgements only, no data)",
                  static_cast<unsigned long long>(dg1 - dg0));
    check(buf, dg1 - dg0 <= 4);
    rig.client.ClearUplinkBudget();
    const bool ok = rig.Drive(link, now_us() + 60000000, [&] { return rig.client.GetCounters().published > 0; });
    check("...and the transfer still completes intact once the budget returns", ok && rig.pub.mismatches == 0);
    rig.Stop();
  }
  // ---- an old host (no bulk channel negotiated): nothing image-related is sent at all
  {
    Rig rig;
    rig.Start(nullptr, r2, /*bulkNegotiated=*/false);
    LoopbackLink link(&rig.svc, 3);
    rig.client.SubmitSnapshot(snapshot_of(img1m, 900));
    rig.Drive(link, now_us() + 3000000, [] { return false; });
    check("feature not negotiated: no offer, no bulk datagram",
          link.exchanges == 0 && rig.bulkDatagramsToHost.load() == 0 && rig.client.GetCounters().offered == 0);
    rig.Stop();
  }
  CoUninitialize();
  timeEndPeriod(1);
  WSACleanup();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}

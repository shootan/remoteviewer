// mobile-abr r1: the phone's reports through the real host ABR -- the shared client session the APK runs
// (ClientSessionController, native_video_client_session.cpp) with a sink that behaves like the APK's
// decoder sink, against an isolated GNLinkStream, judged from the host's own "[abr]" and stats lines.
//
// The field case (2026-10-14, phone SM-S948N -> company host 0.2.157, 3 Mbps / 30 fps): the active-session
// demotion at 14:27:17 "evidence=stale_active|decode_unreported metricsFresh=0 clientDecodedFps=0" with
// the host sending a full cadence -- the phone's reports came ~0.6 a second with gaps of 5-10 s. 118420f
// reported only on seconds its sink had presentation stats for, never with decode-side fields, and lost
// a drained snapshot whenever the control pass went to something else (Codex contract section 1.3).
//
// The sink: the APK sink's codec replaced by a counter. It "decodes" every frame it is handed; it has
// presentation stats only after two present gaps since the last drain (the APK's rule), and with
// --present-duty ON:OFF only during the ON seconds (default 6:8: ~8 s without a presentation sample --
// P7 needs the 3 s freshness gate + 2 stale + 2 severe seconds). With a session that drains decode stats
// (REMOTE60_CLIENT_DECODE_STATS, after A) it gives its decoded count like the APK sink does after A;
// built against 118420f there is no one to give it to, which is the field configuration.
//
// Every run goes through a relay of this process. Each viewer address gets its own socket towards the
// host -- a reconnecting phone opens a new socket, so the host sees a new peer, as it would from a phone
// (one shared back socket made a reconnect look like the session that had just ended). It reads the viewer->host
// control traffic for ControlClientMetrics -- what reaches the host's socket, seq-deduplicated -- so the
// report count, spacing and fields are measured where the host receives them; impairments switch there.
//
// Scenarios (--scenario, one host run each):
//  healthy -- must NOT demote; 60 s judged: 55-65 reports, max report gap < 3 s, reported decoded fps
//             within 15% of what the sink decoded, no report above 1.6 x fps:
//   sparse      sparse presentation stats (decode 30, present 0-1 samples most seconds)
//   nolatch     presentation samples every second, the view latching none (displayed 0)
//   static      the source still for the first 20 s (the host sends little), then moving
//   inputload   sparse, plus a mouse move every 16 ms and a window-list request every 2 s
//   reconnect   sparse; at 15 s the session disconnects and connects again at once (same sink)
//  real faults -- must demote (within the impairment + 3 s):
//   uplink      every viewer->host datagram dropped (the reports really stop; history #341)
//   videostop   every host->viewer video datagram dropped, control flowing
//   thin        9 of every 10 frames dropped whole (3 of 30 arriving)
//   decodestall frames keep arriving, the sink's decoder gives out nothing (recv 30 / decode 0)
//   recovery    thin for the impairment, then 45 s healthy: must demote AND step back up by the ABR's
//               own hold times (not forced)
//  recorded, not judged: loss (25% of video datagrams), delay (+250 ms) -- what the phone can see of them.
//  external (r3): no session here -- the host and the relay wait for a client elsewhere (the APK sink on a
//             real MediaCodec in an emulator: apps/android_direct_client/smoke). The relay's port is printed
//             (RELAY_PORT=) and written to <out>/relay_port.txt; from the first report, --external-sec
//             seconds are judged against the smoke's fixed schedule (surface off +25..+28 s with the stream
//             paused, a refused selection at +45 s): 18-22 reports and no 3 s gap in the first 20 s, reports
//             again after the resume, no ABR down step.
// Impairment scenarios present every second (--present-duty 1:0) unless given.
//
//   remote60_mobile_abr_session_e2e_test --scenario S [--fps N] [--bitrate N] [--present-duty ON:OFF]
//       [--impair-sec N] [--out DIR] [--watchdog-sec N]
//   (REMOTE60_ALLOW_HOST_E2E=1)

#include "e2e_isolation.hpp"
#include "control_resume_e2e_support.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <utility>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "native_video_client_session.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;

namespace {

std::wstring kTargetTitle;
std::atomic<bool> gSourceStill{false};

// Every harness line goes to stdout with one fwrite (product threads write too; std::cout's buffer is
// never swapped -- viewer_stats_chain r2).
void say(const std::string& s) {
  const std::string line = s + "\n";
  std::fwrite(line.data(), 1, line.size(), stdout);
  std::fflush(stdout);
}

// ------------------------------------------------------------------------------ what the host captures
// quality-6m r1: the captured window's size (--target-size WxH). 480x270 is a UI/protocol fixture; a resolution
// claim needs the source it is about (1920x1080).
int gTargetW = 480, gTargetH = 270;

class CadenceTarget {
 public:
  bool Start() {
    thread_ = std::thread([this] { Run(); });
    return wait_until([this] { return ready_.load(); }, 5000) && hwnd_ != nullptr;
  }
  void Stop() {
    if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    if (thread_.joinable()) thread_.join();
  }
  HWND hwnd() const { return hwnd_; }

 private:
  static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<CadenceTarget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_TIMER && self) {
      if (gSourceStill.load()) return 0;  // still: nothing changes, nothing repaints
      ++self->frame_;
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    if (msg == WM_PAINT && self) {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd, &ps);
      RECT client{};
      GetClientRect(hwnd, &client);
      const int phase = static_cast<int>(self->frame_ % 32);
      HBRUSH back = CreateSolidBrush((self->frame_ & 1) ? RGB(20, 20, 30) : RGB(40, 25, 25));
      FillRect(dc, &client, back);
      DeleteObject(back);
      RECT block{phase * 12, 30 + (phase % 7) * 10, phase * 12 + 140, 200};
      HBRUSH fore = CreateSolidBrush(RGB(200, 60 + phase * 5, 40 + phase * 3));
      FillRect(dc, &block, fore);
      DeleteObject(fore);
      EndPaint(hwnd, &ps);
      return 0;
    }
    if (msg == WM_CLOSE) {
      DestroyWindow(hwnd);
      return 0;
    }
    if (msg == WM_DESTROY) {
      PostQuitMessage(0);
      return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  void Run() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Remote60MobileAbrTarget";
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                            wc.lpszClassName, kTargetTitle.c_str(), WS_POPUP, 0, 0, gTargetW, gTargetH, nullptr,
                            nullptr, wc.hInstance, nullptr);
    if (hwnd_) {
      SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
      SetLayeredWindowAttributes(hwnd_, 0, 1, LWA_ALPHA);
      ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
      SetTimer(hwnd_, 1, 16, nullptr);
    }
    ready_.store(true);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
    hwnd_ = nullptr;
  }
  std::thread thread_;
  std::atomic<bool> ready_{false};
  uint64_t frame_ = 0;
  HWND hwnd_ = nullptr;
};

// ------------------------------------------------------------------------------ the phone's sink
class PhoneSink : public ClientEncodedFrameSink {
 public:
  PhoneSink(uint32_t onSec, uint32_t offSec, bool noLatch)
      : onSec_(onSec), offSec_(offSec), noLatch_(noLatch), startUs_(qpc_now_us()) {}
  std::atomic<bool> stall{false};  // decodestall: frames arrive, the decoder gives out nothing

  std::pair<uint32_t, uint32_t> last_size() {
    std::lock_guard<std::mutex> lk(mu_);
    return {lastFrameW_, lastFrameH_};
  }
  void OnEncodedH264Frame(UdpH264AssembledFrame&& frame) override {
    std::lock_guard<std::mutex> lk(mu_);
    ++frames_;
    lastFrameW_ = frame.header.width;  // quality-6m r1: also while stalled (what the host sent)
    lastFrameH_ = frame.header.height;
    if (stall.load()) return;
    width_ = frame.header.width;
    height_ = frame.header.height;
    ++decodedSinceDrain_;
    ++decodedTotal_;
    const uint64_t now = qpc_now_us();
    if (DisplayOn(now)) {
      if (lastPresentUs_ != 0 && now > lastPresentUs_) gaps_.push_back(static_cast<uint32_t>(now - lastPresentUs_));
      lastPresentUs_ = now;
    } else {
      lastPresentUs_ = 0;
    }
  }
  void OnVideoStreamReset() override {}

  // android_video_decoder.cpp after A: every drain closes the window; under two gaps is "no sample".
  bool DrainPresentationStats(ClientPresentationStats* out) override {
    if (!out) return false;
    std::lock_guard<std::mutex> lk(mu_);
    if (gaps_.size() < 2) {
#ifdef REMOTE60_CLIENT_DECODE_STATS
      gaps_.clear();
#endif
      return false;
    }
    std::vector<uint32_t> g = gaps_;
    gaps_.clear();
    std::sort(g.begin(), g.end());
    out->targetIntervalUs = 33333;
    out->sampleCount = static_cast<uint32_t>(g.size());
    out->gapP50Us = g[g.size() / 2];
    out->gapP95Us = g[std::min(g.size() - 1, g.size() * 95 / 100)];
    out->gapMaxUs = g.back();
    out->displayedCount = noLatch_ ? 0 : static_cast<uint32_t>(g.size());
    uint64_t span = 0;
    for (uint32_t x : g) span += x;
    out->fpsX100 = span ? static_cast<uint32_t>(g.size() * 100000000ull / span) : 0;
    ++drains_;
    return true;
  }

#ifdef REMOTE60_CLIENT_DECODE_STATS
#ifdef REMOTE60_DECODE_SPAN
  // android_video_decoder.cpp DrainDecodeStats after r6: the measurement since the episode began (no grace
  // here -- a stand-in decoder has no startup), cumulative, the output size.
  bool DrainDecodeStats(ClientDecodeStats* out) override {
    if (!out) return false;
    std::lock_guard<std::mutex> lk(mu_);
    const uint64_t now = qpc_now_us();
    out->spanId = spanId_;
    out->spanFrames = decodedTotal_ - spanBase_;
    out->spanUs = now > spanStartUs_ ? now - spanStartUs_ : 0;
    out->width = width_;
    out->height = height_;
    return true;
  }
  void OnDecodeEpisodeStart() override {
    std::lock_guard<std::mutex> lk(mu_);
    ++spanId_;
    spanBase_ = decodedTotal_;
    spanStartUs_ = qpc_now_us();
  }
#else
  // android_video_decoder.cpp DrainDecodeStats: pictures given out since the last drain, the output size.
  bool DrainDecodeStats(ClientDecodeStats* out) override {
    if (!out) return false;
    std::lock_guard<std::mutex> lk(mu_);
    out->decodedFrames = static_cast<uint32_t>(decodedSinceDrain_);
    decodedSinceDrain_ = 0;
    out->width = width_;
    out->height = height_;
    return true;
  }
#endif
#endif

  uint64_t frames() {
    std::lock_guard<std::mutex> lk(mu_);
    return frames_;
  }
  uint64_t decoded_total() {
    std::lock_guard<std::mutex> lk(mu_);
    return decodedTotal_;
  }
  uint64_t drains() {
    std::lock_guard<std::mutex> lk(mu_);
    return drains_;
  }

 private:
  bool DisplayOn(uint64_t now) const {
    if (offSec_ == 0) return true;
    const uint64_t cycleUs = static_cast<uint64_t>(onSec_ + offSec_) * 1000000ull;
    return ((now - startUs_) % cycleUs) < static_cast<uint64_t>(onSec_) * 1000000ull;
  }
  std::mutex mu_;
  const uint32_t onSec_, offSec_;
  const bool noLatch_;
  const uint64_t startUs_;
  uint64_t frames_ = 0, drains_ = 0, decodedSinceDrain_ = 0, decodedTotal_ = 0;
  uint32_t lastFrameW_ = 0, lastFrameH_ = 0;
  uint64_t spanId_ = 0, spanBase_ = 0, spanStartUs_ = 0;  // r6
  uint32_t width_ = 0, height_ = 0;
  uint64_t lastPresentUs_ = 0;
  std::vector<uint32_t> gaps_;
};

// ------------------------------------------------------------------------------ the relay
enum class Impair : int { None = 0, Uplink, VideoStop, Thin, Loss, Delay, KeyDrop };

struct SeenReport {
  uint64_t qpcUs;
  uint32_t seq, decodedFpsX100, recvFpsX100, width, height, presentSamples;
};

class Relay {
 public:
  std::atomic<int> mode{static_cast<int>(Impair::None)};
  std::atomic<uint64_t> dropped{0};
  uint16_t frontPort = 0;

  bool Start(uint16_t hostPort) {
    host_.sin_family = AF_INET;
    host_.sin_port = htons(hostPort);
    host_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    front_ = Bind(&frontPort);
    uint16_t backPort = 0;
    const SOCKET back = Bind(&backPort);
    back_.store(back);
    if (front_ == INVALID_SOCKET || back == INVALID_SOCKET) return false;
    running_ = true;
    threads_.emplace_back([this] { Up(); });
    threads_.emplace_back([this] { Down(); });
    threads_.emplace_back([this] { Deliver(); });
    return true;
  }
  void Stop() {
    running_ = false;
    for (auto& t : threads_) t.join();
    threads_.clear();
    if (front_ != INVALID_SOCKET) closesocket(front_);
    if (back_.load() != INVALID_SOCKET) closesocket(back_.load());
    for (SOCKET s : oldBacks_) closesocket(s);
  }
  std::vector<SeenReport> Reports() {
    std::lock_guard<std::mutex> lk(repMu_);
    return reports_;
  }
  // r4 F2: datagrams through the relay, both ways, cumulative.
  std::atomic<uint64_t> upDatagrams{0}, downDatagrams{0};
  // r5 F2: the relay's own receive errors on the viewer side (WSAECONNRESET = an ICMP error reported on a
  // Windows UDP socket), so a stall can be told apart from the relay socket's own trouble.
  std::atomic<uint64_t> frontRecvErrors{0}, frontConnReset{0};
  // r5 F2: every control datagram through the relay -- direction, kind (data / ack / nack), stream,
  // message seq, fragment and, on a first fragment, the message type -- to see which exchange stops.
  struct CtlEvent {
    uint64_t qpcUs;
    bool up;
    uint16_t kind;
    uint32_t streamId, messageSeq;
    uint16_t frag, fragCount, msgType;
  };
  std::vector<CtlEvent> CtlEvents() {
    std::lock_guard<std::mutex> lk(ctlMu_);
    return ctl_;
  }

 private:
  static SOCKET Bind(uint16_t* port) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    int len = sizeof(a);
    getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
    *port = ntohs(a.sin_port);
    int buf = 8 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buf), sizeof(buf));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buf), sizeof(buf));
    DWORD to = 50;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));
    return s;
  }
  static bool IsVideo(const uint8_t* b, int n, uint32_t* seq) {
    if (n < static_cast<int>(sizeof(UdpVideoChunkHeader))) return false;
    UdpVideoChunkHeader h{};
    std::memcpy(&h, b, sizeof(h));
    if (h.magic != kMagic || h.kind != static_cast<uint16_t>(UdpPacketKind::VideoChunk)) return false;
    *seq = h.seq;
    return true;
  }
  // A ControlClientMetrics message in one control chunk (it is ~150 bytes): recorded once per seq.
  void NoteReport(const uint8_t* b, int n) {
    if (n < static_cast<int>(sizeof(UdpControlChunkHeader) + sizeof(ControlClientMetricsMessage))) return;
    UdpControlChunkHeader h{};
    std::memcpy(&h, b, sizeof(h));
    if (h.magic != kMagic || h.kind != static_cast<uint16_t>(UdpPacketKind::ControlData) || h.fragIndex != 0 ||
        h.fragCount != 1) {
      return;
    }
    ControlClientMetricsMessage m{};
    std::memcpy(&m, b + sizeof(h), sizeof(m));
    if (m.header.magic != kMagic || m.header.type != static_cast<uint16_t>(MessageType::ControlClientMetrics)) return;
    std::lock_guard<std::mutex> lk(repMu_);
    for (auto it = reports_.rbegin(); it != reports_.rend() && it - reports_.rbegin() < 8; ++it) {
      if (it->seq == m.seq) return;  // a retransmission
    }
    reports_.push_back({qpc_now_us(), m.seq, m.decodedFpsX100, m.recvFpsX100, m.width, m.height, m.presentSampleCount});
  }
  void NoteControl(const uint8_t* b, int n, bool up) {
    if (n < 8) return;
    uint32_t magic = 0;
    uint16_t kind = 0;
    std::memcpy(&magic, b, 4);
    std::memcpy(&kind, b + 4, 2);
    if (magic != kMagic) return;
    CtlEvent e{qpc_now_us(), up, kind, 0, 0, 0, 0, 0};
    if (kind == static_cast<uint16_t>(UdpPacketKind::ControlData) && n >= static_cast<int>(sizeof(UdpControlChunkHeader))) {
      UdpControlChunkHeader h{};
      std::memcpy(&h, b, sizeof(h));
      e.streamId = h.streamId;
      e.messageSeq = h.messageSeq;
      e.frag = h.fragIndex;
      e.fragCount = h.fragCount;
      if (h.fragIndex == 0 && n >= static_cast<int>(sizeof(h) + sizeof(MessageHeader))) {
        MessageHeader mh{};
        std::memcpy(&mh, b + sizeof(h), sizeof(mh));
        if (mh.magic == kMagic) e.msgType = mh.type;
      }
    } else if ((kind == static_cast<uint16_t>(UdpPacketKind::ControlAck) ||
                kind == static_cast<uint16_t>(UdpPacketKind::ControlNack)) &&
               n >= static_cast<int>(sizeof(UdpControlAckPacket) - sizeof(uint16_t) * kUdpControlMaxMissingPerNack)) {
      UdpControlAckPacket a{};
      std::memcpy(&a, b, std::min<size_t>(sizeof(a), static_cast<size_t>(n)));
      e.streamId = a.streamId;
      e.messageSeq = a.messageSeq;
      e.frag = a.missingCount;
    } else {
      return;
    }
    std::lock_guard<std::mutex> lk(ctlMu_);
    if (ctl_.size() < 200000) ctl_.push_back(e);
  }
  void Up() {
    uint8_t buf[2048];
    while (running_) {
      sockaddr_in from{};
      int fl = sizeof(from);
      const int n = recvfrom(front_, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
      if (n < 0) {
        const int e = WSAGetLastError();
        if (e != WSAETIMEDOUT) ++frontRecvErrors;
        if (e == WSAECONNRESET) ++frontConnReset;
      }
      if (n <= 0) continue;
      {
        std::lock_guard<std::mutex> lk(viewerMu_);
        const bool newViewer = haveViewer_ && (from.sin_port != viewer_.sin_port || from.sin_addr.s_addr != viewer_.sin_addr.s_addr);
        if (newViewer) {  // a new session: a new peer for the host
          uint16_t port = 0;
          oldBacks_.push_back(back_.exchange(Bind(&port)));
        }
        viewer_ = from;
        haveViewer_ = true;
      }
      if (static_cast<Impair>(mode.load()) == Impair::Uplink) {
        ++dropped;
        continue;
      }
      NoteReport(buf, n);
      NoteControl(buf, n, true);
      ++upDatagrams;
      sendto(back_.load(), reinterpret_cast<const char*>(buf), n, 0, reinterpret_cast<const sockaddr*>(&host_), sizeof(host_));
    }
  }
  void Down() {
    std::mt19937 rng(20261014u);
    uint8_t buf[2048];
    while (running_) {
      const int n = recv(back_.load(), reinterpret_cast<char*>(buf), sizeof(buf), 0);
      if (n <= 0) continue;
      const Impair m = static_cast<Impair>(mode.load());
      uint32_t seq = 0;
      const bool video = IsVideo(buf, n, &seq);
      if (!video) NoteControl(buf, n, false);
      bool drop = false;
      if (video && m == Impair::Loss) drop = (rng() % 1000) < 250;
      if (video && m == Impair::Thin) drop = (seq % 10) != 0;
      if (video && m == Impair::VideoStop) drop = true;
      if (video && m == Impair::KeyDrop) {  // r5: every chunk of every keyframe
        UdpVideoChunkHeader h{};
        std::memcpy(&h, buf, sizeof(h));
        drop = (h.flags & 0x1u) != 0;
      }
      if (drop) {
        ++dropped;
        continue;
      }
      const uint64_t due = qpc_now_us() + (m == Impair::Delay ? 250000ull : 0ull);
      std::lock_guard<std::mutex> lk(qMu_);
      q_.push_back({due, std::vector<uint8_t>(buf, buf + n)});
    }
  }
  void Deliver() {
    while (running_) {
      std::vector<uint8_t> b;
      bool have = false;
      {
        std::lock_guard<std::mutex> lk(qMu_);
        if (!q_.empty() && q_.front().first <= qpc_now_us()) {
          b = std::move(q_.front().second);
          q_.pop_front();
          have = true;
        }
      }
      if (!have) {
        std::this_thread::sleep_for(std::chrono::microseconds(500));
        continue;
      }
      sockaddr_in to{};
      {
        std::lock_guard<std::mutex> lk(viewerMu_);
        if (!haveViewer_) continue;
        to = viewer_;
      }
      sendto(front_, reinterpret_cast<const char*>(b.data()), static_cast<int>(b.size()), 0,
             reinterpret_cast<const sockaddr*>(&to), sizeof(to));
      ++downDatagrams;
    }
  }
  sockaddr_in host_{};
  SOCKET front_ = INVALID_SOCKET;
  std::atomic<SOCKET> back_{INVALID_SOCKET};
  std::vector<SOCKET> oldBacks_;  // under viewerMu_; closed at Stop
  std::atomic<bool> running_{false};
  std::mutex viewerMu_;
  sockaddr_in viewer_{};
  bool haveViewer_ = false;
  std::mutex qMu_;
  std::deque<std::pair<uint64_t, std::vector<uint8_t>>> q_;
  std::mutex repMu_;
  std::vector<SeenReport> reports_;
  std::mutex ctlMu_;
  std::vector<CtlEvent> ctl_;
  std::vector<std::thread> threads_;
};

// ------------------------------------------------------------------------------ the host's log
int64_t host_line_ms(const std::string& line) {
  if (line.size() < 18 || line[2] != '-' || line[8] != ':' || line[11] != ':' || line[14] != '.') return -1;
  return ((std::stoll(line.substr(6, 2)) * 60 + std::stoll(line.substr(9, 2))) * 60 + std::stoll(line.substr(12, 2))) *
             1000 +
         std::stoll(line.substr(15, 3));
}
int64_t local_now_ms() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  return ((static_cast<int64_t>(st.wHour) * 60 + st.wMinute) * 60 + st.wSecond) * 1000 + st.wMilliseconds;
}
std::string field_of(const std::string& line, const std::string& key) {
  const std::string needle = " " + key + "=";
  const size_t at = line.find(needle);
  if (at == std::string::npos) return {};
  const size_t start = at + needle.size();
  const size_t end = line.find(' ', start);
  return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
}
int profile_rank(const std::string& p) { return p == "high" ? 0 : p == "mid" ? 1 : p == "low" ? 2 : -1; }

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string scenario = "sparse";
  uint32_t fps = 30, bitrate = 3000000;
  int impairSec = 20;
  uint32_t dutyOn = 6, dutyOff = 8;
  bool dutyGiven = false;
  std::wstring outDir;
  int watchdogOverrideSec = 0;
  int externalSec = 70;
  int keyDropFrom = -1, keyDropTo = -1;  // r5 external: drop keyframes over [from, to) s from the first report
  std::string externalPlan = "app";
  uint32_t runtimeBitrate = 0;  // quality-6m r1: asked for after connecting, the app's runtime path
  for (int i = 1; i < argc; ++i) {
    const std::wstring a = argv[i];
    const bool more = i + 1 < argc;
    if (a == L"--scenario" && more) {
      const std::wstring v = argv[++i];
      scenario.assign(v.begin(), v.end());
    } else if (a == L"--fps" && more) {
      fps = static_cast<uint32_t>(_wtoi(argv[++i]));
    } else if (a == L"--bitrate" && more) {
      bitrate = static_cast<uint32_t>(_wtoi(argv[++i]));
    } else if (a == L"--impair-sec" && more) {
      impairSec = _wtoi(argv[++i]);
    } else if (a == L"--present-duty" && more) {
      dutyGiven = std::swscanf(argv[++i], L"%u:%u", &dutyOn, &dutyOff) == 2;
    } else if (a == L"--out" && more) {
      outDir = argv[++i];
      if (!outDir.empty() && outDir.back() != L'\\' && outDir.back() != L'/') outDir += L"\\";
    } else if (a == L"--keydrop" && more) {
      std::swscanf(argv[++i], L"%d:%d", &keyDropFrom, &keyDropTo);
    } else if (a == L"--plan" && more) {
      const std::wstring v = argv[++i];
      externalPlan.assign(v.begin(), v.end());
    } else if (a == L"--external-sec" && more) {
      externalSec = _wtoi(argv[++i]);
    } else if (a == L"--watchdog-sec" && more) {
      watchdogOverrideSec = _wtoi(argv[++i]);
    } else if (a == L"--runtime-bitrate" && more) {
      runtimeBitrate = static_cast<uint32_t>(_wtoi(argv[++i]));
    } else if (a == L"--target-size" && more) {
      std::swscanf(argv[++i], L"%dx%d", &gTargetW, &gTargetH);
    }
  }
  const bool external = scenario == "external";
  const bool healthy = scenario == "sparse" || scenario == "nolatch" || scenario == "static" ||
                       scenario == "inputload" || scenario == "reconnect";
  Impair impair = Impair::None;
  bool decodeStall = false, recovery = false;
  if (scenario == "uplink") impair = Impair::Uplink;
  else if (scenario == "videostop") impair = Impair::VideoStop;
  else if (scenario == "thin") impair = Impair::Thin;
  else if (scenario == "recovery") impair = Impair::Thin, recovery = true;
  else if (scenario == "decodestall") decodeStall = true;
  else if (scenario == "loss") impair = Impair::Loss;
  else if (scenario == "delay") impair = Impair::Delay;
  else if (!healthy && !external) {
    say("unknown --scenario " + scenario);
    return 2;
  }
  if (scenario == "nolatch" && !dutyGiven) dutyOn = 1, dutyOff = 0;
  if (!healthy && !dutyGiven) dutyOn = 1, dutyOff = 0;
  const bool judgedFault = impair == Impair::Uplink || impair == Impair::VideoStop || impair == Impair::Thin || decodeStall;
  const int warmSec = healthy ? 3 : 12;
  const int judgeSec = 60;
  const int afterSec = recovery ? 45 : 8;
  const int totalSec = external ? 120 + externalSec + 5
                                : healthy ? warmSec + judgeSec + 2 : warmSec + impairSec + afterSec;
  say("mobile_abr_session_e2e_test scenario=" + scenario + " fps=" + std::to_string(fps) + " bitrate=" +
      std::to_string(bitrate) + " presentDuty=" + std::to_string(dutyOn) + ":" + std::to_string(dutyOff) +
      " target=" + std::to_string(gTargetW) + "x" + std::to_string(gTargetH) +
      " runtimeBitrate=" + std::to_string(runtimeBitrate) +
#ifdef REMOTE60_CLIENT_DECODE_STATS
      " session=A(decode stats, pending snapshot)");
#else
      " session=118420f(no decode stats)");
#endif

  if (!host_e2e_allowed()) {
    say("SKIP  mobile_abr_session_e2e_test (starts a listening host)");
    say("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED");
    return kE2eSkippedExit;
  }
  const int hostSec = totalSec + 40;
  const int watchdogSec = watchdogOverrideSec > 0 ? watchdogOverrideSec : hostSec + 240;
  std::thread([watchdogSec] {
    std::this_thread::sleep_for(std::chrono::seconds(watchdogSec));
    const std::string line = "\nFAIL  watchdog: the run did not finish within " + std::to_string(watchdogSec) +
                             " s\nmobile_abr_session_e2e_test: FAIL\nRESULT: FAIL\n";
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    TerminateProcess(GetCurrentProcess(), 3);
  }).detach();

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
  const uint16_t hostPort = e2e_pick_free_udp_port();
  kTargetTitle = e2e_unique_window_title(L"remote60 mobile abr target");

  StagingDir staging;
  if (!staging.Create(L"mobile_abr")) {
    say("FAIL  " + staging.why());
    return 1;
  }
  const std::wstring dir = staging.path();
  const std::wstring myDir = directory_of(self_path());
  const bool staged = CopyFileW((myDir + L"GNLinkStream.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) != 0;
  check("the host could be staged", staged);

  if (scenario == "static") gSourceStill.store(true);
  CadenceTarget target;
  check("an on-screen, invisible, click-through window is up for the host to capture", target.Start());

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  const std::wstring hostLogPath = dir + L"host.log";
  HANDLE hostLog = INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION hostPi{};
  bool launched = false;
  if (staged) {
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_STATS_PRINT_EVERY_SEC", L"1");
    SetEnvironmentVariableW(L"REMOTE60_CLIPBOARD_SYNC", L"0");
    std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                       L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(hostPort) +
                       L" --fps " + std::to_wstring(fps) + L" --bitrate " + std::to_wstring(bitrate) +
                       L" --seconds " + std::to_wstring(hostSec) + L" --input-injection-mode none" +
                       e2e_capture_window_args(kTargetTitle);
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    hostLog = CreateFileW(hostLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hostLog != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = hostLog;
      si.hStdError = hostLog;
    }
    const std::wstring isoAppData = dir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> isoEnv = e2e_isolated_environment(isoAppData);
    std::string isoWhy;
    const bool isoOk = e2e_command_is_isolated(cmd, dir, &isoWhy) && e2e_path_is_under(e2e_block_localappdata(isoEnv), dir);
    check("the launched host is isolated from the user's files, input injection off", isoOk, isoWhy);
    launched = isoOk && CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, hostLog != INVALID_HANDLE_VALUE,
                                       CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, isoEnv.data(),
                                       dir.c_str(), &si, &hostPi) != 0;
    if (launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  check("the host started", launched);

  Relay relay;
  const bool relayUp = launched && relay.Start(hostPort);
  check("the relay is up (every run: it reads the reports the host receives)", relayUp);

  // ------------------------------------------------------------------ the phone: the shared session
  PhoneSink sink(dutyOn, dutyOff, scenario == "nolatch");
  ClientSessionController session;
  ClientSessionConnectArgs args;  // what native_bridge.cpp sets for a direct session
  args.host = "127.0.0.1";
  args.videoPort = relay.frontPort;
  args.controlPort = args.videoPort;
  args.requireUdpHello = true;
  args.requireTcpControl = false;
  args.controlOverUdp = true;
  args.encodedFrameSink = &sink;  // controlIntervalMs: the product default (kClientControlIntervalMsDefault)
  bool connected = false;
  uint64_t extFirstUs = 0, extEndUs = 0;
  int64_t extFirstMs = -1, extEndMs = -1;
  if (relayUp && external) {
    say("RELAY_PORT=" + std::to_string(relay.frontPort));
    if (!outDir.empty()) {
      CreateDirectoryW(outDir.c_str(), nullptr);
      std::ofstream(outDir + L"relay_port.txt") << relay.frontPort << "\n";
    }
    const bool came = wait_until([&] { return !relay.Reports().empty(); }, 120000);
    check("the external client's first report reached the host", came);
    if (came) {
      extFirstUs = relay.Reports().front().qpcUs;
      extFirstMs = local_now_ms();
      // r4 F2: one line a second -- what went through the relay each way.
      uint64_t up0 = relay.upDatagrams.load(), down0 = relay.downDatagrams.load();
      for (int t = 1; t <= externalSec; ++t) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (t == keyDropFrom) relay.mode.store(static_cast<int>(Impair::KeyDrop));
        if (t == keyDropTo) relay.mode.store(static_cast<int>(Impair::None));
        const uint64_t up1 = relay.upDatagrams.load(), down1 = relay.downDatagrams.load();
        say("RELAY t=" + std::to_string(t) + " up=" + std::to_string(up1 - up0) + " down=" + std::to_string(down1 - down0) +
            " reports=" + std::to_string(relay.Reports().size()) + " frontRecvErrors=" +
            std::to_string(relay.frontRecvErrors.load()) + " connReset=" + std::to_string(relay.frontConnReset.load()));
        up0 = up1;
        down0 = down1;
      }
      extEndUs = qpc_now_us();
      extEndMs = local_now_ms();
    }
  }
  if (relayUp && !external) {
    connected = session.Connect(args) &&
                wait_until([&] { return session.Snapshot().state == ClientSessionState::Connected; }, 15000);
    check("the shared session connected (UDP hello + tunnelled control)", connected,
          session.Snapshot().status + " / " + session.Snapshot().lastError);
  }

  int64_t judgeFromMs = -1, judgeToMs = -1, impairFromMs = -1, impairToMs = -1, endMs = -1;
  uint64_t judgeFromUs = 0, judgeToUs = 0, decodedAtFrom = 0, decodedAtTo = 0, reconnectUs = 0;
  bool reconnected = true;
  std::atomic<bool> inputStop{false};
  std::thread inputThread;
  if (connected) {
    const bool picture = wait_until([&] { return sink.frames() > 0; }, 20000);
    check("frames reach the phone's sink", picture, std::to_string(sink.frames()) + " frames");
    if (runtimeBitrate != 0) {
      // quality-6m r1: the bitrate the app's quality button asks for, through the same session call
      // (native_bridge nativeRequestRuntimeConfig -> RequestRuntimeConfig), not the host's command line.
      const bool queued = session.RequestRuntimeConfig(runtimeBitrate, fps);
      const std::string want = "runtime-config-applied seq=";
      const std::string wantRate = " bitrate=" + std::to_string(runtimeBitrate) + " ";
      std::string appliedLine;
      const bool applied = wait_until([&] {
        std::ifstream in(hostLogPath);
        std::string line;
        while (std::getline(in, line)) {
          if (line.find(want) != std::string::npos && line.find(wantRate) != std::string::npos) {
            appliedLine = line;
            return true;
          }
        }
        return false;
      }, 10000);
      check("the runtime request was queued (the session accepted it)", queued);
      check("the host APPLIED it (runtime-config-applied bitrate=" + std::to_string(runtimeBitrate) + ")", applied,
            appliedLine.substr(0, 200));
    }
    if (scenario == "inputload") {
      inputThread = std::thread([&] {
        int i = 0;
        while (!inputStop.load()) {
          (void)session.QueueInputEvent(1, 100 + (i % 200), 100 + (i % 120), 0, 0, 0);  // a mouse move
          if (++i % 125 == 0) (void)session.RequestWindowList();                    // every ~2 s
          std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
      });
    }
    std::this_thread::sleep_for(std::chrono::seconds(warmSec));
    if (healthy) {
      judgeFromMs = local_now_ms();
      judgeFromUs = qpc_now_us();
      decodedAtFrom = sink.decoded_total();
      const uint64_t endAtUs = judgeFromUs + static_cast<uint64_t>(judgeSec) * 1000000ull;
      if (scenario == "static") {
        std::this_thread::sleep_for(std::chrono::seconds(20));
        gSourceStill.store(false);
        say("--- the source starts moving");
      }
      if (scenario == "reconnect") {
        std::this_thread::sleep_for(std::chrono::seconds(15));
        session.Disconnect();
        reconnectUs = qpc_now_us();
        reconnected = session.Connect(args) &&
                      wait_until([&] { return session.Snapshot().state == ClientSessionState::Connected; }, 15000);
        say(std::string("--- disconnected and connected again: ") + (reconnected ? "connected" : "FAILED"));
      }
      while (qpc_now_us() < endAtUs) std::this_thread::sleep_for(std::chrono::milliseconds(50));
      judgeToMs = local_now_ms();
      judgeToUs = qpc_now_us();
      decodedAtTo = sink.decoded_total();
    } else {
      impairFromMs = local_now_ms();
      say("--- impairment " + scenario + " on for " + std::to_string(impairSec) + " s");
      if (decodeStall) sink.stall.store(true);
      relay.mode.store(static_cast<int>(impair));
      std::this_thread::sleep_for(std::chrono::seconds(impairSec));
      relay.mode.store(static_cast<int>(Impair::None));
      sink.stall.store(false);
      impairToMs = local_now_ms();
      say("--- impairment off");
      std::this_thread::sleep_for(std::chrono::seconds(afterSec));
    }
    endMs = local_now_ms();
  }
  inputStop.store(true);
  if (inputThread.joinable()) inputThread.join();
  const uint64_t frames = sink.frames();
  const uint64_t drains = sink.drains();
  session.Disconnect();
  const std::vector<SeenReport> reports = relay.Reports();
  const auto ctlEvents = relay.CtlEvents();
  relay.Stop();
  if (hostPi.hProcess) {
    if (WaitForSingleObject(hostPi.hProcess, 90000) != WAIT_OBJECT_0) {
      say("NOTE  host did not exit by itself; terminated");
      TerminateProcess(hostPi.hProcess, 0);
      WaitForSingleObject(hostPi.hProcess, 5000);
    }
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  CloseHandle(job);
  if (launched) {
    std::string captureLine;
    const bool own = e2e_host_captured_window(hostLogPath, target.hwnd(), &captureLine);
    check("the host captured the window this test paints (its hwnd, this pid)", own, captureLine);
  }
  target.Stop();

  // ------------------------------------------------------------------ the host's account
  struct AbrLine {
    int64_t ms;
    std::string reason, profile, evidence, raw;
  };
  struct HostSec {
    int64_t ms;
    uint64_t sentFrames;
    std::string profile;
  };
  std::vector<AbrLine> abr;
  std::vector<HostSec> hostSecs;
  std::vector<std::string> keySizes;  // quality-6m r1: the host's encoded size, each time it changes (key frames)
  {
    std::ifstream in(hostLogPath);
    std::string line;
    std::string lastKeySize;
    while (std::getline(in, line)) {
      if (line.find("[keyframe] seq=") != std::string::npos) {
        const std::string size = field_of(line, "size");
        if (!size.empty() && size != lastKeySize) {
          keySizes.push_back(line.substr(0, 18) + " size=" + size + " bitrate=" + field_of(line, "bitrate") +
                             " reasons=" + field_of(line, "reasons"));
          lastKeySize = size;
        }
      }
      if (line.find("[abr] profile=") != std::string::npos) {
        abr.push_back({host_line_ms(line), field_of(line, "reason"), field_of(line, "profile"),
                       field_of(line, "evidence"), line});
      } else if (line.find("[native-video-host] encodedFrames=") != std::string::npos &&
                 line.find(" abrProfile=") != std::string::npos) {
        hostSecs.push_back({host_line_ms(line), std::strtoull(field_of(line, "sentFrames").c_str(), nullptr, 10),
                            field_of(line, "abrProfile")});
      }
    }
  }
  say("\n--- host [abr] lines (" + std::to_string(abr.size()) + ") ---");
  for (const auto& a : abr) say(a.raw.substr(0, 520));
  say("phone sink: " + std::to_string(frames) + " frames, " + std::to_string(drains) + " presentation drains; relay saw " +
      std::to_string(reports.size()) + " ControlClientMetrics");
  say("\n--- host encoded size changes (key frames) ---");
  for (const auto& k : keySizes) say(k);
  {
    const auto ls = sink.last_size();
    say("phone sink: the last frame's size " + std::to_string(ls.first) + "x" + std::to_string(ls.second) +
        " (source " + std::to_string(gTargetW) + "x" + std::to_string(gTargetH) + ")");
  }

  // Steps between profiles inside [from, to], in order: down (rank grows) and up.
  auto steps = [&](int64_t from, int64_t to, bool down) {
    std::string s;
    std::string prev;
    for (const auto& h : hostSecs) if (h.ms >= 0 && h.ms < from) prev = h.profile;
    for (const auto& a : abr) {
      if (a.ms < from || a.ms > to) continue;
      const int was = profile_rank(prev.empty() ? "high" : prev);
      const int now = profile_rank(a.profile);
      if ((down && now > was) || (!down && now < was)) s += a.reason + "->" + a.profile + "(" + a.evidence + ") ";
      prev = a.profile;
    }
    return s;
  };
  auto profile_at = [&](int64_t ms, bool firstAfterIfNone) {
    std::string p;
    for (const auto& h : hostSecs) if (h.ms >= 0 && h.ms < ms) p = h.profile;
    if (p.empty() && firstAfterIfNone) {
      for (const auto& h : hostSecs) if (h.ms >= ms) return h.profile;
    }
    return p;
  };
  auto judged_seconds = [&](int64_t from, int64_t to) {
    uint32_t n = 0;
    for (const auto& h : hostSecs) if (h.ms >= from && h.ms <= to && h.sentFrames >= std::max<uint32_t>(2, fps / 2)) ++n;
    return n;
  };

  if (external && extFirstUs) {
    auto span = [&](uint64_t fromS, uint64_t toS) {
      std::vector<SeenReport> v;
      for (const auto& r : reports)
        if (r.qpcUs >= extFirstUs + fromS * 1000000ull && r.qpcUs < extFirstUs + toS * 1000000ull) v.push_back(r);
      return v;
    };
    auto max_gap = [](const std::vector<SeenReport>& v) {
      uint64_t g = 0;
      for (size_t i = 1; i < v.size(); ++i) g = std::max<uint64_t>(g, v[i].qpcUs - v[i - 1].qpcUs);
      return g;
    };
    // r5 plan app: the APK's own order (smoke steps: +20 leave, +23 back, +30 refused selection, +31.5 its
    // failure path, +36 resume, +45 fault A, +58 fault B with keyframes dropped --keydrop 57:66).
    if (externalPlan == "app") {
      const auto s1 = span(2, 18);
      check("app: 14-18 reports in the steady 2..18 s, no gap of 3 s", s1.size() >= 14 && s1.size() <= 18 && max_gap(s1) < 3000000ull,
            std::to_string(s1.size()) + " reports, max gap " + std::to_string(max_gap(s1) / 1000) + " ms");
      say("NOTE  app: reports while left (+21..+23 s): " + std::to_string(span(21, 23).size()));
      const auto back = span(25, 30);
      check("app: reports again after coming back (+25..+30 s: >= 4)", back.size() >= 4, std::to_string(back.size()));
      say("NOTE  app: reports during the refused selection and its failure path (+30..+36 s): " +
          std::to_string(span(30, 36).size()));
      const auto resumed = span(39, 45);
      check("app: reports again after resuming (+39..+45 s: >= 5)", resumed.size() >= 5, std::to_string(resumed.size()));
      const auto afterA = span(48, 57);
      check("app: fault A (abandoned while active, the IDR comes): reports go on (+48..+57 s: >= 8, no 3 s gap)",
            afterA.size() >= 8 && max_gap(afterA) < 3000000ull,
            std::to_string(afterA.size()) + " reports, max gap " + std::to_string(max_gap(afterA) / 1000) + " ms");
      const std::string downsHealthy = steps(extFirstMs - 2000, extFirstMs + 57000, true);
      check("app: THE HOST'S ABR DOES NOT STEP DOWN through the normal lifecycle and fault A (0..57 s)",
            downsHealthy.empty(), downsHealthy.empty() ? "none" : downsHealthy);
      const std::string downsB = steps(extFirstMs + 58000, extFirstMs + 72000, true);
      check("app: fault B (no IDR can come): after the grace the host sees the stall and steps down",
            !downsB.empty(), downsB.empty() ? "none" : downsB);
    }
    const auto steady = span(2, 22);
    double dec = 0;
    for (const auto& r : steady) dec += r.decodedFpsX100 / 100.0;
    if (externalPlan == "r3") {
    check("external: 18-22 reports in the 20 steady seconds, no gap of 3 s",
          steady.size() >= 18 && steady.size() <= 22 && max_gap(steady) < 3000000ull,
          std::to_string(steady.size()) + " reports, max gap " + std::to_string(max_gap(steady) / 1000) +
              " ms, mean reported decoded " + std::to_string(steady.empty() ? 0.0 : dec / steady.size()) + " fps");
    const auto paused = span(26, 28);
    say("NOTE  external: reports while the surface was off and the stream paused (+26..+28 s): " +
        std::to_string(paused.size()));
    const auto resumed = span(31, 43);
    check("external: reports again after the surface and the stream came back (+31..+43 s: >= 9)",
          resumed.size() >= 9, std::to_string(resumed.size()) + " reports, max gap " +
                                   std::to_string(max_gap(resumed) / 1000) + " ms");
    const auto tail = span(50, static_cast<uint64_t>(std::max(51, externalSec - 2)));
    check("external: reports after the refused selection (+50 s on: no long gap)",
          !tail.empty() && max_gap(tail) < 3000000ull,
          std::to_string(tail.size()) + " reports, max gap " + std::to_string(max_gap(tail) / 1000) + " ms");
    const std::string downs = steps(extFirstMs - 2000, extEndMs, true);
    check("external: THE HOST'S ABR DOES NOT STEP DOWN through the run", downs.empty(), downs.empty() ? "none" : downs);
    }
  }
  if (connected) {
    if (healthy) {
      check("the host's ABR was at high when the judged span began", profile_at(judgeFromMs, true) == "high",
            "abrProfile=" + profile_at(judgeFromMs, true));
      const uint32_t judged = judged_seconds(judgeFromMs, judgeToMs);
      const bool cadence = scenario == "static" ? judged >= 30u : judged >= static_cast<uint32_t>(judgeSec * 3 / 4);
      check("the host sent a real cadence through the judged span (ABR-judged seconds)", cadence,
            std::to_string(judged) + " of " + std::to_string(judgeSec) + " s");
      if (scenario == "reconnect") check("the reconnect connected", reconnected);
      const std::string downs = steps(judgeFromMs, judgeToMs, true);
      check("THE HOST'S ABR DOES NOT STEP DOWN ON A HEALTHY PHONE (" + scenario + ")", downs.empty(),
            downs.empty() ? "none" : downs);
      check("...and is still at high at the end", profile_at(judgeToMs, false) == "high",
            "abrProfile=" + profile_at(judgeToMs, false));
      // The reports, where the host receives them.
      std::vector<SeenReport> span;
      for (const auto& r : reports) if (r.qpcUs >= judgeFromUs && r.qpcUs < judgeToUs) span.push_back(r);
      uint64_t maxGapUs = 0;
      for (size_t i = 1; i < span.size(); ++i) maxGapUs = std::max<uint64_t>(maxGapUs, span[i].qpcUs - span[i - 1].qpcUs);
      if (!span.empty()) maxGapUs = std::max<uint64_t>({maxGapUs, span.front().qpcUs - judgeFromUs, judgeToUs - span.back().qpcUs});
      // A reconnect's own gap (the session is gone for a moment) is the one gap allowed to exceed the bound.
      uint64_t maxGapOutsideReconnect = 0;
      for (size_t i = 1; i < span.size(); ++i) {
        const bool acrossReconnect = reconnectUs && span[i - 1].qpcUs < reconnectUs && span[i].qpcUs >= reconnectUs;
        if (!acrossReconnect) maxGapOutsideReconnect = std::max<uint64_t>(maxGapOutsideReconnect, span[i].qpcUs - span[i - 1].qpcUs);
      }
      check("55-65 reports reach the host in the 60 s span", span.size() >= 55 && span.size() <= 65,
            std::to_string(span.size()) + " reports");
      check("no gap between reports of 3 s or more (the host's freshness gate)",
            (reconnectUs ? maxGapOutsideReconnect : maxGapUs) < 3000000ull,
            "max " + std::to_string((reconnectUs ? maxGapOutsideReconnect : maxGapUs) / 1000) + " ms" +
                (reconnectUs ? " (outside the reconnect; across it " + std::to_string(maxGapUs / 1000) + " ms)" : ""));
      uint64_t decSum = 0, decN = 0, overMax = 0;
      for (const auto& r : span) {
        if (r.width == 0 && r.decodedFpsX100 == 0 && r.recvFpsX100 == 0) continue;  // present-only
        decSum += r.decodedFpsX100;
        ++decN;
        if (r.decodedFpsX100 > fps * 160 || r.recvFpsX100 > fps * 160) ++overMax;
      }
      const double sinkFps = (decodedAtTo - decodedAtFrom) * 1e6 / static_cast<double>(judgeToUs - judgeFromUs);
      const double reportedFps = decN ? decSum / 100.0 / decN : 0.0;
      check("the reported decoded fps is what the sink decoded (within 15%, mean over the span)",
            decN > 0 && reportedFps >= sinkFps * 0.85 && reportedFps <= sinkFps * 1.15,
            "reported " + std::to_string(reportedFps) + " vs sink " + std::to_string(sinkFps) + " (" +
                std::to_string(decN) + " reports with decode fields)");
      check("no report above 1.6 x fps (no catch-up burst, none after the reconnect)", overMax == 0,
            std::to_string(overMax) + " reports over " + std::to_string(fps * 1.6));
    } else {
      const int64_t to = impairToMs + 3000;
      check("the host's ABR was at high before the impairment", profile_at(impairFromMs, false) == "high",
            "abrProfile=" + profile_at(impairFromMs, false));
      check("the impairment did something", impair == Impair::Delay || decodeStall || relay.dropped.load() > 0,
            std::to_string(relay.dropped.load()) + " datagrams dropped");
      const std::string downs = steps(impairFromMs, to, true);
      const uint32_t judged = judged_seconds(impairFromMs, to);
      if (judgedFault) {
        check("A REAL " + scenario + " STILL STEPS THE HOST'S ABR DOWN (within the impairment + 3 s)", !downs.empty(),
              (downs.empty() ? std::string("none") : downs) + " (" + std::to_string(judged) + " judged seconds)");
      } else {
        say("NOTE  " + scenario + " (recorded, not judged): down steps " + (downs.empty() ? std::string("none") : downs) +
            " (" + std::to_string(judged) + " judged seconds)");
      }
      if (recovery) {
        const std::string ups = steps(impairToMs, endMs, false);
        check("...AND STEPS BACK UP once the phone reports healthy decoding again (the ABR's own hold times)",
              !ups.empty(), ups.empty() ? "none in " + std::to_string(afterSec) + " s" : ups);
      }
    }
  }

  if (!outDir.empty()) {
    CreateDirectoryW(outDir.c_str(), nullptr);
    CopyFileW(hostLogPath.c_str(), (outDir + std::wstring(scenario.begin(), scenario.end()) + L"_host.log").c_str(), FALSE);
    {
      std::ofstream ctl(outDir + std::wstring(scenario.begin(), scenario.end()) + L"_control.csv");
      ctl << "tMs,dir,kind,streamId,messageSeq,fragOrMissing,fragCount,msgType\n";
      const uint64_t base = ctlEvents.empty() ? 0 : ctlEvents.front().qpcUs;
      const uint64_t firstReport = reports.empty() ? base : reports.front().qpcUs;
      for (const auto& e : ctlEvents)
        ctl << (static_cast<int64_t>(e.qpcUs) - static_cast<int64_t>(firstReport)) / 1000 << "," << (e.up ? "up" : "down") << ","
            << (e.kind == static_cast<uint16_t>(UdpPacketKind::ControlData) ? "data" :
                e.kind == static_cast<uint16_t>(UdpPacketKind::ControlAck) ? "ack" : "nack")
            << "," << e.streamId << "," << e.messageSeq << "," << e.frag << "," << e.fragCount << "," << e.msgType << "\n";
    }
    std::ofstream rep(outDir + std::wstring(scenario.begin(), scenario.end()) + L"_reports.csv");
    rep << "qpcUs,seq,decodedFpsX100,recvFpsX100,width,height,presentSamples\n";
    for (const auto& r : reports)
      rep << r.qpcUs << "," << r.seq << "," << r.decodedFpsX100 << "," << r.recvFpsX100 << "," << r.width << ","
          << r.height << "," << r.presentSamples << "\n";
  }
  if (!staging.Remove()) say("WARN  " + staging.why());
  say("\n" + std::to_string(gChecks) + " checks, " + std::to_string(gFailures) + " failed, " + std::to_string(gSkips) +
      " not judged");
  if (gFailures == 0 && gSkips == 0) {
    say("mobile_abr_session_e2e_test(" + scenario + "): PASS\nRESULT: PASS");
    return 0;
  }
  say("mobile_abr_session_e2e_test(" + scenario + "): FAIL\nRESULT: FAIL");
  return 1;
}

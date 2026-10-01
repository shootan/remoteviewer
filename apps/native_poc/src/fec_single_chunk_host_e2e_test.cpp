// fec-single-chunk-stride, end to end: a real, isolated host (GNLinkStream.exe) capturing a window
// of this process, once with the padded parity layout (REMOTE60_NATIVE_FEC_SINGLE_CHUNK_STRIDE=0,
// what every host in the field sends today) and once with the tight one (the default after this
// change), for three kinds of content -- a still page of text, the same page with a blinking caret
// and one creeping block, and busy video-like motion. MEASUREMENT of the wire, plus the checks
// that make the measurement meaningful:
//
//   - the host was started isolated (e2e_isolation.hpp) with input injection off, capturing only
//     this test's own composed, invisible, click-through window -- never the user's screen;
//   - the host's own startup line says which layout it runs (fecSingleChunkStride=tight|padded);
//   - the receiver is the product's shared session policy (SessionVideoPipeline: the real
//     assembler, in-order hold, NACK scheduler -- NACKs go to the real host and its real replay
//     answers them) and the product decoder on every delivered AU;
//   - lossless: every frame the host sent after the first arrives in order -- no gap, no
//     discontinuity, no keyframe request, nothing malformed, everything decodes -- under BOTH
//     layouts; then the wire: the host's own per-flow byte account (udpTxParityBytes and the
//     rest of its stats line) and this end's count of what arrived, tight against padded.
//   - --loss-permille P --loss-seed S: this end drops that share of video datagrams (by datagram
//     identity, so a replay of a lost chunk is a new draw) before the pipeline sees them, and the
//     NACK/FEC path of the real host is exercised; reported, compared, not gated (two live
//     captures are never the same frames).
//
//   remote60_fec_single_chunk_host_e2e_test [--host <GNLinkStream.exe>] [--seconds N]
//       [--content static|lowmotion|video|all] [--loss-permille P] [--loss-seed S] [--keep-dir]
//   (REMOTE60_ALLOW_HOST_E2E=1; exit 77 = skipped)
// display-capture (a composed window on the interactive desktop), gpu-or-software (encoder in the
// host, decoder here), network (loopback).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "e2e_isolation.hpp"
#include "mf_h264_codec.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"
#include "session_video_pipeline.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "control_resume_e2e_support.hpp"

#include <mfapi.h>
#include <mmsystem.h>

#pragma comment(lib, "mfplat.lib")

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;

namespace {

int gFails = 0;
int gCheckCount = 0;
void check(bool ok, const std::string& what, const std::string& detail = std::string()) {
  ++gCheckCount;
  if (ok) {
    std::printf("PASS %s\n", what.c_str());
  } else {
    std::printf("FAIL %s%s%s\n", what.c_str(), detail.empty() ? "" : " -- ", detail.c_str());
    ++gFails;
  }
}
std::string u(uint64_t v) { return std::to_string(v); }

// The host port is picked at run time per run (e2e_pick_free_udp_port), so runs and tests can overlap.
constexpr uint32_t kFps = 30;
constexpr uint32_t kBitrate = 1500000;  // the user's 1500 setting, where parity was 25% of payload
constexpr int kWinW = 640;
constexpr int kWinH = 360;

enum class Content { StaticText, LowMotion, Video };
const char* content_name(Content c) {
  switch (c) {
    case Content::StaticText: return "static";
    case Content::LowMotion: return "lowmotion";
    case Content::Video: return "video";
  }
  return "?";
}

// ---------------------------------------------------------------------------- the target

// An on-screen window the user cannot see or hit: alpha 1/255, WS_EX_TRANSPARENT, never activated,
// tool window (not in the host's window list, found by title). On screen because an off-screen
// window is never composed and the host then sends only synthetic refresh frames.
class Target {
 public:
  Content content = Content::StaticText;
  std::wstring title;
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
  static uint32_t xorshift(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  // The page: light background, lines of dark glyph-like cells. Identical every frame.
  void PaintPage(HDC dc) {
    RECT client{0, 0, kWinW, kWinH};
    HBRUSH back = CreateSolidBrush(RGB(236, 236, 230));
    FillRect(dc, &client, back);
    DeleteObject(back);
    uint32_t s = 0x1234567u;
    for (int line = 0; line < 22; ++line) {
      const int top = 12 + line * 15;
      for (int col = 0; col < 76; ++col) {
        const uint32_t r = xorshift(s);
        if ((r & 7u) == 0) continue;
        const int left = 10 + col * 8;
        for (int yy = top; yy < top + 10; ++yy) {
          for (int xx = left; xx < left + 6; ++xx) {
            const bool ink = ((r >> ((yy - top) % 8)) & 1u) || ((xx - left) == 2);
            if (ink) SetPixelV(dc, xx, yy, RGB(30, 30, 40));
          }
        }
      }
    }
  }
  void Paint(HDC dc) {
    EnsureSurface(dc);
    if (!memDc_) return;
    if (!pagePainted_) {
      PaintPage(memDc_);
      pagePainted_ = true;
    }
    if (content == Content::LowMotion) {
      // The caret cell and the block's track are repainted from the page, then the moving bits.
      HBRUSH back = CreateSolidBrush(RGB(236, 236, 230));
      RECT caret{620, 12, 623, 24};
      RECT track{0, kWinH - 20, kWinW, kWinH - 4};
      FillRect(memDc_, &caret, back);
      FillRect(memDc_, &track, back);
      DeleteObject(back);
      if ((frame_ / 15) % 2 == 0) {  // blinks every half second at 30 Hz
        HBRUSH ink = CreateSolidBrush(RGB(20, 20, 20));
        FillRect(memDc_, &caret, ink);
        DeleteObject(ink);
      }
      const int bx = static_cast<int>((frame_ * 2) % (kWinW - 16));
      RECT block{bx, kWinH - 20, bx + 16, kWinH - 4};
      HBRUSH grey = CreateSolidBrush(RGB(90, 90, 90));
      FillRect(memDc_, &block, grey);
      DeleteObject(grey);
    } else if (content == Content::Video) {
      uint32_t state = static_cast<uint32_t>(frame_ * 2654435761u + 1u);
      for (int y = kWinH / 4; y < kWinH * 3 / 4; ++y) {
        uint32_t* row = pixels_ + static_cast<size_t>(y) * kWinW;
        for (int x = 0; x < kWinW; ++x) {
          state ^= state << 13;
          state ^= state >> 17;
          state ^= state << 5;
          row[x] = state & 0x00FFFFFFu;
        }
      }
      const int barX = static_cast<int>((frame_ * 12) % (kWinW - 32));
      for (int y = 0; y < kWinH / 4; ++y) {
        uint32_t* row = pixels_ + static_cast<size_t>(y) * kWinW;
        for (int x = 0; x < kWinW; ++x) row[x] = (x >= barX && x < barX + 32) ? 0x00F0F0F0u : 0x00ECECE6u;
      }
    }
    BitBlt(dc, 0, 0, kWinW, kWinH, memDc_, 0, 0, SRCCOPY);
  }
  void EnsureSurface(HDC dc) {
    if (memDc_) return;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = kWinW;
    bi.bmiHeader.biHeight = -kWinH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    memDc_ = CreateCompatibleDC(dc);
    bitmap_ = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels_), nullptr, 0);
    SelectObject(memDc_, bitmap_);
  }
  static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Target*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_TIMER && self) {
      if (self->content != Content::StaticText) {
        ++self->frame_;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }
    if (msg == WM_PAINT && self) {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd, &ps);
      self->Paint(dc);
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
    wc.lpszClassName = L"Remote60FecSingleChunkTarget";
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                            wc.lpszClassName, title.c_str(), WS_POPUP, 0, 0, kWinW, kWinH, nullptr, nullptr,
                            wc.hInstance, nullptr);
    if (hwnd_) {
      SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
      SetLayeredWindowAttributes(hwnd_, 0, 1, LWA_ALPHA);
      ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
      timeBeginPeriod(1);
      SetTimer(hwnd_, 1, 33, nullptr);  // 30 Hz, the host's capture rate
    }
    ready_.store(true);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
    if (memDc_) DeleteDC(memDc_);
    if (bitmap_) DeleteObject(bitmap_);
    hwnd_ = nullptr;
  }
  std::thread thread_;
  std::atomic<bool> ready_{false};
  uint64_t frame_ = 0;
  bool pagePainted_ = false;
  HWND hwnd_ = nullptr;
  HDC memDc_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  uint32_t* pixels_ = nullptr;
};

// ------------------------------------------------------------------------------ host log

std::string value_of(const std::string& line, const std::string& key) {
  const std::string needle = " " + key + "=";
  const size_t at = line.find(needle);
  if (at == std::string::npos) return {};
  const size_t start = at + needle.size();
  const size_t end = line.find(' ', start);
  return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
}
uint64_t num_of(const std::string& line, const std::string& key) {
  const std::string v = value_of(line, key);
  return v.empty() ? 0 : std::strtoull(v.c_str(), nullptr, 10);
}

struct HostAccount {
  std::string layoutToken;  // fecSingleChunkStride=... from the host's own startup line
  size_t statsLines = 0;
  // Cumulative counters of the last stats line (udpTxFrames, not sentFrames: that one is per interval).
  uint64_t txFrames = 0, txBytes = 0, parity = 0, chunkHdr = 0, videoDatagrams = 0, nackBytes = 0,
           nackDatagrams = 0, wireEst = 0;
  size_t keyframeLines = 0;
};

HostAccount read_host_log(const std::wstring& path) {
  HostAccount a;
  std::ifstream in(path);
  std::string line;
  std::string last;
  while (std::getline(in, line)) {
    if (line.find(" fecSingleChunkStride=") != std::string::npos) a.layoutToken = value_of(line, "fecSingleChunkStride");
    if (line.find(" udpTxWireEstBytes=") != std::string::npos) {
      last = line;
      ++a.statsLines;
    }
    if (line.find("[keyframe]") != std::string::npos) ++a.keyframeLines;
  }
  if (!last.empty()) {
    a.txFrames = num_of(last, "udpTxFrames");
    a.txBytes = num_of(last, "udpTxBytes");
    a.parity = num_of(last, "udpTxParityBytes");
    a.chunkHdr = num_of(last, "udpTxChunkHdrBytes");
    a.videoDatagrams = num_of(last, "udpTxVideoDatagrams");
    a.nackBytes = num_of(last, "udpTxNackBytes");
    a.nackDatagrams = num_of(last, "udpTxNackDatagrams");
    a.wireEst = num_of(last, "udpTxWireEstBytes");
  }
  return a;
}

// ------------------------------------------------------------------------------- one run

struct RunResult {
  bool launched = false;
  bool hello = false;
  bool nackNegotiated = false;
  HostAccount host;
  // This end.
  uint64_t rxData = 0, rxParity = 0, rxHeader = 0, rxReplay = 0, rxVideoDatagrams = 0, rxDropped = 0;
  uint32_t delivered = 0, firstSeq = 0, lastSeq = 0, gaps = 0, maxGap = 0, keyReq = 0, disc = 0, nacks = 0,
           malformed = 0, fec = 0, giveUps = 0, singleChunkFrames = 0, keyFrames = 0;
  // Sequence gaps the pipeline saw closed by a complete keyframe (the host's own doing: an IDR
  // supersedes the sender's backlog, whose seqs are never sent -- respond_to_sequence_gap).
  uint32_t gapsBehindKey = 0;
  uint32_t decoded = 0, decodeErrors = 0;
  std::string decoder;
  // Isolation-matrix instrumentation (bitrate-hard-cap r2, completion criterion 3).
  std::vector<std::pair<uint64_t, uint32_t>> wireEvents;  // (arrival qpc us, bytes incl. +28)
  std::vector<uint64_t> deliverUs;        // when each AU was delivered (qpc)
  std::vector<uint64_t> captureStampUs;   // its header captureQpcUs (latency, same clock domain)
  std::vector<uint8_t> deliverWasKey;     // 1 if the delivered AU was a keyframe
  uint32_t appliedBitrate = 0, appliedFps = 0;
  uint64_t switchUs = 0;  // when the runtime bitrate downshift was sent (0 = none)
};

uint64_t fnv1a_u64(uint64_t h, uint64_t v) {
  const uint8_t* b = reinterpret_cast<const uint8_t*>(&v);
  for (size_t i = 0; i < sizeof(v); ++i) {
    h ^= b[i];
    h *= 1099511628211ull;
  }
  return h;
}

RunResult run_host(const std::wstring& hostExe, const std::wstring& runDir, Content content, bool tight,
                   uint16_t port, int seconds, uint32_t lossPermille, uint32_t lossSeed, bool wireCapOn,
                   uint32_t bitrate = kBitrate, uint32_t fps = kFps, uint32_t downshiftBitrate = 0,
                   uint32_t downshiftAtSec = 0) {
  RunResult r;
  CreateDirectoryW(runDir.c_str(), nullptr);
  const std::wstring me = self_path();
  const bool staged = CopyFileW(hostExe.c_str(), (runDir + L"GNLinkStream.exe").c_str(), FALSE) &&
                      CopyFileW(me.c_str(), (runDir + L"GNLinkCapture.exe").c_str(), FALSE);
  check(staged, "the host could be staged into the run directory", std::string(hostExe.begin(), hostExe.end()));
  if (!staged) return r;

  Target target;
  target.content = content;
  target.title = L"remote60 fec single-chunk target " + std::to_wstring(GetCurrentProcessId()) + L" " +
                 std::wstring(tight ? L"tight" : L"padded");
  check(target.Start(), "an on-screen, invisible, click-through window is up for the host to capture");
  const HWND targetHwnd = target.hwnd();

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  const std::wstring hostLogPath = runDir + L"host.log";
  HANDLE hostLog = INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION hostPi{};
  {
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_STATS_PRINT_EVERY_SEC", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_FEC_SINGLE_CHUNK_STRIDE", tight ? L"1" : L"0");
    // The FEC-layout runs pin the wire cap OFF (this test's original purpose is the parity layout,
    // and the cap paces a large AU over its send time, which is orthogonal). A separate cap-ON
    // lossless run below asserts "lossless -> premature NACK 0" so the pin does not erase the cap-ON
    // evidence that the r2 progress-based tail fix holds on real hardware. (bitrate-hard-cap r2.)
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_WIRE_CAP", wireCapOn ? L"1" : L"0");
    std::wstring cmd = L"\"" + runDir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                       L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(port) + L" --fps " +
                       std::to_wstring(fps) + L" --bitrate " + std::to_wstring(bitrate) + L" --seconds " +
                       std::to_wstring(seconds + 30) + L" --input-injection-mode none" +
                       remote60::native_poc::e2e::e2e_capture_window_args(target.title);
    const bool noInjection = cmd.find(L"--input-injection-mode none") != std::wstring::npos;
    check(noInjection, "the host is started with input injection off");
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    hostLog = CreateFileW(hostLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hostLog != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = hostLog;
      si.hStdError = hostLog;
    }
    const std::wstring isoAppData = runDir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> isoEnv = e2e_isolated_environment(isoAppData);
    std::string isoWhy;
    const bool isoOk = e2e_command_is_isolated(cmd, runDir, &isoWhy) &&
                       e2e_path_is_under(e2e_block_localappdata(isoEnv), runDir);
    check(isoOk, "the launched host is isolated from the user's files", isoWhy);
    r.launched = noInjection && isoOk &&
                 CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, hostLog != INVALID_HANDLE_VALUE,
                                CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, isoEnv.data(),
                                runDir.c_str(), &si, &hostPi) != 0;
    if (r.launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  check(r.launched, "the host started");

  std::vector<std::pair<std::vector<uint8_t>, std::pair<bool, uint64_t>>> deliveredAus;  // payload, (key, stamp)
  if (r.launched) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_port = htons(port);
    InetPtonW(AF_INET, L"127.0.0.1", &hostAddr.sin_addr);
    connect(sock, reinterpret_cast<const sockaddr*>(&hostAddr), sizeof(hostAddr));
    DWORD rcvTimeout = 10;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));
    int rcvBuf = 4 << 20;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));

    UdpHelloOptions hello;
    hello.budgetMs = 20000;
    hello.sliceMaxMs = 250;
    hello.retrySleepMs = 50;
    hello.requestNack = true;  // the Windows viewer and the session both ask for NACK
    std::string helloError;
    uint32_t ackFeatures = 0;
    r.hello = udp_hello_handshake(sock, hello, nullptr, &helloError, &ackFeatures, nullptr);
    check(r.hello, "the viewer's Hello is answered", helloError);
    r.nackNegotiated = (ackFeatures & kUdpFeatureVideoNack) != 0;

    UdpControlChannel control;
    control.Configure(
        [&sock](const void* data, size_t len) {
          return send(sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, 1200);
    UdpControlLink link(&control, 12000);

    // The product's receive policy, fed from this socket.
    std::mutex pmu;
    std::atomic<bool> pendingKeyRequest{false};
    uint32_t lastDelivered = 0;
    SessionVideoPipelineConfig cfg;
    cfg.nackEnabled = r.nackNegotiated;
    cfg.holdUs = r.nackNegotiated ? 120000 : 0;
    cfg.maxConcurrent = 8;
    SessionVideoPipeline::Callbacks cb;
    cb.deliver = [&](UdpH264AssembledFrame&& f) {
      ++r.delivered;
      if (r.firstSeq == 0) r.firstSeq = f.header.seq;
      if (lastDelivered != 0 && f.header.seq > lastDelivered + 1) {
        ++r.gaps;
        r.maxGap = std::max(r.maxGap, f.header.seq - lastDelivered - 1);
      }
      lastDelivered = f.header.seq;
      r.lastSeq = f.header.seq;
      const bool key = (f.header.flags & kEncodedFrameFlagKeyFrame) != 0;
      if (key) ++r.keyFrames;
      r.deliverUs.push_back(qpc_now_us());
      r.captureStampUs.push_back(f.header.captureQpcUs);
      r.deliverWasKey.push_back(key ? 1 : 0);
      deliveredAus.emplace_back(std::move(f.payload), std::make_pair(key, f.header.captureQpcUs));
    };
    cb.requestKeyframe = [&] {
      ++r.keyReq;
      pendingKeyRequest.store(true);
    };
    cb.discontinuity = [&] { ++r.disc; };
    cb.sendNack = [&](const UdpVideoNackPacket& p) {
      ++r.nacks;
      (void)send(sock, reinterpret_cast<const char*>(&p), sizeof(p), 0);  // to the real host
    };
    SessionVideoPipeline pipeline(cfg, cb);

    std::atomic<bool> stop{false};
    std::map<uint64_t, uint32_t> occurrences;
    std::thread ingress([&] {
      std::vector<uint8_t> buf(2048);
      while (!stop.load()) {
        control.Tick();
        const int n = recv(sock, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
        const uint64_t now = qpc_now_us();
        if (n > 0 && !control.OnPacket(buf.data(), static_cast<size_t>(n)) &&
            n >= static_cast<int>(sizeof(UdpVideoChunkHeader))) {
          UdpVideoChunkHeader h{};
          std::memcpy(&h, buf.data(), sizeof(h));
          if (h.magic == kMagic && h.kind == static_cast<uint16_t>(UdpPacketKind::VideoChunk) &&
              h.size == sizeof(UdpVideoChunkHeader)) {
            const bool parity = (h.flags & 0x10u) != 0;
            const uint64_t id = (static_cast<uint64_t>(h.streamGeneration) << 48) ^ (static_cast<uint64_t>(h.seq) << 24) ^
                                (static_cast<uint64_t>(h.chunkIndex) << 1) ^ (parity ? 1u : 0u);
            const uint32_t occ = ++occurrences[id];
            const uint32_t body = static_cast<uint32_t>(n) - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
            ++r.rxVideoDatagrams;
            r.rxHeader += sizeof(UdpVideoChunkHeader);
            r.wireEvents.emplace_back(now, static_cast<uint32_t>(n) + 28u);  // IP+UDP for the window math
            if (occ > 1) r.rxReplay += static_cast<uint64_t>(n);
            else if (parity) r.rxParity += body;
            else r.rxData += body;
            if (!parity && h.chunkCount == 1 && occ == 1) ++r.singleChunkFrames;
            bool drop = false;
            if (lossPermille > 0) {
              uint64_t x = fnv1a_u64(0xcbf29ce484222325ull ^ lossSeed, id);
              x = fnv1a_u64(x, occ);
              drop = (x % 1000u) < lossPermille;
            }
            if (drop) {
              ++r.rxDropped;
            } else {
              std::lock_guard<std::mutex> lk(pmu);
              pipeline.OnDatagram(buf.data(), static_cast<size_t>(n), now);
            }
            continue;
          }
        }
        std::lock_guard<std::mutex> lk(pmu);
        pipeline.OnTick(now);
      }
    });

    // The viewer's control thread: pings keep the session served; keyframe requests go out the
    // way the product sends them.
    std::atomic<bool> controlStop{false};
    std::thread controlThread([&] {
      ClientControlScheduler scheduler;
      WindowPanelStateModel windowPanel;
      StreamStateControl streamState;
      CaptureModeRequestState captureMode;
      KeyframeRequestState keyframe(120000, 300000, 3);
      RuntimeTuneState runtimeTune(300000, 30000000, 250000, 1, 240);
      ClientInputQueue inputQueue;
      scheduler.Reset(kClientControlIntervalMsDefault, qpc_now_us());
      const uint64_t downshiftSendUs = downshiftBitrate > 0 ? qpc_now_us() + downshiftAtSec * 1'000'000ull : 0;
      bool downsent = false;
      uint32_t dseq = 50000;
      while (!controlStop.load()) {
        if (downshiftSendUs && !downsent && qpc_now_us() >= downshiftSendUs) {
          ControlRuntimeEncoderConfigMessage m{};
          m.header.magic = kMagic;
          m.header.type = static_cast<uint16_t>(MessageType::ControlRuntimeEncoderConfig);
          m.header.size = static_cast<uint16_t>(sizeof(m));
          m.seq = ++dseq;
          m.bitrate = downshiftBitrate;
          m.flags = 0x1u;  // bitrate valid
          m.clientSendQpcUs = qpc_now_us();
          if (link.Write(&m, sizeof(m)) && link.EndMessage()) {
            r.switchUs = qpc_now_us();
            downsent = true;
          }
        }
        if (pendingKeyRequest.exchange(false)) (void)keyframe.Request(2, qpc_now_us());
        ClientControlMetricsSnapshot metrics{};
        ControlOutboundAction action{};
        const uint64_t now = qpc_now_us();
        if (scheduler.NextAction(now, metrics, &windowPanel, &streamState, &captureMode, &keyframe, &runtimeTune,
                                 &inputQueue, &action, nullptr, nullptr)) {
          TcpControlResponse response;
          if (execute_control_action(link, action, &response) && action.kind == ControlOutboundActionKind::Ping) {
            scheduler.OnPingCompleted(qpc_now_us());
          }
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
      }
    });

    std::this_thread::sleep_for(std::chrono::seconds(seconds));

    controlStop.store(true);
    if (controlThread.joinable()) controlThread.join();
    stop.store(true);
    if (ingress.joinable()) ingress.join();
    {
      std::lock_guard<std::mutex> lk(pmu);
      r.malformed = static_cast<uint32_t>(pipeline.stats().malformed);
      r.fec = static_cast<uint32_t>(pipeline.stats().fecRecoveredChunks);
      r.giveUps = static_cast<uint32_t>(pipeline.stats().stuckHeadGiveUps);
      r.gapsBehindKey = static_cast<uint32_t>(pipeline.stats().gapsBehindCompleteKey);
    }
    closesocket(sock);
  }

  target.Stop();
  if (hostPi.hProcess) {
    TerminateProcess(hostPi.hProcess, 0);
    WaitForSingleObject(hostPi.hProcess, 5000);
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  CloseHandle(job);
  r.host = read_host_log(hostLogPath);
  {
    std::string captureLine;
    const bool own = remote60::native_poc::e2e::e2e_host_captured_window(hostLogPath, targetHwnd, &captureLine);
    std::printf("capture: %s (this pid %lu)\n", captureLine.c_str(), static_cast<unsigned long>(GetCurrentProcessId()));
    check(own, std::string(tight ? "tight" : "padded") +
                   ": the host captured the window this test paints (its hwnd, this pid), not another test's",
          captureLine);
  }

  // Everything delivered, through the product decoder.
  if (!deliveredAus.empty()) {
    H264Decoder dec;
    if (dec.initialize(kWinW, kWinH, fps)) {
      r.decoder = dec.backend_name();
      int64_t stamp = 10000000;
      for (const auto& au : deliveredAus) {
        std::vector<DecodedFrameNv12> out;
        bool overflow = false;
        stamp += 333333;
        if (!dec.decode_access_unit(au.first, au.second.first, stamp, &out, &overflow)) ++r.decodeErrors;
        r.decoded += static_cast<uint32_t>(out.size());
      }
      dec.shutdown();
    } else {
      r.decoder = "init-failed";
    }
  }
  r.appliedBitrate = bitrate;
  r.appliedFps = fps;
  return r;
}

void print_run(Content content, bool tight, uint32_t lossPermille, uint32_t lossSeed, const RunResult& r) {
  std::printf("HOSTRUN content=%s layout=%s hostSays=%s loss=%u seed=%u nack=%d hostTxFrames=%llu hostKeyLines=%zu"
              " hostTxBytes=%llu hostParity=%llu hostChunkHdr=%llu hostVideoDg=%llu hostNackBytes=%llu hostWireEst=%llu"
              " rxDelivered=%u rxFirstSeq=%u rxLastSeq=%u rxSingleChunk=%u rxKeyFrames=%u rxGaps=%u rxGapsBehindKey=%u rxMaxGap=%u"
              " rxKeyReq=%u rxDisc=%u rxNacks=%u rxFec=%u rxGiveUps=%u rxMalformed=%u rxDropped=%llu"
              " rxData=%llu rxParity=%llu rxHdr=%llu rxReplay=%llu rxVideoDg=%llu decoded=%u decErr=%u decoder=%s\n",
              content_name(content), tight ? "tight" : "padded", r.host.layoutToken.c_str(), lossPermille, lossSeed,
              r.nackNegotiated ? 1 : 0, static_cast<unsigned long long>(r.host.txFrames), r.host.keyframeLines,
              static_cast<unsigned long long>(r.host.txBytes), static_cast<unsigned long long>(r.host.parity),
              static_cast<unsigned long long>(r.host.chunkHdr), static_cast<unsigned long long>(r.host.videoDatagrams),
              static_cast<unsigned long long>(r.host.nackBytes), static_cast<unsigned long long>(r.host.wireEst),
              r.delivered, r.firstSeq, r.lastSeq, r.singleChunkFrames, r.keyFrames, r.gaps, r.gapsBehindKey, r.maxGap,
              r.keyReq, r.disc,
              r.nacks, r.fec, r.giveUps, r.malformed, static_cast<unsigned long long>(r.rxDropped),
              static_cast<unsigned long long>(r.rxData), static_cast<unsigned long long>(r.rxParity),
              static_cast<unsigned long long>(r.rxHeader), static_cast<unsigned long long>(r.rxReplay),
              static_cast<unsigned long long>(r.rxVideoDatagrams), r.decoded, r.decodeErrors, r.decoder.c_str());
}

// Peak bytes in any sliding window, as bits/s (two-pointer; wireEvents are in arrival order).
double window_peak_bps(const std::vector<std::pair<uint64_t, uint32_t>>& ev, uint64_t windowUs) {
  double peak = 0;
  size_t lo = 0;
  uint64_t sum = 0;
  for (size_t hi = 0; hi < ev.size(); ++hi) {
    sum += ev[hi].second;
    while (ev[hi].first - ev[lo].first >= windowUs) sum -= ev[lo++].second;
    const double bps = static_cast<double>(sum) * 8.0 * 1e6 / static_cast<double>(windowUs);
    if (bps > peak) peak = bps;
  }
  return peak;
}

// The isolation-matrix metrics for one run + the cap-window / minimal-delivery assertions.
void matrix_metrics(const char* label, uint64_t capBps, const RunResult& r) {
  const double p1s = window_peak_bps(r.wireEvents, 1'000'000);
  const double p250 = window_peak_bps(r.wireEvents, 250'000);
  double fps = 0, firstFrameMs = 0, freezeMaxMs = 0, latP95Ms = 0, latMaxMs = 0, idrPerSec = 0;
  if (r.deliverUs.size() >= 2) {
    const uint64_t span = r.deliverUs.back() - r.deliverUs.front();
    if (span > 0) {
      fps = static_cast<double>(r.decoded) * 1e6 / static_cast<double>(span);
      idrPerSec = static_cast<double>(r.keyFrames) * 1e6 / static_cast<double>(span);
    }
    const uint64_t t0 = r.wireEvents.empty() ? r.deliverUs.front() : r.wireEvents.front().first;
    firstFrameMs = (r.deliverUs.front() - t0) / 1000.0;
    uint64_t gap = 0;
    for (size_t i = 1; i < r.deliverUs.size(); ++i) gap = std::max(gap, r.deliverUs[i] - r.deliverUs[i - 1]);
    freezeMaxMs = gap / 1000.0;
    std::vector<uint64_t> lat;
    for (size_t i = 0; i < r.deliverUs.size(); ++i)
      if (r.deliverUs[i] >= r.captureStampUs[i]) lat.push_back(r.deliverUs[i] - r.captureStampUs[i]);
    if (!lat.empty()) {
      std::sort(lat.begin(), lat.end());
      latP95Ms = lat[(lat.size() * 95) / 100] / 1000.0;
      latMaxMs = lat.back() / 1000.0;
    }
  }
  std::printf("MATRIX %s: cap=%llu applied=%u/%ufps | win1s=%.0f (%.1f%%) win250=%.0f (%.1f%%) | decoded=%u fps=%.1f "
              "firstFrame=%.0fms freezeMax=%.0fms latP95=%.0fms latMax=%.0fms idr/s=%.2f keyReq=%u disc=%u nacks=%u "
              "giveUps=%u decErr=%u decoder=%s rxDropped=%llu\n",
              label, (unsigned long long)capBps, r.appliedBitrate, r.appliedFps, p1s, 100.0 * p1s / capBps, p250,
              100.0 * p250 / capBps, r.decoded, fps, firstFrameMs, freezeMaxMs, latP95Ms, latMaxMs, idrPerSec, r.keyReq,
              r.disc, r.nacks, r.giveUps, r.decodeErrors, r.decoder.c_str(), (unsigned long long)r.rxDropped);
  const std::string tag = std::string("matrix ") + label;
  check(p1s <= capBps * 1.10, tag + ": every 1 s window <= cap +10%");
  check(p250 <= capBps * 1.10, tag + ": every 250 ms window <= cap +10%");
  check(r.decoded >= 2 && r.decodeErrors == 0, tag + ": the decoder actually produced frames (not starved)");
  check(firstFrameMs >= 0 && firstFrameMs <= 3000, tag + ": the first frame arrives within a bounded time");
}

// A runtime 6 -> 1.5 Mbps downshift: windows wholly after the switch (+1.5 s settle) must respect the
// new, lower cap -- the credit is preserved across the change, not refilled.
void matrix_metrics_downshift(const RunResult& r) {
  std::vector<std::pair<uint64_t, uint32_t>> before, after;
  for (const auto& e : r.wireEvents) {
    if (r.switchUs && e.first < r.switchUs) before.push_back(e);
    else if (r.switchUs && e.first >= r.switchUs + 1'500'000) after.push_back(e);
  }
  const double b1 = window_peak_bps(before, 1'000'000);
  const double a1 = window_peak_bps(after, 1'000'000);
  const double a250 = window_peak_bps(after, 250'000);
  std::printf("MATRIX 6->1.5-downshift: switched=%d before1s=%.0f (%.1f%% of 6M) after1s=%.0f (%.1f%% of 1.5M) "
              "after250=%.0f (%.1f%%) decoded=%u decErr=%u beforeEv=%zu afterEv=%zu\n",
              r.switchUs != 0 ? 1 : 0, b1, 100.0 * b1 / 6'000'000.0, a1, 100.0 * a1 / 1'500'000.0, a250,
              100.0 * a250 / 1'500'000.0, r.decoded, r.decodeErrors, before.size(), after.size());
  check(r.switchUs != 0, "matrix downshift: the runtime bitrate change was sent");
  check(before.empty() || b1 <= 6'000'000 * 1.10, "matrix downshift: before the switch, 1 s window <= 6 Mbps +10%");
  check(!after.empty() && a1 <= 1'500'000 * 1.10, "matrix downshift: after the switch (settled), 1 s window <= 1.5 Mbps +10%");
  check(after.empty() || a250 <= 1'500'000 * 1.10, "matrix downshift: after the switch, 250 ms window <= 1.5 Mbps +10%");
}

// completion criterion 3: the product-equivalent host + real MFT + real decoder, across the scenario
// matrix, each with the cap ON and every 1 s / 250 ms window measured on the real UDP receive.
int run_matrix(const std::wstring& hostExe, const std::wstring& dir, int seconds) {
  struct Scn {
    const char* label;
    Content content;
    uint32_t bitrate, fps, lossPermille;
  };
  const Scn scns[] = {
      {"6M/60-motion", Content::Video, 6'000'000, 60, 0},
      {"6M/60-static", Content::StaticText, 6'000'000, 60, 0},
      {"1.5M/30-motion", Content::Video, 1'500'000, 30, 0},
      {"6M/60-loss5%-nack", Content::Video, 6'000'000, 60, 50},
  };
  for (const Scn& s : scns) {
    const uint16_t port = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    if (port == 0) continue;
    std::wstring safe(s.label, s.label + std::strlen(s.label));
    for (wchar_t& c : safe)
      if (c == L'/' || c == L'%' || c == L'.') c = L'_';  // a run-dir name, not a path
    const std::wstring runDir = dir + L"m_" + safe + L"\\";
    const RunResult r = run_host(hostExe, runDir, s.content, /*tight=*/true, port, seconds, s.lossPermille, 1,
                                 /*wireCapOn=*/true, s.bitrate, s.fps);
    matrix_metrics(s.label, s.bitrate, r);
  }
  // Runtime 6 -> 1.5 Mbps downshift mid-run.
  const uint16_t dport = remote60::native_poc::e2e::e2e_pick_free_udp_port();
  if (dport != 0) {
    const RunResult r = run_host(hostExe, dir + L"m_downshift\\", Content::Video, /*tight=*/true, dport,
                                 std::max(seconds, 10), 0, 1, /*wireCapOn=*/true, 6'000'000, 60,
                                 /*downshiftBitrate=*/1'500'000, /*downshiftAtSec=*/static_cast<uint32_t>(std::max(seconds, 10) / 2));
    matrix_metrics_downshift(r);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::wstring hostExe;
  int seconds = 12;
  std::string contentArg = "all";
  uint32_t lossPermille = 0;
  uint32_t lossSeed = 1;
  bool keepDir = false;
  bool matrix = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--host" && i + 1 < argc) {
      const std::string v = argv[++i];
      hostExe.assign(v.begin(), v.end());
    } else if (a == "--seconds" && i + 1 < argc) {
      seconds = std::max(4, std::atoi(argv[++i]));
    } else if (a == "--content" && i + 1 < argc) {
      contentArg = argv[++i];
    } else if (a == "--loss-permille" && i + 1 < argc) {
      lossPermille = static_cast<uint32_t>(std::atoi(argv[++i]));
    } else if (a == "--loss-seed" && i + 1 < argc) {
      lossSeed = static_cast<uint32_t>(std::atoi(argv[++i]));
    } else if (a == "--keep-dir") {
      keepDir = true;
    } else if (a == "--matrix") {
      matrix = true;
    }
  }
  char allow[8]{};
  if (GetEnvironmentVariableA("REMOTE60_ALLOW_HOST_E2E", allow, sizeof(allow)) == 0 || std::string(allow) != "1") {
    std::printf("SKIP fec_single_chunk_host_e2e_test: starts a real (isolated) GNLinkStream.exe capturing a window of this"
                " process.\n      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return kE2eSkippedExit;
  }
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
  if (FAILED(MFStartup(MF_VERSION))) {
    std::printf("FAIL MFStartup\n");
    return 1;
  }
  // Staged inside the repository's test scratch root, never %TEMP%; each run's host gets its own
  // subdirectory beneath it. Removed when main returns, whichever way it returns, unless --keep-dir.
  remote60::native_poc::e2e::StagingDir staging;
  if (!staging.Create(L"fec_e2e")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  staging.set_keep(keepDir, keepDir ? "--keep-dir" : "");
  const std::wstring dir = staging.path();
  const std::wstring me = self_path();
  if (hostExe.empty()) hostExe = directory_of(me) + L"GNLinkStream.exe";
  std::printf("fec_single_chunk_host_e2e_test: host=%s seconds=%d content=%s loss=%u/1000 seed=%u\n",
              std::string(hostExe.begin(), hostExe.end()).c_str(), seconds, contentArg.c_str(), lossPermille, lossSeed);

  std::vector<Content> contents;
  if (contentArg == "all") contents = {Content::StaticText, Content::LowMotion, Content::Video};
  else if (contentArg == "static") contents = {Content::StaticText};
  else if (contentArg == "lowmotion") contents = {Content::LowMotion};
  else if (contentArg == "video") contents = {Content::Video};
  else {
    std::printf("usage: --content static|lowmotion|video|all\n");
    return 1;
  }

  if (matrix) {
    run_matrix(hostExe, dir, seconds);
  } else
  for (Content content : contents) {
    const char* cls = content_name(content);
    RunResult res[2];
    for (int layout = 0; layout < 2; ++layout) {
      const bool tight = layout == 1;  // padded first: what the field sends today
      const std::wstring runDir = dir + std::wstring(cls, cls + std::strlen(cls)) + L"_" +
                                  (tight ? L"tight" : L"padded") + L"\\";
      const uint16_t port = remote60::native_poc::e2e::e2e_pick_free_udp_port();
      check(port != 0, std::string(cls) + (tight ? " tight" : " padded") + ": a free UDP port for the host", u(port));
      if (port == 0) {
        // --bind-port 0 would send the host to the product's default port: nothing is started.
        std::printf("SKIP-LAUNCH %s %s: no free port, the host is not started\n", cls, tight ? "tight" : "padded");
        continue;
      }
      res[layout] = run_host(hostExe, runDir, content, tight, port, seconds, lossPermille, lossSeed,
                             /*wireCapOn=*/false);
      print_run(content, tight, lossPermille, lossSeed, res[layout]);
      const RunResult& r = res[layout];
      const std::string tag = std::string(cls) + " " + (tight ? "tight" : "padded");
      check(r.host.layoutToken == (tight ? "tight" : "padded"), tag + ": the host's own startup line names this layout",
            r.host.layoutToken);
      check(r.host.statsLines >= static_cast<size_t>(seconds / 2), tag + ": the host wrote its stats lines",
            u(r.host.statsLines));
      check(r.delivered >= 2, tag + ": frames were received and delivered", u(r.delivered));
      check(r.malformed == 0, tag + ": nothing the host sent was malformed to the receiver", u(r.malformed));
      check(r.decodeErrors == 0 && r.decoded + 4 >= r.delivered, tag + ": every delivered AU decodes",
            "decoded " + u(r.decoded) + " of " + u(r.delivered) + " errors " + u(r.decodeErrors) + " " + r.decoder);
      if (lossPermille == 0) {
        // A seq gap on a lossless loopback is the host's: an IDR (encoder_gop) supersedes the
        // sender's two-deep backlog and those seqs are never sent. The receiver sees it as a gap
        // closed by a complete keyframe -- no request, no discontinuity (respond_to_sequence_gap).
        // Anything else -- a gap not closed by a key, a discontinuity, a request -- would be a loss.
        check(r.disc == 0 && r.keyReq == 0 && r.gaps == r.gapsBehindKey,
              tag + ": lossless -> in order, no discontinuity, no keyframe request, every seq gap is the"
                    " host's keyframe supersede (closed by a complete IDR)",
              "gaps " + u(r.gaps) + " behindKey " + u(r.gapsBehindKey) + " maxGap " + u(r.maxGap) + " disc " +
                  u(r.disc) + " keyReq " + u(r.keyReq));
        check(r.rxDropped == 0 && r.nacks == 0 && r.fec == 0, tag + ": lossless -> nothing dropped, no NACK, no repair");
        // What the host says it sent and what arrived here agree on the parity bytes (both count
        // datagram payload; the stats line is at most a second behind the receiver, so the
        // allowance is a second and a half of this run's parity rate, not a share of the total --
        // a share is too tight for a short run, where one second is a large share).
        const uint64_t hp = r.host.parity;
        const uint64_t rp = r.rxParity;
        const uint64_t allowance = (std::max(hp, rp) * 3) / (static_cast<uint64_t>(seconds) * 2) + 4096;
        check(hp > 0 && rp > 0 && (hp <= rp + allowance) && (rp <= hp + allowance),
              tag + ": the host's parity account and the received parity agree (within a second of stats)",
              "host " + u(hp) + " received " + u(rp));
      }
      if (tight && content != Content::Video) {
        check(r.singleChunkFrames > 0, tag + ": single-chunk frames occurred (the case this work is about)",
              u(r.singleChunkFrames));
      }
    }
    const RunResult& padded = res[0];
    const RunResult& tight = res[1];
    const std::string tag = std::string(cls);
    // Per frame the host sent (its cumulative udpTxFrames), because two live captures never send
    // the same number of frames in the same seconds.
    const double padParityPerFrame = padded.host.txFrames ? double(padded.host.parity) / padded.host.txFrames : 0;
    const double tightParityPerFrame = tight.host.txFrames ? double(tight.host.parity) / tight.host.txFrames : 0;
    const double padWirePerFrame = padded.host.txFrames ? double(padded.host.wireEst) / padded.host.txFrames : 0;
    const double tightWirePerFrame = tight.host.txFrames ? double(tight.host.wireEst) / tight.host.txFrames : 0;
    // The wire as a multiple of the payload it carried: on a still screen one 64 KB IDR is most
    // of the 12 seconds' bytes and the frame count is ten or eleven, so "per frame" swings 10%
    // on whether the last refresh frame made the last stats line. Numerator and denominator
    // here cover the same frames.
    const double padOverhead = padded.host.txBytes ? double(padded.host.wireEst) / padded.host.txBytes : 0;
    const double tightOverhead = tight.host.txBytes ? double(tight.host.wireEst) / tight.host.txBytes : 0;
    std::printf("COMPARE content=%s padded: frames=%llu payload=%llu parity=%llu wireEst=%llu parity/frame=%.0f wire/frame=%.0f"
                " wire/payload=%.3f | tight: frames=%llu payload=%llu parity=%llu wireEst=%llu parity/frame=%.0f wire/frame=%.0f"
                " wire/payload=%.3f | parity/frame ratio=%.2f wire/frame ratio=%.2f wire/payload ratio=%.3f\n",
                cls, static_cast<unsigned long long>(padded.host.txFrames), static_cast<unsigned long long>(padded.host.txBytes),
                static_cast<unsigned long long>(padded.host.parity), static_cast<unsigned long long>(padded.host.wireEst),
                padParityPerFrame, padWirePerFrame, padOverhead, static_cast<unsigned long long>(tight.host.txFrames),
                static_cast<unsigned long long>(tight.host.txBytes), static_cast<unsigned long long>(tight.host.parity),
                static_cast<unsigned long long>(tight.host.wireEst), tightParityPerFrame, tightWirePerFrame, tightOverhead,
                padParityPerFrame > 0 ? tightParityPerFrame / padParityPerFrame : 0,
                padWirePerFrame > 0 ? tightWirePerFrame / padWirePerFrame : 0,
                padOverhead > 0 ? tightOverhead / padOverhead : 0);
    if (lossPermille == 0 && padded.launched && tight.launched) {
      check(padded.host.txFrames > 0 && tight.host.txFrames > 0, tag + ": both hosts counted the frames they sent",
            u(padded.host.txFrames) + " / " + u(tight.host.txFrames));
      if (content != Content::Video) {
        check(tightParityPerFrame < padParityPerFrame, tag + ": tight sends fewer parity bytes per frame than padded",
              "tight " + std::to_string(tightParityPerFrame) + " padded " + std::to_string(padParityPerFrame));
        check(tightOverhead < padOverhead, tag + ": tight puts fewer wire bytes per payload byte than padded",
              "tight " + std::to_string(tightOverhead) + " padded " + std::to_string(padOverhead));
      } else {
        // Busy motion is multi-chunk frames: the layouts are the same bytes there, so the two live
        // captures differ only by what the encoder produced. Reported, not gated.
        std::printf("INFO video: parity/frame tight %.0f vs padded %.0f (multi-chunk frames are identical under both;"
                    " two live captures)\n", tightParityPerFrame, padParityPerFrame);
      }
    }
  }

  // Cap-ON evidence (bitrate-hard-cap r2): the same lossless scenario with the hard wire cap ON must,
  // after the progress-based tail fix, still make NO premature NACK -- the pinned cap-OFF runs above
  // keep this test's FEC-layout purpose, this run keeps the cap-ON proof. Only on a lossless path.
  if (!matrix && lossPermille == 0) {
    const uint16_t capPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    if (capPort != 0) {
      const std::wstring capDir = dir + L"capon_lossless\\";
      const RunResult cr = run_host(hostExe, capDir, Content::Video, /*tight=*/true, capPort, seconds, 0, lossSeed,
                                    /*wireCapOn=*/true);
      print_run(Content::Video, true, 0, lossSeed, cr);
      check(cr.delivered >= 2 && cr.decodeErrors == 0,
            "cap ON lossless: frames delivered and decoded", "delivered " + u(cr.delivered) + " decErr " + u(cr.decodeErrors));
      check(cr.nacks == 0,
            "cap ON lossless: NO premature NACK (the r2 progress-based tail holds under the cap on real hardware)",
            "rxNacks " + u(cr.nacks));
    }
  }

  MFShutdown();
  WSACleanup();
  if (keepDir) std::printf("kept %s\n", std::string(dir.begin(), dir.end()).c_str());
  else check(staging.Remove(), "the staging directory is removed", staging.why());
  if (gFails != 0) {
    std::printf("fec_single_chunk_host_e2e_test: FAIL (%d of %d checks)\n", gFails, gCheckCount);
    return 1;
  }
  std::printf("fec_single_chunk_host_e2e_test: PASS (%d checks)\n", gCheckCount);
  return 0;
}

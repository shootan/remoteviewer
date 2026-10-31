// motion-settle M4, the real chain (task viewer-stats-window r1): the PC viewer's stats window, end to end.
//
// remote60_viewer_stats_window_test drives StatsWindow -> ClientControlScheduler -> the host's ABR on a
// hand-made clock. This one runs the chain for real, in one process against an isolated host:
//
//   an isolated GNLinkStream capturing a window of this process (animated, so it sends a full cadence)
//     -> UDP video -> the product VideoReceiver::Run (assembler, FrameGate, MF decoder, FrameBuffer,
//        flush_stats_if_due -> publish_metrics -> ctx.metrics)
//     -> the product ControlClient::Run (capture_client_control_metrics_snapshot -> the product
//        ClientControlScheduler -> ControlClientMetrics over the tunnelled UDP control channel)
//     -> the host's ClientMetricsSnapshot -> RateControlState::DecideAbrProfile -> "[abr] profile=" lines.
//
// Nothing in that chain is re-implemented here. The stand-ins, stated because they bound the claim:
//   - the render thread: a loop that stamps the present anchor after each publish, as the ViewerRig of
//     viewer_udp_recovery_test does (no window, no D3D; the decoder runs without a shared D3D device);
//   - no viewer startup: the socket, Hello, control tunnel and scheduler interval are set up with the
//     same product calls viewer_startup.cpp makes (connect_media_socket / attach_control_tunnel /
//     connect_control), the control-resume and clipboard features are left off.
//
// Scenarios (--scenario, one host run each):
//   gap    healthy for --warm-sec, then the viewer pauses the stream with the product StreamStateControl
//          (what the initial picker does; the host stops sending) for --gap-sec, then resumes it. Judged:
//          after the resume the viewer reports again, the picture comes back at the cadence, no report
//          says ~1 fps, the reports do not flood, and the host's ABR does NOT step down.
//          At 592ed96 (the viewer before M4) this is the 2026-10-12 18:04 field case: it must FAIL.
//   loss   healthy, then 25% of the host->viewer video datagrams dropped for --impair-sec   -> must demote
//   thin   healthy, then 9 of every 10 frames dropped whole (3 of 30 arriving)               -> must demote
//   delay  healthy, then +250 ms on every host->viewer datagram                               -> must demote
//   stop   healthy, then every host->viewer video datagram dropped (control still flows), so the
//          viewer has no frame to report on and its reports stop (P7 stale feedback)          -> must demote
// The impairment scenarios put a relay of this process between the viewer and the host; gap does not.
//
//   remote60_viewer_stats_chain_e2e_test --scenario gap|loss|thin|delay|stop [--fps N] [--gap-sec N]
//       [--warm-sec N] [--after-sec N] [--impair-sec N] [--bitrate N] [--out DIR]
//   --out DIR keeps host.log and viewer.log there (the staging directory itself is removed).
//   (REMOTE60_ALLOW_HOST_E2E=1)

#include "e2e_isolation.hpp"
#include "control_resume_e2e_support.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

#include <mfapi.h>

#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "viewer_args.hpp"
#include "viewer_constants.hpp"
#include "viewer_control_client.hpp"
#include "viewer_decoder_state.hpp"
#include "viewer_frame_gate_state.hpp"
#include "viewer_state.hpp"
#include "viewer_udp_session.hpp"
#include "viewer_video_receiver.hpp"

#include <mmsystem.h>  // timeBeginPeriod; after the project headers, which bring winsock2/windows

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;
using remote60::native_poc::viewer::ViewerState;

// The macro window is a WebView2 window with its own loader; nothing here opens it (as in
// viewer_release_all_e2e_test).
namespace remote60::native_poc {
bool macro_window_visible() { return false; }
void macro_window_toggle(HINSTANCE, HWND, const MacroWindowHooks&) {}
}  // namespace remote60::native_poc

namespace {

std::wstring kTargetTitle;
uint32_t gFps = 30;  // --fps; above 30 the target repaints on a 1 ms timer period (8 ms timer), as abr r5 does

// ------------------------------------------------------------------------------ what the host captures
// On screen (an off-screen window is never composed and the host then sends only synthetic refreshes),
// alpha 1/255, click-through, never activated, repainted every 16 ms -- the CadenceTarget of
// abr_client_evidence_e2e_test, default pattern.
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
    wc.lpszClassName = L"Remote60StatsChainTarget";
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                            wc.lpszClassName, kTargetTitle.c_str(), WS_POPUP, 0, 0, 480, 270, nullptr,
                            nullptr, wc.hInstance, nullptr);
    if (hwnd_) {
      SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
      SetLayeredWindowAttributes(hwnd_, 0, 1, LWA_ALPHA);
      ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
      // The default 15.6 ms timer period turns a 16 ms timer into ~32 Hz: enough for 30 fps, not for 60.
      if (gFps > 30) timeBeginPeriod(1);
      SetTimer(hwnd_, 1, gFps > 30 ? 8 : 16, nullptr);
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

// ------------------------------------------------------------------------------ the viewer's stdout
// Every product thread logs to std::cout. Lines are assembled per thread and written whole to a file
// (the console would get a thousand lines a second); the stats lines are kept for the verdicts.
struct StatsLine {
  uint64_t qpcUs = 0;      // eventQpcUs, stamped by log_client_line
  uint64_t recvFrames = 0;
  uint64_t decodedFrames = 0;
  uint64_t windowUs = 0;   // statWindowUs (M4); 0 when the field is absent (592ed96: always 1 s)
  bool published = true;   // statPublished (M4); absent = every line was published (592ed96)
};

class TeeBuf : public std::streambuf {
 public:
  explicit TeeBuf(const std::wstring& path) : file_(path, std::ios::binary) {}
  std::vector<StatsLine> Stats() {
    std::lock_guard<std::mutex> lk(mu_);
    return stats_;
  }
  void Close() {
    std::lock_guard<std::mutex> lk(mu_);
    file_.close();
  }
  uint64_t Lines() {
    std::lock_guard<std::mutex> lk(mu_);
    return lines_;
  }

 protected:
  int overflow(int ch) override {
    if (ch == EOF) return 0;
    Line().push_back(static_cast<char>(ch));
    if (ch == '\n') Emit();
    return ch;
  }
  std::streamsize xsputn(const char* s, std::streamsize n) override {
    for (std::streamsize i = 0; i < n; ++i) overflow(static_cast<unsigned char>(s[i]));
    return n;
  }

 private:
  static std::string& Line() {
    thread_local std::string line;
    return line;
  }
  static uint64_t Field(const std::string& line, const char* key, bool* found = nullptr) {
    const std::string needle = std::string(" ") + key + "=";
    const size_t at = line.find(needle);
    if (found) *found = at != std::string::npos;
    if (at == std::string::npos) return 0;
    return std::strtoull(line.c_str() + at + needle.size(), nullptr, 10);
  }
  void Emit() {
    std::string& line = Line();
    std::lock_guard<std::mutex> lk(mu_);
    ++lines_;
    if (line.find("[native-video-client] recvFrames=") != std::string::npos) {
      StatsLine s;
      s.qpcUs = Field(line, "eventQpcUs");
      s.recvFrames = std::strtoull(line.c_str() + line.find("recvFrames=") + 11, nullptr, 10);
      s.decodedFrames = Field(line, "decodedFrames");
      s.windowUs = Field(line, "statWindowUs");
      bool hasPublished = false;
      const uint64_t published = Field(line, "statPublished", &hasPublished);
      s.published = !hasPublished || published != 0;
      stats_.push_back(s);
    }
    if (file_.is_open()) file_ << line;
    line.clear();
  }
  std::mutex mu_;
  std::ofstream file_;
  std::vector<StatsLine> stats_;
  uint64_t lines_ = 0;
};

// ------------------------------------------------------------------------------ the impairment relay
// One socket each side; the host answers whatever address it heard from, so it binds the session to
// this relay. Impairments switch at run time and touch host->viewer traffic only.
enum class Impair : int { None = 0, Loss, Thin, Delay, Stop };

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
    back_ = Bind(&backPort);
    if (front_ == INVALID_SOCKET || back_ == INVALID_SOCKET) return false;
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
    if (back_ != INVALID_SOCKET) closesocket(back_);
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
  void Up() {
    uint8_t buf[2048];
    while (running_) {
      sockaddr_in from{};
      int fl = sizeof(from);
      const int n = recvfrom(front_, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
      if (n <= 0) continue;
      {
        std::lock_guard<std::mutex> lk(viewerMu_);
        viewer_ = from;
        haveViewer_ = true;
      }
      sendto(back_, reinterpret_cast<const char*>(buf), n, 0, reinterpret_cast<const sockaddr*>(&host_), sizeof(host_));
    }
  }
  void Down() {
    std::mt19937 rng(20261013u);
    uint8_t buf[2048];
    while (running_) {
      const int n = recv(back_, reinterpret_cast<char*>(buf), sizeof(buf), 0);
      if (n <= 0) continue;
      const Impair m = static_cast<Impair>(mode.load());
      uint32_t seq = 0;
      const bool video = IsVideo(buf, n, &seq);
      bool drop = false;
      if (video && m == Impair::Loss) drop = (rng() % 1000) < 250;
      if (video && m == Impair::Thin) drop = (seq % 10) != 0;
      if (video && m == Impair::Stop) drop = true;
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
    }
  }
  sockaddr_in host_{};
  SOCKET front_ = INVALID_SOCKET, back_ = INVALID_SOCKET;
  std::atomic<bool> running_{false};
  std::mutex viewerMu_;
  sockaddr_in viewer_{};
  bool haveViewer_ = false;
  std::mutex qMu_;
  std::deque<std::pair<uint64_t, std::vector<uint8_t>>> q_;
  std::vector<std::thread> threads_;
};

// ------------------------------------------------------------------------------ the host's log
// "MM-DD HH:MM:SS.mmm ..." -> milliseconds since midnight; -1 if the line has no stamp.
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
bool is_down_reason(const std::string& reason) {
  return reason.find("high_to_") == 0 || reason.find("mid_to_low") == 0 || reason == "client_emergency";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string scenario = "gap";
  uint32_t fps = 30;
  int warmSec = 15, gapSec = 120, afterSec = 25, impairSec = 20;
  uint32_t bitrate = 6000000;
  std::wstring outDir;
  for (int i = 1; i < argc; ++i) {
    const std::wstring a = argv[i];
    const bool more = i + 1 < argc;
    if (a == L"--scenario" && more) {
      const std::wstring v = argv[++i];
      scenario.assign(v.begin(), v.end());
    } else if (a == L"--fps" && more) {
      fps = static_cast<uint32_t>(_wtoi(argv[++i]));
    } else if (a == L"--gap-sec" && more) {
      gapSec = _wtoi(argv[++i]);
    } else if (a == L"--warm-sec" && more) {
      warmSec = _wtoi(argv[++i]);
    } else if (a == L"--after-sec" && more) {
      afterSec = _wtoi(argv[++i]);
    } else if (a == L"--impair-sec" && more) {
      impairSec = _wtoi(argv[++i]);
    } else if (a == L"--bitrate" && more) {
      bitrate = static_cast<uint32_t>(_wtoi(argv[++i]));
    } else if (a == L"--out" && more) {
      outDir = argv[++i];
      if (!outDir.empty() && outDir.back() != L'\\' && outDir.back() != L'/') outDir += L"\\";
    }
  }
  Impair impair = Impair::None;
  if (scenario == "loss") impair = Impair::Loss;
  else if (scenario == "thin") impair = Impair::Thin;
  else if (scenario == "delay") impair = Impair::Delay;
  else if (scenario == "stop") impair = Impair::Stop;
  else if (scenario != "gap") {
    std::printf("unknown --scenario %s\n", scenario.c_str());
    return 2;
  }
  gFps = fps;
  const bool gapRun = impair == Impair::None;
  std::printf("viewer_stats_chain_e2e_test scenario=%s fps=%u warmSec=%d %s=%d afterSec=%d bitrate=%u\n",
              scenario.c_str(), fps, warmSec, gapRun ? "gapSec" : "impairSec", gapRun ? gapSec : impairSec,
              afterSec, bitrate);

  if (!host_e2e_allowed()) {
    std::printf("SKIP  viewer_stats_chain_e2e_test (starts a listening host)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return kE2eSkippedExit;
  }
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
  if (FAILED(MFStartup(MF_VERSION))) {
    std::printf("FAIL  MFStartup\n");
    return 1;
  }
  const uint16_t hostPort = e2e_pick_free_udp_port();
  kTargetTitle = e2e_unique_window_title(L"remote60 stats chain target");

  StagingDir staging;
  if (!staging.Create(L"m4_chain")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  const std::wstring dir = staging.path();
  const std::wstring myDir = directory_of(self_path());
  const bool staged = CopyFileW((myDir + L"GNLinkStream.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) != 0;
  check("the host could be staged", staged);

  CadenceTarget target;
  check("an on-screen, invisible, click-through window is up for the host to capture", target.Start());

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  // The host runs a little past the viewer, so it exits by itself and its stdout file is complete.
  const int runSec = warmSec + (gapRun ? gapSec : impairSec) + afterSec;
  const int hostSec = runSec + 25;
  const std::wstring hostLogPath = dir + L"host.log";
  HANDLE hostLog = INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION hostPi{};
  bool launched = false;
  if (staged) {
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_STATS_PRINT_EVERY_SEC", L"1");  // one ABR reading per second
    SetEnvironmentVariableW(L"REMOTE60_CLIPBOARD_SYNC", L"0");                // the user's clipboard stays out
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
  if (launched && !gapRun) check("the impairment relay is up (host->viewer only)", relay.Start(hostPort));

  // ------------------------------------------------------------------ the viewer, product parts
  const std::wstring viewerLogPath = dir + L"viewer.log";
  TeeBuf tee(viewerLogPath);
  std::streambuf* const consoleBuf = std::cout.rdbuf();

  static ViewerState ctx;
  static remote60::native_poc::viewer::Args args;
  static remote60::native_poc::viewer::DecoderState dec;
  static remote60::native_poc::viewer::FrameGateState gate;
  std::optional<remote60::native_poc::viewer::VideoReceiver> receiver;
  std::optional<remote60::native_poc::viewer::ControlClient> control;
  std::thread recvThread, controlThread, presentThread;
  std::atomic<bool> stopPresent{false};
  std::mutex framesMu;
  std::vector<uint64_t> realFrameUs;  // QPC of each real (non-synthetic) frame the renderer picked up
  bool connected = false;

  if (launched) {
    // viewer_args / init_viewer_tuning defaults, as ViewerRig sets them.
    using namespace remote60::native_poc::viewer;
    args.codec = "h264";
    args.transport = "udp";
    args.fpsHint = fps;
    args.udpMtu = 1200;
    args.inputLogEvery = 0;
    dec.useH264 = true;
    dec.transport = VideoTransport::Udp;
    gate.catchupReenterMinIntervalUs = kCatchupReenterMinIntervalUsDefault;
    gate.staleCaptureDropUs = kStaleCaptureDropUs;
    gate.staleReferenceRecoveryMinIntervalUs = kStaleRecoveryMinIntervalUsDefault;
    gate.congestionRecoverMinUs = kCongestionRecoverMinUsDefault;
    gate.congestionRecoveryTimeoutUs = kCongestionRecoveryTimeoutUsDefault;
    gate.decodeQueueLagDropUs = kDecodeQueueLagDropUs;
    gate.catchupLagDropUs = kCatchupLagDropUs;
    gate.denseArrivalMaxGapUs = kDenseArrivalMaxGapUsDefault;
    gate.lagTriggerStreakMin = kLagTriggerStreakMinDefault;
    gate.recoveryRetryIntervalUs = kKeyRecoveryRetryUsDefault;
    gate.recoveryRetryMaxIntervalUs = kKeyRecoveryRetryMaxUsDefault;
    gate.recoveryRetryDeferMaxUs = kKeyRecoveryDeferMaxUsDefault;
    gate.waitForKeyFrame = true;
    ctx.picker.visible.store(false);
    ctx.control.keyframeRequests.Reset();
    ctx.session.running.store(true);
    ctx.session.controlRequired = true;

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(gapRun ? hostPort : relay.frontPort);
    connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    const int recvBuf = 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
    UdpHelloOptions hello = viewer_udp_hello_options(std::string(), /*requestNack=*/true);
    hello.budgetMs = 20000;
    uint32_t ackFeatures = 0;
    std::string helloError;
    const bool helloOk = udp_hello_handshake(s, hello, nullptr, &helloError, &ackFeatures);
    check("the product Hello is answered", helloOk, helloError);
    (void)viewer_arm_udp_recv_timeout(s, 25);
    ctx.session.sock = s;
    viewer_apply_udp_hello_ack(ackFeatures, true, ctx.session);
    // attach_control_tunnel_and_log: control rides the media socket.
    ctx.control.udpControl.Configure(
        [s](const void* data, size_t len) { return send(s, static_cast<const char*>(data), static_cast<int>(len), 0) > 0; },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, args.udpMtu);
    ctx.control.overUdp.store(true, std::memory_order_release);
    // The user's clipboard stays out of it: the viewer's sync is switched off before the worker starts
    // (the host's is off too, REMOTE60_CLIPBOARD_SYNC=0, so it never advertises the capability).
    ctx.control.clipboard.enabled.store(false);
    check("the viewer's clipboard sync is off (the user's clipboard is never touched)",
          !ctx.control.clipboard.enabled.load() && !ctx.control.clipboard.hostSupports.load());

    std::cout.rdbuf(&tee);  // from here every product line goes to viewer.log
    connected = helloOk;
    if (connected) {
      // start_receiver
      remote60::native_poc::viewer::VideoReceiver::NackOptions nack;
      nack.enabled = ctx.session.hostSupportsNack;
      nack.holdUs = 120000;
      receiver.emplace(ctx, args, dec, gate, qpc_now_us(), 0, 0, nack);
      recvThread = std::thread([&] { receiver->Run(); });
      // the render thread's part: stamp the present anchor after each publish (real frames only)
      presentThread = std::thread([&] {
        uint64_t seen = 0;
        while (!stopPresent.load()) {
          {
            std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
            auto& f = ctx.frameBuf.frame;
            if (f.version != seen) {
              seen = f.version;
              ctx.frameBuf.lastPresentedVersion.store(f.version, std::memory_order_relaxed);
              if (!f.synthetic) {
                ctx.frameBuf.lastPresentedCaptureUs.store(f.captureUs, std::memory_order_relaxed);
                std::lock_guard<std::mutex> fl(framesMu);
                realFrameUs.push_back(qpc_now_us());
              }
            }
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
      });
      // connect_control
      control.emplace(ctx, args, /*startInPicker=*/false);
      ctx.control.scheduler.Reset(kClientControlIntervalMsDefault, qpc_now_us());
      ctx.control.connected.store(true, std::memory_order_relaxed);
      controlThread = std::thread([&] { control->Run(); });
    }
  }

  const auto real_frames_between = [&](uint64_t fromUs, uint64_t toUs) {
    std::lock_guard<std::mutex> lk(framesMu);
    return static_cast<uint64_t>(std::count_if(realFrameUs.begin(), realFrameUs.end(),
                                               [&](uint64_t t) { return t >= fromUs && t < toUs; }));
  };
  auto say = [&](const std::string& s) {
    std::cout.rdbuf(consoleBuf);
    std::cout << s << "\n";
    std::cout.rdbuf(&tee);
  };

  uint64_t firstFrameUs = 0, eventUs = 0, eventEndUs = 0, endUs = 0;
  int64_t eventWallMs = -1, eventEndWallMs = -1, endWallMs = -1;
  if (connected) {
    const bool picture = wait_until([&] { return real_frames_between(0, UINT64_MAX) > 0; }, 20000);
    firstFrameUs = qpc_now_us();
    say(std::string(picture ? "PASS  " : "FAIL  ") + "the first real frame reached the FrameBuffer");
    if (!picture) ++gFailures;
    ++gChecks;
    std::this_thread::sleep_for(std::chrono::seconds(warmSec));
    eventUs = qpc_now_us();
    eventWallMs = local_now_ms();
    if (gapRun) {
      say("--- stream paused (product StreamStateControl, as the initial picker does) for " + std::to_string(gapSec) + " s");
      ctx.control.streamState.Request(false);
      std::this_thread::sleep_for(std::chrono::seconds(gapSec));
      eventEndUs = qpc_now_us();
      eventEndWallMs = local_now_ms();
      ctx.control.streamState.Request(true);
      say("--- stream resumed");
    } else {
      say("--- impairment " + scenario + " on for " + std::to_string(impairSec) + " s");
      relay.mode.store(static_cast<int>(impair));
      std::this_thread::sleep_for(std::chrono::seconds(impairSec));
      eventEndUs = qpc_now_us();
      eventEndWallMs = local_now_ms();
      relay.mode.store(static_cast<int>(Impair::None));
      say("--- impairment off");
    }
    std::this_thread::sleep_for(std::chrono::seconds(afterSec));
    endUs = qpc_now_us();
    endWallMs = local_now_ms();
  }

  // ------------------------------------------------------------------ teardown
  if (connected) {
    ctx.session.running.store(false);
    if (controlThread.joinable()) controlThread.join();
    if (recvThread.joinable()) recvThread.join();
    stopPresent.store(true);
    if (presentThread.joinable()) presentThread.join();
  }
  std::cout.rdbuf(consoleBuf);
  tee.Close();
  if (ctx.session.sock != INVALID_SOCKET) closesocket(ctx.session.sock);
  dec.decoder.shutdown();
  if (!gapRun) relay.Stop();
  if (hostPi.hProcess) {
    // It exits by itself at --seconds; terminating it earlier could cut its buffered stdout.
    const DWORD waited = WaitForSingleObject(hostPi.hProcess, 90000);
    if (waited != WAIT_OBJECT_0) {
      std::printf("NOTE  host did not exit by itself; terminated\n");
      TerminateProcess(hostPi.hProcess, 0);
      WaitForSingleObject(hostPi.hProcess, 5000);
    }
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  CloseHandle(job);
  const HWND targetHwnd = target.hwnd();
  if (launched) {
    std::string captureLine;
    const bool own = e2e_host_captured_window(hostLogPath, targetHwnd, &captureLine);
    check("the host captured the window this test paints (its hwnd, this pid)", own, captureLine);
  }
  target.Stop();
  if (connected) {
    check("...and stayed off: the host never advertised clipboard sync during the run",
          !ctx.control.clipboard.enabled.load() && !ctx.control.clipboard.hostSupports.load());
  }

  // ------------------------------------------------------------------ the host's account
  struct AbrLine {
    int64_t ms;
    std::string reason, profile, raw;
  };
  struct HostSec {
    int64_t ms;
    uint64_t sentFrames;
    std::string profile;
  };
  std::vector<AbrLine> abr;
  std::vector<HostSec> hostSecs;
  {
    std::ifstream in(hostLogPath);
    std::string line;
    while (std::getline(in, line)) {
      if (line.find("[abr] profile=") != std::string::npos) {
        abr.push_back({host_line_ms(line), field_of(line, "reason"), field_of(line, "profile"), line});
      } else if (line.find("[native-video-host] encodedFrames=") != std::string::npos &&
                 line.find(" abrProfile=") != std::string::npos) {
        hostSecs.push_back({host_line_ms(line), std::strtoull(field_of(line, "sentFrames").c_str(), nullptr, 10),
                            field_of(line, "abrProfile")});
      }
    }
  }
  const std::vector<StatsLine> stats = tee.Stats();
  std::printf("\n--- host [abr] lines (%zu) ---\n", abr.size());
  for (const auto& a : abr) std::printf("%s\n", a.raw.substr(0, 420).c_str());

  // The viewer's account, per second relative to the event (the resume for gap, the impairment start
  // otherwise): stats lines, published reports, and the lowest published decoded rate.
  const uint64_t anchorUs = gapRun ? eventEndUs : eventUs;
  std::printf("\n--- viewer stats lines around the %s (t = s from it) ---\n", gapRun ? "resume" : "impairment");
  for (int sec = -3; sec <= 12 && anchorUs; ++sec) {
    const uint64_t from = anchorUs + static_cast<int64_t>(sec) * 1000000ll;
    const uint64_t to = from + 1000000ull;
    uint32_t lines = 0, pub = 0;
    double minFps = 1e9;
    std::string recv;
    for (const auto& s : stats) {
      if (s.qpcUs < from || s.qpcUs >= to) continue;
      ++lines;
      if (s.published) {
        ++pub;
        const double rate = s.windowUs ? s.decodedFrames * 1e6 / static_cast<double>(s.windowUs)
                                       : static_cast<double>(s.decodedFrames);
        minFps = std::min(minFps, rate);
      }
      if (recv.size() < 60) recv += std::to_string(s.recvFrames) + (s.published ? "" : "u") + ",";
    }
    std::printf("t=%+3d  lines=%2u published=%2u minPublishedDecodedFps=%s realFramesShown=%llu  recvFrames=[%s]\n", sec,
                lines, pub, pub ? std::to_string(static_cast<int>(minFps)).c_str() : "-",
                static_cast<unsigned long long>(real_frames_between(from, to)), recv.c_str());
  }

  if (connected) {
    // The host was at high before the event, and judged real seconds (>= fps/2 sent) afterwards.
    std::string profileBefore;
    for (const auto& h : hostSecs) if (h.ms >= 0 && h.ms < eventWallMs) profileBefore = h.profile;
    check("the host's ABR was at high before the " + std::string(gapRun ? "gap" : "impairment"),
          profileBefore == "high", "abrProfile=" + profileBefore);
    const int64_t judgeFrom = gapRun ? eventEndWallMs : eventWallMs;
    const int64_t judgeTo = gapRun ? endWallMs : eventEndWallMs + 3000;
    uint32_t judged = 0;
    for (const auto& h : hostSecs)
      if (h.ms >= judgeFrom && h.ms <= judgeTo && h.sentFrames >= std::max<uint32_t>(2, fps / 2)) ++judged;
    std::vector<AbrLine> downs;
    for (const auto& a : abr)
      if (a.ms >= judgeFrom && a.ms <= judgeTo && is_down_reason(a.reason)) downs.push_back(a);
    const std::string downList = [&] {
      std::string s;
      for (const auto& d : downs) s += d.reason + "->" + d.profile + " ";
      return s.empty() ? std::string("none") : s;
    }();

    if (gapRun) {
      const bool cadence = judged >= static_cast<uint32_t>(afterSec / 2);
      check("the host sent a real cadence after the resume (ABR-judged seconds)", cadence,
            std::to_string(judged) + " of " + std::to_string(afterSec) + " s");
      // Reports: they come back, they do not flood, none says ~1 fps.
      uint32_t pubAfter = 0, pubFirst5 = 0, linesFirst5 = 0, fake = 0;
      uint64_t firstPubUs = 0;
      const double collapseFps = fps * 0.12;  // the host's P7 collapse floor (host_abr.hpp, 12%)
      for (const auto& s : stats) {
        if (s.qpcUs < eventEndUs) continue;
        if (s.qpcUs < eventEndUs + 5000000ull) ++linesFirst5;
        if (!s.published) continue;
        ++pubAfter;
        if (!firstPubUs) firstPubUs = s.qpcUs;
        if (s.qpcUs < eventEndUs + 5000000ull) ++pubFirst5;
        const double rate = s.windowUs ? s.decodedFrames * 1e6 / static_cast<double>(s.windowUs)
                                       : static_cast<double>(s.decodedFrames);
        if (rate < collapseFps) ++fake;
      }
      check("the viewer reports again after the resume", pubAfter >= static_cast<uint32_t>(afterSec / 2),
            std::to_string(pubAfter) + " published reports in " + std::to_string(afterSec) + " s, first " +
                (firstPubUs ? std::to_string((firstPubUs - eventEndUs) / 1000) + " ms after the resume" : "never"));
      check("NO REPORT SAYS THE STREAM COLLAPSED (decoded rate under the 12% floor) after the resume", fake == 0,
            std::to_string(fake) + " published reports under " + std::to_string(collapseFps) + " fps");
      check("the reports do not flood in the first 5 s after the resume (<= 8 published)", pubFirst5 <= 8,
            std::to_string(pubFirst5) + " published / " + std::to_string(linesFirst5) + " stats lines");
      // The picture: real frames reaching the FrameBuffer from 3 s after the resume, against what the host
      // really sent over the same seconds (its per-second sentFrames) -- the source window sets the cadence.
      const uint64_t shownFrom = eventEndUs + 3000000ull;
      const uint64_t shown = real_frames_between(shownFrom, endUs);
      const double shownFps = shown * 1e6 / static_cast<double>(endUs - shownFrom);
      uint64_t sentSum = 0, sentN = 0;
      for (const auto& h : hostSecs)
        if (h.ms >= eventEndWallMs + 4000 && h.ms <= endWallMs) { sentSum += h.sentFrames; ++sentN; }
      const double sentFps = sentN ? static_cast<double>(sentSum) / sentN : 0.0;
      check("the picture comes back at the cadence (real frames shown >= 85% of what the host sent, and >= fps/2)",
            sentN > 0 && shownFps >= sentFps * 0.85 && shownFps >= fps / 2.0,
            std::to_string(shownFps) + " fps shown, host sent " + std::to_string(sentFps) + " fps (" +
                std::to_string(sentN) + " host seconds), from 3 s after the resume");
      if (cadence) {
        check("THE HOST'S ABR DOES NOT STEP DOWN AFTER THE RESUME", downs.empty(), downList);
        std::string profileEnd;
        for (const auto& h : hostSecs) if (h.ms >= 0 && h.ms <= endWallMs) profileEnd = h.profile;
        check("...and is still at high at the end", profileEnd == "high", "abrProfile=" + profileEnd);
      } else {
        skip("the host's ABR after the resume", "the host did not send a real cadence; ABR holds on sparse seconds");
      }
    } else {
      if (judged < 3 && impair != Impair::Stop) {
        skip("ABR under " + scenario, "only " + std::to_string(judged) + " ABR-judged seconds in the impairment");
      }
      check("the impairment did something (host->viewer datagrams dropped or delayed)",
            impair == Impair::Delay || relay.dropped.load() > 0, std::to_string(relay.dropped.load()) + " dropped");
      check("A REAL " + scenario + " STILL STEPS THE HOST'S ABR DOWN (within the impairment + 3 s)", !downs.empty(),
            downList + " (" + std::to_string(judged) + " judged seconds)");
    }
  }

  if (!outDir.empty()) {
    CreateDirectoryW(outDir.c_str(), nullptr);
    const std::wstring tag = std::wstring(scenario.begin(), scenario.end()) + L"_";
    CopyFileW(hostLogPath.c_str(), (outDir + tag + L"host.log").c_str(), FALSE);
    CopyFileW(viewerLogPath.c_str(), (outDir + tag + L"viewer.log").c_str(), FALSE);
    std::printf("logs kept in %ls\n", outDir.c_str());
  }
  (void)firstFrameUs;
  const bool removed = staging.Remove();
  if (!removed) std::printf("WARN  %s\n", staging.why().c_str());
  std::printf("\n%d checks, %d failed, %d not judged\n", gChecks, gFailures, gSkips);
  if (gFailures == 0 && gSkips == 0) {
    std::printf("viewer_stats_chain_e2e_test(%s): PASS\nRESULT: PASS\n", scenario.c_str());
    return 0;
  }
  std::printf("viewer_stats_chain_e2e_test(%s): FAIL\nRESULT: FAIL\n", scenario.c_str());
  return 1;
}

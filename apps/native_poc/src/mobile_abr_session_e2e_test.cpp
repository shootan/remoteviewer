// mobile-abr r1: the phone's reports through the real host ABR -- the shared client session the APK runs
// (ClientSessionController, native_video_client_session.cpp) with a sink that behaves like the APK's
// decoder sink, against an isolated GNLinkStream, judged from the host's own "[abr]" and stats lines.
//
// The field case (2026-10-14 14:25-14:28, phone SM-S948N -> company host 0.2.157, 3 Mbps / 30 fps): six
// demotions, every one "evidence=stale_active|decode_unreported metricsFresh=0 clientDecodedFps=0"
// with the host sending a full cadence -- the phone's reports came ~0.6 a second with gaps of 5-10 s.
// The session reported only on seconds its sink had presentation stats for (DrainPresentationStats), and
// never with decode-side fields, so the host's P7 stale-feedback rule read the gaps as a relay collapse.
//
// The sink here: the APK sink's codec replaced by a counter. It "decodes" every frame it is handed, and
// it has presentation stats only when it has presented two frames since the last drain -- the APK's
// rule (android_video_decoder.cpp DrainPresentationStats: fewer than 2 gaps -> false). --present-duty
// ON:OFF makes the display side come and go on a cycle of seconds (the field's sparse reports: ~0.6 a
// second, gaps of 5.5-9.7 s); the default 6:8 leaves ~8 s without a report. P7 needs the 3 s freshness
// gate + 2 stale seconds + 2 severe seconds, so a gap under ~7 s cannot demote on its own.
//
// Scenarios (--scenario, one host run each):
//   sparse     healthy network, sparse presentation stats. Must NOT demote: no down step in the run.
//   uplink     healthy, then every viewer->host datagram dropped for --impair-sec (the reports really
//              stop, history #341's case)                                                -> must demote
//   videostop  healthy, then every host->viewer video datagram dropped (control flows)  -> must demote
//   thin       healthy, then 9 of every 10 frames dropped whole (3 of 30 arriving)       -> must demote
//   loss       healthy, then 25% of the host->viewer video datagrams dropped            -> reported
//   delay      healthy, then +250 ms on every host->viewer datagram                     -> reported
// The impairment scenarios run with presentation stats every second (--present-duty 1:0), so what they
// measure is the impairment, not the sparse display. loss and delay are recorded, not judged: what the
// phone can see of them is the question this run answers.
//
//   remote60_mobile_abr_session_e2e_test --scenario S [--fps N] [--bitrate N] [--warm-sec N]
//       [--run-sec N] [--impair-sec N] [--after-sec N] [--present-duty ON:OFF] [--out DIR] [--watchdog-sec N]
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

// Every line of this harness goes to stdout with one fwrite: the product's threads write to stderr /
// stdout too, and a line built from several insertions can be split by them.
void say(const std::string& s) {
  const std::string line = s + "\n";
  std::fwrite(line.data(), 1, line.size(), stdout);
  std::fflush(stdout);
}

// ------------------------------------------------------------------------------ what the host captures
// On screen, alpha 1/255, click-through, never activated, repainted every 16 ms (abr_client_evidence's
// CadenceTarget, default pattern).
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
    wc.lpszClassName = L"Remote60MobileAbrTarget";
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                            wc.lpszClassName, kTargetTitle.c_str(), WS_POPUP, 0, 0, 480, 270, nullptr,
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
// The APK decoder sink (android_video_decoder.cpp) without AMediaCodec: every frame handed over is
// "decoded" and "presented". Presentation stats follow the APK's rule -- at least 2 present gaps since
// the last drain -- and, with a duty cycle, exist only during the ON seconds of each cycle.
class PhoneSink : public ClientEncodedFrameSink {
 public:
  PhoneSink(uint32_t onSec, uint32_t offSec) : onSec_(onSec), offSec_(offSec), startUs_(qpc_now_us()) {}

  void OnEncodedH264Frame(UdpH264AssembledFrame&& frame) override {
    std::lock_guard<std::mutex> lk(mu_);
    ++frames_;
    width_ = frame.header.width;
    height_ = frame.header.height;
    ++decodedSinceDrain_;
    const uint64_t now = qpc_now_us();
    if (DisplayOn(now)) {
      if (lastPresentUs_ != 0 && now > lastPresentUs_) gaps_.push_back(static_cast<uint32_t>(now - lastPresentUs_));
      if (gaps_.empty() && windowStartUs_ == 0) windowStartUs_ = now;
      lastPresentUs_ = now;
    } else {
      lastPresentUs_ = 0;  // nothing reached the screen: the next present starts a new run of gaps
    }
  }
  void OnVideoStreamReset() override {}

  bool DrainPresentationStats(ClientPresentationStats* out) override {
    if (!out) return false;
    std::lock_guard<std::mutex> lk(mu_);
    if (gaps_.size() < 2) return false;  // android_video_decoder.cpp: the same rule
    std::vector<uint32_t> g = gaps_;
    gaps_.clear();
    std::sort(g.begin(), g.end());
    out->targetIntervalUs = 33333;
    out->sampleCount = static_cast<uint32_t>(g.size());
    out->gapP50Us = g[g.size() / 2];
    out->gapP95Us = g[std::min(g.size() - 1, g.size() * 95 / 100)];
    out->gapMaxUs = g.back();
    out->displayedCount = static_cast<uint32_t>(g.size());
    uint64_t span = 0;
    for (uint32_t x : g) span += x;
    out->fpsX100 = span ? static_cast<uint32_t>(g.size() * 100000000ull / span) : 0;
    ++drains_;
    return true;
  }

  uint64_t frames() {
    std::lock_guard<std::mutex> lk(mu_);
    return frames_;
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
  const uint64_t startUs_;
  uint64_t frames_ = 0, drains_ = 0, decodedSinceDrain_ = 0;
  uint32_t width_ = 0, height_ = 0;
  uint64_t lastPresentUs_ = 0, windowStartUs_ = 0;
  std::vector<uint32_t> gaps_;
};

// ------------------------------------------------------------------------------ the impairment relay
// One socket each side; the host answers whatever address it heard from, so it binds the session to
// this relay. Impairments switch at run time.
enum class Impair : int { None = 0, Uplink, VideoStop, Thin, Loss, Delay };

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
      if (static_cast<Impair>(mode.load()) == Impair::Uplink) {
        ++dropped;
        continue;
      }
      sendto(back_, reinterpret_cast<const char*>(buf), n, 0, reinterpret_cast<const sockaddr*>(&host_), sizeof(host_));
    }
  }
  void Down() {
    std::mt19937 rng(20261014u);
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
      if (video && m == Impair::VideoStop) drop = true;
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
  std::string scenario = "sparse";
  uint32_t fps = 30, bitrate = 3000000;
  int warmSec = 12, runSec = 45, impairSec = 20, afterSec = 8;
  uint32_t dutyOn = 6, dutyOff = 8;
  bool dutyGiven = false;
  std::wstring outDir;
  int watchdogOverrideSec = 0;
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
    } else if (a == L"--warm-sec" && more) {
      warmSec = _wtoi(argv[++i]);
    } else if (a == L"--run-sec" && more) {
      runSec = _wtoi(argv[++i]);
    } else if (a == L"--impair-sec" && more) {
      impairSec = _wtoi(argv[++i]);
    } else if (a == L"--after-sec" && more) {
      afterSec = _wtoi(argv[++i]);
    } else if (a == L"--present-duty" && more) {
      dutyGiven = std::swscanf(argv[++i], L"%u:%u", &dutyOn, &dutyOff) == 2;
    } else if (a == L"--out" && more) {
      outDir = argv[++i];
      if (!outDir.empty() && outDir.back() != L'\\' && outDir.back() != L'/') outDir += L"\\";
    } else if (a == L"--watchdog-sec" && more) {
      watchdogOverrideSec = _wtoi(argv[++i]);
    }
  }
  Impair impair = Impair::None;
  if (scenario == "uplink") impair = Impair::Uplink;
  else if (scenario == "videostop") impair = Impair::VideoStop;
  else if (scenario == "thin") impair = Impair::Thin;
  else if (scenario == "loss") impair = Impair::Loss;
  else if (scenario == "delay") impair = Impair::Delay;
  else if (scenario != "sparse") {
    say("unknown --scenario " + scenario);
    return 2;
  }
  const bool sparseRun = impair == Impair::None;
  if (!sparseRun && !dutyGiven) {
    dutyOn = 1;
    dutyOff = 0;
  }
  const bool judgedImpair = impair == Impair::Uplink || impair == Impair::VideoStop || impair == Impair::Thin;
  const int totalSec = sparseRun ? warmSec + runSec : warmSec + impairSec + afterSec;
  say("mobile_abr_session_e2e_test scenario=" + scenario + " fps=" + std::to_string(fps) + " bitrate=" +
      std::to_string(bitrate) + " warmSec=" + std::to_string(warmSec) + " " +
      (sparseRun ? "runSec=" + std::to_string(runSec) : "impairSec=" + std::to_string(impairSec)) +
      " presentDuty=" + std::to_string(dutyOn) + ":" + std::to_string(dutyOff));

  if (!host_e2e_allowed()) {
    say("SKIP  mobile_abr_session_e2e_test (starts a listening host)");
    say("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED");
    return kE2eSkippedExit;
  }
  const int hostSec = totalSec + 40;
  // Bounded whatever happens: straight to the stdout handle, no CRT lock; the job then kills the host.
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
  if (launched && !sparseRun) check("the impairment relay is up", relay.Start(hostPort));

  // ------------------------------------------------------------------ the phone: the shared session
  PhoneSink sink(dutyOn, dutyOff);
  ClientSessionController session;
  bool connected = false;
  if (launched) {
    ClientSessionConnectArgs args;  // what native_bridge.cpp sets for a direct session
    args.host = "127.0.0.1";
    args.videoPort = sparseRun ? hostPort : relay.frontPort;
    args.controlPort = args.videoPort;
    args.requireUdpHello = true;
    args.requireTcpControl = false;
    args.controlOverUdp = true;
    args.encodedFrameSink = &sink;  // controlIntervalMs: the product default (kClientControlIntervalMsDefault)
    connected = session.Connect(args) &&
                wait_until([&] { return session.Snapshot().state == ClientSessionState::Connected; }, 15000);
    check("the shared session connected (UDP hello + tunnelled control)", connected,
          session.Snapshot().status + " / " + session.Snapshot().lastError);
  }

  uint64_t framesAtStart = 0;
  int64_t judgeFromMs = -1, judgeToMs = -1, impairFromMs = -1, impairToMs = -1;
  if (connected) {
    const bool picture = wait_until([&] { return sink.frames() > 0; }, 20000);
    check("frames reach the phone's sink", picture, std::to_string(sink.frames()) + " frames");
    framesAtStart = sink.frames();
    if (sparseRun) {
      // The whole session is judged: a phone's first reports are where a sparse display starts to count.
      // (The host's per-second stats line prints the profile from before that second's ABR decision, so
      // "high when the span began" is read at the first frame, before any report could have mattered.)
      judgeFromMs = local_now_ms();
      std::this_thread::sleep_for(std::chrono::seconds(warmSec + runSec));
      judgeToMs = local_now_ms();
    } else {
      std::this_thread::sleep_for(std::chrono::seconds(warmSec));
      impairFromMs = local_now_ms();
      say("--- impairment " + scenario + " on for " + std::to_string(impairSec) + " s");
      relay.mode.store(static_cast<int>(impair));
      std::this_thread::sleep_for(std::chrono::seconds(impairSec));
      relay.mode.store(static_cast<int>(Impair::None));
      impairToMs = local_now_ms();
      say("--- impairment off");
      std::this_thread::sleep_for(std::chrono::seconds(afterSec));
    }
  }
  const uint64_t frames = sink.frames();
  const uint64_t drains = sink.drains();
  session.Disconnect();
  if (!sparseRun) relay.Stop();
  if (hostPi.hProcess) {
    // It exits by itself at --seconds; terminating it earlier could cut its buffered stdout.
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
  {
    std::ifstream in(hostLogPath);
    std::string line;
    while (std::getline(in, line)) {
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
  say("phone sink: " + std::to_string(frames) + " frames, " + std::to_string(drains) +
      " presentation drains (reports with present stats)");

  if (connected) {
    const int64_t from = sparseRun ? judgeFromMs : impairFromMs;
    const int64_t to = sparseRun ? judgeToMs : impairToMs + 3000;
    std::string profileBefore;
    for (const auto& h : hostSecs) if (h.ms >= 0 && h.ms < from) profileBefore = h.profile;
    uint32_t judged = 0, seconds = 0;
    for (const auto& h : hostSecs) {
      if (h.ms < from || h.ms > to) continue;
      ++seconds;
      if (h.sentFrames >= std::max<uint32_t>(2, fps / 2)) ++judged;
    }
    std::vector<AbrLine> downs;
    for (const auto& a : abr)
      if (a.ms >= from && a.ms <= to && is_down_reason(a.reason)) downs.push_back(a);
    std::string downList;
    for (const auto& d : downs) downList += d.reason + "->" + d.profile + "(" + d.evidence + ") ";
    if (downList.empty()) downList = "none";

    if (sparseRun) {
      // Before the run starts the host must be at high, or a "no step down" proves nothing.
      check("the host's ABR was at high when the judged span began", profileBefore == "high", "abrProfile=" + profileBefore);
      const bool cadence = judged >= static_cast<uint32_t>((warmSec + runSec) * 3 / 4);
      check("the host sent a real cadence through the judged span (ABR-judged seconds)", cadence,
            std::to_string(judged) + " of " + std::to_string(seconds) + " s");
      if (cadence) {
        check("THE HOST'S ABR DOES NOT STEP DOWN ON A HEALTHY PHONE WITH A SPARSE DISPLAY", downs.empty(), downList);
        std::string profileEnd;
        for (const auto& h : hostSecs) if (h.ms >= 0 && h.ms <= to) profileEnd = h.profile;
        check("...and is still at high at the end", profileEnd == "high", "abrProfile=" + profileEnd);
      } else {
        skip("ABR over the judged span", "the host did not send a real cadence");
      }
    } else {
      check("the host's ABR was at high before the impairment", profileBefore == "high", "abrProfile=" + profileBefore);
      check("the impairment did something", impair == Impair::Delay || relay.dropped.load() > 0,
            std::to_string(relay.dropped.load()) + " datagrams dropped");
      if (judgedImpair) {
        check("A REAL " + scenario + " STILL STEPS THE HOST'S ABR DOWN (within the impairment + 3 s)", !downs.empty(),
              downList + " (" + std::to_string(judged) + " judged seconds)");
      } else {
        say("NOTE  " + scenario + " (recorded, not judged): down steps " + downList + " (" + std::to_string(judged) +
            " judged seconds)");
      }
    }
  }

  if (!outDir.empty()) {
    CreateDirectoryW(outDir.c_str(), nullptr);
    CopyFileW(hostLogPath.c_str(), (outDir + std::wstring(scenario.begin(), scenario.end()) + L"_host.log").c_str(), FALSE);
  }
  (void)framesAtStart;
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

// quality r1, end to end: what ABR does with a client that never sends decode metrics, with one
// that sends them the way the Android APK does (present* only, decode fields 0), and with one that
// reports properly and then goes silent. A real isolated host, the product control scheduler and
// serializer on this side; no phone.
//
// The field case (2026-09-27): this PC -> APK 0.2.21, bitrate 6000 or 3000 at 30 fps, fell to the
// lowest rung within 5-11 s ("high_to_mid_severe" then "mid_to_low_severe") with
// clientDecodedFps=0 clientAvgLatUs=0 clientMbps=0 and nothing wrong on the host. The APK sends
// ControlClientMetrics every second with only the present* block (native_video_client_session.cpp
// fills nothing else), and the host read its zero decode fps as a relay collapse.
//
// Modes (one host run each, --mode):
//   none     never sends ControlClientMetrics           -> must not demote
//   present  sends it APK-style, present* only            -> must not demote
//   stop     sends full Windows-style metrics, then stops -> must demote on stale_active (P7 kept)
// All modes also check the r1 instrumentation is live: [keyframe] lines with reasons, and the
// per-flow byte keys on the stats line.
//
// THE CADENCE. ABR only judges seconds in which the host sent a real cadence (>= fps/2 real
// frames); a sparse second holds. So this capture target is a window of this process that IS
// composed -- on screen, alpha 1/255, click-through, never activated, repainted at 60 Hz -- and the
// test counts real (non-synthetic) frames per second itself. If the host did not send a real
// cadence, the ABR checks are NOT JUDGED rather than passed.
//
//   remote60_abr_client_evidence_e2e_test --mode none|present|stop [--host <GNLinkStream.exe>]
//   (REMOTE60_ALLOW_HOST_E2E=1)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "e2e_isolation.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "control_resume_e2e_support.hpp"

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;

namespace {

constexpr uint16_t kHostPort = 44793;
constexpr uint32_t kFps = 30;
constexpr uint32_t kBitrate = 6000000;
const wchar_t* kTargetTitle = L"remote60 abr evidence target";

// An on-screen window the user cannot see or hit: alpha 1/255, WS_EX_TRANSPARENT, never activated.
// On screen because an off-screen window is never composed and the host then sends only synthetic
// refresh frames (C0 measured about one a second), which ABR treats as sparse and ignores.
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
    wc.lpszClassName = L"Remote60AbrEvidenceTarget";
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                            wc.lpszClassName, kTargetTitle, WS_POPUP, 0, 0, 480, 270, nullptr,
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

std::string value_of(const std::string& line, const std::string& key) {
  const std::string needle = " " + key + "=";
  const size_t at = line.find(needle);
  if (at == std::string::npos) return {};
  const size_t start = at + needle.size();
  const size_t end = line.find(' ', start);
  return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::cout.setf(std::ios::unitbuf);
  if (!host_e2e_allowed()) {
    std::printf("SKIP  abr_client_evidence_e2e_test (starts a listening host)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return kE2eSkippedExit;
  }
  std::string mode;
  std::wstring hostExe;
  for (int i = 1; i < argc; ++i) {
    const std::wstring a = argv[i];
    if (a == L"--thumbnail") {  // staged as GNLinkCapture.exe
      Sleep(300000);
      return 0;
    }
    if (a == L"--mode" && i + 1 < argc) {
      const std::wstring m = argv[++i];
      mode.assign(m.begin(), m.end());
    } else if (a == L"--host" && i + 1 < argc) {
      hostExe = argv[++i];
    }
  }
  if (mode != "none" && mode != "present" && mode != "stop") {
    std::printf("usage: --mode none|present|stop [--host <GNLinkStream.exe>]\n");
    return 2;
  }
  std::cout << "mode: " << mode << "\n";

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const std::wstring dir = std::wstring(temp) + L"remote60_abr_ev_" +
                           std::to_wstring(GetCurrentProcessId()) + L"\\";
  CreateDirectoryW(dir.substr(0, dir.size() - 1).c_str(), nullptr);
  const std::wstring me = self_path();
  if (hostExe.empty()) hostExe = directory_of(me) + L"GNLinkStream.exe";
  const bool staged = CopyFileW(hostExe.c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) &&
                      CopyFileW(me.c_str(), (dir + L"GNLinkCapture.exe").c_str(), FALSE);
  check("a host and a never-answering helper could be staged", staged,
        std::string(hostExe.begin(), hostExe.end()));

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
    std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                       L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(kHostPort) +
                       L" --fps " + std::to_wstring(kFps) + L" --bitrate " + std::to_wstring(kBitrate) +
                       L" --seconds 120 --input-injection-mode none" +
                       L" --capture-window-title \"" + kTargetTitle + L"\"";
    const bool noInjection = cmd.find(L"--input-injection-mode none") != std::wstring::npos;
    check("the host is started with input injection off", noInjection);
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    hostLog = CreateFileW(hostLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
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
    const bool isoOk = e2e_command_is_isolated(cmd, dir, &isoWhy) &&
                       e2e_path_is_under(e2e_block_localappdata(isoEnv), dir);
    check("the launched process is isolated from the user's files", isoOk, isoWhy);
    launched = noInjection && isoOk &&
               CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr,
                              hostLog != INVALID_HANDLE_VALUE,
                              CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                              isoEnv.data(), dir.c_str(), &si, &hostPi) != 0;
    if (launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  check("the host started", launched);

  // Per second of the run: real (non-synthetic) frames completed at this end.
  std::vector<uint32_t> realFramesPerSec;
  uint64_t metricsStoppedAtSec = 0;
  if (launched) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_port = htons(kHostPort);
    InetPtonW(AF_INET, L"127.0.0.1", &hostAddr.sin_addr);
    connect(sock, reinterpret_cast<const sockaddr*>(&hostAddr), sizeof(hostAddr));
    DWORD rcvTimeout = 20;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));
    int rcvBuf = 4 << 20;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));

    UdpHelloOptions hello;
    hello.budgetMs = 20000;
    hello.sliceMaxMs = 250;
    hello.retrySleepMs = 50;
    std::string helloError;
    uint32_t ackFeatures = 0;
    const bool handshake = udp_hello_handshake(sock, hello, nullptr, &helloError, &ackFeatures, nullptr);
    check("the viewer's Hello is answered", handshake, helloError);

    UdpControlChannel control;
    control.Configure(
        [&sock](const void* data, size_t len) {
          return send(sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, 1200);
    UdpControlLink link(&control, 12000);

    std::atomic<bool> stop{false};
    std::mutex fmu;
    std::set<uint32_t> realSeqThisSec;
    std::thread ingress([&] {
      std::vector<uint8_t> buf(2048);
      while (!stop.load()) {
        control.Tick();
        const int n = recv(sock, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
        if (n > 0 && !control.OnPacket(buf.data(), static_cast<size_t>(n)) &&
            n >= static_cast<int>(sizeof(UdpVideoChunkHeader))) {
          UdpVideoChunkHeader h{};
          std::memcpy(&h, buf.data(), sizeof(h));
          if (h.magic == kMagic && h.kind == static_cast<uint16_t>(UdpPacketKind::VideoChunk) &&
              h.size == sizeof(UdpVideoChunkHeader) && (h.flags & 0x10u) == 0 &&
              (h.flags & 0x40u) == 0 && (h.flags & 0x4u) != 0) {  // last chunk of a real frame
            std::lock_guard<std::mutex> lk(fmu);
            realSeqThisSec.insert(h.seq);
          }
        }
      }
    });

    // What this viewer reports, per mode, set once a second by the loop below.
    std::mutex mmu;
    ClientControlMetricsSnapshot pendingMetrics{};
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
      while (!controlStop.load()) {
        ClientControlMetricsSnapshot metrics;
        {
          std::lock_guard<std::mutex> lk(mmu);
          metrics = pendingMetrics;
        }
        ControlOutboundAction action{};
        const uint64_t now = qpc_now_us();
        if (scheduler.NextAction(now, metrics, &windowPanel, &streamState, &captureMode, &keyframe,
                                 &runtimeTune, &inputQueue, &action, nullptr, nullptr)) {
          TcpControlResponse response;
          if (execute_control_action(link, action, &response) &&
              action.kind == ControlOutboundActionKind::Ping) {
            scheduler.OnPingCompleted(qpc_now_us());
          }
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
      }
    });

    // The run: 36 seconds. 'stop' reports fully for the first 14 and is silent after.
    constexpr int kRunSec = 36;
    constexpr int kStopAfterSec = 14;
    for (int s = 0; s < kRunSec; ++s) {
      const uint64_t now = qpc_now_us();
      ClientControlMetricsSnapshot m{};
      if (mode == "present") {
        // Exactly what the APK's session fills: the present* block and nothing else.
        m.message.presentTargetIntervalUs = 1000000 / kFps;
        m.message.presentFpsX100 = kFps * 100;
        m.message.presentGapP50Us = 1000000 / kFps;
        m.message.presentGapP95Us = 1000000 / kFps + 4000;
        m.message.presentGapMaxUs = 1000000 / kFps + 9000;
        m.message.presentSampleCount = kFps;
        m.message.presentDisplayedCount = kFps;
        m.updatedQpcUs = now;
      } else if (mode == "stop" && s < kStopAfterSec) {
        // A healthy Windows-style report: decode fields present and fine.
        m.message.width = 480;
        m.message.height = 270;
        m.message.recvFpsX100 = kFps * 100;
        m.message.decodedFpsX100 = kFps * 100;
        m.message.recvMbpsX1000 = 3000;
        m.message.avgLatencyUs = 30000;
        m.message.maxLatencyUs = 45000;
        m.message.avgDecodeTailUs = 8000;
        m.message.maxDecodeTailUs = 15000;
        m.updatedQpcUs = now;
      }
      if (mode == "stop" && s == kStopAfterSec) metricsStoppedAtSec = static_cast<uint64_t>(s);
      {
        std::lock_guard<std::mutex> lk(mmu);
        pendingMetrics = m;
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
      std::lock_guard<std::mutex> lk(fmu);
      realFramesPerSec.push_back(static_cast<uint32_t>(realSeqThisSec.size()));
      realSeqThisSec.clear();
    }

    controlStop.store(true);
    if (controlThread.joinable()) controlThread.join();
    stop.store(true);
    if (ingress.joinable()) ingress.join();
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

  // ------------------------------------------------------------------ the host's own account
  std::vector<std::string> abrLines, keyLines;
  std::string lastStats;
  {
    std::ifstream in(hostLogPath);
    std::string line;
    while (std::getline(in, line)) {
      if (line.find("[abr] profile=") != std::string::npos) abrLines.push_back(line);
      if (line.find("[keyframe] seq=") != std::string::npos) keyLines.push_back(line);
      if (line.find(" udpTxWireEstBytes=") != std::string::npos) lastStats = line;
    }
  }

  std::string perSec;
  uint32_t cadenceSeconds = 0;
  for (size_t i = 0; i < realFramesPerSec.size(); ++i) {
    perSec += std::to_string(realFramesPerSec[i]) + " ";
    if (i >= 4 && realFramesPerSec[i] >= kFps / 2) ++cadenceSeconds;
  }
  std::cout << "real frames per second at this end: " << perSec << "\n";
  const size_t judged = realFramesPerSec.size() > 4 ? realFramesPerSec.size() - 4 : 0;
  const bool cadenceOk = launched && judged > 0 && cadenceSeconds * 10 >= judged * 8;
  std::cout << "seconds after warmup with a real cadence (>= " << kFps / 2 << "): " << cadenceSeconds
            << " of " << judged << "\n";

  std::cout << "\n--- host [abr] lines (" << abrLines.size() << ") ---\n";
  for (const auto& l : abrLines) std::cout << "  " << l << "\n";
  std::cout << "\n--- host [keyframe] lines (" << keyLines.size() << ") ---\n";
  for (const auto& l : keyLines) std::cout << "  " << l << "\n";
  if (!lastStats.empty()) {
    std::cout << "\n--- per-flow bytes, last stats line ---\n";
    for (const char* k : {"udpTxBytes", "udpTxParityBytes", "udpTxChunkHdrBytes", "udpTxVideoDatagrams",
                          "udpTxNackBytes", "udpTxNackDatagrams", "ctlTxBytes", "ctlTxDatagrams",
                          "udpTxIpUdpHdrEstBytes", "udpTxWireEstBytes"}) {
      std::cout << "  " << k << "=" << value_of(lastStats, k) << "\n";
    }
  }

  if (launched) {
    // Instrumentation (A), independent of the cadence.
    bool keyReasoned = !keyLines.empty();
    for (const auto& l : keyLines) keyReasoned = keyReasoned && !value_of(l, "reasons").empty();
    check("every [keyframe] line names its reasons", keyReasoned, std::to_string(keyLines.size()) + " lines");
    const std::string parity = value_of(lastStats, "udpTxParityBytes");
    const std::string wire = value_of(lastStats, "udpTxWireEstBytes");
    const std::string payload = value_of(lastStats, "udpTxBytes");
    const bool flows = !parity.empty() && !wire.empty() && !payload.empty() &&
                       std::stoull(parity) > 0 && std::stoull(wire) > std::stoull(payload);
    check("the stats line carries the per-flow bytes, and the wire estimate exceeds the payload", flows,
          "payload=" + payload + " parity=" + parity + " wire=" + wire);

    if (!cadenceOk) {
      skip("ABR verdict for mode " + mode, "the host did not send a real cadence -- NOT JUDGED");
    } else if (mode == "none" || mode == "present") {
      check("ABR did not demote a client whose decode evidence " +
                std::string(mode == "none" ? "never arrived" : "was never reported"),
            abrLines.empty(), abrLines.empty() ? "" : abrLines.front());
    } else {
      bool staleDemote = false;
      for (const auto& l : abrLines) {
        if (value_of(l, "evidence").find("stale_active") != std::string::npos &&
            value_of(l, "reason").find("severe") != std::string::npos) {
          staleDemote = true;
        }
      }
      check("ABR demoted a client that reported and then went silent (stale_active, P7 kept)",
            staleDemote, std::to_string(abrLines.size()) + " [abr] lines");
    }
  }
  (void)metricsStoppedAtSec;

  for (int i = 0; i < 40; ++i) {
    DeleteFileW((dir + L"host.log").c_str());
    DeleteFileW((dir + L"GNLinkStream.exe").c_str());
    DeleteFileW((dir + L"GNLinkCapture.exe").c_str());
    remove_tree_under(dir + L"localappdata", dir);
    if (RemoveDirectoryW(dir.substr(0, dir.size() - 1).c_str())) break;
    Sleep(100);
  }
  check("the scratch directory is cleaned up",
        GetFileAttributesW(dir.substr(0, dir.size() - 1).c_str()) == INVALID_FILE_ATTRIBUTES);
  WSACleanup();
  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  (" << gChecks
            << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}

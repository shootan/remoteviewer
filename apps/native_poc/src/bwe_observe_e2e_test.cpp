// C0 stage 1, end to end: a real host, a viewer-side estimator fed exactly what the product feeds
// it, and a bottleneck between them whose truth this test knows. MEASUREMENT, not judgement.
//
// What it answers (task_c0_r1.md, 검증 3):
//   - does the host's [bwe-observe] line show a queue building BEFORE the bottleneck starts
//     dropping or the picture stalls badly?
//   - does it tell an application-limited stream from a path that is full?
//   - what happens on a still screen, when the reports themselves are lost for a while, and when
//     the path's delay steps up (a path change)?
// Nothing here asserts that the estimator is RIGHT -- the constants are provisional and this run
// is what they are to be judged by. What is asserted is the plumbing: the host was started
// isolated, the feature was negotiated (or, with --observe-off, not), the host logged what the
// viewer sent and nothing when it was not negotiated, and the bottleneck did what it says.
//
// THE BOTTLENECK. Every host->viewer datagram (video and control alike) goes through a finite
// drop-tail queue drained by a token bucket at the phase's rate, then through a fixed delay line.
// Cross traffic -- datagrams that are not video, enqueued into the same queue and discarded on
// release -- competes for the rate, which is how the queue is made to grow when the host's own
// video is small. The timestamps the estimator reads are the host's (stamped before this queue),
// so the queue sits exactly where a network's would: behind the timestamp.
//
// THE VIEWER SIDE is this process: its ingress hands every video chunk to the product estimator
// through bwe_chunk_from_header (the same call the viewer's receive loop makes), and its control
// thread sends through the product scheduler (NextAction) and serializer. What it does NOT run is
// the viewer's receive loop itself -- decoder, assembler, window -- and its loss figure is its own
// simple count of incomplete frames, not the assembler's drop permille.
//
// THE VIDEO THE ESTIMATOR IS FED is a stand-in source in this process, not the host's. Measured on
// the first run: a host capturing this test's off-screen window sends ONLY synthetic refresh
// frames (every frame flagged 0x40), about one a second at ~1 kB -- the window is never composed,
// so capture has no content to deliver. One delay sample a second cannot show a queue, so the
// stand-in sends 30 fps x 20 kB frames (4.8 Mb/s) through the same bottleneck, chunked 1200 bytes
// and stamped the way the host stamps them: sendQpcUs once per frame, just before its chunks go out
// (host_encoded_sender.cpp). It uses its own stream generation, and only that generation is fed to
// the estimator. The real host is still the other end of everything else: the Hello, the feature
// negotiation, the control channel, and the [bwe-observe] log this test reads.
//
//   remote60_bwe_observe_e2e_test [--observe-off]      (REMOTE60_ALLOW_HOST_E2E=1)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
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
#include "bandwidth_observe_wire.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "viewer_bwe.hpp"
#include "control_resume_e2e_support.hpp"

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;

namespace {

uint16_t kHostPort = 0;    // all three picked at run time (e2e_pick_free_ports): tests run side by side
uint16_t kShaperPort = 0;
uint16_t kSourcePort = 0;  // the stand-in video source (see the header)
constexpr uint64_t kSourceGeneration = 0xC0C0C0C0ull;

// ------------------------------------------------------------------------------ the bottleneck

class Bottleneck {
 public:
  std::atomic<uint64_t> rateBps{20000000};
  std::atomic<uint64_t> crossBps{0};
  std::atomic<uint64_t> extraDelayUs{0};
  std::atomic<bool> dropUpControl{false};  // viewer -> host control lost (feedback loss)
  std::atomic<uint64_t> queueLimitBytes{150000};
  // Truth, per second (reset by TakeSecond).
  std::atomic<uint64_t> videoDropped{0}, crossDropped{0}, videoDelivered{0}, maxQueueBytes{0};
  std::atomic<uint64_t> upDropped{0};

  bool Start(uint16_t listenPort, uint16_t hostPort, uint16_t sourcePort) {
    sourcePortNet_ = htons(sourcePort);
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in bind{};
    bind.sin_family = AF_INET;
    bind.sin_port = htons(listenPort);
    InetPtonW(AF_INET, L"127.0.0.1", &bind.sin_addr);
    if (::bind(sock_, reinterpret_cast<const sockaddr*>(&bind), sizeof(bind)) != 0) return false;
    DWORD timeout = 20;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    int buf = 4 << 20;
    setsockopt(sock_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buf), sizeof(buf));
    host_.sin_family = AF_INET;
    host_.sin_port = htons(hostPort);
    InetPtonW(AF_INET, L"127.0.0.1", &host_.sin_addr);
    rx_ = std::thread([this] { Receive(); });
    tx_ = std::thread([this] { Release(); });
    cross_ = std::thread([this] { Cross(); });
    return true;
  }
  void Stop() {
    stop_.store(true);
    if (rx_.joinable()) rx_.join();
    if (tx_.joinable()) tx_.join();
    if (cross_.joinable()) cross_.join();
    if (sock_ != INVALID_SOCKET) closesocket(sock_);
  }
  uint64_t QueuedBytes() {
    std::lock_guard<std::mutex> lock(mu_);
    return queuedBytes_;
  }

 private:
  struct Item {
    std::vector<uint8_t> bytes;
    bool cross = false;
    uint64_t deliverAtUs = 0;
  };

  void Enqueue(Item item) {
    std::lock_guard<std::mutex> lock(mu_);
    if (queuedBytes_ + item.bytes.size() > queueLimitBytes.load()) {
      (item.cross ? crossDropped : videoDropped).fetch_add(1);
      return;
    }
    queuedBytes_ += item.bytes.size();
    if (queuedBytes_ > maxQueueBytes.load()) maxQueueBytes.store(queuedBytes_);
    queue_.push_back(std::move(item));
  }

  void Receive() {
    std::vector<uint8_t> buf(2048);
    while (!stop_.load()) {
      sockaddr_in from{};
      int fromLen = sizeof(from);
      const int n = recvfrom(sock_, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()),
                             0, reinterpret_cast<sockaddr*>(&from), &fromLen);
      if (n <= 0) continue;
      const bool fromHost = from.sin_port == host_.sin_port || from.sin_port == sourcePortNet_;
      if (!fromHost) {
        if (!haveViewer_) {
          viewer_ = from;
          haveViewer_ = true;
        }
        // Viewer -> host is not the bottleneck; only its control can be lost on purpose.
        uint16_t kind = 0;
        if (n >= 6) std::memcpy(&kind, buf.data() + 4, 2);
        const bool control = kind == static_cast<uint16_t>(UdpPacketKind::ControlData) ||
                             kind == static_cast<uint16_t>(UdpPacketKind::ControlAck) ||
                             kind == static_cast<uint16_t>(UdpPacketKind::ControlNack);
        if (control && dropUpControl.load()) {
          upDropped.fetch_add(1);
          continue;
        }
        (void)sendto(sock_, reinterpret_cast<const char*>(buf.data()), n, 0,
                     reinterpret_cast<const sockaddr*>(&host_), sizeof(host_));
        continue;
      }
      Item item;
      item.bytes.assign(buf.begin(), buf.begin() + n);
      Enqueue(std::move(item));
    }
  }

  void Cross() {
    uint64_t last = qpc_now_us();
    double credit = 0;
    while (!stop_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      const uint64_t now = qpc_now_us();
      credit += static_cast<double>(crossBps.load()) / 8.0 * static_cast<double>(now - last) / 1e6;
      last = now;
      while (credit >= 1200) {
        credit -= 1200;
        Item item;
        item.bytes.assign(1200, 0);
        item.cross = true;
        Enqueue(std::move(item));
      }
    }
  }

  void Release() {
    uint64_t last = qpc_now_us();
    double tokens = 0;
    std::deque<Item> delayLine;
    while (!stop_.load()) {
      std::this_thread::sleep_for(std::chrono::microseconds(500));
      const uint64_t now = qpc_now_us();
      tokens += static_cast<double>(rateBps.load()) / 8.0 * static_cast<double>(now - last) / 1e6;
      tokens = std::min(tokens, 3000.0);  // no bursts beyond a couple of packets
      last = now;
      for (;;) {
        Item item;
        {
          std::lock_guard<std::mutex> lock(mu_);
          if (queue_.empty() || tokens < static_cast<double>(queue_.front().bytes.size())) break;
          item = std::move(queue_.front());
          queue_.pop_front();
          queuedBytes_ -= item.bytes.size();
        }
        tokens -= static_cast<double>(item.bytes.size());
        if (item.cross) continue;  // cross traffic has used the link; it goes nowhere
        item.deliverAtUs = now + extraDelayUs.load();
        delayLine.push_back(std::move(item));
      }
      while (!delayLine.empty() && delayLine.front().deliverAtUs <= now) {
        if (haveViewer_) {
          const auto& b = delayLine.front().bytes;
          (void)sendto(sock_, reinterpret_cast<const char*>(b.data()), static_cast<int>(b.size()), 0,
                       reinterpret_cast<const sockaddr*>(&viewer_), sizeof(viewer_));
          videoDelivered.fetch_add(b.size());
        }
        delayLine.pop_front();
      }
    }
  }

  SOCKET sock_ = INVALID_SOCKET;
  uint16_t sourcePortNet_ = 0;
  sockaddr_in host_{};
  sockaddr_in viewer_{};
  std::atomic<bool> haveViewer_{false};
  std::atomic<bool> stop_{false};
  std::mutex mu_;
  std::deque<Item> queue_;
  uint64_t queuedBytes_ = 0;
  std::thread rx_, tx_, cross_;
};

// ------------------------------------------------------------------------------ the viewer side

struct ViewerSide {
  SOCKET sock = INVALID_SOCKET;
  UdpControlChannel control;
  std::unique_ptr<UdpControlLink> link;
  ViewerBandwidthEstimator estimator;
  std::mutex mu;
  ClientBandwidthSnapshot snapshot;
  std::vector<std::string> reports;  // the estimator's own reports, with this process's time
  std::atomic<bool> stop{false};
  std::atomic<uint64_t> videoChunks{0};
  std::atomic<uint64_t> forceFreshnessProbe{0};
  std::thread ingress;
  // Loss: frames whose chunks did not all arrive, counted once the frame is 5 behind the newest.
  std::map<uint32_t, std::pair<uint16_t, uint16_t>> frames;  // seq -> (chunkCount, received)
  uint32_t newestSeq = 0;
  uint32_t lostInWindow = 0, completeInWindow = 0;

  void Start(const std::function<std::string()>& phaseName) {
    ingress = std::thread([this, phaseName] {
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
            videoChunks.fetch_add(1);
          }
          // Only the stand-in's generation: the host's own refresh frames are delivered, not fed.
          if (h.magic == kMagic && h.kind == static_cast<uint16_t>(UdpPacketKind::VideoChunk) &&
              h.size == sizeof(UdpVideoChunkHeader) && h.streamGeneration == kSourceGeneration) {
            estimator.OnChunk(bwe_chunk_from_header(h, static_cast<uint32_t>(n), now));
            if ((h.flags & 0x10u) == 0) {
              auto& f = frames[h.seq];
              f.first = h.chunkCount;
              ++f.second;
              if (static_cast<int32_t>(h.seq - newestSeq) > 0) newestSeq = h.seq;
            }
            while (!frames.empty() && static_cast<int32_t>(newestSeq - frames.begin()->first) > 5) {
              const auto& f = frames.begin()->second;
              (f.second >= f.first ? completeInWindow : lostInWindow) += 1;
              frames.erase(frames.begin());
            }
          }
        }
        BweReport r;
        const uint32_t total = lostInWindow + completeInWindow;
        const uint32_t lossPm = total ? lostInWindow * 1000u / total : 0;
        if (estimator.Report(now, lossPm, &r)) {
          lostInWindow = completeInWindow = 0;
          char line[400];
          std::snprintf(line, sizeof(line),
                        "[viewer-bwe] phase=%s bwe=%u state=%s rate=%s goodputUnique=%u wireLoad=%u gradUs=%d thrUs=%d lossPm=%u appLimited=%d static=%d samples=%u frames=%u synthetic=%u",
                        phaseName().c_str(), r.bweBps, to_string(r.usage), to_string(r.rateState),
                        r.goodputUniqueBps, r.wireLoadBps, r.delayGradientUs, r.thresholdUs, r.lossPm,
                        r.appLimited ? 1 : 0, r.staticHold ? 1 : 0, r.delaySamples, r.frames,
                        r.syntheticFrames);
          std::cout << line << "\n";
          std::lock_guard<std::mutex> lock(mu);
          reports.push_back(line);
          snapshot.message = make_client_bandwidth_message(r, 0, now);
          snapshot.updatedQpcUs = now;
        }
      }
    });
  }
  void Stop() {
    stop.store(true);
    if (ingress.joinable()) ingress.join();
  }
};

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::cout.setf(std::ios::unitbuf);
  if (!host_e2e_allowed()) {
    std::printf("SKIP  bwe_observe_e2e_test (starts a listening host)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return kE2eSkippedExit;
  }
  bool observeOff = false;
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--thumbnail") {  // staged as GNLinkCapture.exe
      Sleep(300000);
      return 0;
    }
    if (std::wstring(argv[i]) == L"--observe-off") observeOff = true;
  }
  std::cout << "mode: " << (observeOff ? "observation OFF (viewer does not ask)" : "observation ON") << "\n";

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
  {
    const std::vector<uint16_t> ports = remote60::native_poc::e2e::e2e_pick_free_ports(SOCK_DGRAM, 3);
    if (ports.size() != 3) {
      std::printf("FAIL  no free UDP ports for the host, the shaper and the source\n");
      return 1;
    }
    kHostPort = ports[0];
    kShaperPort = ports[1];
    kSourcePort = ports[2];
    std::printf("ports host %u shaper %u source %u (picked at run time)\n", kHostPort, kShaperPort, kSourcePort);
  }

  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const std::wstring dir = std::wstring(temp) + L"remote60_c0_bwe_" +
                           std::to_wstring(GetCurrentProcessId()) + L"\\";
  CreateDirectoryW(dir.substr(0, dir.size() - 1).c_str(), nullptr);
  const std::wstring me = self_path();
  const std::wstring myDir = directory_of(me);
  const bool staged =
      CopyFileW((myDir + L"GNLinkStream.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) &&
      CopyFileW(me.c_str(), (dir + L"GNLinkCapture.exe").c_str(), FALSE);
  check("a host and a never-answering helper could be staged", staged);

  InjectTarget target;
  check("a window of this process is up for the host to capture", target.Start());
  Bottleneck link;
  check("the bottleneck is listening between the viewer and the host",
        link.Start(kShaperPort, kHostPort, kSourcePort));

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
    // The same two knobs the C3 e2e uses to keep frames coming from an unattended desktop.
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_FRAME_GATING_DISABLE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_STATIC_SCENE_FPS", L"15");
    std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                       L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(kHostPort) +
                       L" --seconds 240 --capture-window-title \"c3 inject target\"";
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
    // Isolated from the user's files (RV-00), the same guard every host e2e now has.
    const std::wstring isoAppData = dir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> isoEnv = e2e_isolated_environment(isoAppData);
    std::string isoWhy;
    const bool isoOk = e2e_command_is_isolated(cmd, dir, &isoWhy) &&
                       e2e_path_is_under(e2e_block_localappdata(isoEnv), dir);
    check("the launched process is isolated from the user's files", isoOk, isoWhy);
    launched = isoOk &&
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

  ViewerSide viewer;
  std::atomic<uint32_t> probeSeq{0};
  std::atomic<int> phaseIndex{0};
  const char* kPhaseNames[] = {"setup", "A-20M-idle", "B-6M-cross-ramp", "C-20M-recover",
                               "D1-still", "D2-moving", "E-feedback-loss", "F-path+40ms", "end"};
  bool negotiated = false;
  std::vector<std::string> truth;

  if (launched) {
    viewer.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in shaperAddr{};
    shaperAddr.sin_family = AF_INET;
    shaperAddr.sin_port = htons(kShaperPort);
    InetPtonW(AF_INET, L"127.0.0.1", &shaperAddr.sin_addr);
    connect(viewer.sock, reinterpret_cast<const sockaddr*>(&shaperAddr), sizeof(shaperAddr));
    DWORD rcvTimeout = 20;
    setsockopt(viewer.sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));
    int rcvBuf = 1 << 20;
    setsockopt(viewer.sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));

    UdpHelloOptions hello;
    hello.budgetMs = 20000;
    hello.sliceMaxMs = 250;
    hello.retrySleepMs = 50;
    hello.requestBandwidthObserve = !observeOff;
    std::string helloError;
    uint32_t ackFeatures = 0;
    const bool handshake = udp_hello_handshake(viewer.sock, hello, nullptr, &helloError, &ackFeatures, nullptr);
    check("the viewer's Hello is answered through the bottleneck", handshake, helloError);
    check("the host advertises bandwidth observation", (ackFeatures & kUdpFeatureBandwidthObserve) != 0,
          "features=" + std::to_string(ackFeatures));
    negotiated = bandwidth_observe_negotiated(!observeOff, ackFeatures);
    check(observeOff ? "...and, not asked for, it is not negotiated" : "...and it is negotiated",
          negotiated == !observeOff);

    viewer.control.Configure(
        [&viewer](const void* data, size_t len) {
          return send(viewer.sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, 1200);
    viewer.link = std::make_unique<UdpControlLink>(&viewer.control, 12000);
    viewer.Start([&] { return std::string(kPhaseNames[phaseIndex.load()]); });

    // The control thread: the product scheduler and serializer, pings and all.
    std::atomic<bool> controlStop{false};
    std::atomic<uint64_t> sentReports{0};
    std::thread controlThread([&] {
      ClientControlScheduler scheduler;
      WindowPanelStateModel windowPanel;
      StreamStateControl streamState;
      CaptureModeRequestState captureMode;
      KeyframeRequestState keyframe(120000, 300000, 3);
      RuntimeTuneState runtimeTune(300000, 30000000, 250000, 1, 240);
      ClientInputQueue inputQueue;
      ClientControlMetricsSnapshot metrics{};
      scheduler.Reset(kClientControlIntervalMsDefault, qpc_now_us());
      bool probeNext = false;
      while (!controlStop.load()) {
        ClientBandwidthSnapshot snap;
        {
          std::lock_guard<std::mutex> lock(viewer.mu);
          snap = viewer.snapshot;
        }
        // One report goes out with a nonsense viewer clock: the host must still take it, because
        // it judges freshness on its own receipt time and never compares the two clocks.
        if (viewer.forceFreshnessProbe.exchange(0) != 0) probeNext = true;
        ControlOutboundAction action{};
        const uint64_t now = qpc_now_us();
        if (scheduler.NextAction(now, metrics, &windowPanel, &streamState, &captureMode, &keyframe,
                                 &runtimeTune, &inputQueue, &action, nullptr,
                                 negotiated ? &snap : nullptr)) {
          if (action.kind == ControlOutboundActionKind::ClientBandwidth) {
            if (probeNext) {
              action.clientBandwidth.clientSendQpcUs = 0xFFFFFFFFFFFF0000ull;
              probeSeq.store(action.clientBandwidth.seq);
              probeNext = false;
            }
            sentReports.fetch_add(1);
          }
          TcpControlResponse response;
          if (execute_control_action(*viewer.link, action, &response) &&
              action.kind == ControlOutboundActionKind::Ping) {
            scheduler.OnPingCompleted(qpc_now_us());
          }
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
      }
    });

    const bool videoUp = wait_until([&] { return viewer.videoChunks.load() > 0; }, 20000);
    check("video arrives through the bottleneck", videoUp, std::to_string(viewer.videoChunks.load()) + " chunks");

    // The stand-in video source (see the header): frames of `sourceBytes` at `sourceFps`, chunked
    // 1200 bytes, sendQpcUs stamped once per frame just before its chunks go out.
    std::atomic<uint32_t> sourceBytes{20000};
    std::atomic<uint32_t> sourceFps{30};
    std::atomic<bool> sourceSynthetic{false};
    std::atomic<bool> sourceStop{false};
    std::thread source([&] {
      SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      sockaddr_in bindAddr{};
      bindAddr.sin_family = AF_INET;
      bindAddr.sin_port = htons(kSourcePort);
      InetPtonW(AF_INET, L"127.0.0.1", &bindAddr.sin_addr);
      bind(s, reinterpret_cast<const sockaddr*>(&bindAddr), sizeof(bindAddr));
      sockaddr_in to{};
      to.sin_family = AF_INET;
      to.sin_port = htons(kShaperPort);
      InetPtonW(AF_INET, L"127.0.0.1", &to.sin_addr);
      std::vector<uint8_t> dgram(sizeof(UdpVideoChunkHeader) + 1200, 0);
      uint32_t seq = 1;
      uint64_t next = qpc_now_us();
      while (!sourceStop.load()) {
        const uint64_t interval = 1000000ull / std::max<uint32_t>(1, sourceFps.load());
        next += interval;
        const uint32_t bytes = sourceBytes.load();
        const uint16_t chunks = static_cast<uint16_t>((bytes + 1199) / 1200);
        UdpVideoChunkHeader h{};
        h.seq = seq++;
        h.payloadSize = bytes;
        h.chunkCount = chunks;
        h.streamGeneration = kSourceGeneration;
        h.sendQpcUs = qpc_now_us();
        for (uint16_t c = 0; c < chunks; ++c) {
          h.chunkIndex = c;
          h.chunkOffset = c * 1200u;
          h.chunkSize = std::min<uint32_t>(1200u, bytes - c * 1200u);
          h.flags = static_cast<uint16_t>((c == 0 ? 0x2u : 0u) | (c + 1 == chunks ? 0x4u : 0u) |
                                          (sourceSynthetic.load() ? 0x40u : 0u));
          std::memcpy(dgram.data(), &h, sizeof(h));
          (void)sendto(s, reinterpret_cast<const char*>(dgram.data()),
                       static_cast<int>(sizeof(h) + h.chunkSize), 0,
                       reinterpret_cast<const sockaddr*>(&to), sizeof(to));
        }
        const uint64_t now = qpc_now_us();
        if (next > now) std::this_thread::sleep_for(std::chrono::microseconds(next - now));
        else next = now;
      }
      closesocket(s);
    });

    // ------------------------------------------------------------------ the phases
    const auto run_phase = [&](int index, int seconds, const std::function<void(int)>& perSecond) {
      phaseIndex.store(index);
      for (int s = 0; s < seconds; ++s) {
        if (perSecond) perSecond(s);
        link.videoDropped.store(0);
        link.crossDropped.store(0);
        link.videoDelivered.store(0);
        link.maxQueueBytes.store(0);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const uint64_t rate = link.rateBps.load();
        const uint64_t maxQ = link.maxQueueBytes.load();
        char line[300];
        std::snprintf(line, sizeof(line),
                      "[truth] phase=%s t=%d rate=%llu cross=%llu extraDelayMs=%llu maxQueueMs=%llu videoDropped=%llu crossDropped=%llu delivered=%llu upControlDropped=%llu",
                      kPhaseNames[index], s, static_cast<unsigned long long>(rate),
                      static_cast<unsigned long long>(link.crossBps.load()),
                      static_cast<unsigned long long>(link.extraDelayUs.load() / 1000),
                      static_cast<unsigned long long>(rate ? maxQ * 8000ull / rate : 0),
                      static_cast<unsigned long long>(link.videoDropped.load()),
                      static_cast<unsigned long long>(link.crossDropped.load()),
                      static_cast<unsigned long long>(link.videoDelivered.load() * 8),
                      static_cast<unsigned long long>(link.upDropped.load()));
        std::cout << line << "\n";
        truth.push_back(line);
      }
    };
    if (videoUp) {
      run_phase(1, 10, [&](int) {
        link.rateBps.store(20000000);
        link.crossBps.store(0);
      });
      // 6 Mbps. The source sends 4.8 Mb/s; cross traffic climbs 0.25 Mb/s a second from zero, so
      // the link fills at about 1.2 Mb/s of cross (5 s in) and the queue builds from there.
      run_phase(2, 14, [&](int s) {
        link.rateBps.store(6000000);
        link.crossBps.store(static_cast<uint64_t>(s) * 250000);
      });
      run_phase(3, 10, [&](int) {
        link.rateBps.store(20000000);
        link.crossBps.store(0);
      });
      // A still screen as the host sends one: small synthetic refresh frames at 15 fps.
      run_phase(4, 8, [&](int s) {
        if (s == 0) {
          target.paused.store(true);
          sourceSynthetic.store(true);
          sourceBytes.store(1000);
          sourceFps.store(15);
        }
      });
      run_phase(5, 8, [&](int s) {
        if (s == 0) {
          target.paused.store(false);
          sourceSynthetic.store(false);
          sourceBytes.store(20000);
          sourceFps.store(30);
        }
      });
      run_phase(6, 8, [&](int s) {
        link.dropUpControl.store(s < 3);  // three seconds of lost feedback, then it comes back
        if (s == 5) viewer.forceFreshnessProbe.store(1);
      });
      link.dropUpControl.store(false);
      run_phase(7, 8, [&](int s) { if (s == 0) link.extraDelayUs.store(40000); });
      phaseIndex.store(8);
    }
    sourceStop.store(true);
    if (source.joinable()) source.join();
    controlStop.store(true);
    if (controlThread.joinable()) controlThread.join();
    viewer.Stop();
    check(observeOff ? "the viewer sent no reports (not negotiated)" : "the viewer sent its reports",
          observeOff ? sentReports.load() == 0 : sentReports.load() >= 40,
          std::to_string(sentReports.load()) + " sent");
  }

  // ------------------------------------------------------------------ teardown, then the host's log
  link.Stop();
  target.Stop();
  if (viewer.sock != INVALID_SOCKET) closesocket(viewer.sock);
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  CloseHandle(job);
  if (hostPi.hProcess) {
    WaitForSingleObject(hostPi.hProcess, 20000);
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);

  std::vector<std::string> hostObserve;
  std::vector<std::string> hostAbr;
  bool ignoredLine = false;
  {
    std::ifstream in(hostLogPath);
    std::string line;
    while (std::getline(in, line)) {
      if (line.find("[bwe-observe]") != std::string::npos) {
        if (line.find("ignored") != std::string::npos) ignoredLine = true;
        else hostObserve.push_back(line);
      }
      // What the regression compares with the feature on and off: every line the rate and
      // pacing decisions write.
      if (line.find("runtime-config-applied") != std::string::npos ||
          line.find("rate-control") != std::string::npos ||
          line.find("bitrateTarget") != std::string::npos ||
          line.find("pace") != std::string::npos) {
        hostAbr.push_back(line);
      }
    }
  }
  if (launched) {
    if (observeOff) {
      check("THE HOST LOGGED NO OBSERVATION (not negotiated)", hostObserve.empty() && !ignoredLine,
            std::to_string(hostObserve.size()) + " lines");
    } else {
      check("THE HOST LOGGED THE VIEWER'S OBSERVATIONS", hostObserve.size() >= 40,
            std::to_string(hostObserve.size()) + " [bwe-observe] lines");
      bool sawProbe = false;
      const std::string probeText = "seq=" + std::to_string(probeSeq.load()) + " ";
      for (const auto& l : hostObserve) {
        if (probeSeq.load() != 0 && l.find(probeText) != std::string::npos) sawProbe = true;
      }
      check("...including the one sent with a nonsense viewer clock: freshness is the host's own",
            sawProbe && !ignoredLine, "probe seq=" + std::to_string(probeSeq.load()));
    }
  }

  // The table, for the report: host lines, then the truth, then the ABR/pacing lines to compare.
  std::cout << "\n--- host [bwe-observe] (" << hostObserve.size() << " lines) ---\n";
  for (const auto& l : hostObserve) std::cout << "  " << l << "\n";
  std::cout << "\n--- host rate/pacing lines (" << hostAbr.size() << ") ---\n";
  for (const auto& l : hostAbr) std::cout << "  " << l << "\n";

  // The decision signature the on/off regression compares: what the rate and pacing code DECIDED,
  // without the timestamps and counters that differ between any two runs. Decision lines verbatim
  // after the timestamp; from the periodic stat lines, only the distinct values of the rate/pacing
  // fields.
  {
    std::set<std::string> sig;
    for (const auto& l : hostAbr) {
      const size_t tag = l.find("[native-video-host]");
      const std::string body = tag == std::string::npos ? l : l.substr(tag);
      if (body.find("rate-control") != std::string::npos ||
          body.find("runtime-config-applied") != std::string::npos) {
        sig.insert("line: " + body);
      }
      std::istringstream words(body);
      std::string w;
      while (words >> w) {
        const size_t eq = w.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = w.substr(0, eq);
        if (key == "bitrateTarget" || key.rfind("pace", 0) == 0 || key.find("Pace") != std::string::npos) {
          sig.insert("value: " + w);
        }
      }
    }
    for (const auto& s : sig) std::cout << "[abr-signature] " << s << "\n";
  }

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

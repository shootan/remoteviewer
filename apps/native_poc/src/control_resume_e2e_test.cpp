// A real host, a real control channel, and a break that only the control traffic feels. (item 8, C3)
//
// Everything before this measured the two halves separately: the host's decision without a viewer,
// the viewer's state machine without a host. What neither could show is the thing the feature is:
// a session whose control channel dies while its video keeps arriving, repaired in place, with the
// same session, the same NAT mapping, and no reconnect.
//
// So this runs the real GNLinkStream and puts a proxy between it and a viewer built out of the
// product's own pieces -- udp_hello_handshake, UdpControlChannel, ViewerControlResume, and the
// recovery sequence itself (ViewerControlResume::Pump). The proxy drops CONTROL datagrams only:
// video, Hello and the resume exchange itself go through untouched, so the liveness that starts a
// recovery is the real one (the channel's retransmits running out) rather than a flag this test
// set. Nothing is forced.
//
// How the result is judged. Not by "the host said ackSent=1" -- that is the host agreeing with
// itself. The host is started with input injection pointed at a window THIS PROCESS owns
// (--input-target-pid), which restricts inject_background_input_event to PostMessage into that
// window and excludes every SendInput path by construction. So a mouse event sent by the viewer
// after the repair has to arrive in this test's own message queue to count. Nothing is injected
// into the user's desktop, and nothing could be: the explicit-target branch never reaches
// SendInput (host_input_inject.cpp:586-598).
//
// Isolation, as in the sibling host e2e tests: a scratch directory, ports nobody else is on, a
// GNLinkCapture.exe of this test's choosing, a job object that takes the host when this process
// goes, and off entirely unless REMOTE60_ALLOW_HOST_E2E=1.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "control_resume.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "viewer_control_resume.hpp"
#include "viewer_recv_liveness.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

bool host_e2e_allowed() {
  wchar_t value[8]{};
  const DWORD n = GetEnvironmentVariableW(L"REMOTE60_ALLOW_HOST_E2E", value, 8);
  return n > 0 && value[0] == L'1';
}

bool wait_until(const std::function<bool()>& done, int budgetMs) {
  const DWORD deadline = GetTickCount() + static_cast<DWORD>(budgetMs);
  while (GetTickCount() < deadline) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return done();
}

std::wstring self_path() {
  std::wstring path(32768, L'\0');
  const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  path.resize(n);
  return path;
}

std::wstring directory_of(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
}

// ---------------------------------------------------------------------------- the transport seam

/** Which way a datagram was going. The two directions are dropped independently. */
enum class Dir { ViewerToHost, HostToViewer };

/** What a datagram is, as far as this proxy cares. */
enum class Kindness { Control, Resume, ResumeAck, Other };

Kindness classify(const uint8_t* bytes, size_t len) {
  if (len < sizeof(uint32_t) + sizeof(uint16_t)) return Kindness::Other;
  uint32_t magic = 0;
  uint16_t kind = 0;
  std::memcpy(&magic, bytes, sizeof(magic));
  std::memcpy(&kind, bytes + sizeof(magic), sizeof(kind));
  if (magic != kMagic) return Kindness::Other;
  switch (static_cast<UdpPacketKind>(kind)) {
    case UdpPacketKind::ControlData:
    case UdpPacketKind::ControlAck:
    case UdpPacketKind::ControlNack:
      return Kindness::Control;
    case UdpPacketKind::ControlResume:
      return Kindness::Resume;
    case UdpPacketKind::ControlResumeAck:
      return Kindness::ResumeAck;
    default:
      return Kindness::Other;
  }
}

/**
 * A UDP relay with a switch on the control traffic.
 *
 * One socket, because the host answers whatever address it heard from: the viewer's datagrams
 * arrive from the viewer and everything else is the host. That also means the host binds its
 * session to THIS proxy's endpoint, which is stable for the whole run -- exactly what the resume
 * path requires of a peer.
 */
class ControlProxy {
 public:
  std::atomic<bool> dropControlUp{false};    // viewer -> host
  std::atomic<bool> dropControlDown{false};  // host -> viewer
  std::atomic<int> dropNextResumeAcks{0};    // swallow the host's answer N times
  std::atomic<int> duplicateResumeAcks{0};   // ...or send it twice
  std::atomic<uint64_t> controlDropped{0};
  std::atomic<uint64_t> mediaPassed{0};
  std::atomic<uint64_t> resumeAsksPassed{0};
  std::atomic<uint64_t> resumeAcksPassed{0};
  std::atomic<uint64_t> otherHostPorts{0};  // host datagrams from a socket other than bind-port
  // Every control datagram seen going down, kept so a pre-break one can be replayed afterwards.
  std::mutex capturedMu;
  std::vector<std::vector<uint8_t>> capturedDown;
  std::atomic<bool> capturing{false};

  bool Start(uint16_t listenPort, uint16_t hostPort) {
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCKET) return false;
    sockaddr_in bind_{};
    bind_.sin_family = AF_INET;
    bind_.sin_port = htons(listenPort);
    InetPtonW(AF_INET, L"127.0.0.1", &bind_.sin_addr);
    if (bind(sock_, reinterpret_cast<const sockaddr*>(&bind_), sizeof(bind_)) != 0) return false;
    DWORD timeout = 50;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));
    int buf = 1 << 20;
    setsockopt(sock_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buf), sizeof(buf));
    host_.sin_family = AF_INET;
    host_.sin_port = htons(hostPort);
    InetPtonW(AF_INET, L"127.0.0.1", &host_.sin_addr);
    worker_ = std::thread([this] { Run(); });
    return true;
  }

  void Stop() {
    stop_.store(true);
    if (worker_.joinable()) worker_.join();
    if (sock_ != INVALID_SOCKET) closesocket(sock_);
    sock_ = INVALID_SOCKET;
  }

  /**
   * Delivers the host's last resume answer a second time, late.
   *
   * Duplicating it inline does not reach the case worth checking: both copies arrive before
   * the worker has applied either, so the second is just another valid answer. The one that
   * matters is a copy that turns up AFTER the re-key -- which is what the host does when it
   * thinks its answer was lost, and what must cost nothing.
   */
  bool ReplayLastResumeAck() {
    std::lock_guard<std::mutex> lock(capturedMu);
    if (lastResumeAck_.empty()) return false;
    return sendto(sock_, reinterpret_cast<const char*>(lastResumeAck_.data()),
                  static_cast<int>(lastResumeAck_.size()), 0,
                  reinterpret_cast<const sockaddr*>(&viewer_), viewerLen_) > 0;
  }

  /** Replays control datagrams captured before a break, as a delayed delivery would. */
  void ReplayCapturedDown(SOCKET viewerSock) {
    std::vector<std::vector<uint8_t>> copy;
    {
      std::lock_guard<std::mutex> lock(capturedMu);
      copy = capturedDown;
    }
    for (const auto& d : copy) {
      (void)sendto(sock_, reinterpret_cast<const char*>(d.data()), static_cast<int>(d.size()), 0,
                   reinterpret_cast<const sockaddr*>(&viewer_), viewerLen_);
    }
    (void)viewerSock;
  }

 private:
  void Run() {
    std::vector<uint8_t> buf(2048);
    while (!stop_.load()) {
      sockaddr_in from{};
      int fromLen = sizeof(from);
      const int n = recvfrom(sock_, reinterpret_cast<char*>(buf.data()),
                             static_cast<int>(buf.size()), 0,
                             reinterpret_cast<sockaddr*>(&from), &fromLen);
      if (n <= 0) continue;
      // The viewer is whoever spoke first, and stays that address.
      //
      // It used to be decided the other way round -- anything not from the host's bind port
      // was taken for the viewer, and viewer_ was reassigned to it. The host owns more than
      // one socket, so that was a way for a frame to be sent back where it came from while
      // this proxy counted it as forwarded. The counter below says it has not happened in the
      // runs since, so it is NOT offered as the explanation for the earlier stall; it is
      // simply the correct rule, and now there is a number that would show it if it did.
      if (!haveViewer_) {
        viewer_ = from;
        viewerLen_ = fromLen;
        haveViewer_ = true;
      }
      const bool fromViewer = from.sin_addr.s_addr == viewer_.sin_addr.s_addr &&
                              from.sin_port == viewer_.sin_port;
      if (!fromViewer && from.sin_port != host_.sin_port) {
        otherHostPorts.fetch_add(1);  // counted, forwarded as the host: worth knowing about
      }
      const Dir dir = fromViewer ? Dir::ViewerToHost : Dir::HostToViewer;

      const Kindness what = classify(buf.data(), static_cast<size_t>(n));
      if (what == Kindness::Control) {
        if (dir == Dir::HostToViewer && capturing.load()) {
          std::lock_guard<std::mutex> lock(capturedMu);
          if (capturedDown.size() < 64) {
            capturedDown.emplace_back(buf.begin(), buf.begin() + n);
          }
        }
        const bool drop = (dir == Dir::ViewerToHost) ? dropControlUp.load() : dropControlDown.load();
        if (drop) {
          controlDropped.fetch_add(1);
          continue;
        }
      } else if (what == Kindness::ResumeAck) {
        {
          std::lock_guard<std::mutex> lock(capturedMu);
          lastResumeAck_.assign(buf.begin(), buf.begin() + n);
        }
        int remaining = dropNextResumeAcks.load();
        while (remaining > 0 &&
               !dropNextResumeAcks.compare_exchange_weak(remaining, remaining - 1)) {
        }
        if (remaining > 0) continue;  // the answer is lost, exactly as it would be on the wire
        resumeAcksPassed.fetch_add(1);
      } else if (what == Kindness::Resume) {
        resumeAsksPassed.fetch_add(1);
      } else {
        mediaPassed.fetch_add(1);
      }

      const sockaddr_in& to = (dir == Dir::ViewerToHost) ? host_ : viewer_;
      const int toLen = (dir == Dir::ViewerToHost) ? static_cast<int>(sizeof(host_)) : viewerLen_;
      (void)sendto(sock_, reinterpret_cast<const char*>(buf.data()), n, 0,
                   reinterpret_cast<const sockaddr*>(&to), toLen);
      if (what == Kindness::ResumeAck && duplicateResumeAcks.load() > 0) {
        duplicateResumeAcks.fetch_sub(1);
        (void)sendto(sock_, reinterpret_cast<const char*>(buf.data()), n, 0,
                     reinterpret_cast<const sockaddr*>(&to), toLen);
      }
    }
  }

  SOCKET sock_ = INVALID_SOCKET;
  sockaddr_in host_{};
  sockaddr_in viewer_{};
  int viewerLen_ = sizeof(sockaddr_in);
  std::vector<uint8_t> lastResumeAck_;  // guarded by capturedMu
  bool haveViewer_ = false;
  std::atomic<bool> stop_{false};
  std::thread worker_;
};

// ------------------------------------------------------------------------ the injection observer

/**
 * A window this process owns, for the host to inject into.
 *
 * Off screen and never activated, so nothing appears and nothing takes focus -- but a real
 * top-level window, because that is what the host's target resolver looks for. Every input the
 * host injects lands here as an ordinary message, which is the only evidence in this test that an
 * input actually reached the remote side.
 */
class InjectTarget {
 public:
  std::atomic<uint64_t> mouseMoves{0};
  std::atomic<uint64_t> keyDowns{0};
  std::atomic<uint64_t> keyUps{0};

  bool Start() {
    thread_ = std::thread([this] { Run(); });
    return wait_until([this] { return ready_.load(); }, 5000);
  }
  void Stop() {
    if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    if (thread_.joinable()) thread_.join();
  }

 private:
  static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<InjectTarget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
      if (msg == WM_MOUSEMOVE) self->mouseMoves.fetch_add(1);
      if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) self->keyDowns.fetch_add(1);
      if (msg == WM_KEYUP || msg == WM_SYSKEYUP) self->keyUps.fetch_add(1);
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
    wc.lpszClassName = L"Remote60C3InjectTarget";
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName,
                            L"remote60 c3 inject target", WS_OVERLAPPEDWINDOW, -4000, -4000, 320,
                            240, nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd_) {
      SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
      ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }
    ready_.store(true);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    hwnd_ = nullptr;
  }

  HWND hwnd_ = nullptr;
  std::atomic<bool> ready_{false};
  std::thread thread_;
};

// -------------------------------------------------------------------------------- the viewer side

/** The viewer's own pieces, wired the way viewer_startup wires them. */
struct ViewerSide {
  SOCKET sock = INVALID_SOCKET;
  UdpControlChannel control;
  ViewerControlResume resume;
  std::unique_ptr<UdpControlLink> link;
  std::thread ingress;
  std::atomic<bool> stop{false};
  std::atomic<uint64_t> videoDatagrams{0};
  std::atomic<uint64_t> lastVideoUs{0};
  std::atomic<uint64_t> unsolicited{0};  // control messages nobody asked for
  uint32_t nextId = 0;

  bool video_alive() const {
    const uint64_t last = lastVideoUs.load();
    if (last == 0) return false;
    const uint64_t now = qpc_now_us();
    return now < last + 5000000ull;  // the same 5 s the session watchdog uses
  }

  void StartIngress() {
    ingress = std::thread([this] {
      std::vector<uint8_t> buf(2048);
      while (!stop.load()) {
        control.Tick();
        const int n = recv(sock, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
        if (n <= 0) continue;
        // Exactly the viewer's order: the resume answer is about the channel, so it is looked at
        // before the channel is fed.
        if (resume.OnDatagram(buf.data(), static_cast<size_t>(n))) continue;
        if (control.OnPacket(buf.data(), static_cast<size_t>(n))) continue;
        videoDatagrams.fetch_add(1);
        lastVideoUs.store(qpc_now_us());
      }
    });
  }

  void StopIngress() {
    stop.store(true);
    if (ingress.joinable()) ingress.join();
  }
};

bool ping(ControlLink& link, uint64_t* elapsedUs = nullptr) {
  ControlOutboundAction action;
  action.kind = ControlOutboundActionKind::Ping;
  action.ping.header.magic = kMagic;
  action.ping.header.type = static_cast<uint16_t>(MessageType::ControlPing);
  action.ping.header.size = static_cast<uint16_t>(sizeof(action.ping));
  action.ping.clientSendQpcUs = qpc_now_us();
  action.expectedResponseType = MessageType::ControlPong;
  action.expectedResponseSize = static_cast<uint16_t>(sizeof(TcpControlResponse{}.pong));
  TcpControlResponse response;
  const uint64_t start = qpc_now_us();
  const bool ok = execute_control_action(link, action, &response) &&
                  response.kind == TcpControlResponseKind::Pong;
  if (elapsedUs) *elapsedUs = qpc_now_us() - start;
  return ok;
}

bool send_input(ControlLink& link, uint16_t kind, uint32_t seq, int32_t x, int32_t y,
                uint32_t keyCode) {
  ControlOutboundAction action;
  action.kind = ControlOutboundActionKind::InputEvent;
  action.inputEvent.header.magic = kMagic;
  action.inputEvent.header.type = static_cast<uint16_t>(MessageType::ControlInputEvent);
  action.inputEvent.header.size = static_cast<uint16_t>(sizeof(action.inputEvent));
  action.inputEvent.seq = seq;
  action.inputEvent.kind = kind;
  action.inputEvent.x = x;
  action.inputEvent.y = y;
  action.inputEvent.keyCode = keyCode;
  action.inputEvent.clientSendQpcUs = qpc_now_us();
  action.expectedResponseType = MessageType::ControlInputAck;
  action.expectedResponseSize = static_cast<uint16_t>(sizeof(TcpControlResponse{}.inputAck));
  TcpControlResponse response;
  return execute_control_action(link, action, &response) &&
         response.kind == TcpControlResponseKind::InputAck;
}

ControlProxy* gProxy = nullptr;  // for the progress line only: which side of the seam went quiet

/** One recovery, driven the way the viewer's worker drives it, with the timings kept apart. */
struct RecoveryResult {
  bool resumed = false;
  bool gaveUp = false;
  bool cancelled = false;
  uint64_t breakToFirstSendUs = 0;
  uint64_t breakToRunningUs = 0;
  uint32_t attempts = 0;
};

RecoveryResult run_recovery(ViewerSide& v, int budgetMs,
                            const std::function<bool()>& cancelled = {}) {
  RecoveryResult out;
  ResumePumpHooks hooks;
  hooks.cancelled = cancelled ? cancelled : std::function<bool()>([] { return false; });
  hooks.nowUs = [] { return qpc_now_us(); };
  hooks.videoAlive = [&v] { return v.video_alive(); };
  hooks.proveChannel = [&v] {
    v.link = std::make_unique<UdpControlLink>(&v.control, 12000);
    return ping(*v.link);
  };
  hooks.onResumed = [&out](uint64_t toFirstSend, uint64_t toRunning) {
    out.breakToFirstSendUs = toFirstSend;
    out.breakToRunningUs = toRunning;
  };
  hooks.idle = [] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); };

  const DWORD deadline = GetTickCount() + static_cast<DWORD>(budgetMs);
  DWORD nextReport = GetTickCount();
  while (GetTickCount() < deadline) {
    if (GetTickCount() >= nextReport) {
      // Printed while it runs, because a recovery that stalls and one that is merely slow
      // look identical in a final verdict. videoAge is the one that decides whether it asks
      // at all.
      nextReport = GetTickCount() + 2000;
      const uint64_t last = v.lastVideoUs.load();
      const uint64_t age = last ? (qpc_now_us() - last) : 0;
      std::cout << "    ...attempts=" << v.resume.attempts()
                << " media=" << v.videoDatagrams.load()
                << " proxyMedia=" << (gProxy ? gProxy->mediaPassed.load() : 0)
                << " videoAgeMs=" << (last ? age / 1000 : 0)
                << " videoAlive=" << (v.video_alive() ? 1 : 0)
                << " refused=" << v.resume.refused_acks()
                << " ignored=" << v.resume.ignored_acks()
                << " rekeyed=" << (v.resume.rekeyed() ? 1 : 0) << "\n";
    }
    const ResumePumpResult r = v.resume.Pump(hooks);
    if (r == ResumePumpResult::Resumed) {
      out.resumed = true;
      break;
    }
    if (r == ResumePumpResult::GaveUp) {
      out.gaveUp = true;
      break;
    }
    if (r == ResumePumpResult::Cancelled) {
      out.cancelled = true;
      break;
    }
  }
  out.attempts = v.resume.attempts();
  return out;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::cout.setf(std::ios::unitbuf);

  if (!host_e2e_allowed()) {
    std::printf("SKIP  control_resume_e2e_test (starts a listening host)\n");
    std::printf("      This test starts a real GNLinkStream, which listens, which makes Windows\n");
    std::printf("      ask about the firewall -- and an unanswered prompt becomes a Block rule on\n");
    std::printf("      the user's machine. Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n");
    std::printf("\nRESULT: SKIPPED\n");
    return 0;
  }
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--thumbnail") {  // staged as GNLinkCapture.exe; answers nothing
      Sleep(300000);
      return 0;
    }
  }

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

  const uint16_t hostPort = 44790;
  const uint16_t proxyPort = 44791;

  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const std::wstring dir = std::wstring(temp) + L"remote60_c3_resume_" +
                           std::to_wstring(GetCurrentProcessId()) + L"\\";
  CreateDirectoryW(dir.substr(0, dir.size() - 1).c_str(), nullptr);

  const std::wstring me = self_path();
  const std::wstring myDir = directory_of(me);
  const bool staged =
      CopyFileW((myDir + L"GNLinkStream.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) &&
      CopyFileW(me.c_str(), (dir + L"GNLinkCapture.exe").c_str(), FALSE);
  check("a host and a never-answering helper could be staged", staged);

  InjectTarget target;
  check("a window of this process is up for the host to inject into", target.Start());

  ControlProxy proxy;
  gProxy = &proxy;
  check("the control seam is listening between the viewer and the host",
        proxy.Start(proxyPort, hostPort));

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
    // Keep frames coming on a desktop nobody is touching.
    //
    // This is not papering over a failure -- it is the difference between the situation
    // the feature exists for and a different one. A resume is asked for only while video
    // is still arriving, because control gone AND video gone is a session that is ending,
    // and the host quite correctly sends nothing at all when the screen has not changed.
    // An unattended run has a perfectly static screen, so without this the test would be
    // measuring the no-video case and calling it the video case. Two env knobs the host
    // already has, set on the child only; neither touches the resume path, and nothing is
    // drawn on the user's screen to produce the change artificially.
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_FRAME_GATING_DISABLE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_STATIC_SCENE_FPS", L"15");
    // --input-target-pid is what keeps this safe as well as observable: with an explicit target,
    // inject_background_input_event resolves a window of THIS process and PostMessages into it,
    // and every SendInput path is behind the desktop-mode branch it no longer takes.
    std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                       L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(hostPort) +
                       L" --seconds 300 --enable-input-injection" +
                       L" --input-injection-mode background_message" + L" --input-target-pid " +
                       std::to_wstring(GetCurrentProcessId());
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    // The host's own account of what it did, kept so the timeline can be read rather than
    // guessed at. When a repair does not complete, the question is always whether the host
    // refused, answered, or had already stopped serving -- and only its log says which.
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
      si.hStdInput = nullptr;
    }
    launched = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr,
                              hostLog != INVALID_HANDLE_VALUE, 
                              CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, dir.c_str(), &si,
                              &hostPi) != 0;
    if (launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  check("the host started", launched);

  ViewerSide viewer;
  bool connected = false;
  bool repaired = false;  // case 1 got the channel back; the later cases need that
  // At this scope on purpose: the resume component holds a reference to it for the whole
  // run, and the first version declared it inside the connect block -- a dangling
  // reference the moment the block ended.
  uint64_t generation = 1;

  if (launched) {
    viewer.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in proxyAddr{};
    proxyAddr.sin_family = AF_INET;
    proxyAddr.sin_port = htons(proxyPort);
    InetPtonW(AF_INET, L"127.0.0.1", &proxyAddr.sin_addr);
    connect(viewer.sock, reinterpret_cast<const sockaddr*>(&proxyAddr), sizeof(proxyAddr));
    DWORD rcvTimeout = 50;
    setsockopt(viewer.sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout),
               sizeof(rcvTimeout));
    int rcvBuf = 1 << 20;
    setsockopt(viewer.sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf),
               sizeof(rcvBuf));

    UdpHelloOptions hello;
    hello.budgetMs = 20000;
    hello.sliceMaxMs = 250;
    hello.retrySleepMs = 50;
    hello.requestControlResume = true;  // the viewer's own option, as viewer_udp_session sets it
    std::string helloError;
    uint32_t ackFeatures = 0;
    const bool handshake =
        udp_hello_handshake(viewer.sock, hello, nullptr, &helloError, &ackFeatures, nullptr);
    check("the viewer's Hello is answered through the seam", handshake, helloError);
    check("...and the host offers control resume",
          (ackFeatures & kUdpFeatureControlResume) != 0,
          "features=" + std::to_string(ackFeatures) + " resumeBit=" +
              std::to_string(kUdpFeatureControlResume));

    viewer.control.Configure(
        [&viewer](const void* data, size_t len) {
          return send(viewer.sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, 1200);
    viewer.StartIngress();

    sockaddr_in peer{};
    int peerLen = sizeof(peer);
    getpeername(viewer.sock, reinterpret_cast<sockaddr*>(&peer), &peerLen);
    ViewerControlResume::Config cfg;
    cfg.decide.giveUpAfterUs = viewer::SessionLivenessConfig{}.controlGoneWithVideoUs;
    viewer.nextId = 0x5100;
    viewer.resume.Configure(
        &viewer.control,
        [&viewer](const void* data, size_t len) {
          return send(viewer.sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        [](const std::string& line) { std::cout << "    " << line << "\n"; },
        [&viewer]() { return ++viewer.nextId; }, [&generation]() { return generation; }, cfg,
        (ackFeatures & kUdpFeatureControlResume) != 0, peer.sin_addr.s_addr, peer.sin_port);

    // The product's own read timeout (viewer_constants.hpp kUdpControlReadTimeoutMs). It
    // has to be LONGER than the channel's retransmit budget, or a failed exchange means
    // "the link was slow" rather than "the channel gave up" -- and the first run of this
    // test used 5 s, which is under the ~6 s budget, so the break it measured was its own
    // impatience.
    viewer.link = std::make_unique<UdpControlLink>(&viewer.control, 12000);
    connected = wait_until([&] { return ping(*viewer.link); }, 20000);
    check("control answers before anything is broken", connected);
    const bool videoUp = wait_until([&] { return viewer.video_alive(); }, 20000);
    check("...and video is arriving", videoUp,
          std::to_string(viewer.videoDatagrams.load()) + " media datagrams");

    if (connected) {
      // The baseline for the judgement at the end: an input sent now reaches this process.
      const uint64_t movesBefore = target.mouseMoves.load();
      const bool acked = send_input(*viewer.link, /*mouse_move=*/1, 1, 40, 30, 0);
      const bool landed = wait_until([&] { return target.mouseMoves.load() > movesBefore; }, 5000);
      check("an input sent before the break is acknowledged", acked);
      check("...AND ARRIVES IN THIS PROCESS'S WINDOW", landed,
            "the host injected it; ackSent alone would not have shown that");
      if (!landed) {
        std::cout << "    (no injection observed -- the remaining input judgements will say so)\n";
      }
    }
  }

  // ----------------------------------------------------------------- the break, and the repair
  if (connected) {
    std::cout << "\n--- case 1: control is cut in both directions, video keeps flowing ---\n";
    proxy.capturing.store(true);
    const uint64_t mediaBefore = proxy.mediaPassed.load();
    proxy.dropControlUp.store(true);
    proxy.dropControlDown.store(true);

    // The real liveness: keep asking until the channel's own retransmits run out. Nothing here
    // tells the viewer the link is dead -- it finds out.
    const uint64_t breakStartUs = qpc_now_us();
    bool noticed = false;
    while (qpc_now_us() - breakStartUs < 20000000ull) {
      if (!ping(*viewer.link)) {
        noticed = true;
        break;
      }
    }
    const uint64_t noticedUs = qpc_now_us() - breakStartUs;
    check("the viewer's control channel declares itself dead on its own",
          noticed && viewer.control.IsClosed(),
          std::to_string(noticedUs / 1000) + "ms, reason=" +
              std::string(to_string(viewer.control.CloseReason())));
    check("...while video kept arriving through the same socket",
          proxy.mediaPassed.load() > mediaBefore && viewer.video_alive(),
          std::to_string(proxy.mediaPassed.load() - mediaBefore) + " media datagrams during it");

    const uint64_t asksBefore = proxy.resumeAsksPassed.load();
    check("a recovery starts", viewer.resume.BeginBreakIfPossible(qpc_now_us(),
                                                                  viewer.video_alive(), false));
    // The uplink comes back. Control datagrams flow again; the session never went away.
    proxy.dropControlUp.store(false);
    proxy.dropControlDown.store(false);
    const RecoveryResult r = run_recovery(viewer, 28000);
    repaired = r.resumed;
    check("THE CONTROL CHANNEL COMES BACK WITHOUT A RECONNECT", r.resumed,
          r.resumed ? (std::to_string(r.attempts) + " asks")
                    : (r.gaveUp ? "gave up" : (r.cancelled ? "cancelled" : "timed out")));
    check("...and the asks went out over the media socket",
          proxy.resumeAsksPassed.load() > asksBefore,
          std::to_string(proxy.resumeAsksPassed.load() - asksBefore) + " resume datagrams");
    std::cout << "    timing: breakToFirstSend=" << (r.breakToFirstSendUs / 1000)
              << "ms  breakToRunning=" << (r.breakToRunningUs / 1000) << "ms  attempts="
              << r.attempts << "\n";
    check("...inside the thirty second budget, which did not move",
          r.resumed && r.breakToRunningUs < 30000000ull,
          std::to_string(r.breakToRunningUs / 1000) + "ms of 30000ms");
    check("...and the first ask went out long before the repair finished",
          r.resumed && r.breakToFirstSendUs <= r.breakToRunningUs,
          "the host is inside Serve() until its own read timeout, so the wait is the host's");

    if (r.resumed) {
      // Three real round trips, on the channel that was just re-keyed.
      int ok = 0;
      for (int i = 0; i < 3; ++i) {
        uint64_t us = 0;
        if (ping(*viewer.link, &us)) ++ok;
      }
      check("THREE REAL ROUND TRIPS ON THE RESUMED CHANNEL", ok == 3,
            std::to_string(ok) + "/3");

      // And the judgement that matters: a remote input, after the repair, observed arriving.
      const uint64_t movesBefore = target.mouseMoves.load();
      const bool acked = send_input(*viewer.link, 1, 2, 60, 45, 0);
      const bool landed = wait_until([&] { return target.mouseMoves.load() > movesBefore; }, 5000);
      check("input works again after the repair (host acknowledged)", acked);
      check("...AND THE HOST ACTUALLY INJECTED IT", landed,
            "judged by this process's own message queue, not by ackSent");

      // A key-up for a key the host believes is held: the release-all contract's payload, over
      // the resumed channel. (The viewer-side trigger for release-all is the window message the
      // worker posts; that wiring is not exercised here -- see the report.)
      const uint64_t upsBefore = target.keyUps.load();
      (void)send_input(*viewer.link, /*key_up=*/6, 3, 0, 0, 'A');
      check("a key-up sent after the repair reaches the host's injection",
            wait_until([&] { return target.keyUps.load() > upsBefore; }, 5000),
            "this is what stops a modifier stranding when the break swallowed the real one");

      // Nothing that was in flight when control broke comes back to haunt the new stream.
      proxy.ReplayCapturedDown(viewer.sock);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      uint64_t us = 0;
      check("A REPLAY OF PRE-BREAK CONTROL DATAGRAMS DOES NOT DISTURB THE RESUMED CHANNEL",
            ping(*viewer.link, &us),
            "the re-key gave it stream ids those datagrams do not carry");
    }
    proxy.capturing.store(false);
  }

  // ------------------------------------------------------------------ the answer itself is lost
  //
  // Each of the remaining cases needs a working channel to break again, so they are skipped
  // rather than run when case 1 did not get one. A stall reported once is a finding; the same
  // stall reported eight more times under other names is noise that hides it.
  if (connected && !repaired) {
    std::cout << "\nSKIP  cases 2-4: the channel was not repaired in case 1, so there is "
                 "nothing to break again.\n";
  }
  if (connected && repaired) {
    std::cout << "\n--- case 2: the host's answer is lost, then duplicated ---\n";
    proxy.dropControlUp.store(true);
    proxy.dropControlDown.store(true);
    const uint64_t breakStartUs = qpc_now_us();
    while (qpc_now_us() - breakStartUs < 20000000ull) {
      if (!ping(*viewer.link)) break;
    }
    const uint32_t idBefore = viewer.resume.resume_id();
    check("a second recovery starts",
          viewer.resume.BeginBreakIfPossible(qpc_now_us(), viewer.video_alive(), false));
    check("...with a DIFFERENT id from the first",
          viewer.resume.resume_id() != idBefore,
          std::to_string(idBefore) + " then " + std::to_string(viewer.resume.resume_id()));
    proxy.dropControlUp.store(false);
    proxy.dropControlDown.store(false);
    proxy.dropNextResumeAcks.store(2);   // the first two answers never arrive
    proxy.duplicateResumeAcks.store(2);  // and the next ones arrive twice

    const RecoveryResult r = run_recovery(viewer, 28000);
    check("THE REPAIR SURVIVES ITS ANSWER BEING LOST TWICE", r.resumed,
          r.resumed ? (std::to_string(r.attempts) + " asks")
                    : (r.gaveUp ? "gave up" : (r.cancelled ? "cancelled" : "timed out")));
    std::cout << "    timing: breakToFirstSend=" << (r.breakToFirstSendUs / 1000)
              << "ms  breakToRunning=" << (r.breakToRunningUs / 1000) << "ms  attempts="
              << r.attempts << "\n";
    check("...it took more than one ask to get there", r.attempts > 1,
          std::to_string(r.attempts) + " asks");
    check("...and both of the lost answers really were dropped, not merely scheduled to be",
          proxy.dropNextResumeAcks.load() == 0,
          std::to_string(proxy.dropNextResumeAcks.load()) + " drops left unused");
    if (r.resumed) {
      // The duplicate that matters: the host's answer arriving again after the viewer has
      // already acted on it. Inline duplication cannot produce this -- both copies land before
      // the worker applies either -- so it is delivered here, late, on purpose.
      const uint64_t ignoredBefore = viewer.resume.ignored_acks();
      check("the host's answer can be delivered again, late", proxy.ReplayLastResumeAck());
      // It arrives after the recovery has been declared over, so the reason it is refused is
      // "no attempt in flight" rather than "duplicate" -- the duplicate-during-recovery path
      // is the pure test's. Either way the requirement is the same and it is checked here on
      // the real channel: nothing is applied, and the link is untouched.
      const bool refused =
          wait_until([&] { return viewer.resume.ignored_acks() > ignoredBefore; }, 3000);
      check("A LATE DUPLICATE OF THE ANSWER IS REFUSED, NOT REAPPLIED", refused,
            std::string("last reason=") + viewer.resume.last_reason());
      check("...and it did not start anything", !viewer.resume.attempt_in_flight());
      int ok = 0;
      for (int i = 0; i < 3; ++i) {
        if (ping(*viewer.link)) ++ok;
      }
      check("...and the channel round trips three times afterwards, undisturbed", ok == 3,
            std::to_string(ok) + "/3");
    }
    proxy.dropNextResumeAcks.store(0);
    proxy.duplicateResumeAcks.store(0);
  }

  // ------------------------------------------------------------ one direction only, and a cancel
  if (connected && repaired) {
    std::cout << "\n--- case 3: only the viewer's uplink control is cut, then the session is "
                 "called off mid-recovery ---\n";
    proxy.dropControlUp.store(true);  // the host can still be heard; it cannot hear the viewer
    const uint64_t breakStartUs = qpc_now_us();
    while (qpc_now_us() - breakStartUs < 20000000ull) {
      if (!ping(*viewer.link)) break;
    }
    check("a one-way control cut is noticed too", viewer.control.IsClosed(),
          std::string("reason=") + to_string(viewer.control.CloseReason()));
    check("...with video still arriving the whole time", viewer.video_alive());
    check("a recovery starts",
          viewer.resume.BeginBreakIfPossible(qpc_now_us(), viewer.video_alive(), false));

    // Cancel after the first ask has gone out. The uplink is still cut, so nothing can succeed;
    // what is being checked is that cancellation ends it rather than the ceiling.
    std::atomic<bool> cancel{false};
    std::thread canceller([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));
      cancel.store(true);
    });
    const uint64_t startUs = qpc_now_us();
    const RecoveryResult r = run_recovery(viewer, 28000, [&] { return cancel.load(); });
    const uint64_t tookUs = qpc_now_us() - startUs;
    canceller.join();
    check("CANCELLATION ENDS THE RECOVERY, not the ceiling", r.cancelled && !r.gaveUp,
          std::to_string(tookUs / 1000) + "ms (the ceiling is 30000ms)");
    check("...promptly", r.cancelled && tookUs < 10000000ull,
          std::to_string(tookUs / 1000) + "ms");
    proxy.dropControlUp.store(false);
  }

  // ------------------------------------------------- an answer for a session that has moved on
  if (connected && repaired) {
    std::cout << "\n--- case 4: the shell replaces this viewer while a recovery is running ---\n";
    // The generation is the viewer's own, read live by the component through the same hook the
    // product installs. Moving it is what the shell does when it calls this viewer off and
    // starts another -- and an answer that crosses that boundary is answering about a session
    // that no longer exists.
    proxy.dropControlUp.store(true);
    proxy.dropControlDown.store(true);
    const uint64_t breakStartUs = qpc_now_us();
    while (qpc_now_us() - breakStartUs < 20000000ull) {
      if (!ping(*viewer.link)) break;
    }
    const bool started =
        viewer.resume.BeginBreakIfPossible(qpc_now_us(), viewer.video_alive(), false);
    check("a recovery starts", started);
    proxy.dropControlUp.store(false);
    proxy.dropControlDown.store(false);
    ++generation;  // the shell started a different session
    const uint64_t ignoredBefore = viewer.resume.ignored_acks();
    const RecoveryResult r = run_recovery(viewer, 16000);
    check("A SESSION THAT HAS BEEN SUPERSEDED IS NOT REPAIRED", !r.resumed,
          "the host answers; the viewer does not act on it");
    check("...nothing was re-keyed", !viewer.resume.rekeyed());
    check("...and the answers that did arrive were refused for the generation",
          viewer.resume.ignored_acks() > ignoredBefore,
          std::to_string(viewer.resume.ignored_acks() - ignoredBefore) +
              " answers ignored, last reason=" + viewer.resume.last_reason());
    viewer.resume.EndSession();
  }
  viewer.StopIngress();
  if (viewer.sock != INVALID_SOCKET) closesocket(viewer.sock);
  proxy.Stop();
  target.Stop();

  // What the host said about its own control session, which is the other half of every
  // timing above. Printed unconditionally: a run that passed and a run that did not have to
  // be comparable afterwards.
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  CloseHandle(job);
  bool hostGone = true;
  if (hostPi.hProcess) {
    hostGone = WaitForSingleObject(hostPi.hProcess, 20000) == WAIT_OBJECT_0;
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);
  check("the host is gone when the job closes", hostGone);
  std::cout << "    seam: media=" << proxy.mediaPassed.load()
            << " controlDropped=" << proxy.controlDropped.load()
            << " resumeAsks=" << proxy.resumeAsksPassed.load()
            << " resumeAcks=" << proxy.resumeAcksPassed.load()
            << " fromOtherHostPorts=" << proxy.otherHostPorts.load() << "\n";

  {
    std::cout << "\n--- what the host said about its control session ---\n";
    FILE* f = nullptr;
    if (_wfopen_s(&f, hostLogPath.c_str(), L"rb") == 0 && f) {
      char line[1024];
      while (std::fgets(line, sizeof(line), f)) {
        const std::string text(line);
        if (text.find("[control]") != std::string::npos ||
            text.find("resume") != std::string::npos ||
            text.find("stream") != std::string::npos) {
          std::cout << "    " << text;
        }
      }
      std::fclose(f);
    } else {
      std::cout << "    (the host log could not be read)\n";
    }
  }

  for (int i = 0; i < 40; ++i) {
    DeleteFileW((dir + L"host.log").c_str());
    DeleteFileW((dir + L"GNLinkStream.exe").c_str());
    DeleteFileW((dir + L"GNLinkCapture.exe").c_str());
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

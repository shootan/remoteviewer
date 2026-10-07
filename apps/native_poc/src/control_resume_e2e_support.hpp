#pragma once

// Shared scaffolding for the host end-to-end tests. (item 8, C3 r3)
//
// None of this is specific to the resume decision: a check counter, the switch that drops
// control datagrams between a viewer and a real host, and a window of this process for the
// host to inject into. A second test needs exactly these, and copying them would be the worse
// option twice -- two copies to keep honest, and a harness free to drift away from the one
// whose numbers were reported.
//
// Isolation, as in the sibling host e2e tests: a scratch directory, ports nobody else is on, a
// GNLinkCapture.exe of the test's choosing, a job object that takes the host when the test
// process goes, and off entirely unless REMOTE60_ALLOW_HOST_E2E=1.

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
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "e2e_isolation.hpp"
#include "poc_protocol.hpp"

namespace remote60::native_poc::e2e {

using remote60::native_poc::kMagic;
using remote60::native_poc::UdpPacketKind;

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  // One write for the whole line: the product's own threads log to the same stdout, and a line built
  // from several insertions can be split by them (a parent that counts lines then miscounts).
  std::string line = (ok ? "PASS  " : "FAIL  ") + name;
  if (!detail.empty()) line += "  " + detail;
  line += "\n";
  std::cout.write(line.data(), static_cast<std::streamsize>(line.size()));
  std::cout.flush();
}

// Checks this run could not judge either way. They are not passes: the summary line says how
// many there were, so a run that judged nothing cannot read as ALL PASS. (RV-16)
int gSkips = 0;

void skip(const std::string& name, const std::string& why) {
  ++gSkips;
  std::cout << "SKIP  " << name << "  NOT JUDGED: " << why << "\n";
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
  std::atomic<bool> dropAllUp{false};        // EVERYTHING viewer -> host (C4 range)
  std::atomic<bool> dropAllDown{false};      // EVERYTHING host -> viewer, video included
  std::atomic<uint64_t> allDropped{0};
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

  /** Sends the viewer's last resume ask to the host again, as a delayed copy would arrive. */
  bool ReplayLastResumeAsk() {
    std::lock_guard<std::mutex> lock(capturedMu);
    if (lastResumeAsk_.empty()) return false;
    return sendto(sock_, reinterpret_cast<const char*>(lastResumeAsk_.data()),
                  static_cast<int>(lastResumeAsk_.size()), 0,
                  reinterpret_cast<const sockaddr*>(&host_), sizeof(host_)) > 0;
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

      // A whole direction cut: video, control, resume asks and answers alike. (RV-02, C4 range)
      if ((dir == Dir::ViewerToHost && dropAllUp.load()) ||
          (dir == Dir::HostToViewer && dropAllDown.load())) {
        allDropped.fetch_add(1);
        continue;
      }
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
        {
          std::lock_guard<std::mutex> lock(capturedMu);
          lastResumeAsk_.assign(buf.begin(), buf.begin() + n);
        }
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
  std::vector<uint8_t> lastResumeAsk_;  // guarded by capturedMu
  bool haveViewer_ = false;
  std::atomic<bool> stop_{false};
  std::thread worker_;
};

// ------------------------------------------------------------------------ the injection observer

/**
 * A window this process owns, for the host to inject into -- and, when asked, to capture.
 *
 * Off screen and never activated, so nothing appears and nothing takes focus -- but a real
 * top-level window, because that is what the host's target resolver looks for. Every input the
 * host injects lands here as an ordinary message, which is the only evidence in this test that an
 * input actually reached the remote side.
 *
 * It also PAINTS, about twenty times a second, and that is not decoration. (C3 r5) A host
 * capturing a monitor that nobody is touching sends almost nothing -- roughly one frame every
 * two seconds -- and a gap like that can cover a whole repair, which made "video kept arriving"
 * fail about one run in nine for a reason that had nothing to do with the product. Pointing the
 * host at THIS window instead (--capture-window-title) and keeping it changing gives a real
 * frame cadence without drawing anything on the user's screen, which is what putting a moving
 * pattern on the desktop would have done.
 */
class InjectTarget {
 public:
  // Unique to this process (e2e_unique_window_title), set before Start() returns: what the host
  // is told to capture / inject into, by pid and by this exact title, never another test's.
  std::wstring title;
  std::atomic<uint64_t> mouseMoves{0};
  std::atomic<uint64_t> keyDowns{0};
  std::atomic<uint64_t> keyUps{0};
  // Per virtual key, so one key's duplicates are not hidden by another key's ups. (RV-01)
  std::atomic<uint32_t> downsByVk[256]{};
  std::atomic<uint32_t> upsByVk[256]{};
  std::atomic<uint32_t> leftDowns{0};
  std::atomic<uint32_t> leftUps{0};
  std::atomic<uint32_t> rightDowns{0};
  std::atomic<uint32_t> rightUps{0};
  std::atomic<uint32_t> middleDowns{0};
  std::atomic<uint32_t> middleUps{0};
  // WM_XBUTTONDOWN / UP by the XBUTTON identifier in the wParam's HIWORD (slots 1 and 2; slot 0
  // is anything else), with the last wParam and client point of each so a test can read which
  // button the message named and the MK_* state it carried. (mouse-xbutton r1)
  std::atomic<uint32_t> xDowns[3]{};
  std::atomic<uint32_t> xUps[3]{};
  std::atomic<uint32_t> lastXDownWparam{0};
  std::atomic<uint32_t> lastXUpWparam{0};
  std::atomic<int32_t> lastXDownX{0};
  std::atomic<int32_t> lastXDownY{0};
  std::atomic<bool> paused{false};  // stop repainting: the captured window goes still

  bool Start() {
    thread_ = std::thread([this] { Run(); });
    return wait_until([this] { return ready_.load(); }, 5000);
  }
  void Stop() {
    if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    if (thread_.joinable()) thread_.join();
  }
  HWND hwnd() const { return hwnd_; }

 private:
  static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<InjectTarget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
      if (msg == WM_MOUSEMOVE) self->mouseMoves.fetch_add(1);
      if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) self->keyDowns.fetch_add(1);
      if (msg == WM_KEYUP || msg == WM_SYSKEYUP) self->keyUps.fetch_add(1);
      if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) self->downsByVk[wp & 0xff].fetch_add(1);
      if (msg == WM_KEYUP || msg == WM_SYSKEYUP) self->upsByVk[wp & 0xff].fetch_add(1);
      if (msg == WM_LBUTTONDOWN) self->leftDowns.fetch_add(1);
      if (msg == WM_LBUTTONUP) self->leftUps.fetch_add(1);
      if (msg == WM_RBUTTONDOWN) self->rightDowns.fetch_add(1);
      if (msg == WM_RBUTTONUP) self->rightUps.fetch_add(1);
      if (msg == WM_MBUTTONDOWN) self->middleDowns.fetch_add(1);
      if (msg == WM_MBUTTONUP) self->middleUps.fetch_add(1);
      if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) {
        const WORD which = GET_XBUTTON_WPARAM(wp);
        const size_t slot = (which == XBUTTON1 || which == XBUTTON2) ? which : 0;
        if (msg == WM_XBUTTONDOWN) {
          self->xDowns[slot].fetch_add(1);
          self->lastXDownWparam.store(static_cast<uint32_t>(wp));
          self->lastXDownX.store(static_cast<int32_t>(static_cast<short>(LOWORD(lp))));
          self->lastXDownY.store(static_cast<int32_t>(static_cast<short>(HIWORD(lp))));
        } else {
          self->xUps[slot].fetch_add(1);
          self->lastXUpWparam.store(static_cast<uint32_t>(wp));
        }
        return TRUE;  // the documented answer for a handled WM_XBUTTON*
      }
    }
    if (msg == WM_TIMER && self) {
      // Paused = a still screen, for the observation e2e's still->moving phase (C0).
      if (self->paused.load()) return 0;
      ++self->frame_;
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    if (msg == WM_PAINT && self) {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd, &ps);
      RECT client{};
      GetClientRect(hwnd, &client);
      // A block that moves every frame, over a background that alternates. Enough changed
      // pixels that no static-scene heuristic can mistake it for a still screen.
      const int phase = static_cast<int>(self->frame_ % 16);
      HBRUSH back = CreateSolidBrush((self->frame_ & 1) ? RGB(20, 20, 30) : RGB(30, 20, 20));
      FillRect(dc, &client, back);
      DeleteObject(back);
      RECT block{phase * 18, 20, phase * 18 + 120, 160};
      HBRUSH fore = CreateSolidBrush(RGB(200, 90 + phase * 8, 40));
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
    wc.lpszClassName = L"Remote60C3InjectTarget";
    RegisterClassExW(&wc);
    title = e2e_unique_window_title(L"remote60 c3 inject target");
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName,
                            title.c_str(), WS_OVERLAPPEDWINDOW, -4000, -4000, 320,
                            240, nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd_) {
      SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
      ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
      // 20 Hz. Measured: raising this to 40 Hz and the window to 960x540 changed the
      // frames the host actually sent by nothing at all -- about two a second either
      // way -- so the cadence is the host's, not this timer's. Kept at the rate that
      // is enough rather than the one that looks busier.
      SetTimer(hwnd_, 1, 50, nullptr);
    }
    ready_.store(true);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    hwnd_ = nullptr;
  }

  uint64_t frame_ = 0;
  HWND hwnd_ = nullptr;
  std::atomic<bool> ready_{false};
  std::thread thread_;
};

}  // namespace remote60::native_poc::e2e

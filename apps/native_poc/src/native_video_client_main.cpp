#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <array>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <mfapi.h>
#include <wrl/client.h>

#include "mf_h264_codec.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

namespace {

using remote60::native_poc::ControlInputAckMessage;
using remote60::native_poc::ControlInputEventMessage;
using remote60::native_poc::ControlPingMessage;
using remote60::native_poc::ControlPongMessage;
using remote60::native_poc::DecodedFrameNv12;
using remote60::native_poc::EncodedFrameHeader;
using remote60::native_poc::H264Decoder;
using remote60::native_poc::MessageHeader;
using remote60::native_poc::MessageType;
using remote60::native_poc::RawFrameHeader;
using remote60::native_poc::UdpCodec;
using remote60::native_poc::UdpHelloPacket;
using remote60::native_poc::UdpPacketKind;
using remote60::native_poc::UdpVideoChunkHeader;
using remote60::native_poc::VideoTransport;
using remote60::native_poc::nv12_to_bgra;
using remote60::native_poc::clamp_udp_mtu;
using remote60::native_poc::parse_video_transport;
using remote60::native_poc::qpc_now_us;
using remote60::native_poc::video_transport_name;

#ifndef REMOTE60_NATIVE_ENCODED_EXPERIMENT
#define REMOTE60_NATIVE_ENCODED_EXPERIMENT 0
#endif

struct WinsockScope {
  bool ok = false;
  WinsockScope() {
    WSADATA wsa{};
    ok = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
  }
  ~WinsockScope() {
    if (ok) WSACleanup();
  }
};

struct Args {
  std::string host = "127.0.0.1";
  uint16_t port = 43000;
  uint16_t controlPort = 0;
  uint32_t controlIntervalMs = 1000;
  uint32_t tcpRecvBufKb = 0;
  uint32_t tcpSendBufKb = 0;
  uint32_t udpMtu = 1200;
  uint32_t traceEvery = 0;
  uint32_t traceMax = 0;
  std::string transport;
  std::string codec = "raw";
  uint32_t seconds = 0;  // 0: infinite
  uint32_t fpsHint = 30;
  bool enableInputChannel = false;
  uint32_t inputLogEvery = 120;
};

bool parse_u32(const char* s, uint32_t* out) {
  if (!s || !out) return false;
  char* end = nullptr;
  const unsigned long v = std::strtoul(s, &end, 10);
  if (!end || *end != '\0') return false;
  *out = static_cast<uint32_t>(v);
  return true;
}

bool env_truthy(const char* key) {
  if (!key) return false;
  const char* v = std::getenv(key);
  if (!v) return false;
  const std::string s = v;
  return s == "1" || s == "true" || s == "TRUE" || s == "on" || s == "ON";
}

Args parse_args(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    if (k == "--host" && i + 1 < argc) {
      a.host = argv[++i];
    } else if (k == "--port" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.port = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
    } else if (k == "--control-port" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.controlPort = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
    } else if (k == "--control-interval-ms" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.controlIntervalMs = std::clamp<uint32_t>(v, 20, 10000);
    } else if (k == "--tcp-recvbuf-kb" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.tcpRecvBufKb = v;
    } else if (k == "--tcp-sendbuf-kb" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.tcpSendBufKb = v;
    } else if (k == "--udp-mtu" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.udpMtu = clamp_udp_mtu(v);
    } else if (k == "--trace-every" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.traceEvery = v;
    } else if (k == "--trace-max" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.traceMax = v;
    } else if (k == "--codec" && i + 1 < argc) {
      a.codec = argv[++i];
    } else if (k == "--transport" && i + 1 < argc) {
      a.transport = argv[++i];
    } else if (k == "--seconds" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.seconds = v;
    } else if (k == "--fps-hint" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.fpsHint = std::clamp<uint32_t>(v, 1, 120);
    } else if (k == "--no-input-channel") {
      a.enableInputChannel = false;
    } else if (k == "--input-log-every" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.inputLogEvery = std::max<uint32_t>(1, v);
    }
  }
  return a;
}

bool recv_all(SOCKET s, void* out, size_t len) {
  auto* p = reinterpret_cast<uint8_t*>(out);
  size_t got = 0;
  while (got < len) {
    const int n = recv(s, reinterpret_cast<char*>(p + got), static_cast<int>(len - got), 0);
    if (n <= 0) return false;
    got += static_cast<size_t>(n);
  }
  return true;
}

bool send_all(SOCKET s, const void* data, size_t len) {
  const char* p = reinterpret_cast<const char*>(data);
  size_t sent = 0;
  while (sent < len) {
    const int n = send(s, p + sent, static_cast<int>(len - sent), 0);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

bool recv_discard(SOCKET s, size_t len) {
  std::vector<uint8_t> scratch(1024);
  size_t left = len;
  while (left > 0) {
    const size_t chunk = std::min(left, scratch.size());
    if (!recv_all(s, scratch.data(), chunk)) return false;
    left -= chunk;
  }
  return true;
}

struct SharedFrame {
  std::mutex mu;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  uint32_t seq = 0;
  uint64_t captureUs = 0;
  uint64_t encodeStartUs = 0;
  uint64_t encodeEndUs = 0;
  uint64_t sendUs = 0;
  uint64_t recvUs = 0;
  uint64_t decodeStartUs = 0;
  uint64_t decodeEndUs = 0;
  uint64_t version = 0;
  std::vector<uint8_t> bytes;
};

SharedFrame gFrame;
std::atomic<bool> gRunning{true};
SOCKET gSock = INVALID_SOCKET;
HWND gHwnd = nullptr;
uint32_t gWindowW = 1280;
uint32_t gWindowH = 720;
std::atomic<bool> gPaintQueued{false};
std::atomic<uint32_t> gTraceEvery{0};
std::atomic<uint32_t> gTraceMax{0};
std::atomic<uint64_t> gTracePresentPrinted{0};
std::atomic<uint64_t> gTraceRecvPrinted{0};
constexpr bool kAllInputBlocked = true;
// Catch-up defaults tuned for software codec path: avoid runaway multi-second lag,
// but still clamp perceived latency quickly for interactive remote use.
constexpr uint64_t kCatchupLagDropUs = 450000;       // 0.45s
constexpr uint64_t kCatchupResumeKeyLagUs = 500000;  // 0.5s
constexpr uint64_t kDecodeQueueLagDropUs = 300000;   // 0.3s
constexpr uint64_t kDecodeQueueLagResumeUs = 400000; // 0.4s
constexpr uint64_t kStaleCaptureDropUs = 50000;      // 50ms

std::mutex gInputMu;
std::deque<ControlInputEventMessage> gInputQueue;
std::atomic<uint32_t> gInputSeq{0};
std::atomic<uint64_t> gInputDropped{0};
std::atomic<bool> gInputEnabled{false};
std::atomic<uint16_t> gMouseButtons{0};

void enqueue_input_event(uint16_t kind, int32_t x, int32_t y, int32_t wheelDelta, uint32_t keyCode) {
  if (kAllInputBlocked) return;
  if (!gInputEnabled.load()) return;
  ControlInputEventMessage msg{};
  msg.header.magic = remote60::native_poc::kMagic;
  msg.header.type = static_cast<uint16_t>(MessageType::ControlInputEvent);
  msg.header.size = static_cast<uint16_t>(sizeof(msg));
  msg.seq = gInputSeq.fetch_add(1) + 1;
  msg.kind = kind;
  msg.buttons = gMouseButtons.load();
  msg.x = x;
  msg.y = y;
  msg.wheelDelta = wheelDelta;
  msg.keyCode = keyCode;
  msg.clientSendQpcUs = qpc_now_us();

  std::lock_guard<std::mutex> lk(gInputMu);
  if (kind == 1 && !gInputQueue.empty() && gInputQueue.back().kind == 1) {
    gInputQueue.back() = msg;
    return;
  }
  if (gInputQueue.size() >= 256) {
    gInputQueue.pop_front();
    gInputDropped.fetch_add(1);
  }
  gInputQueue.push_back(msg);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CLOSE:
      gRunning = false;
      if (gSock != INVALID_SOCKET) shutdown(gSock, SD_BOTH);
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_MOUSEMOVE:
      if (kAllInputBlocked) return 0;
      enqueue_input_event(1, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0, 0);
      return 0;
    case WM_LBUTTONDOWN:
      if (kAllInputBlocked) return 0;
      gMouseButtons.fetch_or(1);
      enqueue_input_event(2, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0, VK_LBUTTON);
      return 0;
    case WM_LBUTTONUP:
      if (kAllInputBlocked) return 0;
      gMouseButtons.fetch_and(static_cast<uint16_t>(~1u));
      enqueue_input_event(3, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0, VK_LBUTTON);
      return 0;
    case WM_RBUTTONDOWN:
      if (kAllInputBlocked) return 0;
      gMouseButtons.fetch_or(2);
      enqueue_input_event(2, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0, VK_RBUTTON);
      return 0;
    case WM_RBUTTONUP:
      if (kAllInputBlocked) return 0;
      gMouseButtons.fetch_and(static_cast<uint16_t>(~2u));
      enqueue_input_event(3, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0, VK_RBUTTON);
      return 0;
    case WM_MBUTTONDOWN:
      if (kAllInputBlocked) return 0;
      gMouseButtons.fetch_or(4);
      enqueue_input_event(2, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0, VK_MBUTTON);
      return 0;
    case WM_MBUTTONUP:
      if (kAllInputBlocked) return 0;
      gMouseButtons.fetch_and(static_cast<uint16_t>(~4u));
      enqueue_input_event(3, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0, VK_MBUTTON);
      return 0;
    case WM_MOUSEWHEEL: {
      if (kAllInputBlocked) return 0;
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &p);
      enqueue_input_event(4, p.x, p.y, GET_WHEEL_DELTA_WPARAM(wp), 0);
      return 0;
    }
    case WM_KEYDOWN:
      if (kAllInputBlocked) return 0;
      enqueue_input_event(5, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_KEYUP:
      if (kAllInputBlocked) return 0;
      enqueue_input_event(6, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
      return kAllInputBlocked ? 0 : DefWindowProc(hwnd, msg, wp, lp);
    case WM_PAINT: {
      gPaintQueued = false;
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT rc{};
      GetClientRect(hwnd, &rc);
      FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

      std::vector<uint8_t> local;
      uint32_t w = 0, h = 0;
      uint32_t seq = 0;
      uint64_t captureUs = 0;
      uint64_t encodeStartUs = 0;
      uint64_t encodeEndUs = 0;
      uint64_t sendUs = 0;
      uint64_t recvUs = 0;
      uint64_t decodeStartUs = 0;
      uint64_t decodeEndUs = 0;
      uint64_t frameVersion = 0;
      {
        std::lock_guard<std::mutex> lk(gFrame.mu);
        if (!gFrame.bytes.empty()) {
          local = gFrame.bytes;
          w = gFrame.width;
          h = gFrame.height;
          seq = gFrame.seq;
          captureUs = gFrame.captureUs;
          encodeStartUs = gFrame.encodeStartUs;
          encodeEndUs = gFrame.encodeEndUs;
          sendUs = gFrame.sendUs;
          recvUs = gFrame.recvUs;
          decodeStartUs = gFrame.decodeStartUs;
          decodeEndUs = gFrame.decodeEndUs;
          frameVersion = gFrame.version;
        }
      }
      if (!local.empty() && w > 0 && h > 0) {
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = static_cast<LONG>(w);
        bmi.bmiHeader.biHeight = -static_cast<LONG>(h);  // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(hdc, COLORONCOLOR);
        StretchDIBits(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                      0, 0, static_cast<int>(w), static_cast<int>(h),
                      local.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
        const uint64_t presentUs = qpc_now_us();
        const uint32_t traceEvery = gTraceEvery.load();
        const uint32_t traceMax = gTraceMax.load();
        if (traceEvery > 0 && (seq % traceEvery) == 0 &&
            (traceMax == 0 || gTracePresentPrinted.load() < traceMax)) {
          const auto nowPrinted = gTracePresentPrinted.fetch_add(1) + 1;
          if (traceMax == 0 || nowPrinted <= traceMax) {
            const uint64_t netUs = (recvUs >= sendUs) ? (recvUs - sendUs) : 0;
            const uint64_t c2eUs = (encodeStartUs >= captureUs) ? (encodeStartUs - captureUs) : 0;
            const uint64_t encUs = (encodeEndUs >= encodeStartUs) ? (encodeEndUs - encodeStartUs) : 0;
            const uint64_t e2sUs = (sendUs >= encodeEndUs) ? (sendUs - encodeEndUs) : 0;
            const uint64_t r2dUs = (decodeStartUs >= recvUs) ? (decodeStartUs - recvUs) : 0;
            const uint64_t decUs = (decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0;
            const uint64_t d2pUs = (presentUs >= decodeEndUs) ? (presentUs - decodeEndUs) : 0;
            const uint64_t renderUs = (presentUs >= recvUs) ? (presentUs - recvUs) : 0;
            const uint64_t totalUs = (presentUs >= captureUs) ? (presentUs - captureUs) : 0;
            std::cout << "[native-video-client][trace_present] seq=" << seq
                      << " captureUs=" << captureUs
                      << " encodeStartUs=" << encodeStartUs
                      << " encodeEndUs=" << encodeEndUs
                      << " sendUs=" << sendUs
                      << " recvUs=" << recvUs
                      << " decodeStartUs=" << decodeStartUs
                      << " decodeEndUs=" << decodeEndUs
                      << " presentUs=" << presentUs
                      << " c2eUs=" << c2eUs
                      << " encUs=" << encUs
                      << " e2sUs=" << e2sUs
                      << " netUs=" << netUs
                      << " r2dUs=" << r2dUs
                      << " decUs=" << decUs
                      << " d2pUs=" << d2pUs
                      << " renderUs=" << renderUs
                      << " totalUs=" << totalUs
                      << "\n";
          }
        }
      }
      EndPaint(hwnd, &ps);
      uint64_t latestVersion = 0;
      {
        std::lock_guard<std::mutex> lk(gFrame.mu);
        latestVersion = gFrame.version;
      }
      if (latestVersion != frameVersion && !gPaintQueued.exchange(true)) {
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }
    default:
      return DefWindowProc(hwnd, msg, wp, lp);
  }
}

bool create_window() {
  HINSTANCE inst = GetModuleHandle(nullptr);
  const wchar_t* cls = L"Remote60NativeVideoClientPoc";
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  wc.lpszClassName = cls;
  if (!RegisterClassExW(&wc)) return false;

  gHwnd = CreateWindowExW(0, cls, L"remote60 native video client poc",
                          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          static_cast<int>(gWindowW), static_cast<int>(gWindowH),
                          nullptr, nullptr, inst, nullptr);
  if (!gHwnd) return false;
  ShowWindow(gHwnd, SW_SHOW);
  UpdateWindow(gHwnd);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::cout.setf(std::ios::unitbuf);
  std::cerr.setf(std::ios::unitbuf);

  const Args args = parse_args(argc, argv);
  gTraceEvery = args.traceEvery;
  gTraceMax = args.traceMax;

  const bool useRaw = (args.codec == "raw");
  const bool useH264 = (args.codec == "h264");
  const bool encodedExperimentEnabled =
      (REMOTE60_NATIVE_ENCODED_EXPERIMENT != 0) || env_truthy("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE");
  if (!useRaw && !useH264) {
    std::cerr << "[native-video-client] unsupported codec: " << args.codec << " (supported: raw,h264)\n";
    return 10;
  }
  if (useH264 && !encodedExperimentEnabled) {
    std::cerr << "[native-video-client] unsupported codec: " << args.codec
              << " (enable REMOTE60_NATIVE_ENCODED_EXPERIMENT or set env REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1)\n";
    return 10;
  }
  std::string effectiveTransport = args.transport;
  if (effectiveTransport.empty()) {
    effectiveTransport = useH264 ? "udp" : "tcp";
  }
  VideoTransport transport = VideoTransport::Tcp;
  if (!parse_video_transport(effectiveTransport, &transport)) {
    std::cerr << "[native-video-client] unsupported transport: " << effectiveTransport << " (supported: tcp,udp)\n";
    return 12;
  }
  if (transport == VideoTransport::Udp && useRaw) {
    std::cerr << "[native-video-client] raw codec over udp is not supported in current phase (use codec=h264)\n";
    return 13;
  }

  WinsockScope ws;
  if (!ws.ok) {
    std::cerr << "[native-video-client] WSAStartup failed\n";
    return 1;
  }

  if (!create_window()) {
    std::cerr << "[native-video-client] window create failed\n";
    return 2;
  }

  bool mfStarted = false;
  H264Decoder decoder;
  bool decoderReady = false;
  bool waitForKeyFrame = useH264;
  uint32_t decoderW = 0;
  uint32_t decoderH = 0;
  Microsoft::WRL::ComPtr<ID3D11Device> decD3dDevice;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> decD3dContext;
  if (useH264) {
    const HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
      std::cerr << "[native-video-client] MFStartup failed hr=0x" << std::hex << static_cast<unsigned long>(hr)
                << std::dec << "\n";
      return 11;
    }
    mfStarted = true;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    const HRESULT d3dHr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                            D3D11_SDK_VERSION, &decD3dDevice, &fl, &decD3dContext);
    if (SUCCEEDED(d3dHr) && decD3dDevice) {
      (void)decoder.set_d3d11_device(decD3dDevice.Get());
    }
  }

  gSock = socket(AF_INET,
                 (transport == VideoTransport::Udp) ? SOCK_DGRAM : SOCK_STREAM,
                 (transport == VideoTransport::Udp) ? IPPROTO_UDP : IPPROTO_TCP);
  if (gSock == INVALID_SOCKET) {
    std::cerr << "[native-video-client] socket create failed\n";
    if (mfStarted) MFShutdown();
    return 3;
  }

  if (transport == VideoTransport::Tcp) {
    int noDelay = 1;
    setsockopt(gSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
  }
  if (transport == VideoTransport::Udp) {
    if (args.tcpRecvBufKb == 0) {
      const int recvBuf = 1024 * 1024;
      (void)setsockopt(gSock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
    }
    if (args.tcpSendBufKb == 0) {
      const int sendBuf = 256 * 1024;
      (void)setsockopt(gSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
    }
  }
  if (args.tcpRecvBufKb > 0) {
    const int recvBuf = static_cast<int>(args.tcpRecvBufKb * 1024u);
    setsockopt(gSock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
  }
  if (args.tcpSendBufKb > 0) {
    const int sendBuf = static_cast<int>(args.tcpSendBufKb * 1024u);
    setsockopt(gSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(args.port);
  if (inet_pton(AF_INET, args.host.c_str(), &addr.sin_addr) != 1) {
    std::cerr << "[native-video-client] invalid host " << args.host << "\n";
    closesocket(gSock);
    gSock = INVALID_SOCKET;
    if (mfStarted) MFShutdown();
    return 4;
  }
  if (connect(gSock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "[native-video-client] connect failed " << args.host << ":" << args.port << "\n";
    closesocket(gSock);
    gSock = INVALID_SOCKET;
    if (mfStarted) MFShutdown();
    return 5;
  }
  if (transport == VideoTransport::Udp) {
    int timeoutMs = 200;
    (void)setsockopt(gSock, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    bool handshakeOk = false;
    for (int attempt = 0; attempt < 40 && !handshakeOk; ++attempt) {
      UdpHelloPacket hello{};
      hello.kind = static_cast<uint16_t>(UdpPacketKind::Hello);
      const int sent = send(gSock, reinterpret_cast<const char*>(&hello), sizeof(hello), 0);
      if (sent <= 0) {
        Sleep(50);
        continue;
      }
      UdpHelloPacket ack{};
      const int n = recv(gSock, reinterpret_cast<char*>(&ack), sizeof(ack), 0);
      if (n >= static_cast<int>(sizeof(UdpHelloPacket)) &&
          ack.magic == remote60::native_poc::kMagic &&
          ack.kind == static_cast<uint16_t>(UdpPacketKind::HelloAck)) {
        handshakeOk = true;
        break;
      }
      Sleep(50);
    }
    timeoutMs = 0;
    (void)setsockopt(gSock, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    if (!handshakeOk) {
      std::cerr << "[native-video-client] udp handshake failed " << args.host << ":" << args.port << "\n";
      closesocket(gSock);
      gSock = INVALID_SOCKET;
      if (mfStarted) MFShutdown();
      return 6;
    }
  }

  std::cout << "[native-video-client] connected host=" << args.host
            << " port=" << args.port
            << " transport=" << video_transport_name(transport)
            << " codec=" << args.codec
            << " seconds=" << args.seconds << "\n";
  if (kAllInputBlocked) {
    std::cout << "[native-video-client] all input blocked (view-only)\n";
  }
  int effectiveRecvBuf = 0;
  int effectiveRecvBufLen = sizeof(effectiveRecvBuf);
  (void)getsockopt(gSock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&effectiveRecvBuf), &effectiveRecvBufLen);
  int effectiveSendBuf = 0;
  int effectiveSendBufLen = sizeof(effectiveSendBuf);
  (void)getsockopt(gSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&effectiveSendBuf), &effectiveSendBufLen);
  std::cout << "[native-video-client] socket rcvbuf=" << effectiveRecvBuf
            << " sndbuf=" << effectiveSendBuf << " bytes\n";

  SOCKET controlSock = INVALID_SOCKET;
  std::thread controlThread;
  if (args.controlPort > 0) {
    controlSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (controlSock != INVALID_SOCKET) {
      int ctlNoDelay = 1;
      setsockopt(controlSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&ctlNoDelay), sizeof(ctlNoDelay));
      sockaddr_in ctlAddr{};
      ctlAddr.sin_family = AF_INET;
      ctlAddr.sin_port = htons(args.controlPort);
      if (inet_pton(AF_INET, args.host.c_str(), &ctlAddr.sin_addr) == 1 &&
          connect(controlSock, reinterpret_cast<const sockaddr*>(&ctlAddr), sizeof(ctlAddr)) == 0) {
        const bool inputChannelEnabled = args.enableInputChannel && !kAllInputBlocked;
        gInputEnabled = inputChannelEnabled;
        if (inputChannelEnabled) {
          enqueue_input_event(1, 0, 0, 0, 0);
        }
        controlThread = std::thread([&]() {
          uint32_t pingSeq = 0;
          uint64_t inputAckCount = 0;
          uint64_t nextPingUs = qpc_now_us();
          while (gRunning.load()) {
            bool didWork = false;
            const uint64_t nowUs = qpc_now_us();
            if (nowUs >= nextPingUs) {
              ControlPingMessage ping{};
              ping.header.magic = remote60::native_poc::kMagic;
              ping.header.type = static_cast<uint16_t>(MessageType::ControlPing);
              ping.header.size = static_cast<uint16_t>(sizeof(ping));
              ping.seq = ++pingSeq;
              ping.clientSendQpcUs = nowUs;
              if (!send_all(controlSock, &ping, sizeof(ping))) break;

              MessageHeader header{};
              if (!recv_all(controlSock, &header, sizeof(header))) break;
              if (header.magic != remote60::native_poc::kMagic ||
                  header.type != static_cast<uint16_t>(MessageType::ControlPong) ||
                  header.size != sizeof(ControlPongMessage)) {
                break;
              }
              ControlPongMessage pong{};
              pong.header = header;
              if (!recv_all(controlSock, &pong.seq, sizeof(pong) - sizeof(MessageHeader))) break;

              const uint64_t doneUs = qpc_now_us();
              const uint64_t rttUs = (doneUs >= ping.clientSendQpcUs) ? (doneUs - ping.clientSendQpcUs) : 0;
              std::cout << "[native-video-client][control] seq=" << pong.seq
                        << " rttUs=" << rttUs
                        << " hostQueueUs=" << ((pong.hostSendQpcUs >= pong.hostRecvQpcUs)
                                                    ? (pong.hostSendQpcUs - pong.hostRecvQpcUs)
                                                    : 0)
                        << "\n";
              nextPingUs = doneUs + static_cast<uint64_t>(std::max<uint32_t>(20, args.controlIntervalMs)) * 1000ULL;
              didWork = true;
            }

            ControlInputEventMessage input{};
            bool hasInput = false;
            {
              std::lock_guard<std::mutex> lk(gInputMu);
              if (!gInputQueue.empty()) {
                input = gInputQueue.front();
                gInputQueue.pop_front();
                hasInput = true;
              }
            }
            if (hasInput) {
              input.clientSendQpcUs = qpc_now_us();
              if (!send_all(controlSock, &input, sizeof(input))) break;

              MessageHeader header{};
              if (!recv_all(controlSock, &header, sizeof(header))) break;
              if (header.magic != remote60::native_poc::kMagic ||
                  header.type != static_cast<uint16_t>(MessageType::ControlInputAck) ||
                  header.size != sizeof(ControlInputAckMessage)) {
                break;
              }
              ControlInputAckMessage ack{};
              ack.header = header;
              if (!recv_all(controlSock, &ack.seq, sizeof(ack) - sizeof(MessageHeader))) break;
              ++inputAckCount;
              if (args.inputLogEvery > 0 && (inputAckCount % args.inputLogEvery) == 0) {
                std::cout << "[native-video-client][input] ackSeq=" << ack.seq
                          << " sent=" << inputAckCount
                          << " dropped=" << gInputDropped.load()
                          << "\n";
              }
              didWork = true;
            }

            if (!didWork) Sleep(2);
          }
        });
        std::cout << "[native-video-client] control connected port=" << args.controlPort
                  << " inputChannel=" << (inputChannelEnabled ? 1 : 0) << "\n";
      } else {
        closesocket(controlSock);
        controlSock = INVALID_SOCKET;
        std::cout << "[native-video-client] control connect failed port=" << args.controlPort << "\n";
      }
    }
  }

  const uint64_t startUs = qpc_now_us();
  std::thread recvThread([&]() {
    uint64_t statAtUs = qpc_now_us() + 1000000ULL;
    uint64_t recvFrames = 0;
    uint64_t decodedFrames = 0;
    uint64_t skippedQueued = 0;
    uint64_t recvBytes = 0;
    uint64_t sumLatencyUs = 0;
    uint64_t maxLatencyUs = 0;
    uint64_t sumDecodeTailUs = 0;
    uint64_t maxDecodeTailUs = 0;
    uint64_t decodeFailCount = 0;
    uint64_t decodeEmptyCount = 0;
    uint64_t waitingKeyDropCount = 0;
    uint64_t lagDropCount = 0;
    bool catchupMode = false;
    uint64_t lastPresentedCaptureUs = 0;
    auto process_h264_frame = [&](const EncodedFrameHeader& h, std::vector<uint8_t>* payloadPtr,
                                  uint64_t packetNowUs) -> bool {
      if (!payloadPtr) return true;
      ++recvFrames;
      recvBytes += h.payloadSize;

      if (!useH264) {
        ++skippedQueued;
        return true;
      }

      if (!decoderReady || decoderW != h.width || decoderH != h.height) {
        decoder.shutdown();
        if (!decoder.initialize(h.width, h.height, args.fpsHint)) {
          std::cerr << "[native-video-client] H264 decoder initialize failed size=" << h.width << "x" << h.height
                    << "\n";
          return false;
        }
        std::cout << "[native-video-client] H264 decoder backend=" << decoder.backend_name()
                  << " hw=" << (decoder.using_hardware() ? 1 : 0)
                  << " size=" << h.width << "x" << h.height << "\n";
        decoderReady = true;
        decoderW = h.width;
        decoderH = h.height;
        waitForKeyFrame = true;
      }

      const bool keyFrame = ((h.flags & 1u) != 0);
      const uint64_t streamLagUs = (packetNowUs >= h.captureQpcUs) ? (packetNowUs - h.captureQpcUs) : 0;
      const uint64_t decodeQueueLagEstimateUs =
          (lastPresentedCaptureUs > 0 && h.captureQpcUs >= lastPresentedCaptureUs)
              ? (h.captureQpcUs - lastPresentedCaptureUs)
              : 0;
      const uint64_t staleBehindUs =
          (lastPresentedCaptureUs > 0 && lastPresentedCaptureUs > h.captureQpcUs)
              ? (lastPresentedCaptureUs - h.captureQpcUs)
              : 0;

      if (staleBehindUs > kStaleCaptureDropUs) {
        ++skippedQueued;
        ++lagDropCount;
        if ((lagDropCount % 120) == 1) {
          std::cout << "[native-video-client] stale frame drop count=" << lagDropCount
                    << " staleBehindUs=" << staleBehindUs
                    << " seq=" << h.seq << "\n";
        }
        return true;
      }

      if (!catchupMode &&
          (decodeQueueLagEstimateUs > kDecodeQueueLagDropUs ||
           (lastPresentedCaptureUs > 0 && streamLagUs > kCatchupLagDropUs))) {
        catchupMode = true;
        waitForKeyFrame = true;
        decoder.reset();
        std::cout << "[native-video-client] catchup enter streamLagUs=" << streamLagUs
                  << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                  << " reason="
                  << ((decodeQueueLagEstimateUs > kDecodeQueueLagDropUs) ? "decode_queue" : "stream_lag_emergency")
                  << " seq=" << h.seq << "\n";
      }
      if (catchupMode && !keyFrame) {
        ++skippedQueued;
        ++lagDropCount;
        if ((lagDropCount % 120) == 1) {
          std::cout << "[native-video-client] catchup drops=" << lagDropCount
                    << " streamLagUs=" << streamLagUs
                    << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                    << "\n";
        }
        return true;
      }
      if (catchupMode && keyFrame) {
        catchupMode = false;
        std::cout << "[native-video-client] catchup exit streamLagUs=" << streamLagUs
                  << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                  << " seq=" << h.seq << "\n";
      }

      if (waitForKeyFrame && !keyFrame) {
        ++skippedQueued;
        ++waitingKeyDropCount;
        if ((waitingKeyDropCount % 120) == 1) {
          std::cout << "[native-video-client] waiting keyframe drops=" << waitingKeyDropCount << "\n";
        }
        if (packetNowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
          const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          std::cout << "[native-video-client] recvFrames=" << recvFrames
                    << " decodedFrames=" << decodedFrames
                    << " skippedQueued=" << skippedQueued
                    << " avgLatencyUs=" << avgLatencyUs
                    << " maxLatencyUs=" << maxLatencyUs
                    << " avgDecodeTailUs=" << avgDecodeTailUs
                    << " maxDecodeTailUs=" << maxDecodeTailUs
                    << " mbps=" << mbps
                    << " size=" << h.width << "x" << h.height
                    << "\n";
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
        return true;
      }

      const uint64_t decodeStartUs = qpc_now_us();
      std::vector<DecodedFrameNv12> outFrames;
      const int64_t inputSampleTimeHns = static_cast<int64_t>(h.captureQpcUs) * 10;
      if (!decoder.decode_access_unit(*payloadPtr, keyFrame, inputSampleTimeHns, &outFrames)) {
        ++skippedQueued;
        ++decodeFailCount;
        if ((decodeFailCount % 60) == 1) {
          std::cout << "[native-video-client] decode failed count=" << decodeFailCount << "\n";
        }
        waitForKeyFrame = true;
        decoder.reset();
        if (packetNowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
          const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          std::cout << "[native-video-client] recvFrames=" << recvFrames
                    << " decodedFrames=" << decodedFrames
                    << " skippedQueued=" << skippedQueued
                    << " avgLatencyUs=" << avgLatencyUs
                    << " maxLatencyUs=" << maxLatencyUs
                    << " avgDecodeTailUs=" << avgDecodeTailUs
                    << " maxDecodeTailUs=" << maxDecodeTailUs
                    << " mbps=" << mbps
                    << " size=" << h.width << "x" << h.height
                    << "\n";
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
        return true;
      }
      waitForKeyFrame = false;
      if (outFrames.empty()) {
        ++decodeEmptyCount;
        if ((decodeEmptyCount % 120) == 1) {
          std::cout << "[native-video-client] decode output empty count=" << decodeEmptyCount << "\n";
        }
        if (packetNowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
          const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          std::cout << "[native-video-client] recvFrames=" << recvFrames
                    << " decodedFrames=" << decodedFrames
                    << " skippedQueued=" << skippedQueued
                    << " avgLatencyUs=" << avgLatencyUs
                    << " maxLatencyUs=" << maxLatencyUs
                    << " avgDecodeTailUs=" << avgDecodeTailUs
                    << " maxDecodeTailUs=" << maxDecodeTailUs
                    << " mbps=" << mbps
                    << " size=" << h.width << "x" << h.height
                    << "\n";
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
        return true;
      }

      const auto& decoded = outFrames.back();
      const bool tsFromMft = decoded.sampleTimeFromOutput && (decoded.sampleTimeHns > 0);
      const bool tsFromInputFallback = (!decoded.sampleTimeFromOutput) && (decoded.sampleTimeHns > 0);
      const bool tsFromHeaderFallback = (decoded.sampleTimeHns <= 0);
      const uint64_t decodedCaptureUs =
          tsFromHeaderFallback ? h.captureQpcUs : static_cast<uint64_t>(decoded.sampleTimeHns / 10);
      const char* tsSource = tsFromMft ? "mft" : (tsFromInputFallback ? "input_fallback" : "header_fallback");
      std::vector<uint8_t> bgra;
      if (!nv12_to_bgra(decoded.bytes.data(), decoded.width, decoded.height, &bgra) || bgra.empty()) {
        ++skippedQueued;
        waitForKeyFrame = true;
        return true;
      }
      const uint64_t decodeEndUs = qpc_now_us();

      const uint64_t nowUs = qpc_now_us();
      {
        std::lock_guard<std::mutex> lk(gFrame.mu);
        gFrame.width = decoded.width;
        gFrame.height = decoded.height;
        gFrame.stride = decoded.width * 4;
        gFrame.seq = h.seq;
        gFrame.captureUs = decodedCaptureUs;
        gFrame.encodeStartUs = h.encodeStartQpcUs;
        gFrame.encodeEndUs = h.encodeEndQpcUs;
        gFrame.sendUs = h.sendQpcUs;
        gFrame.recvUs = packetNowUs;
        gFrame.decodeStartUs = decodeStartUs;
        gFrame.decodeEndUs = decodeEndUs;
        gFrame.version += 1;
        gFrame.bytes.swap(bgra);
      }
      if (gHwnd && !gPaintQueued.exchange(true)) {
        InvalidateRect(gHwnd, nullptr, FALSE);
      }

      if (args.traceEvery > 0 && (h.seq % args.traceEvery) == 0 &&
          (args.traceMax == 0 || gTraceRecvPrinted.load() < args.traceMax)) {
        const auto nowPrinted = gTraceRecvPrinted.fetch_add(1) + 1;
        if (args.traceMax == 0 || nowPrinted <= args.traceMax) {
          std::cout << "[native-video-client][trace_recv] seq=" << h.seq
                    << " captureUs=" << decodedCaptureUs
                    << " hdrCaptureUs=" << h.captureQpcUs
                    << " encodeStartUs=" << h.encodeStartQpcUs
                    << " encodeEndUs=" << h.encodeEndQpcUs
                    << " sendUs=" << h.sendQpcUs
                    << " recvUs=" << packetNowUs
                    << " decodeStartUs=" << decodeStartUs
                    << " decodeEndUs=" << decodeEndUs
                    << " c2eUs=" << ((h.encodeStartQpcUs >= h.captureQpcUs) ? (h.encodeStartQpcUs - h.captureQpcUs) : 0)
                    << " encUs=" << ((h.encodeEndQpcUs >= h.encodeStartQpcUs) ? (h.encodeEndQpcUs - h.encodeStartQpcUs) : 0)
                    << " e2sUs=" << ((h.sendQpcUs >= h.encodeEndQpcUs) ? (h.sendQpcUs - h.encodeEndQpcUs) : 0)
                    << " netUs=" << ((packetNowUs >= h.sendQpcUs) ? (packetNowUs - h.sendQpcUs) : 0)
                    << " r2dUs=" << ((decodeStartUs >= packetNowUs) ? (decodeStartUs - packetNowUs) : 0)
                    << " decUs=" << ((decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0)
                    << " decodeQueueLagUs=" << ((h.captureQpcUs >= decodedCaptureUs) ? (h.captureQpcUs - decodedCaptureUs) : 0)
                    << " tsSource=" << tsSource
                    << " bytes=" << h.payloadSize
                    << " key=" << (keyFrame ? 1 : 0)
                    << "\n";
        }
      }

      ++decodedFrames;
      lastPresentedCaptureUs = decodedCaptureUs;
      const uint64_t latencyUs = (nowUs >= decodedCaptureUs) ? (nowUs - decodedCaptureUs) : 0;
      const uint64_t decodeTailUs = (nowUs >= h.sendQpcUs) ? (nowUs - h.sendQpcUs) : 0;
      sumLatencyUs += latencyUs;
      sumDecodeTailUs += decodeTailUs;
      maxLatencyUs = std::max(maxLatencyUs, latencyUs);
      maxDecodeTailUs = std::max(maxDecodeTailUs, decodeTailUs);

      if (nowUs >= statAtUs) {
        const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
        const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
        const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
        std::cout << "[native-video-client] recvFrames=" << recvFrames
                  << " decodedFrames=" << decodedFrames
                  << " skippedQueued=" << skippedQueued
                  << " avgLatencyUs=" << avgLatencyUs
                  << " maxLatencyUs=" << maxLatencyUs
                  << " avgDecodeTailUs=" << avgDecodeTailUs
                  << " maxDecodeTailUs=" << maxDecodeTailUs
                  << " mbps=" << mbps
                  << " size=" << decoded.width << "x" << decoded.height
                  << "\n";
        recvFrames = 0;
        decodedFrames = 0;
        skippedQueued = 0;
        recvBytes = 0;
        sumLatencyUs = 0;
        maxLatencyUs = 0;
        sumDecodeTailUs = 0;
        maxDecodeTailUs = 0;
        statAtUs += 1000000ULL;
      }
      return true;
    };

    if (transport == VideoTransport::Udp) {
      std::array<uint8_t, 1600> datagram{};
      bool assembling = false;
      uint32_t assemblingSeq = 0;
      uint32_t assemblingExpected = 0;
      uint32_t assemblingNextOffset = 0;
      EncodedFrameHeader assemblingHdr{};
      std::vector<uint8_t> assemblingPayload;
      uint64_t assemblyDropped = 0;

      while (gRunning.load()) {
        const int n = recv(gSock, reinterpret_cast<char*>(datagram.data()), static_cast<int>(datagram.size()), 0);
        if (n <= 0) break;
        if (n < static_cast<int>(sizeof(UdpVideoChunkHeader))) continue;

        UdpVideoChunkHeader u{};
        std::memcpy(&u, datagram.data(), sizeof(u));
        if (u.magic != remote60::native_poc::kMagic ||
            u.kind != static_cast<uint16_t>(UdpPacketKind::VideoChunk) ||
            u.size != sizeof(UdpVideoChunkHeader)) {
          continue;
        }
        if (u.codec != static_cast<uint16_t>(UdpCodec::H264)) {
          ++skippedQueued;
          continue;
        }
        if (u.payloadSize == 0 || u.chunkSize == 0 ||
            u.chunkOffset > u.payloadSize ||
            (u.chunkOffset + u.chunkSize) > u.payloadSize ||
            (sizeof(UdpVideoChunkHeader) + u.chunkSize) > static_cast<uint32_t>(n)) {
          ++skippedQueued;
          assembling = false;
          continue;
        }

        const bool firstChunk = ((u.flags & 0x2u) != 0);
        const bool lastChunk = ((u.flags & 0x4u) != 0);
        if (firstChunk) {
          assembling = true;
          assemblingSeq = u.seq;
          assemblingExpected = u.payloadSize;
          assemblingNextOffset = 0;
          assemblingPayload.assign(assemblingExpected, 0);
          assemblingHdr = {};
          assemblingHdr.header.magic = remote60::native_poc::kMagic;
          assemblingHdr.header.type = static_cast<uint16_t>(MessageType::EncodedFrameH264);
          assemblingHdr.header.size = static_cast<uint16_t>(sizeof(assemblingHdr));
          assemblingHdr.seq = u.seq;
          assemblingHdr.width = u.width;
          assemblingHdr.height = u.height;
          assemblingHdr.flags = (u.flags & 0x1u) ? 1u : 0u;
          assemblingHdr.captureQpcUs = u.captureQpcUs;
          assemblingHdr.encodeStartQpcUs = u.encodeStartQpcUs;
          assemblingHdr.encodeEndQpcUs = u.encodeEndQpcUs;
          assemblingHdr.sendQpcUs = u.sendQpcUs;
        }

        if (!assembling || u.seq != assemblingSeq || u.chunkOffset != assemblingNextOffset) {
          ++skippedQueued;
          ++assemblyDropped;
          if ((assemblyDropped % 120) == 1) {
            std::cout << "[native-video-client] udp assembly drop count=" << assemblyDropped
                      << " seq=" << u.seq
                      << " expectedSeq=" << assemblingSeq
                      << " chunkOffset=" << u.chunkOffset
                      << " nextOffset=" << assemblingNextOffset
                      << "\n";
          }
          continue;
        }

        std::memcpy(assemblingPayload.data() + u.chunkOffset,
                    datagram.data() + sizeof(UdpVideoChunkHeader), u.chunkSize);
        assemblingNextOffset += u.chunkSize;
        if (lastChunk) {
          if (assemblingNextOffset != assemblingExpected) {
            ++skippedQueued;
            ++assemblyDropped;
            assembling = false;
            continue;
          }
          assemblingHdr.payloadSize = assemblingExpected;
          std::vector<uint8_t> payload = std::move(assemblingPayload);
          assembling = false;
          const uint64_t packetNowUs = qpc_now_us();
          if (!process_h264_frame(assemblingHdr, &payload, packetNowUs)) break;
        }

        const uint64_t nowUs = qpc_now_us();
        if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
          break;
        }
      }

      gRunning = false;
      if (gHwnd) PostMessage(gHwnd, WM_CLOSE, 0, 0);
      return;
    }

    while (gRunning.load()) {
      MessageHeader header{};
      if (!recv_all(gSock, &header, sizeof(header))) break;
      if (header.magic != remote60::native_poc::kMagic || header.size < sizeof(header)) break;
      const auto msgType = static_cast<MessageType>(header.type);

      if (msgType == MessageType::RawFrameBgra && header.size == sizeof(RawFrameHeader)) {
        RawFrameHeader h{};
        h.header = header;
        if (!recv_all(gSock, &h.seq, sizeof(h) - sizeof(MessageHeader))) break;
        std::vector<uint8_t> payload(h.payloadSize);
        if (!recv_all(gSock, payload.data(), payload.size())) break;

        if (!useRaw) {
          ++skippedQueued;
          continue;
        }

        const uint64_t nowUs = qpc_now_us();
        {
          std::lock_guard<std::mutex> lk(gFrame.mu);
          gFrame.width = h.width;
          gFrame.height = h.height;
          gFrame.stride = h.stride;
          gFrame.seq = h.seq;
          gFrame.captureUs = h.captureQpcUs;
          gFrame.encodeStartUs = h.encodeStartQpcUs;
          gFrame.encodeEndUs = h.encodeEndQpcUs;
          gFrame.sendUs = h.sendQpcUs;
          gFrame.recvUs = nowUs;
          gFrame.decodeStartUs = nowUs;
          gFrame.decodeEndUs = nowUs;
          gFrame.version += 1;
          gFrame.bytes.swap(payload);
        }
        if (gHwnd && !gPaintQueued.exchange(true)) {
          InvalidateRect(gHwnd, nullptr, FALSE);
        }

        if (args.traceEvery > 0 && (h.seq % args.traceEvery) == 0 &&
            (args.traceMax == 0 || gTraceRecvPrinted.load() < args.traceMax)) {
          const auto nowPrinted = gTraceRecvPrinted.fetch_add(1) + 1;
          if (args.traceMax == 0 || nowPrinted <= args.traceMax) {
            std::cout << "[native-video-client][trace_recv] seq=" << h.seq
                      << " captureUs=" << h.captureQpcUs
                      << " encodeStartUs=" << h.encodeStartQpcUs
                      << " encodeEndUs=" << h.encodeEndQpcUs
                      << " sendUs=" << h.sendQpcUs
                      << " recvUs=" << nowUs
                      << " decodeStartUs=" << nowUs
                      << " decodeEndUs=" << nowUs
                      << " c2eUs=" << ((h.encodeStartQpcUs >= h.captureQpcUs) ? (h.encodeStartQpcUs - h.captureQpcUs) : 0)
                      << " encUs=" << ((h.encodeEndQpcUs >= h.encodeStartQpcUs) ? (h.encodeEndQpcUs - h.encodeStartQpcUs) : 0)
                      << " e2sUs=" << ((h.sendQpcUs >= h.encodeEndQpcUs) ? (h.sendQpcUs - h.encodeEndQpcUs) : 0)
                      << " netUs=" << ((nowUs >= h.sendQpcUs) ? (nowUs - h.sendQpcUs) : 0)
                      << " r2dUs=0"
                      << " decUs=0"
                      << " bytes=" << h.payloadSize
                      << "\n";
          }
        }

        ++recvFrames;
        ++decodedFrames;
        recvBytes += h.payloadSize;
        const uint64_t latencyUs = (nowUs >= h.captureQpcUs) ? (nowUs - h.captureQpcUs) : 0;
        const uint64_t decodeTailUs = (nowUs >= h.sendQpcUs) ? (nowUs - h.sendQpcUs) : 0;
        sumLatencyUs += latencyUs;
        sumDecodeTailUs += decodeTailUs;
        maxLatencyUs = std::max(maxLatencyUs, latencyUs);
        maxDecodeTailUs = std::max(maxDecodeTailUs, decodeTailUs);

        if (nowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (recvFrames > 0) ? (sumLatencyUs / recvFrames) : 0;
          const uint64_t avgDecodeTailUs = (recvFrames > 0) ? (sumDecodeTailUs / recvFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          std::cout << "[native-video-client] recvFrames=" << recvFrames
                    << " decodedFrames=" << decodedFrames
                    << " skippedQueued=" << skippedQueued
                    << " avgLatencyUs=" << avgLatencyUs
                    << " maxLatencyUs=" << maxLatencyUs
                    << " avgDecodeTailUs=" << avgDecodeTailUs
                    << " maxDecodeTailUs=" << maxDecodeTailUs
                    << " mbps=" << mbps
                    << " size=" << h.width << "x" << h.height
                    << "\n";
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
      } else if (msgType == MessageType::EncodedFrameH264 && header.size == sizeof(EncodedFrameHeader)) {
        EncodedFrameHeader h{};
        h.header = header;
        if (!recv_all(gSock, &h.seq, sizeof(h) - sizeof(MessageHeader))) break;
        std::vector<uint8_t> payload(h.payloadSize);
        if (!recv_all(gSock, payload.data(), payload.size())) break;
        const uint64_t packetNowUs = qpc_now_us();
        if (!process_h264_frame(h, &payload, packetNowUs)) break;
      } else {
        const size_t bodySize = static_cast<size_t>(header.size - sizeof(header));
        if (bodySize > 0 && !recv_discard(gSock, bodySize)) break;
        ++skippedQueued;
      }

      const uint64_t nowUs = qpc_now_us();
      if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
        break;
      }
    }
    gRunning = false;
    if (gHwnd) PostMessage(gHwnd, WM_CLOSE, 0, 0);
  });

  MSG msg{};
  while (gRunning.load()) {
    bool hadMessage = false;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      hadMessage = true;
      if (msg.message == WM_QUIT) {
        gRunning = false;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    if (!gRunning.load()) break;

    if (args.seconds > 0) {
      const uint64_t nowUs = qpc_now_us();
      if (nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
        gRunning = false;
        break;
      }
    }

    if (!hadMessage) {
      Sleep(5);
    }
  }

  gRunning = false;
  gInputEnabled = false;
  if (gSock != INVALID_SOCKET) {
    shutdown(gSock, SD_BOTH);
    closesocket(gSock);
    gSock = INVALID_SOCKET;
  }
  if (controlSock != INVALID_SOCKET) {
    shutdown(controlSock, SD_BOTH);
    closesocket(controlSock);
    controlSock = INVALID_SOCKET;
  }
  if (controlThread.joinable()) controlThread.join();
  if (recvThread.joinable()) recvThread.join();

  if (useH264) {
    decoder.shutdown();
    if (mfStarted) MFShutdown();
  }

  std::cout << "[native-video-client] done\n";
  return 0;
}


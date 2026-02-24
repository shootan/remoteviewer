#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mf_h264_codec.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

namespace {

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using remote60::native_poc::ControlInputAckMessage;
using remote60::native_poc::ControlInputEventMessage;
using remote60::native_poc::ControlClientMetricsMessage;
using remote60::native_poc::ControlRequestKeyFrameMessage;
using remote60::native_poc::ControlPingMessage;
using remote60::native_poc::ControlPongMessage;
using remote60::native_poc::EncodedFrameHeader;
using remote60::native_poc::H264AccessUnit;
using remote60::native_poc::H264Encoder;
using remote60::native_poc::MessageHeader;
using remote60::native_poc::MessageType;
using remote60::native_poc::RawFrameHeader;
using remote60::native_poc::UdpCodec;
using remote60::native_poc::UdpHelloPacket;
using remote60::native_poc::UdpPacketKind;
using remote60::native_poc::UdpVideoChunkHeader;
using remote60::native_poc::VideoTransport;
using remote60::native_poc::bgra_to_nv12;
using remote60::native_poc::clamp_udp_mtu;
using remote60::native_poc::parse_video_transport;
using remote60::native_poc::qpc_now_us;
using remote60::native_poc::video_transport_name;

#ifndef REMOTE60_NATIVE_ENCODED_EXPERIMENT
#define REMOTE60_NATIVE_ENCODED_EXPERIMENT 0
#endif
constexpr bool kAllInputBlocked = true;
constexpr uint64_t kMaxEncodedFrameAgeUs = 250000;  // 250ms
constexpr uint32_t kMaxConsecutiveStaleEncodedFrames = 8;
constexpr int kCaptureFramePoolBuffersDefault = 2;
constexpr uint64_t kMaxPreEncodeFrameAgeUs = 25000;  // 25ms

winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateItemForPrimaryMonitor() {
  HMONITOR monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
  if (!monitor) return nullptr;
  auto interop = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                                               IGraphicsCaptureItemInterop>();
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
  interop->CreateForMonitor(monitor, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                            winrt::put_abi(item));
  return item;
}

Microsoft::WRL::ComPtr<ID3D11Texture2D> SurfaceToTexture(
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface const& surface) {
  winrt::com_ptr<::IInspectable> inspectable = surface.as<::IInspectable>();
  Microsoft::WRL::ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
  winrt::check_hresult(inspectable->QueryInterface(IID_PPV_ARGS(&access)));
  Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
  winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),
                                            reinterpret_cast<void**>(tex.GetAddressOf())));
  return tex;
}

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
  uint16_t bindPort = 43000;
  uint16_t controlPort = 0;
  uint32_t tcpSendBufKb = 0;
  uint32_t udpMtu = 1200;
  uint32_t traceEvery = 0;
  uint32_t traceMax = 0;
  uint32_t inputLogEvery = 120;
  std::string transport;
  std::string codec = "raw";
  uint32_t fps = 30;
  uint32_t seconds = 0;  // 0: infinite
  uint32_t bitrate = 1100000;
  uint32_t keyint = 15;
  uint32_t encodeWidth = 0;
  uint32_t encodeHeight = 0;
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
    if (k == "--bind-port" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.bindPort = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
    } else if (k == "--control-port" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.controlPort = static_cast<uint16_t>(std::min<uint32_t>(v, 65535));
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
    } else if (k == "--input-log-every" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.inputLogEvery = std::max<uint32_t>(1, v);
    } else if (k == "--codec" && i + 1 < argc) {
      a.codec = argv[++i];
    } else if (k == "--transport" && i + 1 < argc) {
      a.transport = argv[++i];
    } else if (k == "--fps" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.fps = std::clamp<uint32_t>(v, 1, 120);
    } else if (k == "--seconds" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.seconds = v;
    } else if (k == "--bitrate" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.bitrate = std::max<uint32_t>(100000, v);
    } else if (k == "--keyint" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.keyint = std::max<uint32_t>(1, v);
    } else if (k == "--encode-width" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.encodeWidth = v;
    } else if (k == "--encode-height" && i + 1 < argc) {
      uint32_t v = 0;
      if (parse_u32(argv[++i], &v)) a.encodeHeight = v;
    }
  }
  return a;
}

uint32_t clamp_even_dim(uint32_t v, uint32_t minValue, uint32_t maxValue) {
  if (v < minValue) v = minValue;
  if (v > maxValue) v = maxValue;
  if (v & 1u) {
    if (v < maxValue) {
      ++v;
    } else if (v > minValue) {
      --v;
    }
  }
  return v;
}

void choose_h264_encode_size(const Args& args, uint32_t captureW, uint32_t captureH,
                             uint32_t* outW, uint32_t* outH, bool* outAutoFallback720) {
  if (!outW || !outH || !outAutoFallback720) return;
  *outAutoFallback720 = false;
  uint32_t targetW = captureW;
  uint32_t targetH = captureH;
  if (args.encodeWidth > 0 && args.encodeHeight > 0) {
    targetW = clamp_even_dim(args.encodeWidth, 2, captureW);
    targetH = clamp_even_dim(args.encodeHeight, 2, captureH);
  } else {
    // At low bitrate, avoid encoder queue buildup by auto-falling back toward 720p.
    if (args.bitrate <= 1500000 && (captureW > 1280 || captureH > 720)) {
      const double sx = 1280.0 / static_cast<double>(captureW);
      const double sy = 720.0 / static_cast<double>(captureH);
      const double scale = std::min(sx, sy);
      if (scale > 0.0 && scale < 1.0) {
        targetW = static_cast<uint32_t>(captureW * scale);
        targetH = static_cast<uint32_t>(captureH * scale);
        targetW = clamp_even_dim(targetW, 2, captureW);
        targetH = clamp_even_dim(targetH, 2, captureH);
        *outAutoFallback720 = true;
      }
    }
  }
  *outW = targetW;
  *outH = targetH;
}

void choose_abr_720_size(uint32_t captureW, uint32_t captureH, uint32_t* outW, uint32_t* outH) {
  if (!outW || !outH) return;
  uint32_t targetW = captureW;
  uint32_t targetH = captureH;
  if (captureW > 1280 || captureH > 720) {
    const double sx = 1280.0 / static_cast<double>(captureW);
    const double sy = 720.0 / static_cast<double>(captureH);
    const double scale = std::min(sx, sy);
    if (scale > 0.0 && scale < 1.0) {
      targetW = static_cast<uint32_t>(captureW * scale);
      targetH = static_cast<uint32_t>(captureH * scale);
    }
  }
  targetW = clamp_even_dim(targetW, 2, captureW);
  targetH = clamp_even_dim(targetH, 2, captureH);
  *outW = targetW;
  *outH = targetH;
}

bool resize_bgra_nearest(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint32_t srcStride,
                         uint32_t dstW, uint32_t dstH, std::vector<uint8_t>* outBgra) {
  if (!src || srcW == 0 || srcH == 0 || srcStride < (srcW * 4) || dstW == 0 || dstH == 0 || !outBgra) {
    return false;
  }
  outBgra->resize(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4);
  auto* dst = outBgra->data();
  const uint64_t xScale = (static_cast<uint64_t>(srcW) << 16) / dstW;
  const uint64_t yScale = (static_cast<uint64_t>(srcH) << 16) / dstH;
  for (uint32_t y = 0; y < dstH; ++y) {
    const uint32_t sy = std::min<uint32_t>(srcH - 1, static_cast<uint32_t>((y * yScale) >> 16));
    const uint8_t* srcRow = src + static_cast<size_t>(sy) * srcStride;
    uint8_t* dstRow = dst + static_cast<size_t>(y) * dstW * 4;
    for (uint32_t x = 0; x < dstW; ++x) {
      const uint32_t sx = std::min<uint32_t>(srcW - 1, static_cast<uint32_t>((x * xScale) >> 16));
      const uint8_t* px = srcRow + static_cast<size_t>(sx) * 4;
      uint8_t* outPx = dstRow + static_cast<size_t>(x) * 4;
      outPx[0] = px[0];
      outPx[1] = px[1];
      outPx[2] = px[2];
      outPx[3] = px[3];
    }
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

bool send_udp_chunks(SOCKET s, const sockaddr_in& peer, const uint8_t* payload, size_t payloadSize,
                     const UdpVideoChunkHeader& baseHeader, uint32_t mtuBytes) {
  if (!payload || payloadSize == 0 || s == INVALID_SOCKET) return false;
  const uint32_t safeMtu = clamp_udp_mtu(mtuBytes);
  if (safeMtu <= sizeof(UdpVideoChunkHeader)) return false;
  const uint32_t maxChunk = safeMtu - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
  std::vector<uint8_t> datagram(safeMtu);
  size_t offset = 0;

  while (offset < payloadSize) {
    const uint32_t chunkSize = static_cast<uint32_t>(std::min<size_t>(maxChunk, payloadSize - offset));
    UdpVideoChunkHeader h = baseHeader;
    h.chunkOffset = static_cast<uint32_t>(offset);
    h.chunkSize = chunkSize;
    h.flags &= static_cast<uint16_t>(~(0x2u | 0x4u));
    if (offset == 0) h.flags |= 0x2u;
    if (offset + chunkSize >= payloadSize) h.flags |= 0x4u;
    std::memcpy(datagram.data(), &h, sizeof(h));
    std::memcpy(datagram.data() + sizeof(h), payload + offset, chunkSize);
    const int n = sendto(s, reinterpret_cast<const char*>(datagram.data()),
                         static_cast<int>(sizeof(h) + chunkSize), 0,
                         reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
    if (n <= 0) return false;
    offset += chunkSize;
  }
  return true;
}

struct FrameState {
  std::mutex mu;
  std::condition_variable cv;
  uint64_t version = 0;
  uint32_t seq = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  uint64_t captureUs = 0;
  uint64_t callbackUs = 0;
  uint64_t captureAgeAtCallbackUs = 0;
  std::shared_ptr<std::vector<uint8_t>> payload;
};

}  // namespace

int main(int argc, char** argv) {
  std::cout.setf(std::ios::unitbuf);
  std::cerr.setf(std::ios::unitbuf);

  const Args args = parse_args(argc, argv);
  const bool useRaw = (args.codec == "raw");
  const bool useH264 = (args.codec == "h264");
  const bool guardStaleEncoded = env_truthy("REMOTE60_NATIVE_GUARD_STALE_ENCODED");
  const bool noPacingH264 = env_truthy("REMOTE60_NATIVE_H264_NO_PACING");
  const bool guardStalePreEncode = env_truthy("REMOTE60_NATIVE_GUARD_STALE_PREENCODE");
  const bool abrEnabled = useH264 && !env_truthy("REMOTE60_NATIVE_ABR_DISABLE");
  int captureFramePoolBuffers = kCaptureFramePoolBuffersDefault;
  if (const char* poolEnv = std::getenv("REMOTE60_NATIVE_CAPTURE_POOL_BUFFERS")) {
    const int requested = std::atoi(poolEnv);
    if (requested >= 1 && requested <= 4) {
      captureFramePoolBuffers = requested;
    }
  }
  const bool encodedExperimentEnabled =
      (REMOTE60_NATIVE_ENCODED_EXPERIMENT != 0) || env_truthy("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE");

  if (!useRaw && !useH264) {
    std::cerr << "[native-video-host] unsupported codec: " << args.codec << " (supported: raw,h264)\n";
    return 11;
  }
  if (useH264 && !encodedExperimentEnabled) {
    std::cerr << "[native-video-host] unsupported codec: " << args.codec
              << " (enable REMOTE60_NATIVE_ENCODED_EXPERIMENT or set env REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1)\n";
    return 11;
  }

  WinsockScope ws;
  if (!ws.ok) {
    std::cerr << "[native-video-host] WSAStartup failed\n";
    return 1;
  }
  std::string effectiveTransport = args.transport;
  if (effectiveTransport.empty()) {
    effectiveTransport = useH264 ? "udp" : "tcp";
  }
  VideoTransport transport = VideoTransport::Tcp;
  if (!parse_video_transport(effectiveTransport, &transport)) {
    std::cerr << "[native-video-host] unsupported transport: " << effectiveTransport << " (supported: tcp,udp)\n";
    return 15;
  }
  if (transport == VideoTransport::Udp && useRaw) {
    std::cerr << "[native-video-host] raw codec over udp is not supported in current phase (use codec=h264)\n";
    return 16;
  }

  std::cout << "[native-video-host] waiting client bindPort=" << args.bindPort
            << " transport=" << video_transport_name(transport)
            << " fps=" << args.fps;
  if (useH264) std::cout << " bitrate=" << args.bitrate;
  std::cout << " seconds=" << args.seconds << "\n";
  if (useH264) {
    std::cout << "[native-video-host] h264 pacing=" << (noPacingH264 ? "off" : "on")
              << " stalePreEncodeGuard=" << (guardStalePreEncode ? 1 : 0)
              << " capturePoolBuffers=" << captureFramePoolBuffers
              << " abr=" << (abrEnabled ? "on" : "off") << "\n";
  }
  if (kAllInputBlocked) {
    std::cout << "[native-video-host] all input blocked (view-only)\n";
  }

  SOCKET clientSock = INVALID_SOCKET;
  sockaddr_in udpPeer{};
  bool udpPeerReady = false;
  if (transport == VideoTransport::Tcp) {
    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] listen socket create failed\n";
      return 2;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(args.bindPort);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listenSock, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
      std::cerr << "[native-video-host] bind failed port=" << args.bindPort << "\n";
      closesocket(listenSock);
      return 3;
    }
    if (listen(listenSock, 1) != 0) {
      std::cerr << "[native-video-host] listen failed\n";
      closesocket(listenSock);
      return 4;
    }

    sockaddr_in peer{};
    int peerLen = sizeof(peer);
    clientSock = accept(listenSock, reinterpret_cast<sockaddr*>(&peer), &peerLen);
    closesocket(listenSock);
    if (clientSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] accept failed\n";
      return 5;
    }

    int noDelay = 1;
    setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
  } else {
    clientSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (clientSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] udp socket create failed\n";
      return 2;
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(args.bindPort);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(clientSock, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
      std::cerr << "[native-video-host] udp bind failed port=" << args.bindPort << "\n";
      closesocket(clientSock);
      return 3;
    }

    for (;;) {
      UdpHelloPacket hello{};
      sockaddr_in peer{};
      int peerLen = sizeof(peer);
      const int n = recvfrom(clientSock, reinterpret_cast<char*>(&hello), sizeof(hello), 0,
                             reinterpret_cast<sockaddr*>(&peer), &peerLen);
      if (n <= 0) {
        std::cerr << "[native-video-host] udp handshake recv failed\n";
        closesocket(clientSock);
        return 5;
      }
      if (n < static_cast<int>(sizeof(UdpHelloPacket)) ||
          hello.magic != remote60::native_poc::kMagic ||
          hello.kind != static_cast<uint16_t>(UdpPacketKind::Hello)) {
        continue;
      }

      UdpHelloPacket ack{};
      ack.kind = static_cast<uint16_t>(UdpPacketKind::HelloAck);
      (void)sendto(clientSock, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
                   reinterpret_cast<const sockaddr*>(&peer), peerLen);
      udpPeer = peer;
      udpPeerReady = true;
      break;
    }
  }

  if (transport == VideoTransport::Udp && args.tcpSendBufKb == 0) {
    const int sendBuf = 1024 * 1024;
    (void)setsockopt(clientSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
  }
  if (args.tcpSendBufKb > 0) {
    const int sendBuf = static_cast<int>(args.tcpSendBufKb * 1024u);
    setsockopt(clientSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
  }
  int effectiveSendBuf = 0;
  int effectiveSendBufLen = sizeof(effectiveSendBuf);
  (void)getsockopt(clientSock, SOL_SOCKET, SO_SNDBUF,
                   reinterpret_cast<char*>(&effectiveSendBuf), &effectiveSendBufLen);
  std::cout << "[native-video-host] client connected transport=" << video_transport_name(transport) << "\n";
  std::cout << "[native-video-host] socket sndbuf=" << effectiveSendBuf << " bytes\n";

  std::atomic<bool> stop{false};
  std::atomic<uint64_t> inputEvents{0};
  std::atomic<uint64_t> clientMetricsUpdatedUs{0};
  std::atomic<uint32_t> clientMetricsWidth{0};
  std::atomic<uint32_t> clientMetricsHeight{0};
  std::atomic<uint32_t> clientMetricsRecvFpsX100{0};
  std::atomic<uint32_t> clientMetricsDecodedFpsX100{0};
  std::atomic<uint32_t> clientMetricsRecvMbpsX1000{0};
  std::atomic<uint32_t> clientMetricsSkippedFrames{0};
  std::atomic<uint64_t> clientMetricsAvgLatencyUs{0};
  std::atomic<uint64_t> clientMetricsMaxLatencyUs{0};
  std::atomic<uint64_t> clientMetricsAvgDecodeTailUs{0};
  std::atomic<uint64_t> clientMetricsMaxDecodeTailUs{0};
  std::atomic<bool> clientRequestedKeyFrame{false};
  std::atomic<uint16_t> clientKeyFrameReason{0};
  std::atomic<uint64_t> clientKeyFrameRequestCount{0};
  SOCKET controlListenSock = INVALID_SOCKET;
  SOCKET controlClientSock = INVALID_SOCKET;
  std::thread controlThread;
  if (args.controlPort > 0) {
    controlListenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (controlListenSock == INVALID_SOCKET) {
      std::cerr << "[native-video-host] control listen socket create failed port=" << args.controlPort << "\n";
    } else {
      sockaddr_in ctlLocal{};
      ctlLocal.sin_family = AF_INET;
      ctlLocal.sin_port = htons(args.controlPort);
      ctlLocal.sin_addr.s_addr = htonl(INADDR_ANY);
      if (bind(controlListenSock, reinterpret_cast<const sockaddr*>(&ctlLocal), sizeof(ctlLocal)) != 0 ||
          listen(controlListenSock, 1) != 0) {
        std::cerr << "[native-video-host] control bind/listen failed port=" << args.controlPort << "\n";
        closesocket(controlListenSock);
        controlListenSock = INVALID_SOCKET;
      } else {
        std::cout << "[native-video-host] control waiting port=" << args.controlPort << "\n";
        controlThread = std::thread([&]() {
          sockaddr_in cpeer{};
          int cpeerLen = sizeof(cpeer);
          controlClientSock = accept(controlListenSock, reinterpret_cast<sockaddr*>(&cpeer), &cpeerLen);
          if (controlClientSock == INVALID_SOCKET) return;
          int ctlNoDelay = 1;
          setsockopt(controlClientSock, IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char*>(&ctlNoDelay), sizeof(ctlNoDelay));
          while (!stop.load()) {
            MessageHeader header{};
            if (!recv_all(controlClientSock, &header, sizeof(header))) break;
            if (header.magic != remote60::native_poc::kMagic || header.size < sizeof(header)) break;
            const size_t bodySize = static_cast<size_t>(header.size - sizeof(header));
            const auto type = static_cast<MessageType>(header.type);

            if (type == MessageType::ControlPing && header.size == sizeof(ControlPingMessage)) {
              ControlPingMessage ping{};
              ping.header = header;
              if (!recv_all(controlClientSock, &ping.seq, sizeof(ping) - sizeof(MessageHeader))) break;
              ControlPongMessage pong{};
              pong.header.magic = remote60::native_poc::kMagic;
              pong.header.type = static_cast<uint16_t>(MessageType::ControlPong);
              pong.header.size = static_cast<uint16_t>(sizeof(pong));
              pong.seq = ping.seq;
              pong.clientSendQpcUs = ping.clientSendQpcUs;
              pong.hostRecvQpcUs = qpc_now_us();
              pong.hostSendQpcUs = qpc_now_us();
              if (!send_all(controlClientSock, &pong, sizeof(pong))) break;
              continue;
            }

            if (type == MessageType::ControlInputEvent && header.size == sizeof(ControlInputEventMessage)) {
              ControlInputEventMessage input{};
              input.header = header;
              if (!recv_all(controlClientSock, &input.seq, sizeof(input) - sizeof(MessageHeader))) break;
              if (!kAllInputBlocked) {
                const auto n = inputEvents.fetch_add(1) + 1;
                if (args.inputLogEvery > 0 && (n % args.inputLogEvery) == 0) {
                  std::cout << "[native-video-host][input] seq=" << input.seq
                            << " kind=" << input.kind
                            << " x=" << input.x
                            << " y=" << input.y
                            << " buttons=" << input.buttons
                            << " key=" << input.keyCode
                            << "\n";
                }
              } else if (args.inputLogEvery > 0 && (input.seq % args.inputLogEvery) == 0) {
                std::cout << "[native-video-host][input] blocked seq=" << input.seq
                          << " key=" << input.keyCode
                          << " kind=" << input.kind
                          << "\n";
              }
              ControlInputAckMessage ack{};
              ack.header.magic = remote60::native_poc::kMagic;
              ack.header.type = static_cast<uint16_t>(MessageType::ControlInputAck);
              ack.header.size = static_cast<uint16_t>(sizeof(ack));
              ack.seq = input.seq;
              ack.hostRecvQpcUs = qpc_now_us();
              ack.hostSendQpcUs = qpc_now_us();
              if (!send_all(controlClientSock, &ack, sizeof(ack))) break;
              continue;
            }

            if (type == MessageType::ControlClientMetrics &&
                header.size == sizeof(ControlClientMetricsMessage)) {
              ControlClientMetricsMessage metrics{};
              metrics.header = header;
              if (!recv_all(controlClientSock, &metrics.seq, sizeof(metrics) - sizeof(MessageHeader))) break;
              clientMetricsWidth = metrics.width;
              clientMetricsHeight = metrics.height;
              clientMetricsRecvFpsX100 = metrics.recvFpsX100;
              clientMetricsDecodedFpsX100 = metrics.decodedFpsX100;
              clientMetricsRecvMbpsX1000 = metrics.recvMbpsX1000;
              clientMetricsSkippedFrames = metrics.skippedFrames;
              clientMetricsAvgLatencyUs = metrics.avgLatencyUs;
              clientMetricsMaxLatencyUs = metrics.maxLatencyUs;
              clientMetricsAvgDecodeTailUs = metrics.avgDecodeTailUs;
              clientMetricsMaxDecodeTailUs = metrics.maxDecodeTailUs;
              clientMetricsUpdatedUs = qpc_now_us();
              continue;
            }

            if (type == MessageType::ControlRequestKeyFrame &&
                header.size == sizeof(ControlRequestKeyFrameMessage)) {
              ControlRequestKeyFrameMessage req{};
              req.header = header;
              if (!recv_all(controlClientSock, &req.seq, sizeof(req) - sizeof(MessageHeader))) break;
              clientRequestedKeyFrame = true;
              clientKeyFrameReason = req.reason;
              const uint64_t reqCount = clientKeyFrameRequestCount.fetch_add(1) + 1;
              std::cout << "[native-video-host][control] keyframe-request seq=" << req.seq
                        << " reason=" << req.reason
                        << " total=" << reqCount
                        << "\n";
              continue;
            }

            if (bodySize > 0 && !recv_discard(controlClientSock, bodySize)) break;
          }
        });
      }
    }
  }

  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  if (!GraphicsCaptureSession::IsSupported()) {
    std::cerr << "[native-video-host] WGC not supported\n";
    closesocket(clientSock);
    return 6;
  }

  bool mfStarted = false;
  H264Encoder encoder;
  if (useH264) {
    const HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
      std::cerr << "[native-video-host] MFStartup failed hr=0x" << std::hex << static_cast<unsigned long>(hr)
                << std::dec << "\n";
      closesocket(clientSock);
      return 12;
    }
    mfStarted = true;
  }

  Microsoft::WRL::ComPtr<ID3D11Device> d3d;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
  D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, &d3d, &fl, &ctx);
  if (FAILED(hr)) {
    std::cerr << "[native-video-host] D3D11CreateDevice failed\n";
    closesocket(clientSock);
    if (mfStarted) MFShutdown();
    return 7;
  }
  if (useH264) {
    (void)encoder.set_d3d11_device(d3d.Get());
  }

  auto item = CreateItemForPrimaryMonitor();
  if (!item) {
    std::cerr << "[native-video-host] capture item create failed\n";
    closesocket(clientSock);
    if (mfStarted) MFShutdown();
    return 8;
  }

  const auto size = item.Size();
  const uint32_t width = static_cast<uint32_t>(size.Width);
  const uint32_t height = static_cast<uint32_t>(size.Height);
  if (width < 2 || height < 2) {
    std::cerr << "[native-video-host] invalid capture size\n";
    closesocket(clientSock);
    if (mfStarted) MFShutdown();
    return 9;
  }

  uint32_t encodeW = width;
  uint32_t encodeH = height;
  bool autoFallback720 = false;
  if (useH264) {
    choose_h264_encode_size(args, width, height, &encodeW, &encodeH, &autoFallback720);
  }

  const uint32_t abrHighW = encodeW;
  const uint32_t abrHighH = encodeH;
  const uint32_t abrMidW = abrHighW;
  const uint32_t abrMidH = abrHighH;
  uint32_t abrLowW = abrHighW;
  uint32_t abrLowH = abrHighH;
  if (useH264) {
    choose_abr_720_size(abrHighW, abrHighH, &abrLowW, &abrLowH);
  }
  const bool abrHasLowerResolution = (abrLowW < abrHighW || abrLowH < abrHighH);
  const uint32_t abrHighBitrate = args.bitrate;
  const uint32_t abrMidBitrate = std::min<uint32_t>(
      abrHighBitrate, std::max<uint32_t>(2000000u, (abrHighBitrate * 75u) / 100u));
  const uint32_t abrLowBitrate = std::min<uint32_t>(
      abrHighBitrate, std::max<uint32_t>(1500000u, (abrHighBitrate * 55u) / 100u));
  const bool abrHasMidProfile = (abrMidBitrate < abrHighBitrate);
  const bool abrHasLowProfile = abrHasLowerResolution || (abrLowBitrate < abrMidBitrate);
  int abrProfile = 0;  // 0: high, 1: mid, 2: low
  uint32_t activeEncodeW = abrHighW;
  uint32_t activeEncodeH = abrHighH;
  uint32_t activeBitrate = abrHighBitrate;
  uint64_t abrCooldownUntilUs = 0;
  uint32_t abrGoodSeconds = 0;
  uint32_t abrModeratePressureSeconds = 0;
  uint32_t abrSeverePressureSeconds = 0;

  if (useH264) {
    if (!encoder.initialize(activeEncodeW, activeEncodeH, args.fps, activeBitrate, args.keyint)) {
      std::cerr << "[native-video-host] H264 encoder initialize failed\n";
      closesocket(clientSock);
      if (mfStarted) MFShutdown();
      return 13;
    }
    std::cout << "[native-video-host] H264 encoder backend=" << encoder.backend_name()
              << " hw=" << (encoder.using_hardware() ? 1 : 0)
              << " captureSize=" << width << "x" << height
              << " encodeSize=" << activeEncodeW << "x" << activeEncodeH
              << " auto720=" << (autoFallback720 ? 1 : 0)
              << " abrMidProfile=" << abrMidW << "x" << abrMidH
              << " abrMidBitrate=" << abrMidBitrate
              << " abrLowProfile=" << abrLowW << "x" << abrLowH
              << " abrLowBitrate=" << abrLowBitrate
              << "\n";
  }

  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
  d3d.As(&dxgi);
  winrt::com_ptr<::IInspectable> inspectable;
  winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
  auto d3dDevice = inspectable.as<IDirect3DDevice>();

  auto pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
      d3dDevice, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
      captureFramePoolBuffers, size);
  auto session = pool.CreateCaptureSession(item);

  D3D11_TEXTURE2D_DESC stDesc{};
  stDesc.Width = width;
  stDesc.Height = height;
  stDesc.MipLevels = 1;
  stDesc.ArraySize = 1;
  stDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  stDesc.SampleDesc.Count = 1;
  stDesc.Usage = D3D11_USAGE_STAGING;
  stDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  if (FAILED(d3d->CreateTexture2D(&stDesc, nullptr, &staging)) || !staging) {
    std::cerr << "[native-video-host] staging texture create failed\n";
    closesocket(clientSock);
    if (mfStarted) MFShutdown();
    return 10;
  }

  FrameState frame;
  std::atomic<uint64_t> callbackFrames{0};
  std::atomic<int64_t> captureClockOffsetUs{std::numeric_limits<int64_t>::max()};

  auto token = pool.FrameArrived([&](Direct3D11CaptureFramePool const& sender,
                                     winrt::Windows::Foundation::IInspectable const&) {
    if (stop.load()) return;
    try {
      auto latest = sender.TryGetNextFrame();
      if (!latest) return;
      // Drain queued frames and keep only the newest one to avoid stale-frame backlog.
      while (auto newer = sender.TryGetNextFrame()) {
        latest = newer;
      }

      auto src = SurfaceToTexture(latest.Surface());
      if (!src) return;
      ctx->CopyResource(staging.Get(), src.Get());
      D3D11_MAPPED_SUBRESOURCE map{};
      if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return;
      const uint32_t stride = width * 4;
      auto payload = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(stride) * height);
      auto* dst = payload->data();
      auto* srcRow = reinterpret_cast<const uint8_t*>(map.pData);
      for (uint32_t y = 0; y < height; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * stride,
                    srcRow + static_cast<size_t>(y) * map.RowPitch, stride);
      }
      ctx->Unmap(staging.Get(), 0);
      const uint64_t callbackUs = qpc_now_us();
      uint64_t sourceCaptureUs = callbackUs;
      uint64_t captureAgeAtCallbackUs = 0;
      // Align WGC frame timestamp to qpc_now_us domain using a minimum-offset estimator.
      const auto relTime = latest.SystemRelativeTime();
      const int64_t t100ns = relTime.count();
      if (t100ns > 0) {
        const int64_t wgcUs = t100ns / 10;
        if (static_cast<int64_t>(callbackUs) >= wgcUs) {
          captureAgeAtCallbackUs = static_cast<uint64_t>(static_cast<int64_t>(callbackUs) - wgcUs);
        }
        const int64_t offsetCandidate = static_cast<int64_t>(callbackUs) - wgcUs;
        if (offsetCandidate > 0) {
          int64_t cur = captureClockOffsetUs.load();
          while (offsetCandidate < cur && !captureClockOffsetUs.compare_exchange_weak(cur, offsetCandidate)) {
          }
          const int64_t bestOffset = captureClockOffsetUs.load();
          if (bestOffset != std::numeric_limits<int64_t>::max()) {
            const int64_t aligned = wgcUs + bestOffset;
            if (aligned > 0) {
              sourceCaptureUs = static_cast<uint64_t>(aligned);
            }
          }
        }
      }
      {
        std::lock_guard<std::mutex> lk(frame.mu);
        frame.payload = std::move(payload);
        frame.width = width;
        frame.height = height;
        frame.stride = stride;
        frame.captureUs = sourceCaptureUs;
        frame.callbackUs = callbackUs;
        frame.captureAgeAtCallbackUs = captureAgeAtCallbackUs;
        frame.seq += 1;
        frame.version += 1;
      }
      callbackFrames += 1;
      frame.cv.notify_one();
    } catch (...) {
    }
  });

  session.StartCapture();

  const uint64_t frameIntervalUs = std::max<uint64_t>(1, 1000000ULL / args.fps);
  const uint64_t startUs = qpc_now_us();
  uint64_t nextTickUs = startUs;
  // For encoded path, latency is prioritized over strict send pacing.
  // Raw path keeps legacy pacing to avoid excessive CPU/bandwidth burst.
  const bool paceByTick = useRaw || !noPacingH264;
  uint64_t statAtUs = startUs + 1000000ULL;
  uint64_t sentFrames = 0;
  uint64_t encodedFrames = 0;
  uint64_t sentBytes = 0;
  uint64_t rawEquivalentBytes = 0;
  uint64_t skippedByOverwrite = 0;
  uint64_t lastVersionSent = 0;
  uint64_t tracePrinted = 0;
  uint32_t encodedSeq = 0;
  uint64_t encodeFailCount = 0;
  uint64_t staleEncodedDropCount = 0;
  uint64_t stalePreEncodeDropCount = 0;
  uint64_t encoderResetCount = 0;
  uint64_t captureAgeSumUs = 0;
  uint64_t captureAgeMaxUs = 0;
  uint64_t callbackToEncodeStartSumUs = 0;
  uint64_t callbackToEncodeStartMaxUs = 0;
  uint32_t consecutiveStaleEncodedFrames = 0;
  bool forceKeyNext = true;

  while (!stop.load()) {
    const uint64_t nowUs = qpc_now_us();
    if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
      break;
    }
    if (paceByTick) {
      if (nowUs < nextTickUs) {
        Sleep(1);
        continue;
      }
      nextTickUs += frameIntervalUs;
    }

    std::shared_ptr<std::vector<uint8_t>> payload;
    uint32_t seq = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    uint32_t stride = 0;
    uint64_t captureUs = 0;
    uint64_t callbackUs = 0;
    uint64_t captureAgeAtCallbackUs = 0;
    uint64_t version = 0;
    {
      std::unique_lock<std::mutex> lk(frame.mu);
      frame.cv.wait_for(lk, std::chrono::milliseconds(100), [&] {
        return stop.load() || frame.version != lastVersionSent;
      });
      if (stop.load()) break;
      if (frame.version == lastVersionSent || !frame.payload || frame.payload->empty()) {
        continue;
      }
      version = frame.version;
      payload = frame.payload;
      seq = frame.seq;
      w = frame.width;
      h = frame.height;
      stride = frame.stride;
      captureUs = frame.captureUs;
      callbackUs = frame.callbackUs;
      captureAgeAtCallbackUs = frame.captureAgeAtCallbackUs;
    }
    const uint64_t frameAgeAtSelectUs =
        (callbackUs > 0 && nowUs >= callbackUs) ? (nowUs - callbackUs) : 0;
    if (useH264 && guardStalePreEncode && frameAgeAtSelectUs > kMaxPreEncodeFrameAgeUs) {
      ++stalePreEncodeDropCount;
      continue;
    }
    if (lastVersionSent > 0 && version > lastVersionSent + 1) {
      skippedByOverwrite += (version - lastVersionSent - 1);
    }
    lastVersionSent = version;
    const uint64_t captureStampUs = (callbackUs > 0) ? callbackUs : captureUs;

    bool sendFailed = false;
    if (useRaw) {
      RawFrameHeader hdr{};
      hdr.header.magic = remote60::native_poc::kMagic;
      hdr.header.type = static_cast<uint16_t>(MessageType::RawFrameBgra);
      hdr.header.size = static_cast<uint16_t>(sizeof(hdr));
      hdr.seq = seq;
      hdr.width = w;
      hdr.height = h;
      hdr.stride = stride;
      hdr.payloadSize = static_cast<uint32_t>(payload->size());
      hdr.captureQpcUs = captureStampUs;
      hdr.encodeStartQpcUs = captureStampUs;
      hdr.encodeEndQpcUs = captureStampUs;
      hdr.sendQpcUs = qpc_now_us();

      const bool sentOk =
          (transport == VideoTransport::Tcp) &&
          send_all(clientSock, &hdr, sizeof(hdr)) &&
          send_all(clientSock, payload->data(), payload->size());
      if (!sentOk) {
        std::cout << "[native-video-host] client disconnected\n";
        break;
      }
      ++sentFrames;
      sentBytes += payload->size();
      if (args.traceEvery > 0 && (seq % args.traceEvery) == 0 &&
          (args.traceMax == 0 || tracePrinted < args.traceMax)) {
        ++tracePrinted;
        const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
        const uint64_t encUs = (hdr.encodeEndQpcUs >= hdr.encodeStartQpcUs) ? (hdr.encodeEndQpcUs - hdr.encodeStartQpcUs) : 0;
        const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
        std::cout << "[native-video-host][trace] seq=" << seq
                  << " captureUs=" << hdr.captureQpcUs
                  << " encodeStartUs=" << hdr.encodeStartQpcUs
                  << " encodeEndUs=" << hdr.encodeEndQpcUs
                  << " sendUs=" << hdr.sendQpcUs
                  << " c2eUs=" << c2eUs
                  << " encUs=" << encUs
                  << " e2sUs=" << e2sUs
                  << " payloadBytes=" << hdr.payloadSize
                  << "\n";
      }
    } else {
      const uint8_t* encodeSrc = payload->data();
      uint32_t encodeSrcW = w;
      uint32_t encodeSrcH = h;
      uint32_t encodeSrcStride = stride;
      std::vector<uint8_t> scaledBgra;
      if (activeEncodeW != w || activeEncodeH != h) {
        if (!resize_bgra_nearest(payload->data(), w, h, stride, activeEncodeW, activeEncodeH, &scaledBgra)) {
          continue;
        }
        encodeSrc = scaledBgra.data();
        encodeSrcW = activeEncodeW;
        encodeSrcH = activeEncodeH;
        encodeSrcStride = activeEncodeW * 4;
      }

      std::vector<uint8_t> nv12;
      if (!bgra_to_nv12(encodeSrc, encodeSrcW, encodeSrcH, encodeSrcStride, &nv12) || nv12.empty()) {
        continue;
      }

      const uint64_t beforeEncodeUs = qpc_now_us();
      const uint64_t frameAgeBeforeEncodeUs =
          (callbackUs > 0 && beforeEncodeUs >= callbackUs) ? (beforeEncodeUs - callbackUs) : 0;
      uint64_t latestVersion = version;
      {
        std::lock_guard<std::mutex> lk(frame.mu);
        latestVersion = frame.version;
      }
      if (guardStalePreEncode &&
          frameAgeBeforeEncodeUs > kMaxPreEncodeFrameAgeUs && latestVersion != version) {
        ++stalePreEncodeDropCount;
        continue;
      }

      if (clientRequestedKeyFrame.exchange(false)) {
        const uint16_t reason = clientKeyFrameReason.load();
        std::cout << "[native-video-host][control] keyframe-request-consumed reason=" << reason << "\n";
        forceKeyNext = true;
      }
      const bool forceKeyFrame =
          forceKeyNext || (encodedSeq == 0) || ((args.keyint > 0) && ((seq % args.keyint) == 0));
      const uint64_t encodeStartUs = qpc_now_us();
      const uint64_t callbackToEncodeStartUs =
          (encodeStartUs >= callbackUs) ? (encodeStartUs - callbackUs) : 0;
      std::vector<H264AccessUnit> units;
      if (!encoder.encode_frame(nv12, forceKeyFrame, static_cast<int64_t>(captureStampUs) * 10, &units)) {
        ++encodeFailCount;
        if ((encodeFailCount % 60) == 1) {
          std::cout << "[native-video-host] encode failed count=" << encodeFailCount << "\n";
        }
        continue;
      }
      const uint64_t encodeEndUs = qpc_now_us();
      if (units.empty()) continue;

      captureAgeSumUs += captureAgeAtCallbackUs;
      captureAgeMaxUs = std::max(captureAgeMaxUs, captureAgeAtCallbackUs);
      callbackToEncodeStartSumUs += callbackToEncodeStartUs;
      callbackToEncodeStartMaxUs = std::max(callbackToEncodeStartMaxUs, callbackToEncodeStartUs);

      bool encoderResetTriggered = false;
      bool countedRawForInput = false;
      for (const auto& au : units) {
        if (au.bytes.empty()) continue;
        const uint64_t auCaptureUs =
            (au.sampleTimeHns > 0) ? static_cast<uint64_t>(au.sampleTimeHns / 10) : captureStampUs;
        const uint64_t encodedAgeUs =
            (encodeEndUs >= auCaptureUs) ? (encodeEndUs - auCaptureUs) : 0;
        if (guardStaleEncoded && encodedAgeUs > kMaxEncodedFrameAgeUs) {
          ++staleEncodedDropCount;
          ++consecutiveStaleEncodedFrames;
          if ((staleEncodedDropCount % 60) == 1) {
            std::cout << "[native-video-host] stale encoded drop count=" << staleEncodedDropCount
                      << " encodedAgeUs=" << encodedAgeUs
                      << " thresholdUs=" << kMaxEncodedFrameAgeUs
                      << " consecutive=" << consecutiveStaleEncodedFrames
                      << "\n";
          }
          if (consecutiveStaleEncodedFrames >= kMaxConsecutiveStaleEncodedFrames) {
            std::cout << "[native-video-host] encoder reset due to stale output age="
                      << encodedAgeUs << "us consecutive=" << consecutiveStaleEncodedFrames << "\n";
            encoder.shutdown();
            if (!encoder.initialize(activeEncodeW, activeEncodeH, args.fps, activeBitrate, args.keyint)) {
              std::cerr << "[native-video-host] encoder reinitialize failed\n";
              sendFailed = true;
              break;
            }
            ++encoderResetCount;
            consecutiveStaleEncodedFrames = 0;
            forceKeyNext = true;
            encoderResetTriggered = true;
            break;
          }
          continue;
        }
        consecutiveStaleEncodedFrames = 0;

        EncodedFrameHeader hdr{};
        hdr.header.magic = remote60::native_poc::kMagic;
        hdr.header.type = static_cast<uint16_t>(MessageType::EncodedFrameH264);
        hdr.header.size = static_cast<uint16_t>(sizeof(hdr));
        hdr.seq = ++encodedSeq;
        hdr.width = activeEncodeW;
        hdr.height = activeEncodeH;
        hdr.payloadSize = static_cast<uint32_t>(au.bytes.size());
        hdr.flags = (au.keyFrame || forceKeyFrame || forceKeyNext) ? 1u : 0u;
        hdr.captureQpcUs = auCaptureUs;
        hdr.encodeStartQpcUs = encodeStartUs;
        hdr.encodeEndQpcUs = encodeEndUs;
        hdr.sendQpcUs = qpc_now_us();

        bool sentOk = false;
        if (transport == VideoTransport::Tcp) {
          sentOk = send_all(clientSock, &hdr, sizeof(hdr)) &&
                   send_all(clientSock, au.bytes.data(), au.bytes.size());
        } else {
          if (!udpPeerReady) {
            sentOk = false;
          } else {
            UdpVideoChunkHeader udpHdr{};
            udpHdr.magic = remote60::native_poc::kMagic;
            udpHdr.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
            udpHdr.size = static_cast<uint16_t>(sizeof(udpHdr));
            udpHdr.seq = hdr.seq;
            udpHdr.codec = static_cast<uint16_t>(UdpCodec::H264);
            udpHdr.flags = (hdr.flags & 1u) ? 0x1u : 0u;
            udpHdr.width = hdr.width;
            udpHdr.height = hdr.height;
            udpHdr.stride = 0;
            udpHdr.payloadSize = hdr.payloadSize;
            udpHdr.captureQpcUs = hdr.captureQpcUs;
            udpHdr.encodeStartQpcUs = hdr.encodeStartQpcUs;
            udpHdr.encodeEndQpcUs = hdr.encodeEndQpcUs;
            udpHdr.sendQpcUs = hdr.sendQpcUs;
            sentOk = send_udp_chunks(clientSock, udpPeer, au.bytes.data(), au.bytes.size(),
                                     udpHdr, args.udpMtu);
          }
        }
        if (!sentOk) {
          sendFailed = true;
          break;
        }

        ++sentFrames;
        ++encodedFrames;
        sentBytes += au.bytes.size();
        if (!countedRawForInput) {
          rawEquivalentBytes += static_cast<uint64_t>(nv12.size());
          countedRawForInput = true;
        }
        if ((hdr.flags & 1u) != 0) {
          forceKeyNext = false;
        }

        if (args.traceEvery > 0 && (hdr.seq % args.traceEvery) == 0 &&
            (args.traceMax == 0 || tracePrinted < args.traceMax)) {
          ++tracePrinted;
          const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
          const uint64_t encQueueUs =
              (encodeStartUs >= auCaptureUs) ? (encodeStartUs - auCaptureUs) : 0;
          const uint64_t encUs = (hdr.encodeEndQpcUs >= hdr.encodeStartQpcUs) ? (hdr.encodeEndQpcUs - hdr.encodeStartQpcUs) : 0;
          const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
          std::cout << "[native-video-host][trace] seq=" << hdr.seq
                    << " captureUs=" << hdr.captureQpcUs
                    << " encodeStartUs=" << hdr.encodeStartQpcUs
                    << " encodeEndUs=" << hdr.encodeEndQpcUs
                    << " sendUs=" << hdr.sendQpcUs
                    << " c2eUs=" << c2eUs
                    << " encQueueUs=" << encQueueUs
                    << " cb2eUs=" << callbackToEncodeStartUs
                    << " capAgeUs=" << captureAgeAtCallbackUs
                    << " encUs=" << encUs
                    << " e2sUs=" << e2sUs
                    << " payloadBytes=" << hdr.payloadSize
                    << " key=" << ((hdr.flags & 1u) ? 1 : 0)
                    << "\n";
        }
      }

      if (encoderResetTriggered) {
        continue;
      }
      if (sendFailed) {
        std::cout << "[native-video-host] client disconnected\n";
        break;
      }
    }

    const uint64_t t = qpc_now_us();
    if (t >= statAtUs) {
      const double mbps = (sentBytes * 8.0) / (1000.0 * 1000.0);
      if (useRaw) {
        std::cout << "[native-video-host] sentFrames=" << sentFrames
                  << " callbackFrames=" << callbackFrames.load()
                  << " skippedByOverwrite=" << skippedByOverwrite
                  << " mbps=" << mbps
                  << " size=" << w << "x" << h
                  << "\n";
      } else {
        const uint64_t capAgeAvgUs = (encodedFrames > 0) ? (captureAgeSumUs / encodedFrames) : 0;
        const uint64_t cb2eAvgUs = (encodedFrames > 0) ? (callbackToEncodeStartSumUs / encodedFrames) : 0;
        const double rawEquivMbps = (rawEquivalentBytes * 8.0) / (1000.0 * 1000.0);
        const uint64_t encRatioX100 =
            (sentBytes > 0) ? ((rawEquivalentBytes * 100ULL) / sentBytes) : 0;
        std::cout << "[native-video-host] encodedFrames=" << encodedFrames
                  << " sentFrames=" << sentFrames
                  << " callbackFrames=" << callbackFrames.load()
                  << " skippedByOverwrite=" << skippedByOverwrite
                  << " stalePreEncodeDrops=" << stalePreEncodeDropCount
                  << " staleEncodedDrops=" << staleEncodedDropCount
                  << " encoderResets=" << encoderResetCount
                  << " keyReqTotal=" << clientKeyFrameRequestCount.load()
                  << " inputEvents=" << inputEvents.load()
                  << " capAgeAvgUs=" << capAgeAvgUs
                  << " capAgeMaxUs=" << captureAgeMaxUs
                  << " cb2eAvgUs=" << cb2eAvgUs
                  << " cb2eMaxUs=" << callbackToEncodeStartMaxUs
                  << " mbps=" << mbps
                  << " rawEquivMbps=" << rawEquivMbps
                  << " encRatioX100=" << encRatioX100
                  << " bitrateTarget=" << activeBitrate
                  << " size=" << activeEncodeW << "x" << activeEncodeH
                  << " abrProfile=" << ((abrProfile == 0) ? "high" : ((abrProfile == 1) ? "mid" : "low"))
                  << " abrModSec=" << abrModeratePressureSeconds
                  << " abrSevSec=" << abrSeverePressureSeconds
                  << " abrGoodSec=" << abrGoodSeconds
                  << "\n";

        if (abrEnabled) {
          const uint64_t metricsUpdatedUs = clientMetricsUpdatedUs.load();
          const bool metricsFresh =
              (metricsUpdatedUs > 0) && (t >= metricsUpdatedUs) && ((t - metricsUpdatedUs) <= 3000000ULL);

          const uint64_t clAvgLatencyUs = metricsFresh ? clientMetricsAvgLatencyUs.load() : 0;
          const uint64_t clAvgDecodeTailUs = metricsFresh ? clientMetricsAvgDecodeTailUs.load() : 0;
          const uint32_t clDecodedFpsX100 = metricsFresh ? clientMetricsDecodedFpsX100.load() : 0;
          const uint32_t clRecvMbpsX1000 = metricsFresh ? clientMetricsRecvMbpsX1000.load() : 0;
          const uint32_t clWidth = metricsFresh ? clientMetricsWidth.load() : 0;
          const uint32_t clHeight = metricsFresh ? clientMetricsHeight.load() : 0;

          const uint32_t minGoodFpsX100 = args.fps * 93u;
          const uint32_t minOkayFpsX100 = args.fps * 85u;
          const uint32_t minDegradeFpsX100 = args.fps * 45u;
          const uint32_t minSevereFpsX100 = args.fps * 35u;
          const bool abrWarmupDone = (t >= (startUs + 4000000ULL));

          const bool severeDownByClient =
              metricsFresh &&
              (clAvgLatencyUs > 150000ULL ||
               clAvgDecodeTailUs > 110000ULL ||
               (clDecodedFpsX100 < minSevereFpsX100 &&
                (clAvgLatencyUs > 110000ULL || clAvgDecodeTailUs > 80000ULL)));
          const bool moderateDownByClient =
              metricsFresh &&
              (clAvgLatencyUs > 125000ULL ||
               clAvgDecodeTailUs > 90000ULL ||
               (clDecodedFpsX100 < minDegradeFpsX100 &&
                (clAvgLatencyUs > 95000ULL || clAvgDecodeTailUs > 70000ULL)));
          const bool emergencyDownByClient =
              metricsFresh &&
              (clAvgLatencyUs > 220000ULL ||
               clAvgDecodeTailUs > 160000ULL);
          const bool severeDownByHost = (!metricsFresh && cb2eAvgUs > 90000ULL);
          const bool moderateDownByHost = (!metricsFresh && cb2eAvgUs > 70000ULL);
          const bool severeDown = abrWarmupDone && (severeDownByClient || severeDownByHost);
          const bool moderateDown = abrWarmupDone && (moderateDownByClient || moderateDownByHost);
          const bool emergencyDown = abrWarmupDone && emergencyDownByClient;

          if (severeDown) {
            ++abrSeverePressureSeconds;
          } else {
            abrSeverePressureSeconds = 0;
          }
          if (moderateDown) {
            ++abrModeratePressureSeconds;
          } else {
            abrModeratePressureSeconds = 0;
          }

          const bool goodForLowToMid =
              metricsFresh &&
              (clAvgLatencyUs < 90000ULL) &&
              (clAvgDecodeTailUs < 65000ULL) &&
              (clDecodedFpsX100 >= minOkayFpsX100);
          const bool goodForMidToHigh =
              metricsFresh &&
              (clAvgLatencyUs < 75000ULL) &&
              (clAvgDecodeTailUs < 50000ULL) &&
              (clDecodedFpsX100 >= minGoodFpsX100);

          int targetProfile = abrProfile;
          const char* abrReason = "none";
          if (t >= abrCooldownUntilUs) {
            if (abrProfile == 0) {
              if (emergencyDown && abrHasLowProfile && abrSeverePressureSeconds >= 1) {
                targetProfile = 2;
                abrReason = "client_emergency";
              } else if ((abrSeverePressureSeconds >= 2) || (abrModeratePressureSeconds >= 4)) {
                if (abrHasMidProfile) {
                  targetProfile = 1;
                  abrReason = (abrSeverePressureSeconds >= 2) ? "high_to_mid_severe" : "high_to_mid_moderate";
                } else if (abrHasLowProfile) {
                  targetProfile = 2;
                  abrReason = (abrSeverePressureSeconds >= 2) ? "high_to_low_severe" : "high_to_low_moderate";
                }
              }
              abrGoodSeconds = 0;
            } else if (abrProfile == 1) {
              if (emergencyDown && abrHasLowProfile) {
                targetProfile = 2;
                abrReason = "client_emergency";
                abrGoodSeconds = 0;
              } else if ((abrSeverePressureSeconds >= 3 || abrModeratePressureSeconds >= 5) && abrHasLowProfile) {
                targetProfile = 2;
                abrReason = (abrSeverePressureSeconds >= 3) ? "mid_to_low_severe" : "mid_to_low_moderate";
                abrGoodSeconds = 0;
              } else {
                if (goodForMidToHigh) {
                  ++abrGoodSeconds;
                } else {
                  abrGoodSeconds = 0;
                }
                if (abrGoodSeconds >= 8) {
                  targetProfile = 0;
                  abrReason = "client_stable_high";
                }
              }
            } else {  // abrProfile == 2
              if (goodForLowToMid) {
                ++abrGoodSeconds;
              } else {
                abrGoodSeconds = 0;
              }
              if (abrGoodSeconds >= 5) {
                targetProfile = abrHasMidProfile ? 1 : 0;
                abrReason = "client_stable_mid";
              }
            }
          }

          if (targetProfile != abrProfile) {
            uint32_t targetW = abrHighW;
            uint32_t targetH = abrHighH;
            uint32_t targetBitrate = abrHighBitrate;
            if (targetProfile == 1) {
              targetW = abrMidW;
              targetH = abrMidH;
              targetBitrate = abrMidBitrate;
            } else if (targetProfile == 2) {
              targetW = abrLowW;
              targetH = abrLowH;
              targetBitrate = abrLowBitrate;
            }

            bool switchOk = true;
            if (targetW != activeEncodeW || targetH != activeEncodeH) {
              encoder.shutdown();
              if (!encoder.initialize(targetW, targetH, args.fps, targetBitrate, args.keyint)) {
                std::cerr << "[native-video-host][abr] encoder reinitialize failed for profile switch\n";
                switchOk = false;
              }
            } else if (targetBitrate != activeBitrate) {
              if (!encoder.reconfigure_bitrate(targetBitrate)) {
                encoder.shutdown();
                if (!encoder.initialize(targetW, targetH, args.fps, targetBitrate, args.keyint)) {
                  std::cerr << "[native-video-host][abr] encoder bitrate reconfigure/reinit failed\n";
                  switchOk = false;
                }
              }
            }

            if (!switchOk) {
              break;
            }

            activeEncodeW = targetW;
            activeEncodeH = targetH;
            activeBitrate = targetBitrate;
            abrProfile = targetProfile;
            abrGoodSeconds = 0;
            abrModeratePressureSeconds = 0;
            abrSeverePressureSeconds = 0;
            abrCooldownUntilUs = t + 4000000ULL;
            forceKeyNext = true;

            std::cout << "[native-video-host][abr] profile="
                      << ((abrProfile == 0) ? "high" : ((abrProfile == 1) ? "mid" : "low"))
                      << " encode=" << activeEncodeW << "x" << activeEncodeH
                      << " bitrate=" << activeBitrate
                      << " reason=" << abrReason
                      << " clientSize=" << clWidth << "x" << clHeight
                      << " clientDecodedFps=" << (clDecodedFpsX100 / 100.0)
                      << " clientAvgLatUs=" << clAvgLatencyUs
                      << " clientAvgTailUs=" << clAvgDecodeTailUs
                      << " clientMbps=" << (clRecvMbpsX1000 / 1000.0)
                      << "\n";
          }
        }
      }
      sentFrames = 0;
      encodedFrames = 0;
      sentBytes = 0;
      rawEquivalentBytes = 0;
      skippedByOverwrite = 0;
      stalePreEncodeDropCount = 0;
      staleEncodedDropCount = 0;
      encoderResetCount = 0;
      callbackFrames = 0;
      captureAgeSumUs = 0;
      captureAgeMaxUs = 0;
      callbackToEncodeStartSumUs = 0;
      callbackToEncodeStartMaxUs = 0;
      statAtUs += 1000000ULL;
    }
  }

  stop = true;
  frame.cv.notify_all();
  if (controlClientSock != INVALID_SOCKET) {
    shutdown(controlClientSock, SD_BOTH);
    closesocket(controlClientSock);
    controlClientSock = INVALID_SOCKET;
  }
  if (controlListenSock != INVALID_SOCKET) {
    closesocket(controlListenSock);
    controlListenSock = INVALID_SOCKET;
  }
  if (controlThread.joinable()) controlThread.join();
  try {
    pool.FrameArrived(token);
  } catch (...) {
  }
  try {
    session.Close();
  } catch (...) {
  }
  try {
    pool.Close();
  } catch (...) {
  }
  closesocket(clientSock);
  if (useH264) {
    encoder.shutdown();
    if (mfStarted) MFShutdown();
  }
  std::cout << "[native-video-host] done\n";
  return 0;
}

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
using remote60::native_poc::ControlRuntimeEncoderConfigMessage;
using remote60::native_poc::ControlPingMessage;
using remote60::native_poc::ControlPongMessage;
using remote60::native_poc::H264EncodeFrameStats;
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
constexpr uint64_t kHostUserFeedbackWarnUs = 90000;  // 90ms
constexpr uint64_t kHostUserFeedbackMinIntervalUs = 1000000;  // 1s
constexpr uint64_t kCaptureStallKeepaliveStartUs = 700000;  // 0.7s
constexpr uint64_t kCaptureStallKeepaliveIntervalUs = 1000000;  // 1s
constexpr uint64_t kCaptureCallbackStallRestartUs = 1200000;  // 1.2s
constexpr uint64_t kCaptureCallbackRestartCooldownUs = 3000000;  // 3s
constexpr uint32_t kFrameGatingStaticFpsDefault = 8;
constexpr uint32_t kFrameGatingStaticThresholdPermilleDefault = 6;
constexpr uint32_t kFrameGatingMotionThresholdPermilleDefault = 14;
constexpr uint32_t kFrameGatingEnterFramesDefault = 10;
constexpr uint32_t kFrameGatingExitFramesDefault = 2;
constexpr uint32_t kFrameGatingSampleTargetDefault = 2048;
constexpr uint32_t kKeyReqMinIntervalUsDefault = 120000;  // 120ms
constexpr uint32_t kKeyReqTokenRefillUsDefault = 300000;  // 300ms / token
constexpr uint32_t kKeyReqTokenCapacityDefault = 3;

struct HostBottleneckStage {
  uint32_t code = 0;
  uint64_t us = 0;
  const char* name = "none";
};

void update_host_bottleneck_stage(uint32_t code, uint64_t us, const char* name,
                                  HostBottleneckStage* stage) {
  if (!stage || !name) return;
  if (us > stage->us) {
    stage->code = code;
    stage->us = us;
    stage->name = name;
  }
}

HostBottleneckStage detect_host_bottleneck_stage(uint64_t queueWaitUs, uint64_t queueToEncodeUs,
                                                 uint64_t preEncodePrepUs, uint64_t scaleUs,
                                                 uint64_t nv12Us, uint64_t encUs,
                                                 uint64_t queueToSendUs, uint64_t sendDurUs,
                                                 uint64_t sendIntervalErrUs) {
  HostBottleneckStage stage{};
  update_host_bottleneck_stage(1, queueWaitUs, "queue_wait", &stage);
  update_host_bottleneck_stage(2, queueToEncodeUs, "queue_to_encode", &stage);
  update_host_bottleneck_stage(3, preEncodePrepUs, "pre_encode_prep", &stage);
  update_host_bottleneck_stage(4, scaleUs, "scale", &stage);
  update_host_bottleneck_stage(5, nv12Us, "bgra_to_nv12", &stage);
  update_host_bottleneck_stage(6, encUs, "encoder", &stage);
  update_host_bottleneck_stage(7, queueToSendUs, "queue_to_send", &stage);
  update_host_bottleneck_stage(8, sendDurUs, "send_io", &stage);
  update_host_bottleneck_stage(9, sendIntervalErrUs, "send_interval_jitter", &stage);
  return stage;
}

uint32_t encoder_api_path_code(const char* backendName) {
  if (!backendName) return 0;
  const std::string name = backendName;
  if (name.find("amf") != std::string::npos) return 1;
  if (name.find("nvenc") != std::string::npos || name.find("nvidia") != std::string::npos) return 2;
  if (name.find("qsv") != std::string::npos || name.find("intel") != std::string::npos) return 3;
  if (name.find("mft") != std::string::npos) return 4;
  if (name.find("clsid") != std::string::npos) return 5;
  return 6;
}

winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateItemForPrimaryMonitor() {
  auto interop = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                                               IGraphicsCaptureItemInterop>();
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};

  auto logHresult = [](const char* label, const winrt::hresult_error& e) {
    std::cerr << "[native-video-host] " << label << ", hr=0x" << std::hex
              << static_cast<unsigned long>(e.code()) << std::dec << "\n";
  };

  auto createForMonitor = [&](HMONITOR monitor, const char* source) {
    if (!monitor) return false;
    if (!item) {
      try {
        interop->CreateForMonitor(monitor, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                                 winrt::put_abi(item));
      } catch (const winrt::hresult_error& e) {
        logHresult("CreateForMonitor failed", e);
        item = nullptr;
      }
    }
    if (!item) {
      std::cerr << "[native-video-host] CreateForMonitor failed, source=" << source << "\n";
    } else {
      std::cout << "[native-video-host] capture item source=" << source << "\n";
    }
    return static_cast<bool>(item);
  };

  auto createForWindow = [&](HWND hwnd, const char* source) {
    if (!hwnd) return false;
    if (!item) {
      try {
        interop->CreateForWindow(hwnd, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                                winrt::put_abi(item));
      } catch (const winrt::hresult_error& e) {
        logHresult("CreateForWindow failed", e);
        item = nullptr;
      }
    }
    if (!item) {
      std::cerr << "[native-video-host] CreateForWindow failed, source=" << source << "\n";
    } else {
      std::cout << "[native-video-host] capture item source=" << source << "\n";
    }
    return static_cast<bool>(item);
  };

  HMONITOR monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);

  createForMonitor(monitor, "MonitorFromWindow(GetDesktopWindow())");
  if (item) return item;

  monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  createForMonitor(monitor, "MonitorFromPoint(0,0)");
  if (item) return item;

  struct EnumFirstMonitorState {
    HMONITOR monitor = nullptr;
  };
  EnumFirstMonitorState enumState{};
  auto enumCb = [](HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) -> BOOL {
    auto* state = reinterpret_cast<EnumFirstMonitorState*>(lParam);
    if (state && !state->monitor) {
      state->monitor = hMonitor;
    }
    return TRUE;
  };
  EnumDisplayMonitors(nullptr, nullptr, enumCb, reinterpret_cast<LPARAM>(&enumState));
  if (enumState.monitor) {
    createForMonitor(enumState.monitor, "EnumDisplayMonitors(first)");
    if (item) return item;
  }

  createForWindow(GetForegroundWindow(), "CreateForWindow(GetForegroundWindow)");
  if (item) return item;

  createForWindow(GetDesktopWindow(), "CreateForWindow(GetDesktopWindow())");
  if (item) return item;

  createForWindow(GetShellWindow(), "CreateForWindow(GetShellWindow())");
  if (item) return item;

  createForWindow(GetConsoleWindow(), "CreateForWindow(GetConsoleWindow())");
  if (item) return item;

  struct EnumCaptureWindowState {
    HWND hwnd = nullptr;
  };
  EnumCaptureWindowState enumWindowState{};
  auto enumWindowCb = [](HWND hwnd, LPARAM lParam) -> BOOL {
    if (!hwnd) return TRUE;
    auto* state = reinterpret_cast<EnumCaptureWindowState*>(lParam);
    if (!state->hwnd) {
      const LONG style = GetWindowLongPtr(hwnd, GWL_STYLE);
      if ((style & WS_VISIBLE) && (style & WS_OVERLAPPEDWINDOW)) {
        state->hwnd = hwnd;
      }
    }
    return state->hwnd ? FALSE : TRUE;
  };
  EnumWindows(enumWindowCb, reinterpret_cast<LPARAM>(&enumWindowState));
  if (enumWindowState.hwnd) {
    createForWindow(enumWindowState.hwnd, "EnumWindows(first visible overlapped)");
    if (item) return item;
  }

  HWND shellWorkerW = FindWindowW(L"Progman", nullptr);
  if (shellWorkerW) {
    createForWindow(shellWorkerW, "FindWindowW(Progman)");
    if (item) return item;
  }

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

uint32_t env_u32_clamped(const char* key, uint32_t fallback, uint32_t minValue, uint32_t maxValue) {
  if (!key) return fallback;
  const char* raw = std::getenv(key);
  if (!raw) return fallback;
  uint32_t parsed = 0;
  if (!parse_u32(raw, &parsed)) return fallback;
  return std::clamp<uint32_t>(parsed, minValue, maxValue);
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

uint32_t estimate_bgra_change_permille(const uint8_t* a, const uint8_t* b, size_t sizeBytes,
                                       uint32_t sampleTarget) {
  if (!a || !b || sizeBytes < 4) return 1000;
  const size_t pixels = sizeBytes / 4;
  if (pixels == 0) return 1000;
  const size_t stridePixels = std::max<size_t>(1, pixels / std::max<uint32_t>(1, sampleTarget));
  uint64_t diffSum = 0;
  uint64_t sampleCount = 0;
  for (size_t i = 0; i < pixels; i += stridePixels) {
    const size_t idx = i * 4;
    const int db = std::abs(static_cast<int>(a[idx + 0]) - static_cast<int>(b[idx + 0]));
    const int dg = std::abs(static_cast<int>(a[idx + 1]) - static_cast<int>(b[idx + 1]));
    const int dr = std::abs(static_cast<int>(a[idx + 2]) - static_cast<int>(b[idx + 2]));
    diffSum += static_cast<uint64_t>((db + dg + dr) / 3);
    ++sampleCount;
  }
  if (sampleCount == 0) return 1000;
  const uint64_t avgDiff = diffSum / sampleCount;  // 0..255
  const uint64_t permille = (avgDiff * 1000ULL) / 255ULL;
  return static_cast<uint32_t>(std::min<uint64_t>(permille, 1000ULL));
}

bool resize_bgra_bilinear(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint32_t srcStride,
                          uint32_t dstW, uint32_t dstH, std::vector<uint8_t>* outBgra) {
  if (!src || srcW == 0 || srcH == 0 || srcStride < (srcW * 4) || dstW == 0 || dstH == 0 || !outBgra) {
    return false;
  }
  outBgra->resize(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4);
  auto* dst = outBgra->data();
  const uint64_t xScale =
      (dstW > 1) ? ((static_cast<uint64_t>(srcW - 1) << 16) / static_cast<uint64_t>(dstW - 1)) : 0;
  const uint64_t yScale =
      (dstH > 1) ? ((static_cast<uint64_t>(srcH - 1) << 16) / static_cast<uint64_t>(dstH - 1)) : 0;
  for (uint32_t y = 0; y < dstH; ++y) {
    const uint32_t srcYFixed = static_cast<uint32_t>(static_cast<uint64_t>(y) * yScale);
    const uint32_t y0 = std::min<uint32_t>(srcH - 1, srcYFixed >> 16);
    const uint32_t y1 = std::min<uint32_t>(srcH - 1, y0 + 1);
    const uint32_t wy = (srcYFixed & 0xFFFFu) >> 8;
    const uint32_t invWy = 256u - wy;
    const uint8_t* srcRow0 = src + static_cast<size_t>(y0) * srcStride;
    const uint8_t* srcRow1 = src + static_cast<size_t>(y1) * srcStride;
    uint8_t* dstRow = dst + static_cast<size_t>(y) * dstW * 4;
    for (uint32_t x = 0; x < dstW; ++x) {
      const uint32_t srcXFixed = static_cast<uint32_t>(static_cast<uint64_t>(x) * xScale);
      const uint32_t x0 = std::min<uint32_t>(srcW - 1, srcXFixed >> 16);
      const uint32_t x1 = std::min<uint32_t>(srcW - 1, x0 + 1);
      const uint32_t wx = (srcXFixed & 0xFFFFu) >> 8;
      const uint32_t invWx = 256u - wx;

      const uint8_t* p00 = srcRow0 + static_cast<size_t>(x0) * 4;
      const uint8_t* p10 = srcRow0 + static_cast<size_t>(x1) * 4;
      const uint8_t* p01 = srcRow1 + static_cast<size_t>(x0) * 4;
      const uint8_t* p11 = srcRow1 + static_cast<size_t>(x1) * 4;
      uint8_t* outPx = dstRow + static_cast<size_t>(x) * 4;
      for (int c = 0; c < 4; ++c) {
        const uint32_t top = p00[c] * invWx + p10[c] * wx;
        const uint32_t bottom = p01[c] * invWx + p11[c] * wx;
        const uint32_t blended = (top * invWy + bottom * wy + 32768u) >> 16;
        outPx[c] = static_cast<uint8_t>(blended);
      }
    }
  }
  return true;
}

struct GpuBgraScaler {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice;
  Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessor> processor;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> srcTexture;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> dstTexture;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> dstStaging;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> inputView;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> outputView;
  std::mutex* d3dMutex = nullptr;
  uint32_t srcW = 0;
  uint32_t srcH = 0;
  uint32_t dstW = 0;
  uint32_t dstH = 0;
  bool initialized = false;

  bool initialize(ID3D11Device* d, ID3D11DeviceContext* c, std::mutex* mu) {
    if (!d || !c) return false;
    device = d;
    context = c;
    d3dMutex = mu;
    if (FAILED(device.As(&videoDevice)) || !videoDevice) return false;
    if (FAILED(context.As(&videoContext)) || !videoContext) return false;
    initialized = true;
    return true;
  }

  bool ensure_resources(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH) {
    if (!initialized || !videoDevice || !videoContext) return false;
    if (inW == 0 || inH == 0 || outW == 0 || outH == 0) return false;
    if (srcTexture && dstTexture && dstStaging && inputView && outputView &&
        srcW == inW && srcH == inH && dstW == outW && dstH == outH) {
      return true;
    }

    enumerator.Reset();
    processor.Reset();
    srcTexture.Reset();
    dstTexture.Reset();
    dstStaging.Reset();
    inputView.Reset();
    outputView.Reset();

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
    desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    desc.InputWidth = inW;
    desc.InputHeight = inH;
    desc.OutputWidth = outW;
    desc.OutputHeight = outH;
    desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    if (FAILED(videoDevice->CreateVideoProcessorEnumerator(&desc, &enumerator)) || !enumerator) return false;

    UINT formatSupport = 0;
    if (FAILED(enumerator->CheckVideoProcessorFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, &formatSupport))) {
      return false;
    }
    const UINT requiredFormatSupport =
        D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT | D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT;
    if ((formatSupport & requiredFormatSupport) != requiredFormatSupport) return false;

    if (FAILED(videoDevice->CreateVideoProcessor(enumerator.Get(), 0, &processor)) || !processor) return false;

    D3D11_TEXTURE2D_DESC srcDesc{};
    srcDesc.Width = inW;
    srcDesc.Height = inH;
    srcDesc.MipLevels = 1;
    srcDesc.ArraySize = 1;
    srcDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srcDesc.SampleDesc.Count = 1;
    srcDesc.Usage = D3D11_USAGE_DEFAULT;
    srcDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (FAILED(device->CreateTexture2D(&srcDesc, nullptr, &srcTexture)) || !srcTexture) return false;

    D3D11_TEXTURE2D_DESC dstDesc = srcDesc;
    dstDesc.Width = outW;
    dstDesc.Height = outH;
    if (FAILED(device->CreateTexture2D(&dstDesc, nullptr, &dstTexture)) || !dstTexture) return false;

    D3D11_TEXTURE2D_DESC stagingDesc = dstDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &dstStaging)) || !dstStaging) return false;

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inViewDesc{};
    inViewDesc.FourCC = 0;
    inViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inViewDesc.Texture2D.MipSlice = 0;
    inViewDesc.Texture2D.ArraySlice = 0;
    if (FAILED(videoDevice->CreateVideoProcessorInputView(
            srcTexture.Get(), enumerator.Get(), &inViewDesc, &inputView)) || !inputView) {
      return false;
    }

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outViewDesc{};
    outViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outViewDesc.Texture2D.MipSlice = 0;
    if (FAILED(videoDevice->CreateVideoProcessorOutputView(
            dstTexture.Get(), enumerator.Get(), &outViewDesc, &outputView)) || !outputView) {
      return false;
    }

    srcW = inW;
    srcH = inH;
    dstW = outW;
    dstH = outH;
    return true;
  }

  bool scale(const uint8_t* src, uint32_t inW, uint32_t inH, uint32_t srcStride,
             uint32_t outW, uint32_t outH, std::vector<uint8_t>* outBgra) {
    if (!src || !outBgra || srcStride < inW * 4) return false;
    std::lock_guard<std::mutex> lk(*d3dMutex);
    if (!ensure_resources(inW, inH, outW, outH)) return false;

    context->UpdateSubresource(srcTexture.Get(), 0, nullptr, src, srcStride, 0);

    RECT srcRect{};
    srcRect.left = 0;
    srcRect.top = 0;
    srcRect.right = static_cast<LONG>(inW);
    srcRect.bottom = static_cast<LONG>(inH);
    RECT dstRect{};
    dstRect.left = 0;
    dstRect.top = 0;
    dstRect.right = static_cast<LONG>(outW);
    dstRect.bottom = static_cast<LONG>(outH);

    videoContext->VideoProcessorSetOutputTargetRect(processor.Get(), TRUE, &dstRect);
    videoContext->VideoProcessorSetStreamSourceRect(processor.Get(), 0, TRUE, &srcRect);
    videoContext->VideoProcessorSetStreamDestRect(processor.Get(), 0, TRUE, &dstRect);
    videoContext->VideoProcessorSetStreamFrameFormat(
        processor.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = inputView.Get();
    if (FAILED(videoContext->VideoProcessorBlt(processor.Get(), outputView.Get(), 0, 1, &stream))) {
      return false;
    }

    context->CopyResource(dstStaging.Get(), dstTexture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(dstStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;

    outBgra->resize(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4);
    const uint32_t outStride = outW * 4;
    auto* dst = outBgra->data();
    const auto* mappedData = reinterpret_cast<const uint8_t*>(mapped.pData);
    for (uint32_t row = 0; row < outH; ++row) {
      std::memcpy(dst + static_cast<size_t>(row) * outStride,
                  mappedData + static_cast<size_t>(row) * mapped.RowPitch, outStride);
    }
    context->Unmap(dstStaging.Get(), 0);
    return true;
  }
};

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

struct SendPathStats {
  uint64_t headerUs = 0;
  uint64_t payloadUs = 0;
  uint64_t headerCallCount = 0;
  uint64_t payloadCallCount = 0;
  uint64_t payloadChunkCount = 0;
  uint64_t payloadChunkMaxUs = 0;
};

bool send_all_timed(SOCKET s, const void* data, size_t len, uint64_t* outUs,
                    uint64_t* outCallCount) {
  const char* p = reinterpret_cast<const char*>(data);
  size_t sent = 0;
  uint64_t calls = 0;
  const uint64_t startUs = qpc_now_us();
  while (sent < len) {
    const uint64_t callStartUs = qpc_now_us();
    const int n = send(s, p + sent, static_cast<int>(len - sent), 0);
    const uint64_t callDoneUs = qpc_now_us();
    if (n <= 0) return false;
    ++calls;
    sent += static_cast<size_t>(n);
  }
  const uint64_t doneUs = qpc_now_us();
  if (outUs) *outUs = (doneUs >= startUs) ? (doneUs - startUs) : 0;
  if (outCallCount) *outCallCount = calls;
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

bool send_udp_chunks_timed(SOCKET s, const sockaddr_in& peer, const uint8_t* payload, size_t payloadSize,
                          const UdpVideoChunkHeader& baseHeader, uint32_t mtuBytes,
                          SendPathStats* stats) {
  if (!payload || payloadSize == 0 || s == INVALID_SOCKET) return false;
  const uint64_t startUs = qpc_now_us();
  const uint32_t safeMtu = clamp_udp_mtu(mtuBytes);
  if (safeMtu <= sizeof(UdpVideoChunkHeader)) return false;
  const uint32_t maxChunk = safeMtu - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
  std::vector<uint8_t> datagram(safeMtu);
  size_t offset = 0;
  if (!stats) {
    return send_udp_chunks(s, peer, payload, payloadSize, baseHeader, mtuBytes);
  }

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

    const uint64_t callStartUs = qpc_now_us();
    const int n = sendto(s, reinterpret_cast<const char*>(datagram.data()),
                         static_cast<int>(sizeof(h) + chunkSize), 0,
                         reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
    const uint64_t callDoneUs = qpc_now_us();
    if (n <= 0) return false;
    ++stats->payloadChunkCount;
    ++stats->payloadCallCount;
    const uint64_t callUs = (callDoneUs >= callStartUs) ? (callDoneUs - callStartUs) : 0;
    if (callUs > stats->payloadChunkMaxUs) stats->payloadChunkMaxUs = callUs;
    stats->payloadUs += callUs;
    offset += chunkSize;
  }
  const uint64_t doneUs = qpc_now_us();
  stats->payloadUs = (doneUs >= startUs) ? (doneUs - startUs) : stats->payloadUs;
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
  uint64_t callbackIntervalUs = 0;
  uint64_t captureAgeAtCallbackUs = 0;
  uint64_t captureClockSkewUs = 0;
  uint64_t queuePushUs = 0;
  uint64_t captureIntervalUs = 0;
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
  const bool abrQualityFirst = env_truthy("REMOTE60_NATIVE_ADAPTIVE_QUALITY_FIRST");
  const bool frameGatingEnabled = useH264 && !env_truthy("REMOTE60_NATIVE_FRAME_GATING_DISABLE");
  const uint32_t frameGatingStaticFps = env_u32_clamped(
      "REMOTE60_NATIVE_STATIC_SCENE_FPS", kFrameGatingStaticFpsDefault, 1, 30);
  const uint32_t frameGatingStaticThresholdPermille = env_u32_clamped(
      "REMOTE60_NATIVE_FRAME_GATING_STATIC_THRESHOLD_PM",
      kFrameGatingStaticThresholdPermilleDefault, 1, 400);
  const uint32_t frameGatingMotionThresholdPermille = std::max<uint32_t>(
      frameGatingStaticThresholdPermille + 1,
      env_u32_clamped("REMOTE60_NATIVE_FRAME_GATING_MOTION_THRESHOLD_PM",
                      kFrameGatingMotionThresholdPermilleDefault, 2, 500));
  const uint32_t frameGatingEnterFrames = env_u32_clamped(
      "REMOTE60_NATIVE_FRAME_GATING_ENTER_FRAMES", kFrameGatingEnterFramesDefault, 1, 120);
  const uint32_t frameGatingExitFrames = env_u32_clamped(
      "REMOTE60_NATIVE_FRAME_GATING_EXIT_FRAMES", kFrameGatingExitFramesDefault, 1, 30);
  const uint32_t frameGatingSampleTarget = env_u32_clamped(
      "REMOTE60_NATIVE_FRAME_GATING_SAMPLE_TARGET", kFrameGatingSampleTargetDefault, 128, 16384);
  const uint32_t keyReqMinIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYREQ_MIN_INTERVAL_US", kKeyReqMinIntervalUsDefault, 10000, 1000000);
  const uint32_t keyReqTokenRefillUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYREQ_TOKEN_REFILL_US", kKeyReqTokenRefillUsDefault, 10000, 2000000);
  const uint32_t keyReqTokenCapacity = env_u32_clamped(
      "REMOTE60_NATIVE_KEYREQ_TOKEN_CAPACITY", kKeyReqTokenCapacityDefault, 1, 16);
  const bool gpuScalerRequested = useH264 && !env_truthy("REMOTE60_NATIVE_DISABLE_GPU_SCALER");
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
              << " abr=" << (abrEnabled ? "on" : "off")
              << " abrMode=" << (abrQualityFirst ? "quality-first" : "default")
              << " frameGating=" << (frameGatingEnabled ? "on" : "off")
              << " staticSceneFps=" << frameGatingStaticFps
              << " gatingStaticPm=" << frameGatingStaticThresholdPermille
              << " gatingMotionPm=" << frameGatingMotionThresholdPermille
              << " keyReqMinUs=" << keyReqMinIntervalUs
              << " keyReqBucketCap=" << keyReqTokenCapacity
              << "\n";
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
  std::atomic<uint64_t> clientKeyFrameRequestDropped{0};
  std::atomic<bool> runtimeTunePending{false};
  std::atomic<uint32_t> runtimeTuneBitrate{0};
  std::atomic<uint32_t> runtimeTuneKeyint{0};
  std::atomic<uint32_t> runtimeTuneSeq{0};
  double keyReqTokens = static_cast<double>(keyReqTokenCapacity);
  uint64_t keyReqLastRefillUs = 0;
  uint64_t keyReqNextAllowedUs = 0;
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
              const uint64_t nowUs = qpc_now_us();
              if (keyReqLastRefillUs == 0) keyReqLastRefillUs = nowUs;
              if (nowUs > keyReqLastRefillUs) {
                const double refill =
                    static_cast<double>(nowUs - keyReqLastRefillUs) / static_cast<double>(keyReqTokenRefillUs);
                if (refill > 0.0) {
                  keyReqTokens = std::min<double>(static_cast<double>(keyReqTokenCapacity), keyReqTokens + refill);
                  keyReqLastRefillUs = nowUs;
                }
              }
              const bool minIntervalOk = (keyReqNextAllowedUs == 0 || nowUs >= keyReqNextAllowedUs);
              if (keyReqTokens >= 1.0 && minIntervalOk) {
                keyReqTokens -= 1.0;
                keyReqNextAllowedUs = nowUs + keyReqMinIntervalUs;
                clientRequestedKeyFrame = true;
                clientKeyFrameReason = req.reason;
                const uint64_t reqCount = clientKeyFrameRequestCount.fetch_add(1) + 1;
                std::cout << "[native-video-host][control] keyframe-request seq=" << req.seq
                          << " reason=" << req.reason
                          << " total=" << reqCount
                          << "\n";
              } else {
                const uint64_t dropCount = clientKeyFrameRequestDropped.fetch_add(1) + 1;
                if ((dropCount % 60) == 1) {
                  std::cout << "[native-video-host][control] keyframe-request-throttled seq=" << req.seq
                            << " reason=" << req.reason
                            << " dropped=" << dropCount
                            << " tokens=" << keyReqTokens
                            << "\n";
                }
              }
              continue;
            }

            if (type == MessageType::ControlRuntimeEncoderConfig &&
                header.size == sizeof(ControlRuntimeEncoderConfigMessage)) {
              ControlRuntimeEncoderConfigMessage tune{};
              tune.header = header;
              if (!recv_all(controlClientSock, &tune.seq, sizeof(tune) - sizeof(MessageHeader))) break;
              const bool hasBitrate = ((tune.flags & 0x1u) != 0) && tune.bitrate >= 100000;
              const bool hasKeyint = ((tune.flags & 0x2u) != 0) && tune.keyint >= 1;
              if (hasBitrate || hasKeyint) {
                if (hasBitrate) runtimeTuneBitrate.store(tune.bitrate, std::memory_order_release);
                if (hasKeyint) runtimeTuneKeyint.store(tune.keyint, std::memory_order_release);
                runtimeTuneSeq.store(tune.seq, std::memory_order_release);
                runtimeTunePending.store(true, std::memory_order_release);
                std::cout << "[native-video-host][control] runtime-config seq=" << tune.seq
                          << " bitrate=" << (hasBitrate ? tune.bitrate : 0)
                          << " keyint=" << (hasKeyint ? tune.keyint : 0)
                          << " flags=" << tune.flags
                          << "\n";
              }
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
  std::mutex d3dContextMu;
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
  GpuBgraScaler gpuScaler;
  bool gpuScalerHealthy = false;
  if (gpuScalerRequested) {
    gpuScalerHealthy = gpuScaler.initialize(d3d.Get(), ctx.Get(), &d3dContextMu);
    std::cout << "[native-video-host] gpuScalerRequested=1 gpuScalerReady="
              << (gpuScalerHealthy ? 1 : 0) << "\n";
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
  uint32_t activeKeyint = args.keyint;
  bool runtimeTuneManualOverride = false;
  uint64_t abrCooldownUntilUs = 0;
  uint32_t abrGoodSeconds = 0;
  uint32_t abrModeratePressureSeconds = 0;
  uint32_t abrSeverePressureSeconds = 0;
  int64_t captureTimelineOriginUs = -1;
  int64_t auTimelineOriginUs = -1;
  auto resetHostTimelineAnchors = [&]() {
    captureTimelineOriginUs = -1;
    auTimelineOriginUs = -1;
  };

  if (useH264) {
    if (!encoder.initialize(activeEncodeW, activeEncodeH, args.fps, activeBitrate, activeKeyint)) {
      std::cerr << "[native-video-host] H264 encoder initialize failed\n";
      closesocket(clientSock);
      if (mfStarted) MFShutdown();
      return 13;
    }
    resetHostTimelineAnchors();
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

  Direct3D11CaptureFramePool pool{nullptr};
  GraphicsCaptureSession session{nullptr};
  winrt::event_token token{};
  std::atomic<bool> captureSessionReady{false};
  uint64_t captureSessionStartedUs = 0;
  uint64_t captureRestartCount = 0;
  uint64_t lastCaptureRestartUs = 0;

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
  const auto update_u64_max = [](std::atomic<uint64_t>& target, const uint64_t value) {
    auto old = target.load(std::memory_order_relaxed);
    while (value > old && !target.compare_exchange_weak(old, value, std::memory_order_release, std::memory_order_relaxed)) {
    }
  };
  std::atomic<uint64_t> callbackFrames{0};
  std::atomic<int64_t> captureClockOffsetUs{std::numeric_limits<int64_t>::max()};
  uint64_t queuePushCount = 0;
  uint64_t queuePopCount = 0;
  uint64_t queueWaitTimeoutCount = 0;
  uint64_t queueWaitNoWorkCount = 0;
  std::atomic<uint64_t> lastPopFrameVersion{0};
  std::atomic<uint64_t> queueDepthMax{0};
  std::atomic<uint64_t> lastCallbackUs{0};
  std::atomic<uint64_t> lastCaptureUsForInterval{0};

  auto attach_frame_arrived = [&]() {
    token = pool.FrameArrived([&](Direct3D11CaptureFramePool const& sender,
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
        const uint32_t stride = width * 4;
        auto payload = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(stride) * height);
        {
          std::lock_guard<std::mutex> d3dLock(d3dContextMu);
          ctx->CopyResource(staging.Get(), src.Get());
          D3D11_MAPPED_SUBRESOURCE map{};
          if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return;
          auto* dst = payload->data();
          auto* srcRow = reinterpret_cast<const uint8_t*>(map.pData);
          for (uint32_t y = 0; y < height; ++y) {
            std::memcpy(dst + static_cast<size_t>(y) * stride,
                        srcRow + static_cast<size_t>(y) * map.RowPitch, stride);
          }
          ctx->Unmap(staging.Get(), 0);
        }
        const uint64_t callbackUs = qpc_now_us();
        const uint64_t queuePushUs = qpc_now_us();
        const uint64_t prevCallbackUs = lastCallbackUs.load(std::memory_order_acquire);
        const uint64_t prevCaptureUs = lastCaptureUsForInterval.load(std::memory_order_acquire);
        uint64_t sourceCaptureUs = callbackUs;
        uint64_t captureAgeAtCallbackUs = 0;
        uint64_t captureClockSkewUs = 0;
        uint64_t callbackIntervalUs = 0;
        uint64_t captureIntervalUs = 0;
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
            int64_t cur = captureClockOffsetUs.load(std::memory_order_acquire);
            if (cur == std::numeric_limits<int64_t>::max()) {
              captureClockOffsetUs.store(offsetCandidate, std::memory_order_release);
              cur = offsetCandidate;
            } else {
              while (offsetCandidate < cur &&
                     !captureClockOffsetUs.compare_exchange_weak(cur, offsetCandidate, std::memory_order_acq_rel,
                                                                std::memory_order_acquire)) {
              }
            }
            const int64_t bestOffset = captureClockOffsetUs.load();
            if (bestOffset != std::numeric_limits<int64_t>::max()) {
              const int64_t aligned = wgcUs + bestOffset;
              const int64_t alignedSkewUs = aligned - static_cast<int64_t>(callbackUs);
              if (aligned > 0 && alignedSkewUs >= -500000 && alignedSkewUs <= 500000) {
                captureClockSkewUs = alignedSkewUs >= 0
                    ? static_cast<uint64_t>(alignedSkewUs)
                    : static_cast<uint64_t>(-alignedSkewUs);
                sourceCaptureUs = static_cast<uint64_t>(aligned);
              }
            }
          }
        }
        if (prevCallbackUs > 0 && callbackUs >= prevCallbackUs) {
          callbackIntervalUs = callbackUs - prevCallbackUs;
        }
        if (prevCaptureUs > 0 && sourceCaptureUs >= prevCaptureUs) {
          captureIntervalUs = sourceCaptureUs - prevCaptureUs;
        }
        lastCallbackUs.store(callbackUs, std::memory_order_release);
        lastCaptureUsForInterval.store(sourceCaptureUs, std::memory_order_release);
        uint64_t currentVersion = 0;
        {
          std::lock_guard<std::mutex> lk(frame.mu);
          frame.payload = std::move(payload);
          frame.width = width;
          frame.height = height;
          frame.stride = stride;
          frame.captureUs = sourceCaptureUs;
          frame.callbackUs = callbackUs;
          frame.captureAgeAtCallbackUs = captureAgeAtCallbackUs;
          frame.captureClockSkewUs = captureClockSkewUs;
          frame.queuePushUs = queuePushUs;
          frame.callbackIntervalUs = callbackIntervalUs;
          frame.captureIntervalUs = captureIntervalUs;
          frame.seq += 1;
          frame.version += 1;
          currentVersion = frame.version;
        }
        const uint64_t currentPopVersion = lastPopFrameVersion.load(std::memory_order_acquire);
        const uint64_t depthNow = (currentVersion >= currentPopVersion) ? (currentVersion - currentPopVersion) : 0;
        update_u64_max(queueDepthMax, depthNow);
        ++queuePushCount;
        callbackFrames += 1;
        frame.cv.notify_one();
      } catch (...) {
      }
    });
  };

  auto detach_capture_session = [&]() {
    captureSessionReady.store(false, std::memory_order_release);
    try {
      if (pool) {
        pool.FrameArrived(token);
      }
    } catch (...) {
    }
    token = winrt::event_token{};
    try {
      if (session) session.Close();
    } catch (...) {
    }
    try {
      if (pool) pool.Close();
    } catch (...) {
    }
    session = nullptr;
    pool = nullptr;
  };

  auto restart_capture_session = [&]() -> bool {
    detach_capture_session();
    try {
      pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
          d3dDevice, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
          captureFramePoolBuffers, size);
      session = pool.CreateCaptureSession(item);
      attach_frame_arrived();
      session.StartCapture();
      captureSessionStartedUs = qpc_now_us();
      captureSessionReady.store(true, std::memory_order_release);
      return true;
    } catch (...) {
      detach_capture_session();
      return false;
    }
  };

  if (!restart_capture_session()) {
    std::cerr << "[native-video-host] capture session start failed\n";
    closesocket(clientSock);
    if (mfStarted) MFShutdown();
    return 10;
  }

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
  uint64_t udpTxFrames = 0;
  uint64_t udpTxChunks = 0;
  uint64_t udpTxBytes = 0;
  uint64_t udpTxFail = 0;
  uint64_t udpTxNoPeer = 0;
  uint64_t skippedByOverwrite = 0;
  uint64_t lastVersionSent = 0;
  uint64_t tracePrinted = 0;
  uint32_t encodedSeq = 0;
  uint64_t encodeFailCount = 0;
  uint64_t staleEncodedDropCount = 0;
  uint64_t stalePreEncodeDropCount = 0;
  uint64_t encoderResetCount = 0;
  uint64_t gpuScaleAttempts = 0;
  uint64_t gpuScaleSuccess = 0;
  uint64_t gpuScaleFail = 0;
  uint64_t gpuScaleCpuFallback = 0;
  uint64_t captureAgeSumUs = 0;
  uint64_t captureAgeMaxUs = 0;
  uint64_t callbackToEncodeStartSumUs = 0;
  uint64_t callbackToEncodeStartMaxUs = 0;
  uint32_t consecutiveStaleEncodedFrames = 0;
  uint64_t syntheticKeepaliveCount = 0;
  uint64_t lastSyntheticKeepaliveUs = 0;
  std::shared_ptr<std::vector<uint8_t>> keepalivePayload;
  uint32_t keepaliveW = 0;
  uint32_t keepaliveH = 0;
  uint32_t keepaliveStride = 0;
  uint32_t keepaliveSeq = 0;
  bool forceKeyNext = true;
  uint64_t lastSendStartUs = 0;
  std::shared_ptr<std::vector<uint8_t>> frameGatingRefPayload;
  uint32_t frameGatingRefW = 0;
  uint32_t frameGatingRefH = 0;
  uint32_t frameGatingRefStride = 0;
  uint32_t frameGatingStaticStreak = 0;
  uint32_t frameGatingMotionStreak = 0;
  bool frameGatingStaticMode = false;
  uint64_t frameGatingLastSentUs = 0;
  uint64_t frameGatingSkipCount = 0;
  uint64_t frameGatingStaticSkipCount = 0;
  uint64_t frameGatingChangePermilleLast = 1000;
  uint64_t frameGatingChangePermilleSum = 0;
  uint64_t frameGatingChangePermilleCount = 0;
  const uint64_t frameGatingStaticIntervalUs =
      std::max<uint64_t>(frameIntervalUs, std::max<uint64_t>(1, 1000000ULL / frameGatingStaticFps));

  while (!stop.load()) {
    const uint64_t nowUs = qpc_now_us();
    uint64_t tickWaitUs = 0;
    if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
      break;
    }
    if (useH264 && runtimeTunePending.exchange(false, std::memory_order_acq_rel)) {
      const uint32_t reqSeq = runtimeTuneSeq.load(std::memory_order_acquire);
      uint32_t targetBitrate = runtimeTuneBitrate.load(std::memory_order_acquire);
      uint32_t targetKeyint = runtimeTuneKeyint.load(std::memory_order_acquire);
      if (targetBitrate < 100000) targetBitrate = activeBitrate;
      if (targetKeyint < 1) targetKeyint = activeKeyint;
      const bool bitrateChanged = (targetBitrate != activeBitrate);
      const bool keyintChanged = (targetKeyint != activeKeyint);
      if (bitrateChanged || keyintChanged) {
        bool applyOk = true;
        if (keyintChanged) {
          encoder.shutdown();
          if (!encoder.initialize(activeEncodeW, activeEncodeH, args.fps, targetBitrate, targetKeyint)) {
            applyOk = false;
          } else {
            resetHostTimelineAnchors();
          }
        } else if (bitrateChanged) {
          if (!encoder.reconfigure_bitrate(targetBitrate)) {
            encoder.shutdown();
            if (!encoder.initialize(activeEncodeW, activeEncodeH, args.fps, targetBitrate, targetKeyint)) {
              applyOk = false;
            } else {
              resetHostTimelineAnchors();
            }
          }
        }
        if (!applyOk) {
          std::cerr << "[native-video-host][control] runtime-config apply failed seq=" << reqSeq << "\n";
          break;
        }
        activeBitrate = targetBitrate;
        activeKeyint = targetKeyint;
        runtimeTuneManualOverride = true;
        abrCooldownUntilUs = nowUs + 3000000ULL;
        abrGoodSeconds = 0;
        abrModeratePressureSeconds = 0;
        abrSeverePressureSeconds = 0;
        forceKeyNext = true;
        std::cout << "[native-video-host][control] runtime-config-applied seq=" << reqSeq
                  << " bitrate=" << activeBitrate
                  << " keyint=" << activeKeyint
                  << " abrOverride=1\n";
      }
    }
    if (captureSessionReady.load(std::memory_order_acquire)) {
      const uint64_t lastCbUs = lastCallbackUs.load(std::memory_order_acquire);
      const uint64_t sessionStartUs = captureSessionStartedUs;
      const uint64_t stallBaseUs = (lastCbUs > 0) ? lastCbUs : sessionStartUs;
      const bool restartCooldownDone =
          (lastCaptureRestartUs == 0 ||
           nowUs >= (lastCaptureRestartUs + kCaptureCallbackRestartCooldownUs));
      if (stallBaseUs > 0 && nowUs >= (stallBaseUs + kCaptureCallbackStallRestartUs) &&
          restartCooldownDone) {
        const uint64_t stallUs = nowUs - stallBaseUs;
        lastCaptureRestartUs = nowUs;
        const bool restarted = restart_capture_session();
        if (restarted) {
          ++captureRestartCount;
          captureClockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
          lastCaptureUsForInterval.store(0, std::memory_order_release);
          lastCallbackUs.store(0, std::memory_order_release);
          resetHostTimelineAnchors();
          forceKeyNext = true;
          lastSyntheticKeepaliveUs = 0;
          std::cout << "[native-video-host] capture session restarted count=" << captureRestartCount
                    << " stallUs=" << stallUs
                    << " lastCallbackUs=" << lastCbUs
                    << "\n";
        } else {
          std::cerr << "[native-video-host] capture session restart failed stallUs=" << stallUs
                    << "\n";
        }
      }
    }
    if (paceByTick) {
      if (nowUs < nextTickUs) {
        const uint64_t paceWaitStartUs = qpc_now_us();
        const uint64_t paceWaitTargetUs = nextTickUs - nowUs;
        std::this_thread::sleep_for(std::chrono::microseconds(paceWaitTargetUs));
        const uint64_t paceWaitDoneUs = qpc_now_us();
        tickWaitUs = (paceWaitDoneUs >= paceWaitStartUs) ? (paceWaitDoneUs - paceWaitStartUs) : 0;
        continue;
      }
      if (nowUs > nextTickUs) {
        nextTickUs = nowUs;
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
    uint64_t queuePushUs = 0;
    uint64_t callbackIntervalUs = 0;
    uint64_t captureIntervalUs = 0;
    uint64_t captureClockSkewUs = 0;
    uint64_t captureAgeAtCallbackUs = 0;
    uint64_t version = 0;
    uint32_t queueWaitReason = 0;  // 0: normal, 1: timeout, 2: no-work
    bool syntheticKeepaliveFrame = false;
    const uint64_t queueSelectStartUs = qpc_now_us();
    bool queueReady = false;
    {
      std::unique_lock<std::mutex> lk(frame.mu);
      queueReady = frame.cv.wait_for(lk, std::chrono::milliseconds(100), [&] {
        return stop.load() || frame.version != lastVersionSent;
      });
      if (!queueReady && !stop.load()) {
        queueWaitReason = 1;
        ++queueWaitTimeoutCount;
        const uint64_t timeoutNowUs = qpc_now_us();
        const uint64_t lastCbUs = lastCallbackUs.load(std::memory_order_acquire);
        const bool captureLikelyStalled =
            (lastCbUs > 0 && timeoutNowUs >= (lastCbUs + kCaptureStallKeepaliveStartUs));
        const bool keepaliveDue =
            (lastSyntheticKeepaliveUs == 0 ||
             timeoutNowUs >= (lastSyntheticKeepaliveUs + kCaptureStallKeepaliveIntervalUs));
        if (useH264 && captureLikelyStalled && keepaliveDue &&
            keepalivePayload && !keepalivePayload->empty() &&
            keepaliveW > 0 && keepaliveH > 0 && keepaliveStride >= (keepaliveW * 4u)) {
          syntheticKeepaliveFrame = true;
          payload = keepalivePayload;
          w = keepaliveW;
          h = keepaliveH;
          stride = keepaliveStride;
          keepaliveSeq = (keepaliveSeq == std::numeric_limits<uint32_t>::max()) ? 1u : (keepaliveSeq + 1u);
          seq = keepaliveSeq;
          version = lastVersionSent;
          captureUs = timeoutNowUs;
          callbackUs = timeoutNowUs;
          queuePushUs = timeoutNowUs;
          callbackIntervalUs = (lastCbUs > 0 && timeoutNowUs >= lastCbUs) ? (timeoutNowUs - lastCbUs) : 0;
          captureIntervalUs = callbackIntervalUs;
          captureAgeAtCallbackUs = 0;
          captureClockSkewUs = 0;
          ++syntheticKeepaliveCount;
          if ((syntheticKeepaliveCount % 30) == 1) {
            const uint64_t noCaptureUs =
                (lastCbUs > 0 && timeoutNowUs >= lastCbUs) ? (timeoutNowUs - lastCbUs) : 0;
            std::cout << "[native-video-host] synthetic keepalive count=" << syntheticKeepaliveCount
                      << " noCaptureUs=" << noCaptureUs
                      << "\n";
          }
        } else {
          continue;
        }
      }
      if (stop.load()) break;
      if (!syntheticKeepaliveFrame &&
          (frame.version == lastVersionSent || !frame.payload || frame.payload->empty())) {
        queueWaitReason = 2;
        ++queueWaitNoWorkCount;
        continue;
      }
      if (!syntheticKeepaliveFrame) {
        version = frame.version;
        payload = frame.payload;
        seq = frame.seq;
        w = frame.width;
        h = frame.height;
        stride = frame.stride;
        captureUs = frame.captureUs;
        callbackUs = frame.callbackUs;
        callbackIntervalUs = frame.callbackIntervalUs;
        captureIntervalUs = frame.captureIntervalUs;
        queuePushUs = frame.queuePushUs;
        captureAgeAtCallbackUs = frame.captureAgeAtCallbackUs;
        captureClockSkewUs = frame.captureClockSkewUs;
      }
    }
    if (!syntheticKeepaliveFrame) {
      keepalivePayload = payload;
      keepaliveW = w;
      keepaliveH = h;
      keepaliveStride = stride;
      keepaliveSeq = seq;
    }
  const uint64_t queuePopUs = qpc_now_us();
  const uint64_t queueSelectWaitUs =
      (queuePopUs >= queueSelectStartUs) ? (queuePopUs - queueSelectStartUs) : 0;
  const uint64_t frameAgeAtSelectUs =
      (callbackUs > 0 && queuePopUs >= callbackUs) ? (queuePopUs - callbackUs) : 0;
  const uint64_t captureToCallbackUs =
      (callbackUs > 0 && captureUs > 0)
          ? (callbackUs >= captureUs ? (callbackUs - captureUs) : (captureUs - callbackUs))
          : 0;
  const uint64_t captureToQueueUs =
      (queuePushUs > 0 && captureUs > 0)
          ? (queuePushUs >= captureUs ? (queuePushUs - captureUs) : (captureUs - queuePushUs))
          : 0;
    const uint64_t queueWaitUs =
        (queuePopUs > 0 && queuePushUs > 0 && queuePopUs >= queuePushUs) ? (queuePopUs - queuePushUs) : 0;
    const uint64_t queueGapFrames =
        (lastVersionSent > 0 && version > lastVersionSent) ? (version - lastVersionSent - 1) : 0;
    ++queuePopCount;
    const uint64_t lastPopVersionAtRead = lastPopFrameVersion.load(std::memory_order_acquire);
    const uint64_t queueDepthAtPop = (version > lastPopVersionAtRead) ? (version - lastPopVersionAtRead) : 0;
    update_u64_max(queueDepthMax, queueDepthAtPop);
    lastPopFrameVersion.store(version, std::memory_order_release);
    if (frameGatingEnabled && useH264 && !syntheticKeepaliveFrame && payload && !payload->empty()) {
      if (frameGatingRefPayload && !frameGatingRefPayload->empty() &&
          frameGatingRefW == w && frameGatingRefH == h && frameGatingRefStride == stride) {
        frameGatingChangePermilleLast = estimate_bgra_change_permille(
            payload->data(), frameGatingRefPayload->data(), payload->size(), frameGatingSampleTarget);
        frameGatingChangePermilleSum += frameGatingChangePermilleLast;
        ++frameGatingChangePermilleCount;

        if (frameGatingChangePermilleLast <= frameGatingStaticThresholdPermille) {
          frameGatingStaticStreak = std::min<uint32_t>(frameGatingStaticStreak + 1, 60000);
          frameGatingMotionStreak = 0;
        } else if (frameGatingChangePermilleLast >= frameGatingMotionThresholdPermille) {
          frameGatingMotionStreak = std::min<uint32_t>(frameGatingMotionStreak + 1, 60000);
          frameGatingStaticStreak = 0;
        } else {
          if (frameGatingStaticStreak > 0) --frameGatingStaticStreak;
          if (frameGatingMotionStreak > 0) --frameGatingMotionStreak;
        }
      } else {
        frameGatingStaticStreak = 0;
        frameGatingMotionStreak = 0;
        frameGatingChangePermilleLast = 1000;
      }

      const bool prevStaticMode = frameGatingStaticMode;
      if (!frameGatingStaticMode && frameGatingStaticStreak >= frameGatingEnterFrames) {
        frameGatingStaticMode = true;
      } else if (frameGatingStaticMode && frameGatingMotionStreak >= frameGatingExitFrames) {
        frameGatingStaticMode = false;
      }
      if (prevStaticMode != frameGatingStaticMode) {
        std::cout << "[native-video-host] frame-gating mode="
                  << (frameGatingStaticMode ? "static" : "motion")
                  << " changePm=" << frameGatingChangePermilleLast
                  << " staticStreak=" << frameGatingStaticStreak
                  << " motionStreak=" << frameGatingMotionStreak
                  << "\n";
      }

      const bool keyReqPending = clientRequestedKeyFrame.load(std::memory_order_acquire);
      const uint64_t targetIntervalUs = frameGatingStaticMode ? frameGatingStaticIntervalUs : frameIntervalUs;
      if (!keyReqPending &&
          frameGatingLastSentUs > 0 &&
          queuePopUs < (frameGatingLastSentUs + targetIntervalUs)) {
        ++frameGatingSkipCount;
        if (frameGatingStaticMode) ++frameGatingStaticSkipCount;
        lastVersionSent = version;
        continue;
      }
    }
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
    static uint64_t lastUserFeedbackUs = 0;
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
      SendPathStats sendPathStats{};
      const uint64_t sendStartUs = qpc_now_us();
      const uint64_t sendIntervalUs =
          (lastSendStartUs > 0 && sendStartUs >= lastSendStartUs) ? (sendStartUs - lastSendStartUs) : 0;
      const uint64_t sendIntervalErrUs =
          (frameIntervalUs > 0 && sendIntervalUs > 0)
              ? ((sendIntervalUs >= frameIntervalUs) ? (sendIntervalUs - frameIntervalUs)
                                                   : (frameIntervalUs - sendIntervalUs))
              : 0;
      const uint64_t queueToSendUs = (sendStartUs >= queuePopUs) ? (sendStartUs - queuePopUs) : 0;
      const uint64_t sendWaitUs = queueToSendUs;
      const uint64_t callbackToSendStartUs = (sendStartUs >= callbackUs) ? (sendStartUs - callbackUs) : 0;
      hdr.sendQpcUs = sendStartUs;
      const bool sentOk =
          (transport == VideoTransport::Tcp) &&
          send_all_timed(clientSock, &hdr, sizeof(hdr), &sendPathStats.headerUs,
                         &sendPathStats.headerCallCount) &&
          send_all_timed(clientSock, payload->data(), payload->size(), &sendPathStats.payloadUs,
                         &sendPathStats.payloadCallCount);
      const uint64_t sendDoneUs = qpc_now_us();
      const uint64_t sendDurUs = (sendDoneUs >= sendStartUs) ? (sendDoneUs - sendStartUs) : 0;
      const uint64_t sendCallCount = sendPathStats.headerCallCount + sendPathStats.payloadCallCount;
      if (sentOk) {
        lastSendStartUs = sendStartUs;
        if (frameGatingEnabled && useH264 && !syntheticKeepaliveFrame && payload && !payload->empty()) {
          frameGatingLastSentUs = sendStartUs;
          frameGatingRefPayload = payload;
          frameGatingRefW = w;
          frameGatingRefH = h;
          frameGatingRefStride = stride;
        }
      }

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
        const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
            queueWaitUs, 0, 0, 0, 0, encUs, queueToSendUs, sendDurUs, sendIntervalErrUs);
          std::cout << "[native-video-host][trace] seq=" << seq
                    << " captureUs=" << hdr.captureQpcUs
                    << " encodeStartUs=" << hdr.encodeStartQpcUs
                    << " encodeEndUs=" << hdr.encodeEndQpcUs
                    << " sendUs=" << hdr.sendQpcUs
                    << " bottleneckStageCode=" << bottleneck.code
                    << " bottleneckStageUs=" << bottleneck.us
                    << " bottleneckStageName=" << bottleneck.name
                    << " c2eUs=" << c2eUs
                    << " captureToCallbackUs=" << captureToCallbackUs
                    << " callbackIntervalUs=" << callbackIntervalUs
                    << " captureIntervalUs=" << captureIntervalUs
                    << " captureClockSkewUs=" << captureClockSkewUs
                    << " selectWaitUs=" << frameAgeAtSelectUs
                    << " queueSelectWaitUs=" << queueSelectWaitUs
                   << " queueGapFrames=" << queueGapFrames
                   << " queueDepth=" << queueDepthAtPop
                   << " queueDepthMax=" << queueDepthMax.load(std::memory_order_relaxed)
                   << " captureToQueueUs=" << captureToQueueUs
                   << " queueWaitUs=" << queueWaitUs
                   << " queueWaitReason=" << queueWaitReason
                   << " queueToSendUs=" << queueToSendUs
                   << " sendWaitUs=" << sendWaitUs
                   << " sendIntervalUs=" << sendIntervalUs
                   << " sendIntervalErrUs=" << sendIntervalErrUs
                   << " tickWaitUs=" << tickWaitUs
                   << " sendCallCount=" << sendCallCount
                   << " sendHeaderUs=" << sendPathStats.headerUs
                   << " sendPayloadUs=" << sendPathStats.payloadUs
                   << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                   << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                   << " sendChunkCount=" << sendPathStats.payloadChunkCount
                   << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                   << " sendStartUs=" << sendStartUs
                  << " sendDoneUs=" << sendDoneUs
                  << " sendDurUs=" << sendDurUs
                  << " encUs=" << encUs
                  << " e2sUs=" << e2sUs
                  << " payloadBytes=" << hdr.payloadSize
                  << "\n";
      }
      const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
      const uint64_t encUs = (hdr.encodeEndQpcUs >= hdr.encodeStartQpcUs) ? (hdr.encodeEndQpcUs - hdr.encodeStartQpcUs) : 0;
      const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
      const uint64_t pipeUs = (hdr.sendQpcUs >= hdr.captureQpcUs) ? (hdr.sendQpcUs - hdr.captureQpcUs) : 0;
      const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
          queueWaitUs, 0, 0, 0, 0, encUs, queueToSendUs, sendDurUs, sendIntervalErrUs);
      if (pipeUs >= kHostUserFeedbackWarnUs &&
          (hdr.sendQpcUs >= lastUserFeedbackUs + kHostUserFeedbackMinIntervalUs || lastUserFeedbackUs == 0)) {
        std::cout << "[native-video-host][user-feedback] seq=" << seq
                  << " codec=" << "raw"
                  << " pipeUs=" << pipeUs
                  << " bottleneckStageCode=" << bottleneck.code
                  << " bottleneckStageUs=" << bottleneck.us
                  << " bottleneckStageName=" << bottleneck.name
                  << " captureToCallbackUs=" << captureToCallbackUs
                  << " callbackIntervalUs=" << callbackIntervalUs
                  << " captureIntervalUs=" << captureIntervalUs
                  << " captureClockSkewUs=" << captureClockSkewUs
                  << " selectWaitUs=" << frameAgeAtSelectUs
                  << " queueSelectWaitUs=" << queueSelectWaitUs
                  << " captureToQueueUs=" << captureToQueueUs
                   << " queueWaitUs=" << queueWaitUs
                   << " queueWaitReason=" << queueWaitReason
                    << " queueGapFrames=" << queueGapFrames
                    << " queueDepth=" << queueDepthAtPop
                    << " queueDepthMax=" << queueDepthMax.load(std::memory_order_relaxed)
                    << " queueToSendUs=" << queueToSendUs
                    << " sendIntervalUs=" << sendIntervalUs
                    << " sendIntervalErrUs=" << sendIntervalErrUs
                     << " captureClockSkewUs=" << captureClockSkewUs
                     << " sendWaitUs=" << sendWaitUs
                   << " tickWaitUs=" << tickWaitUs
                   << " sendCallCount=" << sendCallCount
                   << " sendHeaderUs=" << sendPathStats.headerUs
                   << " sendPayloadUs=" << sendPathStats.payloadUs
                   << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                   << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                   << " sendChunkCount=" << sendPathStats.payloadChunkCount
                   << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                   << " c2eUs=" << c2eUs
                  << " cb2eUs=" << callbackToSendStartUs
                  << " encUs=" << encUs
                  << " e2sUs=" << e2sUs
                  << " sendStartUs=" << sendStartUs
                  << " sendDoneUs=" << sendDoneUs
                  << " sendDurUs=" << sendDurUs
                  << "\n";
        lastUserFeedbackUs = hdr.sendQpcUs;
      }
      } else {
        const uint8_t* encodeSrc = payload->data();
      uint32_t encodeSrcW = w;
      uint32_t encodeSrcH = h;
      uint32_t encodeSrcStride = stride;
      std::vector<uint8_t> scaledBgra;
      uint64_t preEncodePrepUs = 0;
      uint64_t scaleUs = 0;
      uint64_t nv12Us = 0;
      const uint64_t preEncodeStartUs = qpc_now_us();
      if (activeEncodeW != w || activeEncodeH != h) {
        const uint64_t scaleStartUs = qpc_now_us();
        bool scaleOk = false;
        if (gpuScalerHealthy) {
          ++gpuScaleAttempts;
          scaleOk = gpuScaler.scale(payload->data(), w, h, stride, activeEncodeW, activeEncodeH, &scaledBgra);
          if (scaleOk) {
            ++gpuScaleSuccess;
          } else {
            ++gpuScaleFail;
            gpuScalerHealthy = false;
            std::cout << "[native-video-host] gpu scaler disabled after failure; fallback=cpu\n";
          }
        }
        if (!scaleOk) {
          ++gpuScaleCpuFallback;
          if (!resize_bgra_bilinear(payload->data(), w, h, stride, activeEncodeW, activeEncodeH, &scaledBgra)) {
            continue;
          }
        }
        encodeSrc = scaledBgra.data();
        encodeSrcW = activeEncodeW;
        encodeSrcH = activeEncodeH;
        encodeSrcStride = activeEncodeW * 4;
        const uint64_t scaleDoneUs = qpc_now_us();
        scaleUs = (scaleDoneUs >= scaleStartUs) ? (scaleDoneUs - scaleStartUs) : 0;
      }

      std::vector<uint8_t> nv12;
      const uint64_t nv12StartUs = qpc_now_us();
      if (!bgra_to_nv12(encodeSrc, encodeSrcW, encodeSrcH, encodeSrcStride, &nv12) || nv12.empty()) {
        continue;
      }
      const uint64_t nv12DoneUs = qpc_now_us();
      nv12Us = (nv12DoneUs >= nv12StartUs) ? (nv12DoneUs - nv12StartUs) : 0;
      preEncodePrepUs = (nv12DoneUs >= preEncodeStartUs) ? (nv12DoneUs - preEncodeStartUs) : 0;

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
            syntheticKeepaliveFrame || forceKeyNext || (encodedSeq == 0) ||
            ((activeKeyint > 0) && ((seq % activeKeyint) == 0));
        const uint64_t encodeStartUs = qpc_now_us();
        const uint64_t encodeInputUs = captureStampUs;
        if (captureTimelineOriginUs < 0) {
          captureTimelineOriginUs = static_cast<int64_t>(encodeInputUs);
        }
        const uint64_t queueToEncodeUs = (encodeStartUs >= queuePopUs) ? (encodeStartUs - queuePopUs) : 0;
       const uint64_t callbackToEncodeStartUs =
            (encodeStartUs >= callbackUs) ? (encodeStartUs - callbackUs) : 0;
        std::vector<H264AccessUnit> units;
        H264EncodeFrameStats encodeStats{};
       if (!encoder.encode_frame(nv12, forceKeyFrame, static_cast<int64_t>(encodeInputUs) * 10, &units,
                                 &encodeStats)) {
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
          const int64_t auCaptureUs = (au.sampleTimeHns > 0) ? (au.sampleTimeHns / 10) : static_cast<int64_t>(encodeInputUs);
          if (auTimelineOriginUs < 0 && captureTimelineOriginUs >= 0) {
            auTimelineOriginUs = static_cast<int64_t>(auCaptureUs) -
                                 (static_cast<int64_t>(encodeInputUs) - captureTimelineOriginUs);
          }
          const int64_t captureTimelineRelativeUs = static_cast<int64_t>(encodeInputUs) - captureTimelineOriginUs;
          const int64_t auTimelineRelativeUs = static_cast<int64_t>(auCaptureUs) - auTimelineOriginUs;
          const int64_t captureToAuTimelineDeltaUs = captureTimelineRelativeUs - auTimelineRelativeUs;
          const uint64_t captureToAuTimelineSkewUs =
              (captureToAuTimelineDeltaUs >= 0)
                  ? static_cast<uint64_t>(captureToAuTimelineDeltaUs)
                  : static_cast<uint64_t>(-captureToAuTimelineDeltaUs);
          const int64_t captureToAuSignedDeltaUs = static_cast<int64_t>(auCaptureUs) - static_cast<int64_t>(encodeInputUs);
          const uint64_t captureToAuSkewUs =
              (captureToAuSignedDeltaUs >= 0)
                  ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                  : static_cast<uint64_t>(-captureToAuSignedDeltaUs);
          const uint64_t captureToAuUs = (captureToAuSignedDeltaUs >= 0)
                                             ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                                             : 0;
          const uint64_t encodedAgeUs =
              (encodeEndUs >= static_cast<uint64_t>(auCaptureUs))
                  ? (encodeEndUs - static_cast<uint64_t>(auCaptureUs))
                  : 0;
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
            if (!encoder.initialize(activeEncodeW, activeEncodeH, args.fps, activeBitrate, activeKeyint)) {
            std::cerr << "[native-video-host] encoder reinitialize failed\n";
              sendFailed = true;
              break;
            }
            resetHostTimelineAnchors();
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
        hdr.captureQpcUs = encodeInputUs;
        hdr.encodeStartQpcUs = encodeStartUs;
        hdr.encodeEndQpcUs = encodeEndUs;
        SendPathStats sendPathStats{};
        const uint64_t sendStartUs = qpc_now_us();
        const uint64_t sendIntervalUs =
            (lastSendStartUs > 0 && sendStartUs >= lastSendStartUs) ? (sendStartUs - lastSendStartUs) : 0;
        const uint64_t sendIntervalErrUs =
            (frameIntervalUs > 0 && sendIntervalUs > 0)
                ? ((sendIntervalUs >= frameIntervalUs) ? (sendIntervalUs - frameIntervalUs)
                                                     : (frameIntervalUs - sendIntervalUs))
                : 0;
        const uint64_t queueToSendUs = (sendStartUs >= queuePopUs) ? (sendStartUs - queuePopUs) : 0;
        const uint64_t sendToEncodeUs = (sendStartUs >= encodeEndUs) ? (sendStartUs - encodeEndUs) : 0;
        const uint64_t encodeSpanUs = (encodeEndUs >= encodeStartUs) ? (encodeEndUs - encodeStartUs) : 0;
        const uint64_t sendWaitUs =
            (queueToSendUs >= (queueToEncodeUs + encodeSpanUs))
                ? (queueToSendUs - queueToEncodeUs - encodeSpanUs)
                : 0;
        const uint64_t callbackToSendStartUs = (sendStartUs >= callbackUs) ? (sendStartUs - callbackUs) : 0;
        hdr.sendQpcUs = sendStartUs;

        bool sentOk = false;
        if (transport == VideoTransport::Tcp) {
          sentOk = send_all_timed(clientSock, &hdr, sizeof(hdr), &sendPathStats.headerUs,
                                  &sendPathStats.headerCallCount) &&
                   send_all_timed(clientSock, au.bytes.data(), au.bytes.size(), &sendPathStats.payloadUs,
                                 &sendPathStats.payloadCallCount);
        } else {
          if (!udpPeerReady) {
            ++udpTxNoPeer;
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
            sentOk = send_udp_chunks_timed(clientSock, udpPeer, au.bytes.data(), au.bytes.size(),
                                          udpHdr, args.udpMtu, &sendPathStats);
          }
        }
        const uint64_t sendDoneUs = qpc_now_us();
        const uint64_t sendDurUs = (sendDoneUs >= sendStartUs) ? (sendDoneUs - sendStartUs) : 0;
        const uint64_t sendCallCount = sendPathStats.headerCallCount + sendPathStats.payloadCallCount;
        if (sentOk) {
          lastSendStartUs = sendStartUs;
          if (transport == VideoTransport::Udp) {
            ++udpTxFrames;
            udpTxChunks += sendPathStats.payloadChunkCount;
            udpTxBytes += au.bytes.size();
          }
          if (frameGatingEnabled && !syntheticKeepaliveFrame && payload && !payload->empty()) {
            frameGatingLastSentUs = sendStartUs;
            frameGatingRefPayload = payload;
            frameGatingRefW = w;
            frameGatingRefH = h;
            frameGatingRefStride = stride;
          }
          if (syntheticKeepaliveFrame) {
            lastSyntheticKeepaliveUs = sendStartUs;
          }
        }
        if (!sentOk) {
          if (transport == VideoTransport::Udp) {
            ++udpTxFail;
          }
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
              (encodeStartUs >= static_cast<uint64_t>(auCaptureUs))
                  ? (encodeStartUs - static_cast<uint64_t>(auCaptureUs))
                  : 0;
          const uint64_t encQueueAlignedUs = (encodeStartUs >= encodeInputUs) ? (encodeStartUs - encodeInputUs) : 0;
          const uint64_t auTsFromOutput = au.sampleTimeFromOutput ? 1ull : 0ull;
          const uint64_t auTsSkewUs = (captureToAuSignedDeltaUs >= 0) ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                                                                     : static_cast<uint64_t>(-captureToAuSignedDeltaUs);
          const uint64_t encUs = (hdr.encodeEndQpcUs >= hdr.encodeStartQpcUs) ? (hdr.encodeEndQpcUs - hdr.encodeStartQpcUs) : 0;
          const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
          const char* encBackendName = encoder.backend_name();
          const uint64_t encApiPathCode = encoder_api_path_code(encBackendName);
          const uint64_t encApiHw = encoder.using_hardware() ? 1ull : 0ull;
          const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
              queueWaitUs, queueToEncodeUs, preEncodePrepUs, scaleUs, nv12Us, encUs, queueToSendUs,
              sendDurUs, sendIntervalErrUs);
          std::cout << "[native-video-host][trace] seq=" << hdr.seq
                    << " captureUs=" << hdr.captureQpcUs
                    << " encodeStartUs=" << hdr.encodeStartQpcUs
                    << " encodeEndUs=" << hdr.encodeEndQpcUs
                    << " sendUs=" << hdr.sendQpcUs
                    << " bottleneckStageCode=" << bottleneck.code
                    << " bottleneckStageUs=" << bottleneck.us
                    << " bottleneckStageName=" << bottleneck.name
                    << " c2eUs=" << c2eUs
                    << " captureToCallbackUs=" << captureToCallbackUs
                    << " callbackIntervalUs=" << callbackIntervalUs
                    << " captureIntervalUs=" << captureIntervalUs
                    << " captureClockSkewUs=" << captureClockSkewUs
                    << " selectWaitUs=" << frameAgeAtSelectUs
                     << " queueSelectWaitUs=" << queueSelectWaitUs
                     << " queueGapFrames=" << queueGapFrames
                     << " encQueueUs=" << encQueueUs
                     << " encQueueAlignedUs=" << encQueueAlignedUs
                     << " captureToAuSkewUs=" << captureToAuSkewUs
                     << " captureToAuTimelineDeltaUs="
                     << (captureToAuTimelineDeltaUs >= 0 ? captureToAuTimelineDeltaUs : 0 - captureToAuTimelineDeltaUs)
                      << " captureToAuTimelineSkewUs=" << captureToAuTimelineSkewUs
                      << " auTsFromOutput=" << auTsFromOutput
                      << " auTsSkewUs=" << auTsSkewUs
                      << " captureTimelineOriginUs=" << captureTimelineOriginUs
                     << " auTimelineOriginUs=" << auTimelineOriginUs
                     << " captureTimelineRelativeUs=" << captureTimelineRelativeUs
                     << " auTimelineRelativeUs=" << auTimelineRelativeUs
                      << " frameCaptureUs=" << captureStampUs
                      << " captureToAuUs=" << captureToAuUs
                      << " auCaptureUs=" << static_cast<uint64_t>(auCaptureUs)
                      << " encodeInputUs=" << encodeInputUs
                      << " captureToQueueUs=" << captureToQueueUs
                     << " queueWaitUs=" << queueWaitUs
                     << " queueWaitReason=" << queueWaitReason
                     << " queueToEncodeUs=" << queueToEncodeUs
                     << " queueToSendUs=" << queueToSendUs
                     << " sendIntervalUs=" << sendIntervalUs
                     << " sendIntervalErrUs=" << sendIntervalErrUs
                     << " preEncodePrepUs=" << preEncodePrepUs
                     << " scaleUs=" << scaleUs
                     << " nv12Us=" << nv12Us
                     << " sendWaitUs=" << sendWaitUs
                     << " sendToEncodeUs=" << sendToEncodeUs
                     << " tickWaitUs=" << tickWaitUs
                     << " queueDepth=" << queueDepthAtPop
                    << " queueDepthMax=" << queueDepthMax.load(std::memory_order_relaxed)
                    << " sendCallCount=" << sendCallCount
                    << " sendHeaderUs=" << sendPathStats.headerUs
                    << " sendPayloadUs=" << sendPathStats.payloadUs
                    << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                    << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                    << " sendChunkCount=" << sendPathStats.payloadChunkCount
                    << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                    << " sendStartUs=" << sendStartUs
                    << " sendDoneUs=" << sendDoneUs
                    << " sendDurUs=" << sendDurUs
                    << " cb2eUs=" << callbackToEncodeStartUs
                    << " capAgeUs=" << captureAgeAtCallbackUs
                    << " encUs=" << encUs
                    << " e2sUs=" << e2sUs
                    << " encApiPathCode=" << encApiPathCode
                    << " encApiHw=" << encApiHw
                    << " encApiInputUs=" << encodeStats.processInputUs
                    << " encApiDrainUs=" << encodeStats.processOutputDrainUs
                    << " encApiNotAcceptingCount=" << encodeStats.processInputNotAcceptingCount
                    << " encApiNeedMoreInputCount=" << encodeStats.processOutputNeedMoreInputCount
                    << " encApiStreamChangeCount=" << encodeStats.processOutputStreamChangeCount
                    << " encApiOutputErrorCount=" << encodeStats.processOutputErrorCount
                    << " encApiAsyncEnabled=" << encodeStats.asyncEnabled
                    << " encApiAsyncPollCount=" << encodeStats.asyncPollCount
                    << " encApiAsyncNoEventCount=" << encodeStats.asyncPollNoEventCount
                    << " encApiAsyncNeedInputCount=" << encodeStats.asyncPollNeedInputCount
                    << " encApiAsyncHaveOutputCount=" << encodeStats.asyncPollHaveOutputCount
                    << " payloadBytes=" << hdr.payloadSize
                    << " key=" << ((hdr.flags & 1u) ? 1 : 0)
                    << "\n";
        }
        const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
        const uint64_t encQueueUs =
            (encodeStartUs >= static_cast<uint64_t>(auCaptureUs)) ? (encodeStartUs - static_cast<uint64_t>(auCaptureUs)) : 0;
        const uint64_t encQueueAlignedUs = (encodeStartUs >= encodeInputUs) ? (encodeStartUs - encodeInputUs) : 0;
        const uint64_t auTsFromOutput = au.sampleTimeFromOutput ? 1ull : 0ull;
        const uint64_t auTsSkewUs = (captureToAuSignedDeltaUs >= 0) ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                                                                   : static_cast<uint64_t>(-captureToAuSignedDeltaUs);
        const uint64_t encUs = (hdr.encodeEndQpcUs >= hdr.encodeStartQpcUs) ? (hdr.encodeEndQpcUs - hdr.encodeStartQpcUs) : 0;
        const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
        const uint64_t pipeUs = (hdr.sendQpcUs >= hdr.captureQpcUs) ? (hdr.sendQpcUs - hdr.captureQpcUs) : 0;
        const char* encBackendName = encoder.backend_name();
        const uint64_t encApiPathCode = encoder_api_path_code(encBackendName);
        const uint64_t encApiHw = encoder.using_hardware() ? 1ull : 0ull;
        const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
            queueWaitUs, queueToEncodeUs, preEncodePrepUs, scaleUs, nv12Us, encUs, queueToSendUs,
            sendDurUs, sendIntervalErrUs);
        if (pipeUs >= kHostUserFeedbackWarnUs &&
            (hdr.sendQpcUs >= lastUserFeedbackUs + kHostUserFeedbackMinIntervalUs || lastUserFeedbackUs == 0)) {
        std::cout << "[native-video-host][user-feedback] seq=" << hdr.seq
                  << " codec=" << "h264"
                  << " pipeUs=" << pipeUs
                  << " bottleneckStageCode=" << bottleneck.code
                  << " bottleneckStageUs=" << bottleneck.us
                  << " bottleneckStageName=" << bottleneck.name
                  << " captureToCallbackUs=" << captureToCallbackUs
                    << " callbackIntervalUs=" << callbackIntervalUs
                    << " captureIntervalUs=" << captureIntervalUs
                    << " selectWaitUs=" << frameAgeAtSelectUs
                    << " queueSelectWaitUs=" << queueSelectWaitUs
                    << " captureClockSkewUs=" << captureClockSkewUs
                    << " captureToQueueUs=" << captureToQueueUs
                   << " queueWaitUs=" << queueWaitUs
                   << " queueWaitReason=" << queueWaitReason
                     << " queueGapFrames=" << queueGapFrames
                     << " queueDepth=" << queueDepthAtPop
                    << " queueDepthMax=" << queueDepthMax.load(std::memory_order_relaxed)
                    << " queueToEncodeUs=" << queueToEncodeUs
                    << " queueToSendUs=" << queueToSendUs
                    << " sendIntervalUs=" << sendIntervalUs
                    << " sendIntervalErrUs=" << sendIntervalErrUs
                    << " captureClockSkewUs=" << captureClockSkewUs
                    << " sendWaitUs=" << sendWaitUs
                    << " sendToEncodeUs=" << sendToEncodeUs
                     << " tickWaitUs=" << tickWaitUs
                     << " preEncodePrepUs=" << preEncodePrepUs
                     << " scaleUs=" << scaleUs
                     << " nv12Us=" << nv12Us
                     << " c2eUs=" << c2eUs
                      << " encQueueUs=" << encQueueUs
                     << " encQueueAlignedUs=" << encQueueAlignedUs
                      << " captureToAuSkewUs=" << captureToAuSkewUs
                      << " captureToAuTimelineSkewUs=" << captureToAuTimelineSkewUs
                      << " auTsFromOutput=" << auTsFromOutput
                      << " auTsSkewUs=" << auTsSkewUs
                      << " captureToAuTimelineDeltaUs="
                      << (captureToAuTimelineDeltaUs >= 0 ? captureToAuTimelineDeltaUs : 0 - captureToAuTimelineDeltaUs)
                      << " captureTimelineOriginUs=" << captureTimelineOriginUs
                      << " auTimelineOriginUs=" << auTimelineOriginUs
                      << " captureTimelineRelativeUs=" << captureTimelineRelativeUs
                      << " auTimelineRelativeUs=" << auTimelineRelativeUs
                      << " frameCaptureUs=" << captureStampUs
                      << " captureToAuUs=" << captureToAuUs
                     << " auCaptureUs=" << static_cast<uint64_t>(auCaptureUs)
                     << " encodeInputUs=" << encodeInputUs
                   << " cb2eUs=" << callbackToEncodeStartUs
                   << " cb2sUs=" << callbackToSendStartUs
                    << " sendCallCount=" << sendCallCount
                    << " sendHeaderUs=" << sendPathStats.headerUs
                    << " sendPayloadUs=" << sendPathStats.payloadUs
                    << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                    << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                    << " sendChunkCount=" << sendPathStats.payloadChunkCount
                    << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                    << " sendStartUs=" << sendStartUs
                    << " sendDoneUs=" << sendDoneUs
                    << " sendDurUs=" << sendDurUs
                    << " capAgeUs=" << captureAgeAtCallbackUs
                    << " encUs=" << encUs
                    << " e2sUs=" << e2sUs
                    << " encApiPathCode=" << encApiPathCode
                    << " encApiHw=" << encApiHw
                    << " encApiInputUs=" << encodeStats.processInputUs
                    << " encApiDrainUs=" << encodeStats.processOutputDrainUs
                    << " encApiNotAcceptingCount=" << encodeStats.processInputNotAcceptingCount
                    << " encApiNeedMoreInputCount=" << encodeStats.processOutputNeedMoreInputCount
                    << " encApiStreamChangeCount=" << encodeStats.processOutputStreamChangeCount
                    << " encApiOutputErrorCount=" << encodeStats.processOutputErrorCount
                    << " encApiAsyncEnabled=" << encodeStats.asyncEnabled
                    << " encApiAsyncPollCount=" << encodeStats.asyncPollCount
                    << " encApiAsyncNoEventCount=" << encodeStats.asyncPollNoEventCount
                    << " encApiAsyncNeedInputCount=" << encodeStats.asyncPollNeedInputCount
                    << " encApiAsyncHaveOutputCount=" << encodeStats.asyncPollHaveOutputCount
                    << " payloadBytes=" << hdr.payloadSize
                    << " key=" << ((hdr.flags & 1u) ? 1 : 0)
                    << "\n";
          lastUserFeedbackUs = hdr.sendQpcUs;
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
                  << " queuePushCount=" << queuePushCount
                  << " queuePopCount=" << queuePopCount
                  << " queueDepthMax=" << queueDepthMax.load(std::memory_order_relaxed)
                  << " queueWaitTimeoutCount=" << queueWaitTimeoutCount
                  << " queueWaitNoWorkCount=" << queueWaitNoWorkCount
                  << " syntheticKeepaliveCount=" << syntheticKeepaliveCount
                  << " captureRestarts=" << captureRestartCount
                  << " keyReqDropTotal=" << clientKeyFrameRequestDropped.load()
                  << " callbackFrames=" << callbackFrames.load()
                  << " skippedByOverwrite=" << skippedByOverwrite
                  << " frameGatingMode=" << (frameGatingStaticMode ? "static" : "motion")
                  << " frameGatingSkips=" << frameGatingSkipCount
                  << " frameGatingStaticSkips=" << frameGatingStaticSkipCount
                  << " mbps=" << mbps
                  << " size=" << w << "x" << h
                  << "\n";
      } else {
        const uint64_t capAgeAvgUs = (encodedFrames > 0) ? (captureAgeSumUs / encodedFrames) : 0;
        const uint64_t cb2eAvgUs = (encodedFrames > 0) ? (callbackToEncodeStartSumUs / encodedFrames) : 0;
        const uint64_t frameGatingChangeAvgPm =
            (frameGatingChangePermilleCount > 0)
                ? (frameGatingChangePermilleSum / frameGatingChangePermilleCount)
                : frameGatingChangePermilleLast;
        const double rawEquivMbps = (rawEquivalentBytes * 8.0) / (1000.0 * 1000.0);
        const uint64_t encRatioX100 =
            (sentBytes > 0) ? ((rawEquivalentBytes * 100ULL) / sentBytes) : 0;
        const uint64_t udpTxChunkPerFrameX100 =
            (udpTxFrames > 0) ? ((udpTxChunks * 100ULL) / udpTxFrames) : 0;
        std::cout << "[native-video-host] encodedFrames=" << encodedFrames
                  << " sentFrames=" << sentFrames
                  << " queuePushCount=" << queuePushCount
                  << " queuePopCount=" << queuePopCount
                  << " queueDepthMax=" << queueDepthMax.load(std::memory_order_relaxed)
                  << " queueWaitTimeoutCount=" << queueWaitTimeoutCount
                  << " queueWaitNoWorkCount=" << queueWaitNoWorkCount
                  << " syntheticKeepaliveCount=" << syntheticKeepaliveCount
                  << " captureRestarts=" << captureRestartCount
                  << " callbackFrames=" << callbackFrames.load()
                  << " skippedByOverwrite=" << skippedByOverwrite
                  << " stalePreEncodeDrops=" << stalePreEncodeDropCount
                  << " staleEncodedDrops=" << staleEncodedDropCount
                  << " encoderResets=" << encoderResetCount
                  << " keyReqTotal=" << clientKeyFrameRequestCount.load()
                  << " keyReqDropTotal=" << clientKeyFrameRequestDropped.load()
                  << " inputEvents=" << inputEvents.load()
                  << " capAgeAvgUs=" << capAgeAvgUs
                  << " capAgeMaxUs=" << captureAgeMaxUs
                  << " cb2eAvgUs=" << cb2eAvgUs
                  << " cb2eMaxUs=" << callbackToEncodeStartMaxUs
                  << " mbps=" << mbps
                  << " rawEquivMbps=" << rawEquivMbps
                  << " encRatioX100=" << encRatioX100
                  << " udpTxFrames=" << udpTxFrames
                  << " udpTxChunks=" << udpTxChunks
                  << " udpTxChunkPerFrameX100=" << udpTxChunkPerFrameX100
                  << " udpTxBytes=" << udpTxBytes
                  << " udpTxFail=" << udpTxFail
                  << " udpTxNoPeer=" << udpTxNoPeer
                  << " bitrateTarget=" << activeBitrate
                  << " keyintTarget=" << activeKeyint
                  << " size=" << activeEncodeW << "x" << activeEncodeH
                  << " gpuScaleReq=" << (gpuScalerRequested ? 1 : 0)
                  << " gpuScaleReady=" << (gpuScalerHealthy ? 1 : 0)
                  << " gpuScaleAttempts=" << gpuScaleAttempts
                  << " gpuScaleSuccess=" << gpuScaleSuccess
                  << " gpuScaleFail=" << gpuScaleFail
                  << " gpuScaleCpuFallback=" << gpuScaleCpuFallback
                  << " abrProfile=" << ((abrProfile == 0) ? "high" : ((abrProfile == 1) ? "mid" : "low"))
                  << " abrModSec=" << abrModeratePressureSeconds
                  << " abrSevSec=" << abrSeverePressureSeconds
                  << " abrGoodSec=" << abrGoodSeconds
                  << " abrOverride=" << (runtimeTuneManualOverride ? 1 : 0)
                  << " frameGatingMode=" << (frameGatingStaticMode ? "static" : "motion")
                  << " frameGatingSkips=" << frameGatingSkipCount
                  << " frameGatingStaticSkips=" << frameGatingStaticSkipCount
                  << " frameGatingChangePm=" << frameGatingChangePermilleLast
                  << " frameGatingChangeAvgPm=" << frameGatingChangeAvgPm
                  << "\n";

        if (abrEnabled && !runtimeTuneManualOverride) {
          const uint64_t metricsUpdatedUs = clientMetricsUpdatedUs.load();
          const bool metricsFresh =
              (metricsUpdatedUs > 0) && (t >= metricsUpdatedUs) && ((t - metricsUpdatedUs) <= 3000000ULL);

          const uint64_t clAvgLatencyUs = metricsFresh ? clientMetricsAvgLatencyUs.load() : 0;
          const uint64_t clAvgDecodeTailUs = metricsFresh ? clientMetricsAvgDecodeTailUs.load() : 0;
          const uint32_t clDecodedFpsX100 = metricsFresh ? clientMetricsDecodedFpsX100.load() : 0;
          const uint32_t clRecvMbpsX1000 = metricsFresh ? clientMetricsRecvMbpsX1000.load() : 0;
          const uint32_t clWidth = metricsFresh ? clientMetricsWidth.load() : 0;
          const uint32_t clHeight = metricsFresh ? clientMetricsHeight.load() : 0;

          const uint32_t minGoodFpsX100 = args.fps * (abrQualityFirst ? 95u : 93u);
          const uint32_t minOkayFpsX100 = args.fps * (abrQualityFirst ? 90u : 85u);
          const uint32_t minDegradeFpsX100 = args.fps * (abrQualityFirst ? 55u : 45u);
          const uint32_t minSevereFpsX100 = args.fps * (abrQualityFirst ? 45u : 35u);
          const bool abrWarmupDone = (t >= (startUs + 4000000ULL));

          const uint64_t severeLatencyUs = abrQualityFirst ? 170000ULL : 150000ULL;
          const uint64_t severeTailUs = abrQualityFirst ? 140000ULL : 110000ULL;
          const uint64_t moderateLatencyUs = abrQualityFirst ? 145000ULL : 125000ULL;
          const uint64_t moderateTailUs = abrQualityFirst ? 120000ULL : 90000ULL;
          const uint64_t emergencyLatencyUs = abrQualityFirst ? 260000ULL : 220000ULL;
          const uint64_t emergencyTailUs = abrQualityFirst ? 190000ULL : 160000ULL;

          const bool severeDownByClient =
              metricsFresh &&
              (clAvgLatencyUs > severeLatencyUs ||
               clAvgDecodeTailUs > severeTailUs ||
               (clDecodedFpsX100 < minSevereFpsX100 &&
                (clAvgLatencyUs > (severeLatencyUs - 30000ULL) || clAvgDecodeTailUs > (severeTailUs - 40000ULL))));
          const bool moderateDownByClient =
              metricsFresh &&
              (clAvgLatencyUs > moderateLatencyUs ||
               clAvgDecodeTailUs > moderateTailUs ||
               (clDecodedFpsX100 < minDegradeFpsX100 &&
                (clAvgLatencyUs > (moderateLatencyUs - 50000ULL) ||
                 clAvgDecodeTailUs > (moderateTailUs - 30000ULL))));
          const bool emergencyDownByClient =
              metricsFresh &&
              (clAvgLatencyUs > emergencyLatencyUs ||
               clAvgDecodeTailUs > emergencyTailUs);
          const bool severeDownByHost = (!metricsFresh && cb2eAvgUs > (abrQualityFirst ? 110000ULL : 90000ULL));
          const bool moderateDownByHost = (!metricsFresh && cb2eAvgUs > (abrQualityFirst ? 90000ULL : 70000ULL));
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
            const uint32_t highToMidSevereSec = abrQualityFirst ? 3u : 2u;
            const uint32_t highToMidModerateSec = abrQualityFirst ? 6u : 4u;
            const uint32_t midToLowSevereSec = abrQualityFirst ? 4u : 3u;
            const uint32_t midToLowModerateSec = abrQualityFirst ? 8u : 5u;
            const uint32_t lowToMidGoodSec = abrQualityFirst ? 8u : 5u;
            const uint32_t midToHighGoodSec = abrQualityFirst ? 12u : 8u;

            if (abrProfile == 0) {
              if (emergencyDown && abrHasLowProfile && abrSeverePressureSeconds >= 1) {
                targetProfile = 2;
                abrReason = "client_emergency";
              } else if ((abrSeverePressureSeconds >= highToMidSevereSec) || (abrModeratePressureSeconds >= highToMidModerateSec)) {
                if (abrHasMidProfile) {
                  targetProfile = 1;
                  abrReason = (abrSeverePressureSeconds >= highToMidSevereSec) ? "high_to_mid_severe" : "high_to_mid_moderate";
                } else if (abrHasLowProfile) {
                  targetProfile = 2;
                  abrReason = (abrSeverePressureSeconds >= highToMidSevereSec) ? "high_to_low_severe" : "high_to_low_moderate";
                }
              }
              abrGoodSeconds = 0;
            } else if (abrProfile == 1) {
              if (emergencyDown && abrHasLowProfile) {
                targetProfile = 2;
                abrReason = "client_emergency";
                abrGoodSeconds = 0;
              } else if ((abrSeverePressureSeconds >= midToLowSevereSec || abrModeratePressureSeconds >= midToLowModerateSec) && abrHasLowProfile) {
                targetProfile = 2;
                abrReason = (abrSeverePressureSeconds >= midToLowSevereSec) ? "mid_to_low_severe" : "mid_to_low_moderate";
                abrGoodSeconds = 0;
              } else {
                if (goodForMidToHigh) {
                  ++abrGoodSeconds;
                } else {
                  abrGoodSeconds = 0;
                }
                if (abrGoodSeconds >= midToHighGoodSec) {
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
              if (abrGoodSeconds >= lowToMidGoodSec) {
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
              if (!encoder.initialize(targetW, targetH, args.fps, targetBitrate, activeKeyint)) {
                std::cerr << "[native-video-host][abr] encoder reinitialize failed for profile switch\n";
                switchOk = false;
              } else {
                resetHostTimelineAnchors();
              }
            } else if (targetBitrate != activeBitrate) {
              if (!encoder.reconfigure_bitrate(targetBitrate)) {
                encoder.shutdown();
                if (!encoder.initialize(targetW, targetH, args.fps, targetBitrate, activeKeyint)) {
                  std::cerr << "[native-video-host][abr] encoder bitrate reconfigure/reinit failed\n";
                  switchOk = false;
                } else {
                  resetHostTimelineAnchors();
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
      udpTxFrames = 0;
      udpTxChunks = 0;
      udpTxBytes = 0;
      udpTxFail = 0;
      udpTxNoPeer = 0;
      skippedByOverwrite = 0;
      stalePreEncodeDropCount = 0;
      staleEncodedDropCount = 0;
      encoderResetCount = 0;
      callbackFrames = 0;
      captureAgeSumUs = 0;
      captureAgeMaxUs = 0;
      callbackToEncodeStartSumUs = 0;
      callbackToEncodeStartMaxUs = 0;
      gpuScaleAttempts = 0;
      gpuScaleSuccess = 0;
      gpuScaleFail = 0;
      gpuScaleCpuFallback = 0;
      syntheticKeepaliveCount = 0;
      frameGatingSkipCount = 0;
      frameGatingStaticSkipCount = 0;
      frameGatingChangePermilleSum = 0;
      frameGatingChangePermilleCount = 0;
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
  detach_capture_session();
  closesocket(clientSock);
  if (useH264) {
    encoder.shutdown();
    if (mfStarted) MFShutdown();
  }
  std::cout << "[native-video-host] done\n";
  return 0;
}

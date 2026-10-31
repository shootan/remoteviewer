#include "capture_runtime.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <windows.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

namespace remote60::host {
namespace {

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
  winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(tex.GetAddressOf())));
  return tex;
}

}  // namespace

CaptureLoopStats run_capture_loop_seconds(int seconds) {
  CaptureLoopStats out;

  using namespace winrt;
  using namespace winrt::Windows::Graphics;
  using namespace winrt::Windows::Graphics::Capture;
  using namespace winrt::Windows::Graphics::DirectX;
  using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

  init_apartment(apartment_type::multi_threaded);

  if (!GraphicsCaptureSession::IsSupported()) {
    out.detail = "wgc_not_supported";
    return out;
  }

  Microsoft::WRL::ComPtr<ID3D11Device> d3d;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
  D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                 nullptr, 0, D3D11_SDK_VERSION, &d3d, &fl, &ctx);
  if (FAILED(hr)) {
    out.detail = "d3d11_create_failed";
    return out;
  }

  auto item = CreateItemForPrimaryMonitor();
  if (!item) {
    out.detail = "capture_item_failed";
    return out;
  }

  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
  d3d.As(&dxgi);

  winrt::com_ptr<::IInspectable> inspectable;
  winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
  auto d3dDevice = inspectable.as<IDirect3DDevice>();

  const auto size = item.Size();
  auto pool = Direct3D11CaptureFramePool::CreateFreeThreaded(d3dDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                                              2, size);
  auto session = pool.CreateCaptureSession(item);

  // M3 skeleton: GPU BGRA->NV12 conversion path via D3D11 VideoProcessor.
  Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice;
  Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext;
  d3d.As(&videoDevice);
  ctx.As(&videoContext);
  if (!videoDevice || !videoContext) {
    out.detail = "video_device_unavailable";
    return out;
  }

  D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc{};
  contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  contentDesc.InputWidth = static_cast<UINT>(size.Width);
  contentDesc.InputHeight = static_cast<UINT>(size.Height);
  contentDesc.OutputWidth = static_cast<UINT>(size.Width);
  contentDesc.OutputHeight = static_cast<UINT>(size.Height);
  contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

  Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> vpEnum;
  hr = videoDevice->CreateVideoProcessorEnumerator(&contentDesc, &vpEnum);
  if (FAILED(hr) || !vpEnum) {
    out.detail = "vp_enum_failed";
    return out;
  }

  Microsoft::WRL::ComPtr<ID3D11VideoProcessor> vp;
  hr = videoDevice->CreateVideoProcessor(vpEnum.Get(), 0, &vp);
  if (FAILED(hr) || !vp) {
    out.detail = "vp_create_failed";
    return out;
  }

  bool conversionEnabled = true;
  std::string conversionReason = "nv12_ready";

  UINT nv12Support = 0;
  hr = vpEnum->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &nv12Support);
  if (FAILED(hr) || (nv12Support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0) {
    conversionEnabled = false;
    conversionReason = "nv12_output_unsupported";
  }

  D3D11_TEXTURE2D_DESC nv12Desc{};
  nv12Desc.Width = static_cast<UINT>(size.Width);
  nv12Desc.Height = static_cast<UINT>(size.Height);
  nv12Desc.MipLevels = 1;
  nv12Desc.ArraySize = 1;
  nv12Desc.Format = DXGI_FORMAT_NV12;
  nv12Desc.SampleDesc.Count = 1;
  nv12Desc.Usage = D3D11_USAGE_DEFAULT;
  nv12Desc.BindFlags = D3D11_BIND_RENDER_TARGET;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> nv12Tex;
  if (conversionEnabled) {
    hr = d3d->CreateTexture2D(&nv12Desc, nullptr, &nv12Tex);
    if (FAILED(hr) || !nv12Tex) {
      conversionEnabled = false;
      conversionReason = "nv12_tex_create_failed";
    }
  }

  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ovDesc{};
  ovDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
  ovDesc.Texture2D.MipSlice = 0;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> outputView;
  if (conversionEnabled) {
    hr = videoDevice->CreateVideoProcessorOutputView(nv12Tex.Get(), vpEnum.Get(), &ovDesc, &outputView);
    if (FAILED(hr) || !outputView) {
      conversionEnabled = false;
      conversionReason = "vp_output_view_failed";
    }
  }

  std::atomic<uint64_t> frames{0};
  std::atomic<uint64_t> callbacks{0};
  std::atomic<uint64_t> converted{0};
  std::atomic<uint64_t> convertFailures{0};
  std::atomic<uint64_t> convertTimeNs{0};

  auto token = pool.FrameArrived([&](Direct3D11CaptureFramePool const& sender,
                                     winrt::Windows::Foundation::IInspectable const&) {
    ++callbacks;
    while (true) {
      auto frame = sender.TryGetNextFrame();
      if (!frame) break;
      ++frames;

      if (conversionEnabled) {
        const auto t0 = std::chrono::steady_clock::now();
        try {
          auto srcTex = SurfaceToTexture(frame.Surface());
          if (!srcTex) {
            ++convertFailures;
            continue;
          }

          D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC ivDesc{};
          ivDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
          ivDesc.Texture2D.MipSlice = 0;
          ivDesc.Texture2D.ArraySlice = 0;

          Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> inputView;
          HRESULT viewHr = videoDevice->CreateVideoProcessorInputView(srcTex.Get(), vpEnum.Get(), &ivDesc, &inputView);
          if (FAILED(viewHr) || !inputView) {
            ++convertFailures;
            continue;
          }

          RECT r{0, 0, size.Width, size.Height};
          videoContext->VideoProcessorSetStreamFrameFormat(vp.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
          videoContext->VideoProcessorSetStreamSourceRect(vp.Get(), 0, TRUE, &r);
          videoContext->VideoProcessorSetStreamDestRect(vp.Get(), 0, TRUE, &r);
          videoContext->VideoProcessorSetOutputTargetRect(vp.Get(), TRUE, &r);

          D3D11_VIDEO_PROCESSOR_STREAM stream{};
          stream.Enable = TRUE;
          stream.pInputSurface = inputView.Get();

          HRESULT bltHr = videoContext->VideoProcessorBlt(vp.Get(), outputView.Get(), 0, 1, &stream);
          if (FAILED(bltHr)) {
            ++convertFailures;
            continue;
          }

          ++converted;
        } catch (...) {
          ++convertFailures;
        }
        const auto t1 = std::chrono::steady_clock::now();
        convertTimeNs += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
      }
    }
  });

  auto start = std::chrono::steady_clock::now();
  session.StartCapture();

  const auto endAt = start + std::chrono::seconds(seconds);
  const auto tickInterval = std::chrono::microseconds(16667);  // ~60Hz
  auto nextTick = start;
  uint64_t consumedFreshFrames = 0;
  bool hasAnyFrame = false;

  while (std::chrono::steady_clock::now() < endAt) {
    nextTick += tickInterval;
    std::this_thread::sleep_until(nextTick);

    ++out.ticks;

    const uint64_t currentFrames = frames.load();
    if (currentFrames > 0) hasAnyFrame = true;

    if (currentFrames > consumedFreshFrames) {
      consumedFreshFrames = currentFrames;
      ++out.freshTicks;
    } else if (hasAnyFrame) {
      ++out.reusedTicks;
    } else {
      ++out.idleTicks;
    }
  }

  session.Close();
  pool.FrameArrived(token);
  pool.Close();

  auto end = std::chrono::steady_clock::now();
  std::chrono::duration<double> elapsed = end - start;

  out.frames = frames.load();
  out.callbacks = callbacks.load();
  out.convertedFrames = converted.load();
  out.convertFailures = convertFailures.load();
  out.elapsedSec = elapsed.count();
  out.fps = out.elapsedSec > 0.0 ? static_cast<double>(out.frames) / out.elapsedSec : 0.0;
  out.timelineFps = out.elapsedSec > 0.0 ? static_cast<double>(out.ticks) / out.elapsedSec : 0.0;
  out.avgConvertMs = out.convertedFrames > 0
                         ? (static_cast<double>(convertTimeNs.load()) / 1'000'000.0) /
                               static_cast<double>(out.convertedFrames)
                         : 0.0;
  out.detail = conversionEnabled ? "ok" : ("ok_m3_skeleton_no_nv12:" + conversionReason);
  return out;
}

}  // namespace remote60::host

#pragma once

// D3D11 NV12 presenter of the viewer window.
//
// Role:    Nv12D3dRenderer -- device/swap chain/shaders, NV12 texture upload (render) or direct
//          decoder-surface sampling (render_surface), letterboxed draw and Present; plus the
//          Nv12RenderTelemetry timings the present trace logs.
// Thread:  UI only (the swap chain belongs to the window thread); the device is shared with the
//          decoder when the DXGI decode-surface opt-in is on.
// Input:   NV12 bytes or an ID3D11Texture2D + visible rect, the destination rect.
// Output:  a presented frame; false with failStage on error (the caller falls back to GDI).
// Callers: WM_PAINT (viewer_window_proc / viewer_present), startup (device sharing).
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-7); the struct
// keeps its in-class member definitions, so this stays header-only.

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

struct Nv12RenderTelemetry {
  uint64_t uploadYUs = 0;
  uint64_t uploadUVUs = 0;
  uint64_t drawUs = 0;
  uint64_t presentBlockUs = 0;
  const char* failStage = "none";
};

struct Nv12D3dRenderer {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
  Microsoft::WRL::ComPtr<ID3D11Buffer> uvConstants;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texY;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texUV;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srvY;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srvUV;
  uint32_t texW = 0;
  uint32_t texH = 0;
  UINT rtvW = 0;
  UINT rtvH = 0;
  uint64_t rtvCreateCount = 0;
  uint64_t rtvResizeCount = 0;
  bool ready = false;

  // Drop the swapchain so DWM stops compositing the last presented frame over this window.
  //
  // A flip-model swapchain bound to the HWND is composited ABOVE anything GDI draws into the same
  // window, so the picker overlay was painted underneath it and never seen: the user pressed
  // "target select" and got a frozen-looking last frame instead of the picker. Releasing the
  // swapchain returns the window to ordinary GDI redirection.
  //
  // The device and context deliberately survive -- the hardware decoder shares them
  // (viewer_startup.cpp binds ctx.dec.d3dDevice to this device), so tearing them down here would
  // break decoding. init() below reuses an existing device and only rebuilds the swapchain.
  // (Viewer ledger F-21.)
  void release_swapchain() {
    if (context) {
      ID3D11RenderTargetView* none[] = {nullptr};
      context->OMSetRenderTargets(1, none, nullptr);
      context->Flush();
    }
    rtv.Reset();
    rtvW = 0;
    rtvH = 0;
    swapChain.Reset();
    ready = false;
  }

  bool init(HWND hwnd) {
    // Reused on a picker round trip: release_swapchain() keeps the device alive because the
    // decoder holds it, so only recreate one when there is none. (F-21.)
    if (!device || !context) {
      UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
      D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
      D3D_FEATURE_LEVEL outLevel = D3D_FEATURE_LEVEL_11_0;
      HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                     levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                     &device, &outLevel, &context);
      if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                               levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                               &device, &outLevel, &context);
        if (FAILED(hr)) return false;
      }
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device.As(&dxgiDevice))) return false;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;
    Microsoft::WRL::ComPtr<IDXGIFactory> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    // Flip-discard presents by reference through DWM instead of blitting the whole frame;
    // the legacy discard model costs a full-frame copy per present. Falls back for the
    // rare pre-Win10 driver that rejects the flip model.
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(factory->CreateSwapChain(device.Get(), &sd, &swapChain))) {
      sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
      if (FAILED(factory->CreateSwapChain(device.Get(), &sd, &swapChain))) return false;
    }
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    static const char* kVsSrc =
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
        "VSOut main(uint id : SV_VertexID) {"
        "  float2 p = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? 3.0 : -1.0);"
        "  VSOut o;"
        "  o.pos = float4(p, 0, 1);"
        "  o.uv = float2((p.x + 1.0) * 0.5, 1.0 - ((p.y + 1.0) * 0.5));"
        "  return o;"
        "}";
    static const char* kPsSrc =
        "cbuffer FrameConstants : register(b0) { float4 uvRect; };"
        "Texture2D texY : register(t0);"
        "Texture2D texUV : register(t1);"
        "SamplerState smp : register(s0);"
        "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {"
        "  float2 sampleUv = uvRect.xy + uv * uvRect.zw;"
        "  float y = texY.Sample(smp, sampleUv).r;"
        "  float2 c = texUV.Sample(smp, sampleUv).rg;"
        "  float Y = max(0.0, y - 16.0 / 255.0);"
        "  float U = c.x - 128.0 / 255.0;"
        "  float V = c.y - 128.0 / 255.0;"
        // BT.709 limited range; must match bgra_to_nv12/nv12_to_bgra in mf_h264_codec.cpp.
        "  float r = 1.16438356 * Y + 1.79274107 * V;"
        "  float g = 1.16438356 * Y - 0.21324861 * U - 0.53290933 * V;"
        "  float b = 1.16438356 * Y + 2.11240178 * U;"
        "  return float4(saturate(r), saturate(g), saturate(b), 1.0);"
        "}";

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    if (FAILED(D3DCompile(kVsSrc, std::strlen(kVsSrc), nullptr, nullptr, nullptr,
                          "main", "vs_4_0", 0, 0, &vsBlob, &errBlob))) {
      return false;
    }
    if (FAILED(D3DCompile(kPsSrc, std::strlen(kPsSrc), nullptr, nullptr, nullptr,
                          "main", "ps_4_0", 0, 0, &psBlob, &errBlob))) {
      return false;
    }
    if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs))) {
      return false;
    }
    if (FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps))) {
      return false;
    }

    D3D11_SAMPLER_DESC sdSamp{};
    sdSamp.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sdSamp.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdSamp.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdSamp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdSamp.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sdSamp, &sampler))) return false;

    D3D11_BUFFER_DESC constantsDesc{};
    constantsDesc.ByteWidth = 16;
    constantsDesc.Usage = D3D11_USAGE_DEFAULT;
    constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&constantsDesc, nullptr, &uvConstants))) return false;

    ready = ensure_rtv(hwnd);
    return ready;
  }

  bool ensure_rtv(HWND hwnd) {
    if (!swapChain || !device) return false;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const UINT w = std::max<LONG>(1, rc.right - rc.left);
    const UINT h = std::max<LONG>(1, rc.bottom - rc.top);

    // The steady state is a cache hit: recreating the view every frame also re-queried the
    // swapchain descriptor every frame, all of it for a window that had not moved.
    if (rtv && rtvW == w && rtvH == h) return true;

    DXGI_SWAP_CHAIN_DESC sd{};
    if (FAILED(swapChain->GetDesc(&sd))) return false;
    if (sd.BufferDesc.Width != w || sd.BufferDesc.Height != h) {
      rtv.Reset();
      ++rtvResizeCount;
      if (FAILED(swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return false;
    if (FAILED(device->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv))) return false;
    ++rtvCreateCount;
    rtvW = w;
    rtvH = h;
    return true;
  }

  bool ensure_nv12_textures(uint32_t w, uint32_t h) {
    if (!device) return false;
    if (texY && texUV && texW == w && texH == h) return true;

    texY.Reset();
    texUV.Reset();
    srvY.Reset();
    srvUV.Reset();
    texW = 0;
    texH = 0;

    D3D11_TEXTURE2D_DESC yDesc{};
    yDesc.Width = w;
    yDesc.Height = h;
    yDesc.MipLevels = 1;
    yDesc.ArraySize = 1;
    yDesc.Format = DXGI_FORMAT_R8_UNORM;
    yDesc.SampleDesc.Count = 1;
    yDesc.Usage = D3D11_USAGE_DYNAMIC;
    yDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    yDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateTexture2D(&yDesc, nullptr, &texY))) return false;

    D3D11_TEXTURE2D_DESC uvDesc{};
    uvDesc.Width = w / 2;
    uvDesc.Height = h / 2;
    uvDesc.MipLevels = 1;
    uvDesc.ArraySize = 1;
    uvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    uvDesc.SampleDesc.Count = 1;
    uvDesc.Usage = D3D11_USAGE_DYNAMIC;
    uvDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    uvDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateTexture2D(&uvDesc, nullptr, &texUV))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC ySrvDesc{};
    ySrvDesc.Format = DXGI_FORMAT_R8_UNORM;
    ySrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    ySrvDesc.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(texY.Get(), &ySrvDesc, &srvY))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC uvSrvDesc{};
    uvSrvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    uvSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    uvSrvDesc.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(texUV.Get(), &uvSrvDesc, &srvUV))) return false;

    texW = w;
    texH = h;
    return true;
  }

  bool draw(HWND hwnd, const RECT& destRect, ID3D11ShaderResourceView* ySrv,
            ID3D11ShaderResourceView* uvSrv, const float uvRect[4],
            Nv12RenderTelemetry* telemetry) {
    if (!ensure_rtv(hwnd) || !ySrv || !uvSrv || !uvConstants) {
      if (telemetry) telemetry->failStage = "draw_args";
      return false;
    }
    context->UpdateSubresource(uvConstants.Get(), 0, nullptr, uvRect, 0, 0);
    RECT rc{};
    GetClientRect(hwnd, &rc);
    RECT drawRect = destRect;
    if (drawRect.right <= drawRect.left || drawRect.bottom <= drawRect.top) drawRect = rc;
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = static_cast<float>(drawRect.left);
    vp.TopLeftY = static_cast<float>(drawRect.top);
    vp.Width = static_cast<float>(std::max<LONG>(1, drawRect.right - drawRect.left));
    vp.Height = static_cast<float>(std::max<LONG>(1, drawRect.bottom - drawRect.top));
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    ID3D11RenderTargetView* rtvs[] = {rtv.Get()};
    context->OMSetRenderTargets(1, rtvs, nullptr);
    context->RSSetViewports(1, &vp);
    const float clearColor[4] = {0, 0, 0, 1};
    context->ClearRenderTargetView(rtv.Get(), clearColor);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->PSSetShader(ps.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[] = {ySrv, uvSrv};
    context->PSSetShaderResources(0, 2, srvs);
    ID3D11Buffer* constants[] = {uvConstants.Get()};
    context->PSSetConstantBuffers(0, 1, constants);
    ID3D11SamplerState* samplers[] = {sampler.Get()};
    context->PSSetSamplers(0, 1, samplers);
    const uint64_t drawStartUs = qpc_now_us();
    context->Draw(3, 0);
    ID3D11ShaderResourceView* nullSrvs[] = {nullptr, nullptr};
    context->PSSetShaderResources(0, 2, nullSrvs);
    const uint64_t drawEndUs = qpc_now_us();
    if (telemetry) telemetry->drawUs = drawEndUs - drawStartUs;

    const uint64_t presentStartUs = qpc_now_us();
    const HRESULT hr = swapChain->Present(0, 0);
    const uint64_t presentDoneUs = qpc_now_us();
    if (telemetry) telemetry->presentBlockUs = presentDoneUs - presentStartUs;
    if (!(SUCCEEDED(hr) || hr == DXGI_STATUS_OCCLUDED) && telemetry) telemetry->failStage = "present";
    return SUCCEEDED(hr) || hr == DXGI_STATUS_OCCLUDED;
  }

  bool render_surface(HWND hwnd, const RECT& destRect, ID3D11Texture2D* texture,
                      uint32_t subresource, uint32_t codedW, uint32_t codedH,
                      uint32_t visLeft, uint32_t visTop, uint32_t w, uint32_t h,
                      Nv12RenderTelemetry* telemetry) {
    if (telemetry) *telemetry = Nv12RenderTelemetry{};
    if (!ready || !texture || !codedW || !codedH || !w || !h) {
      if (telemetry) telemetry->failStage = "surface_args";
      return false;
    }
    D3D11_TEXTURE2D_DESC td{};
    texture->GetDesc(&td);
    if (td.Format != DXGI_FORMAT_NV12 || subresource >= td.MipLevels * td.ArraySize ||
        visLeft + w > codedW || visTop + h > codedH) {
      if (telemetry) telemetry->failStage = "surface_desc";
      return false;
    }
    Microsoft::WRL::ComPtr<ID3D11Device> textureDevice;
    texture->GetDevice(&textureDevice);
    if (textureDevice.Get() != device.Get()) {
      if (telemetry) telemetry->failStage = "surface_device";
      return false;
    }
    const UINT mipSlice = subresource % td.MipLevels;
    const UINT arraySlice = subresource / td.MipLevels;
    auto make_view = [&](DXGI_FORMAT format,
                         Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>* out) -> bool {
      D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
      desc.Format = format;
      if (td.ArraySize > 1) {
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MostDetailedMip = mipSlice;
        desc.Texture2DArray.MipLevels = 1;
        desc.Texture2DArray.FirstArraySlice = arraySlice;
        desc.Texture2DArray.ArraySize = 1;
      } else {
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MostDetailedMip = mipSlice;
        desc.Texture2D.MipLevels = 1;
      }
      return SUCCEEDED(device->CreateShaderResourceView(texture, &desc, out->ReleaseAndGetAddressOf()));
    };
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ySrv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> uvSrv;
    if (!make_view(DXGI_FORMAT_R8_UNORM, &ySrv) ||
        !make_view(DXGI_FORMAT_R8G8_UNORM, &uvSrv)) {
      if (telemetry) telemetry->failStage = "surface_srv";
      return false;
    }
    const float uvRect[4] = {static_cast<float>(visLeft) / codedW,
                             static_cast<float>(visTop) / codedH,
                             static_cast<float>(w) / codedW,
                             static_cast<float>(h) / codedH};
    return draw(hwnd, destRect, ySrv.Get(), uvSrv.Get(), uvRect, telemetry);
  }

  /**
   * Draws the visible rect (w x h at visLeft/visTop) out of a coded NV12 plane. The textures
   * are sized to the visible picture, so the shader never samples the coded padding rows --
   * uploading the full 1088-row plane stretched 8 garbage rows into a 1080p picture and
   * distorted the aspect by 0.74%.
   */
  bool render(HWND hwnd, const RECT& destRect, const uint8_t* nv12, uint32_t codedW,
              uint32_t codedH, uint32_t visLeft, uint32_t visTop, uint32_t w, uint32_t h,
              Nv12RenderTelemetry* telemetry) {
    if (telemetry) {
      *telemetry = Nv12RenderTelemetry{};
    }
    if (!ready || !nv12 || codedW == 0 || codedH == 0 || w == 0 || h == 0 || (codedW & 1u) ||
        (codedH & 1u) || (w & 1u) || (h & 1u) || (visLeft & 1u) || (visTop & 1u) ||
        visLeft + w > codedW || visTop + h > codedH) {
      if (telemetry) telemetry->failStage = "invalid_args";
      return false;
    }
    if (!ensure_rtv(hwnd)) {
      if (telemetry) telemetry->failStage = "ensure_rtv";
      return false;
    }
    if (!ensure_nv12_textures(w, h)) {
      if (telemetry) telemetry->failStage = "ensure_nv12_textures";
      return false;
    }

    const uint8_t* yPlane = nv12 + static_cast<size_t>(visTop) * codedW + visLeft;
    const uint8_t* uvPlane = nv12 + static_cast<size_t>(codedW) * codedH +
                             static_cast<size_t>(visTop / 2) * codedW + visLeft;

    const uint64_t uploadYStartUs = qpc_now_us();
    D3D11_MAPPED_SUBRESOURCE yMap{};
    if (FAILED(context->Map(texY.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &yMap))) {
      if (telemetry) telemetry->failStage = "map_y";
      return false;
    }
    if (codedW == w && static_cast<UINT>(w) == yMap.RowPitch) {
      std::memcpy(reinterpret_cast<uint8_t*>(yMap.pData), yPlane, static_cast<size_t>(h) * w);
    } else {
      for (uint32_t row = 0; row < h; ++row) {
        std::memcpy(reinterpret_cast<uint8_t*>(yMap.pData) + static_cast<size_t>(row) * yMap.RowPitch,
                    yPlane + static_cast<size_t>(row) * codedW, w);
      }
    }
    context->Unmap(texY.Get(), 0);
    if (telemetry) telemetry->uploadYUs = qpc_now_us() - uploadYStartUs;

    const uint64_t uploadUVStartUs = qpc_now_us();
    D3D11_MAPPED_SUBRESOURCE uvMap{};
    if (FAILED(context->Map(texUV.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &uvMap))) {
      if (telemetry) telemetry->failStage = "map_uv";
      return false;
    }
    const uint32_t uvHeight = h / 2;
    if (codedW == w && static_cast<UINT>(w) == uvMap.RowPitch) {
      std::memcpy(reinterpret_cast<uint8_t*>(uvMap.pData), uvPlane,
                  static_cast<size_t>(uvHeight) * w);
    } else {
      for (uint32_t row = 0; row < uvHeight; ++row) {
        std::memcpy(reinterpret_cast<uint8_t*>(uvMap.pData) + static_cast<size_t>(row) * uvMap.RowPitch,
                    uvPlane + static_cast<size_t>(row) * codedW, w);
      }
    }
    context->Unmap(texUV.Get(), 0);
    if (telemetry) telemetry->uploadUVUs = qpc_now_us() - uploadUVStartUs;

    const float uvRect[4] = {0, 0, 1, 1};
    return draw(hwnd, destRect, srvY.Get(), srvUV.Get(), uvRect, telemetry);
  }
};

}  // namespace remote60::native_poc::viewer

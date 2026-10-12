// encoder-rc-budget r13 (Codex C1): the attachment cookie a frame carries is the one its SOURCE was
// attached under, and PublishFrame's guard drops a frame whose cookie is not the live one.
//
// Built from the host's own compiled objects (every function here is the product's: CaptureState::
// PublishCapturedTexture, D3dCaptureReadbackPipeline, CaptureState::PublishFrame) with this file as
// the only replacement for main. A WARP device stands in for the GPU.
//
// The crossing the counterexample pins: the WGC worker checks "is my attachment still the live one"
// and then calls PublishCapturedTexture. A detach between those two steps bumps the live cookie. Until
// r13 PublishCapturedTexture stamped the LIVE cookie onto the meta, so the old frame carried the new
// cookie and passed the guard; now it stamps the cookie the caller gives it -- the one it was
// attached under -- and the guard drops the frame. The negative shape ("stamp the live cookie", what
// the pre-r13 code did) is run too: it must pass the guard, which is exactly the defect.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "host_capture_session.hpp"
#include "host_stats.hpp"

using namespace remote60::native_poc;
using Microsoft::WRL::ComPtr;

namespace {
int gPass = 0, gFail = 0;
void check(bool ok, const char* what, const std::string& detail = "") {
  std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : " -- ", detail.c_str());
  (ok ? gPass : gFail)++;
}
ComPtr<ID3D11Texture2D> source(ID3D11Device* dev, uint32_t w, uint32_t h, uint32_t pixel) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
  std::vector<uint32_t> px(static_cast<size_t>(w) * h, pixel);
  D3D11_SUBRESOURCE_DATA init{px.data(), w * 4, 0};
  ComPtr<ID3D11Texture2D> t;
  if (FAILED(dev->CreateTexture2D(&desc, &init, &t))) return nullptr;
  return t;
}
uint64_t qpc_us() {
  LARGE_INTEGER f{}, c{};
  QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
  return static_cast<uint64_t>(c.QuadPart) * 1000000ULL / static_cast<uint64_t>(f.QuadPart);
}
}  // namespace

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const uint32_t W = 64, H = 64;
  // The product's capture state, resources and stats, as main() would own them.
  auto capture = std::make_unique<CaptureState>();
  auto res = std::make_unique<CaptureResources>();
  auto stats = std::make_unique<HostStats>();
  D3D_FEATURE_LEVEL level{};
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &res->d3d, &level, &res->ctx))) {
    std::printf("SKIP capture_attachment_test: no WARP device\n");
    return 77;
  }
  // The same publish wiring host_startup_capture.cpp installs: the ring's worker calls PublishFrame.
  res->capturePublishFn = [&](std::shared_ptr<std::vector<uint8_t>> payload, uint32_t frameW, uint32_t frameH, uint32_t stride,
                               const CaptureFrameMeta& meta, uint64_t gpuPendingUs, uint64_t workerMapUs, uint64_t workerMemcpyUs) {
    capture->PublishFrame(*res, *stats, std::move(payload), frameW, frameH, stride, meta, gpuPendingUs, workerMapUs, workerMemcpyUs);
  };
  capture->width = W;
  capture->height = H;
  capture->submitLimitEnabled = false;  // every content frame is accepted by the cadence gate
  capture->stagingSlotCount = 2;
  check(res->captureReadback.Initialize(res->d3d.Get(), res->ctx.Get(), &res->d3dContextMu, W, H, 2, res->capturePublishFn), "readback ring initialised (WARP)");
  auto src1 = source(res->d3d.Get(), W, H, 0xff000001);
  auto src2 = source(res->d3d.Get(), W, H, 0xff000002);
  auto src3 = source(res->d3d.Get(), W, H, 0xff000003);
  auto frame_version = [&] { std::lock_guard<std::mutex> lk(res->frame.mu); return res->frame.version; };
  auto frame_pixel = [&] { std::lock_guard<std::mutex> lk(res->frame.mu); return (res->frame.payload && !res->frame.payload->empty()) ? res->frame.payload->at(0) : 0; };
  auto wait_version = [&](uint64_t v, int ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) { if (frame_version() >= v) return true; Sleep(5); }
    return frame_version() >= v;
  };

  // [A] the attached source's frame, under the live cookie, is published
  const uint64_t myCookie = capture->attachmentCookie.load();  // what AttachFrameArrived captures once
  check(myCookie == 1, "the live attachment cookie starts at 1", std::to_string(myCookie));
  capture->PublishCapturedTexture(*res, src1.Get(), qpc_us(), qpc_us(), 0, 0, true, myCookie);
  const bool gotA = wait_version(1, 3000);
  check(gotA && frame_pixel() == 1, "[A] a frame stamped with the live cookie reaches the frame state", "version=" + std::to_string(frame_version()) + " pixel=" + std::to_string(frame_pixel()));
  check(capture->staleAttachmentDrops.load() == 0, "[A] no stale drop so far");

  // [B] THE CROSSING: the worker's cookie check passed (myCookie == live), then a detach bumped the live
  //     cookie (DetachCaptureSession: attachmentCookie.fetch_add(1)), then the worker publishes the old frame.
  capture->attachmentCookie.fetch_add(1, std::memory_order_acq_rel);  // the detach
  const uint64_t versionBefore = frame_version();
  capture->PublishCapturedTexture(*res, src2.Get(), qpc_us(), qpc_us(), 0, 0, true, myCookie);  // stamped with the SOURCE cookie
  Sleep(400);
  check(frame_version() == versionBefore, "[B] the old attachment's frame published after the detach was DROPPED (old publish 0)",
        "version=" + std::to_string(frame_version()) + " pixel=" + std::to_string(frame_pixel()));
  check(capture->staleAttachmentDrops.load() == 1, "[B] the drop was counted as stale-attachment", "drops=" + std::to_string(capture->staleAttachmentDrops.load()));
  check(res->captureReadback.ViewDiag().ok() && res->captureReadback.ViewDiag().gpuPendingCount == 0, "[B] the ring slot was freed (nothing left pending)");

  // [B-neg] the pre-r13 stamping shape: the live cookie on the old frame. It PASSES the guard -- that is
  //         the defect Codex C1 named; this control shows the guard alone could never have caught it.
  const uint64_t liveNow = capture->attachmentCookie.load();
  capture->PublishCapturedTexture(*res, src2.Get(), qpc_us(), qpc_us(), 0, 0, true, liveNow);
  const bool gotNeg = wait_version(versionBefore + 1, 3000);
  check(gotNeg && frame_pixel() == 2,
        "[B-neg] negative control: the same old frame stamped with the LIVE cookie (pre-r13 behaviour) walks past the guard", "version=" + std::to_string(frame_version()));

  // [C] the next attachment's own frames keep flowing under the new cookie
  const uint64_t nextCookie = capture->attachmentCookie.load();
  const uint64_t v2 = frame_version();
  capture->PublishCapturedTexture(*res, src3.Get(), qpc_us(), qpc_us(), 0, 0, true, nextCookie);
  const bool gotC = wait_version(v2 + 1, 3000);
  check(gotC && frame_pixel() == 3, "[C] the new attachment's frame is published under its cookie", "version=" + std::to_string(frame_version()));
  check(capture->staleAttachmentDrops.load() == 1, "[C] no further stale drops");

  // [D] a frame with no attachment identity (0) is never checked (the GDI path's contract)
  const uint64_t v3 = frame_version();
  capture->PublishCapturedTexture(*res, src1.Get(), qpc_us(), qpc_us(), 0, 0, true, 0);
  check(wait_version(v3 + 1, 3000), "[D] cookie 0 (no identity) is not guarded");

  res->captureReadback.Shutdown();
  std::printf("capture_attachment_test: %d PASS, %d FAIL (NV12 lease path not exercised: WARP has no video processor)\n", gPass, gFail);
  return gFail == 0 ? 0 : 1;
}

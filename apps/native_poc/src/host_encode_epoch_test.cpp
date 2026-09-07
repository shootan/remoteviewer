// Host-side test for P11 (UAC-return false congestion) on the real Media Foundation H264Encoder:
//
//  [1] the flush epoch rides the encoder's accepted-input FIFO next to the timestamp and the
//      synthetic flag, so every AU reports the epoch of the input that produced it;
//  [2] the 2026-09-07 15:15:59 shape reproduced first -- input A accepted, a flush (epoch+1,
//      forced key), input B: this backend returns A's AU during call B, i.e. AFTER the flush,
//      real, with A's old stamp -- then the epoch gate (host_epoch_gate.hpp): the first AU sent
//      for the new epoch is its IDR and nothing from the old epoch goes out;
//  [2b] the gate on fabricated AUs: an old-epoch key does not open it, a current-epoch P before
//      the IDR is dropped and asks for a re-force, consecutive flushes, the wait bound -> reset;
//  [3] the hold on a still screen as OBSERVED on this backend: without further input A's AU
//      surfaces only with the next input; a kick 150 ms later surfaces it then. Measured, not
//      assumed: a synchronous encoder makes this trivially 0 and the test says so.
//
// Build: remote60_host_encode_epoch_test (CMake). Run: prints "...: PASS", exit 0.

#include <d3d11.h>
#include <mfapi.h>
#include <objbase.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "host_epoch_gate.hpp"
#include "mf_h264_codec.hpp"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "ole32.lib")

using namespace remote60::native_poc;

namespace {

int gFailures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
      ++gFailures;                                                     \
    }                                                                  \
  } while (0)

constexpr uint32_t kW = 640;
constexpr uint32_t kH = 360;
constexpr int64_t kHnsPerUs = 10;

std::vector<uint8_t> picture(uint32_t index) {
  std::vector<uint8_t> nv12(static_cast<size_t>(kW) * kH * 3 / 2, 128);
  uint8_t* y = nv12.data();
  for (uint32_t row = 0; row < kH; ++row) {
    for (uint32_t x = 0; x < kW; ++x) {
      y[row * kW + x] = static_cast<uint8_t>(64 + ((x / 8 + row / 8 + index) % 2) * 100);
    }
    y[row * kW + ((index * 24) % kW)] = 235;
  }
  return nv12;
}

struct Input {
  uint32_t index;
  uint64_t epoch;
  bool key;
  bool synthetic;
  uint64_t tUs;
};

// The product's input path: NV12 D3D11 textures handed to the MFT through a DXGI device manager
// (host_startup_graphics.cpp set_d3d11_device + encode_frame_surface). The hold the field showed
// belongs to this path -- on the CPU buffer path (encode_frame) the same hardware MFT answers
// each call with its own AU. A pool of textures, like the host's NV12 slots, so the MFT may keep
// one while the next is written.
struct SurfaceRig {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
  std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> slots;
  size_t next = 0;
  bool ok = false;

  bool Init() {
    D3D_FEATURE_LEVEL level{};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, &device, &level, &ctx))) {
      return false;
    }
    Microsoft::WRL::ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(device.As(&mt)) && mt) mt->SetMultithreadProtected(TRUE);
    for (int i = 0; i < 8; ++i) {
      D3D11_TEXTURE2D_DESC desc{};
      desc.Width = kW;
      desc.Height = kH;
      desc.MipLevels = 1;
      desc.ArraySize = 1;
      desc.Format = DXGI_FORMAT_NV12;
      desc.SampleDesc.Count = 1;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
      Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
      if (FAILED(device->CreateTexture2D(&desc, nullptr, &tex))) return false;
      slots.push_back(tex);
    }
    ok = true;
    return true;
  }
  ID3D11Texture2D* Fill(const std::vector<uint8_t>& nv12) {
    auto& tex = slots[next++ % slots.size()];
    ctx->UpdateSubresource(tex.Get(), 0, nullptr, nv12.data(), kW, 0);
    ctx->Flush();
    return tex.Get();
  }
};

SurfaceRig gSurface;

std::vector<H264AccessUnit> encode(H264Encoder& enc, const Input& in) {
  std::vector<H264AccessUnit> units;
  enc.set_next_input_synthetic(in.synthetic);
  enc.set_next_input_epoch(in.epoch);
  const std::vector<uint8_t> nv12 = picture(in.index);
  const int64_t tHns = static_cast<int64_t>(in.tUs) * kHnsPerUs;
  const bool okEncode = gSurface.ok ? enc.encode_frame_surface(gSurface.Fill(nv12), in.key, tHns, &units)
                                    : enc.encode_frame(nv12, in.key, tHns, &units);
  if (!okEncode) {
    std::printf("  encode failed (%s path)\n", gSurface.ok ? "surface" : "cpu");
    ++gFailures;
  }
  return units;
}

bool init(H264Encoder& enc) {
  if (gSurface.ok) (void)enc.set_d3d11_device(gSurface.device.Get());
  if (enc.initialize(kW, kH, 60, 2000000, 600)) return true;
  std::printf("  encoder init failed\n");
  ++gFailures;
  return false;
}

const char* path_name() { return gSurface.ok ? "surface" : "cpu"; }

// [1]
void test_epoch_rides_the_fifo() {
  std::printf("[1] the flush epoch rides the accepted-input FIFO with the stamp and the synthetic flag\n");
  H264Encoder enc;
  if (!init(enc)) return;
  std::vector<Input> inputs;
  for (uint32_t i = 0; i < 10; ++i) inputs.push_back(Input{i, i < 5 ? 1u : 2u, i == 5, i == 2, 1000000 + i * 16667});
  std::vector<H264AccessUnit> aus;
  for (const auto& in : inputs) for (auto& u : encode(enc, in)) aus.push_back(std::move(u));
  for (uint32_t extra = 0; extra < 4 && aus.size() < inputs.size(); ++extra) {
    for (auto& u : encode(enc, Input{100 + extra, 2, false, false, 1000000 + (10 + extra) * 16667})) aus.push_back(std::move(u));
  }
  CHECK(aus.size() >= 8);
  size_t matched = 0;
  bool allMatch = true;
  for (const auto& au : aus) {
    for (const auto& in : inputs) {
      if (static_cast<int64_t>(in.tUs) * kHnsPerUs == au.sampleTimeHns) {
        ++matched;
        if (au.inputEpoch != in.epoch || au.synthetic != in.synthetic) allMatch = false;
      }
    }
  }
  CHECK(matched >= 8);
  CHECK(allMatch);
  enc.shutdown();
}

// [2]
void test_pre_flush_au_reproduced_then_gated() {
  std::printf("[2] a pre-flush AU after the flush: reproduced on the real encoder, then gated\n");
  H264Encoder enc;
  if (!init(enc)) return;
  std::vector<std::pair<H264AccessUnit, uint64_t>> emitted;  // (au, epochNow at emission)
  uint64_t epochNow = 1;
  auto run = [&](const Input& in) { for (auto& u : encode(enc, in)) emitted.emplace_back(std::move(u), epochNow); };
  run(Input{0, 1, true, false, 1000000});
  run(Input{1, 1, false, false, 1016667});
  const uint64_t tA = 1033333;
  run(Input{2, 1, false, false, tA});  // A: the last real input before the UAC
  // ---- flush: epoch 2, forced key (what the flush callers do) ----
  epochNow = 2;
  const uint64_t tB = tA + 800000;  // B: first capture after the switch, 800 ms later
  run(Input{3, 2, true, false, tB});
  run(Input{4, 2, false, false, tB + 16667});
  run(Input{5, 2, false, false, tB + 33333});
  for (uint32_t extra = 0; extra < 4; ++extra) run(Input{6 + extra, 2, false, false, tB + 50000 + extra * 16667});

  // Reproduction: did an epoch-1 AU come out after the flush (during an epoch-2 call)?
  size_t preFlushAfterFlush = 0;
  uint64_t worstHoldUs = 0;
  for (const auto& [au, at] : emitted) {
    if (at == 2 && au.inputEpoch == 1) {
      ++preFlushAfterFlush;
      const uint64_t stampUs = static_cast<uint64_t>(au.sampleTimeHns / kHnsPerUs);
      if (tB > stampUs) worstHoldUs = (std::max)(worstHoldUs, tB - stampUs);
    }
  }
  if (preFlushAfterFlush > 0) {
    std::printf("  reproduced on %s (%s path): %zu pre-flush AU(s) surfaced after the flush, oldest %llu us behind the first post-flush capture\n",
                enc.backend_name(), path_name(), preFlushAfterFlush, static_cast<unsigned long long>(worstHoldUs));
    CHECK(worstHoldUs >= 700000);  // A's stamp is ~800 ms behind B: the field's 820 ms
  } else {
    std::printf("  note: %s (%s path) returned each AU in its own call; nothing surfaced after the flush here\n",
                enc.backend_name(), path_name());
  }

  // The gate, applied in emission order with a clock that follows the stamps.
  EpochGate gate;
  std::vector<H264AccessUnit> sent;
  std::vector<EpochVerdict> verdicts;
  for (auto& [au, at] : emitted) {
    const uint64_t nowUs = static_cast<uint64_t>(au.sampleTimeHns / kHnsPerUs) + 5000;
    const EpochVerdict v = epoch_gate_judge(gate, at, au.inputEpoch, au.keyFrame, au.bytes.size(), nowUs);
    verdicts.push_back(v);
    if (v == EpochVerdict::Emit || v == EpochVerdict::AcceptKey) sent.push_back(std::move(au));
  }
  bool seenEpoch2 = false, firstPostFlushIsKey = false, epoch1AfterFlush = false;
  for (const auto& au : sent) {
    if (au.inputEpoch == 2 && !seenEpoch2) { seenEpoch2 = true; firstPostFlushIsKey = au.keyFrame; }
    if (seenEpoch2 && au.inputEpoch == 1) epoch1AfterFlush = true;
  }
  CHECK(seenEpoch2);
  CHECK(firstPostFlushIsKey);
  CHECK(!epoch1AfterFlush);
  CHECK(gate.droppedOldEpoch == preFlushAfterFlush);
  CHECK(gate.keysAccepted == 2);  // the session's first IDR and the post-flush IDR
  CHECK(gate.resetsRequested == 0);
  CHECK(!gate.awaitingKey);
  CHECK(std::count(verdicts.begin(), verdicts.end(), EpochVerdict::ResetEncoder) == 0);
  enc.shutdown();
}

// [2b]
void test_gate_rules_on_fabricated_aus() {
  std::printf("[2b] gate rules: old-epoch key does not open it, P before the IDR is dropped + re-forced, bounds\n");
  auto au = [](uint64_t epoch, bool key) {
    H264AccessUnit a;
    a.bytes.assign(key ? 3000 : 300, 0x11);
    a.keyFrame = key;
    a.inputEpoch = epoch;
    return a;
  };
  EpochGate g;
  uint64_t t = 1000000;
  auto judge = [&](uint64_t epochNow, const H264AccessUnit& a, uint64_t dt = 16667) {
    t += dt;
    return epoch_gate_judge(g, epochNow, a.inputEpoch, a.keyFrame, a.bytes.size(), t);
  };
  // Epoch 1 open stream.
  CHECK(judge(1, au(1, true)) == EpochVerdict::AcceptKey);
  CHECK(judge(1, au(1, false)) == EpochVerdict::Emit);
  // Flush -> epoch 2. An OLD-EPOCH KEY must not open the gate; old P dropped; the epoch-2 P that
  // arrives before the IDR is dropped and asks for a re-force; the epoch-2 IDR opens it.
  CHECK(judge(2, au(1, true)) == EpochVerdict::DropOldEpoch);
  CHECK(g.awaitingKey);
  CHECK(judge(2, au(1, false)) == EpochVerdict::DropOldEpoch);
  CHECK(judge(2, au(2, false)) == EpochVerdict::DropAwaitingKey);
  CHECK(judge(2, au(2, true)) == EpochVerdict::AcceptKey);
  CHECK(!g.awaitingKey && g.keysAccepted == 2 && g.droppedOldEpoch == 2 && g.droppedAwaitingKey == 1);
  CHECK(judge(2, au(2, false)) == EpochVerdict::Emit);
  // A straggler from epoch 1 after the gate opened is still dropped (never re-sent).
  CHECK(judge(2, au(1, false)) == EpochVerdict::DropOldEpoch);
  // Consecutive flushes: epoch 3 then 4 before any AU; AUs of 2 and 3 are all old.
  CHECK(judge(4, au(2, false)) == EpochVerdict::DropOldEpoch);
  CHECK(judge(4, au(3, true)) == EpochVerdict::DropOldEpoch);
  CHECK(judge(4, au(4, true)) == EpochVerdict::AcceptKey);
  CHECK(g.epochsSeen == 3);
  // Untagged AUs (epoch 0) are never held back.
  CHECK(judge(4, au(0, false)) == EpochVerdict::Emit);
  // Bound by count: epoch 5, the IDR never comes -> after kAwaitMaxDropped drops, reset.
  EpochVerdict v = EpochVerdict::Emit;
  uint32_t drops = 0;
  for (uint32_t i = 0; i < EpochGate::kAwaitMaxDropped + 2; ++i) {
    v = judge(5, au(5, false), 1000);
    if (v == EpochVerdict::DropAwaitingKey) ++drops;
    if (v == EpochVerdict::ResetEncoder) break;
  }
  CHECK(v == EpochVerdict::ResetEncoder);
  CHECK(drops == EpochGate::kAwaitMaxDropped - 1);
  CHECK(g.resetsRequested == 1);
  epoch_gate_note_reset(g, t);
  CHECK(g.awaitingKey && g.awaitDropped == 0);
  CHECK(judge(5, au(5, true)) == EpochVerdict::AcceptKey);
  // Bound by time: epoch 6, one old AU right away, then one 800 ms later -> reset.
  CHECK(judge(6, au(5, false)) == EpochVerdict::DropOldEpoch);
  CHECK(judge(6, au(5, false), 800000) == EpochVerdict::ResetEncoder);
  CHECK(g.resetsRequested == 2);
}

// [3]
void test_hold_observed_with_and_without_kick() {
  std::printf("[3] the hold as observed on this backend: next input vs a 150 ms kick\n");
  auto hold_for = [](bool kick, uint64_t* outHoldUs, std::string* backend) -> bool {
    H264Encoder enc;
    if (!enc.initialize(kW, kH, 60, 2000000, 600)) return false;
    *backend = enc.backend_name();
    std::vector<H264AccessUnit> aus;
    auto run = [&](const Input& in) { for (auto& u : encode(enc, in)) aus.push_back(std::move(u)); };
    run(Input{0, 1, true, false, 1000000});
    run(Input{1, 1, false, false, 1016667});
    const uint64_t tA = 1033333;
    run(Input{2, 1, false, false, tA});
    auto surfaced = [&]() {
      for (const auto& au : aus) if (au.sampleTimeHns == static_cast<int64_t>(tA) * kHnsPerUs) return true;
      return false;
    };
    uint64_t surfacedAtUs = surfaced() ? tA : 0;
    if (!surfacedAtUs && kick) {
      run(Input{2, 1, false, true, tA + 150000});  // the trailing kick: same picture, synthetic, +150 ms
      if (surfaced()) surfacedAtUs = tA + 150000;
    }
    if (!surfacedAtUs) {
      run(Input{3, 1, false, false, tA + 800000});  // the next real input, 800 ms later
      if (surfaced()) surfacedAtUs = tA + 800000;
    }
    if (!surfacedAtUs) {
      run(Input{4, 1, false, false, tA + 816667});
      if (surfaced()) surfacedAtUs = tA + 816667;
    }
    enc.shutdown();
    if (!surfacedAtUs) return false;
    *outHoldUs = surfacedAtUs - tA;
    return true;
  };
  uint64_t holdNoKick = 0, holdKick = 0;
  std::string backend;
  CHECK(hold_for(false, &holdNoKick, &backend));
  CHECK(hold_for(true, &holdKick, &backend));
  std::printf("  %s (%s path): A surfaced %llu us after its capture without a kick, %llu us with a 150 ms kick\n",
              backend.c_str(), path_name(), static_cast<unsigned long long>(holdNoKick), static_cast<unsigned long long>(holdKick));
  CHECK(holdKick <= 150000);
  if (holdNoKick >= 800000) CHECK(holdKick < holdNoKick);  // asynchronous here: the kick is what surfaces it
}

}  // namespace

int main() {
  (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(MFStartup(MF_VERSION))) {
    std::printf("host_encode_epoch_test: MFStartup failed\n");
    return 2;
  }
  if (!gSurface.Init()) {
    std::printf("note: no D3D11 hardware device / NV12 texture; falling back to the CPU input path\n");
  }
  test_epoch_rides_the_fifo();
  test_pre_flush_au_reproduced_then_gated();
  test_gate_rules_on_fabricated_aus();
  test_hold_observed_with_and_without_kick();
  MFShutdown();
  CoUninitialize();
  if (gFailures == 0) {
    std::printf("host_encode_epoch_test: PASS\n");
    return 0;
  }
  std::printf("host_encode_epoch_test: FAIL (%d)\n", gFailures);
  return 1;
}

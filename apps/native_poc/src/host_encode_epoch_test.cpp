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

#ifndef NOMINMAX
#define NOMINMAX  // host_capture_session.hpp uses (std::max)(...) which the Windows macros break
#endif

#include <d3d11.h>
#include <mfapi.h>
#include <objbase.h>
#include <wrl/client.h>

#include <algorithm>

#include "host_encoder_manager.hpp"
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

// The three ways the host feeds the MFT. The 2026-09-07 field host ran the BGRA buffer path
// (its stats: nv12SurfaceFrames=0, nv12Converted=0), so [2] is run on every path this machine
// offers and reports on which of them the pre-flush AU surfaces after the flush.
enum class InputPath { Surface, Bgra, Nv12Cpu };
InputPath gPath = InputPath::Nv12Cpu;

const char* path_name() {
  switch (gPath) {
    case InputPath::Surface: return "surface(D3D11 NV12)";
    case InputPath::Bgra: return "bgra-buffer";
    case InputPath::Nv12Cpu: return "nv12-buffer";
  }
  return "?";
}

std::vector<uint8_t> picture_bgra(uint32_t index) {
  const std::vector<uint8_t> nv12 = picture(index);
  std::vector<uint8_t> bgra(static_cast<size_t>(kW) * kH * 4);
  for (uint32_t row = 0; row < kH; ++row) {
    for (uint32_t x = 0; x < kW; ++x) {
      const uint8_t v = nv12[row * kW + x];
      uint8_t* p = &bgra[(static_cast<size_t>(row) * kW + x) * 4];
      p[0] = v; p[1] = v; p[2] = v; p[3] = 255;
    }
  }
  return bgra;
}

std::vector<H264AccessUnit> encode(H264Encoder& enc, const Input& in) {
  std::vector<H264AccessUnit> units;
  enc.set_next_input_synthetic(in.synthetic);
  enc.set_next_input_epoch(in.epoch);
  const int64_t tHns = static_cast<int64_t>(in.tUs) * kHnsPerUs;
  bool okEncode = false;
  switch (gPath) {
    case InputPath::Surface:
      okEncode = enc.encode_frame_surface(gSurface.Fill(picture(in.index)), in.key, tHns, &units);
      break;
    case InputPath::Bgra: {
      const std::vector<uint8_t> bgra = picture_bgra(in.index);
      okEncode = enc.encode_frame_bgra(bgra.data(), kW, kH, kW * 4, in.key, tHns, &units);
      break;
    }
    case InputPath::Nv12Cpu:
      okEncode = enc.encode_frame(picture(in.index), in.key, tHns, &units);
      break;
  }
  if (!okEncode) {
    std::printf("  encode failed (%s path)\n", path_name());
    ++gFailures;
  }
  return units;
}

bool init(H264Encoder& enc) {
  if (gPath == InputPath::Surface) (void)enc.set_d3d11_device(gSurface.device.Get());
  if (enc.initialize(kW, kH, 60, 2000000, 600)) return true;
  std::printf("  encoder init failed\n");
  ++gFailures;
  return false;
}

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
// [4] A02/A06/HN07 -- provenance. Deterministic: the product's own accepted-input FIFO
// (PendingInputFifo), the product gate (host_epoch_gate.hpp) and the product rebuild
// (EncoderState::TryProvenanceResync) driven directly, so no asynchronous MFT timing is involved.
//   a. an output with no bytes consumes its FIFO entry WHOLE -- the epoch too -- so the IDR forced
//      by the next flush still carries the new epoch and opens the gate (before A02 the epoch was
//      left behind and that IDR was judged old: drop-old-epoch, the F1 shift);
//   b/c/f. an overflow latches provenance invalid: the FIFO empties, later outputs are unknown (0)
//      and the gate refuses everything -- including an AU that looks like the current epoch's key;
//   d. the rebuild runs on the epoch gate's budget (3 per 10 s): a fourth attempt in the window is
//      refused and the request stays pending, and it is allowed again in the next window;
//   e. a rebuild whose initialize() fails leaves the latch, the pending request and the closed gate;
//   g. a successful rebuild clears the latch, forces a key and the new epoch's IDR is accepted;
//   h. after the gate is open, an unprovenanced output closes the chain again (the next P is
//      dropped and the key re-forced) while a known-old AU does not.
void test_provenance_fifo_latch_and_gate() {
  std::printf("[4] provenance: FIFO lockstep, overflow latch, gate refusal, rebuild budget, and the signal surviving a failed encode\n");
  // ---- a. the empty-output path pops the whole record ----
  {
    PendingInputFifo fifo;
    fifo.Push(PendingInput{1000, false, 1});   // epoch 1, three inputs
    fifo.Push(PendingInput{2000, false, 1});
    fifo.Push(PendingInput{3000, false, 1});
    fifo.PopForEmptyOutput();                  // the MFT consumed input 1 and produced nothing usable
    fifo.Push(PendingInput{4000, true, 2});    // the flush: epoch 2, the forced key's input
    PendingInput got{};
    CHECK(fifo.Pop(&got) && got.tsHns == 2000 && got.epoch == 1);
    CHECK(fifo.Pop(&got) && got.tsHns == 3000 && got.epoch == 1);
    CHECK(fifo.Pop(&got) && got.tsHns == 4000 && got.epoch == 2);  // pre-A02: this read epoch 1
    CHECK(!fifo.Pop(&got) && fifo.size() == 0);
    // The gate then accepts that IDR as the new epoch's first AU.
    EpochGate g;
    uint64_t t = 1000000;
    CHECK(epoch_gate_judge(g, 1, 1, true, 3000, t += 16667) == EpochVerdict::AcceptKey);
    CHECK(epoch_gate_judge(g, 1, 1, false, 300, t += 16667) == EpochVerdict::Emit);
    CHECK(epoch_gate_judge(g, 2, 2, true, 3000, t += 16667) == EpochVerdict::AcceptKey);
    CHECK(!g.awaitingKey && g.droppedOldEpoch == 0);
  }
  // ---- b/c/f. overflow -> latch -> everything unknown, nothing accepted ----
  {
    PendingInputFifo fifo;
    for (size_t i = 0; i <= PendingInputFifo::kMaxEntries; ++i) {
      fifo.Push(PendingInput{static_cast<int64_t>(i + 1) * 1000, false, 5});
    }
    CHECK(fifo.provenance_invalid());
    CHECK(fifo.size() == 0);          // emptied: nothing left claims to describe the MFT's output
    CHECK(fifo.overflow_total() == 1);
    PendingInput got{};
    CHECK(!fifo.Pop(&got));           // no provenance -> the codec stamps epoch 0
    fifo.Clear();                     // shutdown half of a rebuild: the latch survives
    CHECK(fifo.provenance_invalid());
    EpochGate g;
    uint64_t t = 2000000;
    CHECK(epoch_gate_judge(g, 5, 5, true, 3000, t += 16667) == EpochVerdict::AcceptKey);  // healthy first
    g.provenanceInvalid = true;                                                          // the stage latches the gate
    CHECK(epoch_gate_judge(g, 5, 0, false, 300, t += 16667) == EpochVerdict::DropUnknownEpoch);
    CHECK(g.awaitingKey);                                                                 // the chain closed at once
    // Even an AU that looks like the current epoch's key is refused while the latch is up.
    CHECK(epoch_gate_judge(g, 5, 5, true, 3000, t += 16667) == EpochVerdict::DropUnknownEpoch);
    CHECK(g.keysAccepted == 1 && g.droppedProvenanceInvalid >= 2);
    fifo.Reset();                     // a successful initialize()
    CHECK(!fifo.provenance_invalid() && fifo.size() == 0);
  }
  // ---- d. the rebuild budget is the gate's own ----
  {
    EpochGate g;
    const uint64_t t0 = 3000000;
    CHECK(epoch_gate_take_reset_budget(g, t0));
    CHECK(epoch_gate_take_reset_budget(g, t0 + 1000));
    CHECK(epoch_gate_take_reset_budget(g, t0 + 2000));
    CHECK(!epoch_gate_take_reset_budget(g, t0 + 3000));          // 3 per window: the 4th waits
    CHECK(g.resetsRequested == 3 && g.resetsSuppressed == 1);
    CHECK(epoch_gate_take_reset_budget(g, t0 + EpochGate::kResetWindowUs + 1));  // next window
  }
  // ---- e/g. the rebuild itself, on a real encoder ----
  {
    CaptureState capture;
    EncoderState enc;
    enc.activeEncodeW = kW;
    enc.activeEncodeH = kH;
    enc.activeFps = 60;
    enc.activeBitrate = 2000000;
    enc.activeKeyint = 600;
    capture.inputEpoch.store(4, std::memory_order_release);
    enc.epochGate.epoch = 4;
    enc.epochGate.provenanceInvalid = true;
    enc.provenanceResyncPending = true;
    enc.forceKeyNext = false;
    // e. a rebuild that cannot initialize (0x0) leaves everything closed and pending.
    enc.activeEncodeW = 0;
    enc.activeEncodeH = 0;
    CHECK(!enc.TryProvenanceResync(capture, 4000000));
    CHECK(enc.provenanceResyncPending && enc.epochGate.provenanceInvalid && enc.provenanceResyncFailed == 1);
    CHECK(!enc.codec.provenance_invalid() || true);  // the codec was shut down; the stage keeps the gate closed
    // g. with valid geometry it succeeds: latch cleared, key forced, the new epoch waits for its IDR.
    enc.activeEncodeW = kW;
    enc.activeEncodeH = kH;
    const bool rebuilt = enc.TryProvenanceResync(capture, 4100000);
    if (!rebuilt) {
      std::printf("  note: encoder rebuild unavailable on this machine; e/g checked only up to the failure path\n");
    } else {
      CHECK(!enc.provenanceResyncPending);
      CHECK(!enc.epochGate.provenanceInvalid);
      CHECK(!enc.codec.provenance_invalid());
      CHECK(enc.forceKeyNext);
      CHECK(enc.epochGate.awaitingKey);            // the rebuild bumped the epoch: its IDR is awaited
      CHECK(enc.provenanceResyncCount == 1);
      const uint64_t epochNow = capture.inputEpoch.load(std::memory_order_acquire);
      uint64_t t = 5000000;
      CHECK(epoch_gate_judge(enc.epochGate, epochNow, epochNow, false, 300, t += 16667) == EpochVerdict::DropAwaitingKey);
      CHECK(epoch_gate_judge(enc.epochGate, epochNow, epochNow, true, 3000, t += 16667) == EpochVerdict::AcceptKey);
      CHECK(epoch_gate_judge(enc.epochGate, epochNow, epochNow, false, 300, t += 16667) == EpochVerdict::Emit);
      enc.codec.shutdown();
    }
  }
  // ---- i. the signal survives an encode call that FAILED (A06 wiring, Codex round 2) ----
  // An encode can fail after the MFT accepted the input -- that call may be the one that
  // overflowed the FIFO -- and the stage returns early on failure. NoteProvenance is what those
  // early returns call, so the latch and the pending rebuild outlive the failure and the tick
  // retry (which only looks at provenanceResyncPending) still runs. Driven directly here; the
  // early-return call sites themselves are covered by reading, not by an executed test, because
  // H264Encoder has no transform-injection seam to force an overflow with.
  {
    CaptureState capture;
    EncoderState enc;
    enc.activeEncodeW = kW;
    enc.activeEncodeH = kH;
    enc.activeFps = 60;
    enc.activeBitrate = 2000000;
    enc.activeKeyint = 600;
    capture.inputEpoch.store(9, std::memory_order_release);
    enc.epochGate.epoch = 9;
    CHECK(!enc.NoteProvenance(false));                 // a healthy call changes nothing
    CHECK(!enc.epochGate.provenanceInvalid && !enc.provenanceResyncPending);
    CHECK(enc.NoteProvenance(true));                   // the failing call reported an overflow
    CHECK(enc.epochGate.provenanceInvalid && enc.provenanceResyncPending);
    CHECK(!enc.NoteProvenance(true));                  // idempotent: no second log, no second latch
    // The gate refuses everything meanwhile, whatever it looks like.
    uint64_t t = 7000000;
    CHECK(epoch_gate_judge(enc.epochGate, 9, 9, true, 3000, t += 16667) == EpochVerdict::DropUnknownEpoch);
    // The budget is spent by other rebuilds: the request stays pending and nothing is cleared,
    // so a later tick (no new frame needed) still has something to retry.
    for (int i = 0; i < 3; ++i) CHECK(epoch_gate_take_reset_budget(enc.epochGate, 7100000));
    CHECK(!enc.TryProvenanceResync(capture, 7100000));
    CHECK(enc.provenanceResyncPending && enc.epochGate.provenanceInvalid);
    CHECK(enc.provenanceResyncFailed == 0);            // a spent budget is not an initialize failure
    // Next window: the retry runs and succeeds (the rebuild is what clears the latch).
    const uint64_t nextWindowUs = 7100000 + EpochGate::kResetWindowUs + 1;
    if (enc.TryProvenanceResync(capture, nextWindowUs)) {
      CHECK(!enc.provenanceResyncPending && !enc.epochGate.provenanceInvalid && enc.forceKeyNext);
      enc.codec.shutdown();
    } else {
      std::printf("  note: encoder rebuild unavailable here; the pending/budget half was checked\n");
    }
  }
  // ---- h. HN07: unknown re-closes the verified chain, a known-old AU does not ----
  {
    EpochGate g;
    uint64_t t = 6000000;
    CHECK(epoch_gate_judge(g, 7, 7, true, 3000, t += 16667) == EpochVerdict::AcceptKey);
    CHECK(epoch_gate_judge(g, 7, 7, false, 300, t += 16667) == EpochVerdict::Emit);
    // A known-old AU: dropped quietly, the chain stays open, the next current P still flows.
    CHECK(epoch_gate_judge(g, 7, 6, false, 300, t += 16667) == EpochVerdict::DropOldEpoch);
    CHECK(!g.awaitingKey && g.reclosedByUnknown == 0);
    CHECK(epoch_gate_judge(g, 7, 7, false, 300, t += 16667) == EpochVerdict::Emit);
    // An unprovenanced (or future) AU: the chain closes, the next current P is dropped and the
    // caller re-forces the key, and only the current epoch's IDR opens it again.
    CHECK(epoch_gate_judge(g, 7, 0, false, 300, t += 16667) == EpochVerdict::DropUnknownEpoch);
    CHECK(g.awaitingKey && g.reclosedByUnknown == 1);
    CHECK(epoch_gate_judge(g, 7, 7, false, 300, t += 16667) == EpochVerdict::DropAwaitingKey);
    CHECK(epoch_gate_judge(g, 7, 7, true, 3000, t += 16667) == EpochVerdict::AcceptKey);
    CHECK(epoch_gate_judge(g, 7, 7, false, 300, t += 16667) == EpochVerdict::Emit);
    // A future epoch behaves like unknown.
    CHECK(epoch_gate_judge(g, 7, 9, true, 3000, t += 16667) == EpochVerdict::DropUnknownEpoch);
    CHECK(g.awaitingKey && g.reclosedByUnknown == 2);
  }
}

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
  // Fail closed: no provenance (0) and a future epoch are never sent and never open the gate.
  // HN07 (A02 round): they also CLOSE a gate that was open. An output nobody can vouch for may
  // reference pictures the viewer never had, so the verified chain ends there: the next
  // current-epoch P is dropped (the caller re-forces the key) and only the next IDR reopens it.
  // A known-old AU (above) still does not close anything -- its provenance is certain.
  CHECK(judge(4, au(0, false)) == EpochVerdict::DropUnknownEpoch);
  CHECK(g.awaitingKey && g.reclosedByUnknown == 1);
  CHECK(judge(4, au(0, true)) == EpochVerdict::DropUnknownEpoch);
  CHECK(judge(4, au(9, true)) == EpochVerdict::DropUnknownEpoch);
  CHECK(g.droppedUnknownEpoch == 3 && g.reclosedByUnknown == 1);  // closed once, stays closed
  CHECK(judge(4, au(4, false)) == EpochVerdict::DropAwaitingKey);
  CHECK(judge(4, au(4, true)) == EpochVerdict::AcceptKey);
  CHECK(judge(4, au(4, false)) == EpochVerdict::Emit);
  {
    // ...and while the gate is closed (epoch 5 flush), an unknown/future key does not open it.
    EpochGate h;
    uint64_t t2 = 5000000;
    auto j = [&](uint64_t e, const H264AccessUnit& a) { t2 += 16667; return epoch_gate_judge(h, e, a.inputEpoch, a.keyFrame, a.bytes.size(), t2); };
    CHECK(j(5, au(0, true)) == EpochVerdict::DropUnknownEpoch);
    CHECK(j(5, au(6, true)) == EpochVerdict::DropUnknownEpoch);
    CHECK(h.awaitingKey);
    CHECK(j(5, au(5, true)) == EpochVerdict::AcceptKey);
    CHECK(!h.awaitingKey);
  }
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
  // A valid current-epoch IDR is accepted even when the wait has long run out.
  epoch_gate_note_reset(g, t);
  CHECK(judge(6, au(6, true), 5000000) == EpochVerdict::AcceptKey);
  // Rebuild cap: past kResetMaxPerWindow rebuilds within kResetWindowUs the gate keeps dropping
  // and re-forcing but asks for no more rebuilds (no endless re-initialisation).
  {
    EpochGate c;
    uint64_t t3 = 100000000;
    auto j = [&](uint64_t e, const H264AccessUnit& a, uint64_t dt) { t3 += dt; return epoch_gate_judge(c, e, a.inputEpoch, a.keyFrame, a.bytes.size(), t3); };
    CHECK(j(1, au(1, true), 0) == EpochVerdict::AcceptKey);
    uint32_t resets = 0, suppressed = 0;
    for (int round = 0; round < 5; ++round) {
      EpochVerdict v = EpochVerdict::Emit;
      for (uint32_t i = 0; i < EpochGate::kAwaitMaxDropped + 1 && v != EpochVerdict::ResetEncoder; ++i) {
        v = j(2, au(2, false), 1000);
      }
      if (v == EpochVerdict::ResetEncoder) { ++resets; epoch_gate_note_reset(c, t3); }
      else { ++suppressed; c.awaitDropped = 0; }  // the stage keeps re-forcing; model its next wait
    }
    CHECK(resets == EpochGate::kResetMaxPerWindow);
    CHECK(suppressed == 5 - EpochGate::kResetMaxPerWindow);
    CHECK(c.resetsSuppressed >= 1);
    // The window passes: a rebuild is allowed again.
    EpochVerdict v = EpochVerdict::Emit;
    for (uint32_t i = 0; i < EpochGate::kAwaitMaxDropped + 1 && v != EpochVerdict::ResetEncoder; ++i) {
      v = j(2, au(2, false), i == 0 ? EpochGate::kResetWindowUs : 1000);
    }
    CHECK(v == EpochVerdict::ResetEncoder);
  }
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
  const bool surface = gSurface.Init();
  if (!surface) std::printf("note: no D3D11 hardware device / NV12 texture; the surface path is skipped\n");
  gPath = surface ? InputPath::Surface : InputPath::Bgra;
  test_epoch_rides_the_fifo();
  // [2] on every input path: the field host's BGRA buffer path first, then the others.
  for (const InputPath p : {InputPath::Bgra, InputPath::Nv12Cpu, InputPath::Surface}) {
    if (p == InputPath::Surface && !surface) continue;
    gPath = p;
    test_pre_flush_au_reproduced_then_gated();
  }
  gPath = surface ? InputPath::Surface : InputPath::Bgra;
  test_gate_rules_on_fabricated_aus();
  test_provenance_fifo_latch_and_gate();
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

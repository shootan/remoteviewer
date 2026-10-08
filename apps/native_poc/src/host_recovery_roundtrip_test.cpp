// The REAL host keyframe-recovery roundtrip at a LONG GOP (stutter-keyframe r2, step 2).
//
// r1's recovery rig had the FakeHost force a key directly in SendFrame, so it never exercised the
// host's own request->force state machine. The contract (Codex 2-4) requires, BEFORE the key period
// is extended (step 4b), one timeline that runs the PRODUCT path:
//
//   viewer ControlRequestKeyFrame -> host mailbox -> reason mapping -> encoder.RequestKey
//     -> (next encode) the force-key decision -> the MFT's forced NAL5 IDR -> decode resumes.
//
// Here the viewer request flows through the REAL HostMainLoopMailbox, the REAL
// map_keyframe_reasons_to_host and the REAL decide_force_key -- the exact functions
// host_stage_time_limit.cpp and host_stage_encode_send_h264.cpp call (host_force_key_decision.hpp) --
// into the REAL H264Encoder, and the emitted IDR is decoded by the REAL H264Decoder. The force-key
// bookkeeping locals (forceKeyNext / realInputsSinceKey / the 300ms submit latch) mirror
// EncoderState line-for-line; each is annotated with the product line it stands in for.
//
// Positive: at a 300-frame (10s @ 30fps) GOP, no periodic IDR appears on its own, and a viewer
// request produces an IDR within a few frames that DECODES. Negative control: with NO request at the
// same long GOP, no IDR appears after the first frame -> the forced key is load-bearing (remove it and
// the "an IDR appears" recovery assertion fails), which is exactly why 4b (a long period) is safe
// only with this roundtrip intact.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>

#include "mf_h264_codec.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "host_force_key_decision.hpp"
#include "host_main_loop_mailbox.hpp"

using namespace remote60::native_poc;

namespace {
int gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what);
  if (!ok) {
    ++gFailures;
    if (!detail.empty()) std::printf("        %s\n", detail.c_str());
  }
}

constexpr uint32_t kW = 320;
constexpr uint32_t kH = 240;
constexpr uint32_t kFps = 30;
constexpr int64_t kHnsPerUs = 10;

// A gentle horizontal-gradient scroll: smooth motion so the encoder has no scene change to key on,
// and every frame differs so it is not a static-screen no-op.
std::vector<uint8_t> picture(uint32_t index) {
  std::vector<uint8_t> nv12(static_cast<size_t>(kW) * kH * 3 / 2, 128);
  for (uint32_t y = 0; y < kH; ++y) {
    for (uint32_t x = 0; x < kW; ++x) {
      nv12[static_cast<size_t>(y) * kW + x] = static_cast<uint8_t>((x + index) & 0xFF);
    }
  }
  return nv12;  // UV plane left at 128 (grey chroma)
}

// Drives the real roundtrip for `frames` frames, posting a viewer request at `requestAtFrame`
// (0xFFFFFFFF = never). Returns the per-frame IDR timeline and whether decode ever produced output
// at/after the request's IDR.
struct RoundtripResult {
  uint32_t firstIdrFrame = 0xFFFFFFFF;   // the initial (first-frame) IDR
  uint32_t requestFrame = 0xFFFFFFFF;    // the frame the viewer request was consumed
  uint32_t recoveryIdrFrame = 0xFFFFFFFF;// the IDR produced for that request
  uint32_t idrCountAfterFirst = 0;       // IDRs emitted AFTER the first-frame IDR
  uint32_t decodedAfterRecovery = 0;     // frames the decoder produced from the recovery IDR onward
  bool ok = true;
};

RoundtripResult run_roundtrip(uint32_t frames, uint32_t requestAtFrame, uint32_t keyint = 300,
                              uint32_t* outSecondIdrGap = nullptr) {
  RoundtripResult r;
  H264Encoder enc;
  H264Decoder dec;
  if (!enc.initialize(kW, kH, kFps, 1500000, keyint) || !dec.initialize(kW, kH, kFps)) {
    std::printf("  codec init failed\n");
    r.ok = false;
    ++gFailures;
    return r;
  }
  MainLoopMailbox mailbox;

  // EncoderState bookkeeping, mirrored:
  bool forceKeyNext = true;              // EncoderState.forceKeyNext starts true (first frame keys)
  uint32_t encodedSeq = 0;               // EncoderState.encodedSeq
  uint32_t realInputsSinceKey = 0;       // EncoderState.realInputsSinceKey
  const uint32_t activeKeyint = keyint;  // EncoderState.activeKeyint (the periodic schedule)
  uint32_t secondIdrFrame = 0xFFFFFFFF;  // the first periodic IDR after the first-frame IDR
  uint64_t forceKeySubmittedAtUs = 0;    // EncoderState.forceKeySubmittedAtUs (the 300ms latch)
  bool recoveryArmed = false;

  for (uint32_t i = 0; i < frames; ++i) {
    const uint64_t nowUs = 1'000'000ULL + static_cast<uint64_t>(i) * (1'000'000ULL / kFps);

    // The viewer asks (host_control_session.cpp posts this on a ControlRequestKeyFrame).
    if (i == requestAtFrame) {
      mailbox.PostRequestKeyframe(kKeyframeReasonViewer, 42);
      recoveryArmed = true;
    }

    // host_stage_time_limit.cpp:156-162 -- drain the mailbox and map to encoder reasons (REAL funcs).
    uint16_t viewerReason = 0;
    const uint32_t keyReasons = mailbox.TakeKeyframeReasons(&viewerReason);
    if (keyReasons != kKeyframeReasonNone) {
      const uint32_t hostReasons = map_keyframe_reasons_to_host(keyReasons);
      forceKeyNext = true;  // EncoderState::RequestKey(hostReasons): sets forceKeyNext
      r.requestFrame = i;
      std::printf("[roundtrip] viewer-request frame=%u tUs=%llu reasons=0x%X -> RequestKey hostReasons=0x%X\n",
                  i, static_cast<unsigned long long>(nowUs), keyReasons, hostReasons);
    }

    // host_stage_encode_send_h264.cpp:287-295 -- the REAL force-key decision (shared pure function).
    const bool scheduledKey = (activeKeyint > 0) && (realInputsSinceKey >= activeKeyint);
    const bool forceKeyInFlight = (forceKeySubmittedAtUs != 0 && nowUs < forceKeySubmittedAtUs + 300'000);
    const ForceKeyDecision d =
        decide_force_key({forceKeyNext, encodedSeq == 0, scheduledKey, forceKeyInFlight});

    std::vector<H264AccessUnit> units;
    const int64_t tHns = static_cast<int64_t>(nowUs) * kHnsPerUs;
    if (!enc.encode_frame(picture(i), d.forceKeyFrame, tHns, &units)) {
      std::printf("  encode failed at frame %u\n", i);
      r.ok = false;
      ++gFailures;
      break;
    }
    ++realInputsSinceKey;  // host_stage_encode_send_h264.cpp:327 (per non-bootstrap input)
    if (d.forceKeyFrame) {
      forceKeySubmittedAtUs = nowUs;  // :440 arm the 300ms latch (r1: only when the MFT accepted it)
    }

    for (auto& au : units) {
      const bool isIdr = au.keyFrame;  // the encoder's own classification (CleanPoint || NAL5 IDR)
      if (isIdr) {
        if (r.firstIdrFrame == 0xFFFFFFFF) {
          r.firstIdrFrame = i;
        } else {
          if (secondIdrFrame == 0xFFFFFFFF) secondIdrFrame = i;  // first periodic/forced IDR after init
          ++r.idrCountAfterFirst;
        }
        if (recoveryArmed && r.recoveryIdrFrame == 0xFFFFFFFF) {
          r.recoveryIdrFrame = i;
          std::printf("[roundtrip] forced IDR frame=%u bytes=%zu (NAL5) -> decode\n", i, au.bytes.size());
        }
        // host_stage_encode_send_h264_au.cpp:558/577 -- a key AU accepted clears the request + period.
        forceKeyNext = false;
        realInputsSinceKey = 0;
      }
      ++encodedSeq;

      std::vector<DecodedFrameNv12> decoded;
      (void)dec.decode_access_unit(au.bytes, au.keyFrame, au.sampleTimeHns, &decoded);
      if (recoveryArmed && r.recoveryIdrFrame != 0xFFFFFFFF) {
        r.decodedAfterRecovery += static_cast<uint32_t>(decoded.size());
      }
    }
  }
  // Flush the decoder's held output so the recovery IDR's picture is accounted for.
  {
    std::vector<DecodedFrameNv12> tail;
    (void)dec.decode_access_unit(std::vector<uint8_t>{}, false, 0, &tail);
    if (recoveryArmed && r.recoveryIdrFrame != 0xFFFFFFFF) {
      r.decodedAfterRecovery += static_cast<uint32_t>(tail.size());
    }
  }
  if (outSecondIdrGap != nullptr) {
    *outSecondIdrGap = (secondIdrFrame != 0xFFFFFFFF && r.firstIdrFrame != 0xFFFFFFFF)
                           ? (secondIdrFrame - r.firstIdrFrame)
                           : 0;
  }
  enc.shutdown();
  dec.shutdown();
  return r;
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("host_recovery_roundtrip_test (real mailbox + decide_force_key + MFT, 10s GOP)\n");

  MFStartup(MF_VERSION);

  // --- positive: at a 10s GOP the periodic IDR is gone, and a viewer request recovers via a NAL5 IDR
  //     that decodes (the full real roundtrip). -------------------------------------------------------
  {
    std::printf("[A] viewer request at a 10s GOP -> forced NAL5 IDR -> decode\n");
    const RoundtripResult r = run_roundtrip(/*frames=*/120, /*requestAtFrame=*/60);
    check("codec roundtrip ran", r.ok);
    check("the first frame produced an IDR", r.firstIdrFrame != 0xFFFFFFFF,
          "firstIdrFrame=" + std::to_string(r.firstIdrFrame));
    // The recovery IDR is the only IDR after the first frame (no periodic one masked it): the request
    // fires at frame 60, far inside the 300-frame GOP, so idrCountAfterFirst == 1 is exactly it.
    check("the recovery IDR is the ONLY IDR after the first frame (no periodic mask)",
          r.idrCountAfterFirst == 1, "idrCountAfterFirst=" + std::to_string(r.idrCountAfterFirst));
    check("a viewer request produced a recovery IDR", r.recoveryIdrFrame != 0xFFFFFFFF,
          "recoveryIdrFrame=" + std::to_string(r.recoveryIdrFrame));
    check("the recovery IDR arrived within a few frames of the request",
          r.recoveryIdrFrame != 0xFFFFFFFF && r.requestFrame != 0xFFFFFFFF &&
              r.recoveryIdrFrame >= r.requestFrame && r.recoveryIdrFrame <= r.requestFrame + 5,
          "request=" + std::to_string(r.requestFrame) + " idr=" + std::to_string(r.recoveryIdrFrame));
    check("the recovery IDR DECODED (stream resumes)", r.decodedAfterRecovery > 0,
          "decodedAfterRecovery=" + std::to_string(r.decodedAfterRecovery));
  }

  // --- negative control: NO request at the same 10s GOP -> NO IDR after the first frame. The forced
  //     key is load-bearing; removing it makes the "an IDR appears" assertion fail. -------------------
  {
    std::printf("[B] NEGATIVE control: no request at a 10s GOP -> no IDR after the first frame\n");
    const RoundtripResult r = run_roundtrip(/*frames=*/150, /*requestAtFrame=*/0xFFFFFFFF);
    check("[neg] the first frame still produced an IDR", r.firstIdrFrame != 0xFFFFFFFF);
    check("[neg] NO further IDR without a request (periodic key gone)", r.idrCountAfterFirst == 0,
          "idrCountAfterFirst=" + std::to_string(r.idrCountAfterFirst));
    check("[neg] no recovery IDR was produced (nothing asked for one)",
          r.recoveryIdrFrame == 0xFFFFFFFF);
  }

  // --- cadence (step 4b evidence): the REAL NAL interval at the baseline vs the extended GOP, this
  //     PC's MFT. Baseline 120 (4s@30fps) keys periodically; candidate 300 (10s) does not within 8s. -
  {
    std::printf("[C] NAL cadence: baseline keyint=120 keys periodically; candidate keyint=300 does not in 8s\n");
    uint32_t baseGap = 0;
    const RoundtripResult rb = run_roundtrip(/*frames=*/220, /*requestAtFrame=*/0xFFFFFFFF, /*keyint=*/120, &baseGap);
    check("baseline 120-GOP: a periodic IDR appears", rb.idrCountAfterFirst >= 1,
          "idrCountAfterFirst=" + std::to_string(rb.idrCountAfterFirst));
    check("baseline 120-GOP: the periodic interval is ~120 frames (4s@30fps)",
          baseGap >= 90 && baseGap <= 160, "secondIdrGap=" + std::to_string(baseGap));
    const RoundtripResult rc = run_roundtrip(/*frames=*/240, /*requestAtFrame=*/0xFFFFFFFF, /*keyint=*/300);
    check("candidate 300-GOP: NO periodic IDR within 240 frames (8s) -- pushed out to ~10s",
          rc.idrCountAfterFirst == 0, "idrCountAfterFirst=" + std::to_string(rc.idrCountAfterFirst));
  }

  MFShutdown();
  if (gFailures == 0) {
    std::printf("host_recovery_roundtrip_test: PASS\n");
    return 0;
  }
  std::printf("host_recovery_roundtrip_test: FAIL (%d)\n", gFailures);
  return 1;
}

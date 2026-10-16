#pragma once

// One main-loop tick of an open catch-up await (motion-settle r11, Codex r11 s1/s4): the single function
// the product calls (host_stage_encode_send_h264.cpp, encode_send_h264_await_tick) and the deterministic
// test drives (host_catchup_collect_test.cpp, a fake async MFT behind the real H264Encoder).
//
// Order, each tick while kick.await is open:
//   1. identity: codec instance / session epoch / input epoch / stream generation changed -> Cancelled;
//      stream paused (stream control inactive) -> Cancelled. Nothing is collected or sent.
//   2. collect what the MFT has, WITHOUT an input (H264Encoder::collect_outputs).
//   3. collect error -> CollectError: the collected AUs are NOT sent (an output failure breaks the
//      reference chain; the caller runs the same output-failure recovery an encode call does) and the
//      await is deferred (reason collect-error, late watch on).
//   4. every collected AU goes through emit(au) -- the ordinary emit path. emit returns a bit set:
//      kEmitHanded when the AU was handed to the sender (not dropped by the epoch gate, the selection
//      fence or the queue policy), kEmitStop when the stage must stop (send failure / encoder reset /
//      reconnect; the caller ends the tick the way encode_send does). Only an AU HANDED TO THE SENDER can satisfy the await (Observed,
//      or the late watch of a deferred one): output-observed is never called delivered.
//   5. nothing satisfied and the deadline passed -> Deferred (reason deadline). Otherwise NoOutput (no AU
//      at all) or Pending (AUs went out, none of them the awaited one).
// Pure on its inputs but for the encoder; the log goes to `out`.

#include <cstdint>
#include <ostream>
#include <vector>

#include "host_kick.hpp"
#include "mf_h264_codec.hpp"

namespace remote60::native_poc {

constexpr int kEmitHanded = 1;
constexpr int kEmitStop = 2;

struct CatchupAwaitIdentity {
  uint64_t codecInstance = 0, sessionEpoch = 0, inputEpoch = 0, streamGeneration = 0;
  bool streamActive = true;
};

enum class CatchupAwaitStep : uint8_t { Idle, Cancelled, CollectError, NoOutput, Pending, Observed, Deferred };

inline const char* catchup_await_step_name(CatchupAwaitStep s) {
  switch (s) {
    case CatchupAwaitStep::Idle: return "idle";
    case CatchupAwaitStep::Cancelled: return "cancelled";
    case CatchupAwaitStep::CollectError: return "collect-error";
    case CatchupAwaitStep::NoOutput: return "no-output";
    case CatchupAwaitStep::Pending: return "pending";
    case CatchupAwaitStep::Observed: return "observed";
    case CatchupAwaitStep::Deferred: return "deferred";
  }
  return "?";
}

struct CatchupAwaitTickResult {
  CatchupAwaitStep step = CatchupAwaitStep::Idle;
  H264CollectStats cs{};
  uint32_t collected = 0;    // AUs the collect returned
  uint32_t handed = 0;       // ... of which the emit stage handed to the sender
  bool stopped = false;      // emit asked the tick to stop
  bool collectFailed = false;
  uint32_t droppedOnError = 0;  // AUs collected by a failed collect and not sent
};

// The late watch of a deferred await, for AUs handed to the sender by ANY path (collect or encode call).
inline void catchup_late_watch_check(KickState& kick, uint64_t inst, uint64_t auOrdinal, uint64_t auEpoch,
                                     uint8_t auReason, const char* via, uint64_t nowUs, std::ostream& out) {
  if (!kick.LateWatchSatisfiedBy(inst, auEpoch, auOrdinal)) return;
  ++kick.awaitLateOutputs;
  out << "[native-video-host][catchup] late-output ordinal=" << kick.lateWatch.ordinal
      << " auOrdinal=" << auOrdinal << " src=" << synthetic_reason_name(auReason)
      << " via=" << via << " sinceAcceptedUs=" << (nowUs - kick.lateWatch.acceptedUs)
      << " sinceDeferredUs=" << (nowUs - kick.lateWatch.endedUs) << "\n";
  kick.lateWatch = KickState::AwaitLateWatch{};
}

// Provenance of an AU the emit stage HANDED TO THE SENDER (taken before the emit moved its bytes).
struct CatchupHandedAu {
  uint64_t ordinal = 0, inputEpoch = 0;
  uint8_t reason = 0;
};

// What AUs handed to the sender mean for the late watch of a deferred await and for an open await --
// the one judgement both paths use: the collect of catchup_await_tick (via "collect") and an encode
// call's own output (via "other", host_stage_encode_send_h264.cpp settle_await). True when the await was
// observed here.
inline bool catchup_await_observe_handed(KickState& kick, uint64_t inst, const std::vector<CatchupHandedAu>& handed,
                                         const char* via, uint64_t nowUs, std::ostream& out) {
  for (const auto& a : handed) catchup_late_watch_check(kick, inst, a.ordinal, a.inputEpoch, a.reason, via, nowUs, out);
  if (!kick.await.active) return false;
  for (const auto& a : handed) {
    if (!kick.AwaitSatisfiedBy(inst, a.inputEpoch, a.ordinal)) continue;
    const bool byCollect = via[0] == 'c';
    ++(byCollect ? kick.awaitObservedByCollect : kick.awaitObservedByOther);
    out << "[native-video-host][catchup] output ordinal=" << kick.await.ordinal << " auOrdinal=" << a.ordinal
        << " via=" << via << " src=" << synthetic_reason_name(a.reason)
        << " sinceAcceptedUs=" << (nowUs - kick.await.acceptedUs) << " collects=" << kick.await.collectAttempts
        << " firstCollectSinceAcceptedUs="
        << (kick.await.firstCollectUs ? kick.await.firstCollectUs - kick.await.acceptedUs : 0)
        << " otherInput=" << (kick.await.advancedByOther ? 1 : 0) << "\n";
    kick.EndAwait(KickState::AwaitEnd::Observed, nowUs);
    return true;
  }
  return false;
}

template <class EmitFn>
CatchupAwaitTickResult catchup_await_tick(KickState& kick, H264Encoder& codec, const CatchupAwaitIdentity& id,
                                          uint64_t nowUs, EmitFn&& emit, std::ostream& out) {
  CatchupAwaitTickResult r;
  if (!kick.await.active) return r;
  const char* cancelWhy = nullptr;
  if (!kick.AwaitIdentityMatches(id.codecInstance, id.sessionEpoch, id.inputEpoch, id.streamGeneration)) {
    cancelWhy = "identity";
  } else if (!id.streamActive) {
    cancelWhy = "paused";
  }
  if (cancelWhy) {
    out << "[native-video-host][catchup] await cancelled ordinal=" << kick.await.ordinal << " why=" << cancelWhy
        << " collects=" << kick.await.collectAttempts << "\n";
    kick.EndAwait(KickState::AwaitEnd::Cancelled, nowUs);
    r.step = CatchupAwaitStep::Cancelled;
    return r;
  }
  if (kick.await.collectAttempts == 0) kick.await.firstCollectUs = nowUs;
  ++kick.await.collectAttempts;
  ++kick.awaitCollectCalls;
  std::vector<H264AccessUnit> units;
  const bool ok = codec.collect_outputs(&units, &r.cs);
  r.collected = static_cast<uint32_t>(units.size());
  kick.awaitCollectAus += units.size();
  if (!ok) {
    ++kick.awaitCollectErrors;
    r.collectFailed = true;
    r.droppedOnError = static_cast<uint32_t>(units.size());
    out << "[native-video-host][catchup] await deferred ordinal=" << kick.await.ordinal << " why=collect-error"
        << " collected=" << units.size() << " drainErrors=" << r.cs.drainErrors
        << " sinceAcceptedUs=" << (nowUs - kick.await.acceptedUs) << " collects=" << kick.await.collectAttempts
        << "\n";
    kick.EndAwait(KickState::AwaitEnd::Deferred, nowUs);
    r.step = CatchupAwaitStep::CollectError;
    return r;
  }
  const uint64_t inst = codec.instance_id();
  std::vector<CatchupHandedAu> handed;
  for (auto& au : units) {
    const CatchupHandedAu prov{au.acceptedInputOrdinal, au.inputEpoch, au.syntheticReason};
    const int e = emit(au);
    if (e & kEmitHanded) {
      ++r.handed;
      handed.push_back(prov);
    }
    if (e & kEmitStop) {
      r.stopped = true;
      break;
    }
  }
  if (catchup_await_observe_handed(kick, inst, handed, "collect", nowUs, out)) {
    r.step = CatchupAwaitStep::Observed;
    return r;
  }
  if (kick.AwaitExpired(nowUs)) {
    out << "[native-video-host][catchup] await deferred ordinal=" << kick.await.ordinal << " why=deadline"
        << " sinceAcceptedUs=" << (nowUs - kick.await.acceptedUs) << " collects=" << kick.await.collectAttempts
        << " otherInput=" << (kick.await.advancedByOther ? 1 : 0) << "\n";
    kick.EndAwait(KickState::AwaitEnd::Deferred, nowUs);
    r.step = CatchupAwaitStep::Deferred;
    return r;
  }
  r.step = units.empty() ? CatchupAwaitStep::NoOutput : CatchupAwaitStep::Pending;
  return r;
}

}  // namespace remote60::native_poc

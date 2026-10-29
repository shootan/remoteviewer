#pragma once
// apk-decoder-liveness r2 (Codex codex_apk_decoder_liveness_design_r2.md (2)): TEST-BUILD instrumentation of the
// client session's progress -- what the receive thread (which also carries the UDP control channel) and the control
// thread are doing right now and since when, the owed-key retry and the keyframe limiter, the last keyframe request
// sent (its application seq: the Host logs the same `keyframe-request seq=`) and the control transport's counters.
//
// Recording is compiled only with GNLINK_LIVENESS_TRACE (the APK's `-Pgnlink.livenessTrace=true`, debug variant only;
// native marker "liveness-trace", which the release gate refuses). Without it every REMOTE60_LT(...) is empty: the
// shipped session records nothing. The fields always exist (one class layout in every build).
//
// Cost on the receive thread: a few relaxed atomic stores -- no lock, no I/O, no allocation. The line is formatted by
// whoever asks (the app's own low-rate logger thread), from the atomics alone. Metadata only: no payload, no key.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>

#if defined(__ANDROID__) || defined(__linux__)
#include <unistd.h>
#endif

#ifdef GNLINK_LIVENESS_TRACE
#define REMOTE60_LT(stmt) \
  do {                    \
    stmt;                 \
  } while (0)
#else
#define REMOTE60_LT(stmt) \
  do {                    \
  } while (0)
#endif

namespace remote60::native_poc {

#ifdef GNLINK_LIVENESS_TRACE
inline const char* kLivenessTraceMarker = "liveness-trace";
#endif

inline uint64_t liveness_tid() {
#if defined(__ANDROID__) || defined(__linux__)
  return static_cast<uint64_t>(gettid());  // the tid logcat shows
#else
  return static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xffffffffu);
#endif
}

// What a thread is doing. Rx*: VideoReceiveMain. Wk*: the control loop (WorkerMain).
enum class LtPhase : uint32_t {
  None = 0,
  RxWait = 1,        // in the readiness wait / recv
  RxControl = 2,     // udpControl_.OnPacket
  RxTick = 3,        // udpControl_.Tick (on a quiet / dropping socket)
  RxAssemble = 4,    // pipeline.OnDatagram (assembly)
  RxDeliver = 5,     // sink->OnEncodedH264Frame
  RxKeyConsume = 6,  // sink->ConsumeDecoderKeyframeRequest + the limiter
  WkPass = 10,       // the loop's own work
  WkSinkHook = 11,   // a sink hook (KeyframeOwedFor, selection result, ...)
  WkWrite = 12,      // an action written to the control link and its answer read (execute_control_action)
  WkRead = 13,       // reading the control link (it ticks the channel while it waits)
};

inline const char* lt_phase_name(uint32_t p) {
  switch (static_cast<LtPhase>(p)) {
    case LtPhase::RxWait: return "wait";
    case LtPhase::RxControl: return "control";
    case LtPhase::RxTick: return "tick";
    case LtPhase::RxAssemble: return "assemble";
    case LtPhase::RxDeliver: return "deliver";
    case LtPhase::RxKeyConsume: return "keyconsume";
    case LtPhase::WkPass: return "pass";
    case LtPhase::WkSinkHook: return "sinkhook";
    case LtPhase::WkWrite: return "exchange";
    case LtPhase::WkRead: return "read";
    default: return "none";
  }
}

struct ClientLivenessTrace {
  // receive thread
  std::atomic<uint32_t> rxPhase{0};
  std::atomic<uint64_t> rxPhaseSinceUs{0};
  std::atomic<uint64_t> rxTid{0};
  std::atomic<uint64_t> rxLastDatagramUs{0};
  std::atomic<uint64_t> rxLastControlUs{0};
  std::atomic<uint64_t> rxLastTickUs{0};
  std::atomic<uint64_t> rxDatagrams{0};
  std::atomic<uint64_t> rxControlPackets{0};
  std::atomic<uint64_t> rxDeliverMaxUs{0};
  // control thread
  std::atomic<uint32_t> wkPhase{0};
  std::atomic<uint64_t> wkPhaseSinceUs{0};
  std::atomic<uint64_t> wkTid{0};
  std::atomic<uint64_t> wkLastPassUs{0};
  // the owed key (r4 M1-A) and the limiter
  std::atomic<uint64_t> owedToken{0};
  std::atomic<uint32_t> owedAttempts{0};
  std::atomic<uint64_t> owedNextUs{0};
  std::atomic<uint64_t> keyWantQueued{0};   // a want the limiter took
  std::atomic<uint64_t> keyWantRefused{0};  // a want the limiter refused (held: decoderRekeyPending_)
  // the last keyframe request written to the control link
  std::atomic<uint32_t> keySentSeq{0};
  std::atomic<uint32_t> keySentReason{0};
  std::atomic<uint32_t> keySentOk{0};
  std::atomic<uint64_t> keySentUs{0};
  std::atomic<uint64_t> keySentTransportMsg{0};  // the channel's messagesSent right after it (UDP control)

  void Rx(LtPhase p, uint64_t nowUs) {
    rxPhase.store(static_cast<uint32_t>(p), std::memory_order_relaxed);
    rxPhaseSinceUs.store(nowUs, std::memory_order_relaxed);
  }
  void Wk(LtPhase p, uint64_t nowUs) {
    wkPhase.store(static_cast<uint32_t>(p), std::memory_order_relaxed);
    wkPhaseSinceUs.store(nowUs, std::memory_order_relaxed);
  }
  void MaxDeliver(uint64_t us) {
    uint64_t cur = rxDeliverMaxUs.load(std::memory_order_relaxed);
    while (us > cur && !rxDeliverMaxUs.compare_exchange_weak(cur, us, std::memory_order_relaxed)) {
    }
  }

  // "rx=deliver/12ms tid=.. dg=3ms ctl=40ms tick=900ms n=../.. dmax=..ms | wk=read/20ms tid=.. pass=21ms |
  //  owed=tok/att next+ms kq=.. kr=.. | kf=seq/reason ok=1 age=..ms tmsg=.."
  std::string Format(uint64_t nowUs) const {
    auto age = [nowUs](uint64_t t) -> long long {
      return t == 0 ? -1 : static_cast<long long>((nowUs >= t ? nowUs - t : 0) / 1000);
    };
    char line[512];
    const uint64_t next = owedNextUs.load(std::memory_order_relaxed);
    std::snprintf(
        line, sizeof(line),
        "rx=%s/%lldms tid=%llu dg=%lldms ctl=%lldms tick=%lldms n=%llu/%llu dmax=%llums | wk=%s/%lldms tid=%llu "
        "pass=%lldms | owed=%llu/%u next%+lldms kq=%llu kr=%llu | kf=%u/%u ok=%u age=%lldms tmsg=%llu",
        lt_phase_name(rxPhase.load(std::memory_order_relaxed)), age(rxPhaseSinceUs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(rxTid.load(std::memory_order_relaxed)),
        age(rxLastDatagramUs.load(std::memory_order_relaxed)), age(rxLastControlUs.load(std::memory_order_relaxed)),
        age(rxLastTickUs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(rxDatagrams.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(rxControlPackets.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(rxDeliverMaxUs.load(std::memory_order_relaxed) / 1000),
        lt_phase_name(wkPhase.load(std::memory_order_relaxed)), age(wkPhaseSinceUs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(wkTid.load(std::memory_order_relaxed)),
        age(wkLastPassUs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(owedToken.load(std::memory_order_relaxed)),
        owedAttempts.load(std::memory_order_relaxed),
        next == 0 ? 0LL : static_cast<long long>(next) / 1000 - static_cast<long long>(nowUs / 1000),
        static_cast<unsigned long long>(keyWantQueued.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(keyWantRefused.load(std::memory_order_relaxed)),
        keySentSeq.load(std::memory_order_relaxed), keySentReason.load(std::memory_order_relaxed),
        keySentOk.load(std::memory_order_relaxed), age(keySentUs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(keySentTransportMsg.load(std::memory_order_relaxed)));
    return line;
  }
};

}  // namespace remote60::native_poc

#pragma once

// The shared client session's once-a-second report to the host (mobile-abr r1, Codex contract
// .claude/codex_mobile_abr_r1.md section 2/3).
//
// Role:    closes a measurement window about once a second on a monotonic clock, turns the window's
//          counts into ControlClientMetrics fields over the time that really elapsed, and keeps the
//          latest measured snapshot PENDING until the control scheduler actually sends it.
// Thread:  the session's control loop only. The receive thread's counters arrive as plain numbers.
// Input:   the sink-delivered AU count and payload bytes (cumulative), the sink's decode drain
//          (decoder outputs since the last drain, valid or not), its presentation drain.
// Output:  Pending(): the snapshot to hand to ClientControlScheduler::NextAction on EVERY pass.
// Callers: ClientSessionController::RunSession; client_telemetry_window_test.cpp.
// Pure, no Windows headers.
//
// Why each rule:
//   - The report used to be built in a local on the pass its stats were drained and dropped when
//     NextAction picked a Ping / list / select / input on that pass -- the measurement was gone and
//     the next one was a second away. The pending snapshot outlives the pass; the scheduler's own
//     dedup (updatedQpcUs) sends it once. A newer window replaces it (latest-wins) with ITS measured
//     time -- an unsent snapshot is never restamped as new.
//   - Rates are per second of the real elapsed window. A late close reports the one window it closes
//     (no catch-up of past windows, no burst), and the next window starts at that close.
//   - Decode fields only once this connection's decoder has produced an output (decodeSeen): before
//     that a 0 would say "decoded nothing" about a decoder that has not started. After it, a window
//     whose decode drain is not valid (no surface, a selection waiting for its answer) produces NO
//     report rather than one with the decode fields at 0 -- the host keeps decodeReported sticky and
//     would read those zeros as a collapse. A valid window with 0 outputs is reported as 0: a real
//     stall must reach the host.
//   - Present-only reports (before decodeSeen) are what older sessions sent; latency / decode tail are
//     not measured and stay 0 ("not measured" to every ABR threshold, each a "greater than").
//   - A cumulative counter that went backwards (a reset) gives a 0 delta for that window and rebases.
//   - r3 (Codex candidate review R2): an unsent snapshot does not outlive the state it measured. A
//     not-valid window drops it; so does a change of the sink's decode generation (surface, selection)
//     seen on any pass; and one older than kMaxPendingAgeUs is not handed out. The host stamps a report
//     with its ARRIVAL time, so a late-sent old window would read as fresh. An ordinary Ping / list / input
//     preemption keeps it: the next pass sends it within the age bound.

#include <algorithm>
#include <cstdint>

#include "native_video_client_shared_core.hpp"

namespace remote60::native_poc {

// One window's worth of display timing, measured where frames actually reach the screen.
struct ClientPresentationStats {
  uint32_t targetIntervalUs = 0;
  uint32_t fpsX100 = 0;
  uint32_t gapP50Us = 0;
  uint32_t gapP95Us = 0;
  uint32_t gapMaxUs = 0;
  uint32_t over1_5xCount = 0;
  uint32_t over2xCount = 0;
  uint32_t sampleCount = 0;
  uint32_t scheduledCount = 0;
  uint32_t immediateCount = 0;
  uint32_t reanchorCount = 0;
  uint32_t displayedCount = 0;
};

// What the sink's decoder produced since the last drain (mobile-abr r1). The session turns the count
// into a rate over the second it really spans, next to its own receive counters, and reports them in
// the decode-side fields of ControlClientMetrics -- the fields the PC viewer fills.
#define REMOTE60_CLIENT_DECODE_STATS 1  // lets a test sink build against sessions before and after this
struct ClientDecodeStats {
  uint32_t decodedFrames = 0;  // valid decoded pictures the decoder gave out (dequeued) since the last drain
  uint32_t width = 0;          // the decoder's current output size (0 = not known)
  uint32_t height = 0;
  uint64_t generation = 0;     // the sink's decode generation the window was measured in
};

class ClientTelemetryWindow {
 public:
  static constexpr uint64_t kWindowUs = 1000000ULL;
  // A measured snapshot is sent within this of its measurement or not at all (r3 R2).
  static constexpr uint64_t kMaxPendingAgeUs = 1500000ULL;

  // A new connection: nothing pending, nothing seen, the first window starts now.
  void Reset(uint64_t nowUs, uint64_t sinkFrames, uint64_t sinkBytes) {
    startUs_ = nowUs;
    baseFrames_ = sinkFrames;
    baseBytes_ = sinkBytes;
    decodeSeen_ = false;
    pending_ = ClientControlMetricsSnapshot{};
    pendingGeneration_ = 0;
    windows_ = reports_ = withheld_ = dropped_ = 0;
  }

  bool Due(uint64_t nowUs) const { return nowUs >= startUs_ + kWindowUs; }

  // Closes the window ending now. decode / present: null when the sink had none to give (or the
  // decode drain said it is not valid now). Returns true when a new snapshot became pending.
  bool Close(uint64_t nowUs, uint64_t sinkFrames, uint64_t sinkBytes, const ClientDecodeStats* decode,
             const ClientPresentationStats* present) {
    const uint64_t windowUs = nowUs > startUs_ ? nowUs - startUs_ : 0;
    const uint64_t frames = sinkFrames >= baseFrames_ ? sinkFrames - baseFrames_ : 0;
    const uint64_t bytes = sinkBytes >= baseBytes_ ? sinkBytes - baseBytes_ : 0;
    startUs_ = nowUs;
    baseFrames_ = sinkFrames;
    baseBytes_ = sinkBytes;
    ++windows_;
    if (windowUs == 0) return false;

    if (decode && decode->decodedFrames > 0) decodeSeen_ = true;
    ClientControlMetricsSnapshot s{};
    bool report = false;
    if (decodeSeen_) {
      if (!decode) {
        ++withheld_;  // decoder state unknown this window: say nothing rather than "0 fps"
        DropPending();  // ...and nothing measured before it either (r3 R2)
        return false;
      }
      s.message.width = decode->width;
      s.message.height = decode->height;
      s.message.decodedFpsX100 = PerSecondX100(decode->decodedFrames, windowUs);
      s.message.recvFpsX100 = PerSecondX100(frames, windowUs);
      // bytes * 8 bits / us = Mbps; x1000.
      s.message.recvMbpsX1000 = static_cast<uint32_t>(std::min<uint64_t>(bytes * 8000ULL / windowUs, 0xFFFFFFFFULL));
      report = true;
    }
    if (present && present->sampleCount > 0) {
      s.message.presentTargetIntervalUs = present->targetIntervalUs;
      s.message.presentFpsX100 = present->fpsX100;
      s.message.presentGapP50Us = present->gapP50Us;
      s.message.presentGapP95Us = present->gapP95Us;
      s.message.presentGapMaxUs = present->gapMaxUs;
      s.message.presentOver1_5xCount = present->over1_5xCount;
      s.message.presentOver2xCount = present->over2xCount;
      s.message.presentSampleCount = present->sampleCount;
      s.message.presentScheduledCount = present->scheduledCount;
      s.message.presentImmediateCount = present->immediateCount;
      s.message.presentReanchorCount = present->reanchorCount;
      s.message.presentDisplayedCount = present->displayedCount;
      report = true;
    }
    if (!report) return false;
    s.updatedQpcUs = nowUs;  // the time this window was measured, and only this window
    pending_ = s;
    pendingGeneration_ = decode ? decode->generation : 0;
    ++reports_;
    return true;
  }

  // Every pass, before NextAction: the sink's current decode generation. A snapshot measured in another
  // generation is dropped.
  void OnDecodeGeneration(uint64_t generation) {
    if (pending_.updatedQpcUs != 0 && generation != pendingGeneration_) DropPending();
  }
  // Handed to NextAction on every pass; the scheduler sends a given updatedQpcUs once. Past
  // kMaxPendingAgeUs from its measurement it is no longer handed out.
  const ClientControlMetricsSnapshot& Pending(uint64_t nowUs) const {
    if (pending_.updatedQpcUs != 0 && nowUs > pending_.updatedQpcUs + kMaxPendingAgeUs) return empty_;
    return pending_;
  }
  // The pending snapshot as it is (tests).
  const ClientControlMetricsSnapshot& Pending() const { return pending_; }

  bool decode_seen() const { return decodeSeen_; }
  uint64_t windows() const { return windows_; }
  uint64_t reports() const { return reports_; }
  uint64_t withheld() const { return withheld_; }
  uint64_t dropped() const { return dropped_; }

  static uint32_t PerSecondX100(uint64_t count, uint64_t elapsedUs) {
    if (elapsedUs == 0) return 0;
    return static_cast<uint32_t>(std::min<uint64_t>(count * 100ULL * kWindowUs / elapsedUs, 0xFFFFFFFFULL));
  }

 private:
  uint64_t startUs_ = 0;
  uint64_t baseFrames_ = 0;
  uint64_t baseBytes_ = 0;
  bool decodeSeen_ = false;
  void DropPending() {
    if (pending_.updatedQpcUs != 0) ++dropped_;
    pending_ = ClientControlMetricsSnapshot{};
    pendingGeneration_ = 0;
  }
  ClientControlMetricsSnapshot pending_{};
  uint64_t pendingGeneration_ = 0;
  const ClientControlMetricsSnapshot empty_{};
  uint64_t windows_ = 0, reports_ = 0, withheld_ = 0, dropped_ = 0;
};

}  // namespace remote60::native_poc

#pragma once

// The shared client session's once-a-second report to the host (mobile-abr r1, Codex contract
// .claude/codex_mobile_abr_r1.md section 2/3).
//
// Role:    closes a measurement window about once a second on a monotonic clock, turns the window's
//          counts into ControlClientMetrics fields over the time that really elapsed, and keeps the
//          latest measured snapshot PENDING until the control scheduler actually sends it.
// Thread:  the session's control loop only. The receive thread's counters arrive as plain numbers.
// Input:   the sink-delivered AU count and payload bytes (cumulative), the sink's decode drain
//          (its valid measurement span, or not valid now), its presentation drain.
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
//   - Decode fields only once the sink has given a valid count in this connection (decodeSeen; r5: a
//     valid count, even a 0, not only a first positive one -- the sink holds it back only through a
//     bounded startup grace, so a decoder that never produces anything still reaches the host as 0).
//     Before that a 0 would say "decoded nothing" about a decoder that has not started. After it, a
//     window whose decode drain is not valid (no surface, inside a startup grace) produces NO report
//     rather than one with the decode fields at 0 -- the host keeps decodeReported sticky and would read
//     those zeros as a collapse. A valid window with 0 outputs is reported as 0: a real stall must reach
//     the host.
//   - Present-only reports (before decodeSeen) are what older sessions sent. Latency / decode tail are
//     not measured by this session and stay 0: no severe / moderate / emergency threshold (each a
//     "greater than") fires on them, but the recovery conditions are "less than" and a 0 passes them --
//     recovery then rests on the decode rate alone.
//   - A cumulative counter that went backwards (a reset) gives a 0 delta for that window and rebases.
//   - r5 (Codex F1 contract): a paused stream (the viewer asked the host to stop) is intended inactivity:
//     not measured, no 0-fps report, the unsent snapshot dropped.
//   - r6 (Codex S1, codex_mobile_abr_e599123.md): the decode rate is the sink's measurement span -- the
//     count and the time since the measurement began, together -- never a count over the whole window.
//     The window's own time could include a startup grace (deliberately not measured) or the time before
//     a generation change, so one picture just out of a grace read as ~1 fps. The host keeps a report
//     fresh for 3 s and counts severe on every 1 s tick, so one such report could be counted twice and
//     demote. A measurement shorter than kMinSampleUs is not reported yet: Hold keeps the window open and
//     the control loop (10 ms passes, no frame needed) closes it once the measurement is long enough --
//     at most kMinSampleUs later, never a second, and never by summing another generation's count.
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
#define REMOTE60_CLIENT_DECODE_STATS 1
// Feature macros, so a test builds against the sessions before and after each change:
#define REMOTE60_TELEMETRY_PAUSED 1  // r5: Close(..., paused)
#define REMOTE60_DECODE_SPAN 1       // r6: ClientDecodeStats is the measurement span; Hold()
struct ClientDecodeStats {
  uint32_t width = 0;          // the decoder's current output size (0 = not known)
  uint32_t height = 0;
  uint64_t generation = 0;     // the sink's decode generation the window was measured in
  // r6 (Codex S1): the valid measurement now in progress (client_decode_snapshot.hpp ClientDecodeSpan),
  // cumulative -- reading it consumes nothing. The window takes its own difference from the last report.
  uint64_t spanId = 0;         // changes when a new measurement begins (generation, startup grace)
  uint64_t spanFrames = 0;     // valid decoded pictures (dequeued) since it began
  uint64_t spanUs = 0;         // how long it has been measured
};

class ClientTelemetryWindow {
 public:
  static constexpr uint64_t kWindowUs = 1000000ULL;
  // A measured snapshot is sent within this of its measurement or not at all (r3 R2).
  static constexpr uint64_t kMaxPendingAgeUs = 1500000ULL;
  // r6: the shortest measurement a decode rate is given over (Codex S1: about 250 ms).
  static constexpr uint64_t kMinSampleUs = 250000ULL;

  // A new connection: nothing pending, nothing seen, the first window starts now.
  void Reset(uint64_t nowUs, uint64_t sinkFrames, uint64_t sinkBytes) {
    startUs_ = nowUs;
    baseFrames_ = sinkFrames;
    baseBytes_ = sinkBytes;
    decodeSeen_ = false;
    pending_ = ClientControlMetricsSnapshot{};
    pendingGeneration_ = 0;
    spanSet_ = false;
    spanId_ = spanFrames_ = spanUs_ = 0;
    holdStartUs_ = holdUntilUs_ = 0;
    windows_ = reports_ = withheld_ = dropped_ = held_ = holdLimited_ = 0;
  }

  // A window held for its sample (Hold) is due again when the sample will be long enough.
  bool Due(uint64_t nowUs) const {
    if (holdUntilUs_ != 0) return nowUs >= holdUntilUs_;
    return nowUs >= startUs_ + kWindowUs;
  }

  // r6 (Codex S1): on a due pass, before the presentation drain and Close. true = the decode measurement
  // the window would report is shorter than kMinSampleUs (it began just now: the first picture after a
  // startup grace, a generation change) -- nothing is closed, drained or dropped, and the pass is asked again
  // when the measurement will be long enough. Bounded: one window is held at most kMinSampleUs past its
  // first hold, then closed with what it has -- a measurement that keeps restarting does not hold it longer.
  bool Hold(uint64_t nowUs, const ClientDecodeStats* decode, bool paused) {
    if (paused || !decode) {
      EndHold();
      return false;
    }
    uint64_t frames = 0, us = 0;
    SpanSample(*decode, &frames, &us);
    if (us >= kMinSampleUs) {
      EndHold();
      return false;
    }
    if (holdStartUs_ == 0) holdStartUs_ = nowUs;
    const uint64_t limitUs = holdStartUs_ + kMinSampleUs;
    if (nowUs >= limitUs) {
      ++holdLimited_;
      EndHold();
      return false;
    }
    holdUntilUs_ = std::min(nowUs + (kMinSampleUs - us), limitUs);
    ++held_;
    return true;
  }

  // Closes the window ending now. decode / present: null when the sink had none to give (or the
  // decode drain said it is not valid now). Returns true when a new snapshot became pending.
  bool Close(uint64_t nowUs, uint64_t sinkFrames, uint64_t sinkBytes, const ClientDecodeStats* decode,
             const ClientPresentationStats* present, bool paused = false) {
    EndHold();
    const uint64_t windowUs = nowUs > startUs_ ? nowUs - startUs_ : 0;
    const uint64_t frames = sinkFrames >= baseFrames_ ? sinkFrames - baseFrames_ : 0;
    const uint64_t bytes = sinkBytes >= baseBytes_ ? sinkBytes - baseBytes_ : 0;
    startUs_ = nowUs;
    baseFrames_ = sinkFrames;
    baseBytes_ = sinkBytes;
    ++windows_;
    if (windowUs == 0) return false;

    // r6: this window's part of the measurement -- since the last close if it is the same one, since its
    // start if a new one began. The base moves on every close that has one (paused included: paused time
    // is never counted later).
    uint64_t decodedFrames = 0, decodedUs = 0;
    if (decode) {
      SpanSample(*decode, &decodedFrames, &decodedUs);
      spanSet_ = true;
      spanId_ = decode->spanId;
      spanFrames_ = decode->spanFrames;
      spanUs_ = decode->spanUs;
    }
    if (paused) {  // intended inactivity: nothing measured, nothing kept
      ++withheld_;
      DropPending();
      return false;
    }
    if (decode) decodeSeen_ = true;  // r5: a valid count, even a 0
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
      // r6: over the time the count covers, never the window's (which may include a grace or the time
      // before a generation change); a sample cut short by Hold's bound is at least 1 ms.
      s.message.decodedFpsX100 = PerSecondX100(decodedFrames, std::max<uint64_t>(decodedUs, 1000));
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
  uint64_t held() const { return held_; }                  // r6: due passes held for the sample
  uint64_t hold_limited() const { return holdLimited_; }   // r6: windows closed at Hold's bound

  static uint32_t PerSecondX100(uint64_t count, uint64_t elapsedUs) {
    if (elapsedUs == 0) return 0;
    return static_cast<uint32_t>(std::min<uint64_t>(count * 100ULL * kWindowUs / elapsedUs, 0xFFFFFFFFULL));
  }

 private:
  uint64_t startUs_ = 0;
  uint64_t baseFrames_ = 0;
  uint64_t baseBytes_ = 0;
  bool decodeSeen_ = false;
  // r6: the decode measurement as of the last close that had one.
  bool spanSet_ = false;
  uint64_t spanId_ = 0, spanFrames_ = 0, spanUs_ = 0;
  uint64_t holdStartUs_ = 0, holdUntilUs_ = 0;
  void SpanSample(const ClientDecodeStats& d, uint64_t* frames, uint64_t* us) const {
    if (spanSet_ && d.spanId == spanId_) {
      *frames = d.spanFrames >= spanFrames_ ? d.spanFrames - spanFrames_ : 0;
      *us = d.spanUs >= spanUs_ ? d.spanUs - spanUs_ : 0;
    } else {
      *frames = d.spanFrames;
      *us = d.spanUs;
    }
  }
  void EndHold() { holdStartUs_ = holdUntilUs_ = 0; }
  void DropPending() {
    if (pending_.updatedQpcUs != 0) ++dropped_;
    pending_ = ClientControlMetricsSnapshot{};
    pendingGeneration_ = 0;
  }
  ClientControlMetricsSnapshot pending_{};
  uint64_t pendingGeneration_ = 0;
  const ClientControlMetricsSnapshot empty_{};
  uint64_t windows_ = 0, reports_ = 0, withheld_ = 0, dropped_ = 0, held_ = 0, holdLimited_ = 0;
};

}  // namespace remote60::native_poc

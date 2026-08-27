#pragma once

// Tuning constants and message ids of the viewer, in one place.
//
// Role:    the kXxx constants the monolith declared next to its globals: catch-up / congestion
//          thresholds and their env-overridable defaults, keyframe-request limiter defaults, the
//          picker mis-click guard, the reveal message id, thumbnail refresh, remote-cursor overlay
//          timer/size, runtime-tune bounds, the compile-time input policy.
// Thread:  none (compile-time constants).
// Input:   -
// Output:  -
// Callers: the state structs (default member initialisers), recv thread, WndProc, picker, cursor overlay.
//
// Moved verbatim from viewer_globals.hpp (viewer split refactor Phase 1-0) so the state-struct headers
// can use them without including the globals header.

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

constexpr bool kInputPolicyForceBlock = false;
// Catch-up defaults tuned for software codec path: avoid runaway multi-second lag,
// but still clamp perceived latency quickly for interactive remote use.
constexpr uint64_t kCatchupLagDropUs = 450000;       // 0.45s
constexpr uint64_t kCatchupResumeKeyLagUs = 500000;  // 0.5s
constexpr uint64_t kDecodeQueueLagDropUs = 300000;   // 0.3s
constexpr uint64_t kDecodeQueueLagResumeUs = 400000; // 0.4s
constexpr uint64_t kStaleCaptureDropUs = 50000;      // 50ms
constexpr uint64_t kUserFeedbackLagWarnUs = 90000;   // 90ms
constexpr uint64_t kUserFeedbackGapWarnUs = 50000;   // 50ms
constexpr uint64_t kUserFeedbackMinIntervalUs = 1000000;  // 1s
constexpr uint64_t kKeyframeRequestMinIntervalUsDefault = 120000;  // 120ms
constexpr uint64_t kKeyframeRequestTokenRefillUsDefault = 300000;  // 300ms / token
constexpr uint32_t kKeyframeRequestTokenCapacityDefault = 3;
constexpr uint64_t kCatchupReenterMinIntervalUsDefault = 600000;  // 600ms
constexpr uint64_t kCongestionRecoverMinUsDefault = 250000;  // 250ms
constexpr uint64_t kCongestionRecoveryTimeoutUsDefault = 1500000;  // 1.5s

// Long enough that a slow host answering a window list is not mistaken for a dead link.
constexpr uint32_t kUdpControlReadTimeoutMs = 12000;

// Picker mis-click guard. In the field a frozen-looking stream had the user frantically clicking;
// one UP landed on the first window card and silently switched the capture to another window.
// A selection now requires DOWN and UP on the SAME target and a picker that has been visible for
// at least 300ms (kPickerSelectMinShownUs), so a click that started before the picker appeared --
// or that merely ends on a card -- cannot select. ~0 = nothing pressed; 0 = desktop is a valid id.
constexpr uint64_t kPickerPressNone = ~0ULL;
constexpr uint64_t kPickerSelectMinShownUs = 300000;

// Posted to the video window when the first selected frame is ready, so the toolbar (a window of
// its own, whose show/hide must run on the UI thread) is revealed on the thread that owns it.
constexpr UINT kMsgRevealStreamView = WM_APP + 10;

constexpr uint64_t kThumbRefreshUs = 5000000;  // refresh a preview after 5 s

constexpr UINT_PTR kCursorOverlayTimerId = 0x711;
constexpr uint64_t kRemoteCursorStaleUs = 500000;  // hide after 500ms without a sample
constexpr int kCursorOverlaySize = 24;             // ring bitmap edge; window is centered on the point

constexpr uint32_t kRuntimeBitrateMin = 300000;
constexpr uint32_t kRuntimeBitrateMax = 30000000;
constexpr uint32_t kRuntimeBitrateStep = 250000;
constexpr uint32_t kRuntimeKeyintMin = 1;
constexpr uint32_t kRuntimeKeyintMax = 240;

}  // namespace remote60::native_poc::viewer

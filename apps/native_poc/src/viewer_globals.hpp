#pragma once

// Every piece of process-wide viewer state, declared in one place.
//
// Role:    the monolith's 88 file-scope globals (+ their state types and tuning constants) as
//          `extern` declarations, grouped by feature exactly as they were declared. This header
//          is transitional: Phase 1 folds each group into a state struct and Phase 3 hands them
//          to a ViewerContext owned by main(), after which this file disappears.
// Thread:  see the `// thread:` block above each group (the pre-refactor rules, unchanged).
// Input:   -
// Output:  declarations only; definitions live in viewer_globals.cpp with the original initialisers.
// Callers: every viewer_* module and native_video_client_main.cpp.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-0).

#include "viewer_common.hpp"
#include "viewer_present_stats.hpp"
#include "viewer_frame_buffer.hpp"
#include "viewer_session_state.hpp"
#include "viewer_constants.hpp"
#include "viewer_nv12_renderer.hpp"

namespace remote60::native_poc::viewer {

// thread: recv writes gFrameBuf.frame/version, UI reads it under gFrameBuf.frame.mu and stamps gLastPresented*;
// gFrameBuf.paintQueued coalesces InvalidateRect between recv and UI; trace counters recv/UI.

// Diagnostics-only: expected present interval (from fpsHint), published once at startup so the
// present-stage stream telemetry on the UI thread can flag gaps past 1.5x cadence without reaching
// into the recv thread's Args. 0 => fall back to a 60fps assumption.

enum class ClientCongestionState : uint8_t {
  Normal = 0,
  Recovering = 1,
  Congested = 2,
};

// thread: UI enqueues input (gInputQueueState) that the control thread drains; gSession.inputEnabled is
// set by main at connect and cleared at shutdown; gUdpControl is written by the control thread
// and ticked/fed by the recv thread (one reader, one writer on the shared socket).
extern ClientInputQueue gInputQueueState;
// Which candidate won the race. The relay is billed per byte, so the session says which one it
// is rather than leaving the user to guess from the bill.

// Control over the media socket, for hosts reached through the directory.
//
// A second TCP connection cannot be opened to a host behind NAT: only the UDP socket was
// punched, so control has to ride it. Everything the session needs -- input, the window list,
// the monitor list, runtime tuning -- goes through here, which is why a session without it
// shows a picture and responds to nothing.
extern remote60::native_poc::UdpControlChannel gUdpControl;
extern std::atomic<bool> gControlOverUdp;
// Counted at the point the exchange succeeded, so it can be compared against the acks: the two
// diverging is what tells "the host never answered" apart from "nothing was ever sent".
extern std::atomic<uint16_t> gMouseButtons;
extern std::atomic<int32_t> gLastInputVideoX;
extern std::atomic<int32_t> gLastInputVideoY;

struct ClientRuntimeMetrics {
  std::atomic<uint32_t> seq{0};
  std::atomic<uint32_t> width{0};
  std::atomic<uint32_t> height{0};
  std::atomic<uint32_t> recvFpsX100{0};
  std::atomic<uint32_t> decodedFpsX100{0};
  std::atomic<uint32_t> recvMbpsX1000{0};
  std::atomic<uint32_t> skippedFrames{0};
  std::atomic<uint64_t> avgLatencyUs{0};
  std::atomic<uint64_t> maxLatencyUs{0};
  std::atomic<uint64_t> avgDecodeTailUs{0};
  std::atomic<uint64_t> maxDecodeTailUs{0};
  std::atomic<uint32_t> congestionState{0};
  std::atomic<uint32_t> congestionTransitions{0};
  std::atomic<uint32_t> congestionRecoveryCount{0};
  std::atomic<uint32_t> congestionRecoveryReq{0};
  std::atomic<uint32_t> congestionRecoveryMaxUs{0};
  std::atomic<uint32_t> queueDepthMax{0};
  std::atomic<uint32_t> queueDepthH4p{0};
  std::atomic<uint32_t> udpAssemblyDropPm{0};
  std::atomic<uint64_t> updatedQpcUs{0};
};

// thread: recv publishes gClientMetrics (atomics); control snapshots it for the host, UI reads
// fps for the toolbar. Present counters are UI-written and read by the recv 1s stats line.
extern ClientRuntimeMetrics gClientMetrics;
extern KeyframeRequestState gKeyframeRequests;
// While the picker overlays a live stream (mid-session picker no longer stops it), presents pause
// but frames keep arriving, so lag-vs-last-presented would misread the overlay as decode backlog
// and start catchup churn. Suppress catchup while the picker is up and briefly after it closes
// (until the first present re-anchors gFrameBuf.lastPresentedCaptureUs).


// thread: main fills gSession.overlayConfig once; control writes the host capture meta on every pong;
// the request states (tune/stream/capture-mode/scheduler) are UI/main producers, control consumer.
extern std::atomic<bool> gControlConnected;
extern std::atomic<uint32_t> gHostCaptureTargetPid;
extern std::atomic<uint32_t> gHostCaptureTargetFlags;
extern std::atomic<uint32_t> gHostCaptureRebindCount;
extern std::atomic<uint64_t> gHostCaptureTargetHwnd;
extern std::atomic<uint64_t> gHostCaptureMetaUpdatedUs;
extern std::mutex gHostCaptureMetaMu;
extern std::string gHostCaptureTargetProcess;
extern std::string gHostCaptureTargetTitle;
extern RuntimeTuneState gRuntimeTuneState;
extern std::atomic<bool> gCaptureOverviewMode;
extern remote60::native_poc::StreamStateControl gStreamStateControl;

extern CaptureModeRequestState gCaptureModeRequests;
extern ClientControlScheduler gControlScheduler;

struct OverlayMetricSample {
  uint64_t tsUs = 0;
  uint32_t recvFpsX100 = 0;
  uint32_t decodedFpsX100 = 0;
  uint32_t recvMbpsX1000 = 0;
  uint64_t avgLatencyUs = 0;
};

struct OverlayMetricAverages {
  uint32_t recvFpsX100 = 0;
  uint32_t decodedFpsX100 = 0;
  uint32_t recvMbpsX1000 = 0;
  uint64_t avgLatencyUs = 0;
  uint32_t sampleCount = 0;
};

// thread: recv pushes overlay metric samples; UI (overlay) reads under gOverlayMetricsMu.
extern std::mutex gOverlayMetricsMu;
extern std::deque<OverlayMetricSample> gOverlayMetrics;

// thread: picker state is UI-owned; control applies window/monitor lists and thumbnails;
// the selection gate is begun/committed on UI, acked on control, gated on recv (see comments).
extern WindowPanelStateModel gWindowPanelState;
// Which screen the shell asked for. Applied once the host has said it understands the monitor
// messages, which it does in the window list.
extern std::atomic<bool> gWindowPickerVisible;
extern std::atomic<bool> gWindowPickerToggleDown;
extern std::atomic<int> gGridScrollRow;  // card grid scroll, in whole rows
extern std::atomic<uint64_t> gPickerShownAtUs;
extern std::atomic<uint64_t> gPickerPressTargetId;

// Target-selection gate, mirroring the Android policy (commit 4892dea). After connecting the
// session opens on the picker; picking a target starts the stream but the picker stays up, and
// video is not presented, until the first frame of the *acknowledged* generation has decoded.
// That keeps an initial default-desktop frame -- or a frame from the previously selected target
// -- from flashing under the picker, and keeps a slow first frame from being mistaken for a
// failed selection.
//   gSelectionPending      : a selection is in flight (from click until first frame or failure).
//   gSelectionAwaitingAck  : request sent, host's WindowSelected ack not yet seen.
//   gSelectionExpectedGeneration : the ack's streamGeneration for the *in-flight* transaction;
//                                  frames of other generations drop while pending.
//   gSelectionEpoch        : bumped per selection so the receive loop resets the decoder once.
//   gActiveStreamGeneration : generation of the last successfully revealed selection; after
//                             reveal this is the persistent filter (0 = accept anything, which
//                             covers the legacy stream-view start and the window before any pick).
extern std::atomic<bool> gSelectionPending;
extern std::atomic<bool> gSelectionAwaitingAck;
extern std::atomic<uint64_t> gSelectionExpectedGeneration;
extern std::atomic<uint64_t> gSelectionEpoch;
extern std::atomic<uint64_t> gActiveStreamGeneration;
// The reveal is decided on the video thread but *committed* on the UI thread, so a cancel / new
// selection / disconnect that races the post cannot wrongly close the picker. The video thread
// records the candidate (generation + epoch) and posts once; the UI handler revalidates against
// the live selection state before committing, and always releases the latch so a later legitimate
// first frame can re-post.
extern std::atomic<uint64_t> gSelectionReadyGeneration;
extern std::atomic<uint64_t> gSelectionReadyEpoch;
extern std::atomic<bool> gSelectionRevealPosted;

// Preview thumbnails for the target picker, fetched over the control channel when the host
// advertises kControlWindowListFlagThumbnails. Keyed by window id; id 0 is the desktop.
struct WindowThumb {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> bgra;
  uint64_t fetchedUs = 0;
};
extern std::mutex gThumbMu;
extern std::unordered_map<uint64_t, std::shared_ptr<const WindowThumb>> gThumbs;
extern std::deque<uint64_t> gThumbFetchQueue;
extern std::atomic<bool> gHostSupportsThumbnails;

// thread: touch/mouse suppression is UI-only; the remote cursor sample is recv-written,
// UI-timer-read (latest wins).
extern std::atomic<uint64_t> gSuppressMouseUntilUs;
// Remote hardware-cursor state (UdpCursorPosPacket, DXGI desktop capture only). The host's
// pipeline drops pointer-only frames, so this side channel is what keeps the remote cursor
// visibly moving on a still screen. Drawn as a layered overlay; hidden when stale (>500ms).
extern std::atomic<int32_t> gRemoteCursorX;
extern std::atomic<int32_t> gRemoteCursorY;
extern std::atomic<uint32_t> gRemoteCursorCapW;
extern std::atomic<uint32_t> gRemoteCursorCapH;
extern std::atomic<uint64_t> gRemoteCursorGeneration;  // stream generation the sample belongs to
extern std::atomic<bool> gRemoteCursorVisible;
extern std::atomic<uint64_t> gRemoteCursorUpdateUs;
extern HWND gCursorOverlayHwnd;
extern std::atomic<uint32_t> gActiveTouchPointerId;
extern std::atomic<bool> gActiveTouchDown;


// thread: UI only (GDI objects).
// GDI defaults to the legacy System bitmap font, which is unscalable and cannot render
// non-Latin window titles. Everything drawn through draw_text_utf8 selects this instead.
extern HFONT gUiFont;
extern HFONT gUiTitleFont;
extern int gUiDpi;

// thread: UI only (key state), macro engine shared with the macro window on the UI thread.
// Which keys this client forwarded a down for, so the matching up is forwarded by memory
// rather than by re-deciding. The decision depends on modifier state, and re-evaluating it
// at release time strands keys on the host: Ctrl+A with Ctrl released first re-classifies
// the A as text on the way up, and the host holds A down forever.
extern std::atomic<bool> gForwardedKeyDown[256];


extern remote60::native_poc::InputMacro gInputMacro;
extern std::atomic<bool> gMacroButtonDown;

// thread: UI only (swap chain); the decoder shares its device when the DXGI surface opt-in is on.
extern Nv12D3dRenderer gNv12Renderer;

extern SessionState gSession;
extern FrameBuffer gFrameBuf;
extern PresentStats gPresent;
}  // namespace remote60::native_poc::viewer

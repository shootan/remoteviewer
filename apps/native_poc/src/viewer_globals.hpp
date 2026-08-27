#pragma once

// The viewer's process-wide state: ten feature structs, one instance each.
//
// Role:    declares the instances every viewer module and main() share. Phase 0 kept the monolith's
//          88 globals here as `extern` declarations; Phase 1 folded them into the state structs
//          below (each header documents its fields, initialisers and thread rules). This header is
//          still transitional: Phase 3 moves the instances into a ViewerContext owned by main(),
//          after which this file disappears.
// Thread:  per struct -- see the `// cross-thread:` blocks in each state header.
// Input:   -
// Output:  declarations only; definitions live in viewer_globals.cpp.
// Callers: every viewer_* module and native_video_client_main.cpp.

#include "viewer_common.hpp"
#include "viewer_constants.hpp"
#include "viewer_session_state.hpp"
#include "viewer_frame_buffer.hpp"
#include "viewer_present_stats.hpp"
#include "viewer_client_metrics.hpp"
#include "viewer_control_state.hpp"
#include "viewer_picker_state.hpp"
#include "viewer_selection_gate.hpp"
#include "viewer_input_state.hpp"
#include "viewer_remote_cursor.hpp"
#include "viewer_ui_resources.hpp"
#include "viewer_frame_gate_state.hpp"

namespace remote60::native_poc::viewer {

extern SessionState gSession;          // 1-1  run flag, sockets, window handle, input on/off, log mutex
extern FrameBuffer gFrameBuf;          // 1-2  latest decoded frame + present bookkeeping
extern PresentStats gPresent;          // 1-3  present counters, trace switches, WM_PAINT bookkeeping
extern ClientMetricsState gMetrics;    // 1-4  metrics published to the host / overlay ring
extern ControlChannelState gControl;   // 1-5  scheduler, request states, UDP tunnel, host capture meta
extern PickerState gPicker;            // 1-6  target picker: panel model, gestures, thumbnails
extern SelectionGateState gSel;        // 1-7  PC-side selection gate + active generation filter
extern InputState gInput;              // 1-8  mouse/key/touch state, macro engine
extern RemoteCursorState gCursor;      // 1-9  remote cursor sample + overlay window
extern UiResources gUi;                // 1-10 fonts, dpi, brush cache, NV12 presenter

}  // namespace remote60::native_poc::viewer

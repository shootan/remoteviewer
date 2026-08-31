#pragma once

// The viewer's session state: ten feature structs, one instance each, owned by main() through
// ViewerContext (viewer_context.hpp) and handed to every module as `ViewerState& ctx`.
//
// Role:    what used to be the process-wide globals gSession / gFrameBuf / ... of viewer_globals.hpp
//          (viewer split refactor Phase 1). Phase 3 left them global because threading a context
//          through ~55 free functions was deferred; viewer ledger F-17 finished that: there are no
//          viewer globals left, the window procedure finds its state through GWLP_USERDATA, and the
//          two threads hold a reference. Member order is the former definition order of
//          viewer_globals.cpp, so destruction order is unchanged.
// Thread:  per struct -- see the `// cross-thread:` blocks in each state header.
// Input:   -
// Output:  -
// Callers: every viewer_* module, ViewerContext.

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

struct ViewerState {
  SessionState session;          // 1-1  run flag, sockets, window handle, input on/off, log mutex
  FrameBuffer frameBuf;          // 1-2  latest decoded frame + present bookkeeping
  PresentStats present;          // 1-3  present counters, trace switches, WM_PAINT bookkeeping
  ClientMetricsState metrics;    // 1-4  metrics published to the host / toolbar
  ControlChannelState control;   // 1-5  scheduler, request states, UDP tunnel
  PickerState picker;            // 1-6  target picker: panel model, gestures, thumbnails
  SelectionGateState sel;        // 1-7  PC-side selection gate + active generation filter
  InputState input;              // 1-8  mouse/key/touch state, macro engine
  RemoteCursorState cursor;      // 1-9  remote cursor sample + overlay window
  UiResources ui;                // 1-10 fonts, dpi, brush cache, NV12 presenter
};

}  // namespace remote60::native_poc::viewer

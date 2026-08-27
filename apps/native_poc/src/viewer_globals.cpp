// Definitions of the viewer's process-wide state (see viewer_globals.hpp for roles and threads).
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-0): each
// definition keeps its original initialiser. None of the dynamic initialisers reads another
// global, so the order across translation units does not matter.

#include "viewer_globals.hpp"

namespace remote60::native_poc::viewer {

HFONT gUiFont = nullptr;
HFONT gUiTitleFont = nullptr;
int gUiDpi = 96;
Nv12D3dRenderer gNv12Renderer;
SessionState gSession;FrameBuffer gFrameBuf;PresentStats gPresent;ClientMetricsState gMetrics;ControlChannelState gControl;PickerState gPicker;SelectionGateState gSel;InputState gInput;RemoteCursorState gCursor;
}  // namespace remote60::native_poc::viewer

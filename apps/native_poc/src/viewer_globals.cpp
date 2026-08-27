// Definitions of the viewer's process-wide state instances (see viewer_globals.hpp).
//
// Every initialiser is a default member initialiser in the state header, copied from the monolith
// (viewer split refactor Phase 0-0 -> Phase 1). None of them reads another global, so the order
// across translation units does not matter.

#include "viewer_globals.hpp"

namespace remote60::native_poc::viewer {

SessionState gSession;
FrameBuffer gFrameBuf;
PresentStats gPresent;
ClientMetricsState gMetrics;
ControlChannelState gControl;
PickerState gPicker;
SelectionGateState gSel;
InputState gInput;
RemoteCursorState gCursor;
UiResources gUi;

}  // namespace remote60::native_poc::viewer

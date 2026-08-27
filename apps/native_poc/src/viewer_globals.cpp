// Definitions of the viewer's process-wide state (see viewer_globals.hpp for roles and threads).
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-0): each
// definition keeps its original initialiser. None of the dynamic initialisers reads another
// global, so the order across translation units does not matter.

#include "viewer_globals.hpp"

namespace remote60::native_poc::viewer {

std::atomic<uint16_t> gMouseButtons{0};
std::atomic<int32_t> gLastInputVideoX{0};
std::atomic<int32_t> gLastInputVideoY{0};
WindowPanelStateModel gWindowPanelState;
std::atomic<bool> gWindowPickerVisible{true};
std::atomic<bool> gWindowPickerToggleDown{false};
std::atomic<int> gGridScrollRow{0};  // card grid scroll, in whole rows
std::atomic<uint64_t> gPickerShownAtUs{0};
std::atomic<uint64_t> gPickerPressTargetId{kPickerPressNone};
std::atomic<bool> gSelectionPending{false};
std::atomic<bool> gSelectionAwaitingAck{false};
std::atomic<uint64_t> gSelectionExpectedGeneration{0};
std::atomic<uint64_t> gSelectionEpoch{0};
std::atomic<uint64_t> gActiveStreamGeneration{0};
std::atomic<uint64_t> gSelectionReadyGeneration{0};
std::atomic<uint64_t> gSelectionReadyEpoch{0};
std::atomic<bool> gSelectionRevealPosted{false};
std::mutex gThumbMu;
std::unordered_map<uint64_t, std::shared_ptr<const WindowThumb>> gThumbs;
std::deque<uint64_t> gThumbFetchQueue;
std::atomic<bool> gHostSupportsThumbnails{false};
std::atomic<uint64_t> gSuppressMouseUntilUs{0};
std::atomic<int32_t> gRemoteCursorX{0};
std::atomic<int32_t> gRemoteCursorY{0};
std::atomic<uint32_t> gRemoteCursorCapW{0};
std::atomic<uint32_t> gRemoteCursorCapH{0};
std::atomic<uint64_t> gRemoteCursorGeneration{0};  // stream generation the sample belongs to
std::atomic<bool> gRemoteCursorVisible{false};
std::atomic<uint64_t> gRemoteCursorUpdateUs{0};
HWND gCursorOverlayHwnd = nullptr;
std::atomic<uint32_t> gActiveTouchPointerId{0};
std::atomic<bool> gActiveTouchDown{false};
HFONT gUiFont = nullptr;
HFONT gUiTitleFont = nullptr;
int gUiDpi = 96;
std::atomic<bool> gForwardedKeyDown[256]{};
remote60::native_poc::InputMacro gInputMacro;
std::atomic<bool> gMacroButtonDown{false};
Nv12D3dRenderer gNv12Renderer;
SessionState gSession;FrameBuffer gFrameBuf;PresentStats gPresent;ClientMetricsState gMetrics;ControlChannelState gControl;
}  // namespace remote60::native_poc::viewer

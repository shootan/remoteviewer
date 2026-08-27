#pragma once

// The viewer's present path: WM_PAINT.
//
// Role:    paint_video_frame -- snapshot the latest FrameBuffer frame under its mutex, present it
//          (D3D11 NV12 surface / NV12 upload, GDI fallbacks: NV12->BGRA conversion, raw BGRA) or the
//          black background before the first frame / under the picker, draw the picker overlay, stamp
//          lastPresented*, emit the present telemetry ([trace_present], [present] frameGapUs,
//          [telemetry] stage=present, [user-feedback]) and re-invalidate when a newer frame arrived
//          while painting.
// Thread:  UI only (swap chain, GDI, the WM_PAINT bookkeeping in PresentStats).
// Input:   gFrameBuf.frame, picker visibility, PresentStats trace switches.
// Output:  the picture; present counters and timestamps; log lines.
// Callers: WndProc (WM_PAINT).
//
// Body is the WM_PAINT case of WndProc, verbatim (viewer split refactor Phase 2-8).

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

LRESULT paint_video_frame(HWND hwnd);

}  // namespace remote60::native_poc::viewer

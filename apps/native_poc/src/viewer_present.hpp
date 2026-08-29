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

// Ask the UI thread to repaint the video, coalescing bursts.
//
// The one place allowed to set gFrameBuf.paintQueued. It was open-coded in four places, so a fix
// in one of them would have left the others wedging the viewer. Rolls the latch back if
// InvalidateRect fails, because a latch set without a matching invalid region is exactly the
// permanent-freeze state. (Viewer ledger F-20.)
void request_video_paint(HWND hwnd);

// Safety net for the same failure mode: if frames are arriving but nothing has painted for a
// while, ask again regardless of the latch. Called from the existing UI timer.
void poll_video_paint_liveness(HWND hwnd);

}  // namespace remote60::native_poc::viewer

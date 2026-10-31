#pragma once

// dxgi-wedge-fix r3 (Codex E3) -- TEST BUILD ONLY (GNLinkViewerR3Test.exe,
// REMOTE60_VIEWER_PRESENT_FAIL_TEST_SEAM). Marker: viewer-present-fail-seam.
//
// While the file named by GNLINK_VIEWER_TEST_PRESENT_FAIL_FILE exists, the viewer's video present does not
// render: the frame is decoded, revealed and handed to the present path as always, and the present then
// reports failure -- exactly what a failed swapchain present gives the product's code that follows
// (note_present_result: retry, then exit 43 after 2 s; restore_frame_presented is not reached). The shipped
// GNLinkViewer has none of this (the release gate refuses the marker and the variable's prefix).

#ifdef REMOTE60_VIEWER_PRESENT_FAIL_TEST_SEAM

#include <windows.h>

namespace remote60::native_poc::viewer {

inline const char* kViewerPresentFailSeamMarker = "viewer-present-fail-seam";

inline bool viewer_present_fail_armed() {
  wchar_t path[MAX_PATH] = {};
  const DWORD n = GetEnvironmentVariableW(L"GNLINK_VIEWER_TEST_PRESENT_FAIL_FILE", path, MAX_PATH);
  return n > 0 && n < MAX_PATH && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace remote60::native_poc::viewer

#endif  // REMOTE60_VIEWER_PRESENT_FAIL_TEST_SEAM

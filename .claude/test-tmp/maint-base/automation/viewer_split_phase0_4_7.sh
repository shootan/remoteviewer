#!/usr/bin/env bash
# Viewer split refactor Phase 0-4 (viewer_args), 0-5 (viewer_decoder_backend), 0-6 (viewer_gdi_util),
# 0-7 (viewer_nv12_renderer, header-only + gNv12Renderer to viewer_globals). Same pattern as 0-2/0-3:
# verbatim move, include wiring, identity check against HEAD, build + e2e, commit. Run from the repo root.
set -euo pipefail
S=apps/native_poc/src
M=$S/native_video_client_main.cpp
C=apps/native_poc/CMakeLists.txt
git diff --quiet HEAD || { echo "tree not clean"; exit 1; }
T=/tmp/v0407; rm -rf $T; mkdir -p $T

add_include() { perl -0pi -e 's/((?:#include "viewer_[a-z_0-9]+\.hpp"\r\n)+)/$1#include "'"$1"'"\r\n/ or die "include anchor"' "$M"; }
add_cmake()   { perl -0pi -e 's/(  src\/viewer_globals\.cpp\r?\n)/$1  src\/'"$1"'\r\n/ or die "cmake anchor"' "$C"; }
check() { local r=$1; shift; cat "$@" > $T/concat.txt; perl automation/viewer_split_check.pl HEAD "$r" $T/concat.txt; }
ranges() { echo "$1" | sed -n 's/^RANGES //p'; }
prelude() {  # $1 out file, $2 module name, $3.. role lines ; emits a header prelude
  local out=$1 name=$2; shift 2
  { echo '#pragma once'; echo; printf '%s\n' "$@"; echo; } > "$out"
}

# ================= 0-4 viewer_args =================
cat > $T/args_hpp.txt <<'EOF'
#pragma once

// Command line of GNLinkViewer.
//
// Role:    struct Args (every --flag the viewer accepts, with defaults) and parse_args, including
//          the --config JSON profile overrides.
// Thread:  main only, before anything else starts.
// Input:   argc/argv, optional JSON profile, environment.
// Output:  a filled Args.
// Callers: main(); the shell (client_shell_main.cpp) is the other side of this contract.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-4).

#include "viewer_common.hpp"
#include "viewer_env_util.hpp"

namespace remote60::native_poc::viewer {

EOF
cat > $T/args_cpp.txt <<'EOF'
// See viewer_args.hpp. Extracted verbatim from native_video_client_main.cpp (Phase 0-4).

#include "viewer_args.hpp"

#include <iostream>

namespace remote60::native_poc::viewer {

EOF
OUT=$(perl automation/viewer_split_move.pl --src "$M" --hpp $S/viewer_args.hpp --hpp-prelude $T/args_hpp.txt \
  --cpp $S/viewer_args.cpp --cpp-prelude $T/args_cpp.txt \
  --start '^struct Args \{$' --start '^Args parse_args\(int argc, char\*\* argv\) \{$')
echo "$OUT"; R=$(ranges "$OUT")
add_include viewer_args.hpp; add_cmake viewer_args.cpp
check "$R" $S/viewer_args.hpp $S/viewer_args.cpp
bash automation/viewer_split_gate.sh --e2e
git add "$M" "$C" $S/viewer_args.hpp $S/viewer_args.cpp
git commit -q -F - <<EOF
refactor(viewer): Phase 0-4 — Args and parse_args to viewer_args (verbatim)

Source ranges $R of the previous revision. Gates: move check PASS, build exit 0, viewer e2e ALL PASS.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
git log -1 --format='%h %s'

# ================= 0-5 viewer_decoder_backend =================
cat > $T/dec_hpp.txt <<'EOF'
#pragma once

// Decoder backend request matching, for the "backendFallbackReason" the viewer logs when the
// H.264 decoder comes up with something other than what REMOTE60_NATIVE_DECODER_BACKEND asked for.
//
// Role:    backend_request_is_any / backend_request_satisfied / backend_request_is_vendor_specific /
//          backend_fallback_reason -- pure string policy.
// Thread:  none (pure).
// Input:   the requested backend string and the resolved backend name.
// Output:  a fallback-reason token for the log line.
// Callers: recv thread, decoder initialisation log.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-5). The host has
// the same four functions in host_capture_device.cpp -- Phase 0-15 unifies them.

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

EOF
cat > $T/dec_cpp.txt <<'EOF'
// See viewer_decoder_backend.hpp. Extracted verbatim from native_video_client_main.cpp (Phase 0-5).

#include "viewer_decoder_backend.hpp"

#include "viewer_env_util.hpp"

namespace remote60::native_poc::viewer {

EOF
OUT=$(perl automation/viewer_split_move.pl --src "$M" --hpp $S/viewer_decoder_backend.hpp --hpp-prelude $T/dec_hpp.txt \
  --cpp $S/viewer_decoder_backend.cpp --cpp-prelude $T/dec_cpp.txt \
  --start '^bool backend_request_is_any\(const std::string& requestLower, const char\* const\* values,$' \
  --start '^bool backend_request_satisfied\(const std::string& requestLower, const std::string& resolvedLower\) \{$' \
  --start '^bool backend_request_is_vendor_specific\(const std::string& requestLower\) \{$' \
  --start '^std::string backend_fallback_reason\(const std::string& requestedRaw, const char\* resolvedBackendRaw\) \{$')
echo "$OUT"; R=$(ranges "$OUT")
add_include viewer_decoder_backend.hpp; add_cmake viewer_decoder_backend.cpp
check "$R" $S/viewer_decoder_backend.hpp $S/viewer_decoder_backend.cpp
bash automation/viewer_split_gate.sh --e2e
git add "$M" "$C" $S/viewer_decoder_backend.hpp $S/viewer_decoder_backend.cpp
git commit -q -F - <<EOF
refactor(viewer): Phase 0-5 — decoder backend request matching to viewer_decoder_backend (verbatim)

Source ranges $R of the previous revision. Gates: move check PASS, build exit 0, viewer e2e ALL PASS.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
git log -1 --format='%h %s'

# ================= 0-6 viewer_gdi_util =================
cat > $T/gdi_hpp.txt <<'EOF'
#pragma once

// GDI helpers of the viewer window.
//
// Role:    DPI scaling (dpi_scale), the UI fonts (ensure_ui_font), the per-colour brush cache
//          (brush_cache / cached_brush / destroy_cached_gdi_objects), and the small drawing
//          primitives (draw_text_utf8, draw_alpha_rect, draw_panel_button).
// Thread:  UI only -- every function touches GDI objects owned by the window thread.
// Input:   HDC/RECT/colours/UTF-8 text.
// Output:  pixels on the paint DC; cached HFONT/HBRUSH objects (freed by destroy_cached_gdi_objects).
// Callers: viewer_layout (dpi_scale), viewer_overlay_draw, WndProc (WM_DPICHANGED, WM_DESTROY), create_window.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-6).

#include "viewer_common.hpp"
#include "viewer_globals.hpp"

namespace remote60::native_poc::viewer {

EOF
cat > $T/gdi_cpp.txt <<'EOF'
// See viewer_gdi_util.hpp. Extracted verbatim from native_video_client_main.cpp (Phase 0-6).

#include "viewer_gdi_util.hpp"

#include "viewer_env_util.hpp"

namespace remote60::native_poc::viewer {

EOF
OUT=$(perl automation/viewer_split_move.pl --src "$M" --hpp $S/viewer_gdi_util.hpp --hpp-prelude $T/gdi_hpp.txt \
  --cpp $S/viewer_gdi_util.cpp --cpp-prelude $T/gdi_cpp.txt \
  --start '^int dpi_scale\(int value\) \{ return MulDiv\(value, gUiDpi, 96\); \}$' \
  --start '^void ensure_ui_font\(HWND hwnd\) \{$' \
  --start '^std::unordered_map<COLORREF, HBRUSH>& brush_cache\(\) \{$' \
  --start '^HBRUSH cached_brush\(COLORREF color\) \{$' \
  --start '^void destroy_cached_gdi_objects\(\) \{$' \
  --start '^void draw_text_utf8\(HDC hdc, const std::string& text, RECT\* rect, UINT format\) \{$' \
  --start '^void draw_alpha_rect\(HDC hdc, const RECT& rect, COLORREF color, BYTE alpha\) \{$' \
  --start '^void draw_panel_button\(HDC hdc, const RECT& rect, const char\* label, bool active = false,$')
echo "$OUT"; R=$(ranges "$OUT")
add_include viewer_gdi_util.hpp; add_cmake viewer_gdi_util.cpp
check "$R" $S/viewer_gdi_util.hpp $S/viewer_gdi_util.cpp
bash automation/viewer_split_gate.sh --e2e
git add "$M" "$C" $S/viewer_gdi_util.hpp $S/viewer_gdi_util.cpp
git commit -q -F - <<EOF
refactor(viewer): Phase 0-6 — GDI font/brush/draw helpers to viewer_gdi_util (verbatim)

Source ranges $R of the previous revision. draw_panel_button keeps its default arguments on the
declaration only (C++ rule); bodies byte-identical. Gates: move check PASS, build exit 0, viewer e2e ALL PASS.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
git log -1 --format='%h %s'

# ================= 0-7 viewer_nv12_renderer (header-only) + gNv12Renderer =================
cat > $T/nv12_hpp.txt <<'EOF'
#pragma once

// D3D11 NV12 presenter of the viewer window.
//
// Role:    Nv12D3dRenderer -- device/swap chain/shaders, NV12 texture upload (render) or direct
//          decoder-surface sampling (render_surface), letterboxed draw and Present; plus the
//          Nv12RenderTelemetry timings the present trace logs.
// Thread:  UI only (the swap chain belongs to the window thread); the device is shared with the
//          decoder when the DXGI decode-surface opt-in is on.
// Input:   NV12 bytes or an ID3D11Texture2D + visible rect, the destination rect.
// Output:  a presented frame; false with failStage on error (the caller falls back to GDI).
// Callers: WM_PAINT (viewer_window_proc / viewer_present), startup (device sharing).
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-7); the struct
// keeps its in-class member definitions, so this stays header-only.

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

EOF
OUT=$(perl automation/viewer_split_move.pl --src "$M" --hpp $S/viewer_nv12_renderer.hpp --hpp-prelude $T/nv12_hpp.txt --header-only \
  --start '^struct Nv12RenderTelemetry \{$' --start '^struct Nv12D3dRenderer \{$')
echo "$OUT"; R=$(ranges "$OUT")
# the global instance joins viewer_globals (extern in the header, definition in the .cpp)
perl -0pi -e 's{\r\nNv12D3dRenderer gNv12Renderer;\r\n}{\r\n} or die "gNv12Renderer def"' "$M"
perl -0pi -e 's~#include "viewer_common.hpp"\r\n~#include "viewer_common.hpp"\r\n#include "viewer_nv12_renderer.hpp"\r\n~ or die "globals include"; s~\r\n\}  // namespace remote60::native_poc::viewer\r\n$~\r\n// thread: UI only (swap chain); the decoder shares its device when the DXGI surface opt-in is on.\r\nextern Nv12D3dRenderer gNv12Renderer;\r\n\r\n}  // namespace remote60::native_poc::viewer\r\n~ or die "globals hpp tail"' $S/viewer_globals.hpp
perl -0pi -e 's~\r\n\}  // namespace remote60::native_poc::viewer\r\n$~Nv12D3dRenderer gNv12Renderer;\r\n\r\n}  // namespace remote60::native_poc::viewer\r\n~ or die "globals cpp tail"' $S/viewer_globals.cpp
grep -q '^Nv12D3dRenderer gNv12Renderer;' $S/viewer_globals.cpp && grep -q '^extern Nv12D3dRenderer gNv12Renderer;' $S/viewer_globals.hpp
add_include viewer_nv12_renderer.hpp
check "$R" $S/viewer_nv12_renderer.hpp
bash automation/viewer_split_gate.sh --e2e
git add "$M" $S/viewer_nv12_renderer.hpp $S/viewer_globals.hpp $S/viewer_globals.cpp
git commit -q -F - <<EOF
refactor(viewer): Phase 0-7 — Nv12D3dRenderer to viewer_nv12_renderer.hpp (header-only, verbatim)

Source ranges $R of the previous revision; the gNv12Renderer instance joins viewer_globals
(extern + definition, no initialiser). Gates: move check PASS, build exit 0, viewer e2e ALL PASS.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
git log -1 --format='%h %s'
echo "main.cpp now $(wc -l < "$M") lines"

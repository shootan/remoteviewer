#!/usr/bin/perl
# Viewer split refactor Phase 2-8: the WM_PAINT body of WndProc (viewer_window_proc.cpp) becomes
# paint_video_frame(hwnd) in viewer_present.hpp/.cpp, verbatim (re-indented by -4); the case reads
# `case WM_PAINT: return paint_video_frame(hwnd);`.
use strict;
use warnings;
my $S = 'apps/native_poc/src';
sub slurp { my ($f) = @_; open(my $h, '<:raw', $f) or die "open $f: $!"; local $/; my $s = <$h>; close $h; $s =~ s/\r\n/\n/g; return $s; }
sub spew  { my ($f, $s) = @_; $s =~ s/\r?\n/\r\n/g; open(my $h, '>:raw', $f) or die "write $f: $!"; print $h $s; close $h; }

my $W = slurp("$S/viewer_window_proc.cpp");
$W =~ s/    case WM_PAINT: \{\n(.*?)\n      return 0;\n    \}\n    default:\n/    case WM_PAINT:\n      return paint_video_frame(hwnd);\n    default:\n/s or die "WM_PAINT anchor";
my $body = $1;
$body =~ s/^    //mg;   # case-body indent 6 -> function-body indent 2
$W =~ s/(#include "viewer_picker\.hpp"\n)/$1#include "viewer_present.hpp"\n/ or die "include anchor";
spew("$S/viewer_window_proc.cpp", $W);

spew("$S/viewer_present.hpp", <<'EOF');
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
EOF

my $cpp = <<'EOF';
// See viewer_present.hpp. Body is the WM_PAINT case of WndProc (native_video_client_main.cpp ->
// viewer_window_proc.cpp), verbatim (viewer split refactor Phase 2-8).

#include "viewer_present.hpp"

#include <iostream>
#include <sstream>

#include "viewer_gdi_util.hpp"
#include "viewer_globals.hpp"
#include "viewer_layout.hpp"
#include "viewer_log.hpp"
#include "viewer_nv12_renderer.hpp"
#include "viewer_overlay_draw.hpp"

namespace remote60::native_poc::viewer {

LRESULT paint_video_frame(HWND hwnd) {
EOF
$cpp .= $body . "\n  return 0;\n}\n\n}  // namespace remote60::native_poc::viewer\n";
spew("$S/viewer_present.cpp", $cpp);

my $C = slurp('apps/native_poc/CMakeLists.txt');
$C =~ s/(  src\/viewer_globals\.cpp\n)/$1  src\/viewer_present.cpp\n/ or die "cmake";
spew('apps/native_poc/CMakeLists.txt', $C);
print "ok: WM_PAINT -> paint_video_frame\n";

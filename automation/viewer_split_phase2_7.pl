#!/usr/bin/perl
# Viewer split refactor Phase 2-7: viewer_layout.cpp's arithmetic moved to viewer_layout_math.hpp (pure,
# header-only, written by hand from the same bodies); this rewires viewer_layout.hpp/.cpp to thin
# wrappers that read the window / state and call the pure functions, and adds the T4 test target.
use strict;
use warnings;
my $S = 'apps/native_poc/src';
sub slurp { my ($f) = @_; open(my $h, '<:raw', $f) or die "open $f: $!"; local $/; my $s = <$h>; close $h; $s =~ s/\r\n/\n/g; return $s; }
sub spew  { my ($f, $s) = @_; $s =~ s/\r?\n/\r\n/g; open(my $h, '>:raw', $f) or die "write $f: $!"; print $h $s; close $h; }
sub must  { my ($ok, $what) = @_; die "anchor failed: $what\n" unless $ok; print "ok: $what\n"; }

# ---- header: the types and the pure helpers now come from viewer_layout_math.hpp ----
my $H = slurp("$S/viewer_layout.hpp");
must($H =~ s/(#include "viewer_gdi_util\.hpp"\n)/$1#include "viewer_layout_math.hpp"\n/, 'hpp include');
must($H =~ s/struct ClientLayout \{\n.*?\};\n\n//s, 'hpp drop ClientLayout');
must($H =~ s/RECT make_rect\(int x, int y, int w, int h\);\n\n//, 'hpp drop make_rect');
must($H =~ s/bool point_in_rect\(const RECT& r, int x, int y\);\n\n//, 'hpp drop point_in_rect');
must($H =~ s/\/\/ Geometry of the card grid inside ClientLayout::listRect\. Cards hold a 16:10 preview and a\n\/\/ one-line caption, laid out left-to-right then top-to-bottom\.\nstruct CardGridMetrics \{\n.*?\};\n\n//s, 'hpp drop CardGridMetrics');
must($H =~ s/RECT card_rect_for_slot\(const RECT& gridRect, const CardGridMetrics& m, int slot\);\n\n//, 'hpp drop card_rect_for_slot');
must($H =~ s/RECT aspect_fit_rect\(const RECT& containerRect, uint32_t contentWidth, uint32_t contentHeight\);\n\n//, 'hpp drop aspect_fit_rect');
spew("$S/viewer_layout.hpp", $H);

# ---- cpp: drop the pure definitions, wrap the rest ----
my $C = slurp("$S/viewer_layout.cpp");
must($C =~ s/RECT make_rect\(int x, int y, int w, int h\) \{\n.*?\n\}\n\n//s, 'cpp drop make_rect');
must($C =~ s/bool point_in_rect\(const RECT& r, int x, int y\) \{\n.*?\n\}\n\n//s, 'cpp drop point_in_rect');
must($C =~ s/CardGridMetrics compute_card_grid\(const RECT& gridRect\) \{\n.*?\n\}\n/CardGridMetrics compute_card_grid(const RECT& gridRect) {\n  return compute_card_grid_at(gridRect, gUi.dpi);\n}\n/s, 'cpp compute_card_grid');
must($C =~ s/RECT card_rect_for_slot\(const RECT& gridRect, const CardGridMetrics& m, int slot\) \{\n.*?\n\}\n\n//s, 'cpp drop card_rect_for_slot');
must($C =~ s/RECT aspect_fit_rect\(const RECT& containerRect, uint32_t contentWidth, uint32_t contentHeight\) \{\n.*?\n  return make_rect\(containerRect\.left \+ offsetX, containerRect\.top \+ offsetY, drawWidth, drawHeight\);\n\}\n\n//s, 'cpp drop aspect_fit_rect');
must($C =~ s/ClientLayout compute_client_layout\(HWND hwnd\) \{\n  ClientLayout layout\{\};\n  if \(hwnd && IsWindow\(hwnd\)\) \{\n    GetClientRect\(hwnd, &layout\.clientRect\);\n  \} else \{\n    layout\.clientRect = make_rect\(0, 0, static_cast<int>\(gSession\.windowW\), static_cast<int>\(gSession\.windowH\)\);\n  \}\n.*?\n  return layout;\n\}\n/ClientLayout compute_client_layout(HWND hwnd) {\n  RECT clientRect{};\n  if (hwnd && IsWindow(hwnd)) {\n    GetClientRect(hwnd, &clientRect);\n  } else {\n    clientRect = make_rect(0, 0, static_cast<int>(gSession.windowW), static_cast<int>(gSession.windowH));\n  }\n  return compute_client_layout_at(clientRect, gPicker.visible.load(std::memory_order_relaxed), gUi.dpi);\n}\n/s, 'cpp compute_client_layout');
must($C =~ s/  const int relX =\n      std::clamp<int>\(x - contentRect\.left, 0,\n                      std::max<int>\(0, static_cast<int>\(contentRect\.right - contentRect\.left - 1\)\)\);\n  const int relY =\n      std::clamp<int>\(y - contentRect\.top, 0,\n                      std::max<int>\(0, static_cast<int>\(contentRect\.bottom - contentRect\.top - 1\)\)\);\n  const int videoW = std::max<int>\(1, static_cast<int>\(contentRect\.right - contentRect\.left\)\);\n  const int videoH = std::max<int>\(1, static_cast<int>\(contentRect\.bottom - contentRect\.top\)\);\n  \*outVideoX = static_cast<int32_t>\(\(static_cast<uint64_t>\(relX\) \* static_cast<uint64_t>\(frameW - 1\) \+\n                                     static_cast<uint64_t>\(videoW \/ 2\)\) \/\n                                    static_cast<uint64_t>\(videoW\)\);\n  \*outVideoY = static_cast<int32_t>\(\(static_cast<uint64_t>\(relY\) \* static_cast<uint64_t>\(frameH - 1\) \+\n                                     static_cast<uint64_t>\(videoH \/ 2\)\) \/\n                                    static_cast<uint64_t>\(videoH\)\);\n/  map_point_to_video(contentRect, frameW, frameH, x, y, outVideoX, outVideoY);\n/, 'cpp map point');
spew("$S/viewer_layout.cpp", $C);

# ---- picker: the card hit test's geometry ----
my $P = slurp("$S/viewer_picker.cpp");
must($P =~ s/  const ClientLayout layout = compute_client_layout\(hwnd\);\n  if \(!point_in_rect\(layout\.listRect, x, y\)\) return false;\n  const CardGridMetrics grid = compute_card_grid\(layout\.listRect\);\n  const int relX = x - layout\.listRect\.left;\n  const int relY = y - layout\.listRect\.top;\n  const int col = relX \/ \(grid\.cardW \+ grid\.gap\);\n  const int row = relY \/ \(grid\.cardH \+ grid\.gap\);\n  if \(col < 0 \|\| col >= grid\.cols \|\| row < 0 \|\| row >= grid\.visibleRows\) return false;\n  \/\/ Reject clicks that land in the gaps between cards\.\n  if \(relX - col \* \(grid\.cardW \+ grid\.gap\) >= grid\.cardW\) return false;\n  if \(relY - row \* \(grid\.cardH \+ grid\.gap\) >= grid\.cardH\) return false;\n  const WindowPanelSnapshot snap = gPicker\.windowPanel\.Snapshot\(\);\n  const int cardIndex =\n      gPicker\.gridScrollRow\.load\(std::memory_order_relaxed\) \* grid\.cols \+ row \* grid\.cols \+ col;\n/  const ClientLayout layout = compute_client_layout(hwnd);\n  if (!point_in_rect(layout.listRect, x, y)) return false;\n  const CardGridMetrics grid = compute_card_grid(layout.listRect);\n  int cardIndex = 0;\n  if (!card_hit_test(layout.listRect, grid, gPicker.gridScrollRow.load(std::memory_order_relaxed), x, y, &cardIndex)) {\n    return false;\n  }\n  const WindowPanelSnapshot snap = gPicker.windowPanel.Snapshot();\n/, 'picker hit test');
spew("$S/viewer_picker.cpp", $P);

# ---- CMake: the T4 target ----
my $M = slurp('apps/native_poc/CMakeLists.txt');
must($M =~ s/(target_link_libraries\(remote60_viewer_picker_gesture_test PRIVATE common ws2_32\)\n)/$1\n# Viewer split refactor T4: the viewer layout arithmetic (aspect fit, card grid, client layout, point mapping, hit test).\nadd_executable(remote60_viewer_layout_test\n  src\/viewer_layout_test.cpp\n)\ntarget_include_directories(remote60_viewer_layout_test PRIVATE src)\ntarget_link_libraries(remote60_viewer_layout_test PRIVATE user32 gdi32)\n/, 'cmake T4');
spew('apps/native_poc/CMakeLists.txt', $M);
print "done\n";

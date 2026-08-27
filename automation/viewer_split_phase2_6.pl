#!/usr/bin/perl
# Viewer split refactor Phase 2-6: the picker gesture latch in viewer_window_proc.cpp (mouse and touch
# DOWN/UP, capture/focus loss) and viewer_picker.cpp use the PickerState members of
# viewer_picker_state.cpp. Exact anchors; a miss dies before any file is written.
use strict;
use warnings;
my $S = 'apps/native_poc/src';
sub slurp { my ($f) = @_; open(my $h, '<:raw', $f) or die "open $f: $!"; local $/; my $s = <$h>; close $h; $s =~ s/\r\n/\n/g; return $s; }
sub spew  { my ($f, $s) = @_; $s =~ s/\r?\n/\r\n/g; open(my $h, '>:raw', $f) or die "write $f: $!"; print $h $s; close $h; }
sub must  { my ($ok, $what) = @_; die "anchor failed: $what\n" unless $ok; print "ok: $what\n"; }

my $W = slurp("$S/viewer_window_proc.cpp");
# a pending selection cancels the press (mouse DOWN and the touch path) -- 2 sites
my $n = ($W =~ s/(if \(gSel\.pending\.load\(std::memory_order_acquire\)\) \{\n\s+)gPicker\.pressTargetId\.store\(kPickerPressNone, std::memory_order_relaxed\);\n/$1gPicker.CancelPress();\n/g);
must($n == 2, "pending cancels press (2 sites, got $n)");
# DOWN: the 300 ms rule + the latch store -> PressTarget (mouse 8-space indent, touch 10-space)
$n = ($W =~ s/(\s+)if \(qpc_now_us\(\) <\n\s+gPicker\.shownAtUs\.load\(std::memory_order_relaxed\) \+ kPickerSelectMinShownUs\) \{\n\s+pressedId = kPickerPressNone;\n\s+\}\n\s+gPicker\.pressTargetId\.store\(pressedId, std::memory_order_relaxed\);\n/$1gPicker.PressTarget(pressedId, qpc_now_us());\n/g);
must($n == 2, "PressTarget (2 sites, got $n)");
# UP: consume the latch -> ReleaseTarget (2 sites; the mouse site carries a 2-line comment that moved to the member)
must($W =~ s/        \/\/ Consume the press latch FIRST, unconditionally: any UP ends the gesture, and an early\n        \/\/ return below must not leave a stale latch to approve a later unrelated UP\.\n        const uint64_t pressedId =\n            gPicker\.pressTargetId\.exchange\(kPickerPressNone, std::memory_order_relaxed\);\n/        const uint64_t pressedId = gPicker.ReleaseTarget();\n/, 'mouse ReleaseTarget');
must($W =~ s/          const uint64_t pressedId =\n              gPicker\.pressTargetId\.exchange\(kPickerPressNone, std::memory_order_relaxed\);\n/          const uint64_t pressedId = gPicker.ReleaseTarget();\n/, 'touch ReleaseTarget');
# mouse UP: the shown-time rule
must($W =~ s/        const uint64_t shownAtUs = gPicker\.shownAtUs\.load\(std::memory_order_relaxed\);\n        const uint64_t nowUs = qpc_now_us\(\);\n        if \(nowUs < shownAtUs \+ kPickerSelectMinShownUs\) return 0;\n        const uint64_t shownAgeMs = \(nowUs - shownAtUs\) \/ 1000;\n/        const uint64_t nowUs = qpc_now_us();\n        if (!gPicker.ShownLongEnough(nowUs)) return 0;\n        const uint64_t shownAgeMs = gPicker.ShownAgeMs(nowUs);\n/, 'mouse shown rule');
# touch UP: the shown-time rule
must($W =~ s/          const uint64_t shownAtUs = gPicker\.shownAtUs\.load\(std::memory_order_relaxed\);\n          const uint64_t nowUs = qpc_now_us\(\);\n          const bool shownLongEnough = nowUs >= shownAtUs \+ kPickerSelectMinShownUs;\n          const uint64_t shownAgeMs = \(nowUs - shownAtUs\) \/ 1000;\n/          const uint64_t nowUs = qpc_now_us();\n          const bool shownLongEnough = gPicker.ShownLongEnough(nowUs);\n          const uint64_t shownAgeMs = gPicker.ShownAgeMs(nowUs);\n/, 'touch shown rule');
# capture / focus loss cancel the press (2 sites)
$n = ($W =~ s/      gPicker\.pressTargetId\.store\(kPickerPressNone, std::memory_order_relaxed\);\n/      gPicker.CancelPress();\n/g);
must($n == 2, "capture/focus cancel (2 sites, got $n)");
die "pressTargetId still touched directly in WndProc\n" if $W =~ /pressTargetId/;
spew("$S/viewer_window_proc.cpp", $W);

my $P = slurp("$S/viewer_picker.cpp");
must($P =~ s/    gPicker\.pressTargetId\.store\(kPickerPressNone, std::memory_order_relaxed\);\n/    gPicker.CancelPress();\n/, 'picker show cancels press');
spew("$S/viewer_picker.cpp", $P);

my $C = slurp('apps/native_poc/CMakeLists.txt');
must($C =~ s/(  src\/viewer_globals\.cpp\n)/$1  src\/viewer_picker_state.cpp\n/, 'cmake viewer source');
must($C =~ s/(target_link_libraries\(remote60_viewer_selection_gate_test PRIVATE common\)\n)/$1\n# Viewer split refactor T3: the picker gesture latch (DOWN\/UP on the same target, 300 ms stability, cancel on capture loss).\nadd_executable(remote60_viewer_picker_gesture_test\n  src\/viewer_picker_state.cpp\n  src\/native_video_client_shared_core.cpp\n  src\/viewer_picker_gesture_test.cpp\n)\ntarget_include_directories(remote60_viewer_picker_gesture_test PRIVATE src)\ntarget_link_libraries(remote60_viewer_picker_gesture_test PRIVATE common ws2_32)\n/, 'cmake T3');
spew('apps/native_poc/CMakeLists.txt', $C);
print "done\n";

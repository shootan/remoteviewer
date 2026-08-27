#!/usr/bin/perl
# Viewer split refactor Phase 2-3: viewer_video_receiver.cpp (810 lines) splits by stage. The per-frame
# members -- process_h264_frame (decoder init + backend log, decode, publish to the FrameBuffer, trace /
# telemetry), flush_stats_if_due, publish_metrics, load_present_counters, append_present_counter_fields --
# move verbatim to viewer_video_receiver_frame.cpp; the socket loops (Run / run_udp / run_tcp) and the
# sink stay in viewer_video_receiver.cpp. Same class, two translation units (the host_stage_* pattern).
use strict;
use warnings;
my $S = 'apps/native_poc/src';
sub slurp { my ($f) = @_; open(my $h, '<:raw', $f) or die "open $f: $!"; local $/; my $s = <$h>; close $h; $s =~ s/\r\n/\n/g; return $s; }
sub spew  { my ($f, $s) = @_; $s =~ s/\r?\n/\r\n/g; open(my $h, '>:raw', $f) or die "write $f: $!"; print $h $s; close $h; }

my $R = slurp("$S/viewer_video_receiver.cpp");
my @moved;
for my $name (qw(load_present_counters append_present_counter_fields publish_metrics flush_stats_if_due process_h264_frame)) {
  # optional leading // comment lines, the definition header, the body up to the first column-0 "}"
  $R =~ s/^((?:\/\/[^\n]*\n)*\S[^\n]*VideoReceiver::\Q$name\E\([^{]*\{\n.*?^\}\n\n)//ms or die "block $name";
  push @moved, $1;
}
my $F = <<'EOF';
// The per-frame stage of the viewer's recv thread: the decoder path of process_h264_frame (decoder
// init + backend log, decode, publish to the FrameBuffer, trace / telemetry) and the once-a-second
// stats line. Same class as viewer_video_receiver.cpp (the socket loops); split by stage so each file
// reads in one go (viewer split refactor Phase 2-3). Bodies verbatim.

#include "viewer_video_receiver.hpp"

#include <iostream>
#include <sstream>

#include "viewer_decoder_backend.hpp"
#include "viewer_env_util.hpp"
#include "viewer_log.hpp"
#include "viewer_overlay_draw.hpp"
#include "viewer_picker.hpp"

namespace remote60::native_poc::viewer {

EOF
$F .= join('', @moved) . "}  // namespace remote60::native_poc::viewer\n";
spew("$S/viewer_video_receiver_frame.cpp", $F);
$R =~ s/\n\n\n+/\n\n/g;
spew("$S/viewer_video_receiver.cpp", $R);
my $C = slurp('apps/native_poc/CMakeLists.txt');
$C =~ s/(  src\/viewer_video_receiver\.cpp\n)/$1  src\/viewer_video_receiver_frame.cpp\n/ or die "cmake";
spew('apps/native_poc/CMakeLists.txt', $C);
print "ok: receiver split\n";

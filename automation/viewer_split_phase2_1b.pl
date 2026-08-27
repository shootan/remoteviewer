#!/usr/bin/perl
# Viewer split refactor Phase 2-1b: the five copies of the once-a-second stats block in
# viewer_video_receiver.cpp become one member, VideoReceiver::flush_stats_if_due(). Each copy is
# located by its "if (<now> >= st.statAtUs) {" line, its text is checked against the canonical
# template with that copy's variations substituted (three identical copies; the post-decode copy adds
# the coded size; the raw-path copy divides by recvFrames), and only then replaced by the call. The
# member body is the template with the variations as parameters, so every line still prints the same.
use strict;
use warnings;

my $f = 'apps/native_poc/src/viewer_video_receiver.cpp';
open(my $h, '<:raw', $f) or die; local $/; my $text = <$h>; close $h;
$text =~ s/\r\n/\n/g;
my @lines = split /\n/, $text, -1;

# canonical block, given the varying pieces; leading whitespace is stripped for comparison
sub canon {
  my ($now, $div, $w, $hh, $coded, $visible) = @_;
  my @b;
  push @b, "if ($now >= st.statAtUs) {";
  push @b, "const uint64_t avgLatencyUs = (st.$div > 0) ? (st.sumLatencyUs / st.$div) : 0;";
  push @b, "const uint64_t avgDecodeTailUs = (st.$div > 0) ? (st.sumDecodeTailUs / st.$div) : 0;";
  push @b, "const double mbps = (st.recvBytes * 8.0) / (1000.0 * 1000.0);";
  push @b, "const double decodedRawMbps = (st.decodedBytes * 8.0) / (1000.0 * 1000.0);";
  push @b, "const uint64_t decodeRatioX100 =";
  push @b, "(st.recvBytes > 0) ? ((st.decodedBytes * 100ULL) / st.recvBytes) : 0;";
  if ($visible) {
    push @b, "const uint32_t visibleW = (decoded.visibleWidth > 0) ? decoded.visibleWidth : decoded.width;";
    push @b, "const uint32_t visibleH = (decoded.visibleHeight > 0) ? decoded.visibleHeight : decoded.height;";
  }
  push @b, "publish_metrics($w, $hh, $now,";
  push @b, "avgLatencyUs, st.maxLatencyUs, avgDecodeTailUs, st.maxDecodeTailUs, mbps);";
  push @b, "std::ostringstream oss;";
  push @b, "oss << \"[native-video-client] recvFrames=\" << st.recvFrames";
  push @b, "<< \" decodedFrames=\" << st.decodedFrames";
  push @b, "<< \" skippedQueued=\" << st.skippedQueued";
  push @b, "<< \" avgLatencyUs=\" << avgLatencyUs";
  push @b, "<< \" maxLatencyUs=\" << st.maxLatencyUs";
  push @b, "<< \" avgDecodeTailUs=\" << avgDecodeTailUs";
  push @b, "<< \" maxDecodeTailUs=\" << st.maxDecodeTailUs";
  push @b, "<< \" mbps=\" << mbps";
  push @b, "<< \" decodedRawMbps=\" << decodedRawMbps";
  push @b, "<< \" decodeRatioX100=\" << decodeRatioX100";
  if ($coded) {
    push @b, "<< \" size=\" << $w << \"x\" << $hh";
    push @b, "<< \" codedSize=\" << decoded.width << \"x\" << decoded.height;";
  } else {
    push @b, "<< \" size=\" << $w << \"x\" << $hh;";
  }
  push @b, "append_congestion_fields(oss);";
  push @b, "append_present_counter_fields(oss);";
  push @b, "log_client_line(oss.str());";
  push @b, "st.$_ = 0;" for qw(recvFrames decodedFrames skippedQueued recvBytes decodedBytes sumLatencyUs maxLatencyUs sumDecodeTailUs maxDecodeTailUs);
  push @b, "st.statAtUs += 1000000ULL;";
  push @b, "}";
  return join("\n", @b);
}

my @out;
my $i = 0;
my $replaced = 0;
while ($i < @lines) {
  my $l = $lines[$i];
  if ($l =~ /^(\s*)if \((\w+) >= st\.statAtUs\) \{$/) {
    my ($ind, $now) = ($1, $2);
    my $j = $i + 1;
    $j++ while $j < @lines && $lines[$j] ne "$ind}";
    die "block end not found at line " . ($i + 1) . "\n" if $j >= @lines;
    my @blk = @lines[$i .. $j];
    my $got = join("\n", map { (my $s = $_) =~ s/^\s+//; $s } @blk);
    # detect the variations
    my ($div) = $got =~ /avgLatencyUs = \(st\.(\w+) > 0\)/ or die "divisor at line " . ($i + 1);
    my ($w, $hh) = $got =~ /publish_metrics\(([\w.]+), ([\w.]+), \Q$now\E,/ or die "publish args at line " . ($i + 1);
    my $coded = ($got =~ /codedSize=/) ? 1 : 0;
    my $visible = ($got =~ /const uint32_t visibleW =/) ? 1 : 0;
    my $want = canon($now, $div, $w, $hh, $coded, $visible);
    die "block at line " . ($i + 1) . " is not the canonical stats block:\n---got---\n$got\n---want---\n$want\n" unless $got eq $want;
    my $divArg = ($div eq 'recvFrames') ? 'true' : 'false';
    if ($visible) {
      push @out, "${ind}const uint32_t visibleW = (decoded.visibleWidth > 0) ? decoded.visibleWidth : decoded.width;";
      push @out, "${ind}const uint32_t visibleH = (decoded.visibleHeight > 0) ? decoded.visibleHeight : decoded.height;";
    }
    my $codedArgs = $coded ? "true, decoded.width, decoded.height" : "false, 0, 0";
    push @out, "${ind}flush_stats_if_due($now, $w, $hh, $codedArgs, $divArg);";
    $replaced++;
    $i = $j + 1;
    next;
  }
  push @out, $l;
  $i++;
}
die "expected 5 stats blocks, replaced $replaced\n" unless $replaced == 5;
$text = join("\n", @out);

# the member: canonical block with the variations as parameters
my $member = <<'EOF';
// The once-a-second stats line + metrics publish, formerly copied at five early-return sites of
// process_h264_frame and the raw path (F-08). `divideByRecvFrames` keeps the raw path's average
// divisor (recvFrames) apart from the H.264 path's (decodedFrames) -- F-02; `codedSize` adds the
// post-decode copy's " codedSize=" field. Output is byte-identical to the five copies.
void VideoReceiver::flush_stats_if_due(uint64_t nowUs, uint32_t w, uint32_t h, bool codedSize,
                                       uint32_t codedW, uint32_t codedH, bool divideByRecvFrames) {
  if (nowUs >= st.statAtUs) {
    const uint64_t frames = divideByRecvFrames ? st.recvFrames : st.decodedFrames;
    const uint64_t avgLatencyUs = (frames > 0) ? (st.sumLatencyUs / frames) : 0;
    const uint64_t avgDecodeTailUs = (frames > 0) ? (st.sumDecodeTailUs / frames) : 0;
    const double mbps = (st.recvBytes * 8.0) / (1000.0 * 1000.0);
    const double decodedRawMbps = (st.decodedBytes * 8.0) / (1000.0 * 1000.0);
    const uint64_t decodeRatioX100 =
        (st.recvBytes > 0) ? ((st.decodedBytes * 100ULL) / st.recvBytes) : 0;
    publish_metrics(w, h, nowUs,
                    avgLatencyUs, st.maxLatencyUs, avgDecodeTailUs, st.maxDecodeTailUs, mbps);
    std::ostringstream oss;
    oss << "[native-video-client] recvFrames=" << st.recvFrames
        << " decodedFrames=" << st.decodedFrames
        << " skippedQueued=" << st.skippedQueued
        << " avgLatencyUs=" << avgLatencyUs
        << " maxLatencyUs=" << st.maxLatencyUs
        << " avgDecodeTailUs=" << avgDecodeTailUs
        << " maxDecodeTailUs=" << st.maxDecodeTailUs
        << " mbps=" << mbps
        << " decodedRawMbps=" << decodedRawMbps
        << " decodeRatioX100=" << decodeRatioX100
        << " size=" << w << "x" << h;
    if (codedSize) oss << " codedSize=" << codedW << "x" << codedH;
    append_congestion_fields(oss);
    append_present_counter_fields(oss);
    log_client_line(oss.str());
    st.recvFrames = 0;
    st.decodedFrames = 0;
    st.skippedQueued = 0;
    st.recvBytes = 0;
    st.decodedBytes = 0;
    st.sumLatencyUs = 0;
    st.maxLatencyUs = 0;
    st.sumDecodeTailUs = 0;
    st.maxDecodeTailUs = 0;
    st.statAtUs += 1000000ULL;
  }
}

EOF
$text =~ s/(\nbool VideoReceiver::process_h264_frame\()/\n$member$1/ or die "insert member before process_h264_frame";
$text =~ s/\r?\n/\r\n/g;
open(my $o, '>:raw', $f) or die; print $o $text; close $o;

my $hf = 'apps/native_poc/src/viewer_video_receiver.hpp';
open($h, '<:raw', $hf) or die; my $hdr = <$h>; close $h;
$hdr =~ s/(  bool process_h264_frame\([^\r\n]*\);\r\n)/$1  void flush_stats_if_due(uint64_t nowUs, uint32_t w, uint32_t h, bool codedSize, uint32_t codedW, uint32_t codedH, bool divideByRecvFrames);\r\n/ or die "hpp decl anchor";
open($o, '>:raw', $hf) or die; print $o $hdr; close $o;
print "ok: 5 stats blocks -> flush_stats_if_due\n";

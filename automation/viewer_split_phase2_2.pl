#!/usr/bin/perl
# Viewer split refactor Phase 2-2: the frame-gating blocks of VideoReceiver::process_h264_frame become
# FrameGate (viewer_frame_gate.hpp/.cpp), driven through FrameGateInputs / FrameGateLag / FrameGateSink
# so the decisions can be unit-tested with time as an argument.
#   - waitForKeyFrame moves from DecoderState to FrameGateState (dec.waitForKeyFrame -> gate.waitForKeyFrame)
#   - the five helper members (queue_depth_frames, sample_queue_depth, transition_congestion_state,
#     append_congestion_fields, aligned_lag_us) move from VideoReceiver to FrameGate verbatim
#   - congestion_state_name moves from viewer_log to viewer_frame_gate_state.hpp (inline)
#   - the gating blocks are cut out of process_h264_frame verbatim, with these substitutions only:
#       h.captureQpcUs/h.seq/packetNowUs/recvGapUs/keyFrame -> in.*, presentedCapUs/catchupSuppressed -> in.*,
#       streamLagUs/decodeQueueLagEstimateUs -> lag.*, dec.decoder.reset() -> sink.reset_decoder(),
#       request_keyframe( -> sink.request_keyframe(, the decoder rebuild -> sink.rebuild_decoder(),
#       `return true;` of a drop -> the verdict; the stats flush stays in the receiver (called on the verdict)
# Every anchor is exact; a miss dies before any file is written.
use strict;
use warnings;

my $S = 'apps/native_poc/src';
sub slurp { my ($f) = @_; open(my $h, '<:raw', $f) or die "open $f: $!"; local $/; my $s = <$h>; close $h; $s =~ s/\r\n/\n/g; return $s; }
sub spew  { my ($f, $s) = @_; $s =~ s/\r?\n/\r\n/g; open(my $h, '>:raw', $f) or die "write $f: $!"; print $h $s; close $h; }
sub cut   { # remove the first occurrence of $re from $$t, return the match (dies if absent)
  my ($t, $re, $what) = @_;
  $$t =~ s/$re// or die "anchor failed: $what\n";
  return $&;
}
sub deindent { my ($s, $n) = @_; $s =~ s/^ {$n}//mg; return $s; }

my $R = slurp("$S/viewer_video_receiver.cpp");
my $RH = slurp("$S/viewer_video_receiver.hpp");
my $M = slurp("$S/native_video_client_main.cpp");
my $DS = slurp("$S/viewer_decoder_state.hpp");
my $FGS = slurp("$S/viewer_frame_gate_state.hpp");
my $LH = slurp("$S/viewer_log.hpp");
my $LC = slurp("$S/viewer_log.cpp");

# ---------- 1. waitForKeyFrame: DecoderState -> FrameGateState ----------
$DS =~ s/  bool waitForKeyFrame = false;  \/\/ set to useH264 at startup\n// or die "DS waitForKeyFrame";
$FGS =~ s/(  uint64_t frameIntervalUs = 0;  \/\/ from args\.fpsHint, set at thread start\n)/$1  \/\/ Hold non-key frames until the next IDR (set at startup to useH264, after every decoder reset).\n  bool waitForKeyFrame = false;\n/ or die "FGS waitForKeyFrame";
$M =~ s/  dec\.waitForKeyFrame = dec\.useH264;\n/  gate.waitForKeyFrame = dec.useH264;\n/ or die "main waitForKeyFrame";
$R =~ s/dec\.waitForKeyFrame/gate.waitForKeyFrame/g;

# ---------- 2. helper members VideoReceiver -> FrameGate ----------
my @helpers;
for my $name (qw(queue_depth_frames sample_queue_depth transition_congestion_state append_congestion_fields aligned_lag_us)) {
  my $m = cut(\$R, qr/^(\S[^\n]*VideoReceiver::\Q$name\E\([^{]*\{\n.*?^\}\n\n)/ms, "helper $name");
  $m =~ s/VideoReceiver::/FrameGate::/;
  push @helpers, $m;
}
for my $decl ('  uint32_t queue_depth_frames(uint64_t lagUs);', '  void sample_queue_depth(uint64_t lagUs);',
              '  void transition_congestion_state(ClientCongestionState nextState, uint64_t nowUs, const char* reason, uint64_t streamLagUs, uint64_t decodeQueueLagEstimateUs, uint32_t seq);',
              '  void append_congestion_fields(std::ostream& os);',
              '  uint64_t aligned_lag_us(uint64_t remoteTsUs, uint64_t localNowUs, bool& timelineReady, uint64_t& remoteBaseUs, uint64_t& localBaseUs);') {
  $RH =~ s/\Q$decl\E\n// or die "RH decl $decl";
}
# congestion_state_name: viewer_log -> viewer_frame_gate_state.hpp (inline)
my $csn = cut(\$LC, qr/^const char\* congestion_state_name\(ClientCongestionState state\) \{\n.*?^\}\n\n/ms, "congestion_state_name def");
$LH =~ s/^const char\* congestion_state_name\(ClientCongestionState state\);\n//m or die "congestion_state_name decl";
$csn =~ s/^const char\*/inline const char*/;
$FGS =~ s/(^\};\n\n)(struct FrameGateState \{)/$1$csn$2/m or die "FGS insert congestion_state_name";

# ---------- 3. the gating blocks out of process_h264_frame ----------
# 3a note_packet: recvGapUs computation + lastPacketRecvUs + sparse-arrival streak reset
my $np = cut(\$R, qr/    const uint64_t recvGapUs =\n        \(gate\.lastPacketRecvUs > 0 && packetNowUs >= gate\.lastPacketRecvUs\) \? \(packetNowUs - gate\.lastPacketRecvUs\) : 0;\n    gate\.lastPacketRecvUs = packetNowUs;\n    if \(recvGapUs > 250000\) \{\n      \/\/ Sparse arrival usually means source\/capture stall, not decoder backlog\.\n      gate\.lagTriggerStreak = 0;\n    \}\n/s, "note_packet block");
$R =~ s/(    st\.recvBytes \+= h\.payloadSize;\n)/$1    const uint64_t recvGapUs = fg.note_packet(packetNowUs);\n/ or die "note_packet call site";
my $np_body = deindent($np, 2);
$np_body =~ s/\n$//;
$np_body .= "\n  return recvGapUs;";

# 3b admit: from the latestCaptureSeenUs update to the end of the keyframe-wait branch
my $admit = cut(\$R, qr/    if \(h\.captureQpcUs > gate\.latestCaptureSeenUs\) \{\n.*?      flush_stats_if_due\(packetNowUs, h\.width, h\.height, false, 0, 0, false\);\n      return true;\n    \}\n/s, "admit block");
my $admit_src = $admit;
# the two inputs the receiver now supplies
$admit =~ s/    const uint64_t presentedCapUs = gFrameBuf\.lastPresentedCaptureUs\.load\(std::memory_order_relaxed\);\n// or die "admit presentedCapUs";
$admit =~ s/    \/\/ The picker overlay pauses presents on purpose; lag measured against a frozen present\n    \/\/ anchor is not congestion\. Same for the short post-close grace until the anchor is fresh\.\n    const bool catchupSuppressed =\n        gPicker\.visible\.load\(std::memory_order_relaxed\) \|\|\n        packetNowUs < gFrameBuf\.catchupSuppressUntilUs\.load\(std::memory_order_relaxed\);\n// or die "admit catchupSuppressed";
# the three drops become verdicts; the keyframe-wait drop's stats flush stays with the receiver
$admit =~ s/(      \/\/ A frame older than the anchor[^\n]*\n(?:[^\n]*\n)*?)      return true;\n    \}\n\n    const bool lagTrigger =/$1      return FrameGateVerdict::DropStale;\n    }\n\n    const bool lagTrigger =/s or die "admit stale verdict";
$admit =~ s/(                  << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs\n                  << "\\n";\n      \}\n)      return true;\n    \}\n    if \(gate\.congestionState == ClientCongestionState::Congested && keyFrame\)/$1      return FrameGateVerdict::DropCongested;\n    }\n    if (gate.congestionState == ClientCongestionState::Congested && keyFrame)/s or die "admit congested verdict";
$admit =~ s/      flush_stats_if_due\(packetNowUs, h\.width, h\.height, false, 0, 0, false\);\n      return true;\n    \}\n$/      return FrameGateVerdict::DropWaitingKeyframe;\n    }\n/s or die "admit waiting verdict";
# lag estimates are computed here and handed back
$admit =~ s/(    const uint64_t decodeQueueLagEstimateUs =\n        \(presentedCapUs > 0 && h\.captureQpcUs >= presentedCapUs\)\n            \? \(h\.captureQpcUs - presentedCapUs\)\n            : 0;\n)/$1    lag->streamLagUs = streamLagUs;\n    lag->decodeQueueLagEstimateUs = decodeQueueLagEstimateUs;\n/ or die "admit lag out";
sub gatify {
  my ($s) = @_;
  $s =~ s/\bh\.captureQpcUs\b/in.captureQpcUs/g;
  $s =~ s/\bh\.seq\b/in.seq/g;
  $s =~ s/\bpacketNowUs\b/in.packetNowUs/g;
  $s =~ s/\brecvGapUs\b/in.recvGapUs/g;
  $s =~ s/(?<![.\w])keyFrame\b/in.keyFrame/g;
  $s =~ s/(?<![.\w])presentedCapUs\b/in.presentedCapUs/g;
  $s =~ s/(?<![.\w])catchupSuppressed\b/in.catchupSuppressed/g;
  $s =~ s/dec\.decoder\.reset\(\);/sink.reset_decoder();/g;
  $s =~ s/(?<![.\w])request_keyframe\(/sink.request_keyframe(/g;
  $s =~ s/dec\.decoder\.initialize\(dec\.decoderW, dec\.decoderH, args\.fpsHint\)/sink.rebuild_decoder()/g;
  die "gatify: a dec./args./gFrameBuf/gPicker reference survived:\n$s\n" if $s =~ /\b(dec|args|gFrameBuf|gPicker|gSession)\./;
  return $s;
}
my $admit_body = gatify(deindent($admit, 2));
$admit_body =~ s/\n$//;
$admit_body .= "\n  return FrameGateVerdict::Decode;";
# receiver side of admit
my $admit_call = <<'EOF';
    FrameGateInputs in{};
    in.captureQpcUs = h.captureQpcUs;
    in.seq = h.seq;
    in.keyFrame = keyFrame;
    in.packetNowUs = packetNowUs;
    in.recvGapUs = recvGapUs;
    in.presentedCapUs = gFrameBuf.lastPresentedCaptureUs.load(std::memory_order_relaxed);
    // The picker overlay pauses presents on purpose; lag measured against a frozen present
    // anchor is not congestion. Same for the short post-close grace until the anchor is fresh.
    in.catchupSuppressed =
        gPicker.visible.load(std::memory_order_relaxed) ||
        packetNowUs < gFrameBuf.catchupSuppressUntilUs.load(std::memory_order_relaxed);
    FrameGateLag lag{};
    switch (fg.admit(in, &lag)) {
      case FrameGateVerdict::DropStale:
      case FrameGateVerdict::DropCongested:
        return true;
      case FrameGateVerdict::DropWaitingKeyframe:
        flush_stats_if_due(packetNowUs, h.width, h.height, false, 0, 0, false);
        return true;
      case FrameGateVerdict::Decode:
        break;
    }
EOF
$R =~ s/(    const bool keyFrame = \(\(h\.flags & 1u\) != 0\);\n)/$1$admit_call/ or die "admit call site";

# 3c decode failure
my $fail = cut(\$R, qr/(?<=&pendingTimestampOverflow\)\) \{\n)      gate\.decodeEmptyStreak = 0;\n.*?      transition_congestion_state\(ClientCongestionState::Congested, packetNowUs, "decode_fail",\n                                  streamLagUs, decodeQueueLagEstimateUs, h\.seq\);\n/s, "decode failure block");
$R =~ s/(&pendingTimestampOverflow\)\) \{\n)/$1      fg.note_decode_failure(in, lag);\n/ or die "failure call site";
my $fail_body = gatify(deindent($fail, 4)); $fail_body =~ s/\n$//;
$fail_body =~ s/\bstreamLagUs, decodeQueueLagEstimateUs\b/lag.streamLagUs, lag.decodeQueueLagEstimateUs/g;

# 3d decode ok
$R =~ s/    gate\.decodeConsecutiveFailCount = 0;\n    if \(pendingTimestampOverflow\) \{\n/    fg.note_decode_ok();\n    if (pendingTimestampOverflow) {\n/ or die "decode ok site";

# 3e timestamp overflow
my $over = cut(\$R, qr/(?<=    if \(pendingTimestampOverflow\) \{\n)      gate\.decodeEmptyStreak = 0;\n.*?      transition_congestion_state\(ClientCongestionState::Congested, packetNowUs, "decode_ts_overflow",\n                                  streamLagUs, decodeQueueLagEstimateUs, h\.seq\);\n/s, "ts overflow block");
$R =~ s/(    if \(pendingTimestampOverflow\) \{\n)/$1      fg.note_timestamp_overflow(in, lag);\n/ or die "overflow call site";
my $over_body = gatify(deindent($over, 4)); $over_body =~ s/\n$//;
$over_body =~ s/\bstreamLagUs, decodeQueueLagEstimateUs\b/lag.streamLagUs, lag.decodeQueueLagEstimateUs/g;

# 3f reference sync
my $sync = cut(\$R, qr/    gate\.waitForKeyFrame = false;\n    if \(keyFrame\) \{\n      \/\/ Advance the reference-chain anchor: a successfully decoded IDR resyncs the decoder, so\n      \/\/ any later stale frame older than this is safe to quiet-drop\.\n      gate\.lastDecodedKeyCaptureUs = h\.captureQpcUs;\n    \}\n/s, "reference sync block");
$R =~ s/(    if \(outFrames\.empty\(\)\) \{\n)/    fg.note_reference_sync(in);\n$1/ or die "sync call site";
my $sync_body = gatify(deindent($sync, 2)); $sync_body =~ s/\n$//;

# 3g decode empty
my $empty = cut(\$R, qr/(?<=    if \(outFrames\.empty\(\)\) \{\n)      \+\+st\.decodeEmptyCount;\n.*?                  << "\\n";\n      \}\n(?=      flush_stats_if_due\(packetNowUs, h\.width, h\.height, false, 0, 0, false\);\n      return true;\n    \}\n)/s, "decode empty block");
$R =~ s/(    if \(outFrames\.empty\(\)\) \{\n)/$1      fg.note_decode_empty(in, lag);\n/ or die "empty call site";
my $empty_body = gatify(deindent($empty, 4)); $empty_body =~ s/\n$//;
$empty_body =~ s/\bstreamLagUs, decodeQueueLagEstimateUs\b/lag.streamLagUs, lag.decodeQueueLagEstimateUs/g;

# 3h clear empty streak
$R =~ s/    gate\.decodeEmptyStreak = 0;\n    gate\.decodeEmptyStreakStartUs = 0;\n\n    auto& decoded = outFrames\.back\(\);\n/    fg.clear_empty_streak();\n\n    auto& decoded = outFrames.back();\n/ or die "clear site";

# remaining receiver references to the moved helpers and to the lag estimates admit() now returns
$R =~ s/(?<![.\w])aligned_lag_us\(/FrameGate::aligned_lag_us(/g;
$R =~ s/(?<![.\w])decodeQueueLagEstimateUs\b/lag.decodeQueueLagEstimateUs/g;
$R =~ s/(?<![.\w])streamLagUs\b/lag.streamLagUs/g;
$R =~ s/(?<![.\w])append_congestion_fields\(oss\)/fg.append_congestion_fields(oss)/g;
die "receiver still calls a moved helper directly\n" if $R =~ /(?<![.\w:])(sample_queue_depth|transition_congestion_state|queue_depth_frames)\(/;
# the receiver's sink + gate members
$R =~ s/(#include "viewer_video_receiver\.hpp"\n)/$1\n#include "viewer_frame_gate.hpp"\n/ or die "R include";
my $sink_impl = <<'EOF';
void VideoReceiver::DecoderSink::reset_decoder() { dec.decoder.reset(); }

bool VideoReceiver::DecoderSink::rebuild_decoder() {
  return dec.decoder.initialize(dec.decoderW, dec.decoderH, args.fpsHint);
}

void VideoReceiver::DecoderSink::request_keyframe(uint16_t reason) { viewer::request_keyframe(reason); }

EOF
$R =~ s/(namespace remote60::native_poc::viewer \{\n\n)/$1$sink_impl/ or die "R sink impl";

# ---------- 4. receiver header: sink + gate members ----------
$RH =~ s/(#include "viewer_decoder_state\.hpp"\n)/$1#include "viewer_frame_gate.hpp"\n/ or die "RH include";
$RH =~ s/(  RecvStats st;\n)/$1  \/\/ what the frame gate may do to the decoder and the control path\n  struct DecoderSink : FrameGateSink {\n    DecoderSink(DecoderState& dec, const Args& args) : dec(dec), args(args) {}\n    void reset_decoder() override;\n    bool rebuild_decoder() override;\n    void request_keyframe(uint16_t reason) override;\n    DecoderState& dec;\n    const Args& args;\n  };\n  DecoderSink sink{dec, args};\n  FrameGate fg{gate, st, sink};\n/ or die "RH members";

# ---------- 5. write viewer_frame_gate.cpp ----------
my $FG = <<'EOF';
// See viewer_frame_gate.hpp. Bodies are the gating blocks of VideoReceiver::process_h264_frame
// (native_video_client_main.cpp -> viewer_video_receiver.cpp), verbatim apart from the documented
// substitutions (viewer split refactor Phase 2-2).

#include "viewer_frame_gate.hpp"

#include <algorithm>
#include <iostream>
#include <limits>

#include "viewer_constants.hpp"

namespace remote60::native_poc::viewer {

EOF
$FG .= join('', @helpers);
$FG .= "uint64_t FrameGate::note_packet(uint64_t packetNowUs) {\n$np_body\n}\n\n";
$FG .= "FrameGateVerdict FrameGate::admit(const FrameGateInputs& in, FrameGateLag* lag) {\n$admit_body\n}\n\n";
$FG .= "void FrameGate::note_decode_failure(const FrameGateInputs& in, const FrameGateLag& lag) {\n$fail_body\n}\n\n";
$FG .= "// decode_access_unit succeeded: the transform is healthy, so the wedge streak is clear.\nvoid FrameGate::note_decode_ok() {\n  gate.decodeConsecutiveFailCount = 0;\n}\n\n";
$FG .= "void FrameGate::note_timestamp_overflow(const FrameGateInputs& in, const FrameGateLag& lag) {\n$over_body\n}\n\n";
$FG .= "void FrameGate::note_reference_sync(const FrameGateInputs& in) {\n$sync_body\n}\n\n";
$FG .= "void FrameGate::note_decode_empty(const FrameGateInputs& in, const FrameGateLag& lag) {\n$empty_body\n}\n\n";
$FG .= "void FrameGate::clear_empty_streak() {\n  gate.decodeEmptyStreak = 0;\n  gate.decodeEmptyStreakStartUs = 0;\n}\n\n";
$FG .= "}  // namespace remote60::native_poc::viewer\n";
# the receiver no longer needs the "decode_access_unit succeeded" comment line (moved with note_decode_ok)
$R =~ s/    \/\/ decode_access_unit succeeded: the transform is healthy, so the wedge streak is clear\.\n(    fg\.note_decode_ok\(\);)/$1/ or die "ok comment";

spew("$S/viewer_frame_gate.cpp", $FG);
spew("$S/viewer_video_receiver.cpp", $R);
spew("$S/viewer_video_receiver.hpp", $RH);
spew("$S/native_video_client_main.cpp", $M);
spew("$S/viewer_decoder_state.hpp", $DS);
spew("$S/viewer_frame_gate_state.hpp", $FGS);
spew("$S/viewer_log.hpp", $LH);
spew("$S/viewer_log.cpp", $LC);

my $C = slurp('apps/native_poc/CMakeLists.txt');
$C =~ s/(  src\/viewer_globals\.cpp\n)/$1  src\/viewer_frame_gate.cpp\n/ or die "cmake";
spew('apps/native_poc/CMakeLists.txt', $C);
print "ok: FrameGate extracted\n";

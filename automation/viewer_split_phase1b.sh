#!/usr/bin/env bash
# Viewer split refactor Phase 1-11..1-13: the recv thread's ~60 lambda locals and main()'s decoder /
# codec locals become three structs -- RecvStats st, FrameGateState gate, DecoderState dec -- still
# instances local to main()/the lambda (Phase 2 makes them VideoReceiver members). Mechanical:
# declaration lines are replaced by the struct instance (assignments keep the original expression and
# place where an initialiser was not a constant), every use is renamed with rename_outside_strings.pl
# --code-only (whole words, not in strings, not in comments, not after . -> ::). Gate per step:
# build + viewer e2e. Run from the repo root on a clean tree; pass 11/12/13 to start from a step.
set -euo pipefail
S=apps/native_poc/src
M=$S/native_video_client_main.cpp
GH=$S/viewer_globals.hpp
FROM=${1:-11}
git diff --quiet HEAD || { echo "tree not clean"; exit 1; }
T=/tmp/v1b; rm -rf $T; mkdir -p $T

region() {  # prints "a b": the recvThread lambda's first and last line numbers
  local a b
  a=$(grep -n '^  std::thread recvThread(\[&\]() {' "$M" | cut -d: -f1)
  [ -n "$a" ] || { echo "recv lambda start not found"; exit 1; }
  b=$(awk -v s="$a" 'NR>s && /^  \}\);\r?$/ {print NR; exit}' "$M")
  [ -n "$b" ] || { echo "recv lambda end not found"; exit 1; }
  echo "$a $b"
}
region_rename() {  # $1 mapfile: rename inside the recv lambda only
  local a b; read -r a b < <(region)
  head -n $((a - 1)) "$M" > $T/pre; sed -n "${a},${b}p" "$M" > $T/mid; tail -n +$((b + 1)) "$M" > $T/post
  perl automation/rename_outside_strings.pl --code-only "$1" < $T/mid > $T/mid2
  cat $T/pre $T/mid2 $T/post > "$M"
}
main_rename() { perl automation/rename_outside_strings.pl --code-only "$1" < "$M" > $T/m2 && cp $T/m2 "$M"; }
del_line() { LINE="$1" perl -0pi -e 's~^\Q$ENV{LINE}\E\r\n~~m or die "line not found: $ENV{LINE}\n"' "$M"; }
rep_line() { LINE="$1" NEW="$2" perl -0pi -e '(my $n = $ENV{NEW}) =~ s/\n/\r\n/g; s~^\Q$ENV{LINE}\E\r\n~$n\r\n~m or die "line not found: $ENV{LINE}\n"' "$M"; }
add_include() { perl -0pi -e 's/((?:#include "viewer_[a-z_0-9]+\.hpp"\r\n)+)/$1#include "'"$1"'"\r\n/ or die "include anchor"' "$M"; }
mapfile_from() { sed -E 's/^([A-Za-z0-9_]+) +/\1\t/' > "$1"; }
verify_gone() {  # $1 mapfile $2 file: no old bare identifier left (not after . -> :: or an identifier char)
  local old new bad=0
  while IFS=$'\t' read -r old new; do
    # perl, because this grep's -P refuses the CP949 locale; comment-only lines are ignored
    if OLD="$old" perl -ne 'print "$.: $_" if /(?<![.\w>:])\Q$ENV{OLD}\E\b/ && !/^\s*\/\//' "$2" | grep . ; then echo "still referenced: $old"; bad=1; fi
  done < "$1"
  [ $bad -eq 0 ]
}
commit_step() {  # $1 step, $2 title, $3 body, rest files
  local step=$1 title=$2 body=$3; shift 3
  git add "$@"
  git commit -q -F - <<EOF
refactor(viewer): Phase $step — $title

$body
Gates: build exit 0, viewer e2e ALL PASS.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
  git log -1 --format='%h %s'
}

# ================= 1-11 RecvStats st =================
if [ "$FROM" -le 11 ]; then
  for n in recvFrames decodedFrames skippedQueued recvBytes decodedBytes sumLatencyUs maxLatencyUs sumDecodeTailUs maxDecodeTailUs decodeFailCount decodeTimestampOverflowCount decodeEmptyCount decodeEmptyRecoveryCount udpChunkRecvCount udpAssemblyCompletedCount udpAssemblyDroppedCount udpAssemblyMalformedCount udpAssemblyReorderCount udpAssemblyKeyReqCount udpAssemblyFecRecoveredCount queueDepthSampleCount; do
    del_line "    uint64_t $n = 0;"
  done
  del_line "    uint32_t udpAssemblyDropPmLast = 0;"
  del_line "    uint64_t queueDepthHist[5] = {0, 0, 0, 0, 0};"
  del_line "    uint32_t queueDepthFramesMax = 0;"
  rep_line "    uint64_t statAtUs = qpc_now_us() + 1000000ULL;" "    RecvStats st;
    st.statAtUs = qpc_now_us() + 1000000ULL;"
  # the lambda-local PresentCounterSnapshot moved to the header verbatim (indentation aside)
  git show HEAD:$M | tr -d '\r' | awk '/^    struct PresentCounterSnapshot \{$/,/^    \};$/' | sed 's/^    //' > $T/pcs_head.txt
  [ -s $T/pcs_head.txt ] || { echo "PresentCounterSnapshot not found in HEAD main"; exit 1; }
  tr -d '\r' < $S/viewer_recv_stats.hpp > $T/rs.txt
  perl -e 'local $/; open(A,"<",$ARGV[0]); $a=<A>; open(B,"<",$ARGV[1]); $b=<B>; exit(index($b,$a) >= 0 ? 0 : 1)' $T/pcs_head.txt $T/rs.txt || { echo "PresentCounterSnapshot not verbatim in viewer_recv_stats.hpp"; exit 1; }
  perl -0pi -e 's~    struct PresentCounterSnapshot \{\r\n.*?    \};\r\n~~s or die "drop local struct"' "$M"
  rep_line "    PresentCounterSnapshot lastPresentCounters = load_present_counters();" "    st.lastPresentCounters = load_present_counters();"
  mapfile_from $T/map11 <<'EOF'
statAtUs st.statAtUs
recvFrames st.recvFrames
decodedFrames st.decodedFrames
skippedQueued st.skippedQueued
recvBytes st.recvBytes
decodedBytes st.decodedBytes
sumLatencyUs st.sumLatencyUs
maxLatencyUs st.maxLatencyUs
sumDecodeTailUs st.sumDecodeTailUs
maxDecodeTailUs st.maxDecodeTailUs
decodeFailCount st.decodeFailCount
decodeTimestampOverflowCount st.decodeTimestampOverflowCount
decodeEmptyCount st.decodeEmptyCount
decodeEmptyRecoveryCount st.decodeEmptyRecoveryCount
udpChunkRecvCount st.udpChunkRecvCount
udpAssemblyCompletedCount st.udpAssemblyCompletedCount
udpAssemblyDroppedCount st.udpAssemblyDroppedCount
udpAssemblyMalformedCount st.udpAssemblyMalformedCount
udpAssemblyReorderCount st.udpAssemblyReorderCount
udpAssemblyKeyReqCount st.udpAssemblyKeyReqCount
udpAssemblyFecRecoveredCount st.udpAssemblyFecRecoveredCount
udpAssemblyDropPmLast st.udpAssemblyDropPmLast
queueDepthSampleCount st.queueDepthSampleCount
queueDepthHist st.queueDepthHist
queueDepthFramesMax st.queueDepthFramesMax
lastPresentCounters st.lastPresentCounters
EOF
  region_rename $T/map11
  read -r a b < <(region); sed -n "${a},${b}p" "$M" > $T/mid_check.txt
  # the two assignments keep their bare names on the left (st.x = ...): verify nothing else is bare
  verify_gone $T/map11 $T/mid_check.txt
  add_include viewer_recv_stats.hpp
  bash automation/viewer_split_gate.sh --e2e
  commit_step 1-11 "RecvStats st — the recv thread's 26 stats locals become one struct" \
    "Declarations replaced by \`RecvStats st\` (same zero initial values; statAtUs and lastPresentCounters assigned where they were initialised); uses renamed st.<member> inside the recv lambda only (code-only rename: strings and comments untouched); the lambda-local PresentCounterSnapshot moves to the header verbatim." \
    "$M" $S/viewer_recv_stats.hpp
fi

# ================= 1-12 FrameGateState gate =================
if [ "$FROM" -le 12 ]; then
  rep_line "  const uint64_t catchupReenterMinIntervalUs = env_u32_clamped(" "  FrameGateState gate;
  gate.catchupReenterMinIntervalUs = env_u32_clamped("
  rep_line "  const uint64_t staleCaptureDropUs = env_u32_clamped(" "  gate.staleCaptureDropUs = env_u32_clamped("
  rep_line "  const uint64_t congestionRecoverMinUs = env_u32_clamped(" "  gate.congestionRecoverMinUs = env_u32_clamped("
  rep_line "  const uint64_t congestionRecoveryTimeoutUs = env_u32_clamped(" "  gate.congestionRecoveryTimeoutUs = env_u32_clamped("
  for l in "    uint64_t lastPacketRecvUs = 0;" "    uint32_t lagTriggerStreak = 0;" "    uint64_t lastCatchupEnterUs = 0;" "    uint64_t catchupEnterThrottledCount = 0;" "    bool catchupMode = false;" "    bool captureTimelineReady = false;" "    uint64_t captureRemoteBaseUs = 0;" "    uint64_t captureLocalBaseUs = 0;" "    bool sendTimelineReady = false;" "    uint64_t sendRemoteBaseUs = 0;" "    uint64_t sendLocalBaseUs = 0;" "    ClientCongestionState congestionState = ClientCongestionState::Normal;" "    uint64_t congestionStateEnterUs = 0;" "    uint64_t congestionTransitionCount = 0;" "    uint64_t congestionRecoveryCount = 0;" "    uint64_t congestionRecoveryTotalUs = 0;" "    uint64_t congestionRecoveryMaxUs = 0;" "    uint64_t congestionRecoveryRequestCount = 0;" "    uint64_t staleDropCount = 0;" "    uint64_t holdLatestDropCount = 0;" "    uint64_t burstDropCount = 0;" "    uint64_t staleReferenceRecoveryCount = 0;" "    uint64_t lastDecodedKeyCaptureUs = 0;" "    uint64_t latestCaptureSeenUs = 0;" "    uint64_t recoveringSinceUs = 0;" "    uint32_t recoveringHealthyStreak = 0;" "    uint64_t lastRecoveryRequestUs = 0;" "    uint32_t decodeConsecutiveFailCount = 0;" "    constexpr uint32_t kDecodeRebuildThreshold = 8;" "    uint64_t decodeEmptyStreak = 0;" "    uint64_t decodeEmptyStreakStartUs = 0;" "    uint64_t waitingKeyDropCount = 0;" "    uint64_t lagDropCount = 0;"; do
    del_line "$l"
  done
  # the three comment blocks that documented those locals now live in the header
  perl -0pi -e 's~    // Consecutive hard decode failures\. A flush \(decoder\.reset\) recovers a corrupt frame, but\r\n    // not a wedged hardware MFT or a lost D3D device -- and the viewer.s only recovery for a\r\n    // same-resolution decode error was that flush, so once the decoder wedged \(a YouTube scene\r\n    // change on a busy GPU could do it\) every following frame failed identically and the\r\n    // picture froze until the app was restarted\. Past a threshold, rebuild the decoder instead\.\r\n~~ or die "c-consecutive"; s~    // lastPresentedCaptureUs is now [^\r\n]*\r\n~~ or die "c-lastpresented"; s~    // Capture timestamp of the newest keyframe the decoder has successfully consumed\. A stale\r\n    // frame OLDER than this anchor was already resynced past \(safe to quiet-drop\); one AT OR\r\n    // AFTER it still sits in the live reference chain, so dropping it needs an IDR resync\.\r\n~~ or die "c-anchor"' "$M"
  rep_line "    const uint64_t frameIntervalUs = std::max<uint64_t>(" "    gate.frameIntervalUs = std::max<uint64_t>("
  mapfile_from $T/map12 <<'EOF'
lastPacketRecvUs gate.lastPacketRecvUs
lagTriggerStreak gate.lagTriggerStreak
lastCatchupEnterUs gate.lastCatchupEnterUs
catchupEnterThrottledCount gate.catchupEnterThrottledCount
catchupMode gate.catchupMode
captureTimelineReady gate.captureTimelineReady
captureRemoteBaseUs gate.captureRemoteBaseUs
captureLocalBaseUs gate.captureLocalBaseUs
sendTimelineReady gate.sendTimelineReady
sendRemoteBaseUs gate.sendRemoteBaseUs
sendLocalBaseUs gate.sendLocalBaseUs
frameIntervalUs gate.frameIntervalUs
congestionState gate.congestionState
congestionStateEnterUs gate.congestionStateEnterUs
congestionTransitionCount gate.congestionTransitionCount
congestionRecoveryCount gate.congestionRecoveryCount
congestionRecoveryTotalUs gate.congestionRecoveryTotalUs
congestionRecoveryMaxUs gate.congestionRecoveryMaxUs
congestionRecoveryRequestCount gate.congestionRecoveryRequestCount
staleDropCount gate.staleDropCount
holdLatestDropCount gate.holdLatestDropCount
burstDropCount gate.burstDropCount
staleReferenceRecoveryCount gate.staleReferenceRecoveryCount
lastDecodedKeyCaptureUs gate.lastDecodedKeyCaptureUs
latestCaptureSeenUs gate.latestCaptureSeenUs
recoveringSinceUs gate.recoveringSinceUs
recoveringHealthyStreak gate.recoveringHealthyStreak
lastRecoveryRequestUs gate.lastRecoveryRequestUs
decodeConsecutiveFailCount gate.decodeConsecutiveFailCount
kDecodeRebuildThreshold gate.kDecodeRebuildThreshold
decodeEmptyStreak gate.decodeEmptyStreak
decodeEmptyStreakStartUs gate.decodeEmptyStreakStartUs
waitingKeyDropCount gate.waitingKeyDropCount
lagDropCount gate.lagDropCount
EOF
  region_rename $T/map12
  mapfile_from $T/map12m <<'EOF'
catchupReenterMinIntervalUs gate.catchupReenterMinIntervalUs
staleCaptureDropUs gate.staleCaptureDropUs
congestionRecoverMinUs gate.congestionRecoverMinUs
congestionRecoveryTimeoutUs gate.congestionRecoveryTimeoutUs
EOF
  main_rename $T/map12m
  read -r a b < <(region); sed -n "${a},${b}p" "$M" > $T/mid_check.txt
  verify_gone $T/map12 $T/mid_check.txt
  verify_gone $T/map12m "$M"
  # ClientCongestionState leaves viewer_globals.hpp for the frame-gate header (verbatim)
  git show HEAD:$GH | tr -d '\r' | awk '/^enum class ClientCongestionState : uint8_t \{$/,/^\};$/' > $T/enum_head.txt
  [ -s $T/enum_head.txt ] || { echo "enum not in HEAD globals"; exit 1; }
  tr -d '\r' < $S/viewer_frame_gate_state.hpp > $T/fg.txt
  perl -e 'local $/; open(A,"<",$ARGV[0]); $a=<A>; open(B,"<",$ARGV[1]); $b=<B>; exit(index($b,$a) >= 0 ? 0 : 1)' $T/enum_head.txt $T/fg.txt || { echo "enum not verbatim in frame gate header"; exit 1; }
  perl -0pi -e 's~^enum class ClientCongestionState : uint8_t \{\r\n.*?^\};\r\n~~ms or die "drop enum"' "$GH"
  perl -0pi -e 's~(#include "viewer_common\.hpp"\r\n)~$1#include "viewer_frame_gate_state.hpp"\r\n~ or die "globals include"' "$GH"
  add_include viewer_frame_gate_state.hpp
  bash automation/viewer_split_gate.sh --e2e
  commit_step 1-12 "FrameGateState gate — congestion / stale-anchor / keyframe-wait state and its env config become one struct" \
    "The 33 recv-lambda locals of the frame gate and main()'s four env-derived thresholds become \`FrameGateState gate\` (declared in main() before the thresholds are read; frameIntervalUs assigned at thread start where the local was); ClientCongestionState moves to the header verbatim; uses renamed gate.<member> (code-only)." \
    "$M" "$GH" $S/viewer_frame_gate_state.hpp
fi

# ================= 1-13 DecoderState dec =================
if [ "$FROM" -le 13 ]; then
  rep_line "  const Args args = parse_args(argc, argv);" "  const Args args = parse_args(argc, argv);
  DecoderState dec;"
  rep_line '  const bool useRaw = (args.codec == "raw");' '  dec.useRaw = (args.codec == "raw");'
  rep_line '  const bool useH264 = (args.codec == "h264");' '  dec.useH264 = (args.codec == "h264");'
  del_line "  VideoTransport transport = VideoTransport::Tcp;"
  del_line "  bool mfStarted = false;"
  del_line "  H264Decoder decoder;"
  del_line "  bool decoderReady = false;"
  rep_line "  bool waitForKeyFrame = useH264;" "  dec.waitForKeyFrame = dec.useH264;"
  del_line "  uint32_t decoderW = 0;"
  del_line "  uint32_t decoderH = 0;"
  del_line "  Microsoft::WRL::ComPtr<ID3D11Device> decD3dDevice;"
  del_line "  Microsoft::WRL::ComPtr<ID3D11DeviceContext> decD3dContext;"
  perl -0pi -e 's~^    uint64_t recvSelectionEpoch = (gSel\.epoch|gSelectionEpoch)\.load\(std::memory_order_acquire\);\r\n~    dec.recvSelectionEpoch = $1.load(std::memory_order_acquire);\r\n~m or die "recvSelectionEpoch"' "$M"
  perl -0pi -e 's~    // Which selection generation this loop has already reset the decoder for\. A bump by\r\n    // begin_pc_target_selection\(\) on the UI thread makes the next frame flush stale references\.\r\n~~ or die "c-epoch"' "$M"
  mapfile_from $T/map13 <<'EOF'
useRaw dec.useRaw
useH264 dec.useH264
transport dec.transport
mfStarted dec.mfStarted
decoder dec.decoder
decoderReady dec.decoderReady
waitForKeyFrame dec.waitForKeyFrame
decoderW dec.decoderW
decoderH dec.decoderH
decD3dDevice dec.d3dDevice
decD3dContext dec.d3dContext
recvSelectionEpoch dec.recvSelectionEpoch
EOF
  main_rename $T/map13
  verify_gone $T/map13 "$M"
  add_include viewer_decoder_state.hpp
  bash automation/viewer_split_gate.sh --e2e
  commit_step 1-13 "DecoderState dec — codec/transport choice, the H264Decoder and its device become one struct" \
    "main()'s useRaw / useH264 / transport / mfStarted / decoder / decoderReady / waitForKeyFrame / decoderW / decoderH / decD3dDevice / decD3dContext and the recv lambda's recvSelectionEpoch become \`DecoderState dec\` declared right after parse_args (same initial values; waitForKeyFrame = useH264 and recvSelectionEpoch assigned where the locals were); uses renamed dec.<member> (code-only)." \
    "$M" $S/viewer_decoder_state.hpp
fi
echo "Phase 1-11..1-13 done; main.cpp $(wc -l < "$M") lines"

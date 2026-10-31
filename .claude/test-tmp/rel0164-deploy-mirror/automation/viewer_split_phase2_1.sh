#!/usr/bin/env bash
# Viewer split refactor Phase 2-1: the recv thread lambda of main() becomes class VideoReceiver
# (viewer_video_receiver.hpp/.cpp). The eight helper lambdas and process_h264_frame become member
# functions with the same names and verbatim bodies (re-indented by -2); the UDP / TCP loops become
# run_udp() / run_tcp(); Run() is the lambda prologue followed by the same transport branch. The
# lambda's captured state (RecvStats st, DecoderState& dec, FrameGateState& gate, const Args& args,
# startUs, udpSimDropPm, udpSimDropSeed) are members with the same names, so every body reads the
# same. main() keeps `std::thread recvThread([&]() { receiver.Run(); })`. Gate B: every lambda body
# and both loops are found verbatim (indentation aside) in the new .cpp. Run from the repo root.
set -euo pipefail
S=apps/native_poc/src
M=$S/native_video_client_main.cpp
C=apps/native_poc/CMakeLists.txt
H=$S/viewer_video_receiver.hpp
CPP=$S/viewer_video_receiver.cpp
git diff --quiet HEAD || { echo "tree not clean"; exit 1; }
T=/tmp/v21; rm -rf $T; mkdir -p $T
tr -d '\r' < "$M" > $T/main.txt
O=$T/main.txt
lineof() { local n; n=$(grep -n -- "$1" $O | cut -d: -f1 | head -1); [ -n "$n" ] || { echo "anchor not found: $1"; exit 1; }; echo "$n"; }
A=$(lineof '^  std::thread recvThread(\[&\]() {$')
Z=$(awk -v s="$A" 'NR>s && /^  \}\);$/ {print NR; exit}' $O)
echo "recv lambda: $A..$Z"
# prologue: lines after the lambda header up to the first helper lambda
P1=$((A + 1)); P2=$(($(lineof '^    auto queue_depth_frames = \[&\]') - 1))
UDP_A=$(lineof '^    if (dec.transport == VideoTransport::Udp) {$'); UDP_Z=$(awk -v s="$UDP_A" 'NR>s && /^    \}$/ {print NR; exit}' $O)
TCP_A=$((UDP_Z + 2)); TCP_Z=$((Z - 1))
[ "$(sed -n "${TCP_A}p" $O)" = '    while (gSession.running.load()) {' ] || { echo "tcp loop anchor: $(sed -n "${TCP_A}p" $O)"; exit 1; }
LPC=$(lineof '^    st.lastPresentCounters = load_present_counters();$')
echo "prologue $P1..$P2, udp $UDP_A..$UDP_Z, tcp $TCP_A..$TCP_Z, lastPresentCounters line $LPC"

# ---- lambdas -> members ----
# emits "RET VideoReceiver::NAME(PARAMS) {" + body (de-indented by 2) + "}" ; records RANGES of the body
RANGES=""
DECLS=""
emit_member() {  # $1 name
  local name=$1 s e k sig ret params
  s=$(lineof "^    auto $name = \[&\]")
  e=$(awk -v s="$s" 'NR>s && /^    \};$/ {print NR; exit}' $O)
  # signature may span lines until the one ending with "{"
  k=$s; sig=""
  while :; do sig="$sig$(sed -n "${k}p" $O)"$'\n'; grep -q '{$' <(sed -n "${k}p" $O) && break; k=$((k + 1)); done
  sig=$(printf '%s' "$sig" | perl -0pe 's/\n$//')
  # parse: "    auto NAME = [&](PARAMS) -> RET {"  or without "-> RET"
  ret=$(printf '%s' "$sig" | perl -0ne 'if (/\)\s*->\s*([^{]+?)\s*\{$/s) { print $1 } else { print "void" }')
  params=$(printf '%s' "$sig" | perl -0ne 's/^    auto \w+ = \[&\]\(//s; s/\)\s*(->\s*[^{]+?)?\s*\{$//s; s/\n\s+/\n    /g; print')
  {
    echo "$ret VideoReceiver::$name($params) {"
    sed -n "$((k + 1)),$((e - 1))p" $O | sed 's/^  //'
    echo "}"
    echo
  } >> $T/members.txt
  RANGES="$RANGES,$((k + 1))-$((e - 1))"
  DECLS="$DECLS  $ret $name($(printf '%s' "$params" | perl -0pe 's/\n\s+/ /g'));"$'\n'
}
: > $T/members.txt
for fn in queue_depth_frames sample_queue_depth transition_congestion_state load_present_counters append_present_counter_fields append_congestion_fields aligned_lag_us publish_metrics process_h264_frame; do
  emit_member $fn
done
# ---- loops ----
{
  echo "void VideoReceiver::run_udp() {"
  sed -n "$((UDP_A + 1)),$((UDP_Z - 1))p" $O | sed 's/^    //'
  echo "}"
  echo
  echo "void VideoReceiver::run_tcp() {"
  sed -n "${TCP_A},${TCP_Z}p" $O | sed 's/^  //'
  echo "}"
  echo
  echo "void VideoReceiver::Run() {"
  # the lambda's local `RecvStats st;` is the member now -- it must not be redeclared (it would shadow it)
  sed -n "${P1},${P2}p" $O | grep -v '^    RecvStats st;$' | sed 's/^  //'
  echo "  st.lastPresentCounters = load_present_counters();"
  echo "  if (dec.transport == VideoTransport::Udp) {"
  echo "    run_udp();"
  echo "    return;"
  echo "  }"
  echo "  run_tcp();"
  echo "}"
} >> $T/members.txt
RANGES="$RANGES,$((UDP_A + 1))-$((UDP_Z - 1)),${TCP_A}-${TCP_Z}"
grep -q '^    RecvStats st;$' <(sed -n "${P1},${P2}p" $O) || { echo "prologue lost its RecvStats line?"; exit 1; }
RANGES=${RANGES#,}

# ---- header ----
cat > $T/hpp.txt <<EOF
#pragma once

// The viewer's video receive thread: reads the media socket, reassembles / gates / decodes frames
// and publishes them to the FrameBuffer.
//
// Role:    Run() is the former recvThread lambda of main(): the UDP datagram loop (control tunnel
//          tick, cursor packets, FEC assembly, sim drop) or the TCP message loop (raw BGRA / H.264),
//          process_h264_frame (selection gate, stale / congestion / keyframe-wait gating, decode,
//          publish, trace) and the once-a-second stats line.
// Thread:  recv only. Owns RecvStats; writes FrameBuffer under its mutex, the remote cursor sample,
//          the client metrics; reads the selection gate, picker visibility and present counters.
// Input:   the media socket (gSession.sock), DecoderState, FrameGateState, Args.
// Output:  FrameBuffer frames, metrics, stats/telemetry lines, keyframe requests.
// Callers: main() (std::thread recvThread([&]{ receiver.Run(); })).
//
// Bodies are the lambda bodies of native_video_client_main.cpp, verbatim (viewer split refactor
// Phase 2-1); the captured state became members with the same names so the code reads unchanged.

#include "viewer_args.hpp"
#include "viewer_common.hpp"
#include "viewer_decoder_state.hpp"
#include "viewer_frame_gate_state.hpp"
#include "viewer_globals.hpp"
#include "viewer_recv_stats.hpp"

namespace remote60::native_poc::viewer {

class VideoReceiver {
 public:
  VideoReceiver(const Args& args, DecoderState& dec, FrameGateState& gate, uint64_t startUs,
                uint32_t udpSimDropPm, uint32_t udpSimDropSeed)
      : args(args), dec(dec), gate(gate), startUs(startUs), udpSimDropPm(udpSimDropPm),
        udpSimDropSeed(udpSimDropSeed) {}
  // The thread body (formerly the recvThread lambda).
  void Run();

 private:
  // captured by reference in the monolith; same names so the bodies read unchanged
  const Args& args;
  DecoderState& dec;
  FrameGateState& gate;
  const uint64_t startUs;
  const uint32_t udpSimDropPm;
  const uint32_t udpSimDropSeed;
  RecvStats st;

  // the helper lambdas, now members (verbatim bodies)
$DECLS  void run_udp();
  void run_tcp();
};

}  // namespace remote60::native_poc::viewer
EOF
cat > $T/cpp_head.txt <<'EOF'
// See viewer_video_receiver.hpp. Bodies are the recvThread lambda of native_video_client_main.cpp,
// verbatim (viewer split refactor Phase 2-1).

#include "viewer_video_receiver.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>

#include "viewer_decoder_backend.hpp"
#include "viewer_env_util.hpp"
#include "viewer_log.hpp"
#include "viewer_overlay_draw.hpp"
#include "viewer_picker.hpp"

namespace remote60::native_poc::viewer {

EOF
{ cat $T/cpp_head.txt; cat $T/members.txt; echo; echo "}  // namespace remote60::native_poc::viewer"; } > $T/cpp.txt
sed 's/$/\r/' $T/hpp.txt > "$H"
sed 's/$/\r/' $T/cpp.txt > "$CPP"

# ---- main.cpp: the lambda becomes the receiver + a thread that runs it ----
{
  sed -n "1,$((A - 1))p" $O
  echo "  VideoReceiver receiver(args, dec, gate, startUs, udpSimDropPm, udpSimDropSeed);"
  echo "  std::thread recvThread([&]() { receiver.Run(); });"
  sed -n "$((Z + 1)),\$p" $O
} | sed 's/$/\r/' > "$M"
perl -0pi -e 's/((?:#include "viewer_[a-z_0-9]+\.hpp"\r\n)+)/$1#include "viewer_video_receiver.hpp"\r\n/ or die "include anchor"' "$M"
perl -0pi -e 's/(  src\/viewer_globals\.cpp\r?\n)/$1  src\/viewer_video_receiver.cpp\r\n/ or die "cmake anchor"' "$C"

echo "=== gate B (bodies verbatim, indentation aside) ==="
perl automation/viewer_split_check.pl --ignore-indent HEAD "$RANGES" "$CPP"
bash automation/viewer_split_gate.sh --e2e
git add "$M" "$C" "$H" "$CPP"
git commit -q -F - <<EOF
refactor(viewer): Phase 2-1 — the recv thread lambda becomes class VideoReceiver (verbatim bodies)

main()'s 1,240-line recvThread lambda is now VideoReceiver::Run() in viewer_video_receiver.cpp:
the eight helper lambdas and process_h264_frame are members with the same names and verbatim
bodies (source ranges $RANGES of the previous revision, re-indented), the UDP/TCP loops are
run_udp()/run_tcp(), and the captured state (RecvStats st, DecoderState& dec, FrameGateState& gate,
const Args& args, startUs, udpSimDropPm, udpSimDropSeed) are members named as before. main()
constructs the receiver and runs it on the same std::thread.
Gates: move check PASS, build exit 0, viewer e2e ALL PASS.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
git log -1 --format='%h %s'
echo "main.cpp now $(wc -l < "$M") lines; receiver.cpp $(wc -l < "$CPP")"

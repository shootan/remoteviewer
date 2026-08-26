#!/usr/bin/env bash
# Host split refactor Phase 3.5b: bring the two remaining >800-line stage files under the target.
#  - host_stage_encode_send.cpp: the raw and H.264 arms of `if (useRaw) { ... } else { ... }` become
#    encode_send_raw / encode_send_h264 (new file host_stage_encode_send_h264.cpp for the latter).
#  - host_stage_stats.cpp: the H.264 arm of the 1s tick (`} else {` block that prints the stats line
#    and runs the ABR / M9 decisions) becomes stats_tick_h264 in host_stage_stats_h264.cpp; it reads
#    seven const locals of the tick, passed by value / const ref.
# Bodies verbatim; alias preludes recomputed per function. Run once from the repo root.
set -euo pipefail
S=apps/native_poc/src
E=$S/host_stage_encode_send.cpp
T=$S/host_stage_stats.cpp
C=apps/native_poc/CMakeLists.txt
cp "$E" /tmp/encode_send_before.cpp; cp "$T" /tmp/stats_before.cpp
HN="args useH264 useRaw transport stop guardStaleEncoded guardStalePreEncode paceByTick startUs nextTickUs captureWindowRebindIntervalUs nextCaptureWindowCheckUs streamActiveApplied streamActiveSinceUs poppedNv12Slot poppedNv12Generation powerKeepalive item token windowSelectionTxn frameGating rate kick clientMetrics backend watchdog inputRouter sender clientSession encoder stats capture res lastUserFeedbackUs"
TCN="nowUs tickWaitUs payload seq w h stride streamGeneration captureUs callbackUs queuePushUs callbackIntervalUs captureIntervalUs captureClockSkewUs captureAgeAtCallbackUs captureD3DWaitUs captureCopyMapUs captureMemcpyUs captureUnmapWaitUs captureUnmapUs version nv12Slot nv12Generation nv12W nv12H queueWaitReason queueSelectStartUs servedBootstrap kickForcedKey queueWaitUs queueGapFrames queueDepthAtPop captureStampUs sendFailed queuePopUs queueSelectWaitUs frameAgeAtSelectUs captureToCallbackUs captureToQueueUs"
aliases() {  # aliases BODYFILE -> alias lines for the names used
  for n in $HN; do grep -q -E "(^|[^.A-Za-z0-9_>])$n\b" "$1" && echo "  auto& $n = hx.$n;"; done
  for n in $TCN; do grep -q -E "(^|[^.A-Za-z0-9_>])$n\b" "$1" && echo "  auto& $n = tc.$n;"; done
  return 0
}
preamble() { local ns; ns=$(grep -n -m1 '^namespace remote60::native_poc {$' "$1" | cut -d: -f1); sed -n "1,$((ns - 1))p" "$1" | grep -v -E '^// (Stage 1[12]:|Host split refactor Phase 3\.5:|own; see host_main_loop|$)'; }

# ---------------- encode_send: raw / h264 arms ----------------
FS=$(grep -n -m1 '^Flow stage_encode_send(HostContext& hx, TickContext& tc) {$' "$E" | cut -d: -f1)
IFL=$(awk -v s="$FS" 'NR>s && /^  if \(useRaw\) \{$/ {print NR; exit}' "$E")
IFE=$(perl automation/block_end.pl "$IFL" < "$E")
ELSE=$(for n in $(awk -v a="$IFL" -v b="$IFE" 'NR>a && NR<b && /^ *\} else \{$/ {print NR}' "$E"); do d=$(perl automation/loop_depth_at.pl "$IFL" "$IFE" "$n" < "$E" | sed -E 's/.*d=([0-9]+).*/\1/'); [ "$d" = 1 ] && { echo "$n"; break; }; done)
echo "encode_send fn $FS, if $IFL, else $ELSE, end $IFE"
[ -n "$ELSE" ] || { echo "no else arm"; exit 1; }
sed -n "$((IFL + 1)),$((ELSE - 1))p" "$E" | sed -E 's/^  //' > /tmp/arm_raw.txt
sed -n "$((ELSE + 1)),$((IFE - 1))p" "$E" | sed -E 's/^  //' > /tmp/arm_h264.txt
preamble "$E" > /tmp/pre_e.txt
{
  echo '// Stage 11 (raw path): copy the popped frame into a raw frame message and send it over TCP.'
  echo '//'
  echo '// Host split refactor Phase 3.5b: the raw arm of the former stage_encode_send if/else, verbatim;'
  echo '// see host_main_loop.hpp for the loop, HostContext / TickContext and Flow.'
  echo
  cat /tmp/pre_e.txt
  echo 'namespace remote60::native_poc {'
  echo
  echo 'Flow encode_send_raw(HostContext& hx, TickContext& tc) {'
  aliases /tmp/arm_raw.txt
  cat /tmp/arm_raw.txt
  echo '  return Flow::Next;'
  echo '}'
  echo
  echo '// Stage 11 dispatcher: raw or H.264 path.'
  echo 'Flow stage_encode_send(HostContext& hx, TickContext& tc) {'
  echo '  if (hx.useRaw) return encode_send_raw(hx, tc);'
  echo '  return encode_send_h264(hx, tc);'
  echo '}'
  echo
  echo '}  // namespace remote60::native_poc'
} > /tmp/new_encode_send.cpp
{
  echo '// Stage 11 (H.264 path): refit debounce, force-key latch, MFT encode, starvation heartbeat, AU'
  echo '// loop into the sender queue (UDP) or straight onto the TCP socket.'
  echo '//'
  echo '// Host split refactor Phase 3.5b: the H.264 arm of the former stage_encode_send if/else, verbatim;'
  echo '// see host_main_loop.hpp for the loop, HostContext / TickContext and Flow.'
  echo
  cat /tmp/pre_e.txt
  echo 'namespace remote60::native_poc {'
  echo
  echo 'Flow encode_send_h264(HostContext& hx, TickContext& tc) {'
  aliases /tmp/arm_h264.txt
  cat /tmp/arm_h264.txt
  echo '  return Flow::Next;'
  echo '}'
  echo
  echo '}  // namespace remote60::native_poc'
} > $S/host_stage_encode_send_h264.cpp
mv /tmp/new_encode_send.cpp "$E"

# ---------------- stats: h264 arm of the 1s tick ----------------
FS2=$(grep -n -m1 '^Flow stage_stats(HostContext& hx, TickContext& tc) {$' "$T" | cut -d: -f1)
RAWL=$(awk -v s="$FS2" 'NR>s && /^    if \(useRaw\) \{$/ {print NR; exit}' "$T")
RAWE=$(perl automation/block_end.pl "$RAWL" < "$T")           # closes the whole if/else
ELSE2=$(for n in $(awk -v a="$RAWL" -v b="$RAWE" 'NR>a && NR<b && /^ *\} else \{$/ {print NR}' "$T"); do d=$(perl automation/loop_depth_at.pl "$RAWL" "$RAWE" "$n" < "$T" | sed -E 's/.*d=([0-9]+).*/\1/'); [ "$d" = 1 ] && { echo "$n"; break; }; done)
echo "stats fn $FS2, if(useRaw) $RAWL, else $ELSE2, end $RAWE"
[ -n "$ELSE2" ] || { echo "no else arm in stats"; exit 1; }
sed -n "$((ELSE2 + 1)),$((RAWE - 1))p" "$T" | sed -E 's/^    //' > /tmp/arm_stats.txt
preamble "$T" > /tmp/pre_t.txt
{
  echo '// Stage 12 (H.264 arm of the 1s tick): the stats line, encoder-starvation summary, ABR profile and'
  echo '// M9 level decisions.'
  echo '//'
  echo '// Host split refactor Phase 3.5b: the `} else {` arm of the former stage_stats tick, verbatim; the'
  echo '// seven per-tick values it reads are passed in. See host_main_loop.hpp for HostContext / Flow.'
  echo
  cat /tmp/pre_t.txt
  echo 'namespace remote60::native_poc {'
  echo
  echo 'Flow stats_tick_h264(HostContext& hx, TickContext& tc, uint64_t t, bool statsPrintDue, double mbps,'
  echo '                     const std::string& targetProcessName, uint64_t queuePushPerSec,'
  echo '                     uint64_t callbackFramesPerSec, uint64_t idleHoldPerSec) {'
  aliases /tmp/arm_stats.txt
  cat /tmp/arm_stats.txt
  echo '  return Flow::Next;'
  echo '}'
  echo
  echo '}  // namespace remote60::native_poc'
} > $S/host_stage_stats_h264.cpp
# replace the else arm in place with the call
{
  sed -n "1,${ELSE2}p" "$T"
  echo '      const Flow h264Flow = stats_tick_h264(hx, tc, t, statsPrintDue, mbps, targetProcessName,'
  echo '                                            queuePushPerSec, callbackFramesPerSec, idleHoldPerSec);'
  echo '      if (h264Flow != Flow::Next) return h264Flow;'
  sed -n "${RAWE},\$p" "$T"
} > /tmp/new_stats.cpp
mv /tmp/new_stats.cpp "$T"

# declarations in host_main_loop.hpp
sed -i 's|^Flow stage_encode_send(HostContext& hx, TickContext& tc);   // raw send / H.264 encode + enqueue$|Flow stage_encode_send(HostContext\& hx, TickContext\& tc);   // raw send / H.264 encode + enqueue (dispatcher)\nFlow encode_send_raw(HostContext\& hx, TickContext\& tc);\nFlow encode_send_h264(HostContext\& hx, TickContext\& tc);|' $S/host_main_loop.hpp
sed -i 's|^Flow stage_stats(HostContext& hx, TickContext& tc);         // 1s stats tick, drain watchdog, ABR/M9$|Flow stage_stats(HostContext\& hx, TickContext\& tc);         // 1s stats tick, drain watchdog, ABR/M9\nFlow stats_tick_h264(HostContext\& hx, TickContext\& tc, uint64_t t, bool statsPrintDue, double mbps,\n                     const std::string\& targetProcessName, uint64_t queuePushPerSec,\n                     uint64_t callbackFramesPerSec, uint64_t idleHoldPerSec);|' $S/host_main_loop.hpp
sed -i 's|^  src/host_stage_encode_send.cpp$|  src/host_stage_encode_send.cpp\n  src/host_stage_encode_send_h264.cpp|; s|^  src/host_stage_stats.cpp$|  src/host_stage_stats.cpp\n  src/host_stage_stats_h264.cpp|' "$C"

# verification: arm text unchanged (modulo alias prelude)
awk '/^Flow encode_send_raw\(/ {on=1; next} on && /^  auto& / {next} on && /^  return Flow::Next;$/ {exit} on' "$E" | diff - /tmp/arm_raw.txt && echo "raw arm IDENTICAL"
awk '/^Flow encode_send_h264\(/ {on=1; next} on && /^  auto& / {next} on && /^  return Flow::Next;$/ {exit} on' $S/host_stage_encode_send_h264.cpp | diff - /tmp/arm_h264.txt && echo "h264 arm IDENTICAL"
awk '/^Flow stats_tick_h264\(/ {on=1} on && /^  auto& / {next} on && /^  return Flow::Next;$/ {exit} on && !/^Flow stats_tick_h264\(|^                     / {print}' $S/host_stage_stats_h264.cpp | diff - /tmp/arm_stats.txt && echo "stats arm IDENTICAL"
wc -l "$E" $S/host_stage_encode_send_h264.cpp "$T" $S/host_stage_stats_h264.cpp

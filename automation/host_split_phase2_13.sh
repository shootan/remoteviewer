#!/usr/bin/env bash
# Host split refactor Phase 2-13: bring host_stage_encode_send_h264.cpp (989 lines) under the 800-line
# target by moving the per-access-unit body of its `for (const auto& au : units)` loop (491 lines) into
# encode_send_h264_emit_au() in host_stage_encode_send_h264_au.cpp, verbatim except:
#  - the loop's own `continue;` / `break;` (10 sites, all at loop level -- verified with loop_exits.pl;
#    the body has no `return`) become `return AuFlow::Continue;` / `return AuFlow::Break;`
#  - the body is de-indented by the loop's 4 columns
#  - the 14 function-level locals the body reads are passed as reference members of H264AuBatch (built
#    once before the loop, like HostContext); kSenderQueueMaxFrames (a constexpr local used only by
#    the body) moves to the new header host_stage_encode_send_h264.hpp.
# Run once from the repo root (host_stage_encode_send_h264.cpp = 989 lines, loop at 487..977).
set -euo pipefail
S=apps/native_poc/src
E=$S/host_stage_encode_send_h264.cpp
N=$S/host_stage_encode_send_h264_au.cpp
H=$S/host_stage_encode_send_h264.hpp
C=apps/native_poc/CMakeLists.txt
git diff --quiet HEAD -- "$E" "$C" || { echo "tree not clean"; exit 1; }
[ "$(wc -l < "$E")" = 989 ] || { echo "unexpected length $(wc -l < "$E")"; exit 1; }
cp "$E" /tmp/esh_before.cpp
O=/tmp/esh_before.cpp
B=/tmp/p213; rm -rf $B; mkdir -p $B
# anchors
[ "$(sed -n '487p' $O)" = '    for (const auto& au : units) {' ] || { echo "loop header moved"; exit 1; }
[ "$(sed -n '977p' $O)" = '  }' ] || { echo "loop end moved"; exit 1; }
[ "$(sed -n '480p' $O)" = '    constexpr size_t kSenderQueueMaxFrames = 6;' ] || { echo "constexpr moved"; exit 1; }
for n in 488 549 561 574 742; do sed -n "${n}p" $O | grep -q -E '\bcontinue;' || { echo "no continue at $n"; exit 1; }; done
for n in 537 547 727 731 734; do sed -n "${n}p" $O | grep -q -E '^\s*break;$' || { echo "no break at $n"; exit 1; }; done
echo "loop-level exits (must be exactly the 10 rewritten below):"; perl automation/loop_exits.pl 487 977 < $O | awk -F: '{print $1}' | tr '\n' ' '; echo

# ---------------- the body: rewrite the 10 exits, de-indent 4 ----------------
sed -e '488s/\bcontinue;/return AuFlow::Continue;/' -e '549s/\bcontinue;/return AuFlow::Continue;/' \
    -e '561s/\bcontinue;/return AuFlow::Continue;/' -e '574s/\bcontinue;/return AuFlow::Continue;/' \
    -e '742s/\bcontinue;/return AuFlow::Continue;/' \
    -e '537s/\bbreak;/return AuFlow::Break;/' -e '547s/\bbreak;/return AuFlow::Break;/' \
    -e '727s/\bbreak;/return AuFlow::Break;/' -e '731s/\bbreak;/return AuFlow::Break;/' \
    -e '734s/\bbreak;/return AuFlow::Break;/' $O | sed -n '488,976p' | sed -E 's/^    //' > $B/body.txt
[ "$(grep -c 'return AuFlow::' $B/body.txt)" = 10 ] || { echo "exit rewrite count != 10"; exit 1; }
awk 'NR>=488 && NR<=976 && !/^    / && !/^$/' $O | grep -q . && { echo "body line with indent < 4"; exit 1; } || true

# ---------------- aliases ----------------
HN="args useH264 useRaw transport stop guardStaleEncoded guardStalePreEncode paceByTick startUs nextTickUs captureWindowRebindIntervalUs nextCaptureWindowCheckUs streamActiveApplied streamActiveSinceUs poppedNv12Slot poppedNv12Generation powerKeepalive item token windowSelectionTxn frameGating rate kick clientMetrics backend watchdog inputRouter sender clientSession encoder stats capture res lastUserFeedbackUs"
TCN="nowUs tickWaitUs payload seq w h stride streamGeneration captureUs callbackUs queuePushUs callbackIntervalUs captureIntervalUs captureClockSkewUs captureAgeAtCallbackUs captureD3DWaitUs captureCopyMapUs captureMemcpyUs captureUnmapWaitUs captureUnmapUs version nv12Slot nv12Generation nv12W nv12H queueWaitReason queueSelectStartUs servedBootstrap kickForcedKey queueWaitUs queueGapFrames queueDepthAtPop captureStampUs sendFailed queuePopUs queueSelectWaitUs frameAgeAtSelectUs captureToCallbackUs captureToQueueUs"
BN="scaleReadbackTiming preEncodePrepUs scaleUs nv12Us encodeStartUs encodeInputUs queueToEncodeUs callbackToEncodeStartUs encodeStats encodeEndUs encoderResetTriggered sessionReconnectTriggered countedRawForInput senderBacklogged"
{
  for n in $HN; do grep -q -E "(^|[^.A-Za-z0-9_>])$n\b" $B/body.txt && echo "  auto& $n = hx.$n;"; done
  for n in $TCN; do grep -q -E "(^|[^.A-Za-z0-9_>])$n\b" $B/body.txt && echo "  auto& $n = tc.$n;"; done
  for n in $BN; do grep -q -E "(^|[^.A-Za-z0-9_>])$n\b" $B/body.txt && echo "  auto& $n = b.$n;"; done
  true
} > $B/aliases.txt
[ "$(grep -c ' = b\.' $B/aliases.txt)" = 14 ] || { echo "batch alias count != 14: $(grep -c ' = b\.' $B/aliases.txt)"; exit 1; }

# ---------------- header ----------------
cat > "$H" <<'EOF'
#pragma once

// Stage 11 (H.264 path) internals shared by host_stage_encode_send_h264.cpp and its per-access-unit
// half host_stage_encode_send_h264_au.cpp.
//
// Role:    H264AuBatch is the set of encode_send_h264() locals the AU loop body reads, bound by
//          reference once per encoded frame (same pattern as HostContext); AuFlow is what the body
//          reports where it used to `continue` / `break` the loop.
// Thread:  main encode loop only.
// Callers: encode_send_h264() (loop) -> encode_send_h264_emit_au() (one access unit).
//
// Host split refactor Phase 2-13: the loop body moved verbatim out of encode_send_h264().

#include <cstddef>
#include <cstdint>

#include "host_bottleneck.hpp"
#include "host_main_loop.hpp"
#include "mf_h264_codec.hpp"

namespace remote60::native_poc {

// What one AU-loop iteration asks the loop to do next.
enum class AuFlow { Next, Continue, Break };

// Absolute cap on the sender queue (frames); see the backlog comment above the loop in
// encode_send_h264().
constexpr size_t kSenderQueueMaxFrames = 6;

// encode_send_h264() locals the AU body reads / updates, in their declaration order.
struct H264AuBatch {
  D3DReadbackTiming& scaleReadbackTiming;
  uint64_t& preEncodePrepUs;
  uint64_t& scaleUs;
  uint64_t& nv12Us;
  const uint64_t& encodeStartUs;
  const uint64_t& encodeInputUs;
  const uint64_t& queueToEncodeUs;
  const uint64_t& callbackToEncodeStartUs;
  H264EncodeFrameStats& encodeStats;
  const uint64_t& encodeEndUs;
  bool& encoderResetTriggered;
  bool& sessionReconnectTriggered;
  bool& countedRawForInput;
  const bool& senderBacklogged;
};

// One access unit: timestamps / kick cancel, key-frame bookkeeping, sender-queue policy (UDP) or the
// direct TCP send, per-AU telemetry.
AuFlow encode_send_h264_emit_au(HostContext& hx, TickContext& tc, H264AuBatch& b, const H264AccessUnit& au);

}  // namespace remote60::native_poc
EOF

# ---------------- new TU ----------------
NS=$(grep -n -m1 '^namespace remote60::native_poc {$' $O | cut -d: -f1)
{
  echo '// Stage 11 (H.264 path), per access unit: AU timestamp / trailing-kick cancel, key-frame bookkeeping,'
  echo '// sender-queue policy + enqueue (UDP) or the direct TCP send, per-AU telemetry.'
  echo '//'
  echo '// Host split refactor Phase 2-13: the body of the `for (const auto& au : units)` loop of'
  echo '// encode_send_h264(), verbatim (its continue / break became AuFlow); see host_stage_encode_send_h264.hpp.'
  echo
  sed -n "1,$((NS - 1))p" $O | awk '/^#include <winsock2.h>$/ {on=1} on' | sed 's|^#include "host_main_loop.hpp"$|#include "host_main_loop.hpp"\n#include "host_stage_encode_send_h264.hpp"|'
  echo 'namespace remote60::native_poc {'
  echo
  echo 'AuFlow encode_send_h264_emit_au(HostContext& hx, TickContext& tc, H264AuBatch& b, const H264AccessUnit& au) {'
  cat $B/aliases.txt
  cat $B/body.txt
  echo '  return AuFlow::Next;'
  echo '}'
  echo
  echo '}  // namespace remote60::native_poc'
} > "$N"
grep -q '^#include "host_stage_encode_send_h264.hpp"$' "$N" || { echo "new TU lacks the header include"; exit 1; }

# ---------------- parent: drop the constexpr, replace the loop ----------------
{
  sed -n '1,479p' $O | sed 's|^#include "host_main_loop.hpp"$|#include "host_main_loop.hpp"\n#include "host_stage_encode_send_h264.hpp"|'
  sed -n '481,486p' $O
  echo '    H264AuBatch b{scaleReadbackTiming, preEncodePrepUs, scaleUs, nv12Us, encodeStartUs, encodeInputUs,'
  echo '                  queueToEncodeUs, callbackToEncodeStartUs, encodeStats, encodeEndUs, encoderResetTriggered,'
  echo '                  sessionReconnectTriggered, countedRawForInput, senderBacklogged};'
  echo '    for (const auto& au : units) {'
  echo '      const AuFlow f = encode_send_h264_emit_au(hx, tc, b, au);'
  echo '      if (f == AuFlow::Continue) continue;'
  echo '      if (f == AuFlow::Break) break;'
  echo '    }'
  sed -n '978,$p' $O
} > $B/parent.cpp
mv $B/parent.cpp "$E"
grep -q '^#include "host_stage_encode_send_h264.hpp"$' "$E" || { echo "parent lacks the header include"; exit 1; }
sed -i 's|^  src/host_stage_encode_send_h264.cpp$|  src/host_stage_encode_send_h264.cpp\n  src/host_stage_encode_send_h264_au.cpp|' "$C"
grep -q 'src/host_stage_encode_send_h264_au.cpp' "$C" || { echo "CMake edit failed"; exit 1; }

# ---------------- verification ----------------
echo "== body vs original loop body (expected: only the 10 exit rewrites):"
diff <(sed -n '488,976p' $O | sed -E 's/^    //') <(awk '/^AuFlow encode_send_h264_emit_au\(/ {on=1; next} on && /^  auto& / {next} on && /^  return AuFlow::Next;$/ {exit} on' "$N") | grep -E '^[<>]' | sed -E 's/^(.{0,100}).*/\1/'
echo "== parent outside 480..977 unchanged:"
diff <(sed -n '1,479p' $O; sed -n '978,$p' $O) <(grep -v '^#include "host_stage_encode_send_h264.hpp"$' "$E" | awk '/^    H264AuBatch b\{/ {skip=1} skip && /^    \}$/ {skip=0; next} !skip' | awk 'NR==1 || !(/^    size_t senderBacklogBeforeBatch = 0;$/ && seen++) ' ) | grep -c '^[<>]' || true
grep -o '"\([^"\\]\|\\.\)*"' $O | sort -u > $B/lit_before.txt
cat "$E" "$N" "$H" | grep -o '"\([^"\\]\|\\.\)*"' | sort -u > $B/lit_after.txt
echo "== literals only in OLD (must be empty):"; comm -23 $B/lit_before.txt $B/lit_after.txt
echo "== literals new (header names / new comments only):"; comm -13 $B/lit_before.txt $B/lit_after.txt
wc -l "$E" "$N" "$H"

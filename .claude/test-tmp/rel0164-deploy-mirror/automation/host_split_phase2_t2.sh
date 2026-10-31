#!/usr/bin/env bash
# Host split refactor Phase 2-T2 (unit-testable frame gating): the decision lines of stage_gate_static
# (host_stage_gate_static.cpp) become FrameGatingState members in host_frame_gate.hpp:
#   RecordChange(changePermille)   <- lines 106..117 (the estimate stays in the stage; it needs the pixels)
#   RecordReferenceMiss()          <- lines 119..121
#   UpdateMode() -> changed        <- lines 124..134
#   ShouldSkip(queuePopUs, keyReqPending, activeFrameIntervalUs, paceByTick) const <- 145, 152..155
# Bodies verbatim (de-indented where the member body sits shallower); the only rename is
# encoder.activeFrameIntervalUs -> the activeFrameIntervalUs parameter. The stage keeps the change
# estimate call, the mode-change log, the skip bookkeeping and Flow::Continue. Run once from the repo root.
set -euo pipefail
S=apps/native_poc/src
G=$S/host_stage_gate_static.cpp
H=$S/host_frame_gate.hpp
C=apps/native_poc/CMakeLists.txt
git diff --quiet HEAD -- "$G" "$H" "$C" || { echo "tree not clean"; exit 1; }
B=/tmp/p2t2; rm -rf $B; mkdir -p $B
tr -d '\r' < "$G" > $B/gate_before.cpp
tr -d '\r' < "$H" > $B/fg_before.hpp
O=$B/gate_before.cpp
[ "$(wc -l < $O)" = 179 ] || { echo "unexpected stage length $(wc -l < $O)"; exit 1; }
chk() { [ "$(sed -n "$1p" $O)" = "$2" ] || { echo "anchor $1 moved: $(sed -n "$1p" $O)"; exit 1; }; }
chk 106 '      frameGating.changePermilleLast = estimate_bgra_change_permille('
chk 107 '          payload->data(), frameGating.refPayload->data(), payload->size(), frameGating.sampleTarget);'
chk 108 '      frameGating.changePermilleSum += frameGating.changePermilleLast;'
chk 109 '      ++frameGating.changePermilleCount;'
chk 111 '      if (frameGating.changePermilleLast == 0) {'
chk 117 '      }'
chk 118 '    } else {'
chk 119 '      frameGating.staticStreak = 0;'
chk 121 '      frameGating.changePermilleLast = 1000;'
chk 122 '    }'
chk 124 '    const bool prevStaticMode = frameGating.staticMode;'
chk 128 '    const bool motionNow = frameGating.changePermilleLast > 0;'
chk 134 '    }'
chk 135 '    if (prevStaticMode != frameGating.staticMode) {'
chk 142 '    }'
chk 144 '    const bool keyReqPending = clientMetrics.requestedKeyFrame.load(std::memory_order_acquire);'
chk 145 '    const uint64_t targetIntervalUs = frameGating.staticMode ? frameGating.staticIntervalUs : encoder.activeFrameIntervalUs;'
chk 152 '    const bool needsGatingRateLimit = frameGating.staticMode || !paceByTick;'
chk 153 '    if (needsGatingRateLimit && !keyReqPending && !motionNow &&'
chk 154 '        frameGating.lastSentUs > 0 &&'
chk 155 '        queuePopUs < (frameGating.lastSentUs + targetIntervalUs)) {'
chk 156 '      ++frameGating.skipCount;'
chk 159 '      return Flow::Continue;'
chk 160 '    }'

# ---------------- header ----------------
SE=$(grep -n -m1 '^struct FrameGatingState {$' $B/fg_before.hpp | cut -d: -f1)
CE=$(awk -v s="$SE" 'NR>s && /^};$/ {print NR; exit}' $B/fg_before.hpp)
{
  sed -n "1,$((CE - 1))p" $B/fg_before.hpp | sed 's|^#include <cstdint>$|#include <algorithm>\n#include <cstdint>|'
  cat <<'EOF'

  // --- decisions (Phase 2-T2: the former stage_gate_static lines, pure on this state) ---
  // The reference frame matched: record this frame's change estimate and advance the streaks.
  void RecordChange(uint64_t changePermille) {
    FrameGatingState& frameGating = *this;
    frameGating.changePermilleLast = changePermille;
EOF
  sed -n '108,117p' $O | sed -E 's/^  //'
  cat <<'EOF'
  }
  // No usable reference (first frame, size change): the streaks restart and the frame is motion.
  void RecordReferenceMiss() {
    FrameGatingState& frameGating = *this;
EOF
  sed -n '119,121p' $O | sed -E 's/^  //'
  cat <<'EOF'
  }
  // Static <-> motion transition on the streaks; true when the mode changed (the caller logs it).
  bool UpdateMode() {
    FrameGatingState& frameGating = *this;
EOF
  sed -n '124,134p' $O
  cat <<'EOF'
    return prevStaticMode != frameGating.staticMode;
  }
  // Whether this frame is throttled by the gate: only in static mode or on the unpaced path, never
  // a frame that changed or one a key request is waiting on.
  bool ShouldSkip(uint64_t queuePopUs, bool keyReqPending, uint64_t activeFrameIntervalUs,
                  bool paceByTick) const {
    const FrameGatingState& frameGating = *this;
    const bool motionNow = frameGating.changePermilleLast > 0;
EOF
  sed -n '145p' $O | sed 's/encoder\.activeFrameIntervalUs/activeFrameIntervalUs/'
  sed -n '146,152p' $O
  echo '    return needsGatingRateLimit && !keyReqPending && !motionNow &&'
  sed -n '154,154p' $O
  sed -n '155p' $O | sed -E 's/\)\) \{$/);/'
  echo '  }'
  sed -n "${CE},\$p" $B/fg_before.hpp
} > "$H"

# ---------------- stage ----------------
{
  sed -n '1,105p' $O
  echo '      frameGating.RecordChange(estimate_bgra_change_permille('
  sed -n '107p' $O | sed -E 's/\);$/));/'
  echo '    } else {'
  echo '      frameGating.RecordReferenceMiss();'
  echo '    }'
  echo
  echo '    if (frameGating.UpdateMode()) {'
  sed -n '136,144p' $O
  sed -n '146,151p' $O
  echo '    if (frameGating.ShouldSkip(queuePopUs, keyReqPending, encoder.activeFrameIntervalUs, paceByTick)) {'
  sed -n '156,$p' $O
} > "$G"

# ---------------- CMake (the working tree may be CRLF: tolerate a CR before the newline) ----------------
perl -0pi -e 's|(target_include_directories\(remote60_host_abr_test PRIVATE src\)\r?\n)|$1\nadd_executable(remote60_host_frame_gate_test\n  src/host_frame_gate_test.cpp\n)\ntarget_include_directories(remote60_host_frame_gate_test PRIVATE src)\n|' "$C"
grep -q 'remote60_host_frame_gate_test' "$C" || { echo "CMake edit failed"; exit 1; }

# ---------------- verification ----------------
echo "== stage diff:"; (diff $O "$G" || true) | grep -E '^[<>]' | cut -c1-120
grep -o '"\([^"\\]\|\\.\)*"' $O $B/fg_before.hpp | sed 's/^[^:]*://' | sort -u > $B/lit_before.txt
grep -o '"\([^"\\]\|\\.\)*"' "$G" "$H" | sed 's/^[^:]*://' | sort -u > $B/lit_after.txt
echo "== literals only in OLD (must be empty):"; comm -23 $B/lit_before.txt $B/lit_after.txt || true
echo "== literals new (must be empty):"; comm -13 $B/lit_before.txt $B/lit_after.txt || true
wc -l "$G" "$H"

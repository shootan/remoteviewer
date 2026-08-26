#!/usr/bin/env bash
# Host split refactor Phase 2-T1 (unit-testable ABR / M9): the two once-a-second decision blocks of
# stats_tick_h264 (host_stage_stats_h264.cpp) become RateControlState members in host_abr.hpp,
#   DecideAbrProfile(const AbrInputs&, t) / CommitAbrProfile(...)   <- lines 327..461 / 486..492
#   DecideM9Level(const M9Inputs&, t)     / CommitM9Level(...)      <- lines 510..551 / 587..590
# The bodies are the original text except that the nine / eight values the blocks read from outside
# RateControlState (client metrics of this second, host fallback evidence, sender cadence, gating mode,
# encoder.activeFps, startUs) are read from the `in` struct instead -- a mechanical rename verified by
# diff below. The stage computes the same locals, fills the input structs and applies the decision
# exactly as before (encoder.ApplyTarget + logs + forceKeyNext stay in the stage).
# Time is an argument (t), so host_abr_test.cpp can drive whole sessions. Run once from the repo root.
set -euo pipefail
S=apps/native_poc/src
T=$S/host_stage_stats_h264.cpp
H=$S/host_abr.hpp
C=apps/native_poc/CMakeLists.txt
git diff --quiet HEAD -- "$T" "$H" "$C" || { echo "tree not clean"; exit 1; }
B=/tmp/p2t1; rm -rf $B; mkdir -p $B
tr -d '\r' < "$T" > $B/stage_before.cpp
tr -d '\r' < "$H" > $B/abr_before.hpp
O=$B/stage_before.cpp
[ "$(wc -l < $O)" = 596 ] || { echo "unexpected stage length $(wc -l < $O)"; exit 1; }
chk() { [ "$(sed -n "$1p" $O)" = "$2" ] || { echo "anchor $1 moved: $(sed -n "$1p" $O)"; exit 1; }; }
chk 320 '  if (rate.abrEnabled && !encoder.tuneManualOverride && !rate.m9Apply) {'
chk 327 '    const uint32_t abrExpectedFps = std::max<uint32_t>(1, encoder.activeFps);'
chk 406 '    int targetProfile = rate.abrProfile;'
chk 461 '    }'
chk 463 '    if (targetProfile != rate.abrProfile) {'
chk 486 '      rate.encodeLadderReduced = ladderChoice.reduced;'
chk 492 '      rate.abrCooldownUntilUs = t + 4000000ULL;'
chk 493 '      encoder.forceKeyNext = true;'
chk 509 '  if (rate.m9Enabled && !encoder.tuneManualOverride) {'
chk 510 '    const bool downByClient ='
chk 541 '    int targetLevel = rate.m9Level;'
chk 551 '    }'
chk 554 '    if (targetLevel != rate.m9Level) {'
chk 587 '      rate.m9Level = targetLevel;'
chk 590 '      rate.m9UpPressureSeconds = 0;'
chk 591 '    }'

RENAME_ABR='s/\bencoder\.activeFps\b/in.activeFps/g; s/\bstartUs\b/in.startUs/g; s/\bsender\.sentFrames\b/in.sentFrames/g; s/\bframeGating\.staticMode\b/in.staticMode/g; s/\bmetricsFresh\b/in.metricsFresh/g; s/\bclAvgLatencyUs\b/in.clAvgLatencyUs/g; s/\bclAvgDecodeTailUs\b/in.clAvgDecodeTailUs/g; s/\bclDecodedFpsX100\b/in.clDecodedFpsX100/g; s/\bcb2eAvgUs\b/in.cb2eAvgUs/g'
RENAME_M9='s/\bmetricsFresh\b/in.metricsFresh/g; s/\bclCongestionState\b/in.clCongestionState/g; s/\bclDecodedFpsX100\b/in.clDecodedFpsX100/g; s/\bclQueueDepthMax\b/in.clQueueDepthMax/g; s/\bclUdpDropPm\b/in.clUdpDropPm/g; s/\bclAvgLatencyUs\b/in.clAvgLatencyUs/g; s/\bclAvgDecodeTailUs\b/in.clAvgDecodeTailUs/g; s/\bcb2eAvgUs\b/in.cb2eAvgUs/g'
sed -n '327,461p' $O | perl -pe "$RENAME_ABR" > $B/abr_body.txt
sed -n '510,551p' $O | perl -pe "$RENAME_M9" > $B/m9_body.txt
grep -q -E '\bencoder\.|\bsender\.|\bframeGating\.|\bclientMetrics\.' $B/abr_body.txt && { echo "abr body still reads outside state"; grep -n -E '\bencoder\.|\bsender\.|\bframeGating\.' $B/abr_body.txt; exit 1; } || true
grep -q -E '\bencoder\.|\bsender\.|\bframeGating\.|\bclientMetrics\.' $B/m9_body.txt && { echo "m9 body still reads outside state"; exit 1; } || true

# ---------------- header: input / decision structs + the four members ----------------
SE=$(grep -n -m1 '^struct RateControlState {$' $B/abr_before.hpp | cut -d: -f1)
CE=$(awk -v s="$SE" 'NR>s && /^};$/ {print NR; exit}' $B/abr_before.hpp)
{
  sed -n "1,$((SE - 1))p" $B/abr_before.hpp | sed 's|^#include <cstdint>$|#include <algorithm>\n#include <cstdint>|'
  cat <<'EOF'
// Inputs of the once-a-second ABR profile decision: what stats_tick_h264 measured this second.
struct AbrInputs {
  bool metricsFresh = false;      // client metrics arrived this second (else host evidence only)
  uint32_t clDecodedFpsX100 = 0;  // client decoded fps x100
  uint64_t clAvgLatencyUs = 0;    // client average end-to-end latency
  uint64_t clAvgDecodeTailUs = 0; // client average decode tail
  uint64_t cb2eAvgUs = 0;         // host callback->encode-start average (fallback evidence)
  uint64_t sentFrames = 0;        // frames the sender actually sent this second
  bool staticMode = false;        // frame gating is in static mode
  uint32_t activeFps = 0;         // the encoder's current fps target
  uint64_t startUs = 0;           // stream start (warmup anchor)
};
struct AbrDecision {
  int targetProfile = 0;          // 0 high, 1 mid, 2 low (== abrProfile: hold)
  const char* reason = "none";
};
// Inputs of the once-a-second M9 level decision.
struct M9Inputs {
  bool metricsFresh = false;
  uint32_t clCongestionState = 0;
  uint32_t clDecodedFpsX100 = 0;
  uint32_t clQueueDepthMax = 0;
  uint32_t clUdpDropPm = 0;
  uint64_t clAvgLatencyUs = 0;
  uint64_t clAvgDecodeTailUs = 0;
  uint64_t cb2eAvgUs = 0;
};
struct M9Decision {
  int targetLevel = 0;            // 0..3 (== m9Level: hold)
  const char* reason = "hold";
};

EOF
  sed -n "${SE},$((CE - 1))p" $B/abr_before.hpp
  cat <<'EOF'

  // --- decisions (Phase 2-T1: the former stats_tick_h264 blocks, pure on this state + `in` + t) ---
  // ABR profile for this second: updates the pressure / good-second counters and returns the profile
  // to run (== abrProfile means hold). The caller applies it to the encoder and, if that succeeds,
  // calls CommitAbrProfile.
  AbrDecision DecideAbrProfile(const AbrInputs& in, uint64_t t) {
    RateControlState& rate = *this;
EOF
  cat $B/abr_body.txt
  cat <<'EOF'
    return AbrDecision{targetProfile, abrReason};
  }
  // The encoder accepted the new profile: record it and start the 4s cooldown.
  void CommitAbrProfile(int targetProfile, bool ladderReduced, uint64_t t) {
    RateControlState& rate = *this;
    rate.encodeLadderReduced = ladderReduced;
EOF
  sed -n '488,492p' $O | sed -E 's/^  //'
  cat <<'EOF'
  }
  // M9 level for this second: updates the down / up pressure counters and returns the level to run
  // (== m9Level means hold). The caller logs / applies it and calls CommitM9Level.
  M9Decision DecideM9Level(const M9Inputs& in, uint64_t t) {
    RateControlState& rate = *this;
EOF
  cat $B/m9_body.txt
  cat <<'EOF'
    return M9Decision{targetLevel, m9Reason};
  }
  void CommitM9Level(int targetLevel, uint64_t t) {
    RateControlState& rate = *this;
EOF
  sed -n '587,590p' $O | sed -E 's/^  //'
  echo '  }'
  sed -n "${CE},\$p" $B/abr_before.hpp
} > "$H"

# ---------------- stage: call the members ----------------
{
  sed -n '1,326p' $O
  cat <<'EOF'
    const AbrInputs abrIn{metricsFresh, clDecodedFpsX100, clAvgLatencyUs, clAvgDecodeTailUs, cb2eAvgUs,
                          sender.sentFrames, frameGating.staticMode, encoder.activeFps, startUs};
    const AbrDecision abrDecision = rate.DecideAbrProfile(abrIn, t);
    const int targetProfile = abrDecision.targetProfile;
    const char* abrReason = abrDecision.reason;
EOF
  sed -n '462,485p' $O
  echo '      rate.CommitAbrProfile(targetProfile, ladderChoice.reduced, t);'
  sed -n '493,509p' $O
  cat <<'EOF'
    const M9Inputs m9In{metricsFresh, clCongestionState, clDecodedFpsX100, clQueueDepthMax, clUdpDropPm,
                        clAvgLatencyUs, clAvgDecodeTailUs, cb2eAvgUs};
    const M9Decision m9Decision = rate.DecideM9Level(m9In, t);
    const int targetLevel = m9Decision.targetLevel;
    const char* m9Reason = m9Decision.reason;
EOF
  sed -n '552,586p' $O
  echo '      rate.CommitM9Level(targetLevel, t);'
  sed -n '591,$p' $O
} > "$T"

# ---------------- CMake: the test ----------------
perl -0pi -e 's|(target_include_directories\(remote60_capture_cadence_gate_test PRIVATE src\)\n)|$1\nadd_executable(remote60_host_abr_test\n  src/host_abr_test.cpp\n)\ntarget_include_directories(remote60_host_abr_test PRIVATE src)\n|' "$C"
grep -q 'remote60_host_abr_test' "$C" || { echo "CMake edit failed"; exit 1; }

# ---------------- verification ----------------
echo "== ABR body vs original 327..461 (every changed line must be a rename to in.*):"
diff <(sed -n '327,461p' $O) $B/abr_body.txt | grep -E '^>' | grep -v -E '\bin\.' || echo "  (all > lines carry in.)"
echo "  changed lines: $(diff <(sed -n '327,461p' $O) $B/abr_body.txt | grep -c '^>')"
echo "== M9 body vs original 510..551:"
diff <(sed -n '510,551p' $O) $B/m9_body.txt | grep -E '^>' | grep -v -E '\bin\.' || echo "  (all > lines carry in.)"
echo "  changed lines: $(diff <(sed -n '510,551p' $O) $B/m9_body.txt | grep -c '^>')"
echo "== stage diff (expected: the two blocks -> the input/decision lines; commit lines -> Commit* calls):"
diff $O "$T" | grep -c -E '^[<>]'
grep -o '"\([^"\\]\|\\.\)*"' $O $B/abr_before.hpp | sed 's/^[^:]*://' | sort -u > $B/lit_before.txt
grep -o '"\([^"\\]\|\\.\)*"' "$T" "$H" | sed 's/^[^:]*://' | sort -u > $B/lit_after.txt
echo "== literals only in OLD (must be empty):"; comm -23 $B/lit_before.txt $B/lit_after.txt
echo "== literals new (header include only):"; comm -13 $B/lit_before.txt $B/lit_after.txt
wc -l "$T" "$H"

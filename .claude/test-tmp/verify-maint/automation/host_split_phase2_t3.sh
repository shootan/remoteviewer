#!/usr/bin/env bash
# Host split refactor Phase 2-T3 (unit-testable trailing kick / static refresh): the decision
# conditions of stage_pop_frame (host_stage_pop_frame.cpp) become KickState members in host_kick.hpp:
#   Due(nowUs)                      <- line 159's condition
#   NeedKick(barrierClosed)         <- lines 176..183 (comments + the three predicates)
#   MarkKickedForCurrentInput()     <- line 199
#   StaticRefreshDue(nowUs)         <- lines 226..229 (the kick-side half of the refresh condition)
# Bodies verbatim (de-indented); the stage keeps the ring / barrier / sender-queue reads, the fill and
# the bookkeeping. Run once from the repo root.
set -euo pipefail
S=apps/native_poc/src
P=$S/host_stage_pop_frame.cpp
H=$S/host_kick.hpp
C=apps/native_poc/CMakeLists.txt
git diff --quiet HEAD -- "$P" "$H" "$C" || { echo "tree not clean"; exit 1; }
B=/tmp/p2t3; rm -rf $B; mkdir -p $B
tr -d '\r' < "$P" > $B/pop_before.cpp
tr -d '\r' < "$H" > $B/kick_before.hpp
O=$B/pop_before.cpp
[ "$(wc -l < $O)" = 339 ] || { echo "unexpected stage length $(wc -l < $O)"; exit 1; }
chk() { [ "$(sed -n "$1p" $O)" = "$2" ] || { echo "anchor $1 moved: $(sed -n "$1p" $O)"; exit 1; }; }
chk 159 '  if (kick.pending && nowUs >= kick.dueAtUs) {'
chk 176 '      // The latest real input is "stuck" until its capture timestamp is observed on an emitted AU;'
chk 178 '      const bool latestInputStuck = (kick.lastRealInputCaptureUs > kick.lastEmittedAuCaptureUs);'
chk 181 '      const bool alreadyKickedThisInput ='
chk 182 '          (kick.lastRealInputCaptureUs != 0 && kick.lastKickedForInputCaptureUs == kick.lastRealInputCaptureUs);'
chk 183 '      const bool needKick = barrierClosed || (latestInputStuck && !alreadyKickedThisInput);'
chk 199 '        kick.lastKickedForInputCaptureUs = kick.lastRealInputCaptureUs;  // one-shot per held input'
chk 225 '  if (!servedBootstrap && kick.staticRefreshIntervalUs > 0 && useH264 &&'
chk 226 '      clientSession.streamControlActive.load(std::memory_order_acquire) && !kick.pending &&'
chk 227 '      kick.lastEmittedAuCaptureUs != 0 &&'
chk 228 '      nowUs >= kick.lastEmittedAuCaptureUs + kick.staticRefreshIntervalUs &&'
chk 229 '      nowUs >= kick.lastStaticRefreshAttemptUs + kick.staticRefreshIntervalUs) {'

# ---------------- header ----------------
CE=$(grep -n -m1 '^};$' $B/kick_before.hpp | cut -d: -f1)
{
  sed -n "1,$((CE - 1))p" $B/kick_before.hpp
  cat <<'EOF'

  // --- decisions (Phase 2-T3: the former stage_pop_frame conditions, pure on this state) ---
  // The armed trailing edge has arrived.
  bool Due(uint64_t nowUs) const { return pending && nowUs >= dueAtUs; }
  // Whether the trailing edge needs a kick: the barrier still wants a key, or the newest real input is
  // still held in the encoder and has not been kicked yet.
  bool NeedKick(bool barrierClosed) const {
    const KickState& kick = *this;
EOF
  sed -n '176,182p' $O | sed -E 's/^  //'
  echo '    return barrierClosed || (latestInputStuck && !alreadyKickedThisInput);'
  cat <<'EOF'
  }
  // One-shot per held input: remember which input the kick just re-served.
  void MarkKickedForCurrentInput() { lastKickedForInputCaptureUs = lastRealInputCaptureUs; }
  // Periodic static refresh cadence (the kick-side half of the stage condition): no kick pending, an AU
  // has been seen, and both the emitted-AU and the attempt anchors are at least one interval old.
  bool StaticRefreshDue(uint64_t nowUs) const {
    const KickState& kick = *this;
    return !kick.pending &&
EOF
  sed -n '227,228p' $O | sed -E 's/^ {6}/           /'
  sed -n '229p' $O | sed -E 's/^ {6}/           /; s/\) \{$/;/'
  echo '  }'
  sed -n "${CE},\$p" $B/kick_before.hpp
} > "$H"

# ---------------- stage ----------------
{
  sed -n '1,158p' $O
  echo '  if (kick.Due(nowUs)) {'
  sed -n '160,175p' $O
  echo '      const bool needKick = kick.NeedKick(barrierClosed);'
  sed -n '184,198p' $O
  echo '        kick.MarkKickedForCurrentInput();'
  sed -n '200,225p' $O
  echo '      clientSession.streamControlActive.load(std::memory_order_acquire) && kick.StaticRefreshDue(nowUs)) {'
  sed -n '230,$p' $O
} > "$P"

# ---------------- CMake (the working tree may be CRLF: tolerate a CR before the newline) ----------------
perl -0pi -e 's|(target_include_directories\(remote60_host_frame_gate_test PRIVATE src\)\r?\n)|$1\nadd_executable(remote60_host_kick_test\n  src/host_kick_test.cpp\n)\ntarget_include_directories(remote60_host_kick_test PRIVATE src)\n|' "$C"
grep -q 'remote60_host_kick_test' "$C" || { echo "CMake edit failed"; exit 1; }

# ---------------- verification ----------------
echo "== stage diff:"; (diff $O "$P" || true) | grep -E '^[<>]' | cut -c1-120
grep -o '"\([^"\\]\|\\.\)*"' $O $B/kick_before.hpp | sed 's/^[^:]*://' | sort -u > $B/lit_before.txt
grep -o '"\([^"\\]\|\\.\)*"' "$P" "$H" | sed 's/^[^:]*://' | sort -u > $B/lit_after.txt
echo "== literals only in OLD (must be empty):"; comm -23 $B/lit_before.txt $B/lit_after.txt || true
echo "== literals new (must be empty):"; comm -13 $B/lit_before.txt $B/lit_after.txt || true
wc -l "$P" "$H"

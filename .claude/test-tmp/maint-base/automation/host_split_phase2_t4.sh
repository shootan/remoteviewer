target_include_directories(remote60_host_backend_policy_test PRIVATE src)
target_link_libraries(remote60_host_backend_policy_test PRIVATE capture)
|#!/usr/bin/env bash
# Host split refactor Phase 2-T4 (unit-testable desktop-backend promotion gate): the pure state updates
# of stage_backend's demotion/promotion block (host_stage_backend.cpp lines 211..310) become
# DesktopBackendState members in host_backend_policy.hpp:
#   NoteDemotionEpisode(nowUs)         <- 215        DefaultProbeDue(nowUs)         <- 220
#   NoteDefaultProbe(nowUs, isDefault) <- 221..227   DefaultStable(nowUs)           <- 229..231
#   RetryDue(nowUs)                    <- 232..234   NoteSecureAtDeadline()         <- 241..242
#   NoteDeferredForSecure()            <- 247..249   NotePromotionAttempt()         <- 253..254
#   NotePromotionSuccess(nowUs)        <- 277..280 + 284..286
#   NotePromotionFailure(nowUs)        <- 291, 293..295   ConsumeStabilityEvidence() <- 299..300
#   ResetPromotionGate()               <- 304..309
# The stage keeps the two OpenInputDesktop probes, restart_capture_session, the capture/encoder resets
# and the log lines, in the same order. The four kDesktopBackend* / kDesktopDefault* constants move from
# host_main_loop.hpp to host_backend_policy.hpp (which host_main_loop.hpp includes). Run once from the repo root.
set -euo pipefail
S=apps/native_poc/src
P=$S/host_stage_backend.cpp
H=$S/host_backend_policy.hpp
L=$S/host_main_loop.hpp
C=apps/native_poc/CMakeLists.txt
git diff --quiet HEAD -- "$P" "$H" "$L" "$C" || { echo "tree not clean"; exit 1; }
B=/tmp/p2t4; rm -rf $B; mkdir -p $B
tr -d '\r' < "$P" > $B/stage_before.cpp
tr -d '\r' < "$H" > $B/policy_before.hpp
tr -d '\r' < "$L" > $B/loop_before.hpp
O=$B/stage_before.cpp
[ "$(wc -l < $O)" = 315 ] || { echo "unexpected stage length $(wc -l < $O)"; exit 1; }
chk() { [ "$(sed -n "$1p" $O)" = "$2" ] || { echo "anchor $1 moved: $(sed -n "$1p" $O)"; exit 1; }; }
chk 211 '  if (backend.active != backend.requested &&'
chk 214 '    const uint64_t nowUs = qpc_now_us();'
chk 215 '    if (backend.demotionSinceUs == 0) backend.demotionSinceUs = nowUs;'
chk 220 '    if (nowUs >= backend.defaultProbeAtUs) {'
chk 221 '      backend.defaultProbeAtUs = nowUs + kDesktopDefaultProbeIntervalUs;'
chk 222 '      if (interactive_desktop_is_default_uncached()) {'
chk 227 '      }'
chk 228 '    }'
chk 229 '    const bool defaultStable ='
chk 231 '        (nowUs - backend.defaultStableSinceUs) >= kDesktopDefaultStableUs;'
chk 232 '    if (backend.retryAtUs == 0) {'
chk 234 '    } else if (nowUs >= backend.retryAtUs) {'
chk 238 '      bool finalDefault = defaultStable;'
chk 240 '        finalDefault = false;'
chk 241 '        backend.defaultStableSinceUs = 0;  // secure again: restart the settle clock'
chk 242 '        backend.secureProbeFalseTotal.fetch_add(1, std::memory_order_relaxed);'
chk 243 '      }'
chk 247 '        if (!backend.promotionDeferredForCurrentDeadline) {'
chk 249 '          backend.promotionDeferredSecureTotal.fetch_add(1, std::memory_order_relaxed);'
chk 250 '          std::cout << "[native-video-host] desktop-promotion-deferred reason=secure-desktop\n";'
chk 252 '      } else {'
chk 253 '        backend.promotionDeferredForCurrentDeadline = false;'
chk 254 '        backend.promotionAttempts.fetch_add(1, std::memory_order_relaxed);'
chk 255 '        const DesktopCaptureBackend demoted = backend.active;'
chk 276 '        if (promoted) {'
chk 277 '          backend.promotionSuccess.fetch_add(1, std::memory_order_relaxed);'
chk 280 '          }'
chk 284 '          backend.retryAtUs = 0;'
chk 286 '          backend.demotionSinceUs = 0;'
chk 287 '        } else {'
chk 291 '          backend.promotionFail.fetch_add(1, std::memory_order_relaxed);'
chk 292 '          backend.active = demoted;'
chk 295 '          backend.retryAtUs = nowUs + backend.retryDelayUs;'
chk 296 '        }'
chk 299 '        backend.defaultStableSinceUs = 0;'
chk 300 '        backend.defaultProbeAtUs = 0;'
chk 301 '      }'
chk 303 '  } else {'
chk 304 '    backend.retryAtUs = 0;'
chk 309 '    backend.promotionDeferredForCurrentDeadline = false;'
chk 310 '  }'
[ "$(sed -n '97p' $B/loop_before.hpp)" = "constexpr uint64_t kDesktopBackendRetryMinUs = 3'000'000;" ] || { echo "constants moved"; exit 1; }
[ "$(sed -n '100p' $B/loop_before.hpp | cut -c1-60)" = "constexpr uint64_t kDesktopDefaultProbeIntervalUs = 200'000;" ] || { echo "constants moved (100)"; exit 1; }

# ---------------- policy header: constants + members ----------------
NS=$(grep -n -m1 '^namespace remote60::native_poc {$' $B/policy_before.hpp | cut -d: -f1)
CE=$(awk -v s="$NS" 'NR>s && /^};$/ {print NR; exit}' $B/policy_before.hpp)
{
  sed -n "1,${NS}p" $B/policy_before.hpp | sed 's|^#include <atomic>$|#include <algorithm>\n#include <atomic>|'
  echo
  echo '// Promotion retry backoff and the secure-desktop settle gate (formerly in host_main_loop.hpp).'
  sed -n '97,100p' $B/loop_before.hpp
  sed -n "$((NS + 1)),$((CE - 1))p" $B/policy_before.hpp
  cat <<'EOF'

  // --- promotion gate / backoff (Phase 2-T4: the former stage_backend lines, pure on this state) ---
  // First sight of a demotion episode: remember when it began (for the promotion-wait metric).
  void NoteDemotionEpisode(uint64_t nowUs) {
    DesktopBackendState& backend = *this;
EOF
  sed -n '215p' $O
  cat <<'EOF'
  }
  // The bounded-cadence probe of the interactive desktop is due.
  bool DefaultProbeDue(uint64_t nowUs) const { return nowUs >= defaultProbeAtUs; }
  // One uncached probe result: schedules the next probe and runs the settle clock.
  void NoteDefaultProbe(uint64_t nowUs, bool isDefault) {
    DesktopBackendState& backend = *this;
EOF
  sed -n '221p' $O | sed -E 's/^  //'
  echo '    if (isDefault) {'
  sed -n '223,227p' $O | sed -E 's/^  //'
  cat <<'EOF'
  }
  // The default desktop has been up continuously for the whole settle window.
  bool DefaultStable(uint64_t nowUs) const {
    const DesktopBackendState& backend = *this;
    return backend.defaultStableSinceUs != 0 &&
           (nowUs - backend.defaultStableSinceUs) >= kDesktopDefaultStableUs;
  }
  // Retry deadline: the first call of an episode arms it (not due yet); afterwards, due once passed.
  bool RetryDue(uint64_t nowUs) {
    DesktopBackendState& backend = *this;
    if (backend.retryAtUs == 0) {
      backend.retryAtUs = nowUs + kDesktopBackendRetryMinUs;
      return false;
    }
    return nowUs >= backend.retryAtUs;
  }
  // The final uncached check at the deadline saw a secure desktop: restart the settle clock.
  void NoteSecureAtDeadline() {
    DesktopBackendState& backend = *this;
EOF
  sed -n '241,242p' $O | sed -E 's/^    //'
  cat <<'EOF'
  }
  // Deferred by the secure gate; true the first time in this deadline episode (the caller logs once).
  bool NoteDeferredForSecure() {
    DesktopBackendState& backend = *this;
    if (backend.promotionDeferredForCurrentDeadline) return false;
EOF
  sed -n '248,249p' $O | sed -E 's/^      //'
  cat <<'EOF'
    return true;
  }
  // A promotion is being attempted now.
  void NotePromotionAttempt() {
    DesktopBackendState& backend = *this;
EOF
  sed -n '253,254p' $O | sed -E 's/^    //'
  cat <<'EOF'
  }
  // The requested backend is back: telemetry, and the retry / episode clocks reset.
  void NotePromotionSuccess(uint64_t nowUs) {
    DesktopBackendState& backend = *this;
EOF
  sed -n '277,280p' $O | sed -E 's/^      //'
  sed -n '284,286p' $O | sed -E 's/^      //'
  cat <<'EOF'
  }
  // Still unavailable with the default desktop up: exponential backoff up to the ceiling.
  void NotePromotionFailure(uint64_t nowUs) {
    DesktopBackendState& backend = *this;
EOF
  sed -n '291p' $O | sed -E 's/^      //'
  sed -n '293,295p' $O | sed -E 's/^      //'
  cat <<'EOF'
  }
  // Any attempt consumes the stability evidence; the next one must gather fresh proof.
  void ConsumeStabilityEvidence() {
    DesktopBackendState& backend = *this;
EOF
  sed -n '299,300p' $O | sed -E 's/^    //'
  cat <<'EOF'
  }
  // No demotion in progress: the whole gate back to idle.
  void ResetPromotionGate() {
    DesktopBackendState& backend = *this;
EOF
  sed -n '304,309p' $O
  echo '  }'
  sed -n "${CE},\$p" $B/policy_before.hpp
} > "$H"

# ---------------- host_main_loop.hpp: drop the four constants ----------------
{ sed -n '1,96p' $B/loop_before.hpp; sed -n '101,$p' $B/loop_before.hpp; } > "$L"

# ---------------- stage ----------------
{
  sed -n '1,214p' $O
  echo '    backend.NoteDemotionEpisode(nowUs);'
  sed -n '216,219p' $O
  echo '    if (backend.DefaultProbeDue(nowUs)) {'
  echo '      backend.NoteDefaultProbe(nowUs, interactive_desktop_is_default_uncached());'
  echo '    }'
  echo '    const bool defaultStable = backend.DefaultStable(nowUs);'
  echo '    if (backend.RetryDue(nowUs)) {'
  sed -n '235,240p' $O
  echo '        backend.NoteSecureAtDeadline();'
  sed -n '243,246p' $O
  echo '        if (backend.NoteDeferredForSecure()) {'
  sed -n '250,252p' $O
  echo '        backend.NotePromotionAttempt();'
  sed -n '255,276p' $O
  echo '          backend.NotePromotionSuccess(nowUs);'
  sed -n '281,283p' $O
  echo '        } else {'
  sed -n '288,290p' $O
  echo '          backend.active = demoted;'
  echo '          backend.NotePromotionFailure(nowUs);'
  sed -n '296,298p' $O
  echo '        backend.ConsumeStabilityEvidence();'
  sed -n '301,303p' $O
  echo '    backend.ResetPromotionGate();'
  sed -n '310,$p' $O
} > "$P"

# ---------------- CMake (CR-tolerant anchor) ----------------
perl -0pi -e 's|(target_include_directories\(remote60_host_kick_test PRIVATE src\)\r?\n)|$1\nadd_executable(remote60_host_backend_policy_test\n  src/host_backend_policy_test.cpp\n)\ntarget_include_directories(remote60_host_backend_policy_test PRIVATE src)\n|' "$C"
grep -q 'remote60_host_backend_policy_test' "$C" || { echo "CMake edit failed"; exit 1; }

# ---------------- verification ----------------
echo "== stage diff:"; (diff $O "$P" || true) | grep -E '^[<>]' | cut -c1-120
echo "== constants: policy=$(grep -c -E '^constexpr uint64_t kDesktop' "$H") loop=$(grep -c -E '^constexpr uint64_t kDesktop' "$L")"
grep -o '"\([^"\\]\|\\.\)*"' $O $B/policy_before.hpp $B/loop_before.hpp | sed 's/^[^:]*://' | sort -u > $B/lit_before.txt
grep -o '"\([^"\\]\|\\.\)*"' "$P" "$H" "$L" | sed 's/^[^:]*://' | sort -u > $B/lit_after.txt
echo "== literals only in OLD (must be empty):"; comm -23 $B/lit_before.txt $B/lit_after.txt || true
echo "== literals new (must be empty):"; comm -13 $B/lit_before.txt $B/lit_after.txt || true
wc -l "$P" "$H" "$L"

#!/usr/bin/env bash
# Viewer split refactor Phase 0-0: the 88 file-scope globals (plus the constants and the state
# types they need) leave native_video_client_main.cpp for
#   viewer_common.hpp   -- the monolith's include block + using-declarations (lines 1-53, 57-103, 105-107)
#   viewer_globals.hpp  -- state types (SharedFrame, ClientRuntimeMetrics, OverlayConfigSnapshot,
#                          OverlayMetricSample/Averages, WindowThumb, ClientCongestionState), constants,
#                          and `extern` declarations of every global, grouped as in the monolith
#   viewer_globals.cpp  -- the global definitions, initialisers byte-identical
# main.cpp keeps every function; its anonymous namespace becomes remote60::native_poc::viewer so the
# moved-out modules and main() see one namespace. Behaviour is unchanged: same storage duration, same
# initialisers, no initialiser reads another global (checked below), so cross-TU init order is moot.
# Run once from the repo root on a clean tree (line numbers are those of e346ff7).
set -euo pipefail
S=apps/native_poc/src
M=$S/native_video_client_main.cpp
C=apps/native_poc/CMakeLists.txt
git diff --quiet HEAD -- "$M" "$C" || { echo "tree not clean"; exit 1; }
[ ! -e $S/viewer_globals.hpp ] || { echo "viewer_globals.hpp exists"; exit 1; }
B=/tmp/v00; rm -rf $B; mkdir -p $B
tr -d '\r' < "$M" > $B/main.cpp
O=$B/main.cpp
[ "$(wc -l < $O)" = 5349 ] || { echo "unexpected length $(wc -l < $O)"; exit 1; }
chk() { [ "$(sed -n "$1p" $O)" = "$2" ] || { echo "anchor $1 moved: $(sed -n "$1p" $O)"; exit 1; }; }
chk 50 '#include "time_utils.hpp"'
chk 53 '#pragma comment(lib, "Imm32.lib")'
chk 55 'namespace {'
chk 57 'using remote60::native_poc::ControlInputAckMessage;'
chk 103 'namespace json_profile = remote60::native_poc::json_profile;'
chk 105 '#ifndef REMOTE60_NATIVE_ENCODED_EXPERIMENT'
chk 107 '#endif'
chk 306 '// GDI defaults to the legacy System bitmap font, which is unscalable and cannot render'
chk 310 'int gUiDpi = 96;'
chk 549 'struct SharedFrame {'
chk 585 '};'
chk 587 'SharedFrame gFrame;'
chk 618 'constexpr uint64_t kCongestionRecoveryTimeoutUsDefault = 1500000;  // 1.5s'
chk 620 'enum class ClientCongestionState : uint8_t {'
chk 624 '};'
chk 626 'const char* congestion_state_name(ClientCongestionState state) {'
chk 639 'ClientInputQueue gInputQueueState;'
chk 660 'std::atomic<int32_t> gLastInputVideoY{0};'
chk 662 'struct ClientRuntimeMetrics {'
chk 683 '};'
chk 685 'ClientRuntimeMetrics gClientMetrics;'
chk 686 'KeyframeRequestState gKeyframeRequests{'
chk 689 '    kKeyframeRequestTokenCapacityDefault};'
chk 705 'std::mutex gLogMu;'
chk 707 'struct OverlayConfigSnapshot {'
chk 722 '};'
chk 724 'OverlayConfigSnapshot gOverlayConfig;'
chk 734 'RuntimeTuneState gRuntimeTuneState{'
chk 739 '    240};'
chk 741 'remote60::native_poc::StreamStateControl gStreamStateControl;'
chk 743 '// Browsing targets must not keep the host encoding (F1). The request rides the control'
chk 749 'void update_cursor_overlay(HWND hwnd);'
chk 750 'CaptureModeRequestState gCaptureModeRequests;'
chk 751 'ClientControlScheduler gControlScheduler;'
chk 753 'ClientControlMetricsSnapshot capture_client_control_metrics_snapshot() {'
chk 780 'struct OverlayMetricSample {'
chk 794 '};'
chk 796 'std::mutex gOverlayMetricsMu;'
chk 797 'std::deque<OverlayMetricSample> gOverlayMetrics;'
chk 798 'void log_client_line(const std::string& line);'
chk 800 'WindowPanelStateModel gWindowPanelState;'
chk 850 'struct WindowThumb {'
chk 855 '};'
chk 860 'constexpr uint64_t kThumbRefreshUs = 5000000;  // refresh a preview after 5 s'
chk 862 'void queue_thumbnail_fetches_from_panel() {'
chk 877 '}'
chk 878 'std::atomic<uint64_t> gSuppressMouseUntilUs{0};'
chk 894 'std::atomic<bool> gActiveTouchDown{false};'
chk 896 '// Panel metrics are authored at 96 DPI and scaled per monitor; the process is'
chk 908 'constexpr uint32_t kRuntimeBitrateMin = 300000;'
chk 912 'constexpr uint32_t kRuntimeKeyintMax = 240;'
chk 1202 '// Which keys this client forwarded a down for, so the matching up is forwarded by memory'
chk 1206 'std::atomic<bool> gForwardedKeyDown[256]{};'
chk 2135 'remote60::native_poc::InputMacro gInputMacro;'
chk 2189 'std::atomic<bool> gMacroButtonDown{false};'
chk 3189 '}  // namespace'
chk 3191 'int main(int argc, char** argv) {'

# Line sets. HDR = header (types/constants/comments verbatim; globals become extern).
# The global-definition lines inside HDR also go, verbatim, to the .cpp.
HDR_RANGES="549-618 620-624 639-660 662-683 685-705 707-722 724-741 750-751 780-794 796-797 800-860 878-894 908-912 306-310 1202-1206 2135-2135 2189-2189"
# Lines removed from main.cpp = HDR ranges + the include/using prelude replaced by viewer_common.hpp.
DEL_RANGES="1-53 57-103 105-107 $HDR_RANGES"

# ---------------- viewer_common.hpp ----------------
{
  cat <<'EOF'
#pragma once

// Shared prelude of the GNLinkViewer translation units.
//
// Role:    the include block and using-declarations native_video_client_main.cpp opened with, so
//          every viewer_* module compiles the moved code with the same names in scope.
// Thread:  none (declarations only).
// Input:   -
// Output:  namespace remote60::native_poc::viewer with the protocol/codec names imported.
// Callers: every viewer_*.cpp/.hpp and native_video_client_main.cpp.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-0).

EOF
  sed -n '1,53p' $O
  echo
  sed -n '105,107p' $O
  echo
  echo 'namespace remote60::native_poc::viewer {'
  echo
  sed -n '57,103p' $O
  echo
  echo '}  // namespace remote60::native_poc::viewer'
} > $B/viewer_common.hpp

# ---------------- viewer_globals.hpp / .cpp ----------------
# classify: a line at column 0 that defines a `gXxx` global (single-line initialiser or none)
GLOBAL_RE='^((?:std::|remote60::|[A-Z]|[a-z])[\w:<>, *&]*?)\s+(g[A-Z]\w*)(\[\d+\])?\s*(\{[^;]*\}|=\s*[^;]*)?;(\s*\/\/.*)?$'
extract_hdr() {  # prints header lines for a range: globals -> extern, everything else verbatim
  local a=$1 b=$2
  sed -n "${a},${b}p" $O | perl -pe '
    BEGIN { $re = qr/'"$GLOBAL_RE"'/; }
    if (/^KeyframeRequestState gKeyframeRequests\{$/) { $_ = "extern KeyframeRequestState gKeyframeRequests;\n"; $skip = 3; next }
    if (/^RuntimeTuneState gRuntimeTuneState\{$/) { $_ = "extern RuntimeTuneState gRuntimeTuneState;\n"; $skip = 5; next }
    if ($skip) { $skip--; $_ = ""; next }
    if ($_ =~ $re) { $_ = "extern $1 $2$3;$5\n"; s/;\s*$/;/; $_ .= "\n" unless /\n$/; }
  '
}
extract_defs() {  # prints the global definitions of a range, verbatim, including multi-line initialisers
  local a=$1 b=$2
  sed -n "${a},${b}p" $O | perl -ne '
    BEGIN { $re = qr/'"$GLOBAL_RE"'/; }
    if (/^KeyframeRequestState gKeyframeRequests\{$/ || /^RuntimeTuneState gRuntimeTuneState\{$/) { print; $multi = 1; next }
    if ($multi) { print; $multi = 0 if /\};\s*$/; next }
    print if $_ =~ $re;
  '
}
{
  cat <<'EOF'
#pragma once

// Every piece of process-wide viewer state, declared in one place.
//
// Role:    the monolith's 88 file-scope globals (+ their state types and tuning constants) as
//          `extern` declarations, grouped by feature exactly as they were declared. This header
//          is transitional: Phase 1 folds each group into a state struct and Phase 3 hands them
//          to a ViewerContext owned by main(), after which this file disappears.
// Thread:  see the `// thread:` block above each group (the pre-refactor rules, unchanged).
// Input:   -
// Output:  declarations only; definitions live in viewer_globals.cpp with the original initialisers.
// Callers: every viewer_* module and native_video_client_main.cpp.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-0).

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

// thread: recv writes gFrame/version, UI reads it under gFrame.mu and stamps gLastPresented*;
// gPaintQueued coalesces InvalidateRect between recv and UI; trace counters recv/UI.
EOF
  extract_hdr 549 618
  echo
  extract_hdr 620 624
  echo
  echo '// thread: UI enqueues input (gInputQueueState) that the control thread drains; gInputEnabled is'
  echo '// set by main at connect and cleared at shutdown; gUdpControl is written by the control thread'
  echo '// and ticked/fed by the recv thread (one reader, one writer on the shared socket).'
  extract_hdr 639 660
  echo
  extract_hdr 662 683
  echo
  echo '// thread: recv publishes gClientMetrics (atomics); control snapshots it for the host, UI reads'
  echo '// fps for the toolbar. Present counters are UI-written and read by the recv 1s stats line.'
  extract_hdr 685 705
  echo
  extract_hdr 707 722
  echo
  echo '// thread: main fills gOverlayConfig once; control writes the host capture meta on every pong;'
  echo '// the request states (tune/stream/capture-mode/scheduler) are UI/main producers, control consumer.'
  extract_hdr 724 741
  echo
  extract_hdr 750 751
  echo
  extract_hdr 780 794
  echo
  echo '// thread: recv pushes overlay metric samples; UI (overlay) reads under gOverlayMetricsMu.'
  extract_hdr 796 797
  echo
  echo '// thread: picker state is UI-owned; control applies window/monitor lists and thumbnails;'
  echo '// the selection gate is begun/committed on UI, acked on control, gated on recv (see comments).'
  extract_hdr 800 860
  echo
  echo '// thread: touch/mouse suppression is UI-only; the remote cursor sample is recv-written,'
  echo '// UI-timer-read (latest wins).'
  extract_hdr 878 894
  echo
  extract_hdr 908 912
  echo
  echo '// thread: UI only (GDI objects).'
  extract_hdr 306 310
  echo
  echo '// thread: UI only (key state), macro engine shared with the macro window on the UI thread.'
  extract_hdr 1202 1207
  echo
  extract_hdr 2135 2135
  extract_hdr 2189 2189
  echo
  echo '}  // namespace remote60::native_poc::viewer'
} > $B/viewer_globals.hpp

{
  cat <<'EOF'
// Definitions of the viewer's process-wide state (see viewer_globals.hpp for roles and threads).
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-0): each
// definition keeps its original initialiser. None of the dynamic initialisers reads another
// global, so the order across translation units does not matter.

#include "viewer_globals.hpp"

namespace remote60::native_poc::viewer {

EOF
  for r in $HDR_RANGES; do extract_defs ${r%-*} ${r#*-}; done
  echo
  echo '}  // namespace remote60::native_poc::viewer'
} > $B/viewer_globals.cpp

# Sanity: 88 globals expected in the .cpp (one definition each; the two multi-line ones counted once).
NDEF=$(perl -ne 'BEGIN { $re = qr/'"$GLOBAL_RE"'/; } $n++ if $_ =~ $re || /^(KeyframeRequestState gKeyframeRequests|RuntimeTuneState gRuntimeTuneState)\{$/; END { print $n + 0; }' $B/viewer_globals.cpp)
[ "$NDEF" = 88 ] || { echo "expected 88 global definitions, got $NDEF"; cut -c1-60 $B/viewer_globals.cpp; exit 1; }
NEXT=$(grep -c '^extern ' $B/viewer_globals.hpp)
[ "$NEXT" = 88 ] || { echo "expected 88 extern declarations, got $NEXT"; exit 1; }
# No initialiser may read another global (cross-TU init order would then matter).
if perl -ne 'BEGIN { $re = qr/'"$GLOBAL_RE"'/; } if ($_ =~ $re && defined $4 && $4 =~ /\bg[A-Z]/) { print; $bad = 1; } END { exit($bad ? 0 : 1); }' $B/viewer_globals.cpp; then echo "initialiser reads a global"; exit 1; fi

# ---------------- main.cpp: delete moved lines, wire the headers, rename the namespace ----------------
SEDDEL=""
for r in $DEL_RANGES; do SEDDEL="$SEDDEL ${r%-*},${r#*-}d;"; done
sed "$SEDDEL" $O > $B/main_stripped.cpp
{
  echo '#include "viewer_common.hpp"'
  echo '#include "viewer_globals.hpp"'
  cat $B/main_stripped.cpp
} > $B/main_new.cpp
# the anonymous namespace becomes the viewer namespace; main() (outside it) imports the names
perl -0pi -e 's/\n\nnamespace \{\n/\n\nnamespace remote60::native_poc::viewer {\n/ or die "namespace open";
              s/\n\}  \/\/ namespace\n\nint main\(/\n}  \/\/ namespace remote60::native_poc::viewer\n\nusing namespace remote60::native_poc::viewer;\n\nint main(/ or die "namespace close";' $B/main_new.cpp
grep -q '^namespace remote60::native_poc::viewer {$' $B/main_new.cpp || { echo "namespace rename failed"; exit 1; }
grep -q '^using namespace remote60::native_poc::viewer;$' $B/main_new.cpp || { echo "using failed"; exit 1; }
# nothing moved may remain
for name in gFrame gRunning gForwardedKeyDown gInputMacro gMacroButtonDown gUiDpi; do
  if grep -qE "^[A-Za-z].*\b$name\b.*[;{]\s*$" $B/main_new.cpp | grep -v extern; then echo "$name still defined in main"; exit 1; fi
done

# ---------------- CMake ----------------
tr -d '\r' < "$C" > $B/cmake.txt
perl -0pi -e 's/(add_executable\(remote60_native_video_client_poc\n  src\/native_video_client_main\.cpp\n)/$1  src\/viewer_globals.cpp\n/ or die "cmake anchor"' $B/cmake.txt

# ---------------- install (CRLF, like the rest of the tree) ----------------
sed 's/$/\r/' $B/main_new.cpp > "$M"
sed 's/$/\r/' $B/viewer_common.hpp > $S/viewer_common.hpp
sed 's/$/\r/' $B/viewer_globals.hpp > $S/viewer_globals.hpp
sed 's/$/\r/' $B/viewer_globals.cpp > $S/viewer_globals.cpp
sed 's/$/\r/' $B/cmake.txt > "$C"
echo "main.cpp: 5349 -> $(wc -l < "$M") lines; globals.hpp $(wc -l < $S/viewer_globals.hpp), globals.cpp $(wc -l < $S/viewer_globals.cpp), common.hpp $(wc -l < $S/viewer_common.hpp)"

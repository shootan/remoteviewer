#!/usr/bin/env bash
# Viewer split refactor Phase 0-2 (viewer_env_util.hpp, header-only) and 0-3 (viewer_log.hpp/.cpp).
# Each step: move blocks verbatim with viewer_split_move.pl, wire the include, prove identity with
# viewer_split_check.pl against HEAD, build + viewer e2e, commit. Run from the repo root.
set -euo pipefail
S=apps/native_poc/src
M=$S/native_video_client_main.cpp
C=apps/native_poc/CMakeLists.txt
git diff --quiet HEAD || { echo "tree not clean"; exit 1; }
T=/tmp/v0203; rm -rf $T; mkdir -p $T

add_include() {  # $1 header name: inserted after the last existing viewer_*.hpp include of main.cpp
  perl -0pi -e 's/((?:#include "viewer_[a-z_]+\.hpp"\r\n)+)/$1#include "'"$1"'"\r\n/ or die "include anchor"' "$M"
}
add_cmake() {  # $1 cpp file: after src/viewer_globals.cpp in the viewer target
  perl -0pi -e 's/(  src\/viewer_globals\.cpp\r?\n)/$1  src\/'"$1"'\r\n/ or die "cmake anchor"' "$C"
}
check() {  # $1 ranges, rest: files whose concatenation must contain every range verbatim
  local r=$1; shift
  cat "$@" > $T/concat.txt
  perl automation/viewer_split_check.pl HEAD "$r" $T/concat.txt
}

# ================= 0-2 viewer_env_util.hpp =================
cat > $T/env_hpp_prelude.txt <<'EOF'
#pragma once

// Environment and string helpers of the viewer.
//
// Role:    parse_u32, env_truthy, env_u32_clamped, env_string_or_empty, trim_ascii, ascii_lower,
//          fixed_cstr_to_string, utf8_to_wide -- pure functions, no shared state.
// Thread:  none (pure).
// Input:   C strings / std::string / env var names.
// Output:  parsed numbers, trimmed or lowered strings, wide strings.
// Callers: viewer_args (parse_args), viewer_decoder_backend, startup env switches, control pong logs.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-2); header-only
// inline so no translation unit is added. Byte-identical copies live in host_args.hpp and
// host_string_util.hpp -- Phase 0-15 unifies them.

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

EOF
OUT=$(perl automation/viewer_split_move.pl --src "$M" --hpp $S/viewer_env_util.hpp --hpp-prelude $T/env_hpp_prelude.txt --header-only \
  --start '^bool parse_u32\(const char\* s, uint32_t\* out\) \{$' \
  --start '^bool env_truthy\(const char\* key\) \{$' \
  --start '^uint32_t env_u32_clamped\(const char\* key, uint32_t fallback, uint32_t minValue, uint32_t maxValue\) \{$' \
  --start '^std::string trim_ascii\(std::string v\) \{$' \
  --start '^std::string ascii_lower\(std::string v\) \{$' \
  --start '^std::string env_string_or_empty\(const char\* key\) \{$' \
  --start '^std::string fixed_cstr_to_string\(const char\* buf, size_t cap\) \{$' \
  --start '^std::wstring utf8_to_wide\(const std::string& utf8\) \{$')
echo "$OUT"
R=$(echo "$OUT" | sed -n 's/^RANGES //p')
add_include viewer_env_util.hpp
check "$R" $S/viewer_env_util.hpp
bash automation/viewer_split_gate.sh --e2e
git add "$M" $S/viewer_env_util.hpp
git commit -q -F - <<EOF
refactor(viewer): Phase 0-2 — env/string helpers to viewer_env_util.hpp (header-only, verbatim)

parse_u32, env_truthy, env_u32_clamped, trim_ascii, ascii_lower, env_string_or_empty,
fixed_cstr_to_string, utf8_to_wide leave main.cpp as inline functions (source ranges $R of the
previous revision, bodies byte-identical). Gates: move check PASS, build exit 0, viewer e2e ALL PASS.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
git log -1 --format='%h %s'

# ================= 0-3 viewer_log.hpp/.cpp =================
cat > $T/log_hpp_prelude.txt <<'EOF'
#pragma once

// Viewer logging and keyframe-request entry points.
//
// Role:    log_client_line (one serialised stdout line), request_keyframe (rate-limited keyframe
//          request with the throttle log), congestion_state_name.
// Thread:  any -- log_client_line serialises on gLogMu; request_keyframe only touches the
//          atomic-backed gKeyframeRequests.
// Input:   a finished log line / a keyframe reason code.
// Output:  stdout / a pending keyframe request for the control thread.
// Callers: recv thread (stats, telemetry, recovery), UI thread (present telemetry), control thread.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-3).

#include "viewer_common.hpp"
#include "viewer_globals.hpp"

namespace remote60::native_poc::viewer {

EOF
cat > $T/log_cpp_prelude.txt <<'EOF'
// See viewer_log.hpp. Extracted verbatim from native_video_client_main.cpp (Phase 0-3).

#include "viewer_log.hpp"

#include <iostream>
#include <mutex>

namespace remote60::native_poc::viewer {

EOF
OUT=$(perl automation/viewer_split_move.pl --src "$M" --hpp $S/viewer_log.hpp --hpp-prelude $T/log_hpp_prelude.txt \
  --cpp $S/viewer_log.cpp --cpp-prelude $T/log_cpp_prelude.txt \
  --start '^const char\* congestion_state_name\(ClientCongestionState state\) \{$' \
  --start '^void log_client_line\(const std::string& line\) \{$' \
  --start '^void request_keyframe\(uint16_t reason\) \{$')
echo "$OUT"
R=$(echo "$OUT" | sed -n 's/^RANGES //p')
# the forward declaration main.cpp carried for log_client_line is now the header's job
perl -0pi -e 's{void log_client_line\(const std::string& line\);\r\n}{}' "$M"
grep -q 'void log_client_line(const std::string& line);' "$M" && { echo "forward decl still present"; exit 1; }
add_include viewer_log.hpp
add_cmake viewer_log.cpp
check "$R" $S/viewer_log.hpp $S/viewer_log.cpp
bash automation/viewer_split_gate.sh --e2e
git add "$M" "$C" $S/viewer_log.hpp $S/viewer_log.cpp
git commit -q -F - <<EOF
refactor(viewer): Phase 0-3 — log_client_line / request_keyframe / congestion_state_name to viewer_log (verbatim)

Source ranges $R of the previous revision move to viewer_log.hpp/.cpp; the forward declaration
main.cpp carried for log_client_line is dropped in favour of the header. Gates: move check PASS,
build exit 0, viewer e2e ALL PASS.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
git log -1 --format='%h %s'
echo "main.cpp now $(wc -l < "$M") lines"

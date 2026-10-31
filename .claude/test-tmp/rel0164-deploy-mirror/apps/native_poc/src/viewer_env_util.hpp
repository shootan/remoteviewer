#pragma once

// Environment and string helpers of the viewer.
//
// Role:    fixed_cstr_to_string (viewer-only) plus the shared env/string helpers re-exported into
//          namespace viewer -- pure functions, no shared state.
// Thread:  none (pure).
// Input:   C strings / std::string / env var names.
// Output:  parsed numbers, trimmed or lowered strings, wide strings.
// Callers: viewer_args (parse_args), viewer_decoder_backend, startup env switches, control pong logs.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-2); header-only
// inline so no translation unit is added. Phase 0-15 replaced the byte-identical copies of the host
// helpers by env_util.hpp / string_util.hpp.

#include "viewer_common.hpp"
#include "env_util.hpp"
#include "string_util.hpp"

namespace remote60::native_poc::viewer {

// Shared with the host since Phase 0-15 (env_util.hpp, string_util.hpp); imported so main() and the
// viewer modules keep using the unqualified names.
using remote60::native_poc::parse_u32;
using remote60::native_poc::env_truthy;
using remote60::native_poc::env_u32_clamped;
using remote60::native_poc::env_string_or_empty;
using remote60::native_poc::trim_ascii;
using remote60::native_poc::ascii_lower;
using remote60::native_poc::utf8_to_wide;

inline std::string fixed_cstr_to_string(const char* buf, size_t cap) {
  if (!buf || cap == 0) return std::string{};
  size_t n = 0;
  while (n < cap && buf[n] != '\0') ++n;
  return std::string(buf, buf + n);
}

}  // namespace remote60::native_poc::viewer

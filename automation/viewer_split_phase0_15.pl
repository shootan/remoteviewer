#!/usr/bin/perl
# Viewer split refactor Phase 0-15: the byte-identical helper copies of host and viewer become one
# shared header each -- env_util.hpp (parse_u32/env_truthy/env_u32_clamped/env_string_or_empty),
# string_util.hpp (the former host_string_util.hpp, which becomes a forwarding include) and
# backend_request_match.hpp (backend_request_* / backend_fallback_reason). The shared headers were
# written beforehand; this script rewires host_args.hpp, host_string_util.hpp, host_capture_device.hpp/.cpp,
# viewer_env_util.hpp, viewer_decoder_backend.hpp and the viewer CMake list. Anchors are exact; any
# miss dies before writing. CRLF preserved. Run once from the repo root.
use strict;
use warnings;

my $S = 'apps/native_poc/src';
sub slurp { my ($f) = @_; open(my $h, '<:raw', $f) or die "open $f: $!"; local $/; my $s = <$h>; close $h; $s =~ s/\r\n/\n/g; return $s; }
sub spew  { my ($f, $s) = @_; $s =~ s/\r?\n/\r\n/g; open(my $h, '>:raw', $f) or die "write $f: $!"; print $h $s; close $h; }
sub must  { my ($ok, $what) = @_; die "anchor failed: $what\n" unless $ok; print "ok: $what\n"; }

# ---- host_args.hpp: drop the four env helpers, include env_util.hpp ----
{
  my $f = "$S/host_args.hpp"; my $t = slurp($f);
  must($t =~ s/inline std::string env_string_or_empty\(const char\* key\) \{\n.*?\n\}\n\n//s, 'host_args env_string_or_empty');
  must($t =~ s/inline bool parse_u32\(const char\* s, uint32_t\* out\) \{\n.*?\n\}\n\n//s, 'host_args parse_u32');
  must($t =~ s/inline bool env_truthy\(const char\* key\) \{\n.*?\n\}\n\n//s, 'host_args env_truthy');
  must($t =~ s/inline uint32_t env_u32_clamped\(const char\* key, uint32_t fallback, uint32_t minValue, uint32_t maxValue\) \{\n.*?\n\}\n\n//s, 'host_args env_u32_clamped');
  must($t =~ s/#include <vector>\n/#include <vector>\n\n#include "env_util.hpp"\n/, 'host_args include');
  must($t =~ s/(\/\/ switch prelude at the top of main\(\) stays there until Phase 1 folds it into the state structs\.\n)/$1\/\/ The env_* helpers moved to env_util.hpp, shared with the viewer (viewer split refactor Phase 0-15).\n/, 'host_args comment');
  spew($f, $t);
}

# ---- host_string_util.hpp: forwarding include ----
spew("$S/host_string_util.hpp", <<'EOF');
#pragma once

// Forwarding include. The host string helpers (trim_ascii, ascii_lower, utf8_to_wide, wide_to_utf8,
// wide_lower, hr_hex, parse_csv_lower, base_name_lower) live in string_util.hpp, shared with the
// viewer since the viewer split refactor Phase 0-15; host modules keep including this name.

#include "string_util.hpp"
EOF
print "ok: host_string_util forwarding\n";

# ---- host_capture_device.hpp/.cpp: declarations and definitions replaced by the shared header ----
{
  my $f = "$S/host_capture_device.hpp"; my $t = slurp($f);
  must($t =~ s/\/\/ --- encoder backend request matching -+\nbool backend_request_is_any\(const std::string& requestLower, const char\* const\* values,\n\s+size_t valueCount\);\nbool backend_request_satisfied\(const std::string& requestLower, const std::string& resolvedLower\);\nbool backend_request_is_vendor_specific\(const std::string& requestLower\);\nstd::string backend_fallback_reason\(const std::string& requestedRaw, const char\* resolvedBackendRaw\);\n\n//, 'capture_device.hpp decls');
  must($t =~ s/#include "capture_backend_dxgi\.hpp"\n/#include "backend_request_match.hpp"\n#include "capture_backend_dxgi.hpp"\n/, 'capture_device.hpp include');
  spew($f, $t);
  $f = "$S/host_capture_device.cpp"; $t = slurp($f);
  must($t =~ s/\nbool backend_request_is_any\(const std::string& requestLower, const char\* const\* values,\n.*?\nstd::string backend_fallback_reason\(const std::string& requestedRaw, const char\* resolvedBackendRaw\) \{\n.*?\n\}\n\n/\n/s, 'capture_device.cpp defs');
  spew($f, $t);
}

# ---- viewer_env_util.hpp: keep fixed_cstr_to_string, import the rest from the shared headers ----
{
  my $f = "$S/viewer_env_util.hpp"; my $t = slurp($f);
  for my $sig ('inline bool parse_u32\(const char\* s, uint32_t\* out\)', 'inline bool env_truthy\(const char\* key\)',
               'inline uint32_t env_u32_clamped\(const char\* key, uint32_t fallback, uint32_t minValue, uint32_t maxValue\)',
               'inline std::string trim_ascii\(std::string v\)', 'inline std::string ascii_lower\(std::string v\)',
               'inline std::string env_string_or_empty\(const char\* key\)', 'inline std::wstring utf8_to_wide\(const std::string& utf8\)') {
    must($t =~ s/$sig \{\n.*?\n\}\n\n?//s, "viewer_env_util drop $sig");
  }
  must($t =~ s/#include "viewer_common\.hpp"\n/#include "viewer_common.hpp"\n#include "env_util.hpp"\n#include "string_util.hpp"\n/, 'viewer_env_util includes');
  must($t =~ s/(namespace remote60::native_poc::viewer \{\n\n)/$1\/\/ Shared with the host since Phase 0-15 (env_util.hpp, string_util.hpp); imported so main() and the\n\/\/ viewer modules keep using the unqualified names.\nusing remote60::native_poc::parse_u32;\nusing remote60::native_poc::env_truthy;\nusing remote60::native_poc::env_u32_clamped;\nusing remote60::native_poc::env_string_or_empty;\nusing remote60::native_poc::trim_ascii;\nusing remote60::native_poc::ascii_lower;\nusing remote60::native_poc::utf8_to_wide;\n\n/, 'viewer_env_util usings');
  must($t =~ s/\/\/ Role:    parse_u32, env_truthy, env_u32_clamped, env_string_or_empty, trim_ascii, ascii_lower,\n\/\/          fixed_cstr_to_string, utf8_to_wide -- pure functions, no shared state\./\/\/ Role:    fixed_cstr_to_string (viewer-only) plus the shared env\/string helpers re-exported into\n\/\/          namespace viewer -- pure functions, no shared state./, 'viewer_env_util role');
  must($t =~ s/\/\/ inline so no translation unit is added\. Byte-identical copies live in host_args\.hpp and\n\/\/ host_string_util\.hpp -- Phase 0-15 unifies them\./\/\/ inline so no translation unit is added. Phase 0-15 replaced the byte-identical copies of the host\n\/\/ helpers by env_util.hpp \/ string_util.hpp./, 'viewer_env_util note');
  spew($f, $t);
}

# ---- viewer_decoder_backend.hpp: re-export the shared functions; the .cpp goes away ----
spew("$S/viewer_decoder_backend.hpp", <<'EOF');
#pragma once

// Decoder backend request matching, for the "backendFallbackReason" the viewer logs when the
// H.264 decoder comes up with something other than what REMOTE60_NATIVE_DECODER_BACKEND asked for.
//
// Role:    re-exports backend_request_* / backend_fallback_reason from backend_request_match.hpp
//          (shared with the host since viewer split Phase 0-15) into namespace viewer.
// Thread:  none (pure).
// Input:   the requested backend string and the resolved backend name.
// Output:  a fallback-reason token for the log line.
// Callers: recv thread, decoder initialisation log.

#include "backend_request_match.hpp"
#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

using remote60::native_poc::backend_request_is_any;
using remote60::native_poc::backend_request_satisfied;
using remote60::native_poc::backend_request_is_vendor_specific;
using remote60::native_poc::backend_fallback_reason;

}  // namespace remote60::native_poc::viewer
EOF
unlink("$S/viewer_decoder_backend.cpp") or die "unlink viewer_decoder_backend.cpp: $!";
print "ok: viewer_decoder_backend re-export, .cpp removed\n";

{
  my $f = 'apps/native_poc/CMakeLists.txt'; my $t = slurp($f);
  must($t =~ s/  src\/viewer_decoder_backend\.cpp\n//, 'cmake drop viewer_decoder_backend.cpp');
  spew($f, $t);
}
print "done\n";

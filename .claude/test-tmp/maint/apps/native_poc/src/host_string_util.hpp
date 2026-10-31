#pragma once

// Forwarding include. The host string helpers (trim_ascii, ascii_lower, utf8_to_wide, wide_to_utf8,
// wide_lower, hr_hex, parse_csv_lower, base_name_lower) live in string_util.hpp, shared with the
// viewer since the viewer split refactor Phase 0-15; host modules keep including this name.

#include "string_util.hpp"

#pragma once

// sh-filter r3 (Codex R2) -- TEST BUILD ONLY: compiled only with GNLINK_SH_TEST_SEAM, which the Gradle file
// defines for the debug variant with -Pgnlink.shTestSeam=true and never for release. Marker: sh-test-seam.
//
// Two system properties a test sets with `adb shell setprop` (debug.* is writable from the shell):
//   debug.gnlink.sh.handoff_delay_ms  N   -- sleep N ms (<= 2000) between publishing a frame's handoff
//                                            entry and releasing its buffer (or the reverse, below);
//   debug.gnlink.sh.handoff_order     after -- r2's order: release the buffer FIRST, then the delay, then
//                                            publish -- the race R2 describes, as the counter-case.

#ifdef GNLINK_SH_TEST_SEAM

#include <sys/system_properties.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace remote60::android_direct {

inline const char* kShSeamNativeMarker = "sh-test-seam";

inline bool sh_seam_publish_after_release() {
  char v[PROP_VALUE_MAX] = {};
  return __system_property_get("debug.gnlink.sh.handoff_order", v) > 0 && std::strcmp(v, "after") == 0;
}

inline void sh_seam_handoff_delay() {
  char v[PROP_VALUE_MAX] = {};
  if (__system_property_get("debug.gnlink.sh.handoff_delay_ms", v) <= 0) return;
  const long ms = std::strtol(v, nullptr, 10);
  if (ms > 0 && ms <= 2000) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // namespace remote60::android_direct

#endif  // GNLINK_SH_TEST_SEAM

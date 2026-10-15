#pragma once

// Per-AU trace (diagnostic, off by default): one "[native-video-host][au]" line per AU at the send
// decision and every wire line in the sender, only with REMOTE60_NATIVE_AU_TRACE=1, bounded by
// kAuTraceMaxLines. The catch-up's verification joins host seq -> AU source / content version ->
// enqueue / wire times -> the receiver's marker id.
// Thread: main encode loop (au line), sender thread (wire line); the flag is read once.

#include <cstdint>
#include <cstdlib>

namespace remote60::native_poc {

constexpr uint64_t kAuTraceMaxLines = 250000;

inline bool au_trace_enabled() {
  static const bool on = [] {
    const char* v = std::getenv("REMOTE60_NATIVE_AU_TRACE");
    return v && (v[0] == '1' || v[0] == 't' || v[0] == 'T');
  }();
  return on;
}

}  // namespace remote60::native_poc

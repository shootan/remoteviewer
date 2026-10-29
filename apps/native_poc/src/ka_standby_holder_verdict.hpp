#pragma once
// rel-0164 old-APK lane r3 (Codex codex_old_apk_lane_src_r2.md N2): the standby holder's exit, decided from
// EVERYTHING its end depends on -- not only whether the lease was ready and the viewer did not die. TEST TOOL ONLY.
// Pure (no process), so its cases are checked by ka_standby_holder_verdict_test.
//   0  the lease was ready, Release was sent, the viewer ended by itself within its deadline, and the shared child
//      verdict (its end confirmed, its output read to EOF and kept in its log -- ka_pc_e2e::children_end_all) is clean
//   2  setup (decided before any of this)
//   3  never ready
//   4  the viewer died on its own (before the stop)
//   5  the end was not a clean Release: Release could not be sent, or the viewer did not end within the deadline after
//      it (the job then ends it: that is CLEANUP, never a pass)
//   6  the child verdict failed (its end not confirmed, its output not read to EOF / not kept, its end record lost)
// When several hold, the first in this order is the exit: 4, 3, 5, 6.
#include <string>

namespace remote60::native_poc::ka_standby {

struct HolderEnd {
  bool ready = false;            // a State ready was seen
  bool died = false;             // the viewer ended on its own before the stop
  bool releaseSent = false;      // the Release message was written to the viewer
  bool endedAfterRelease = false;  // ...and the viewer ended by itself within the deadline after it
  int childFailures = 0;         // ka_pc_e2e::children_end_all() after the viewer's End()
};

inline int holder_exit(const HolderEnd& e) {
  if (e.died) return 4;
  if (!e.ready) return 3;
  if (!e.releaseSent || !e.endedAfterRelease) return 5;
  if (e.childFailures != 0) return 6;
  return 0;
}

inline std::string holder_exit_why(const HolderEnd& e) {
  switch (holder_exit(e)) {
    case 0: return "ok: ready, released, ended by itself, its output kept";
    case 3: return "never ready";
    case 4: return "the viewer died on its own";
    case 5: return !e.releaseSent ? "Release could not be sent (the job ended the viewer: cleanup only)"
                                  : "the viewer did not end within the deadline after Release (the job ended it: cleanup only)";
    case 6: return "the child verdict failed (" + std::to_string(e.childFailures) + "): end / EOF / log";
    default: return "?";
  }
}

}  // namespace remote60::native_poc::ka_standby

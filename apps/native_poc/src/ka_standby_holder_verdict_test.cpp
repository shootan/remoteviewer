// rel-0164 old-APK lane r3 (Codex codex_old_apk_lane_src_r2.md N2): the standby holder's exit from its whole end --
// ready / died / Release sent / ended within the deadline / the shared child verdict. Pure-logic, no process.
#include <cstdio>
#include <string>

#include "ka_standby_holder_verdict.hpp"

using namespace remote60::native_poc::ka_standby;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
HolderEnd clean() {
  HolderEnd e;
  e.ready = true;
  e.releaseSent = true;
  e.endedAfterRelease = true;
  return e;
}
}  // namespace

int main() {
  std::printf("ka_standby_holder_verdict_test\n");
  check("clean end: 0", holder_exit(clean()) == 0, holder_exit_why(clean()));
  {
    HolderEnd e = clean();
    e.endedAfterRelease = false;  // ready, Release sent, the viewer did not end in time: the job ended it
    check("ready, then NOT ended within the deadline after Release: 5 (cleanup is not a pass)", holder_exit(e) == 5,
          holder_exit_why(e));
  }
  {
    HolderEnd e = clean();
    e.releaseSent = false;
    check("ready, Release could not be sent: 5", holder_exit(e) == 5, holder_exit_why(e));
  }
  {
    HolderEnd e = clean();
    e.childFailures = 1;  // e.g. its output not read to EOF, or its log not kept
    check("ready and released, but the child verdict failed: 6", holder_exit(e) == 6, holder_exit_why(e));
  }
  {
    HolderEnd e = clean();
    e.ready = false;
    e.childFailures = 2;
    check("never ready (whatever else): 3", holder_exit(e) == 3, holder_exit_why(e));
  }
  {
    HolderEnd e = clean();
    e.died = true;
    e.releaseSent = false;
    e.endedAfterRelease = false;
    check("the viewer died on its own: 4", holder_exit(e) == 4, holder_exit_why(e));
  }
  {
    HolderEnd e = clean();
    e.endedAfterRelease = false;
    e.childFailures = 1;
    check("not ended in time AND a child failure: 5 (the first in the order)", holder_exit(e) == 5, holder_exit_why(e));
  }
  std::printf("\n%d checks, %d failed\nRESULT: %s\n", gChecks, gFailures, gFailures ? "FAIL" : "PASS");
  return gFailures ? 1 : 0;
}

// rel-0164 old-APK lane r3 (Codex codex_old_apk_lane_src_r2.md N2): the standby holder's exit from its whole end --
// ready / died / Release sent / ended within the deadline / the shared child verdict. Pure-logic, no process.
#include <cstdio>
#include <string>

#include "ka_standby_holder_env.hpp"
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
  // r5: the viewer's environment block -- the variables set replace what the base had, the rest is kept
  {
    std::vector<wchar_t> base;
    for (const wchar_t* e : {L"PATH=C:\\x", L"remote60_native_encoded_experiment_force=0", L"=C:=C:\\", L"LOCALAPPDATA=L"}) {
      base.insert(base.end(), e, e + wcslen(e));
      base.push_back(L'\0');
    }
    base.push_back(L'\0');
    const auto out = env_block_with(base, {{L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1"}, {L"REMOTE60_VIEWER_TEST_WINDOW_X", L"9999"}});
    int forceEntries = 0;
    for (size_t i = 0; i < out.size() && out[i] != L'\0';) {
      const std::wstring entry(&out[i]);
      i += entry.size() + 1;
      if (_wcsnicmp(entry.c_str(), L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=", 41) == 0) ++forceEntries;
    }
    check("r5 viewer env: FORCE=1 replaces the caller's value (one entry), X added, PATH / =C: / LOCALAPPDATA kept",
          env_block_value(out, L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE") == L"1" && forceEntries == 1 &&
              env_block_value(out, L"REMOTE60_VIEWER_TEST_WINDOW_X") == L"9999" && env_block_value(out, L"PATH") == L"C:\\x" &&
              env_block_value(out, L"LOCALAPPDATA") == L"L" && out.size() >= 2 && out[out.size() - 1] == L'\0' &&
              out[out.size() - 2] == L'\0');
    const auto bare = env_block_with(base, {});
    check("r5 viewer env: with nothing set, FORCE is what the base had (the r4 way: the caller decides)",
          env_block_value(bare, L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE") == L"0");
  }
  std::printf("\n%d checks, %d failed\nRESULT: %s\n", gChecks, gFailures, gFailures ? "FAIL" : "PASS");
  return gFailures ? 1 : 0;
}

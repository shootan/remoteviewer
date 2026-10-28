// The injection gate's own negative controls (Codex codex_test_injection_finding.md rule 4): every state that must
// stop a test before any client -- enabled, off for another reason, a config not loaded, silence -- is refused, and
// only the explicit OFF passes. Pure: no host is started, nothing is connected, no input API is called.

#include <cstdio>
#include <string>

#include "e2e_injection_guard.hpp"

namespace e = remote60::native_poc::e2e;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
const std::string kHead = "10-28 08:54:50.199 [native-video-host] latency-priority processAboveNormal=1\n";
}  // namespace

int main() {
  std::string why;
  check("the explicit OFF passes",
        e::e2e_injection_state(kHead + "x [native-video-host] input injection disabled (enableInputInjection=false)\n", &why) == 1);
  why.clear();
  check("ENABLED is refused",
        e::e2e_injection_state(kHead + "x [native-video-host] input injection enabled mode=background_message targetPid=0\n",
                               &why) == -1 && why.find("ENABLED") != std::string::npos, why);
  why.clear();
  check("off by an unsupported mode (not the explicit setting) is refused",
        e::e2e_injection_state(kHead + "x [native-video-host] input injection disabled (unsupported mode) mode=none\n", &why) == -1,
        why);
  why.clear();
  check("off by compile-time policy is not taken for the explicit setting either",
        e::e2e_injection_state(kHead + "x [native-video-host] input injection blocked by compile-time policy\n", &why) == 0);
  why.clear();
  check("a config the host could not load is refused",
        e::e2e_injection_state(kHead + "[native-video-host] failed to load --config file: x (failed to open file)\n", &why) == -1, why);
  why.clear();
  check("an earlier ENABLED in the same output wins over a later disabled line (contradiction)",
        e::e2e_injection_state("input injection enabled mode=x\ninput injection disabled (enableInputInjection=false)\n", &why) == -1);
  why.clear();
  check("silence is not yet a pass", e::e2e_injection_state(kHead, &why) == 0);
  why.clear();
  check("silence until the budget: refused, said why",
        !e::e2e_wait_injection_off([] { return kHead; }, 200, &why) && why.find("never said") != std::string::npos, why);
  why.clear();
  check("the wait passes on the explicit OFF",
        e::e2e_wait_injection_off([] { return kHead + "input injection disabled (enableInputInjection=false)\n"; }, 200, &why));
  check("a later injectionEnabled=1 in the input log is a contradiction",
        e::e2e_injection_contradicted("[input-timing] seq=1 eventKind=6 injectionEnabled=1 ackSent=1"));
  check("injectionEnabled=0 is not", !e::e2e_injection_contradicted("[input-timing] seq=1 eventKind=6 injectionEnabled=0"));
  std::printf("\n%d checks, %d failures\n%s\n", gChecks, gFailures, gFailures == 0 ? "RESULT PASS" : "RESULT FAIL");
  return gFailures == 0 ? 0 : 1;
}

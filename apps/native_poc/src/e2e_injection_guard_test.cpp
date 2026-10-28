// The injection gate's own negative controls (Codex codex_test_injection_finding.md rule 4 and "mode=none 채택"): every
// state that must stop a test before any client -- enabled, another mode or off reason, a partial line, a config not
// loaded, silence -- is refused, and only this launch's whole `mode=none` line passes. Pure: no host is started,
// nothing is connected, no input API is called.

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
const std::string kPower = "10-28 08:54:50.190 [native-video-host] " + e::e2e_power_off_line() + "\n";
const std::string kOff = kPower + "10-28 08:54:50.200 [native-video-host] input injection disabled (unsupported mode) mode=none";
}  // namespace

int main() {
  std::string why;
  check("the whole mode=none line passes (LF)", e::e2e_injection_state(kHead + kOff + "\n", &why) == 1);
  check("...and with CRLF", e::e2e_injection_state(kHead + kOff + "\r\n", &why) == 1);
  check("the arguments are exactly `--input-injection-mode none --host-power-management off`",
        e::e2e_injection_off_arg(L"x\\") == L" --input-injection-mode none --host-power-management off");
  why.clear();
  check("injection OFF without the power line (an older build) is not yet a pass",
        e::e2e_injection_state(kHead + "x [native-video-host] input injection disabled (unsupported mode) mode=none\n", &why) == 0);
  why.clear();
  check("power ON (the default line) is refused",
        e::e2e_injection_state(kHead + "x [native-video-host] host power management=on (local startup policy, default): execution "
                               "state kept, display wake at a stream start\n" + kOff + "\n", &why) == -1, why);
  why.clear();
  check("a refused power argument is refused",
        e::e2e_injection_state(kHead + "[native-video-host] invalid --host-power-management value: of (on|off) -- not started\n",
                               &why) == -1, why);
  why.clear();
  check("the power line still being written is not yet a pass",
        e::e2e_injection_state(kHead + "[native-video-host] host power management=of", &why) == 0);
  why.clear();
  check("a line still being written is not yet a pass (no line end yet)", e::e2e_injection_state(kHead + kOff, &why) == 0);
  why.clear();
  check("another mode value (mode=none2) is refused",
        e::e2e_injection_state(kHead + kOff + "2\n", &why) == -1, why);
  why.clear();
  check("an unknown mode's fallback (mode=bogus) is refused",
        e::e2e_injection_state(kHead + "[native-video-host] input injection disabled (unsupported mode) mode=bogus\n", &why) == -1, why);
  why.clear();
  check("ENABLED is refused",
        e::e2e_injection_state(kHead + "x [native-video-host] input injection enabled mode=background_message targetPid=0\n",
                               &why) == -1 && why.find("ENABLED") != std::string::npos, why);
  why.clear();
  check("off by a config (another standard here) is refused",
        e::e2e_injection_state(kHead + "x [native-video-host] input injection disabled (enableInputInjection=false)\n", &why) == -1,
        why);
  why.clear();
  check("off by compile-time policy is not taken for it either (r5 P2-2: refused -- not the explicit OFF line)",
        e::e2e_injection_state(kHead + "x [native-video-host] input injection blocked by compile-time policy\n", &why) == -1);
  why.clear();
  check("a config the host could not load is refused",
        e::e2e_injection_state(kHead + "[native-video-host] failed to load --config file: x (failed to open file)\n", &why) == -1, why);
  why.clear();
  check("an ENABLED anywhere in the same output wins over a disabled line (contradiction)",
        e::e2e_injection_state(kPower + "input injection enabled mode=x\n" + kOff + "\n", &why) == -1);
  why.clear();
  check("silence is not yet a pass", e::e2e_injection_state(kHead, &why) == 0);
  why.clear();
  check("silence until the budget: refused, said why",
        !e::e2e_wait_injection_off([] { return kHead; }, 200, &why) && why.find("never said") != std::string::npos, why);
  why.clear();
  check("the wait passes on the whole mode=none line",
        e::e2e_wait_injection_off([] { return kHead + kOff + "\n"; }, 200, &why));
  check("a later injectionEnabled=1 in the input log is a contradiction",
        e::e2e_injection_contradicted("[input-timing] seq=1 eventKind=6 injectionEnabled=1 ackSent=1"));
  check("injectionEnabled=0 is not", !e::e2e_injection_contradicted("[input-timing] seq=1 eventKind=6 injectionEnabled=0"));
  std::printf("\n%d checks, %d failures\n%s\n", gChecks, gFailures, gFailures == 0 ? "RESULT PASS" : "RESULT FAIL");
  return gFailures == 0 ? 0 : 1;
}

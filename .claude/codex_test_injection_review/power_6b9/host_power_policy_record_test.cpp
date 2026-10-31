// The host power policy, through the product's own HostPowerKeepalive, inside the record-only OS boundary
// (2026-10-28, Codex Finding-3 (1), its conditions 3-6). Compiled with REMOTE60_OS_INPUT_RECORD_ONLY: every
// SetThreadExecutionState / SendInput / PostMessage this test makes the product code request is RECORDED and never
// performed -- this PC's power state, cursor and monitors are untouched (the ON trace included). Pure: no host, no
// session.
//   * the binding: on / off / missing / a typo / absent (the default), the last on the command line wins;
//   * OFF: construction, stream edges with the wake requested, repeats, the end -- ZERO effects;
//   * ON (the product default): the constructor's ES_SYSTEM_REQUIRED, the stream edge's ES_DISPLAY_REQUIRED + the
//     wake (SendInput n=2 +1/-1, the SC_MONITORPOWER broadcast) once per real edge, the release at the end;
//   * the startup lines the harness gate matches, whole.

#include <cstdio>
#include <string>
#include <vector>

#include "host_log.hpp"
#include "host_power_policy.hpp"
#include "e2e_injection_guard.hpp"

#if !defined(REMOTE60_OS_INPUT_RECORD_ONLY)
#error "this test is only ever built record-only: it must never call the real power / input APIs"
#endif

namespace os = remote60::native_poc::os_input;
using remote60::native_poc::HostPowerKeepalive;
using remote60::native_poc::HostPowerPolicy;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
int count(const char* prefix) {
  std::lock_guard<std::mutex> lock(os::record_mu());
  int n = 0;
  for (const auto& r : os::recorded()) n += r.rfind(prefix, 0) == 0 ? 1 : 0;
  return n;
}
void reset() {
  std::lock_guard<std::mutex> lock(os::record_mu());
  os::recorded().clear();
}
HostPowerPolicy bind(std::vector<std::string> args) {
  std::vector<char*> argv;
  static std::string self = "GNLinkStream.exe";
  argv.push_back(self.data());
  for (auto& a : args) argv.push_back(a.data());
  return remote60::native_poc::bind_host_power_policy(static_cast<int>(argv.size()), argv.data());
}
}  // namespace

int main() {
  std::printf("--- the binding ---\n");
  {
    const auto d = bind({"--transport", "udp"});
    check("absent: ON (the product default), not from the command line", d.ok && d.enabled && !d.fromCommandLine);
    const auto off = bind({"--host-power-management", "off"});
    check("off: OFF", off.ok && !off.enabled && off.fromCommandLine);
    const auto on = bind({"--host-power-management", "on"});
    check("on: ON", on.ok && on.enabled && on.fromCommandLine);
    const auto last = bind({"--host-power-management", "on", "--host-power-management", "off"});
    check("the last one wins", last.ok && !last.enabled);
    const auto typo = bind({"--host-power-management", "of"});
    check("a typo refuses the start (no silent default)", !typo.ok && typo.bad == "of");
    const auto missing = bind({"--host-power-management"});
    check("a missing value refuses the start", !missing.ok);
    const auto other = bind({"--host-power-managment", "off"});
    check("a misspelt switch is not this switch: the default (ON) stands -- the harness gate refuses that host",
          other.ok && other.enabled && !other.fromCommandLine);
  }

  std::printf("\n--- OFF: zero effects ---\n");
  reset();
  {
    HostPowerKeepalive p(false);
    p.SetStreaming(true, true);
    p.SetStreaming(true, true);
    p.SetStreaming(false);
    p.SetStreaming(true, true);
  }
  {
    std::lock_guard<std::mutex> lock(os::record_mu());
    check("construction, stream edges with the wake requested, repeats, the end: nothing at all",
          os::recorded().empty(), std::to_string(os::recorded().size()) + " recorded");
  }

  std::printf("\n--- ON (the product default): the requests, recorded, never performed ---\n");
  reset();
  {
    HostPowerKeepalive p(true);
    check("construction: one ES_CONTINUOUS|ES_SYSTEM_REQUIRED request",
          count("api=SetThreadExecutionState flags=0x80000001") == 1);
    p.SetStreaming(true, true);
    check("the stream edge: ES_DISPLAY_REQUIRED added, and the wake (SendInput n=2 +1/-1, the broadcast)",
          count("api=SetThreadExecutionState flags=0x80000003") == 1 && count("api=SendInput n=2 mouse flags=0x1 dx=1") == 1 &&
              count("api=PostMessage") == 1);
    p.SetStreaming(true, true);
    check("a repeated true: nothing more (the edge rule)", count("api=SendInput") == 1 && count("api=PostMessage") == 1);
    p.SetStreaming(false);
    p.SetStreaming(true, false);
    check("off and on again without the wake asked: the states, no second wake",
          count("api=SendInput") == 1 && count("api=SetThreadExecutionState flags=0x80000003") == 2);
  }
  check("the end: the release (ES_CONTINUOUS)", count("api=SetThreadExecutionState flags=0x80000000") == 1);

  std::printf("\n--- the startup lines the gate matches ---\n");
  {
    HostPowerPolicy off;
    off.enabled = false;
    off.fromCommandLine = true;
    const std::string offLine = remote60::native_poc::host_power_policy_line(off);
    check("the OFF line is the gate's line", offLine == remote60::native_poc::e2e::e2e_power_off_line(), offLine);
    std::string why;
    const std::string inj = "[native-video-host] input injection disabled (unsupported mode) mode=none\n";
    check("the gate: injection OFF + power OFF -> pass",
          remote60::native_poc::e2e::e2e_injection_state("[native-video-host] " + offLine + "\n" + inj, &why) == 1, why);
    HostPowerPolicy on;
    check("the gate: injection OFF + power ON (default) -> refused",
          remote60::native_poc::e2e::e2e_injection_state(
              "[native-video-host] " + remote60::native_poc::host_power_policy_line(on) + "\n" + inj, &why) == -1, why);
    check("the gate: injection OFF, no power line (an older build) -> not yet, then refused at the budget",
          remote60::native_poc::e2e::e2e_injection_state(inj, &why) == 0 &&
              !remote60::native_poc::e2e::e2e_wait_injection_off([&] { return inj; }, 150, &why), why);
    check("the gate: a refused power argument -> refused",
          remote60::native_poc::e2e::e2e_injection_state(
              "[native-video-host] invalid --host-power-management value: of (on|off) -- not started\n", &why) == -1, why);
  }

  std::printf("\n%d checks, %d failures\n%s\n", gChecks, gFailures, gFailures == 0 ? "RESULT PASS" : "RESULT FAIL");
  return gFailures == 0 ? 0 : 1;
}

// The host's power management policy (2026-10-28, Codex codex_test_injection_finding.md Finding-3, decision (1)):
// ONE local, startup-only switch for everything HostPowerKeepalive does to this machine's power -- the
// execution-state requests (ES_SYSTEM_REQUIRED from the start, ES_DISPLAY_REQUIRED while streaming, their release
// at the end) and the display wake at a stream start (a synthetic relative mouse move +1/-1 and the SC_MONITORPOWER
// broadcast). Default ON: the product's behaviour is unchanged.
//
//   --host-power-management on|off     (the command line only)
//
// Bound once, before the power object exists and before any of its effects (native_video_host_main.cpp); immutable
// after that. Nothing remote -- the directory, the control channel, a viewer, a runtime tune, a stream restart -- can
// change it: there is no setter. A value other than on/off refuses the start (no silent default). The bound value
// is said in one line at startup ("host power management=on|off ...").
#ifndef REMOTE60_NATIVE_POC_HOST_POWER_POLICY_HPP
#define REMOTE60_NATIVE_POC_HOST_POWER_POLICY_HPP

#include <string>

namespace remote60::native_poc {

struct HostPowerPolicy {
  bool enabled = true;            // the product default
  bool fromCommandLine = false;   // said explicitly on this launch's command line
  bool ok = true;                 // false: an unknown value -- the host refuses to start
  std::string bad;                // that value
};

inline HostPowerPolicy bind_host_power_policy(int argc, char** argv) {
  HostPowerPolicy p;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) != "--host-power-management") continue;
    if (i + 1 >= argc) {
      p.ok = false;
      p.bad = "(missing)";
      return p;
    }
    const std::string v = argv[++i];
    if (v == "on") {
      p.enabled = true;
    } else if (v == "off") {
      p.enabled = false;
    } else {
      p.ok = false;
      p.bad = v;
      return p;
    }
    p.fromCommandLine = true;  // the last one wins, like the other switches
  }
  return p;
}

// The one startup line (the harness matches it whole).
inline std::string host_power_policy_line(const HostPowerPolicy& p) {
  if (p.enabled) {
    return std::string("host power management=on (local startup policy, ") +
           (p.fromCommandLine ? "--host-power-management on" : "default") +
           "): execution state kept, display wake at a stream start";
  }
  return "host power management=off (local startup policy, --host-power-management off): no execution-state "
         "request, no display wake, no monitor-power broadcast";
}

}  // namespace remote60::native_poc

#endif

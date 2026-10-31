R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/'


def edit(rel, reps):
    p = R + rel
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (rel, a[:80], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', rel)


edit('src/host_power_policy_record_test.cpp', [
    ('#include "host_log.hpp"\n', '#include "host_args.hpp"\n#include "host_log.hpp"\n'),
    ('''HostPowerPolicy bind(std::vector<std::string> args) {
  std::vector<char*> argv;
  static std::string self = "GNLinkStream.exe";
  argv.push_back(self.data());
  for (auto& a : args) argv.push_back(a.data());
  return remote60::native_poc::bind_host_power_policy(static_cast<int>(argv.size()), argv.data());
}''', '''// r5 P2-1: the host's ONE parser (parse_args), as main() uses it before the power object exists
HostPowerPolicy bind(std::vector<std::string> args) {
  std::vector<char*> argv;
  static std::string self = "GNLinkStream.exe";
  argv.push_back(self.data());
  for (auto& a : args) argv.push_back(a.data());
  return remote60::native_poc::parse_args(static_cast<int>(argv.size()), argv.data()).hostPower;
}'''),
    ('''    const auto other = bind({"--host-power-managment", "off"});
    check("a misspelt switch is not this switch: the default (ON) stands -- the harness gate refuses that host",
          other.ok && other.enabled && !other.fromCommandLine);
  }''', '''    const auto other = bind({"--host-power-managment", "off"});
    check("a misspelt switch is not this switch: the default (ON) stands -- the harness gate refuses that host",
          other.ok && other.enabled && !other.fromCommandLine);
    // r5 P2-1 (Codex): another switch's operand is never this switch
    const auto titleOnly = bind({"--capture-window-title", "--host-power-management"});
    check("P2-1: a window title \\"--host-power-management\\" is the title: no error, the default stands",
          titleOnly.ok && titleOnly.enabled && !titleOnly.fromCommandLine);
    const auto titleThenOff = bind({"--capture-window-title", "--host-power-management", "off"});
    check("P2-1: ...followed by a stray \\"off\\": still no power switch at all -- the default (ON) stands, not OFF",
          titleThenOff.ok && titleThenOff.enabled && !titleThenOff.fromCommandLine);
    const auto realAfterTitle = bind({"--capture-window-title", "--host-power-management", "--host-power-management", "off"});
    check("P2-1: the title, then the real switch: OFF", realAfterTitle.ok && !realAfterTitle.enabled && realAfterTitle.fromCommandLine);
    const auto offOperandOfBindAddress = bind({"--bind-address", "--host-power-management", "on"});
    check("P2-1: as the operand of --bind-address too: not the switch", offOperandOfBindAddress.ok && !offOperandOfBindAddress.fromCommandLine);
    const auto offThenSeconds = bind({"--host-power-management", "off", "--seconds", "3"});
    check("P2-1: its own operand only (the next switch is read as itself)", offThenSeconds.ok && !offThenSeconds.enabled);
  }'''),
    ('''    check("the gate: a refused power argument -> refused",''', '''    check("P2-2: power OFF, input OFF, then power ON later in the same launch -> refused (a contradiction)",
          remote60::native_poc::e2e::e2e_injection_state(
              "[native-video-host] " + offLine + "\\n" + inj + "[native-video-host] " +
                  remote60::native_poc::host_power_policy_line(on) + "\\n", &why) == -1, why);
    check("P2-2: input OFF, then input ENABLED later -> refused",
          remote60::native_poc::e2e::e2e_injection_state(
              "[native-video-host] " + offLine + "\\n" + inj + "[native-video-host] input injection enabled mode=background_message\\n",
              &why) == -1, why);
    check("P2-2: input OFF, then another mode later -> refused",
          remote60::native_poc::e2e::e2e_injection_state(
              "[native-video-host] " + offLine + "\\n" + inj +
                  "[native-video-host] input injection disabled (unsupported mode) mode=bogus\\n", &why) == -1, why);
    check("P2-2: the same OFF lines twice -> still a pass",
          remote60::native_poc::e2e::e2e_injection_state(
              "[native-video-host] " + offLine + "\\n" + inj + "[native-video-host] " + offLine + "\\n" + inj, &why) == 1, why);
    check("the gate: a refused power argument -> refused",'''),
])

edit('src/e2e_injection_guard_test.cpp', [
    ('''  check("off by compile-time policy is not taken for it either",
        e::e2e_injection_state(kHead + "x [native-video-host] input injection blocked by compile-time policy\\n", &why) == 0);''',
     '''  check("off by compile-time policy is not taken for it either (r5 P2-2: refused -- not the explicit OFF line)",
        e::e2e_injection_state(kHead + "x [native-video-host] input injection blocked by compile-time policy\\n", &why) == -1);'''),
])

edit('CMakeLists.txt', [
    ('''add_executable(remote60_host_power_policy_record_test src/host_power_policy_record_test.cpp)''',
     '''add_executable(remote60_host_power_policy_record_test src/host_power_policy_record_test.cpp src/host_args.cpp)'''),
])

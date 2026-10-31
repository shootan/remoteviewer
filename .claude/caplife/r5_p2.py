import re

R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'


def load(name):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    return p, s.replace('\r\n', '\n'), crlf


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', p.rsplit('/', 1)[-1])


def one(s, a, b):
    assert s.count(a) == 1, (a[:90], s.count(a))
    return s.replace(a, b)


# ---- Args carries the policy
p, s, crlf = load('host_args.hpp')
s = one(s, '#include "env_util.hpp"\n', '#include "env_util.hpp"\n#include "host_power_policy.hpp"\n')
s = one(s, '  bool enableInputInjection = true;\n', '''  bool enableInputInjection = true;
  // 2026-10-28 (Codex Finding-3 (1), r5 P2-1): --host-power-management on|off, read by parse_args with the same
  // token / arity rules as every other switch (an operand of another switch is never it). main() parses BEFORE the
  // power object exists. Not ok -> the host refuses to start.
  HostPowerPolicy hostPower;
''')
save(p, s, crlf)

# ---- parse_args reads it (replacing the r4 skip)
p, s, crlf = load('host_args.cpp')
s = one(s, '''    } else if (k == "--host-power-management" && i + 1 < argc) {
      ++i;  // bound before parse_args, before the power object exists (host_power_policy.hpp); its value is not an arg
''', '''    } else if (k == "--host-power-management") {
      // r5 P2-1: this switch's own operand, by the same rule as the others (the next token); the last one wins
      if (i + 1 >= argc) {
        a.hostPower.ok = false;
        a.hostPower.bad = "(missing)";
      } else {
        const std::string v = argv[++i];
        if (v == "on" || v == "off") {
          a.hostPower.enabled = (v == "on");
          a.hostPower.fromCommandLine = true;
        } else {
          a.hostPower.ok = false;
          a.hostPower.bad = v;
        }
      }
''')
save(p, s, crlf)

# ---- host_power_policy.hpp: no own argv scan any more
p, s, crlf = load('host_power_policy.hpp')
i = s.index('inline HostPowerPolicy bind_host_power_policy(')
j = s.index('// The one startup line (the harness matches it whole).')
s = s[:i] + s[j:]
s = one(s, '''// Bound once, before the power object exists and before any of its effects (native_video_host_main.cpp); immutable
// after that.''', '''// Read by parse_args (host_args.cpp) with the same token / arity rules as every other switch -- an operand of another
// switch (e.g. a window title "--host-power-management") is never taken for it (r5 P2-1) -- and bound once, before
// the power object exists and before any of its effects (native_video_host_main.cpp parses first); immutable after
// that.''')
save(p, s, crlf)

# ---- main: parse first, then the power object from args
p, s, crlf = load('native_video_host_main.cpp')
s = one(s, '''  const remote60::native_poc::HostPowerPolicy powerPolicy = remote60::native_poc::bind_host_power_policy(argc, argv);
  if (!powerPolicy.ok) {''', '''  // r5 P2-1: the ONE parser (parse_args, the same token / arity rules for every switch), before any power effect.
  // It reads arguments and a --config profile only; nothing in it acts on the machine.
  const Args args = parse_args(argc, argv);
  const remote60::native_poc::HostPowerPolicy& powerPolicy = args.hostPower;
  if (!powerPolicy.ok) {''')
s = one(s, '''  startup_process_setup();

  const Args args = parse_args(argc, argv);
''', '''  startup_process_setup();

''')
save(p, s, crlf)

# ---- P2-2: the guard refuses any contradiction among a launch's state lines
p, s, crlf = load('e2e_injection_guard.hpp')
s = one(s, '''inline int e2e_whole_line(const std::string& out, const std::string& prefix, const std::string& want) {
  const size_t at = out.find(prefix);
  if (at == std::string::npos) return 0;
  const size_t end = at + want.size();
  if (out.compare(at, want.size(), want) == 0) {
    if (end >= out.size()) return 0;
    return (out[end] == '\\r' || out[end] == '\\n') ? 1 : -1;
  }
  // a shorter tail still being written is "not yet"; anything that differs is refused
  if (out.size() - at < want.size() && want.compare(0, out.size() - at, out, at, out.size() - at) == 0) return 0;
  return -1;
}''', '''// r5 P2-2: EVERY line with that prefix, not only the first: one that differs anywhere -- before or after a matching
// one -- refuses the launch (a later "power ON" after "power OFF" is a contradiction, not noise).
inline int e2e_whole_line(const std::string& out, const std::string& prefix, const std::string& want) {
  int seen = 0;
  for (size_t at = out.find(prefix); at != std::string::npos; at = out.find(prefix, at + prefix.size())) {
    const size_t end = at + want.size();
    if (out.compare(at, want.size(), want) == 0) {
      if (end >= out.size()) return seen > 0 ? seen : 0;  // the last line still being written
      if (out[end] != '\\r' && out[end] != '\\n') return -1;
      seen = 1;
      continue;
    }
    // a shorter tail still being written (the last occurrence) is "not yet"; anything that differs is refused
    if (out.size() - at < want.size() && want.compare(0, out.size() - at, out, at, out.size() - at) == 0) return seen > 0 ? seen : 0;
    return -1;
  }
  return seen;
}''')
# the injection half: every "input injection " line must be the exact OFF line
s = one(s, '''  const size_t at = out.find("input injection disabled");
  if (at != std::string::npos) {''', '''  // r5 P2-2: every injection-state line of this launch must be the explicit OFF line (a contradiction anywhere refuses)
  {
    const int every = e2e_whole_line(out, "input injection ", e2e_injection_off_line());
    if (every < 0) {
      if (why) *why = "the host's input injection states contradict the explicit OFF";
      return -1;
    }
  }
  const size_t at = out.find("input injection disabled");
  if (at != std::string::npos) {''')
save(p, s, crlf)

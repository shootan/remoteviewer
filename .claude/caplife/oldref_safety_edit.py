"""old-ref r1 SAFETY port (Codex codex_old_apk_lane_peer_lost_decision_r1.md: "로컬 input/power OFF 효과 경계"): the
0.2.163 source host (fae0e1f) gets ONLY the candidate's local power switch -- host_power_policy.hpp verbatim from
53fced4, --host-power-management on|off read by parse_args, HostPowerKeepalive(enabled) gating every effect, and the
parse moved BEFORE the keepalive object exists (fae0e1f built it first: its constructor asked for the execution state
before any argument was read). Nothing else of ba2eaf2 / the candidate."""
import subprocess

WT = r'C:\work\gnlink-wt\old-ref-163'
SRC = WT + r'\apps\native_poc\src'


def load(p):
    raw = open(p, 'rb').read()
    return raw.decode('utf-8').replace('\r\n', '\n'), b'\r\n' in raw


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


def rep(s, a, b):
    assert s.count(a) == 1, (a[:100], s.count(a))
    return s.replace(a, b)


# 1. host_power_policy.hpp, byte for byte the candidate's (the shared guard matches its line whole)
b = subprocess.run(['git', '-C', WT, 'show', '53fced4:apps/native_poc/src/host_power_policy.hpp'], capture_output=True,
                   check=True).stdout
open(SRC + r'\host_power_policy.hpp', 'wb').write(b)

# 2. Args gains the policy
p = SRC + r'\host_args.hpp'
s, crlf = load(p)
s = rep(s, '#include "env_util.hpp"\n', '#include "env_util.hpp"\n#include "host_power_policy.hpp"  // old-ref r1 safety port\n')
s = rep(s, 'struct Args {\n', 'struct Args {\n  // old-ref r1 safety port (53fced4 ba2eaf2): --host-power-management on|off, read by parse_args\n'
        '  HostPowerPolicy hostPower;\n')
save(p, s, crlf)

# 3. parse_args reads it (the candidate's rule: its own operand, the next token; on|off, else refuse)
p = SRC + r'\host_args.cpp'
s, crlf = load(p)
s = rep(s, '''    if (k == "--bind-port" && i + 1 < argc) {
      // Accepts one port or an ordered comma-separated fallback list.''', '''    if (k == "--host-power-management") {
      // old-ref r1 safety port (53fced4 host_args.cpp): this switch's own operand, the next token; the last one wins
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
      continue;
    }
    if (k == "--bind-port" && i + 1 < argc) {
      // Accepts one port or an ordered comma-separated fallback list.''')
save(p, s, crlf)

# 4. HostPowerKeepalive(enabled): nothing on this machine when OFF (the execution state, its release, the wake)
p = SRC + r'\host_log.hpp'
s, crlf = load(p)
s = rep(s, '''class HostPowerKeepalive {
 public:
  HostPowerKeepalive() {
    Apply(false);
  }

  ~HostPowerKeepalive() {
    (void)SetThreadExecutionState(ES_CONTINUOUS);
  }
''', '''class HostPowerKeepalive {
 public:
  // old-ref r1 safety port (53fced4): bound to the local startup policy BEFORE the object exists; OFF -> no
  // execution-state request, no release, no display wake (synthetic mouse move / monitor-power broadcast).
  explicit HostPowerKeepalive(bool enabled) : enabled_(enabled) {
    if (enabled_) Apply(false);
  }

  ~HostPowerKeepalive() {
    if (enabled_) (void)SetThreadExecutionState(ES_CONTINUOUS);
  }

  HostPowerKeepalive(const HostPowerKeepalive&) = delete;
  HostPowerKeepalive& operator=(const HostPowerKeepalive&) = delete;
''')
s = rep(s, '''    if (streaming_ == streaming) return;
    streaming_ = streaming;
    Apply(streaming);''', '''    if (streaming_ == streaming) return;
    streaming_ = streaming;
    if (!enabled_) return;  // old-ref r1: the local policy is OFF -- a call site's request changes nothing here
    Apply(streaming);''')
s = rep(s, '''  bool streaming_ = false;
};''', '''  const bool enabled_;
  bool streaming_ = false;
};''')
save(p, s, crlf)

# 5. main: parse first, the line, refuse an unknown value, then the keepalive with the policy
p = SRC + r'\native_video_host_main.cpp'
s, crlf = load(p)
s = rep(s, '''  HostPowerKeepalive powerKeepalive;
  // Outlive every capture/encoder/session owner, including their destructors after shutdown_host.
  // A GPU resource Release may block too; disarming before those destructors left a teardown gap.
  WatchdogState watchdog;
  MainLoopWatchdogThread mainLoopWatchdog;
  startup_process_setup();

  const Args args = parse_args(argc, argv);
''', '''  // old-ref r1 safety port (53fced4): the ONE parser first -- it reads arguments and a --config profile only -- so
  // the power policy is bound before the keepalive object exists and before any of its effects.
  const Args args = parse_args(argc, argv);
  const remote60::native_poc::HostPowerPolicy& powerPolicy = args.hostPower;
  if (!powerPolicy.ok) {
    std::cerr << "[native-video-host] invalid --host-power-management value: " << powerPolicy.bad
              << " (on|off) -- not started\\n";
    return 65;
  }
  std::cout << "[native-video-host] " << remote60::native_poc::host_power_policy_line(powerPolicy) << "\\n";
  HostPowerKeepalive powerKeepalive(powerPolicy.enabled);
  // Outlive every capture/encoder/session owner, including their destructors after shutdown_host.
  // A GPU resource Release may block too; disarming before those destructors left a teardown gap.
  WatchdogState watchdog;
  MainLoopWatchdogThread mainLoopWatchdog;
  startup_process_setup();
''')
save(p, s, crlf)
print('ok')

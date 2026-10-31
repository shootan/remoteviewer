NL = chr(92) + 'n'
base = 'C:\\work\\gnlink-wt\\ka-pc\\apps\\native_poc\\src\\'

def edit(name, pairs):
    p = base + name
    b = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in b
    s = b.replace('\r\n', '\n')
    for a, c in pairs:
        assert s.count(a) == 1, (name, s.count(a), a[:90])
        s = s.replace(a, c)
    open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

# ---------------------------------------------------------------- managed e2e
edit('ka_pc_managed_e2e_test.cpp', [
('''#include "e2e_isolation.hpp"''', '''#include "e2e_injection_guard.hpp"
#include "e2e_isolation.hpp"'''),
('''                       L" --directory-cache \\"" + dir + L"cache.json\\" --seconds 900 --input-injection-mode none",''',
 '''                       L" --directory-cache \\"" + dir + L"cache.json\\" --seconds 900" + e2e::e2e_injection_off_arg(dir),'''),
('''  // fail closed: no viewer connects to a host that would inject input into this PC
  if (!hostA.WaitFor([](const std::string& t) { return t.find("input injection disabled") != std::string::npos; }, 10000)) {
    std::printf("FAIL  host KA-A did not say 'input injection disabled' -- stopping before any viewer connects''' + NL + '''");
    return 1;
  }''', '''  // The shared test-host guard (ka-r3 6b9cf79, e2e_injection_guard.hpp): THIS launch must have said input injection
  // OFF and host power management OFF, both whole lines, before any viewer exists -- else INVALID, exit 2.
  {
    std::string why;
    if (!e2e::e2e_wait_injection_off([&hostA] { return hostA.Text(); }, 20000, &why))
      e2e::e2e_injection_setup_failed("host KA-A", why);
  }'''),
('''  hostA.End();
  if (haveOld) {''', '''  hostA.End();
  // The published 0.2.163 Host is NOT started any more (the shared guard, Codex Finding-3 (4)): an older binary does not
  // know --host-power-management and would never say it is off. This case is NOT RUN -- unverified from here on.
  if (haveOld) std::printf("NOT RUN  the published 0.2.163 Host (an older binary is never started on this PC)''' + NL + '''");
  if ((false)) {'''),
('''  if (haveOld && !idOld.empty()) {''', '''  if ((false) && !idOld.empty()) {'''),
('''  hostOld.End();
  server.End();
  std::printf("''' + NL + '''ka_pc_managed_e2e_test: %s''' + NL + '''", gFailures == 0 ? "ALL PASS" : (std::to_string(gFailures) + " FAILED").c_str());
  return gFailures == 0 ? 0 : 1;''', '''  hostOld.End();
  server.End();
  // rule 4 of the guard: a host that took input with injection on makes the run INVALID
  if (e2e::e2e_injection_contradicted(hostA.Text())) {
    std::printf("FAIL  host KA-A logged input with injection on''' + NL + '''RESULT: INVALID''' + NL + '''");
    return 2;
  }
  std::printf("''' + NL + '''ka_pc_managed_e2e_test: %s''' + NL + '''", gFailures == 0 ? "ALL PASS" : (std::to_string(gFailures) + " FAILED").c_str());
  return gFailures == 0 ? 0 : 1;'''),
])

# ---------------------------------------------------------------- shell e2e
edit('ka_pc_shell_e2e_test.cpp', [
('''            L" --input-injection-mode none",''', '''            e2e::e2e_injection_off_arg(dir),'''),
('''    // fail closed: no viewer connects to a host that would inject input into this PC
    const uint64_t injUntil = GetTickCount64() + 10000;
    while (GetTickCount64() < injUntil && h->proc.Text().find("input injection disabled") == std::string::npos &&
           h->proc.Text().find("input injection enabled") == std::string::npos)
      Sleep(50);
    if (h->proc.Text().find("input injection disabled") == std::string::npos) {
      std::printf("FAIL  host %s did not say 'input injection disabled' -- stopping before any viewer connects''' + NL + '''", name);
      return 1;
    }''', '''    // The shared test-host guard (ka-r3 6b9cf79): THIS launch said input injection OFF and host power management
    // OFF, both whole lines, before any viewer exists -- else INVALID, exit 2 (its hosts die with their jobs).
    {
      std::string why;
      TestHost* th = h.get();
      if (!e2e::e2e_wait_injection_off([th] { return th->proc.Text(); }, 20000, &why))
        e2e::e2e_injection_setup_failed(name, why);
    }'''),
('''  for (auto& h : hosts) h->proc.End();
  server.End();''', '''  // rule 4 of the guard: a host that took input with injection on makes the run INVALID
  for (auto& h : hosts) {
    if (e2e::e2e_injection_contradicted(h->proc.Text())) {
      std::printf("FAIL  host %s logged input with injection on''' + NL + '''RESULT: INVALID''' + NL + '''", h->name.c_str());
      for (auto& k : hosts) k->proc.End();
      server.End();
      return 2;
    }
  }
  for (auto& h : hosts) h->proc.End();
  server.End();'''),
])
print('ok')

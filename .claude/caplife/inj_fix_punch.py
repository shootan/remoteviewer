p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_punch_reply_e2e_test.cpp'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep("""std::wstring isolation_args(const std::wstring& stagingDir) {
  return L" --directory-cache \\"" + stagingDir + L"host_cache.json\\"";
}""", """// 2026-10-28 (verifier, after worker2's finding): GNLinkStream injects input by default (host_args.hpp
// enableInputInjection = true) -- a test host must not drive THIS PC's input. Every host here gets a config with
// injection off, and a test client connects only after the host itself said "input injection disabled" (StartHost).
std::wstring isolation_args(const std::wstring& stagingDir) {
  return L" --directory-cache \\"" + stagingDir + L"host_cache.json\\"" + L" --config \\"" + stagingDir +
         L"host-config.json\\"";
}

bool write_injection_off_config(const std::wstring& stagingDir) {
  std::ofstream cfg(stagingDir + L"host-config.json", std::ios::binary | std::ios::trunc);
  cfg << "{\\"enableInputInjection\\": false}\\n";
  return static_cast<bool>(cfg);
}""")

rep("""  const std::wstring roleArgs = prelogin ? L" --prelogin" : L" --transport udp --codec h264";""",
    """  if (!write_injection_off_config(host->dir)) {
    std::printf("FAIL  %s: the injection-off config could not be written -- the host is not started\\n", tag);
    return false;
  }
  const std::wstring roleArgs = prelogin ? L" --prelogin" : L" --transport udp --codec h264";""")

rep("""  if (prelogin) return host->WaitFor("[native-video-host][prelogin]", 20000);""",
    """  if (prelogin) return host->WaitFor("[native-video-host][prelogin]", 20000);
  // fail closed: no test client reaches a host that has not said its input injection is off
  const bool injSaid = host->WaitFor("input injection disabled", 20000);
  const bool injOn = host->log().find("input injection enabled") != std::string::npos;
  check("the host says its input injection is off (before any client connects)", injSaid && !injOn,
        injOn ? "it said ENABLED -- stopped" : (injSaid ? "" : "it never said"));
  if (!injSaid || injOn) {
    TerminateJobObject(host->job, 1);
    return false;
  }""")
if '#include <fstream>' not in s:
    rep('#include <string>\n', '#include <fstream>\n#include <string>\n')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')

import sys

R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'
NS = 'remote60::native_poc::e2e::'


def edit(name, reps):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, a[:100], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name)


which = sys.argv[1]

if which == 'android_session_reboot':
    edit('android_session_reboot_e2e_test.cpp', [
        ('#include "e2e_isolation.hpp"\n', '#include "e2e_isolation.hpp"\n#include "e2e_injection_guard.hpp"\n'),
        ("""    std::wstring cmd = L"\\"" + dir + L"GNLinkStream.exe\\" --transport udp --codec h264 --bind-address 127.0.0.1" +
                       L" --bind-port " + std::to_wstring(mediaPort) + L" --seconds 90";""",
         """    // input injection off (e2e_injection_guard.hpp): this test's input must never reach this PC's desktop
    const bool injCfg = """ + NS + """e2e_write_injection_off_config(dir);
    std::wstring cmd = L"\\"" + dir + L"GNLinkStream.exe\\" --transport udp --codec h264 --bind-address 127.0.0.1" +
                       L" --bind-port " + std::to_wstring(mediaPort) + L" --seconds 90" +
                       """ + NS + """e2e_injection_off_arg(dir);"""),
        ("""    launched = isoOk && CreateProcessAsUserW(""", """    launched = isoOk && injCfg && CreateProcessAsUserW("""),
        ("""  if (launched && safe) {
    ClientSessionConnectArgs args;""", """  // fail closed: no session to a host that has not said its input injection is off
  std::string injWhy;
  const bool injOff = launched && """ + NS + """e2e_wait_injection_off(
      [&] { return """ + NS + """e2e_read_text_file(hostLogPath); }, 20000, &injWhy);
  check("the host says its input injection is off (before the phone connects)", injOff, injWhy);

  if (launched && safe && injOff) {
    ClientSessionConnectArgs args;"""),
    ])

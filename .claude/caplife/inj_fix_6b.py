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


INC = ('#include "e2e_isolation.hpp"\n', '#include "e2e_isolation.hpp"\n#include "e2e_injection_guard.hpp"\n')


def wait_block(log_expr, indent='  '):
    return (f"""{indent}// fail closed (e2e_injection_guard.hpp): no client reaches a host that has not said its input injection is off
{indent}std::string injWhy;
{indent}const bool injOff = launched && {NS}e2e_wait_injection_off(
{indent}    [&] {{ return {NS}e2e_read_text_file({log_expr}); }}, 20000, &injWhy);
{indent}check("the host says its input injection is off (before any client connects)", injOff, injWhy);
{indent}if (launched && !injOff) TerminateJobObject(job, 1);  // stopped before anything is sent to it
""")


# ---- bwe_observe: its host already writes host.log
edit('bwe_observe_e2e_test.cpp', [
    INC,
    ("""                       L" --seconds 240" + remote60::native_poc::e2e::e2e_capture_window_args(target.title);""",
     """                       L" --seconds 240" + remote60::native_poc::e2e::e2e_capture_window_args(target.title) +
                       """ + NS + """e2e_injection_off_arg(dir);
    const bool injCfg = """ + NS + """e2e_write_injection_off_config(dir);"""),
    ("""    launched = isoOk &&
               CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr,
                              hostLog != INVALID_HANDLE_VALUE,""",
     """    launched = isoOk && injCfg &&
               CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr,
                              hostLog != INVALID_HANDLE_VALUE,"""),
    ("""  check("the host started", launched);

  ViewerSide viewer;""", """  check("the host started", launched);
""" + wait_block('hostLogPath') + """  if (!injOff) launched = false;

  ViewerSide viewer;"""),
])

# ---- host_control_deadline: stdout to a log of its own, then the same
edit('host_control_deadline_e2e_test.cpp', [
    INC,
    ("""  std::wstring cmd = L"\\"" + dir + L"GNLinkStream.exe\\" --bind-address 127.0.0.1 --bind-port " +
                     std::to_wstring(mediaPort) + L" --control-port " +
                     std::to_wstring(controlPort) + L" --seconds 120";
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);""", """  const bool injCfg = """ + NS + """e2e_write_injection_off_config(dir);
  std::wstring cmd = L"\\"" + dir + L"GNLinkStream.exe\\" --bind-address 127.0.0.1 --bind-port " +
                     std::to_wstring(mediaPort) + L" --control-port " +
                     std::to_wstring(controlPort) + L" --seconds 120" + """ + NS + """e2e_injection_off_arg(dir);
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  // its stdout to a log in the staging directory: where it says whether it injects input (read below)
  const std::wstring hostLogPath = dir + L"host.log";
  SECURITY_ATTRIBUTES logSa{};
  logSa.nLength = sizeof(logSa);
  logSa.bInheritHandle = TRUE;
  HANDLE hostLog = CreateFileW(hostLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &logSa,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hostLog != INVALID_HANDLE_VALUE) {
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hostLog;
    si.hStdError = hostLog;
  }"""),
    ("""  const bool launched =
      isoOk && CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                     isoEnv.data(), dir.c_str(), &si, &hostPi) != 0;""",
     """  const bool launched =
      isoOk && injCfg && hostLog != INVALID_HANDLE_VALUE &&
      CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                     CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                     isoEnv.data(), dir.c_str(), &si, &hostPi) != 0;
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);  // the host holds its own copy"""),
    ("""  check("the host started", launched);

  SOCKET media = INVALID_SOCKET;""", """  check("the host started", launched);
""" + wait_block('hostLogPath') + """
  SOCKET media = INVALID_SOCKET;"""),
])

# ---- host_two_dispatcher: the same
edit('host_two_dispatcher_e2e_test.cpp', [
    INC,
    ("""                       std::to_wstring(controlPort) + L" --seconds 90";
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);""", """                       std::to_wstring(controlPort) + L" --seconds 90" + """ + NS + """e2e_injection_off_arg(dir);
    injCfg = """ + NS + """e2e_write_injection_off_config(dir);
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    // its stdout to a log in the staging directory: where it says whether it injects input (read below)
    SECURITY_ATTRIBUTES logSa{};
    logSa.nLength = sizeof(logSa);
    logSa.bInheritHandle = TRUE;
    hostLog = CreateFileW(hostLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &logSa,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hostLog != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = hostLog;
      si.hStdError = hostLog;
    }"""),
    ("""    launched = isoOk &&
               CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                              CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                              isoEnv.data(), dir.c_str(), &si, &hostPi) != 0;""",
     """    launched = isoOk && injCfg && hostLog != INVALID_HANDLE_VALUE &&
               CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                              CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                              isoEnv.data(), dir.c_str(), &si, &hostPi) != 0;
    if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);  // the host holds its own copy"""),
    ("""  bool launched = false;
  if (staged) {""", """  bool launched = false;
  bool injCfg = false;
  HANDLE hostLog = INVALID_HANDLE_VALUE;
  const std::wstring hostLogPath = dir + L"host.log";
  if (staged) {"""),
    ("""  check("the host started", launched);

  NullSink sink;""", """  check("the host started", launched);
""" + wait_block('hostLogPath') + """  if (!injOff) launched = false;

  NullSink sink;"""),
])

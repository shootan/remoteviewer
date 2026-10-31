import re

p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_fatal_exception_test.cpp'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')
NS = 'remote60::native_poc::e2e::'


def rep(a, b, n=1):
    global s
    assert s.count(a) == n, (a[:90], s.count(a))
    s = s.replace(a, b)


# ---- F-a: a port is secured or the run is INVALID before any launch
i = s.index('uint16_t free_udp_port() {')
j = s.index('}\n', s.index('return ntohs(a.sin_port);', i)) + 2
s = s[:i] + '''// r6 F-a: a loopback UDP port nobody holds, or 0 -- every step checked (socket, bind, getsockname). A 0 must never
// reach a host: --bind-port 0 leaves its candidate list empty and the host falls back to the product's 43000.
uint16_t free_udp_port() {
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) return 0;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  uint16_t port = 0;
  int n = sizeof(a);
  if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0 &&
      getsockname(s, reinterpret_cast<sockaddr*>(&a), &n) == 0) {
    port = ntohs(a.sin_port);
  }
  closesocket(s);
  return port;
}

// The port, or the run ends INVALID here -- before any child of this launch exists.
uint16_t port_or_invalid(const char* where) {
  const uint16_t port = free_udp_port();
  if (port != 0) return port;
  std::printf("FAIL  SETUP %s: no loopback port could be secured -- not started (a 0 would fall back to the product's "
              "port)\\nRESULT: INVALID\\n", where);
  std::fflush(stdout);
  std::_Exit(2);
}

// r6 F-c: a child of this test ended through its own handle -- true only when the wait SAW it end (bounded). An end
// not confirmed is a FAIL, said as such; nothing is called "ended" on the strength of the request alone.
bool end_child_confirmed(HANDLE process, UINT code) {
  if (!process) return false;
  if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) return true;
  const BOOL asked = TerminateProcess(process, code);
  if (WaitForSingleObject(process, 10000) == WAIT_OBJECT_0) return true;
  std::printf("FAIL  a child of this test: terminate %s, its end NOT confirmed within 10 s\\n", asked ? "asked" : "failed");
  ++gFailures;
  return false;
}
''' + s[j:]
rep('''                     L" --bind-port " + std::to_wstring(free_udp_port()) + L" --control-port " + std::to_wstring(free_udp_port()) +
                     L" --seconds 3"''', '''                     L" --bind-port " + std::to_wstring(port_or_invalid("run")) + L" --control-port " +
                     std::to_wstring(port_or_invalid("run")) + L" --seconds 3"''')
rep('''  const uint16_t port = free_udp_port();
  const std::wstring log = gRun + L"fatal_" + tag + L".log";''', '''  const uint16_t port = port_or_invalid("run_with_client");
  const std::wstring log = gRun + L"fatal_" + tag + L".log";''')
rep('''                     L" --bind-port " + std::to_wstring(port) + L" --control-port " + std::to_wstring(free_udp_port()) +''',
    '''                     L" --bind-port " + std::to_wstring(port) + L" --control-port " + std::to_wstring(port_or_invalid("run_with_client")) +''')
rep('''    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_BIND_PORT", std::to_wstring(free_udp_port()).c_str());''',
    '''    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_BIND_PORT", std::to_wstring(port_or_invalid("prelogin")).c_str());''')

# ---- F-c: every terminate + wait through the confirming helper
n_before = len(re.findall(r'TerminateProcess\(pi\.hProcess, [^)]*\);\n[ \t]*WaitForSingleObject\(pi\.hProcess, 5000\);', s))
s = re.sub(r'TerminateProcess\(pi\.hProcess, ([^)]*)\);\n([ \t]*)WaitForSingleObject\(pi\.hProcess, 5000\);',
           r'(void)end_child_confirmed(pi.hProcess, \1);  // r6 F-c', s)
print('terminate+wait pairs replaced', n_before)
assert n_before == 4, n_before

# ---- F-b: the --prelogin worker's timeout: its end confirmed, else its logs kept
rep('''      ended = WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0;
      if (ended) GetExitCodeProcess(pi.hProcess, &pcode);
      else TerminateProcess(pi.hProcess, 99);''', '''      ended = WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0;
      if (ended) GetExitCodeProcess(pi.hProcess, &pcode);
      else workerGone = end_child_confirmed(pi.hProcess, 99);  // r6 F-b: confirmed, or its logs are kept below''')
rep('''    PROCESS_INFORMATION pi{};
    DWORD pcode = 0xFFFFFFFFu;
    bool ended = false;''', '''    PROCESS_INFORMATION pi{};
    DWORD pcode = 0xFFFFFFFFu;
    bool ended = false;
    bool workerGone = true;  // r6 F-b: false = its end not confirmed -> nothing of it is removed''')
rep('''    // r5 F1: the one file it writes there, then the folders made for it -- each alone, none recursively
    delete_known_file(logs + L"\\\\prelogin_worker.log");
    remove_made_folder(logs);
    remove_made_folder(root + L"\\\\GNLink");
    remove_made_folder(root);''', '''    // r5 F1: the one file it writes there, then the folders made for it -- each alone, none recursively.
    // r6 F-b: only once the worker's end is confirmed; otherwise everything of it is kept (said), cleanup deferred
    if ((ended || workerGone)) {
      delete_known_file(logs + L"\\\\prelogin_worker.log");
      remove_made_folder(logs);
      remove_made_folder(root + L"\\\\GNLink");
      remove_made_folder(root);
    } else {
      std::printf("NOTE  the worker's end is not confirmed: its log root and log are kept, cleanup deferred: %ls\\n", root.c_str());
      gKeepPreloginLog = true;
    }''')
rep('''  delete_known_file(gRun + L"fatal_prelogin.log");''', '''  if (!gKeepPreloginLog) delete_known_file(gRun + L"fatal_prelogin.log");  // r6 F-b: kept if its worker's end is unconfirmed''')
rep('''std::wstring gRun;''', '''std::wstring gRun;
bool gKeepPreloginLog = false;  // r6 F-b''')

# ---- G1 in fatal: the whole log judged again at each case's end
rep('''    closesocket(s);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }''', '''    closesocket(s);
    // r6 G1: the whole launch output judged again at the end (a later contradiction -> this case INVALID)
    {
      std::string why;
      if (code != kInjectionInvalid && ''' + NS + '''e2e_injection_contradicted(read_all(log), &why)) {
        std::printf("FAIL  RUN %ls: input / power state contradicted during the run (%s)\\n", tag.c_str(), why.c_str());
        code = kInjectionInvalid;
      }
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }''')
rep('''  check("std: the host said its power management OFF (r5 F4, the record)", said_power_off(out));''',
    '''  check("std: the host said its power management OFF (r5 F4, the record)", said_power_off(out));
  check("std: no input / power contradiction in its whole output (r6 G1)", !''' + NS + '''e2e_injection_contradicted(out));''')
rep('''  check("winrt: the host said its power management OFF (r5 F4, the record)", said_power_off(out));''',
    '''  check("winrt: the host said its power management OFF (r5 F4, the record)", said_power_off(out));
  check("winrt: no input / power contradiction in its whole output (r6 G1)", !''' + NS + '''e2e_injection_contradicted(out));''')
rep('''  check("normal control: the host said input injection OFF and power management OFF, both whole (r5 F4)",''',
    '''  check("normal control: no input / power contradiction in its whole output (r6 G1)", !''' + NS + '''e2e_injection_contradicted(out));
  check("normal control: the host said input injection OFF and power management OFF, both whole (r5 F4)",''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')

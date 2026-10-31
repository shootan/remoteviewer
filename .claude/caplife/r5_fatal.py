p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_fatal_exception_test.cpp'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')
NS = 'remote60::native_poc::e2e::'


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:90], s.count(a))
    s = s.replace(a, b)


rep('#include "e2e_injection_guard.hpp"\n', '#include "e2e_injection_guard.hpp"\n#include "e2e_isolation.hpp"\n')

# ---- header wording (F2, F3, F4, F1)
rep('''// 2026-10-28 (Codex codex_test_injection_finding.md, fatal_exception): every host here gets
// `--input-injection-mode none`. The run() cases have no client at all (no socket): they die at a startup step
// before any input path -- their OFF line is not confirmed, and they are judged as startup faults with zero
// connections and zero input.''', '''// 2026-10-28 (Codex codex_test_injection_finding.md, fatal_exception; r5 F1-F4): every host here is started with
// `--input-injection-mode none --host-power-management off`, bound to 127.0.0.1 on ports nobody holds, with its own
// LOCALAPPDATA inside this run's folder (not the user's; not every interface -- no firewall prompt). The run() cases
// std / winrt are startup faults (a throw at the first configure step); `none` is the NORMAL control (a plain start,
// no fault, 3 s). This test opens no socket to any of them -- no client of this test connects (other local programs
// could reach a loopback listener; nothing here does). Their power effects are zero by the order in main(): the
// policy is bound from the command line before HostPowerKeepalive exists, and OFF makes no request at all -- the
// host's own "host power management=off" line is checked after the fact (not a gate: nothing connects anyway).''')
rep('''// inside that folder. (Before: logs beside the executable and a fatal_prelogin_logroot there, remove_all'd -- a
// recursive delete outside the repository for a build outside it.)''', '''// inside that folder, and only files this test knows by name plus the folders it made, one by one, never
// recursively (r5 F1); a delete that fails counts as a FAIL; a folder that is not empty stays. (Before: logs beside
// the executable and a fatal_prelogin_logroot there, remove_all'd -- a recursive delete outside the repository for a
// build outside it.)''')

# ---- F1: no recursive delete -- known files and the folders made, non-recursively, failures counted
rep('''// The one recursive delete: a path strictly inside THIS run's own new folder, no ".." in it, no link on the way.
void remove_inside_run(const std::wstring& path) {
  std::error_code ec;
  const std::filesystem::path canon = std::filesystem::weakly_canonical(path, ec);
  const std::wstring c = canon.wstring();
  if (gRun.empty() || ec || c.size() <= gRun.size() || _wcsnicmp(c.c_str(), gRun.c_str(), gRun.size()) != 0 ||
      c.find(L"..") != std::wstring::npos || is_reparse(c)) {
    std::printf("FAIL  refused to delete outside this run's folder\\n");
    ++gFailures;
    return;
  }
  std::filesystem::remove_all(canon, ec);
}''', '''// r5 F1: no recursive delete anywhere. A file this test knows by name, directly in this run's folder tree, removed
// alone; a folder it made, removed only if empty. A failure (other than "already gone") is a FAIL; a folder that is
// not empty stays where it is (said, not forced).
bool inside_run(const std::wstring& path) {
  return !gRun.empty() && path.size() > gRun.size() && _wcsnicmp(path.c_str(), gRun.c_str(), gRun.size()) == 0 &&
         path.find(L"..") == std::wstring::npos;
}
void delete_known_file(const std::wstring& path) {
  if (!inside_run(path)) {
    std::printf("FAIL  refused to delete outside this run's folder\\n");
    ++gFailures;
    return;
  }
  if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) {
    std::printf("FAIL  a file of this run could not be removed (%lu)\\n", GetLastError());
    ++gFailures;
  }
}
void remove_made_folder(const std::wstring& path) {
  if (!inside_run(path + L"\\\\") && path + L"\\\\" != gRun) {
    std::printf("FAIL  refused to remove a folder outside this run's folder\\n");
    ++gFailures;
    return;
  }
  if (!RemoveDirectoryW(path.c_str())) {
    const DWORD e = GetLastError();
    if (e == ERROR_DIR_NOT_EMPTY) {
      std::printf("NOTE  kept (not empty, inside the test root): %ls\\n", path.c_str());
    } else if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) {
      std::printf("FAIL  a folder of this run could not be removed (%lu)\\n", e);
      ++gFailures;
    }
  }
}

// r5 F3: the host's environment -- this process's, with LOCALAPPDATA in this run's folder (its diagnostics land
// there, not in the user's). Kept with the run's folder; never removed recursively.
std::vector<wchar_t> host_env(const std::wstring& tag) {
  const std::wstring lad = gRun + L"lad_" + tag;
  CreateDirectoryW(lad.c_str(), nullptr);
  return ''' + NS + '''e2e_isolated_environment(lad);
}

// r5 F4: the host said its power management OFF, whole (the effect-0 basis is main()'s order; this is the record)
bool said_power_off(const std::string& out) {
  std::string why;
  return ''' + NS + '''e2e_power_state(out, &why) == 1;
}''')

# ---- run(): loopback, free ports, isolated env (F3); the none case is the normal control (F2)
rep('''  // a port nobody uses, a short run; the injected steps come before any wait for a client. NO client: no socket is
  // opened here at all -- a startup-fault case (its OFF line not confirmed; zero connections, zero input)
  std::wstring cmd = L"\\"" + dir + L"GNLinkStreamMigrationTest.exe\\" --transport udp --codec h264 --seconds 3" +
                     ''' + NS + '''e2e_injection_off_arg(dir);
  PROCESS_INFORMATION pi{};
  DWORD code = 0xFFFFFFFFu;
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) {''',
    '''  // loopback, ports nobody holds, a short run. This test opens no socket to it (no client of this test connects);
  // a throw comes at the first configure step, before any wait for a client; `none` is the normal control (r5 F2)
  std::wstring cmd = L"\\"" + dir + L"GNLinkStreamMigrationTest.exe\\" --transport udp --codec h264 --bind-address 127.0.0.1" +
                     L" --bind-port " + std::to_wstring(free_udp_port()) + L" --control-port " + std::to_wstring(free_udp_port()) +
                     L" --seconds 3" + ''' + NS + '''e2e_injection_off_arg(dir);
  std::vector<wchar_t> env = host_env(tag);
  PROCESS_INFORMATION pi{};
  DWORD code = 0xFFFFFFFFu;
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, env.data(),
                     dir.c_str(), &si, &pi)) {''')

# ---- run_with_client(): loopback + isolated env
rep('''  std::wstring cmd = L"\\"" + dir + L"GNLinkStreamMigrationTest.exe\\" --transport udp --codec h264 --bind-port " + std::to_wstring(port) + L" --control-port " + std::to_wstring(free_udp_port()) +
                     L" --seconds 20" + ''' + NS + '''e2e_injection_off_arg(dir);
  PROCESS_INFORMATION pi{};
  DWORD code = 0xFFFFFFFFu;
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) {''',
    '''  std::wstring cmd = L"\\"" + dir + L"GNLinkStreamMigrationTest.exe\\" --transport udp --codec h264 --bind-address 127.0.0.1" +
                     L" --bind-port " + std::to_wstring(port) + L" --control-port " + std::to_wstring(free_udp_port()) +
                     L" --seconds 20" + ''' + NS + '''e2e_injection_off_arg(dir);
  std::vector<wchar_t> hostEnv = host_env(tag);
  PROCESS_INFORMATION pi{};
  DWORD code = 0xFFFFFFFFu;
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                     hostEnv.data(), dir.c_str(), &si, &pi)) {''')

# ---- the cases' claims (F2, F4)
rep('''  DWORD code = run(dir, L"std", "startup_configure_from_env", &out, 20000);''',
    '''  DWORD code = run(dir, L"std", "startup_configure_from_env", &out, 20000);
  check("std: the host said its power management OFF (r5 F4, the record)", said_power_off(out));''')
rep('''  code = run(dir, L"winrt", "winrt:startup_configure_from_env", &out, 20000);''',
    '''  code = run(dir, L"winrt", "winrt:startup_configure_from_env", &out, 20000);
  check("winrt: the host said its power management OFF (r5 F4, the record)", said_power_off(out));''')
rep('''  code = run(dir, L"none", "", &out, 30000);
  check("control (nothing injected): no FATAL line", out.find("FATAL:") == std::string::npos, "exit " + std::to_string(code));''',
    '''  // r5 F2: the NORMAL control -- no fault injected, a plain 3 s start (not a startup fault); no client of this test
  code = run(dir, L"none", "", &out, 30000);
  check("normal control (nothing injected): no FATAL line", out.find("FATAL:") == std::string::npos, "exit " + std::to_string(code));
  check("normal control: the host said input injection OFF and power management OFF, both whole (r5 F4)",
        ''' + NS + '''e2e_injection_state(out, nullptr) == 1);''')

# ---- the --prelogin worker: loopback port by its test seam, isolated env (F3), power line (F4), known files only (F1)
rep('''    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_LOG_ROOT", root.c_str());
    SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", "startup_configure_from_env");''',
    '''    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_LOG_ROOT", root.c_str());
    SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", "startup_configure_from_env");
    // r5 F3: the worker's own test seam names its listener -- loopback, a port nobody holds, a short life (without
    // it, a worker takes the product's ports on every interface)
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_BIND_ADDRESS", L"127.0.0.1");
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_BIND_PORT", std::to_wstring(free_udp_port()).c_str());
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_SECONDS", L"5");''')
rep('''    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) {
      ended = WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0;''',
    '''    std::vector<wchar_t> workerEnv = host_env(L"prelogin");
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                       workerEnv.data(), dir.c_str(), &si, &pi)) {
      ended = WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0;''')
rep('''    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_LOG_ROOT", nullptr);''',
    '''    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_LOG_ROOT", nullptr);
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_BIND_ADDRESS", nullptr);
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_BIND_PORT", nullptr);
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_SECONDS", nullptr);''')
rep('''    remove_inside_run(root);''', '''    check("...the worker said its power management OFF (r5 F4, the record)", said_power_off(read_all(log)));
    // r5 F1: the one file it writes there, then the folders made for it -- each alone, none recursively
    delete_known_file(logs + L"\\\\prelogin_worker.log");
    remove_made_folder(logs);
    remove_made_folder(root + L"\\\\GNLink");
    remove_made_folder(root);''')

# ---- the end: known logs, then the run folder if empty (the host diagnostics folders stay, said)
rep('''  // this run's own logs go with its folder (bounded to it); anything unexpected in it stays
  for (const wchar_t* t : {L"std", L"winrt", L"none", L"probe_trap", L"wrapper_trap", L"wgc_throw", L"wgc_false", L"normal"}) {
    DeleteFileW((gRun + L"fatal_" + t + L".log").c_str());
  }
  DeleteFileW((gRun + L"fatal_prelogin.log").c_str());
  RemoveDirectoryW(gRun.c_str());''', '''  // r5 F1: this run's own logs, by name, one by one; then its folder only if empty. The hosts' LOCALAPPDATA folders
  // (lad_*: their diagnostics) are not removed -- they stay inside the test root, said
  for (const wchar_t* t : {L"std", L"winrt", L"none", L"probe_trap", L"wrapper_trap", L"wgc_throw", L"wgc_false", L"normal"}) {
    delete_known_file(gRun + L"fatal_" + t + L".log");
  }
  delete_known_file(gRun + L"fatal_prelogin.log");
  remove_made_folder(gRun.substr(0, gRun.size() - 1));''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')

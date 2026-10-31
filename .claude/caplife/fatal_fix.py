p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_fatal_exception_test.cpp'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:90], s.count(a))
    s = s.replace(a, b)


# ---- header note
rep('''//   normal        nothing injected    -> past both (the encoder line), no FATAL
''', '''//   normal        nothing injected    -> past both (the encoder line), no FATAL
//
// 2026-10-28 (Codex codex_test_injection_finding.md, fatal_exception): every host here gets
// `--input-injection-mode none`. The run() cases have no client at all (no socket): they die at a startup step
// before any input path -- their OFF line is not confirmed, and they are judged as startup faults with zero
// connections and zero input. The run_with_client() cases send a client Hello only AFTER this launch said the whole
// "input injection disabled (unsupported mode) mode=none" line; a host that dies before it gets no Hello at all; any
// other injection state ends the case at once, nothing sent (INVALID). Their logs and the pre-login log root live in
// a NEW folder of this run under the trusted test root (REMOTE60_TEST_TMP directly under
// D:/remote/remote/.claude/test-tmp, as host_agent_punch_test) -- the only place this test deletes in, and only
// inside that folder. (Before: logs beside the executable and a fatal_prelogin_logroot there, remove_all'd -- a
// recursive delete outside the repository for a build outside it.)
''')

# ---- the run root helpers (after read_all)
rep('''// Runs the host with `step` injected (empty = none); returns the exit code (or 0xFFFFFFFF on timeout).''',
    '''const wchar_t kTestAnchor[] = L"D:\\\\remote\\\\remote\\\\.claude\\\\test-tmp";
std::wstring gRun;  // this run's own new folder (with a trailing backslash): logs and the log root live in it

bool is_reparse(const std::wstring& p) {
  const DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// REMOTE60_TEST_TMP: one plain folder directly under the anchor (no links); in it, a NEW folder of this run's own
// (CreateDirectory fails on any existing name). Checked before anything is created; refuses otherwise.
bool init_run_root(std::string* why) {
  wchar_t given[1024] = {};
  const DWORD n = GetEnvironmentVariableW(L"REMOTE60_TEST_TMP", given, 1024);
  if (n == 0 || n >= 1024) { *why = "REMOTE60_TEST_TMP is not set"; return false; }
  wchar_t full[1024] = {};
  wchar_t* namePart = nullptr;
  if (GetFullPathNameW(given, 1024, full, &namePart) == 0 || !namePart || !*namePart) {
    *why = "REMOTE60_TEST_TMP is not a usable path";
    return false;
  }
  const std::wstring name(namePart);
  for (wchar_t ch : name) {
    const bool ok = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') ||
                    ch == L'-' || ch == L'_' || ch == L'.';
    if (!ok || name == L"." || name == L"..") { *why = "the test root's name must be a plain ASCII folder name"; return false; }
  }
  std::wstring parent(full, namePart - full);
  while (!parent.empty() && (parent.back() == L'\\\\' || parent.back() == L'/')) parent.pop_back();
  if (_wcsicmp(parent.c_str(), kTestAnchor) != 0) {
    *why = "REMOTE60_TEST_TMP must be a folder directly under D:/remote/remote/.claude/test-tmp";
    return false;
  }
  const DWORD anchorAttr = GetFileAttributesW(kTestAnchor);
  if (anchorAttr == INVALID_FILE_ATTRIBUTES || !(anchorAttr & FILE_ATTRIBUTE_DIRECTORY) || is_reparse(kTestAnchor)) {
    *why = "the test anchor is missing or a link";
    return false;
  }
  const std::wstring root = std::wstring(kTestAnchor) + L"\\\\" + name;
  if (!CreateDirectoryW(root.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
    *why = "the test root could not be created";
    return false;
  }
  const DWORD rootAttr = GetFileAttributesW(root.c_str());
  if (rootAttr == INVALID_FILE_ATTRIBUTES || !(rootAttr & FILE_ATTRIBUTE_DIRECTORY) || (rootAttr & FILE_ATTRIBUTE_REPARSE_POINT)) {
    *why = "the test root is not a plain folder";
    return false;
  }
  for (int attempt = 0; attempt < 16; ++attempt) {
    const std::wstring run = root + L"\\\\fatal-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                             std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
    if (CreateDirectoryW(run.c_str(), nullptr)) {
      if (is_reparse(run)) { *why = "this run's folder is a link"; return false; }
      gRun = run + L"\\\\";
      return true;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) break;
  }
  *why = "no new folder for this run could be made";
  return false;
}

// The one recursive delete: a path strictly inside THIS run's own new folder, no ".." in it, no link on the way.
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
}

// Runs the host with `step` injected (empty = none); returns the exit code (or 0xFFFFFFFF on timeout).''')

# logs into the run folder
rep('''  const std::wstring log = dir + L"fatal_" + tag + L".log";
  SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", step.empty() ? nullptr : step.c_str());''',
    '''  const std::wstring log = gRun + L"fatal_" + tag + L".log";
  SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", step.empty() ? nullptr : step.c_str());''')
rep('''  // a port nobody uses, a short run; the injected steps come before any wait for a client
  // input injection off (e2e_injection_guard.hpp) -- this host dies at a startup step and no client here sends input;
  // there is no wait for its "disabled" line (it may die before saying it)
''', '''  // a port nobody uses, a short run; the injected steps come before any wait for a client. NO client: no socket is
  // opened here at all -- a startup-fault case (its OFF line not confirmed; zero connections, zero input)
''')
rep('''  const uint16_t port = free_udp_port();
  const std::wstring log = dir + L"fatal_" + tag + L".log";''', '''  const uint16_t port = free_udp_port();
  const std::wstring log = gRun + L"fatal_" + tag + L".log";''')
rep('''                     L" --seconds 20" + remote60::native_poc::e2e::e2e_injection_off_arg(dir);  // input injection off; its only client sends a bare Hello''',
    '''                     L" --seconds 20" + remote60::native_poc::e2e::e2e_injection_off_arg(dir);''')
rep('''      if (GetTickCount64() > deadline) {
        TerminateProcess(pi.hProcess, 0xFFFFFFFFu);
        WaitForSingleObject(pi.hProcess, 5000);
        break;
      }
      sendto(''', '''      if (GetTickCount64() > deadline) {
        TerminateProcess(pi.hProcess, 0xFFFFFFFFu);
        WaitForSingleObject(pi.hProcess, 5000);
        break;
      }
      // the gate: a Hello only after THIS launch said its whole input-injection-OFF line; another state -> the
      // case ends here, nothing sent (a host that dies before saying it simply never gets one)
      std::string injWhy;
      const int inj = remote60::native_poc::e2e::e2e_injection_state(read_all(log), &injWhy);
      if (inj < 0) {
        TerminateProcess(pi.hProcess, 0xFFFFFFFFu);
        WaitForSingleObject(pi.hProcess, 5000);
        code = kInjectionInvalid;
        std::printf("FAIL  SETUP %ls: input injection not provably off (%s) -- no Hello sent\\n", tag.c_str(), injWhy.c_str());
        break;
      }
      if (inj == 0) continue;  // not yet said: no Hello
      ++*hellosSent;
      sendto(''')
rep('''DWORD run_with_client(const std::wstring& dir, const std::wstring& tag, const std::vector<std::pair<std::string, std::string>>& env,
                      const std::string& seenNeedle, std::string* out) {''',
    '''constexpr DWORD kInjectionInvalid = 0xFFFFFFFEu;  // the injection gate refused: the case is INVALID, nothing sent
DWORD run_with_client(const std::wstring& dir, const std::wstring& tag, const std::vector<std::pair<std::string, std::string>>& env,
                      const std::string& seenNeedle, std::string* out, int* hellosSent) {
  *hellosSent = 0;''')

# main: the run root first
rep('''  check("the stream test build is beside this test", std::filesystem::exists(dir + L"GNLinkStreamMigrationTest.exe"));
''', '''  check("the stream test build is beside this test", std::filesystem::exists(dir + L"GNLinkStreamMigrationTest.exe"));
  {
    std::string why;
    if (!init_run_root(&why)) {
      std::printf("FAIL  no valid test root (%s): refused to run -- nothing created or deleted\\nRESULT: FAILED\\n", why.c_str());
      return 2;
    }
  }
''')
rep('''    const std::wstring root = dir + L"fatal_prelogin_logroot";
    const std::wstring logs = root + L"\\\\GNLink\\\\logs";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    CreateDirectoryW(root.c_str(), nullptr);''', '''    // inside this run's own new folder: nothing to clear first
    const std::wstring root = gRun + L"fatal_prelogin_logroot";
    const std::wstring logs = root + L"\\\\GNLink\\\\logs";
    CreateDirectoryW(root.c_str(), nullptr);''')
rep('''    const std::wstring log = dir + L"fatal_prelogin.log";''', '''    const std::wstring log = gRun + L"fatal_prelogin.log";''')
rep('''          read_all(log).find("at step=startup_configure_from_env -- exit 49") != std::string::npos);
    std::filesystem::remove_all(root, ec);''', '''          read_all(log).find("at step=startup_configure_from_env -- exit 49") != std::string::npos);
    remove_inside_run(root);''')

# the five run_with_client calls
import re
calls = re.findall(r'run_with_client\(dir, L"(\w+)", (.*?), (""|"H264 encoder backend="), &out\);', s)
assert len(calls) == 5, calls
s = re.sub(r'(code = run_with_client\(dir, L"\w+", .*?, (?:""|"H264 encoder backend="), &out)\);',
           r'\1, &hellos);\n  check("...the injection gate held (no Hello before the OFF line)", code != kInjectionInvalid);', s)
rep('''  // ---- D2: the ordinary path's two WinRT calls ----''', '''  // ---- D2: the ordinary path's two WinRT calls ----
  int hellos = 0;''')

# end: this run's folder (its files removed by the bounded delete)
rep('''  std::printf("\\nRESULT: %s  (%d checks, %d failed)\\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);''',
    '''  // this run's own logs go with its folder (bounded to it); anything unexpected in it stays
  for (const wchar_t* t : {L"std", L"winrt", L"none", L"probe_trap", L"wrapper_trap", L"wgc_throw", L"wgc_false", L"normal"}) {
    DeleteFileW((gRun + L"fatal_" + t + L".log").c_str());
  }
  DeleteFileW((gRun + L"fatal_prelogin.log").c_str());
  RemoveDirectoryW(gRun.c_str());
  std::printf("\\nRESULT: %s  (%d checks, %d failed)\\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')

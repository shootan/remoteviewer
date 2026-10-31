p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/e2e_guard_runtime_test.cpp'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')


def rep(a, b, n=1):
    global s
    assert s.count(a) == n, (a[:100], s.count(a))
    s = s.replace(a, b)


rep('''#include "e2e_child_util.hpp"
#include "e2e_injection_guard.hpp"''', '''#include "e2e_child_util.hpp"
#include "e2e_injection_guard.hpp"
#include "e2e_test_root.hpp"''')

rep('''//   N2:  a case's contradiction ends the WHOLE test''', '''//   r9:  its files live only in a NEW run folder of its own inside REMOTE60_TEST_TMP (e2e_test_root.hpp) -- no %TEMP%;
//        without a valid root the test refuses before it creates anything; it deletes only the files it made there.
//   N2:  a case's contradiction ends the WHOLE test''')

rep('''// r8: a file of this test (its own temp folder) with `text`; empty path on failure
std::wstring temp_file(const char* tag, const std::string& text) {
  wchar_t dir[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, dir);
  const std::wstring path = std::wstring(dir) + L"e2e_guard_runtime_" + std::wstring(tag, tag + strlen(tag)) + L"_" +
                            std::to_wstring(GetCurrentProcessId()) + L".log";
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (h == INVALID_HANDLE_VALUE) return {};
  DWORD put = 0;
  const bool ok = text.empty() || (WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &put, nullptr) && put == text.size());
  CloseHandle(h);
  return ok ? path : std::wstring();
}''', '''// r9: this run's own folder inside REMOTE60_TEST_TMP (the parent makes it new; a child is handed it in
// REMOTE60_E2E_RUN_DIR and re-checks it). Never %TEMP%.
constexpr wchar_t kRunPrefix[] = L"guard-runtime-";
std::wstring gRunDir;

bool child_run_dir(std::string* why) {
  wchar_t v[1024] = {};
  const DWORD n = GetEnvironmentVariableW(L"REMOTE60_E2E_RUN_DIR", v, 1024);
  if (n == 0 || n >= 1024) {
    *why = "REMOTE60_E2E_RUN_DIR is not set";
    return false;
  }
  if (!e::e2e_run_dir_valid(v, kRunPrefix, why)) return false;
  gRunDir = v;
  return true;
}

// A file this process makes in the run folder (CREATE_NEW: never one that is there already); empty path on failure.
std::wstring run_file(const char* tag, const std::string& text) {
  if (gRunDir.empty()) return {};
  const std::wstring path = gRunDir + L"\\\\" + std::wstring(tag, tag + strlen(tag)) + L"_" +
                            std::to_wstring(GetCurrentProcessId()) + L".log";
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return {};
  DWORD put = 0;
  const bool ok = text.empty() || (WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &put, nullptr) && put == text.size());
  CloseHandle(h);
  return ok ? path : std::wstring();
}

// Deletes a file run_file made -- only a path directly inside the run folder (nothing else is ever deleted).
void delete_run_file(const std::wstring& path) {
  const std::wstring head = gRunDir + L"\\\\";
  if (gRunDir.empty() || path.size() <= head.size() || path.compare(0, head.size(), head) != 0 ||
      path.find(L'\\\\', head.size()) != std::wstring::npos) {
    return;
  }
  DeleteFileW(path.c_str());
}''')

rep('''      const std::wstring f = temp_file("empty", "");  // opens and reads to EOF -- 0 bytes, no OFF line
      const e::E2eFinalOutput fin = e::e2e_final_file(f);
      DeleteFileW(f.c_str());''', '''      const std::wstring f = run_file("empty", "");  // opens and reads to EOF -- 0 bytes, no OFF line
      if (f.empty()) {
        std::printf("NO_RUN_FILE\\n");
        return 9;
      }
      const e::E2eFinalOutput fin = e::e2e_final_file(f);
      delete_run_file(f);''')
rep('''        const std::wstring f = temp_file("quiet", offLines());
        fin = e::e2e_final_file(f);
        DeleteFileW(f.c_str());''', '''        const std::wstring f = run_file("quiet", offLines());
        if (f.empty()) {
          std::printf("NO_RUN_FILE\\n");
          return 9;
        }
        fin = e::e2e_final_file(f);
        delete_run_file(f);''')
rep('''    const std::wstring wf = temp_file("writeonly", "abc");''', '''    const std::wstring wf = run_file("writeonly", "abc");''')
rep('''    DeleteFileW(wf.c_str());''', '''    delete_run_file(wf);''')

# the child: the file cases need the run folder handed to them
rep('''  if (argc >= 3 && std::string(argv[1]) == "--monitor-case") return monitor_case(argv[2]);''',
    '''  if (argc >= 3 && std::string(argv[1]) == "--monitor-case") {
    const std::string kind = argv[2];
    if (kind == "empty_file" || kind == "quiet_file") {  // r9: the only cases that make a file
      std::string why;
      if (!child_run_dir(&why)) {
        std::printf("NO_RUN_DIR %s\\n", why.c_str());
        return 9;
      }
    }
    return monitor_case(kind);
  }''')

# the parent: its run folder before anything else
rep('''  if (argc >= 2 && std::string(argv[1]) == "--unconfirmed-case") return unconfirmed_case();
''', '''  if (argc >= 2 && std::string(argv[1]) == "--unconfirmed-case") return unconfirmed_case();

  // r9: a NEW run folder of this test inside REMOTE60_TEST_TMP, or nothing at all (refused before anything is made)
  {
    std::string why;
    std::wstring root;
    if (!e::e2e_test_root(true, &root, &why) || !e::e2e_new_run_dir(root, kRunPrefix, &gRunDir, &why)) {
      std::printf("FAIL  no valid test root (%s): refused to run -- no %%TEMP%%, nothing else created or deleted\\n"
                  "RESULT: INVALID\\n", why.c_str());
      return 2;
    }
    SetEnvironmentVariableW(L"REMOTE60_E2E_RUN_DIR", gRunDir.c_str());
    std::printf("NOTE  this run's folder (its files only are deleted; the folder is left): %ls\\n", gRunDir.c_str());
  }
''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')

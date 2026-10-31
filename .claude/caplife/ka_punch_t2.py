p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_agent_punch_test.cpp'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


reps = [("""// keepalive-r2 FINDING r2 (Codex codex_ka_punch_thread_src_r1.md N1): the host agent's punch queue lives exactly one
// Start..Stop. The real HostAgent (directory_client.cpp) with its real Start / Punch / Stop and its punch thread; the
// directory is an address nobody answers on (127.0.0.1:9 -- its cycle only fails and retries); the send function
// records what would go out. Loopback only, nothing is sent anywhere (the recorder sends nothing).
""", """// keepalive-r2 FINDING r2 (Codex codex_ka_punch_thread_src_r1.md N1): the host agent's punch queue lives exactly one
// Start..Stop. The real HostAgent (directory_client.cpp) with its real Start / Punch / Stop and its punch thread; the
// directory is an address nobody answers on (127.0.0.1:9 -- its cycle only fails and retries); the send function
// records what would go out. Loopback only, nothing is sent anywhere (the recorder sends nothing).
//
// r3 (Codex codex_ka_punch_thread_src_r2.md T2, as dirretry r2 d2f1243): the agent's cache file lives in ONE test root
// -- REMOTE60_TEST_TMP must name a folder directly under the trusted anchor D:/remote/remote/.claude/test-tmp (no links)
// -- under a name unique to this process; the test deletes that file only, through a guard, and refuses to run without
// a valid root. (r2 named a fixed file beside the executable and deleted it at start and end: for a build outside the
// repository that is a delete outside it. The agent never wrote it -- an unreachable directory never registers -- so
// those deletes found nothing; see the report.)
"""),
("""#include "directory_client.hpp"

namespace d = remote60::native_poc::directory;

namespace {
int gChecks = 0, gFailures = 0;""", """#include "directory_client.hpp"

namespace d = remote60::native_poc::directory;

namespace {
int gChecks = 0, gFailures = 0;

const wchar_t kTestAnchor[] = L"D:\\\\remote\\\\remote\\\\.claude\\\\test-tmp";
std::string gRoot;  // the validated test root (ASCII: the anchor is, and the name is checked)

bool is_reparse(const std::wstring& p) {
  const DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// REMOTE60_TEST_TMP: one plain folder directly under the anchor; the anchor exists and is no link; the folder is made
// one level inside it and is no link either. Checked before anything is created.
bool init_root(std::string* why) {
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
  if (is_reparse(root)) { *why = "the test root is a link"; return false; }
  gRoot.clear();
  for (wchar_t ch : root) gRoot.push_back(static_cast<char>(ch));
  return true;
}

// The one delete this test does: a file directly in its root, of this run's own name.
void delete_own_file(const std::string& path) {
  const std::string prefix = gRoot + "\\\\";
  if (gRoot.empty() || path.size() <= prefix.size() || _strnicmp(path.c_str(), prefix.c_str(), prefix.size()) != 0 ||
      path.find("..") != std::string::npos || path.find_first_of("\\\\/", prefix.size()) != std::string::npos) {
    std::printf("FAIL  refused to delete outside the test root: %s\\n", path.c_str());
    ++gFailures;
    return;
  }
  DeleteFileA(path.c_str());
}"""),
("""  WSADATA w;
  WSAStartup(MAKEWORD(2, 2), &w);
  char exe[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string dir(exe);
  dir = dir.substr(0, dir.find_last_of("\\\\/") + 1);
  const std::string cache = dir + "host_agent_punch_test.cache";  // its own; removed at the end
  DeleteFileA(cache.c_str());
""", """  std::string why;
  if (!init_root(&why)) {
    std::printf("FAIL  no valid test root (%s): refused to run -- nothing created or deleted\\n", why.c_str());
    std::printf("RESULT FAIL\\n");
    return 2;
  }
  WSADATA w;
  WSAStartup(MAKEWORD(2, 2), &w);
  // this run's own file in the root (an unreachable directory never registers: the agent never writes it -- the name
  // is still unique and the delete still guarded)
  const std::string cache = gRoot + "\\\\host-agent-punch-" + std::to_string(GetCurrentProcessId()) + ".cache";
  delete_own_file(cache);
"""),
("""  DeleteFileA(cache.c_str());
  std::printf("\\n%d checks, %d failures""", """  delete_own_file(cache);
  std::printf("\\n%d checks, %d failures"""),
]
for a, b in reps:
    rep(a, b)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')

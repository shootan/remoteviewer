import os
R = r'C:\work\gnlink-wt\dirretry-r2'
rel = r'apps\native_poc\src\directory_retry_test.cpp'
b = open(os.path.join(R, rel), 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:90])
    s = s.replace(a, c)

# ------------------------------------------------------------ T2: the cache root
rep("""std::string exe_directory() {
  char path[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, path, MAX_PATH);
  std::string text(path);
  // Both separators. This was "\\/" -- an unknown escape that is just "/", so on a Windows path
  // nothing was found and the fixture cache landed in the working directory instead. (RV-17)
  const size_t slash = text.find_last_of("\\\\/");
  return slash == std::string::npos ? std::string(".") : text.substr(0, slash);
}""", """// dirretry r2 (Codex T2): where the fixture caches live -- and the only place this test deletes anything.
// It used to be beside the executable, which for a build outside the repository meant creating and
// deleting files outside it. Now the caller names ONE folder directly under the trusted anchor
// D:/remote/remote/.claude/test-tmp (REMOTE60_TEST_TMP, the directory suite's rule, test_tmp.js); the
// path is checked before anything is made: the anchor must exist and be no link, the folder is created
// one level inside it and must not be a link either. Without a valid root the test refuses to run.
const wchar_t kTestAnchor[] = L"D:\\\\remote\\\\remote\\\\.claude\\\\test-tmp";
std::string gCacheRoot;  // narrow form of the validated root (ASCII: the anchor is, and the name is checked)

bool is_reparse(const std::wstring& p) {
  const DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

bool init_cache_root(std::string* why) {
  wchar_t given[1024] = {};
  const DWORD n = GetEnvironmentVariableW(L"REMOTE60_TEST_TMP", given, 1024);
  if (n == 0 || n >= 1024) { *why = "REMOTE60_TEST_TMP is not set"; return false; }
  wchar_t full[1024] = {};
  wchar_t* namePart = nullptr;
  if (GetFullPathNameW(given, 1024, full, &namePart) == 0 || !namePart || !*namePart) {
    *why = "REMOTE60_TEST_TMP is not a usable path"; return false;
  }
  std::wstring name(namePart);
  for (wchar_t ch : name) {
    const bool ok = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') ||
                    ch == L'-' || ch == L'_' || ch == L'.';
    if (!ok || name == L"." || name == L"..") { *why = "the test root's name must be a plain ASCII folder name"; return false; }
  }
  std::wstring parent(full, namePart - full);
  while (!parent.empty() && (parent.back() == L'\\\\' || parent.back() == L'/')) parent.pop_back();
  if (_wcsicmp(parent.c_str(), kTestAnchor) != 0) {
    *why = "REMOTE60_TEST_TMP must be a folder directly under D:/remote/remote/.claude/test-tmp"; return false;
  }
  const DWORD anchorAttr = GetFileAttributesW(kTestAnchor);
  if (anchorAttr == INVALID_FILE_ATTRIBUTES || !(anchorAttr & FILE_ATTRIBUTE_DIRECTORY) || is_reparse(kTestAnchor)) {
    *why = "the test anchor is missing or a link"; return false;
  }
  const std::wstring root = std::wstring(kTestAnchor) + L"\\\\" + name;
  if (!CreateDirectoryW(root.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
    *why = "the test root could not be created"; return false;
  }
  if (is_reparse(root)) { *why = "the test root is a link"; return false; }
  gCacheRoot.assign(root.begin(), root.end());  // ASCII by the checks above
  return true;
}

// A fixture file under the root; and the one delete the test does, of such a file only.
std::string cache_file(const std::string& leaf) { return gCacheRoot + "\\\\" + leaf; }
void delete_fixture_file(const std::string& path) {
  const std::string prefix = gCacheRoot + "\\\\";
  if (gCacheRoot.empty() || path.size() <= prefix.size() || _strnicmp(path.c_str(), prefix.c_str(), prefix.size()) != 0 ||
      path.find("..") != std::string::npos || path.find_first_of("\\\\/", prefix.size()) != std::string::npos) {
    std::printf("FAIL  refused to delete outside the test root: %s\\n", path.c_str());
    ++gFailures;
    return;
  }
  DeleteFileA(path.c_str());
}""")
rep("""  // Beside this executable and unique per case, so no run reads another's cached token and
  // nothing outside the build tree is written.
  cfg.cachePath = exe_directory() + "\\\\retry-fixture-" + std::string(label) + ".json";""",
    """  // In the test root (dirretry r2, Codex T2) and unique per case, so no run reads another's cached token
  // and nothing outside that root is written or deleted.
  cfg.cachePath = cache_file("retry-fixture-" + std::string(label) + ".json");""")
rep("""  if (pump.joinable()) pump.join();
  DeleteFileA(cfg.cachePath.c_str());
  return status;""", """  if (pump.joinable()) pump.join();
  delete_fixture_file(cfg.cachePath);
  return status;""")
rep("""    cfg_.cachePath = exe_directory() + "\\\\retry-fixture-staged-" + std::string(label) + ".json";""",
    """    cfg_.cachePath = cache_file("retry-fixture-staged-" + std::string(label) + ".json");""")
rep("""    if (!cachePath.empty()) DeleteFileA(cachePath.c_str());""",
    """    if (!cachePath.empty()) delete_fixture_file(cachePath);""")
rep("""  harness->cachePath = exe_directory() + "\\\\punch-fixture-" + std::string(label) + ".json";""",
    """  harness->cachePath = cache_file("punch-fixture-" + std::string(label) + ".json");""")

# ------------------------------------------------------------ T1: the staged host steps cycle by cycle
rep("""    legacyPort_ = static_cast<uint16_t>(dir.httpPort() + 1);
    std::string error;
    started_ = agent_.Start(""", """    legacyPort_ = static_cast<uint16_t>(dir.httpPort() + 1);
    label_ = label;
    // dirretry r2 (Codex T1): the loop waits at each cycle's end until Wait() lets it go on. 60 s is the
    // bound; reaching it is a failure (checked when this ends), never a silent continue.
    agent_.EnableCycleGateForTest(60000);
    std::string error;
    started_ = agent_.Start(""")
rep("""  ~StagedHost() {
    agent_.Stop();
    pumping_ = false;
    closesocket(media_);
    if (pump_.joinable()) pump_.join();
    DeleteFileA(cfg_.cachePath.c_str());
  }

  /** The record of cycle n, once that cycle has completed (bounded: 12 s, about two cycles). */
  bool Wait(uint64_t n, Cycle* out) {
    return started_ && agent_.WaitForCycleEnd(n, 12000, out);
  }""", """  ~StagedHost() {
    const bool gateTimedOut = agent_.CycleGateTimedOutForTest();
    agent_.Stop();
    pumping_ = false;
    closesocket(media_);
    if (pump_.joinable()) pump_.join();
    check((std::string("[") + label_ + "] the cycle barrier was never left by its timeout").c_str(), !gateTimedOut);
    delete_fixture_file(cfg_.cachePath);
  }

  /**
   * The record of cycle n, once that cycle has completed (bounded: 12 s per cycle). dirretry r2: cycles are
   * stepped -- each earlier one is released only on the way to n -- and the loop is HELD at the end of n
   * until the next Wait (or the end). So whatever the test reads now (the fake's counters, LegacySends) is
   * cycle n's, however slow the test is, and a Script() it installs now is in place before cycle n+1.
   * A lower n than the one held just returns that past record.
   */
  bool Wait(uint64_t n, Cycle* out) {
    if (!started_) return false;
    while (stepped_ < n) {
      if (stepped_ > 0) agent_.ReleaseCycleForTest(stepped_);
      if (!agent_.WaitForCycleEnd(stepped_ + 1, 12000, nullptr)) return false;
      ++stepped_;
    }
    return agent_.WaitForCycleEnd(n, 1, out);
  }""")
rep("""  uint16_t legacyPort_ = 0;
  std::atomic<int> legacySends_{0};""", """  uint16_t legacyPort_ = 0;
  std::string label_;
  uint64_t stepped_ = 0;  // the cycle the loop is held at (0: none yet)
  std::atomic<int> legacySends_{0};""")

# ------------------------------------------------------------ T1 limited cases: a slow observer
rep("""    {
      StagedHost host(dir, "inactive", false, dir.udpPort());
      StagedHost::Cycle c1, c3, c4;
      const bool got1 = host.Wait(1, &c1);""", """    for (int slowMs : {0, 7000}) {
      // dirretry r2 (Codex T1): the second pass is a slow observer -- 7 s (more than a 5 s cycle) between
      // having cycle 1's record and reading the counters. Without the barrier the loop would be in cycle 2+
      // by then (Codex reproduced it with 17 s: heartbeat count 2 at "cycle 1"); with it, the same counts.
      const char* tag = slowMs ? "inactive-slow" : "inactive";
      if (slowMs) dir.Script("/api/host/heartbeat",
                             {Reply{401, "{\\"error\\":\\"unknown host token\\",\\"code\\":\\"account_inactive\\"}"},
                              Reply{200, "{\\"ok\\":true}"}});
      const int hb0 = dir.Count("/api/host/heartbeat"), reg0 = dir.Count("/api/host/register");
      StagedHost host(dir, tag, false, dir.udpPort());
      StagedHost::Cycle c1, c3, c4;
      const bool got1 = host.Wait(1, &c1);
      if (slowMs) std::this_thread::sleep_for(std::chrono::milliseconds(slowMs));""")
# the inactive checks: counts relative to this pass's start
rep("""            got1 && !c1.heartbeatOk && dir.Count("/api/host/heartbeat") == 1 && dir.Count("/api/host/register") == 1,
            std::to_string(dir.Count("/api/host/heartbeat")) + "/" + std::to_string(dir.Count("/api/host/register")));""",
    """            got1 && !c1.heartbeatOk && dir.Count("/api/host/heartbeat") - hb0 == 1 && dir.Count("/api/host/register") - reg0 == 1,
            std::string(tag) + " " + std::to_string(dir.Count("/api/host/heartbeat") - hb0) + "/" +
                std::to_string(dir.Count("/api/host/register") - reg0));""")
rep("""            got3 && c2.skippedInactive && c3.skippedInactive && dir.Count("/api/host/heartbeat") == 1,
            cycle_text(c3) + " heartbeats=" + std::to_string(dir.Count("/api/host/heartbeat")));""",
    """            got3 && c2.skippedInactive && c3.skippedInactive && dir.Count("/api/host/heartbeat") - hb0 == 1,
            std::string(tag) + " " + cycle_text(c3) + " heartbeats=" + std::to_string(dir.Count("/api/host/heartbeat") - hb0));""")
rep("""            dir.Count("/api/host/heartbeat") == 2 && dir.Count("/api/host/register") == 1,
            std::to_string(dir.Count("/api/host/heartbeat")) + "/" + std::to_string(dir.Count("/api/host/register")));
    }
    dir.Stop();""", """            dir.Count("/api/host/heartbeat") - hb0 == 2 && dir.Count("/api/host/register") - reg0 == 1,
            std::string(tag) + " " + std::to_string(dir.Count("/api/host/heartbeat") - hb0) + "/" +
                std::to_string(dir.Count("/api/host/register") - reg0));
    }
    dir.Stop();""")

# cached-silent: a slow pass that delays installing the advertisement until after cycle 2 is held
rep("""    {
      StagedHost host(dir, "cached-silent", true, 0);""", """    for (int slowMs : {0, 7000}) {
      // dirretry r2 (Codex T1): the second pass waits 7 s after cycle 2 before installing the advertisement --
      // longer than the cooldown's two 5 s cycles. Without the barrier cycle 4 would already have asked (and
      // been told nothing); with it, the script is in place before cycle 3 starts, as in the fast pass.
      const char* tag = slowMs ? "cached-silent-slow" : "cached-silent";
      dir.Script("/healthz", {Reply{200, "{\\"ok\\":true}"}});
      const int health0 = dir.Count("/healthz"), hb0 = dir.Count("/api/host/heartbeat"), reg0 = dir.Count("/api/host/register");
      StagedHost host(dir, tag, true, 0);""")
rep("""      check("cached-silent: cycle 1 completed", got1, cycle_text(c1));
      check("cycle 1: the health route was asked once and said nothing about observations",
            got1 && c1.healthAsked && c1.health == ObserveHealth::NoAdvertisement && dir.Count("/healthz") == 1,
            cycle_text(c1) + " healthz=" + std::to_string(dir.Count("/healthz")));""",
    """      check((std::string(tag) + ": cycle 1 completed").c_str(), got1, cycle_text(c1));
      check("cycle 1: the health route was asked once and said nothing about observations",
            got1 && c1.healthAsked && c1.health == ObserveHealth::NoAdvertisement && dir.Count("/healthz") - health0 == 1,
            std::string(tag) + " " + cycle_text(c1) + " healthz=" + std::to_string(dir.Count("/healthz") - health0));""")
rep("""            !c1.heartbeatOk && c1.status.find("public=") == std::string::npos && dir.Count("/api/host/heartbeat") == 0,""",
    """            !c1.heartbeatOk && c1.status.find("public=") == std::string::npos && dir.Count("/api/host/heartbeat") - hb0 == 0,""")
rep("""            got2 && !c2.healthAsked && dir.Count("/healthz") == 1 && host.LegacySends() > legacyAfter1 &&""",
    """            got2 && !c2.healthAsked && dir.Count("/healthz") - health0 == 1 && host.LegacySends() > legacyAfter1 &&""")
rep("""      // Now the directory advertises where observations go; cycle 4 is the next to ask (cooldown 2).
      dir.Script("/healthz",""", """      // Now the directory advertises where observations go; cycle 4 is the next to ask (cooldown 2). The
      // loop is held at the end of cycle 2 while this is installed (dirretry r2), however late it comes.
      if (slowMs) std::this_thread::sleep_for(std::chrono::milliseconds(slowMs));
      dir.Script("/healthz",""")
rep("""            c4.status.find("public=") != std::string::npos && dir.Count("/api/host/register") == 0 &&
                dir.Count("/api/host/heartbeat") >= 1 && dir.Count("/healthz") == 2,
            c4.status + " healthz=" + std::to_string(dir.Count("/healthz")));""",
    """            c4.status.find("public=") != std::string::npos && dir.Count("/api/host/register") - reg0 == 0 &&
                dir.Count("/api/host/heartbeat") - hb0 >= 1 && dir.Count("/healthz") - health0 == 2,
            std::string(tag) + " " + c4.status + " healthz=" + std::to_string(dir.Count("/healthz") - health0));""")

# ------------------------------------------------------------ main: refuse without a valid root
rep("""int main() {
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("FAIL  winsock did not start\\n");
    return 1;
  }
""", """int main() {
  // dirretry r2 (Codex T2): no valid test root, no run -- the caches (and their deletes) go nowhere else.
  {
    std::string why;
    if (!init_cache_root(&why)) {
      std::printf("FAIL  no test root: %s\\n", why.c_str());
      return 2;
    }
    std::printf("test root: %s\\n", gCacheRoot.c_str());
  }
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("FAIL  winsock did not start\\n");
    return 1;
  }
""")
open(os.path.join(R, rel), 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')

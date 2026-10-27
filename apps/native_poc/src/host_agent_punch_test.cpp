// keepalive-r2 FINDING r2 (Codex codex_ka_punch_thread_src_r1.md N1): the host agent's punch queue lives exactly one
// Start..Stop. The real HostAgent (directory_client.cpp) with its real Start / Punch / Stop and its punch thread; the
// directory is an address nobody answers on (127.0.0.1:9 -- its cycle only fails and retries); the send function
// records what would go out. Loopback only, nothing is sent anywhere (the recorder sends nothing).
//
// r3 (Codex codex_ka_punch_thread_src_r2.md T2, as dirretry r2 d2f1243): the agent's cache file lives in ONE test root
// -- REMOTE60_TEST_TMP must name a folder directly under the trusted anchor D:/remote/remote/.claude/test-tmp (no links)
// -- the root itself checked to BE a plain folder (r4 T2-a), and inside it a NEW folder of this run's own (made with
// CreateDirectory, which fails on any existing name: r4 T2-b) holds the cache file. Nothing is deleted before the run;
// after it, that file (if the agent wrote it) and this run's folder (not recursive) go. Without a valid root it refuses
// to run. (r2 named a fixed file beside the executable and deleted it at start and end: for a build outside the
// repository that is a delete outside it. What is known is that the test never made that file -- the agent never
// wrote it: an unreachable directory never registers. Whether a file of that name was there before, and what each
// delete returned, was not recorded; see the report.)

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "directory_client.hpp"

namespace d = remote60::native_poc::directory;

namespace {
int gChecks = 0, gFailures = 0;

const wchar_t kTestAnchor[] = L"D:\\remote\\remote\\.claude\\test-tmp";
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
  while (!parent.empty() && (parent.back() == L'\\' || parent.back() == L'/')) parent.pop_back();
  if (_wcsicmp(parent.c_str(), kTestAnchor) != 0) {
    *why = "REMOTE60_TEST_TMP must be a folder directly under D:/remote/remote/.claude/test-tmp";
    return false;
  }
  const DWORD anchorAttr = GetFileAttributesW(kTestAnchor);
  if (anchorAttr == INVALID_FILE_ATTRIBUTES || !(anchorAttr & FILE_ATTRIBUTE_DIRECTORY) || is_reparse(kTestAnchor)) {
    *why = "the test anchor is missing or a link";
    return false;
  }
  const std::wstring root = std::wstring(kTestAnchor) + L"\\" + name;
  if (!CreateDirectoryW(root.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
    *why = "the test root could not be created";
    return false;
  }
  // r4 (Codex T2-a): what is there now must BE a folder -- looked up, a directory, no link (a plain file of that name
  // used to pass: CreateDirectory said "already exists" and only the link bit was read)
  const DWORD rootAttr = GetFileAttributesW(root.c_str());
  if (rootAttr == INVALID_FILE_ATTRIBUTES || !(rootAttr & FILE_ATTRIBUTE_DIRECTORY) || (rootAttr & FILE_ATTRIBUTE_REPARSE_POINT)) {
    *why = "the test root is not a plain folder";
    return false;
  }
  // r4 (Codex T2-b): this run's own folder in it, made NEW (CreateDirectory fails on any existing name -- a folder, a
  // file, another run's): what is in it is this run's alone. Nothing that was already there is touched.
  for (int attempt = 0; attempt < 16; ++attempt) {
    const std::wstring run = root + L"\\run-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                             std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
    if (CreateDirectoryW(run.c_str(), nullptr)) {
      if (is_reparse(run)) { *why = "this run's folder is a link"; return false; }
      gRoot.clear();
      for (wchar_t ch : run) gRoot.push_back(static_cast<char>(ch));
      return true;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) break;
  }
  *why = "no new folder for this run could be made";
  return false;
}

// The one delete this test does: a file directly in THIS run's own new folder (gRoot is that folder).
void delete_own_file(const std::string& path) {
  const std::string prefix = gRoot + "\\";
  if (gRoot.empty() || path.size() <= prefix.size() || _strnicmp(path.c_str(), prefix.c_str(), prefix.size()) != 0 ||
      path.find("..") != std::string::npos || path.find_first_of("\\/", prefix.size()) != std::string::npos) {
    std::printf("FAIL  refused to delete outside the test root: %s\n", path.c_str());
    ++gFailures;
    return;
  }
  DeleteFileA(path.c_str());
}
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}
void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Recorder {
  std::mutex mu;
  std::vector<std::pair<uint16_t, std::string>> sent;  // (port, the proof the punch carried)
  void Add(const void* data, size_t len, const sockaddr_in& to) {
    std::string proof;
    if (len >= 16 + 33) proof = std::string(static_cast<const char*>(data) + 16);  // UdpHelloPacket::authToken (offset 16)
    std::lock_guard<std::mutex> lock(mu);
    sent.emplace_back(ntohs(to.sin_port), proof);
  }
  int To(uint16_t port) {
    std::lock_guard<std::mutex> lock(mu);
    int n = 0;
    for (const auto& s : sent) n += s.first == port ? 1 : 0;
    return n;
  }
  bool ProofSeen(const std::string& proof) {
    std::lock_guard<std::mutex> lock(mu);
    for (const auto& s : sent)
      if (s.second == proof) return true;
    return false;
  }
};
}  // namespace

int main() {
  std::string why;
  if (!init_root(&why)) {
    std::printf("FAIL  no valid test root (%s): refused to run -- nothing created or deleted\n", why.c_str());
    std::printf("RESULT FAIL\n");
    return 2;
  }
  WSADATA w;
  WSAStartup(MAKEWORD(2, 2), &w);
  // in this run's own new folder: nothing to clear first (r4 T2-b -- no pre-delete of a name another run could hold)
  const std::string cache = gRoot + "\\host-agent.cache";

  d::HostAgentConfig cfg;
  cfg.url = "http://127.0.0.1:9";  // nobody answers: the cycle fails and retries, nothing else happens
  cfg.preloadedHostId = "punch-test-host";
  cfg.preloadedHostToken = "punch-test-token";
  cfg.cachePath = cache;
  cfg.heartbeatSeconds = 5;
  in_addr lo{};
  inet_pton(AF_INET, "127.0.0.1", &lo);

  Recorder rec;
  d::HostAgent agent;
  std::string err;
  check("the agent starts (an unreachable directory)",
        agent.Start(cfg, [&](const void* p, size_t n, const sockaddr_in& to) { rec.Add(p, n, to); }, &err), err);

  std::printf("\n--- a Stop in the middle of a train ---\n");
  agent.PunchForTest(lo.s_addr, 40001, "proof-old");
  sleep_ms(450);
  const int during = rec.To(40001);
  check("the train runs beside the cycle (punches out, more queued)", during >= 2 && agent.PendingPunchJobsForTest() == 1,
        "sent=" + std::to_string(during));
  agent.Stop();
  check("Stop: the queue is gone (the rest of the train is discarded, not kept)", agent.PendingPunchJobsForTest() == 0,
        "pending=" + std::to_string(agent.PendingPunchJobsForTest()));
  const int atStop = rec.To(40001);
  sleep_ms(500);
  check("...and nothing more goes out after the Stop", rec.To(40001) == atStop);

  std::printf("\n--- a late Punch after Stop (the cycle's step between its check and its push) ---\n");
  agent.PunchForTest(lo.s_addr, 40003, "proof-late");
  check("a Punch while stopped queues nothing", agent.PendingPunchJobsForTest() == 0);

  std::printf("\n--- the next Start ---\n");
  check("the agent starts again", agent.Start(cfg, [&](const void* p, size_t n, const sockaddr_in& to) { rec.Add(p, n, to); }, &err),
        err);
  sleep_ms(800);
  check("...the old train never comes back: no punch to its target, its host proof not on the wire again",
        rec.To(40001) == atStop && rec.To(40003) == 0 && !rec.ProofSeen("proof-late"),
        "old=" + std::to_string(rec.To(40001) - atStop) + " late=" + std::to_string(rec.To(40003)));
  agent.PunchForTest(lo.s_addr, 40002, "proof-new");
  sleep_ms(450);
  check("...a new train of this Start goes out (with its own proof)", rec.To(40002) >= 2 && rec.ProofSeen("proof-new") &&
        rec.To(40001) == atStop);
  agent.Stop();
  check("Stop again: empty", agent.PendingPunchJobsForTest() == 0);

  delete_own_file(cache);  // only if the agent wrote it; in this run's folder either way
  std::wstring wroot;
  for (char ch : gRoot) wroot.push_back(static_cast<wchar_t>(ch));
  RemoveDirectoryW(wroot.c_str());  // this run's folder, not recursive: anything unexpected in it stays
  std::printf("\n%d checks, %d failures\n%s\n", gChecks, gFailures, gFailures == 0 ? "RESULT PASS" : "RESULT FAIL");
  WSACleanup();
  return gFailures == 0 ? 0 : 1;
}

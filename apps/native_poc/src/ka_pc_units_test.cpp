// ka-pc-impl r1 (KA ③): the shell <-> managed viewer channel and the recent-PC store, without a viewer, a host
// or a network. Pipes are real anonymous pipes in this process; files go only under REMOTE60_TEST_TMP (one folder
// directly under D:/remote/remote/.claude/test-tmp), including a non-ASCII sub-folder.
#include <windows.h>

#include <cstdio>
#include <string>
#include <thread>

#include "client_recent_hosts.hpp"
#include "managed_viewer_ipc.hpp"

namespace {
int gFailures = 0;
void check(const char* name, bool ok, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ", detail.c_str());
  if (!ok) ++gFailures;
}
using namespace remote60::native_poc;
}  // namespace

int main() {
  // ------------------------------------------------------------ frames
  managed_ipc::Message m;
  m.type = managed_ipc::Type::Init;
  m.fields = {{"session", "tok%en\nwith\rbreaks"}, {"key", "https://dir\nacct\nh-1"}, {"empty", ""}};
  const std::string frame = managed_ipc::encode(m);
  managed_ipc::Message back;
  const bool parsed = managed_ipc::parse_payload(frame.substr(8), &back);
  check("a frame round-trips its fields, escapes included", parsed && back.fields == m.fields);
  check("the header carries version 1 and the type", frame[4] == 1 && frame[5] == 0 && static_cast<uint8_t>(frame[6]) == 1);
  managed_ipc::Message junk;
  check("a payload line without '=' is refused", !managed_ipc::parse_payload("novalue\n", &junk));
  check("a bad escape is refused", !managed_ipc::parse_payload("k=%zz\n", &junk));
  check("a payload without its final newline is refused", !managed_ipc::parse_payload("k=v", &junk));

  // ------------------------------------------------------------ a channel over real pipes
  {
    HANDLE aRead, bWrite, bRead, aWrite;
    CreatePipe(&aRead, &bWrite, nullptr, 1 << 16);  // b -> a
    CreatePipe(&bRead, &aWrite, nullptr, 1 << 16);  // a -> b
    managed_ipc::Channel a, b;
    a.Start(aRead, aWrite);
    b.Start(bRead, bWrite);
    managed_ipc::Message hello;
    hello.type = managed_ipc::Type::Hello;
    hello.fields = {{"inc", "7"}};
    check("send", a.Send(hello));
    managed_ipc::Message got;
    check("the other side receives it", b.Receive(&got, 2000) && got.type == managed_ipc::Type::Hello && got.num("inc") == 7);
    a.Close();
    const bool timedOut = !b.Receive(&got, 2000);
    check("closing one side is EOF on the other (it is no longer open)", timedOut && !b.open(), b.why());
    b.Close();
  }
  {
    // a peer that writes a frame of another version: the channel ends, nothing is delivered
    HANDLE r, w;
    CreatePipe(&r, &w, nullptr, 1 << 16);
    HANDLE r2, w2;
    CreatePipe(&r2, &w2, nullptr, 1 << 16);
    managed_ipc::Channel c;
    c.Start(r, w2);
    std::string bad = managed_ipc::encode(m);
    bad[4] = 9;  // version 9
    DWORD put = 0;
    WriteFile(w, bad.data(), static_cast<DWORD>(bad.size()), &put, nullptr);
    managed_ipc::Message got;
    const bool none = !c.Receive(&got, 1500);
    check("a frame of another version ends the channel, delivering nothing", none && !c.open(), c.why());
    // an oversized length is refused the same way (fresh channel)
    CloseHandle(w);
    c.Close();
    CloseHandle(r2);
  }
  {
    HANDLE r, w, r2, w2;
    CreatePipe(&r, &w, nullptr, 1 << 16);
    CreatePipe(&r2, &w2, nullptr, 1 << 16);
    managed_ipc::Channel c;
    c.Start(r, w2);
    char header[8] = {0, 0, 0x10, 0, 1, 0, 1, 0};  // length 0x100000 > kMaxPayload
    DWORD put = 0;
    WriteFile(w, header, 8, &put, nullptr);
    managed_ipc::Message got;
    const bool none = !c.Receive(&got, 1500);
    check("an oversized frame ends the channel", none && !c.open(), c.why());
    CloseHandle(w);
    c.Close();
    CloseHandle(r2);
  }
  {
    // a peer that never reads: Close is still bounded (the blocked write is cancelled)
    HANDLE r, w, r2, w2;
    CreatePipe(&r, &w, nullptr, 4096);
    CreatePipe(&r2, &w2, nullptr, 4096);
    managed_ipc::Channel c;
    c.Start(r, w2);
    managed_ipc::Message big;
    big.type = managed_ipc::Type::State;
    big.fields = {{"x", std::string(12000, 'a')}};
    for (int i = 0; i < 8; ++i) c.Send(big);  // > the pipe's buffer; nobody reads r2
    const auto t0 = GetTickCount64();
    c.Close();
    const auto ms = GetTickCount64() - t0;
    check("Close with a peer that never reads returns in bounded time", ms < 3000, std::to_string(ms) + " ms");
    CloseHandle(w);
    CloseHandle(r2);
  }

  // ------------------------------------------------------------ recent store
  using namespace remote60::native_poc::shell;
  {
    std::vector<RecentHost> all;
    all = recent_touch(all, "https://d", "acct", "h1");
    all = recent_touch(all, "https://d", "acct", "h2");
    all = recent_touch(all, "https://d", "acct", "h3");
    all = recent_touch(all, "https://d", "other", "h9");
    all = recent_touch(all, "https://d", "acct", "h4");
    const auto mine = recent_for(all, "https://d", "acct");
    check("three per account, newest first", mine.size() == 3 && mine[0] == "h4" && mine[1] == "h3" && mine[2] == "h2");
    check("another account is kept apart", recent_for(all, "https://d", "other").size() == 1);
    all = recent_touch(all, "https://d", "acct", "h2");
    const auto again = recent_for(all, "https://d", "acct");
    check("a reuse moves it to the front", again[0] == "h2" && again[1] == "h4" && again[2] == "h3");
    const auto round = recent_parse(recent_format(all));
    check("format/parse round trip keeps order", recent_for(round, "https://d", "acct") == again);
    std::vector<RecentHost> odd{{5, "https://d", "a\tb%c\nd", "h\r1"}};
    check("separators in a field are escaped", recent_parse(recent_format(odd)).size() == 1 &&
                                                 recent_parse(recent_format(odd))[0].account == "a\tb%c\nd");
    check("a wrong header is an empty history", recent_parse("other v9\n1\ta\tb\tc\n").empty());
    check("a bad line is dropped, a good one kept",
          recent_parse(std::string(kRecentHeader) + "\nx\ta\tb\tc\n7\ta\tb\tc\n").size() == 1);
  }
  {
    wchar_t root[1024] = {};
    if (!GetEnvironmentVariableW(L"REMOTE60_TEST_TMP", root, 1024)) {
      check("REMOTE60_TEST_TMP is set (the file cases need a test root)", false);
    } else {
      std::wstring dir = std::wstring(root);
      const std::wstring parent = dir.substr(0, dir.find_last_of(L"\\/"));
      if (_wcsicmp(parent.c_str(), L"D:\\remote\\remote\\.claude\\test-tmp") != 0 &&
          _wcsicmp(parent.c_str(), L"D:/remote/remote/.claude/test-tmp") != 0) {
        check("the test root is directly under D:/remote/remote/.claude/test-tmp", false);
      } else {
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring korean = dir + L"\\\xD55C\xAE00 \xC0AC\xC6A9\xC790";  // a non-ASCII folder
        CreateDirectoryW(korean.c_str(), nullptr);
        const std::wstring path = korean + L"\\recent_hosts.v1";
        std::vector<RecentHost> all = recent_touch({}, "https://d", "acct", "h1");
        all = recent_touch(all, "https://d", "acct", "h2");
        check("saved under a non-ASCII folder", recent_save(path, all));
        check("...and read back", recent_for(recent_load(path), "https://d", "acct") == std::vector<std::string>{"h2", "h1"});
        DeleteFileW(path.c_str());  // inside the test root only
        RemoveDirectoryW(korean.c_str());
      }
    }
  }
  std::printf("\nka_pc_units_test: %s\n", gFailures == 0 ? "ALL PASS" : (std::to_string(gFailures) + " FAILED").c_str());
  return gFailures == 0 ? 0 : 1;
}

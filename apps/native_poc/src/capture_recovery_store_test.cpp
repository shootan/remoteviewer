// dxgi-wedge-fix r1 (Codex B2/B3): the persistent DXGI quarantine store.
//
// Pure: the record format and its refusals (schema, size, times that run backwards, an expiry that was
// extended, shapes), the next record after a wedge (driver change starts over, a clock that went back
// never moves `last` back), and the startup decision (expired / future / adapter absent / driver changed).
// Real folders (a staging root this process makes inside the repository): a write is read back; a second
// wedge counts; reading never extends the expiry; a damaged record of our own is replaced; a record anyone
// else may read, a hard link to a file outside, and a folder that is a junction are all refused -- and the
// file behind them is left byte-for-byte as it was; the 16-record limit drops the oldest; two writers do
// not lose a wedge; a held lock makes a write fail in bounded time instead of hanging.
// r2 D3: a record whose `last` is refused as "future" at startup is not carried on by the next wedge (it
// starts over from now, and the following startup quarantines); in an EMPTY protected folder, two writer
// PROCESSES released together both record (no sharing violation on the first lock-file create), within a
// bound. Mutants (each must FAIL here): REMOTE60_STORE_MUTANT_CONTINUE_FUTURE, ..._UNSHARED_LOCK_CREATE.
// The test's owner rule trusts this user besides SYSTEM/Administrators (a non-elevated test cannot make
// SYSTEM-owned folders); the product's rule is SYSTEM/Administrators only. pure-logic + filesystem.

#include <winsock2.h>
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <winioctl.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "capture_recovery_store.hpp"
#include "e2e_isolation.hpp"
#include "prelogin_store_chain.hpp"

namespace cr = remote60::native_poc::capture_recovery;
namespace pl = remote60::native_poc::prelogin;
using remote60::native_poc::CaptureAdapterIdentity;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}

std::wstring my_sid_string() {
  HANDLE tok = nullptr;
  OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
  DWORD need = 0;
  GetTokenInformation(tok, TokenUser, nullptr, 0, &need);
  std::vector<BYTE> buf(need);
  GetTokenInformation(tok, TokenUser, buf.data(), need, &need);
  CloseHandle(tok);
  LPWSTR s = nullptr;
  ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &s);
  std::wstring out = s;
  LocalFree(s);
  return out;
}

bool make_dir(const std::wstring& path, const std::wstring& sddl) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return false;
  SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
  const bool ok = CreateDirectoryW(path.c_str(), &sa) != FALSE;
  LocalFree(sd);
  return ok;
}

bool make_file(const std::wstring& path, const std::wstring& sddl, const std::string& content) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return false;
  SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  LocalFree(sd);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD wrote = 0;
  const bool ok = WriteFile(f, content.data(), static_cast<DWORD>(content.size()), &wrote, nullptr) && wrote == content.size();
  CloseHandle(f);
  return ok;
}

std::string slurp(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream s;
  s << in.rdbuf();
  return s.str();
}

bool exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

size_t count_records(const std::wstring& folder) {
  size_t n = 0;
  WIN32_FIND_DATAW fd{};
  HANDLE f = FindFirstFileW((folder + L"\\*.rec").c_str(), &fd);
  if (f == INVALID_HANDLE_VALUE) return 0;
  do ++n; while (FindNextFileW(f, &fd));
  FindClose(f);
  return n;
}

#pragma pack(push, 1)
struct MountPointReparse {
  DWORD ReparseTag;
  WORD ReparseDataLength;
  WORD Reserved;
  WORD SubstituteNameOffset;
  WORD SubstituteNameLength;
  WORD PrintNameOffset;
  WORD PrintNameLength;
  WCHAR PathBuffer[1];
};
#pragma pack(pop)

bool make_junction(const std::wstring& link, const std::wstring& target) {
  if (!CreateDirectoryW(link.c_str(), nullptr)) return false;
  HANDLE h = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                         FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  const std::wstring subst = L"\\??\\" + target;
  const std::wstring print = target;
  const size_t substBytes = subst.size() * sizeof(wchar_t);
  const size_t printBytes = print.size() * sizeof(wchar_t);
  const size_t pathBuf = substBytes + sizeof(wchar_t) + printBytes + sizeof(wchar_t);
  const size_t headerToPath = offsetof(MountPointReparse, PathBuffer);
  std::vector<uint8_t> raw(headerToPath + pathBuf, 0);
  auto* rp = reinterpret_cast<MountPointReparse*>(raw.data());
  rp->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
  rp->ReparseDataLength = static_cast<WORD>(8 + pathBuf);
  rp->SubstituteNameOffset = 0;
  rp->SubstituteNameLength = static_cast<WORD>(substBytes);
  rp->PrintNameOffset = static_cast<WORD>(substBytes + sizeof(wchar_t));
  rp->PrintNameLength = static_cast<WORD>(printBytes);
  std::memcpy(rp->PathBuffer, subst.c_str(), substBytes + sizeof(wchar_t));
  std::memcpy(reinterpret_cast<uint8_t*>(rp->PathBuffer) + substBytes + sizeof(wchar_t), print.c_str(),
              printBytes + sizeof(wchar_t));
  DWORD ret = 0;
  const bool ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, raw.data(), static_cast<DWORD>(raw.size()), nullptr, 0,
                                  &ret, nullptr) != FALSE;
  CloseHandle(h);
  return ok;
}

CaptureAdapterIdentity adapter(const char* key, const char* driver) {
  CaptureAdapterIdentity a;
  a.key = key;
  a.driver = driver;
  return a;
}

constexpr uint64_t kNow = 1761000000ull;  // 2025-10 -- any fixed instant
constexpr uint64_t kWeek = cr::kQuarantineSeconds;

}  // namespace

// r2 D3 child: one of the concurrent writer processes. --writer <folder> <count> <event>
int writer_main(char** argv) {
  const std::wstring me = my_sid_string();
  PSID mine = nullptr;
  ConvertStringSidToSidW(me.c_str(), &mine);
  const auto trustMe = [mine](PSID s) { return pl::OwnerIsSystemOrAdministrators(s) || (s && EqualSid(s, mine)); };
  const std::wstring fileSddl = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;" + me + L")";
  auto widen_acp = [](const char* s) {
    const int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L' ');
    if (n > 1) MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
    return w;
  };
  const std::wstring folder = widen_acp(argv[2]);
  const int count = std::atoi(argv[3]);
  HANDLE ev = OpenEventW(SYNCHRONIZE, FALSE, widen_acp(argv[4]).c_str());
  if (!ev || WaitForSingleObject(ev, 15000) != WAIT_OBJECT_0) return 100;
  const cr::Store store(folder, trustMe, fileSddl.c_str());
  int fails = 0;
  for (int i = 0; i < count; ++i) {
    std::string w;
    const DWORD rc = store.RecordWedge(adapter("0123456789abcdef", "31.0.15.5244"), kNow + 100, nullptr, &w);
    if (rc != 0) {
      ++fails;
      std::printf("      writer pid=%lu failed rc=%lu why=%s\n", GetCurrentProcessId(), rc, w.c_str());
    }
  }
  CloseHandle(ev);
  LocalFree(mine);
  return fails;
}

int main(int argc, char** argv) {
  if (argc >= 5 && std::string(argv[1]) == "--writer") return writer_main(argv);
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("capture-recovery store (dxgi-wedge-fix r1)\n\n--- pure ---\n");
  const auto A = adapter("0123456789abcdef", "31.0.15.5244");
  const auto A2 = adapter("0123456789abcdef", "32.0.15.6094");  // same card, new driver
  const auto B = adapter("fedcba9876543210", "1.2.3.4");

  {
    const cr::Record r = cr::NextRecord(nullptr, A, kNow);
    cr::Record back;
    std::string why;
    check("a first wedge: wedges=1, first=last=now, expires=now+7d",
          r.wedges == 1 && r.firstUnix == kNow && r.lastUnix == kNow && r.expiresUnix == kNow + kWeek);
    check("format -> parse round-trips", cr::ParseRecord(cr::FormatRecord(r), &back, &why) &&
                                             back.adapterKey == r.adapterKey && back.driver == r.driver &&
                                             back.expiresUnix == r.expiresUnix && back.wedges == 1, why);
    const cr::Record r2 = cr::NextRecord(&r, A, kNow + 100);
    check("a second wedge on the same driver: counts, keeps first, expires = new last + 7d",
          r2.wedges == 2 && r2.firstUnix == kNow && r2.lastUnix == kNow + 100 && r2.expiresUnix == kNow + 100 + kWeek);
    const cr::Record r3 = cr::NextRecord(&r2, A2, kNow + 200);
    check("a wedge on a NEW driver starts over (wedges=1, first=now)", r3.wedges == 1 && r3.firstUnix == kNow + 200);
    const cr::Record r4 = cr::NextRecord(&r2, A, kNow + 50);
    check("a clock that went back never moves last back (nor the expiry)", r4.lastUnix == kNow + 100 &&
                                                                            r4.expiresUnix == kNow + 100 + kWeek);
    // r2 D3: a stored `last` beyond the skew allowance is refused at every startup; carried on, the real
    // wedge recorded now would never quarantine a start either.
    cr::Record fut = r;
    fut.lastUnix = kNow + cr::kFutureSkewSeconds + 3600;
    fut.expiresUnix = fut.lastUnix + kWeek;
    check("D3 (premise) a record from beyond the skew is refused at startup",
          !cr::DecideAtStartup({fut}, {A}, kNow).quarantine);
    const cr::Record r5 = cr::NextRecord(&fut, A, kNow);
    check("D3 a wedge after a refused future record starts over from now (not that `last`)",
          r5.wedges == 1 && r5.firstUnix == kNow && r5.lastUnix == kNow && r5.expiresUnix == kNow + kWeek,
          "wedges=" + std::to_string(r5.wedges) + " last=" + std::to_string(r5.lastUnix));
    check("D3 ...and the next startup quarantines on it", cr::DecideAtStartup({r5}, {A}, kNow + 10).quarantine);
    cr::Record ahead = r;
    ahead.lastUnix = kNow + 100;  // within the allowance: accepted at startup, so it is continued
    ahead.expiresUnix = ahead.lastUnix + kWeek;
    const cr::Record r6 = cr::NextRecord(&ahead, A, kNow);
    check("D3 a record only slightly ahead (within the skew) is continued: counts, keeps last",
          r6.wedges == 2 && r6.lastUnix == kNow + 100 && cr::DecideAtStartup({r6}, {A}, kNow).quarantine);
  }
  {
    const std::string good = cr::FormatRecord(cr::NextRecord(nullptr, A, kNow));
    std::string why;
    auto refused = [&](std::string text, const char* expect) {
      why.clear();
      return !cr::ParseRecord(text, nullptr, &why) && why == expect;
    };
    auto replaced = [&](const std::string& key, const std::string& value) {
      std::string t = good;
      const size_t at = t.find(key + "=");
      const size_t end = t.find('\n', at);
      return t.substr(0, at) + key + "=" + value + t.substr(end);
    };
    check("CRLF lines are accepted", cr::ParseRecord(replaced("wedges", "1\r"), nullptr, &why), why);
    check("refused: another schema", refused(replaced("schema", "2"), "schema"), why);
    check("refused: an extended expiry (last + 8 days)",
          refused(replaced("expires", std::to_string(kNow + kWeek + 86400)), "times"), why);
    check("refused: first after last", refused(replaced("first", std::to_string(kNow + 1)), "times"), why);
    check("refused: a missing field", refused(good.substr(0, good.find("wedges=")), "malformed"), why);
    check("refused: an unknown field", refused(good + "extra=1\n", "malformed"), why);
    check("refused: a duplicated field", refused(good + "wedges=1\n", "malformed"), why);
    check("refused: a non-number", refused(replaced("last", "12a"), "malformed"), why);
    check("refused: a key of the wrong shape", refused(replaced("adapter", "XYZ"), "malformed"), why);
    check("refused: a driver of the wrong shape", refused(replaced("driver", "1.2.3"), "malformed"), why);
    check("refused: no final newline", refused(good.substr(0, good.size() - 1), "malformed"), why);
    check("refused: over 4 KiB", refused(good + std::string(5000, '#') + "\n", "size"), why);
    check("refused: zero wedges", refused(replaced("wedges", "0"), "malformed"), why);
  }
  {
    const cr::Record r = cr::NextRecord(nullptr, A, kNow);
    auto d = cr::DecideAtStartup({r}, {A}, kNow + 10);
    check("startup: same adapter, same driver, in time -> quarantine", d.quarantine && d.adapterKey == A.key &&
                                                                         d.expiresUnix == kNow + kWeek);
    check("startup: the child's reason names key, driver and expiry",
          cr::PersistedReason(d) == "persisted:key=0123456789abcdef:driver=31.0.15.5244:expires=" +
                                        std::to_string(kNow + kWeek), cr::PersistedReason(d));
    d = cr::DecideAtStartup({r}, {A}, kNow + kWeek);
    check("startup: at the expiry -> no quarantine (expired)", !d.quarantine && d.notes.size() == 1 &&
                                                                 d.notes[0].find("expired") != std::string::npos);
    d = cr::DecideAtStartup({r}, {A2}, kNow + 10);
    check("startup: driver changed -> no quarantine", !d.quarantine && d.notes.size() == 1 &&
                                                        d.notes[0].find("driver-changed") != std::string::npos);
    d = cr::DecideAtStartup({r}, {B}, kNow + 10);
    check("startup: that adapter is not present -> no quarantine (no default-GPU assumption)",
          !d.quarantine && d.notes[0].find("adapter-absent") != std::string::npos);
    d = cr::DecideAtStartup({r}, {A}, kNow - cr::kFutureSkewSeconds - 10);
    check("startup: a record from the future -> refused", !d.quarantine && d.notes[0].find("future") != std::string::npos);
    d = cr::DecideAtStartup({}, {A}, kNow);
    check("startup: nothing stored -> no quarantine", !d.quarantine && d.notes.empty());
  }

  std::printf("\n--- real folders ---\n");
  remote60::native_poc::e2e::StagingDir staging;
  if (!staging.Create(L"capture_recovery")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  const std::wstring me = my_sid_string();
  PSID mine = nullptr;
  ConvertStringSidToSidW(me.c_str(), &mine);
  const auto trustMe = [mine](PSID s) { return pl::OwnerIsSystemOrAdministrators(s) || (s && EqualSid(s, mine)); };
  const std::wstring privateSddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + me + L")";
  const std::wstring ancestorSddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + me + L")(A;;0x1200a9;;;BU)";
  const std::wstring fileSddl = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;" + me + L")";
  auto build = [&](const std::wstring& root) {
    return make_dir(root, ancestorSddl) && make_dir(root + L"\\GNLink", ancestorSddl) &&
           make_dir(root + L"\\GNLink\\capture-recovery", privateSddl);
  };
  auto store_at = [&](const std::wstring& root) {
    return cr::Store(root + L"\\GNLink\\capture-recovery", trustMe, fileSddl.c_str());
  };

  {
    const std::wstring root = staging.path() + L"good";
    check("a good folder chain is built", build(root));
    const std::wstring folder = root + L"\\GNLink\\capture-recovery";
    const cr::Store store = store_at(root);
    std::vector<cr::Record> recs;
    std::vector<std::string> notes;
    check("an empty folder loads nothing", store.LoadAll(&recs, &notes) == 0 && recs.empty());
    cr::Record written;
    std::string why;
    check("a wedge is recorded", store.RecordWedge(A, kNow, &written, &why) == 0 && written.wedges == 1, why);
    check("...as <key>.rec, and no temporary is left", exists(folder + L"\\0123456789abcdef.rec") &&
                                                      count_records(folder) == 1);
    {
      WIN32_FIND_DATAW fd{};
      HANDLE f = FindFirstFileW((folder + L"\\*.tmp").c_str(), &fd);
      check("no .tmp left behind", f == INVALID_HANDLE_VALUE);
      if (f != INVALID_HANDLE_VALUE) FindClose(f);
    }
    check("it loads back", store.LoadAll(&recs, &notes) == 0 && recs.size() == 1 && recs[0].adapterKey == A.key &&
                               recs[0].expiresUnix == kNow + kWeek);
    const std::string before = slurp(folder + L"\\0123456789abcdef.rec");
    store.LoadAll(&recs, &notes);
    (void)cr::DecideAtStartup(recs, {A}, kNow + 1000);
    check("reading and deciding never rewrite the record (no extension by a start)",
          slurp(folder + L"\\0123456789abcdef.rec") == before);
    check("a second wedge counts", store.RecordWedge(A, kNow + 60, &written, &why) == 0 && written.wedges == 2 &&
                                       written.expiresUnix == kNow + 60 + kWeek, why);

    // A damaged record of our own: refused on load, replaced on the next wedge.
    check("a damaged record is planted", make_file(folder + L"\\fedcba9876543210.rec", fileSddl, "garbage"));
    store.LoadAll(&recs, &notes);
    bool sawMalformed = false;
    for (const auto& n : notes) sawMalformed |= n.find("fedcba9876543210 refused=malformed") != std::string::npos;
    check("a damaged record is refused on load, with the reason", sawMalformed && recs.size() == 1);
    check("the next wedge on that adapter replaces it", store.RecordWedge(B, kNow, &written, &why) == 0 &&
                                                          written.wedges == 1, why);
    store.LoadAll(&recs, &notes);
    check("...and it loads", recs.size() == 2);

    // Two supervisors at once: no wedge is lost.
    std::atomic<int> failures{0};
    auto writer = [&] {
      for (int i = 0; i < 15; ++i) {
        std::string w;
        if (store.RecordWedge(A, kNow + 100, nullptr, &w) != 0) ++failures;
      }
    };
    std::thread t1(writer), t2(writer);
    t1.join();
    t2.join();
    store.LoadAll(&recs, &notes);
    uint32_t aWedges = 0;
    for (const auto& r : recs)
      if (r.adapterKey == A.key) aWedges = r.wedges;
    check("two concurrent writers: every wedge counted (2 + 30)", failures.load() == 0 && aWedges == 32,
          "failures=" + std::to_string(failures.load()) + " wedges=" + std::to_string(aWedges));

    // A held lock: the write fails in bounded time, nothing is written.
    {
      HANDLE lock = CreateFileW((folder + L"\\capture-recovery.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
      OVERLAPPED ov{};
      const bool held = lock != INVALID_HANDLE_VALUE && LockFileEx(lock, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov);
      const std::string beforeLock = slurp(folder + L"\\0123456789abcdef.rec");
      const ULONGLONG t0 = GetTickCount64();
      std::string w;
      const DWORD rc = store.RecordWedge(A, kNow + 200, nullptr, &w);
      const ULONGLONG took = GetTickCount64() - t0;
      check("a lock held elsewhere: the write fails within ~3 s, record untouched",
            held && rc != 0 && w == "lock" && took < 6000 && slurp(folder + L"\\0123456789abcdef.rec") == beforeLock,
            "rc=" + std::to_string(rc) + " why=" + w + " ms=" + std::to_string(took));
      if (held) {
        OVERLAPPED ov2{};
        UnlockFileEx(lock, 0, 1, 0, &ov2);
      }
      if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    }
  }
  {
    // A record someone else may read: refused on load, never replaced.
    const std::wstring root = staging.path() + L"loose";
    build(root);
    const std::wstring folder = root + L"\\GNLink\\capture-recovery";
    const cr::Store store = store_at(root);
    const std::string body = cr::FormatRecord(cr::NextRecord(nullptr, A, kNow));
    check("a record Users may read is planted",
          make_file(folder + L"\\0123456789abcdef.rec", L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;" + me + L")(A;;FR;;;BU)",
                    body));
    std::vector<cr::Record> recs;
    std::vector<std::string> notes;
    store.LoadAll(&recs, &notes);
    check("a loose record is refused (security)", recs.empty() && !notes.empty() &&
                                                   notes[0].find("refused=security") != std::string::npos);
    std::string why;
    const DWORD rc = store.RecordWedge(A, kNow + 5, nullptr, &why);
    check("...and a wedge does not replace it", rc != 0 && why == "existing-record-untrusted" &&
                                                 slurp(folder + L"\\0123456789abcdef.rec") == body, why);
    // Negative control: the same body with the private descriptor loads -- the descriptor is what refused it.
    make_file(folder + L"\\fedcba9876543210.rec", fileSddl, cr::FormatRecord(cr::NextRecord(nullptr, B, kNow)));
    store.LoadAll(&recs, &notes);
    check("negative control: a private record next to it loads", recs.size() == 1 && recs[0].adapterKey == B.key);
  }
  {
    // A hard link to a file outside the folder.
    const std::wstring root = staging.path() + L"link";
    build(root);
    const std::wstring folder = root + L"\\GNLink\\capture-recovery";
    const std::wstring outside = staging.path() + L"outside.txt";
    const std::string body = cr::FormatRecord(cr::NextRecord(nullptr, A, kNow));
    make_file(outside, fileSddl, body);
    const bool linked = CreateHardLinkW((folder + L"\\0123456789abcdef.rec").c_str(), outside.c_str(), nullptr) != FALSE;
    check("a hard link to an outside file is planted as a record", linked);
    const cr::Store store = store_at(root);
    std::vector<cr::Record> recs;
    std::vector<std::string> notes;
    store.LoadAll(&recs, &notes);
    check("a hard-linked record is refused", recs.empty() && !notes.empty() &&
                                              notes[0].find("refused=security") != std::string::npos);
    std::string why;
    store.RecordWedge(A, kNow + 5, nullptr, &why);
    check("...a wedge does not write through it: the outside file is unchanged", slurp(outside) == body &&
                                                                                  why == "existing-record-untrusted", why);
  }
  {
    // The folder itself a junction to somewhere else.
    const std::wstring root = staging.path() + L"junction";
    make_dir(root, ancestorSddl);
    make_dir(root + L"\\GNLink", ancestorSddl);
    const std::wstring elsewhere = staging.path() + L"elsewhere";
    make_dir(elsewhere, privateSddl);
    check("capture-recovery as a junction is planted", make_junction(root + L"\\GNLink\\capture-recovery", elsewhere));
    const cr::Store store = store_at(root);
    std::vector<cr::Record> recs;
    std::vector<std::string> notes;
    check("a junctioned folder is refused on load", store.LoadAll(&recs, &notes) == ERROR_ACCESS_DENIED);
    std::string why;
    store.RecordWedge(A, kNow, nullptr, &why);
    check("...and on write, with nothing written behind it", why == "folder-refused" && count_records(elsewhere) == 0 &&
                                                               !exists(elsewhere + L"\\capture-recovery.lock"), why);
    RemoveDirectoryW((root + L"\\GNLink\\capture-recovery").c_str());  // the link, not its target
  }
  {
    // The 16-record limit.
    const std::wstring root = staging.path() + L"limit";
    build(root);
    const std::wstring folder = root + L"\\GNLink\\capture-recovery";
    const cr::Store store = store_at(root);
    bool allOk = true;
    for (int i = 0; i < 16; ++i) {
      char key[17];
      std::snprintf(key, sizeof(key), "%016x", 0x1000 + i);
      allOk &= store.RecordWedge(adapter(key, "1.0.0.0"), kNow + i, nullptr, nullptr) == 0;
    }
    check("16 adapters are recorded", allOk && count_records(folder) == 16);
    check("a 17th adapter is recorded", store.RecordWedge(adapter("00000000000fffff", "1.0.0.0"), kNow + 100,
                                                          nullptr, nullptr) == 0);
    check("...the limit holds and the one that wedged longest ago is gone",
          count_records(folder) == 16 && !exists(folder + L"\\0000000000001000.rec") &&
              exists(folder + L"\\00000000000fffff.rec") && exists(folder + L"\\0000000000001001.rec"));
  }
  {
    // r2 D3, on disk: a planted future record, then a real wedge -> the next startup is quarantined.
    const std::wstring root = staging.path() + L"future";
    check("D3 a folder chain for the future record is built", build(root));
    const std::wstring folder = root + L"\\GNLink\\capture-recovery";
    cr::Record fut = cr::NextRecord(nullptr, A, kNow);
    fut.lastUnix = kNow + cr::kFutureSkewSeconds + 86400;
    fut.expiresUnix = fut.lastUnix + kWeek;
    check("D3 a future record is planted", make_file(folder + L"\\0123456789abcdef.rec", fileSddl, cr::FormatRecord(fut)));
    const cr::Store store = store_at(root);
    std::vector<cr::Record> recs;
    std::vector<std::string> notes;
    store.LoadAll(&recs, &notes);
    check("D3 (premise) startup refuses it", !cr::DecideAtStartup(recs, {A}, kNow).quarantine);
    cr::Record written;
    std::string why;
    check("D3 a real wedge now is recorded", store.RecordWedge(A, kNow, &written, &why) == 0, why);
    store.LoadAll(&recs, &notes);
    const auto d = cr::DecideAtStartup(recs, {A}, kNow + 10);
    check("D3 the next startup quarantines (the wedge was not lost behind the future record)",
          d.quarantine && written.lastUnix == kNow && written.wedges == 1,
          "last=" + std::to_string(written.lastUnix) + " wedges=" + std::to_string(written.wedges));
  }
  {
    // r2 D3: two writer PROCESSES released together into an EMPTY protected folder (no lock file yet).
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    int badRounds = 0;
    ULONGLONG worstMs = 0;
    std::string detail;
    constexpr int kRounds = 20, kEach = 3;
    for (int round = 0; round < kRounds; ++round) {
      const std::wstring root = staging.path() + L"race" + std::to_wstring(round);
      if (!build(root)) {
        ++badRounds;
        detail += " build" + std::to_string(round);
        continue;
      }
      const std::wstring folder = root + L"\\GNLink\\capture-recovery";
      const std::wstring evName = L"Local\\gnlink-cr-race-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                  std::to_wstring(round);
      HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, evName.c_str());
      HANDLE kids[2]{};
      for (auto& kid : kids) {
        std::wstring cmd = L"\"" + std::wstring(self) + L"\" --writer \"" + folder + L"\" " + std::to_wstring(kEach) +
                           L" " + evName;
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
          CloseHandle(pi.hThread);
          kid = pi.hProcess;
        }
      }
      Sleep(300);  // both are waiting on the event
      const ULONGLONG t0 = GetTickCount64();
      SetEvent(ev);
      DWORD rcs[2] = {999, 999};
      for (int k = 0; k < 2; ++k) {
        if (!kids[k]) continue;
        WaitForSingleObject(kids[k], 20000);
        GetExitCodeProcess(kids[k], &rcs[k]);
        CloseHandle(kids[k]);
      }
      worstMs = (std::max)(worstMs, GetTickCount64() - t0);
      CloseHandle(ev);
      std::vector<cr::Record> recs;
      std::vector<std::string> notes;
      store_at(root).LoadAll(&recs, &notes);
      const uint32_t wedges = recs.size() == 1 ? recs[0].wedges : 0;
      if (rcs[0] != 0 || rcs[1] != 0 || wedges != 2 * kEach) {
        ++badRounds;
        detail += " r" + std::to_string(round) + ":rc=" + std::to_string(rcs[0]) + "/" + std::to_string(rcs[1]) +
                  ",wedges=" + std::to_string(wedges);
      }
    }
    check("D3 empty protected folder, 2 writer processes at once, 20 rounds: every wedge recorded, none refused",
          badRounds == 0, "bad=" + std::to_string(badRounds) + detail);
    check("D3 ...each round within the bound (< 10 s)", worstMs < 10000, "worst ms=" + std::to_string(worstMs));
  }
  {
    // The owner rule is load-bearing: with a rule that does not trust this user, the same folder is refused.
    const std::wstring root = staging.path() + L"good";
    const cr::Store strict(root + L"\\GNLink\\capture-recovery", pl::OwnerIsSystemOrAdministrators);
    std::vector<cr::Record> recs;
    std::vector<std::string> notes;
    const DWORD rc = strict.LoadAll(&recs, &notes);
    check("negative control: the product's owner rule refuses a user-owned folder", rc == ERROR_ACCESS_DENIED,
          std::to_string(rc));
  }

  LocalFree(mine);
  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks,
              gFailures);
  return gFailures == 0 ? 0 : 1;
}

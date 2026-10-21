// See capture_recovery_store.hpp (dxgi-wedge-fix r1).

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "capture_recovery_store.hpp"

#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "prelogin_store_chain.hpp"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

namespace remote60::native_poc::capture_recovery {

namespace pl = remote60::native_poc::prelogin;

namespace {

constexpr wchar_t kProductFileSddl[] = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)";
constexpr wchar_t kLockName[] = L"capture-recovery.lock";
constexpr DWORD kLockWaitMs = 3000;

bool parse_u64(const std::string& s, uint64_t* out) {
  if (s.empty() || s.size() > 20) return false;
  uint64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    const uint64_t d = static_cast<uint64_t>(c - '0');
    if (v > (UINT64_MAX - d) / 10) return false;
    v = v * 10 + d;
  }
  *out = v;
  return true;
}

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

HANDLE open_no_follow(const std::wstring& path, DWORD access, DWORD share) {
  return CreateFileW(path.c_str(), access, share, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}

// A regular file (no reparse point, one link) whose owner and DACL pass the private rule.
bool file_acceptable(HANDLE h, const std::function<bool(PSID)>& ownerTrusted) {
  BY_HANDLE_FILE_INFORMATION bi{};
  if (!GetFileInformationByHandle(h, &bi)) return false;
  if (bi.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) return false;
  if (bi.nNumberOfLinks != 1) return false;
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, nullptr, nullptr,
                      nullptr, nullptr, &sd) != ERROR_SUCCESS) {
    return false;
  }
  const bool ok = pl::SecurityDescriptorAcceptable(sd, pl::ChainLevel::Private, ownerTrusted);
  LocalFree(sd);
  return ok;
}

// Reads a checked record file. ERROR_FILE_NOT_FOUND when there is none.
DWORD read_checked(const std::wstring& path, const std::function<bool(PSID)>& ownerTrusted, std::string* out) {
  out->clear();
  HANDLE h = open_no_follow(path, GENERIC_READ | READ_CONTROL, FILE_SHARE_READ);
  if (h == INVALID_HANDLE_VALUE) return GetLastError();
  if (!file_acceptable(h, ownerTrusted)) {
    CloseHandle(h);
    return ERROR_ACCESS_DENIED;
  }
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(h, &size) || size.QuadPart > static_cast<LONGLONG>(kRecordMaxBytes)) {
    CloseHandle(h);
    return ERROR_FILE_TOO_LARGE;
  }
  out->resize(static_cast<size_t>(size.QuadPart));
  DWORD got = 0;
  const bool ok = out->empty() || (ReadFile(h, out->data(), static_cast<DWORD>(out->size()), &got, nullptr) &&
                                   got == out->size());
  CloseHandle(h);
  if (!ok) {
    out->clear();
    return ERROR_READ_FAULT;
  }
  return 0;
}

HANDLE create_new_with(const std::wstring& path, const std::wstring& sddl, DWORD access) {
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
    return INVALID_HANDLE_VALUE;
  }
  SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
  HANDLE h = CreateFileW(path.c_str(), access, 0, &sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  const DWORD e = GetLastError();
  LocalFree(sd);
  SetLastError(e);
  return h;
}

// The lock file inside the protected folder, opened (or created) and checked, then locked.
class FolderLock {
 public:
  DWORD Acquire(const std::wstring& folder, const std::function<bool(PSID)>& ownerTrusted, const std::wstring& sddl) {
    const std::wstring path = folder + L"\\" + kLockName;
    constexpr DWORD kAccess = GENERIC_READ | GENERIC_WRITE | READ_CONTROL;
    h_ = open_no_follow(path, kAccess, FILE_SHARE_READ | FILE_SHARE_WRITE);
    if (h_ == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND) {
      h_ = create_new_with(path, sddl, kAccess);
      // Lost a create race to another supervisor: open what it made.
      if (h_ == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_EXISTS) {
        h_ = open_no_follow(path, kAccess, FILE_SHARE_READ | FILE_SHARE_WRITE);
      }
    }
    if (h_ == INVALID_HANDLE_VALUE) return GetLastError();
    if (!file_acceptable(h_, ownerTrusted)) return ERROR_ACCESS_DENIED;
    const ULONGLONG deadline = GetTickCount64() + kLockWaitMs;
    for (;;) {
      OVERLAPPED ov{};
      if (LockFileEx(h_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
        locked_ = true;
        return 0;
      }
      if (GetLastError() != ERROR_LOCK_VIOLATION || GetTickCount64() >= deadline) return ERROR_LOCK_VIOLATION;
      Sleep(50);
    }
  }
  ~FolderLock() {
    if (locked_) {
      OVERLAPPED ov{};
      UnlockFileEx(h_, 0, 1, 0, &ov);
    }
    if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
  }

 private:
  HANDLE h_ = INVALID_HANDLE_VALUE;
  bool locked_ = false;
};

bool record_file_name(const std::wstring& name, std::string* key) {
  if (name.size() != 20 || name.compare(16, 4, L".rec") != 0) return false;
  std::string k;
  for (size_t i = 0; i < 16; ++i) {
    if (name[i] > 0x7f) return false;
    k.push_back(static_cast<char>(name[i]));
  }
  if (!capture_adapter_key_shape_ok(k)) return false;
  *key = k;
  return true;
}

// Names of the record files present (by name only -- each is opened and checked before use).
std::vector<std::pair<std::string, std::wstring>> list_records(const std::wstring& folder) {
  std::vector<std::pair<std::string, std::wstring>> out;
  WIN32_FIND_DATAW fd{};
  HANDLE f = FindFirstFileExW((folder + L"\\*.rec").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
  if (f == INVALID_HANDLE_VALUE) return out;
  do {
    std::string key;
    if (record_file_name(fd.cFileName, &key)) out.emplace_back(key, folder + L"\\" + fd.cFileName);
  } while (FindNextFileW(f, &fd) && out.size() < 4 * kMaxRecords);
  FindClose(f);
  std::sort(out.begin(), out.end());
  return out;
}

// Removes a record file that passes the check, through the checked handle.
bool remove_checked(const std::wstring& path, const std::function<bool(PSID)>& ownerTrusted) {
  HANDLE h = open_no_follow(path, DELETE | READ_CONTROL | FILE_READ_ATTRIBUTES, FILE_SHARE_READ);
  if (h == INVALID_HANDLE_VALUE) return false;
  bool ok = false;
  if (file_acceptable(h, ownerTrusted)) {
    FILE_DISPOSITION_INFO di{};
    di.DeleteFile = TRUE;
    ok = SetFileInformationByHandle(h, FileDispositionInfo, &di, sizeof(di)) != FALSE;
  }
  CloseHandle(h);
  return ok;
}

}  // namespace

// ---- pure ----------------------------------------------------------------------------------------

std::string FormatRecord(const Record& r) {
  char buf[512];
  std::snprintf(buf, sizeof(buf), "schema=%d\nadapter=%s\ndriver=%s\nfirst=%llu\nlast=%llu\nexpires=%llu\nwedges=%u\n",
                kSchema, r.adapterKey.c_str(), r.driver.c_str(), static_cast<unsigned long long>(r.firstUnix),
                static_cast<unsigned long long>(r.lastUnix), static_cast<unsigned long long>(r.expiresUnix), r.wedges);
  return buf;
}

bool ParseRecord(const std::string& text, Record* out, std::string* why) {
  auto fail = [&](const char* w) {
    if (why) *why = w;
    return false;
  };
  if (text.size() > kRecordMaxBytes) return fail("size");
  std::map<std::string, std::string> kv;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) return fail("malformed");  // every line ends with a newline
    std::string line = text.substr(pos, end - pos);
    pos = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t eq = line.find('=');
    if (eq == std::string::npos || eq == 0) return fail("malformed");
    const std::string k = line.substr(0, eq);
    if (kv.count(k)) return fail("malformed");
    kv[k] = line.substr(eq + 1);
  }
  static const char* kKeys[] = {"schema", "adapter", "driver", "first", "last", "expires", "wedges"};
  if (kv.size() != 7) {
    if (kv.count("schema") && kv["schema"] != std::to_string(kSchema)) return fail("schema");
    return fail("malformed");
  }
  for (const char* k : kKeys) {
    if (!kv.count(k)) return fail("malformed");
  }
  if (kv["schema"] != std::to_string(kSchema)) return fail("schema");
  Record r;
  r.adapterKey = kv["adapter"];
  r.driver = kv["driver"];
  if (!capture_adapter_key_shape_ok(r.adapterKey) || !capture_adapter_driver_shape_ok(r.driver)) return fail("malformed");
  uint64_t wedges = 0;
  if (!parse_u64(kv["first"], &r.firstUnix) || !parse_u64(kv["last"], &r.lastUnix) ||
      !parse_u64(kv["expires"], &r.expiresUnix) || !parse_u64(kv["wedges"], &wedges)) {
    return fail("malformed");
  }
  if (wedges == 0 || wedges > 1000000) return fail("malformed");
  r.wedges = static_cast<uint32_t>(wedges);
  // Times that run backwards, or an expiry that is not exactly last + 7 days (an extended one), are refused.
  if (r.firstUnix == 0 || r.firstUnix > r.lastUnix || r.expiresUnix != r.lastUnix + kQuarantineSeconds) {
    return fail("times");
  }
  if (out) *out = std::move(r);
  if (why) why->clear();
  return true;
}

Record NextRecord(const Record* existing, const CaptureAdapterIdentity& adapter, uint64_t nowUnix) {
  Record r;
  r.adapterKey = adapter.key;
  r.driver = adapter.driver;
  const bool continues = existing && existing->adapterKey == adapter.key && existing->driver == adapter.driver &&
                         existing->firstUnix <= nowUnix;
  r.firstUnix = continues ? existing->firstUnix : nowUnix;
  // A clock that went backwards since the last wedge never moves `last` back (nor the expiry forward).
  r.lastUnix = continues ? std::max(existing->lastUnix, nowUnix) : nowUnix;
  r.wedges = continues ? std::min<uint32_t>(existing->wedges + 1, 1000000) : 1;
  r.expiresUnix = r.lastUnix + kQuarantineSeconds;
  return r;
}

StartupDecision DecideAtStartup(const std::vector<Record>& records, const std::vector<CaptureAdapterIdentity>& present,
                                uint64_t nowUnix) {
  StartupDecision d;
  for (const Record& r : records) {
    std::string note = "key=" + r.adapterKey + " ";
    if (r.lastUnix > nowUnix + kFutureSkewSeconds) {
      d.notes.push_back(note + "refused=future");
      continue;
    }
    if (nowUnix >= r.expiresUnix) {
      d.notes.push_back(note + "skipped=expired");
      continue;
    }
    const CaptureAdapterIdentity* match = nullptr;
    for (const auto& a : present) {
      if (a.key == r.adapterKey) match = &a;
    }
    if (!match) {
      d.notes.push_back(note + "skipped=adapter-absent");
      continue;
    }
    if (match->driver != r.driver) {
      d.notes.push_back(note + "skipped=driver-changed recorded=" + r.driver + " now=" + match->driver);
      continue;
    }
    if (!d.quarantine || r.expiresUnix > d.expiresUnix) {
      d.quarantine = true;
      d.adapterKey = r.adapterKey;
      d.driver = r.driver;
      d.expiresUnix = r.expiresUnix;
    }
  }
  return d;
}

std::string PersistedReason(const StartupDecision& d) {
  if (!d.quarantine) return {};
  return "persisted:key=" + d.adapterKey + ":driver=" + d.driver + ":expires=" + std::to_string(d.expiresUnix);
}

uint64_t NowUnix() {
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  const uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
  return t / 10000000ull - 11644473600ull;
}

// ---- folder ---------------------------------------------------------------------------------------

std::wstring ProductFolder() {
  PWSTR programData = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programData)) || !programData) return {};
  std::wstring p = programData;
  CoTaskMemFree(programData);
  return p + L"\\GNLink\\capture-recovery";
}

Store::Store(std::wstring folder, std::function<bool(PSID)> ownerTrusted, const wchar_t* fileSddl)
    : folder_(std::move(folder)), ownerTrusted_(std::move(ownerTrusted)), fileSddl_(fileSddl ? fileSddl : kProductFileSddl) {}

DWORD Store::LoadAll(std::vector<Record>* out, std::vector<std::string>* refused) const {
  out->clear();
  if (refused) refused->clear();
  if (folder_.empty()) return ERROR_PATH_NOT_FOUND;
  pl::HeldChain held;
  const DWORD chain = pl::HoldRecordChain(folder_ + L"\\" + kLockName, /*create=*/false, ownerTrusted_, &held);
  if (chain != 0) return chain;
  const auto names = list_records(folder_);
  for (const auto& [key, path] : names) {
    if (out->size() >= kMaxRecords) {
      if (refused) refused->push_back("key=" + key + " refused=over-limit");
      continue;
    }
    std::string text;
    const DWORD rd = read_checked(path, ownerTrusted_, &text);
    if (rd != 0) {
      if (refused) refused->push_back("key=" + key + " refused=" + (rd == ERROR_ACCESS_DENIED ? "security" :
                                                                   rd == ERROR_FILE_TOO_LARGE ? "size" : "read"));
      continue;
    }
    Record r;
    std::string why;
    if (!ParseRecord(text, &r, &why)) {
      if (refused) refused->push_back("key=" + key + " refused=" + why);
      continue;
    }
    if (r.adapterKey != key) {
      if (refused) refused->push_back("key=" + key + " refused=name-mismatch");
      continue;
    }
    out->push_back(std::move(r));
  }
  return 0;
}

DWORD Store::RecordWedge(const CaptureAdapterIdentity& adapter, uint64_t nowUnix, Record* written,
                         std::string* why) const {
  auto fail = [&](DWORD e, const std::string& w) {
    if (why) *why = w;
    return e ? e : ERROR_GEN_FAILURE;
  };
  if (!adapter.valid()) return fail(ERROR_INVALID_PARAMETER, "adapter-unknown");
  if (folder_.empty()) return fail(ERROR_PATH_NOT_FOUND, "folder-unresolved");
  pl::HeldChain held;
  if (const DWORD chain = pl::HoldRecordChain(folder_ + L"\\" + kLockName, /*create=*/true, ownerTrusted_, &held)) {
    return fail(chain, "folder-refused");
  }
  FolderLock lock;
  if (const DWORD l = lock.Acquire(folder_, ownerTrusted_, fileSddl_)) return fail(l, "lock");

  const std::wstring target = folder_ + L"\\" + widen(adapter.key) + L".rec";
  Record existing;
  bool haveExisting = false;
  {
    std::string text;
    const DWORD rd = read_checked(target, ownerTrusted_, &text);
    if (rd == 0) {
      std::string pwhy;
      haveExisting = ParseRecord(text, &existing, &pwhy) && existing.adapterKey == adapter.key;
      // A damaged record of our own is replaced below; one that fails the security check is not touched.
    } else if (rd != ERROR_FILE_NOT_FOUND) {
      if (rd == ERROR_ACCESS_DENIED) return fail(rd, "existing-record-untrusted");
      if (rd != ERROR_FILE_TOO_LARGE) return fail(rd, "existing-record-unreadable");
    }
  }
  // Room for one more adapter: drop the record that wedged longest ago (checked, by handle).
  if (!haveExisting) {
    auto names = list_records(folder_);
    if (names.size() >= kMaxRecords) {
      std::vector<std::pair<uint64_t, std::wstring>> byLast;
      for (const auto& [key, path] : names) {
        std::string text;
        Record r;
        if (read_checked(path, ownerTrusted_, &text) == 0 && ParseRecord(text, &r, nullptr)) {
          byLast.emplace_back(r.lastUnix, path);
        } else {
          byLast.emplace_back(0, path);  // unparseable: oldest
        }
      }
      std::sort(byLast.begin(), byLast.end());
      size_t over = names.size() - kMaxRecords + 1;
      for (const auto& [last, path] : byLast) {
        if (over == 0) break;
        if (remove_checked(path, ownerTrusted_)) --over;
      }
      if (over != 0) return fail(ERROR_DISK_FULL, "over-limit");
    }
  }
  const Record next = NextRecord(haveExisting ? &existing : nullptr, adapter, nowUnix);
  const std::string body = FormatRecord(next);

  wchar_t tmpName[96];
  std::swprintf(tmpName, 96, L"\\%hs.%lu.%llu.tmp", adapter.key.c_str(), GetCurrentProcessId(),
                static_cast<unsigned long long>(GetTickCount64()));
  const std::wstring tmp = folder_ + tmpName;
  HANDLE h = create_new_with(tmp, fileSddl_, GENERIC_WRITE | DELETE | READ_CONTROL | FILE_READ_ATTRIBUTES);
  if (h == INVALID_HANDLE_VALUE) return fail(GetLastError(), "temp-create");
  auto discard = [&] {
    FILE_DISPOSITION_INFO di{};
    di.DeleteFile = TRUE;
    SetFileInformationByHandle(h, FileDispositionInfo, &di, sizeof(di));
    CloseHandle(h);
  };
  DWORD wrote = 0;
  if (!file_acceptable(h, ownerTrusted_) ||
      !WriteFile(h, body.data(), static_cast<DWORD>(body.size()), &wrote, nullptr) || wrote != body.size() ||
      !FlushFileBuffers(h)) {
    const DWORD e = GetLastError();
    discard();
    return fail(e, "temp-write");
  }
  // The target, if any, must be our own kind of file before it is replaced: a hard link to something
  // else, or a file someone else may touch, is left alone and the write refused.
  {
    HANDLE t = open_no_follow(target, READ_CONTROL | FILE_READ_ATTRIBUTES, FILE_SHARE_READ);
    if (t != INVALID_HANDLE_VALUE) {
      const bool ok = file_acceptable(t, ownerTrusted_);
      CloseHandle(t);
      if (!ok) {
        discard();
        return fail(ERROR_ACCESS_DENIED, "existing-record-untrusted");
      }
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
      const DWORD e = GetLastError();
      discard();
      return fail(e, "existing-record-unreadable");
    }
  }
  std::vector<uint8_t> buf(sizeof(FILE_RENAME_INFO) + (target.size() + 1) * sizeof(wchar_t));
  auto* ri = reinterpret_cast<FILE_RENAME_INFO*>(buf.data());
  ri->ReplaceIfExists = TRUE;
  ri->RootDirectory = nullptr;
  ri->FileNameLength = static_cast<DWORD>(target.size() * sizeof(wchar_t));
  std::memcpy(ri->FileName, target.c_str(), (target.size() + 1) * sizeof(wchar_t));
  if (!SetFileInformationByHandle(h, FileRenameInfo, ri, static_cast<DWORD>(buf.size()))) {
    const DWORD e = GetLastError();
    discard();
    return fail(e, "replace");
  }
  CloseHandle(h);
  if (written) *written = next;
  if (why) why->clear();
  return 0;
}

}  // namespace remote60::native_poc::capture_recovery

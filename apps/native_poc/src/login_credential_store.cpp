#include "login_credential_store.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <sstream>
#include <utility>

#pragma comment(lib, "crypt32.lib")

namespace remote60::native_poc::login_store {
namespace {

// Part of what DPAPI protects with. A blob made for something else -- the viewer's unlock
// password is protected the same way, under another string -- does not open as one of these.
const wchar_t kEntropy[] = L"GNLink-login-v1";
const char kCredentialHeader[] = "gnlink-login-credential-1";
const char kRevokeHeader[] = "gnlink-login-revoke-1";

constexpr size_t kMaxFileBytes = 64 * 1024;
constexpr size_t kMaxFieldBytes = 512;

bool read_file(const std::wstring& path, std::string* out, bool* exists) {
  out->clear();
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    if (exists) *exists = !(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
    return false;
  }
  if (exists) *exists = true;
  LARGE_INTEGER size{};
  bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
            static_cast<uint64_t>(size.QuadPart) <= kMaxFileBytes;
  if (ok) {
    out->resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    ok = out->empty() ||
         (ReadFile(file, out->data(), static_cast<DWORD>(out->size()), &read, nullptr) &&
          read == out->size());
  }
  CloseHandle(file);
  if (!ok) out->clear();
  return ok;
}

/** Beside the target, then moved over it: a reader never sees half of a write. */
bool write_file_atomically(const std::wstring& path, const std::string& bytes) {
  const std::wstring temporary = path + L".tmp";
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  bool ok = bytes.empty() ||
            (WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
             written == bytes.size());
  ok = ok && FlushFileBuffers(file);
  CloseHandle(file);
  if (ok) {
    ok = MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  }
  if (!ok) DeleteFileW(temporary.c_str());
  return ok;
}

bool gone(const std::wstring& path) {
  if (DeleteFileW(path.c_str())) return true;
  const DWORD error = GetLastError();
  return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

void wipe(std::string* text) {
  if (!text->empty()) SecureZeroMemory(text->data(), text->size());
  text->clear();
}

bool protect(const std::string& plain, std::string* out) {
  DATA_BLOB in{};
  in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
  in.cbData = static_cast<DWORD>(plain.size());
  DATA_BLOB entropy{};
  entropy.pbData = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(kEntropy));
  entropy.cbData = static_cast<DWORD>(sizeof(kEntropy));
  DATA_BLOB sealed{};
  // User scope, and never a prompt: this runs while a window is coming up.
  if (!CryptProtectData(&in, L"GNLink sign-in", &entropy, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &sealed)) {
    return false;
  }
  out->assign(reinterpret_cast<const char*>(sealed.pbData), sealed.cbData);
  LocalFree(sealed.pbData);
  return true;
}

bool unprotect(const std::string& sealed, std::string* out) {
  if (sealed.empty()) return false;
  DATA_BLOB in{};
  in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(sealed.data()));
  in.cbData = static_cast<DWORD>(sealed.size());
  DATA_BLOB entropy{};
  entropy.pbData = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(kEntropy));
  entropy.cbData = static_cast<DWORD>(sizeof(kEntropy));
  DATA_BLOB plain{};
  if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          &plain)) {
    return false;
  }
  out->assign(reinterpret_cast<const char*>(plain.pbData), plain.cbData);
  SecureZeroMemory(plain.pbData, plain.cbData);
  LocalFree(plain.pbData);
  return true;
}

/** A field is one line: nothing in it may end the line or exceed what a token ever is. */
bool field_ok(const std::string& value, bool required) {
  if (value.empty()) return !required;
  if (value.size() > kMaxFieldBytes) return false;
  for (const char c : value) {
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return false;
  }
  return true;
}

std::vector<std::string> lines_of(const std::string& text) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at <= text.size()) {
    const size_t end = text.find('\n', at);
    if (end == std::string::npos) {
      if (at < text.size()) out.push_back(text.substr(at));
      break;
    }
    out.push_back(text.substr(at, end - at));
    at = end + 1;
  }
  return out;
}

std::vector<std::string> split_tabs(const std::string& line) {
  std::vector<std::string> out;
  size_t at = 0;
  for (;;) {
    const size_t end = line.find('\t', at);
    out.push_back(line.substr(at, end == std::string::npos ? std::string::npos : end - at));
    if (end == std::string::npos) break;
    at = end + 1;
  }
  return out;
}

bool pending_ok(const PendingRevoke& p) {
  return field_ok(p.serverOrigin, true) && field_ok(p.deviceId, true) &&
         field_ok(p.revokeToken, true) && p.serverOrigin.find('\t') == std::string::npos &&
         p.deviceId.find('\t') == std::string::npos &&
         p.revokeToken.find('\t') == std::string::npos;
}

bool save_pending(const std::wstring& path, const std::vector<PendingRevoke>& list) {
  if (list.empty()) return gone(path);
  std::string plain = std::string(kRevokeHeader) + "\n";
  for (const PendingRevoke& p : list) {
    plain += p.serverOrigin + "\t" + p.deviceId + "\t" + p.revokeToken + "\n";
  }
  std::string sealed;
  const bool ok = protect(plain, &sealed) && write_file_atomically(path, sealed);
  wipe(&plain);
  return ok;
}

}  // namespace

// ------------------------------------------------------------------------------ the lock

Store::Lock::Lock(Lock&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

Store::Lock& Store::Lock::operator=(Lock&& other) noexcept {
  if (this != &other) {
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = std::exchange(other.handle_, nullptr);
  }
  return *this;
}

Store::Lock::~Lock() {
  if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
}

Store::Store(std::wstring directory) : directory_(std::move(directory)) {
  while (!directory_.empty() && (directory_.back() == L'\\' || directory_.back() == L'/')) {
    directory_.pop_back();
  }
  CreateDirectoryW(directory_.c_str(), nullptr);
}

std::wstring Store::credential_path() const { return directory_ + L"\\login.cred"; }
std::wstring Store::revoke_path() const { return directory_ + L"\\login.cred.revoke"; }
std::wstring Store::generation_path() const { return directory_ + L"\\login.cred.gen"; }
std::wstring Store::lock_path() const { return directory_ + L"\\login.cred.lock"; }

Store::Lock Store::Acquire(uint32_t waitMs) const {
  Lock lock;
  const uint64_t deadline = GetTickCount64() + waitMs;
  for (;;) {
    // No sharing at all: a second opener fails with a sharing violation for as long as this
    // handle lives, in whatever process or logon session it is. The handle going away -- by
    // scope, or by the process dying -- is the release, so a crash cannot leave it held.
    HANDLE handle = CreateFileW(lock_path().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
      lock.handle_ = handle;
      return lock;
    }
    const DWORD error = GetLastError();
    const bool busy = error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION ||
                      error == ERROR_ACCESS_DENIED;
    if (!busy || GetTickCount64() >= deadline) return lock;
    Sleep(25);
  }
}

// ------------------------------------------------------------------------------ the credential

ReadResult Store::Load(const Lock& lock, Credential* out, std::string* why) const {
  if (out) *out = Credential{};
  if (!lock.held()) {
    if (why) *why = "the store is not locked";
    return ReadResult::Unreadable;
  }
  std::string sealed;
  bool exists = false;
  if (!read_file(credential_path(), &sealed, &exists)) {
    if (!exists) return ReadResult::None;
    if (why) *why = "the file could not be read";
    return ReadResult::Unreadable;
  }
  std::string plain;
  if (!unprotect(sealed, &plain)) {
    // Damaged, truncated, or protected for another user. Which of those is not something the
    // caller can act on differently, and the error code is all there is to say.
    if (why) *why = "the file could not be opened for this user (" +
                    std::to_string(GetLastError()) + ")";
    return ReadResult::Unreadable;
  }
  const std::vector<std::string> lines = lines_of(plain);
  wipe(&plain);
  Credential read;
  const bool shaped = lines.size() == 6 && lines[0] == kCredentialHeader;
  if (shaped) {
    read.serverOrigin = lines[1];
    read.accountId = lines[2];
    read.deviceId = lines[3];
    read.deviceCredential = lines[4];
    read.revokeToken = lines[5];
  }
  if (!shaped || !field_ok(read.serverOrigin, true) || !field_ok(read.accountId, true) ||
      !field_ok(read.deviceId, true) || !field_ok(read.deviceCredential, true) ||
      !field_ok(read.revokeToken, true)) {
    if (why) *why = "the file is not a sign-in credential";
    return ReadResult::Unreadable;
  }
  if (out) *out = std::move(read);
  return ReadResult::Ok;
}

bool Store::Save(const Lock& lock, const Credential& credential, std::string* why) const {
  if (!lock.held()) {
    if (why) *why = "the store is not locked";
    return false;
  }
  if (!field_ok(credential.serverOrigin, true) || !field_ok(credential.accountId, true) ||
      !field_ok(credential.deviceId, true) || !field_ok(credential.deviceCredential, true) ||
      !field_ok(credential.revokeToken, true)) {
    if (why) *why = "a field is missing or is not one line";
    return false;
  }
  std::string plain = std::string(kCredentialHeader) + "\n" + credential.serverOrigin + "\n" +
                      credential.accountId + "\n" + credential.deviceId + "\n" +
                      credential.deviceCredential + "\n" + credential.revokeToken + "\n";
  std::string sealed;
  const bool sealedOk = protect(plain, &sealed);
  wipe(&plain);
  if (!sealedOk) {
    if (why) *why = "could not protect it (" + std::to_string(GetLastError()) + ")";
    return false;
  }
  if (!write_file_atomically(credential_path(), sealed)) {
    if (why) *why = "could not write it (" + std::to_string(GetLastError()) + ")";
    return false;
  }
  return true;
}

bool Store::Erase(const Lock& lock) const {
  if (!lock.held()) return false;
  const bool temporaryGone = gone(credential_path() + L".tmp");
  return gone(credential_path()) && temporaryGone;
}

// ------------------------------------------------------------------------------ the counter

uint64_t Store::Generation(const Lock& lock) const {
  if (!lock.held()) return 0;
  std::string text;
  if (!read_file(generation_path(), &text, nullptr)) return 0;
  uint64_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') break;
    value = value * 10 + static_cast<uint64_t>(c - '0');
  }
  return value;
}

uint64_t Store::BumpGeneration(const Lock& lock) const {
  if (!lock.held()) return 0;
  const uint64_t next = Generation(lock) + 1;
  return write_file_atomically(generation_path(), std::to_string(next) + "\n") ? next : 0;
}

// ------------------------------------------------------------------------------ sign-outs owed

ReadResult Store::LoadPendingRevokes(const Lock& lock, std::vector<PendingRevoke>* out) const {
  if (out) out->clear();
  if (!lock.held()) return ReadResult::Unreadable;
  std::string sealed;
  bool exists = false;
  if (!read_file(revoke_path(), &sealed, &exists)) {
    return exists ? ReadResult::Unreadable : ReadResult::None;
  }
  std::string plain;
  if (!unprotect(sealed, &plain)) return ReadResult::Unreadable;
  const std::vector<std::string> lines = lines_of(plain);
  wipe(&plain);
  if (lines.empty() || lines[0] != kRevokeHeader) return ReadResult::Unreadable;
  std::vector<PendingRevoke> list;
  for (size_t i = 1; i < lines.size(); ++i) {
    if (lines[i].empty()) continue;
    const std::vector<std::string> fields = split_tabs(lines[i]);
    if (fields.size() != 3) return ReadResult::Unreadable;
    PendingRevoke p{fields[0], fields[1], fields[2]};
    if (!pending_ok(p)) return ReadResult::Unreadable;
    list.push_back(std::move(p));
  }
  if (list.empty()) return ReadResult::None;
  if (out) *out = std::move(list);
  return ReadResult::Ok;
}

bool Store::AddPendingRevoke(const Lock& lock, const PendingRevoke& pending,
                             std::string* why) const {
  if (!lock.held()) {
    if (why) *why = "the store is not locked";
    return false;
  }
  if (!pending_ok(pending)) {
    if (why) *why = "a field is missing or is not one line";
    return false;
  }
  std::vector<PendingRevoke> list;
  // A list that cannot be read is NOT started again. Writing a new one over it would throw
  // away sign-outs that are still owed -- devices still alive at the directory, and the only
  // copies of the tokens that end them. It stays as it is and this one is refused; the caller
  // has to end the device some other way, or not at all.
  if (LoadPendingRevokes(lock, &list) == ReadResult::Unreadable) {
    if (why) *why = "the list of owed sign-outs cannot be read, and is not written over";
    return false;
  }
  list.erase(std::remove_if(list.begin(), list.end(),
                            [&](const PendingRevoke& p) { return p.deviceId == pending.deviceId; }),
             list.end());
  // Full is refused too. The oldest entry is as much owed as the newest, and dropping it to
  // make room is forgetting a device that is still signed in somewhere.
  if (list.size() >= kMaxPendingRevokes) {
    if (why) *why = "the list of owed sign-outs is full";
    return false;
  }
  list.push_back(pending);
  if (!save_pending(revoke_path(), list)) {
    if (why) *why = "could not write it (" + std::to_string(GetLastError()) + ")";
    return false;
  }
  return true;
}

bool Store::SetAsidePendingRevokes(const Lock& lock, std::string* movedTo) const {
  if (!lock.held()) return false;
  std::vector<PendingRevoke> list;
  if (LoadPendingRevokes(lock, &list) != ReadResult::Unreadable) return false;
  // Kept, whole, under a name nothing reads: what is in it cannot be opened here, and may yet
  // be by whoever comes to find out why.
  for (int n = 1; n <= 64; ++n) {
    const std::wstring aside = revoke_path() + L".unreadable-" + std::to_wstring(n);
    if (MoveFileExW(revoke_path().c_str(), aside.c_str(), MOVEFILE_WRITE_THROUGH)) {
      if (movedTo) *movedTo = "login.cred.revoke.unreadable-" + std::to_string(n);
      return true;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS && GetLastError() != ERROR_FILE_EXISTS) return false;
  }
  return false;
}

bool Store::RemovePendingRevoke(const Lock& lock, const std::string& deviceId) const {
  if (!lock.held()) return false;
  std::vector<PendingRevoke> list;
  const ReadResult read = LoadPendingRevokes(lock, &list);
  if (read == ReadResult::None) return true;
  if (read == ReadResult::Unreadable) return false;
  list.erase(std::remove_if(list.begin(), list.end(),
                            [&](const PendingRevoke& p) { return p.deviceId == deviceId; }),
             list.end());
  return save_pending(revoke_path(), list);
}

}  // namespace remote60::native_poc::login_store

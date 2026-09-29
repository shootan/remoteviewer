// The stored sign-in: what is on disk, who may touch it, and what each step leaves behind.
//
// The store and the flow are the product's (login_credential_store.cpp, login_flow.cpp). The
// directory is three functions this file owns, which count what they are asked, answer what a
// case tells them to, and can be made to answer late -- a real server cannot be told to be slow
// at exactly the moment a sign-out happens.
//
// Everything is written under a directory this process creates beside its own executable, one
// per case, and removes at the end. DPAPI is the real one, for the user running the test.
//
// What this does not show: a real directory (client_auto_login_runner.js does), another
// Windows user, another logon session, or a machine being switched off mid-write.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "login_credential_store.hpp"
#include "login_flow.hpp"

#pragma comment(lib, "crypt32.lib")

namespace {

using namespace remote60::native_poc;
using login_flow::Deps;
using login_flow::Remembered;
using login_flow::Return;
using login_store::Credential;
using login_store::PendingRevoke;
using login_store::ReadResult;
using login_store::Store;

int gChecks = 0;
int gFailures = 0;

void check(const std::string& what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ",
              detail.c_str());
  std::fflush(stdout);
}

const char kOrigin[] = "http://127.0.0.1:18200";

std::wstring gRoot;
int gCase = 0;

std::wstring self_path() {
  wchar_t path[MAX_PATH * 2]{};
  GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
  return path;
}

std::wstring fresh_dir() {
  const std::wstring dir = gRoot + L"\\case-" + std::to_wstring(++gCase);
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::string read_bytes(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_bytes(const std::wstring& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << bytes;
}

bool exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

/** Removes only what this run made: the files a store writes, then the directory. */
void remove_case_dirs() {
  for (int i = 1; i <= gCase; ++i) {
    const std::wstring dir = gRoot + L"\\case-" + std::to_wstring(i);
    for (const wchar_t* name : {L"login.cred", L"login.cred.tmp", L"login.cred.revoke",
                                L"login.cred.revoke.tmp", L"login.cred.gen",
                                L"login.cred.gen.tmp", L"login.cred.lock",
                                L"login.cred.revoke.unreadable-1"}) {
      DeleteFileW((dir + L"\\" + name).c_str());
      RemoveDirectoryW((dir + L"\\" + name).c_str());   // a case that blocked a path with one
    }
    RemoveDirectoryW(dir.c_str());
  }
  RemoveDirectoryW(gRoot.c_str());
}

Credential credential(const std::string& n, const std::string& origin = kOrigin) {
  return Credential{origin, "tester", "device-" + n + "-0123456789abcdef",
                    "credential-" + n + "-7c1e9a4455", "revoke-" + n + "-b20f6d3318"};
}

/** The directory, as three functions that remember what they were asked. */
struct FakeDirectory {
  std::mutex mu;
  std::vector<std::string> presented;   // credentials handed to refresh, in order
  std::vector<std::string> revoked;     // device ids handed to revoke, in order
  std::vector<std::string> revokeTokens;
  SessionCall refreshAnswer = SessionCall::Ok;
  SessionCall revokeAnswer = SessionCall::Ok;
  std::atomic<int> issued{0};
  HANDLE holdRefresh = nullptr;         // when set, refresh waits on it before answering
  HANDLE refreshEntered = nullptr;      // ...and signals this when it has been called
  std::vector<std::string> log;

  Deps deps(uint32_t lockWaitMs = 4000) {
    Deps d;
    d.serverOrigin = kOrigin;
    d.lockWaitMs = lockWaitMs;
    d.refresh = [this](const std::string& deviceId, const std::string& cred, DeviceSignIn* out,
                       std::string* error) {
      {
        std::lock_guard<std::mutex> lock(mu);
        presented.push_back(cred);
      }
      if (refreshEntered) SetEvent(refreshEntered);
      if (holdRefresh) WaitForSingleObject(holdRefresh, 20000);
      if (refreshAnswer != SessionCall::Ok) {
        if (error) *error = "fixture refusal";
        return refreshAnswer;
      }
      const int n = ++issued;
      out->deviceId = deviceId;
      out->sessionToken = "session-" + std::to_string(n) + "-5e0a71c9d3";
      out->deviceCredential = "rotated-" + std::to_string(n) + "-91aa04f6e2";
      return SessionCall::Ok;
    };
    d.revoke = [this](const std::string& deviceId, const std::string& token, std::string* error) {
      std::lock_guard<std::mutex> lock(mu);
      revoked.push_back(deviceId);
      revokeTokens.push_back(token);
      if (revokeAnswer != SessionCall::Ok && error) *error = "fixture refusal";
      return revokeAnswer;
    };
    d.log = [this](const std::string& line) {
      std::lock_guard<std::mutex> lock(mu);
      log.push_back(line);
    };
    return d;
  }
};

bool seed(const Store& store, const Credential& c) {
  Store::Lock lock = store.Acquire(1000);
  return lock.held() && store.Save(lock, c);
}

ReadResult load(const Store& store, Credential* out) {
  Store::Lock lock = store.Acquire(1000);
  return store.Load(lock, out);
}

uint64_t generation(const Store& store) {
  Store::Lock lock = store.Acquire(1000);
  return store.Generation(lock);
}

std::vector<PendingRevoke> owed(const Store& store) {
  std::vector<PendingRevoke> list;
  Store::Lock lock = store.Acquire(1000);
  (void)store.LoadPendingRevokes(lock, &list);
  return list;
}

// Every secret any case used, for the last check.
std::vector<std::string> gSecrets;
void secret(const Credential& c) {
  gSecrets.push_back(c.deviceCredential);
  gSecrets.push_back(c.revokeToken);
}
std::vector<std::string> gLogLines;
void keep_log(FakeDirectory& dir) {
  std::lock_guard<std::mutex> lock(dir.mu);
  gLogLines.insert(gLogLines.end(), dir.log.begin(), dir.log.end());
}

// ------------------------------------------------------------------------------ the store

void test_store_files() {
  const Store store(fresh_dir());
  const Credential c = credential("store");
  secret(c);
  check("[store] a credential is stored", seed(store, c));
  const std::string bytes = read_bytes(store.credential_path());
  check("[store] the file holds none of it in the clear",
        !bytes.empty() && bytes.find(c.deviceCredential) == std::string::npos &&
            bytes.find(c.revokeToken) == std::string::npos &&
            bytes.find(c.deviceId) == std::string::npos &&
            bytes.find(c.accountId) == std::string::npos);
  Credential back;
  check("[store] it reads back whole",
        load(store, &back) == ReadResult::Ok && back.serverOrigin == c.serverOrigin &&
            back.accountId == c.accountId && back.deviceId == c.deviceId &&
            back.deviceCredential == c.deviceCredential && back.revokeToken == c.revokeToken);
  check("[store] no temporary file is left beside it", !exists(store.credential_path() + L".tmp"));

  std::string damaged = bytes;
  damaged[damaged.size() / 2] = static_cast<char>(damaged[damaged.size() / 2] ^ 0x5a);
  write_bytes(store.credential_path(), damaged);
  check("[store] a damaged file is unreadable, not empty and not half a credential",
        load(store, &back) == ReadResult::Unreadable && back.deviceCredential.empty());
  write_bytes(store.credential_path(), bytes.substr(0, bytes.size() / 2));
  check("[store] a truncated file is unreadable", load(store, &back) == ReadResult::Unreadable);
  write_bytes(store.credential_path(), "");
  check("[store] an empty file is unreadable", load(store, &back) == ReadResult::Unreadable);

  // Protected for this user, but as something else: the entropy is part of the protection.
  {
    const std::string plain = "gnlink-login-credential-1\nx\ny\nz\nw\nv\n";
    DATA_BLOB in{static_cast<DWORD>(plain.size()),
                 reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB out{};
    const bool sealed = CryptProtectData(&in, L"other", nullptr, nullptr, nullptr,
                                         CRYPTPROTECT_UI_FORBIDDEN, &out) != FALSE;
    if (sealed) {
      write_bytes(store.credential_path(),
                  std::string(reinterpret_cast<const char*>(out.pbData), out.cbData));
      LocalFree(out.pbData);
    }
    check("[store] a blob protected as something else does not open as a credential",
          sealed && load(store, &back) == ReadResult::Unreadable);
  }

  {
    Store::Lock lock = store.Acquire(1000);
    Credential bad = c;
    bad.accountId = "two\nlines";
    check("[store] a field that is not one line is refused", !store.Save(lock, bad));
    bad = c;
    bad.revokeToken.clear();
    check("[store] a credential with a field missing is refused", !store.Save(lock, bad));
    check("[store] erasing leaves nothing", store.Erase(lock) && !exists(store.credential_path()));
    check("[store] erasing nothing is still done", store.Erase(lock));
  }
  check("[store] no file is no credential", load(store, &back) == ReadResult::None);

  {
    Store::Lock none;
    check("[store] nothing is read, written or erased without the lock",
          store.Load(none, &back) == ReadResult::Unreadable && !store.Save(none, c) &&
              !store.Erase(none) && store.BumpGeneration(none) == 0 &&
              !store.AddPendingRevoke(none, PendingRevoke{kOrigin, "d", "r"}));
  }
}

void test_store_generation_and_owed() {
  const Store store(fresh_dir());
  {
    Store::Lock lock = store.Acquire(1000);
    check("[store] the counter starts at nothing and moves by one",
          store.Generation(lock) == 0 && store.BumpGeneration(lock) == 1 &&
              store.BumpGeneration(lock) == 2 && store.Generation(lock) == 2);
  }
  check("[store] ...and is still there for whoever opens the store next",
        generation(Store(store.directory())) == 2);

  {
    Store::Lock lock = store.Acquire(1000);
    for (int i = 0; i < 20; ++i) {
      const std::string n = std::to_string(i);
      gSecrets.push_back("owed-revoke-token-" + n + "-c4d1");
      store.AddPendingRevoke(lock, PendingRevoke{kOrigin, "owed-device-" + n,
                                                 "owed-revoke-token-" + n + "-c4d1"});
    }
  }
  std::vector<PendingRevoke> list = owed(store);
  check("[store] sign-outs owed are a bounded list: past the bound none is added, none dropped",
        list.size() == login_store::kMaxPendingRevokes && list.front().deviceId == "owed-device-0" &&
            list.back().deviceId == "owed-device-15",
        std::to_string(list.size()));
  const std::string bytes = read_bytes(store.revoke_path());
  check("[store] the list is protected like the credential",
        !bytes.empty() && bytes.find("owed-revoke-token") == std::string::npos &&
            bytes.find("owed-device") == std::string::npos);
  {
    Store::Lock lock = store.Acquire(1000);
    // An entry for a device already in the list replaces it, full or not.
    store.AddPendingRevoke(lock, PendingRevoke{kOrigin, "owed-device-10", "replaced-token-77e0"});
    store.RemovePendingRevoke(lock, "owed-device-15");
  }
  list = owed(store);
  bool replaced = false, removed = true;
  int tens = 0;
  for (const PendingRevoke& p : list) {
    if (p.deviceId == "owed-device-10") { ++tens; replaced = p.revokeToken == "replaced-token-77e0"; }
    if (p.deviceId == "owed-device-15") removed = false;
  }
  check("[store] one entry per device, the later one kept; an entry can be removed",
        tens == 1 && replaced && removed && list.size() == login_store::kMaxPendingRevokes - 1);
  {
    Store::Lock lock = store.Acquire(1000);
    for (const PendingRevoke& p : list) store.RemovePendingRevoke(lock, p.deviceId);
  }
  check("[store] an empty list leaves no file", !exists(store.revoke_path()));
}

// A second process, holding the store. argv: --hold-store <dir> <milliseconds>
int hold_store_mode(const std::wstring& dir, DWORD ms) {
  const Store store(dir);
  Store::Lock lock = store.Acquire(2000);
  if (!lock.held()) return 3;
  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  write_bytes(dir + L"\\holder.ready", "1");
  WaitForSingleObject(ready, ms);
  CloseHandle(ready);
  return 0;
}

void test_store_lock() {
  const Store store(fresh_dir());
  {
    Store::Lock first = store.Acquire(1000);
    const uint64_t began = GetTickCount64();
    Store::Lock second = store.Acquire(300);
    const uint64_t waited = GetTickCount64() - began;
    check("[lock] while one holds the store, another does not get it",
          first.held() && !second.held());
    check("[lock] ...and gives up when its wait is over, not before and not much after",
          waited >= 250 && waited < 2000, std::to_string(waited) + " ms");
  }
  {
    Store::Lock again = store.Acquire(300);
    check("[lock] once it is let go, the next one has it", again.held());
  }

  // Another PROCESS. The lock is a file opened with no sharing, so it is the same lock from
  // anywhere that can reach the file.
  const std::wstring command = L"\"" + self_path() + L"\" --hold-store \"" + store.directory() +
                               L"\" 60000";
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  const bool started = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != FALSE;
  check("[lock] (a second process starts)", started);
  if (!started) return;
  bool holding = false;
  for (int i = 0; i < 200 && !holding; ++i) {
    holding = exists(store.directory() + L"\\holder.ready");
    if (!holding) Sleep(25);
  }
  {
    Store::Lock mine = store.Acquire(300);
    check("[lock] A STORE HELD BY ANOTHER PROCESS IS NOT HAD BY THIS ONE",
          holding && !mine.held());
  }
  // The holder dies without letting go, the way a crashed client would.
  TerminateProcess(pi.hProcess, 9);   // the process this test started, by the handle it was given
  WaitForSingleObject(pi.hProcess, 10000);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  {
    Store::Lock mine = store.Acquire(2000);
    check("[lock] a holder that died has let go", mine.held());
  }
  DeleteFileW((store.directory() + L"\\holder.ready").c_str());
}

// ------------------------------------------------------------------------------ coming back

void test_come_back() {
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const auto r = login_flow::come_back(store, dir.deps());
    check("[return] nothing stored: the sign-in form, and the directory is not asked",
          r.outcome == Return::NoCredential && dir.presented.empty());
    keep_log(dir);
  }
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Credential c = credential("ok");
    secret(c);
    seed(store, c);
    const uint64_t before = generation(store);
    const auto r = login_flow::come_back(store, dir.deps());
    Credential after;
    const bool read = load(store, &after) == ReadResult::Ok;
    gSecrets.push_back(after.deviceCredential);
    gSecrets.push_back(r.sessionToken);
    check("[return] a stored credential signs in with no password",
          r.outcome == Return::SignedIn && !r.sessionToken.empty() && r.accountId == "tester");
    check("[return] what was presented is what was stored",
          dir.presented.size() == 1 && dir.presented[0] == c.deviceCredential);
    check("[return] THE NEXT CREDENTIAL IS ON DISK, the same device and revoke token with it",
          read && after.deviceCredential == "rotated-1-91aa04f6e2" &&
              after.deviceId == c.deviceId && after.revokeToken == c.revokeToken);
    check("[return] the counter did not move: nothing was signed in or out",
          generation(store) == before);
    keep_log(dir);
  }
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Credential c = credential("rejected");
    secret(c);
    seed(store, c);
    dir.refreshAnswer = SessionCall::Rejected;
    const uint64_t before = generation(store);
    const auto r = login_flow::come_back(store, dir.deps());
    Credential after;
    check("[return] refused by the directory: the credential is erased, the id is kept for the form",
          r.outcome == Return::Rejected && load(store, &after) == ReadResult::None &&
              r.accountId == "tester" && r.sessionToken.empty());
    check("[return] ...and the counter moved", generation(store) == before + 1);
    keep_log(dir);
  }
  for (const auto& [answer, name] :
       {std::pair{SessionCall::Limited, "429"}, std::pair{SessionCall::Failed, "5xx"},
        std::pair{SessionCall::Unreachable, "no answer"}}) {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Credential c = credential(std::string("later-") + name[0]);
    secret(c);
    seed(store, c);
    const std::string bytes = read_bytes(store.credential_path());
    dir.refreshAnswer = answer;
    const auto r = login_flow::come_back(store, dir.deps());
    check(std::string("[return] ") + name + ": not an answer about the credential -- it is kept, byte for byte",
          r.outcome == Return::TryLater && read_bytes(store.credential_path()) == bytes &&
              r.sessionToken.empty());
    keep_log(dir);
  }
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Credential c = credential("old-server");
    secret(c);
    seed(store, c);
    const std::string bytes = read_bytes(store.credential_path());
    dir.refreshAnswer = SessionCall::Unsupported;
    const auto r = login_flow::come_back(store, dir.deps());
    check("[return] a directory with no refresh route: kept, and the form is shown",
          r.outcome == Return::ServerCannot && read_bytes(store.credential_path()) == bytes);
    keep_log(dir);
  }
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Credential c = credential("elsewhere", "https://another.example:443");
    secret(c);
    seed(store, c);
    const std::string bytes = read_bytes(store.credential_path());
    const auto r = login_flow::come_back(store, dir.deps());
    check("[return] A CREDENTIAL ISSUED BY ANOTHER SERVER IS NOT PRESENTED",
          r.outcome == Return::NoCredential && dir.presented.empty());
    check("[return] ...and not erased", read_bytes(store.credential_path()) == bytes);
    keep_log(dir);
  }
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Credential c = credential("damaged");
    secret(c);
    seed(store, c);
    std::string bytes = read_bytes(store.credential_path());
    bytes[bytes.size() / 3] = static_cast<char>(bytes[bytes.size() / 3] ^ 0x33);
    write_bytes(store.credential_path(), bytes);
    const auto r = login_flow::come_back(store, dir.deps());
    check("[return] a file that cannot be read: the form, nothing sent, the file left as found",
          r.outcome == Return::Unreadable && dir.presented.empty() &&
              read_bytes(store.credential_path()) == bytes);
    keep_log(dir);
  }
  {
    // A sign-out that got as far as being written down, and no further.
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Credential c = credential("half-out");
    secret(c);
    seed(store, c);
    {
      Store::Lock lock = store.Acquire(1000);
      store.AddPendingRevoke(lock, PendingRevoke{c.serverOrigin, c.deviceId, c.revokeToken});
    }
    const auto r = login_flow::come_back(store, dir.deps());
    Credential after;
    check("[return] A SIGN-OUT THAT WAS WRITTEN DOWN WINS: the credential is not presented",
          r.outcome == Return::SignedOut && dir.presented.empty());
    check("[return] ...it is erased, and the sign-out is still owed to the directory",
          load(store, &after) == ReadResult::None && owed(store).size() == 1);
    keep_log(dir);
  }
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Credential c = credential("busy");
    secret(c);
    seed(store, c);
    Store::Lock held = store.Acquire(1000);
    const auto r = login_flow::come_back(store, dir.deps(300));
    check("[return] a store somebody else holds: nothing is sent, and it says so",
          r.outcome == Return::Busy && dir.presented.empty() && !r.detail.empty());
    keep_log(dir);
  }
}

void test_two_at_once() {
  const Store store(fresh_dir());
  FakeDirectory dir;
  const Credential c = credential("race");
  secret(c);
  seed(store, c);
  dir.holdRefresh = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  dir.refreshEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);

  login_flow::ReturnResult first, second;
  std::thread a([&] { first = login_flow::come_back(store, dir.deps(15000)); });
  // The first is inside its refresh, holding the store.
  const bool entered = WaitForSingleObject(dir.refreshEntered, 10000) == WAIT_OBJECT_0;
  std::thread b([&] { second = login_flow::come_back(store, dir.deps(15000)); });
  Sleep(400);
  size_t presentedWhileHeld = 0;
  {
    std::lock_guard<std::mutex> lock(dir.mu);
    presentedWhileHeld = dir.presented.size();
  }
  SetEvent(dir.holdRefresh);
  a.join();
  b.join();
  CloseHandle(dir.holdRefresh);
  CloseHandle(dir.refreshEntered);

  gSecrets.push_back(first.sessionToken);
  gSecrets.push_back(second.sessionToken);
  check("[two] while the first is refreshing, the second has sent nothing",
        entered && presentedWhileHeld == 1, std::to_string(presentedWhileHeld));
  check("[two] both come back signed in",
        first.outcome == Return::SignedIn && second.outcome == Return::SignedIn);
  check("[two] THE SECOND PRESENTED WHAT THE FIRST HAD JUST STORED, not what it started with",
        dir.presented.size() == 2 && dir.presented[0] == c.deviceCredential &&
            dir.presented[1] == "rotated-1-91aa04f6e2",
        dir.presented.size() == 2 ? dir.presented[1].substr(0, 9) : "?");
  Credential after;
  check("[two] what is on disk is the last one issued",
        load(store, &after) == ReadResult::Ok && after.deviceCredential == "rotated-2-91aa04f6e2");
  gSecrets.push_back(after.deviceCredential);
  gSecrets.push_back("rotated-1-91aa04f6e2");
  keep_log(dir);
}

// ------------------------------------------------------------------------------ signing in

DeviceSignIn signed_in(const std::string& n) {
  DeviceSignIn s;
  s.sessionToken = "login-session-" + n + "-0d5b";
  s.deviceId = "login-device-" + n + "-fedcba9876";
  s.deviceCredential = "login-credential-" + n + "-3f8e";
  s.revokeToken = "login-revoke-" + n + "-a61c";
  gSecrets.push_back(s.sessionToken);
  gSecrets.push_back(s.deviceCredential);
  gSecrets.push_back(s.revokeToken);
  return s;
}

void test_sign_in() {
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Deps deps = dir.deps();
    const uint64_t g = login_flow::begin_sign_in(store, deps);
    const DeviceSignIn s = signed_in("first");
    const Remembered r = login_flow::remember_sign_in(store, deps, g, "tester", s);
    Credential after;
    check("[sign-in] a sign-in that succeeded is stored",
          g == 1 && r == Remembered::Stored && load(store, &after) == ReadResult::Ok &&
              after.deviceCredential == s.deviceCredential && after.revokeToken == s.revokeToken &&
              after.serverOrigin == kOrigin && after.accountId == "tester");
    check("[sign-in] nothing is ended by it", dir.revoked.empty() && owed(store).empty());

    // Signing in again, on purpose, over a credential that is already there.
    const uint64_t g2 = login_flow::begin_sign_in(store, deps);
    const DeviceSignIn s2 = signed_in("second");
    const Remembered r2 = login_flow::remember_sign_in(store, deps, g2, "tester", s2);
    const std::vector<PendingRevoke> list = owed(store);
    check("[sign-in] a second sign-in replaces it, and the device before it is to be ended",
          r2 == Remembered::Stored && load(store, &after) == ReadResult::Ok &&
              after.deviceId == s2.deviceId && list.size() == 1 &&
              list[0].deviceId == s.deviceId && list[0].revokeToken == s.revokeToken);
    check("[sign-in] ...which the directory is told the next time sign-outs are settled",
          login_flow::settle_owed_sign_outs(store, deps) == 0 && dir.revoked.size() == 1 &&
              dir.revoked[0] == s.deviceId && owed(store).empty());
    keep_log(dir);
  }
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Deps deps = dir.deps();
    DeviceSignIn plain;
    plain.sessionToken = "plain-session-1b7e";
    const uint64_t g = login_flow::begin_sign_in(store, deps);
    Credential after;
    check("[sign-in] a directory that issued no credential: the sign-in stands, nothing is stored",
          login_flow::remember_sign_in(store, deps, g, "tester", plain) == Remembered::NotIssued &&
              load(store, &after) == ReadResult::None && dir.revoked.empty());
    keep_log(dir);
  }
}

void test_late_answers() {
  {
    // The user signs in, then signs out, and THEN the sign-in's answer arrives.
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Deps deps = dir.deps();
    const uint64_t g = login_flow::begin_sign_in(store, deps);
    (void)login_flow::sign_out(store, deps);
    const DeviceSignIn late = signed_in("late");
    const Remembered r = login_flow::remember_sign_in(store, deps, g, "tester", late);
    Credential after;
    check("[late] A SIGN-IN ANSWERED AFTER THE SIGN-OUT IS NOT STORED",
          r == Remembered::Superseded && load(store, &after) == ReadResult::None);
    check("[late] ...and the device it was given is ended, with its own revoke token",
          dir.revoked.size() == 1 && dir.revoked[0] == late.deviceId &&
              dir.revokeTokens[0] == late.revokeToken);
    keep_log(dir);
  }
  {
    // The same, with the directory out of reach when the device has to be ended.
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Deps deps = dir.deps();
    const uint64_t g = login_flow::begin_sign_in(store, deps);
    (void)login_flow::sign_out(store, deps);
    dir.revokeAnswer = SessionCall::Unreachable;
    const DeviceSignIn late = signed_in("late-offline");
    const Remembered r = login_flow::remember_sign_in(store, deps, g, "tester", late);
    const std::vector<PendingRevoke> list = owed(store);
    Credential after;
    check("[late] when the directory cannot be told, ending that device is recorded as owed",
          r == Remembered::Superseded && load(store, &after) == ReadResult::None &&
              list.size() == 1 && list[0].deviceId == late.deviceId);
    keep_log(dir);
  }
  {
    // Two sign-ins; the first one's answer arrives last.
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Deps deps = dir.deps();
    const uint64_t g1 = login_flow::begin_sign_in(store, deps);
    const uint64_t g2 = login_flow::begin_sign_in(store, deps);
    const DeviceSignIn second = signed_in("newer");
    const DeviceSignIn first = signed_in("older");
    const Remembered r2 = login_flow::remember_sign_in(store, deps, g2, "tester", second);
    const Remembered r1 = login_flow::remember_sign_in(store, deps, g1, "tester", first);
    Credential after;
    check("[late] an earlier sign-in answered after a later one does not replace it",
          r2 == Remembered::Stored && r1 == Remembered::Superseded &&
              load(store, &after) == ReadResult::Ok && after.deviceId == second.deviceId);
    check("[late] ...and only the earlier one's device is ended",
          dir.revoked.size() == 1 && dir.revoked[0] == first.deviceId);
    keep_log(dir);
  }
  {
    // Another PROCESS signed out while this one's sign-in was in flight. This process's own
    // counter of sign-ins knows nothing about that; the one in the store does.
    const Store store(fresh_dir());
    FakeDirectory mine, theirs;
    const uint64_t g = login_flow::begin_sign_in(store, mine.deps());
    const Store sameFiles(store.directory());   // what the other process has open
    (void)login_flow::sign_out(sameFiles, theirs.deps());
    const DeviceSignIn late = signed_in("other-process");
    const Remembered r = login_flow::remember_sign_in(store, mine.deps(), g, "tester", late);
    Credential after;
    check("[late] a sign-out made by another process makes this one's answer late too",
          r == Remembered::Superseded && load(store, &after) == ReadResult::None &&
              mine.revoked.size() == 1);
    keep_log(mine);
    keep_log(theirs);
  }
}

// ------------------------------------------------------------------------------ signing out

void test_sign_out() {
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Credential c = credential("out");
    secret(c);
    seed(store, c);
    const uint64_t before = generation(store);
    const auto r = login_flow::sign_out(store, dir.deps());
    Credential after;
    check("[sign-out] the credential is gone and the directory was told",
          r.localDone && r.serverTold && !r.owed && load(store, &after) == ReadResult::None &&
              dir.revoked.size() == 1 && dir.revoked[0] == c.deviceId &&
              dir.revokeTokens[0] == c.revokeToken);
    check("[sign-out] nothing is owed afterwards, and the counter moved",
          owed(store).empty() && generation(store) == before + 1);
    check("[sign-out] coming back afterwards is the sign-in form",
          login_flow::come_back(store, dir.deps()).outcome == Return::NoCredential &&
              dir.presented.empty());
    keep_log(dir);
  }
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Credential c = credential("offline");
    secret(c);
    seed(store, c);
    dir.revokeAnswer = SessionCall::Unreachable;
    const auto r = login_flow::sign_out(store, dir.deps());
    Credential after;
    const std::vector<PendingRevoke> list = owed(store);
    check("[sign-out] WITH THE DIRECTORY OUT OF REACH THE CREDENTIAL STILL GOES",
          r.localDone && !r.serverTold && load(store, &after) == ReadResult::None);
    check("[sign-out] ...and the sign-out is owed, with what it takes to make it",
          r.owed && list.size() == 1 && list[0].deviceId == c.deviceId &&
              list[0].revokeToken == c.revokeToken);
    check("[sign-out] while the directory stays out of reach it stays owed",
          login_flow::settle_owed_sign_outs(store, dir.deps()) == 1 && owed(store).size() == 1);
    dir.revokeAnswer = SessionCall::Ok;
    check("[sign-out] when it answers, the debt is settled",
          login_flow::settle_owed_sign_outs(store, dir.deps()) == 0 && owed(store).empty() &&
              dir.revoked.back() == c.deviceId);
    keep_log(dir);
  }
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const auto r = login_flow::sign_out(store, dir.deps());
    check("[sign-out] with nothing stored there is nothing to tell the directory",
          r.localDone && dir.revoked.empty() && owed(store).empty());
    keep_log(dir);
  }
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Credential c = credential("theirs", "https://another.example:443");
    secret(c);
    seed(store, c);
    const std::string bytes = read_bytes(store.credential_path());
    const auto r = login_flow::sign_out(store, dir.deps());
    check("[sign-out] another server's credential is not sent to this one, and not erased",
          dir.revoked.empty() && read_bytes(store.credential_path()) == bytes && r.localDone);
    {
      Store::Lock lock = store.Acquire(1000);
      store.AddPendingRevoke(lock, PendingRevoke{"https://another.example:443", c.deviceId,
                                                 c.revokeToken});
    }
    check("[sign-out] a sign-out owed to another server is not sent to this one either",
          login_flow::settle_owed_sign_outs(store, dir.deps()) == 1 && dir.revoked.empty() &&
              owed(store).size() == 1);
    keep_log(dir);
  }
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Credential c = credential("held");
    secret(c);
    seed(store, c);
    const std::string bytes = read_bytes(store.credential_path());
    Store::Lock held = store.Acquire(1000);
    const auto r = login_flow::sign_out(store, dir.deps(300));
    check("[sign-out] a store somebody else holds: it says it did not sign out, and sends nothing",
          !r.localDone && dir.revoked.empty() && !r.detail.empty() &&
              read_bytes(store.credential_path()) == bytes);
    keep_log(dir);
  }
}

// ------------------------------------------------------------------------------ when a write fails
//
// The review of 6e4764a found six places where something that could not be written was
// carried on from as if it had been. Each case below makes ONE write fail -- for real: the
// place the store writes to is occupied by a directory, so the file system refuses -- and
// looks at what is on disk and what the directory was sent afterwards.

/** Occupies `path` with a directory, so that nothing can be written or moved there. */
bool block_path(const std::wstring& path) {
  DeleteFileW(path.c_str());
  return CreateDirectoryW(path.c_str(), nullptr) != FALSE;
}
void unblock_path(const std::wstring& path) { RemoveDirectoryW(path.c_str()); }

/** The counter cannot be written: its temporary file's place is taken. */
struct BrokenCounter {
  std::wstring path;
  explicit BrokenCounter(const Store& store) : path(store.generation_path() + L".tmp") {
    block_path(path);
  }
  ~BrokenCounter() { unblock_path(path); }
};

/** The list of owed sign-outs cannot be written (it can still be read, or is absent). */
struct BrokenJournalWrite {
  std::wstring path;
  explicit BrokenJournalWrite(const Store& store) : path(store.revoke_path() + L".tmp") {
    block_path(path);
  }
  ~BrokenJournalWrite() { unblock_path(path); }
};

void test_r5_1_counter_cannot_be_written() {
  // A sign-in is in flight (generation g). The user signs out. The counter cannot be written.
  const Store store(fresh_dir());
  FakeDirectory dir;
  const Deps deps = dir.deps();
  const Credential c = credential("r5-1");
  secret(c);
  seed(store, c);
  const uint64_t g = login_flow::begin_sign_in(store, deps);
  const std::string before = read_bytes(store.credential_path());
  {
    BrokenCounter broken(store);
    const auto out = login_flow::sign_out(store, deps);
    check("[r5-1] the counter cannot be written: THE SIGN-OUT IS NOT MADE, and says so",
          !out.localDone && !out.serverTold && !out.detail.empty(), out.detail);
    check("[r5-1] ...nothing was erased, nothing was sent, nothing is owed",
          read_bytes(store.credential_path()) == before && dir.revoked.empty() &&
              owed(store).empty());
    check("[r5-1] ...and the counter is where it was", generation(store) == g);

    // The answer to the sign-in arrives now. There was no barrier to make it late.
    const DeviceSignIn late = signed_in("r5-1-late");
    const Remembered r = login_flow::remember_sign_in(store, deps, g, "tester", late);
    Credential after;
    check("[r5-1] THE ANSWER THAT ARRIVES AFTERWARDS IS NOT STORED",
          r == Remembered::NotSaved && load(store, &after) == ReadResult::Ok &&
              after.deviceId == c.deviceId,
          login_flow::remembered_name(r));
    check("[r5-1] ...and the device it carried is ended",
          dir.revoked.size() == 1 && dir.revoked[0] == late.deviceId);

    check("[r5-1] a sign-in cannot even begin: there is nothing to check its answer against",
          login_flow::begin_sign_in(store, deps) == 0);
  }
  // With the counter working again the sign-out goes through.
  const auto again = login_flow::sign_out(store, deps);
  Credential after;
  check("[r5-1] once the counter can be written the sign-out is made",
        again.localDone && load(store, &after) == ReadResult::None && generation(store) == g + 1);

  // The same rule where a credential leaves by being refused, or by a sign-out owed.
  {
    const Store refused(fresh_dir());
    FakeDirectory d2;
    const Credential c2 = credential("r5-1-refused");
    secret(c2);
    seed(refused, c2);
    const std::string bytes = read_bytes(refused.credential_path());
    d2.refreshAnswer = SessionCall::Rejected;
    BrokenCounter broken(refused);
    const auto r = login_flow::come_back(refused, d2.deps());
    check("[r5-1] a refused credential is not erased either, without the counter",
          r.outcome == Return::Rejected && r.sessionToken.empty() &&
              read_bytes(refused.credential_path()) == bytes);
    keep_log(d2);
  }
  {
    const Store halfOut(fresh_dir());
    FakeDirectory d3;
    const Credential c3 = credential("r5-1-owed");
    secret(c3);
    seed(halfOut, c3);
    {
      Store::Lock lock = halfOut.Acquire(1000);
      halfOut.AddPendingRevoke(lock, PendingRevoke{c3.serverOrigin, c3.deviceId, c3.revokeToken});
    }
    const std::string bytes = read_bytes(halfOut.credential_path());
    BrokenCounter broken(halfOut);
    const auto r = login_flow::come_back(halfOut, d3.deps());
    check("[r5-1] ...nor one with a sign-out owed; it is still not presented",
          r.outcome == Return::SignedOut && d3.presented.empty() &&
              read_bytes(halfOut.credential_path()) == bytes);
    keep_log(d3);
  }
  keep_log(dir);
}

void test_r5_2_sign_out_cannot_be_written_down() {
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    dir.revokeAnswer = SessionCall::Unreachable;
    const Credential c = credential("r5-2");
    secret(c);
    seed(store, c);
    const std::string before = read_bytes(store.credential_path());
    BrokenJournalWrite broken(store);
    const auto out = login_flow::sign_out(store, dir.deps());
    check("[r5-2] the sign-out cannot be written down and the directory cannot be told: "
          "IT IS NOT MADE, and says so",
          !out.localDone && !out.serverTold && !out.owed && !out.detail.empty(), out.detail);
    check("[r5-2] THE CREDENTIAL, AND WITH IT THE MEANS OF ENDING THE DEVICE, IS STILL THERE",
          read_bytes(store.credential_path()) == before);
    Credential kept;
    check("[r5-2] ...whole", load(store, &kept) == ReadResult::Ok &&
                                 kept.revokeToken == c.revokeToken);
    keep_log(dir);
  }
  {
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Credential c = credential("r5-2-told");
    secret(c);
    seed(store, c);
    BrokenJournalWrite broken(store);
    const auto out = login_flow::sign_out(store, dir.deps());
    Credential after;
    check("[r5-2] when the directory itself confirms the device is ended, the credential goes",
          out.localDone && out.serverTold && load(store, &after) == ReadResult::None &&
              dir.revoked.size() == 1 && dir.revoked[0] == c.deviceId);
    keep_log(dir);
  }
  {
    // Signing in over a stored sign-in, when the one before cannot be written down as owed.
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Deps deps = dir.deps();
    const Credential before = credential("r5-2-previous");
    secret(before);
    seed(store, before);
    const std::string bytes = read_bytes(store.credential_path());
    const uint64_t g = login_flow::begin_sign_in(store, deps);
    dir.revokeAnswer = SessionCall::Unreachable;
    BrokenJournalWrite broken(store);
    const DeviceSignIn s = signed_in("r5-2-new");
    const Remembered r = login_flow::remember_sign_in(store, deps, g, "tester", s);
    check("[r5-2] a sign-in does not write over one it could neither record nor end",
          r == Remembered::NotSaved && read_bytes(store.credential_path()) == bytes,
          login_flow::remembered_name(r));
    check("[r5-2] ...the directory was asked to end both: the one before, and the one not kept",
          dir.revoked.size() == 2 && dir.revoked[0] == before.deviceId &&
              dir.revoked[1] == s.deviceId);

    dir.revokeAnswer = SessionCall::Ok;
    dir.revoked.clear();
    const uint64_t g2 = login_flow::begin_sign_in(store, deps);
    const DeviceSignIn s2 = signed_in("r5-2-newer");
    const Remembered r2 = login_flow::remember_sign_in(store, deps, g2, "tester", s2);
    Credential after;
    check("[r5-2] when the directory confirms the one before is ended, the new one is stored",
          r2 == Remembered::Stored && load(store, &after) == ReadResult::Ok &&
              after.deviceId == s2.deviceId && dir.revoked.size() == 1 &&
              dir.revoked[0] == before.deviceId);
    keep_log(dir);
  }
}

void test_r5_3_the_list_of_owed_sign_outs() {
  {
    // Seventeen. Sixteen are owed; the directory is out of reach.
    const Store store(fresh_dir());
    FakeDirectory dir;
    dir.revokeAnswer = SessionCall::Unreachable;
    {
      Store::Lock lock = store.Acquire(1000);
      for (int i = 0; i < 16; ++i) {
        const std::string n = std::to_string(i);
        gSecrets.push_back("r5-3-token-" + n + "-e19b");
        store.AddPendingRevoke(lock, PendingRevoke{kOrigin, "r5-3-device-" + n,
                                                   "r5-3-token-" + n + "-e19b"});
      }
    }
    const std::string journal = read_bytes(store.revoke_path());
    const Credential c = credential("r5-3-17th");
    secret(c);
    seed(store, c);
    const std::string cred = read_bytes(store.credential_path());
    const auto out = login_flow::sign_out(store, dir.deps());
    const std::vector<PendingRevoke> list = owed(store);
    check("[r5-3] THE SEVENTEENTH DOES NOT PUSH THE FIRST OUT: all sixteen are still owed",
          list.size() == 16 && list.front().deviceId == "r5-3-device-0" &&
              list.back().deviceId == "r5-3-device-15" &&
              read_bytes(store.revoke_path()) == journal);
    check("[r5-3] ...and the seventeenth sign-out is not made: its credential is kept",
          !out.localDone && !out.detail.empty() && read_bytes(store.credential_path()) == cred,
          out.detail);

    // The directory comes back. The debts are settled, and then there is room.
    dir.revokeAnswer = SessionCall::Ok;
    check("[r5-3] when the directory answers, the sixteen are settled",
          login_flow::settle_owed_sign_outs(store, dir.deps()) == 0 && owed(store).empty() &&
              dir.revoked.size() >= 16);
    const auto again = login_flow::sign_out(store, dir.deps());
    Credential after;
    check("[r5-3] ...and the seventeenth sign-out goes through",
          again.localDone && again.serverTold && load(store, &after) == ReadResult::None);
    keep_log(dir);
  }
  {
    // The list cannot be read.
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Deps deps = dir.deps();
    const Credential c = credential("r5-3-unreadable");
    secret(c);
    seed(store, c);
    {
      Store::Lock lock = store.Acquire(1000);
      store.AddPendingRevoke(lock, PendingRevoke{kOrigin, "r5-3-older-device", "r5-3-older-7a2c"});
    }
    gSecrets.push_back("r5-3-older-7a2c");
    std::string journal = read_bytes(store.revoke_path());
    journal[journal.size() / 2] = static_cast<char>(journal[journal.size() / 2] ^ 0x41);
    write_bytes(store.revoke_path(), journal);
    const std::string cred = read_bytes(store.credential_path());

    std::vector<PendingRevoke> read;
    {
      Store::Lock lock = store.Acquire(1000);
      check("[r5-3] a list that cannot be read is unreadable, not empty",
            store.LoadPendingRevokes(lock, &read) == ReadResult::Unreadable && read.empty());
      std::string why;
      check("[r5-3] NOTHING IS WRITTEN OVER IT",
            !store.AddPendingRevoke(lock, PendingRevoke{kOrigin, "x", "y"}, &why) &&
                read_bytes(store.revoke_path()) == journal && !why.empty(),
            why);
      check("[r5-3] ...and nothing is removed from it",
            !store.RemovePendingRevoke(lock, "r5-3-older-device") &&
                read_bytes(store.revoke_path()) == journal);
    }
    const auto back = login_flow::come_back(store, deps);
    check("[r5-3] THE STORED CREDENTIAL IS NOT PRESENTED: it may be one that was signed out of",
          back.outcome == Return::Unreadable && dir.presented.empty() &&
              read_bytes(store.credential_path()) == cred,
          login_flow::return_name(back.outcome));
    check("[r5-3] settling sends nothing and says something is still owed",
          login_flow::settle_owed_sign_outs(store, deps) >= 1 && dir.revoked.empty());

    // The user signs in, on purpose. That is when the store stops refusing.
    const uint64_t g = login_flow::begin_sign_in(store, deps);
    const DeviceSignIn s = signed_in("r5-3-after");
    const Remembered r = login_flow::remember_sign_in(store, deps, g, "tester", s);
    Credential after;
    check("[r5-3] a sign-in made on purpose is stored",
          r == Remembered::Stored && load(store, &after) == ReadResult::Ok &&
              after.deviceId == s.deviceId, login_flow::remembered_name(r));
    check("[r5-3] ...the unreadable list is kept, whole, under another name",
          read_bytes(store.revoke_path() + L".unreadable-1") == journal);
    const std::vector<PendingRevoke> fresh = owed(store);
    check("[r5-3] ...and the credential it replaced is owed a sign-out in a new one",
          fresh.size() == 1 && fresh[0].deviceId == c.deviceId);
    DeleteFileW((store.revoke_path() + L".unreadable-1").c_str());
    keep_log(dir);
  }
}

void test_r5_4_a_server_without_the_route() {
  const Store store(fresh_dir());
  FakeDirectory dir;
  const Credential c = credential("r5-4");
  secret(c);
  seed(store, c);
  // A server from before device credentials, put back during a rollback: 404.
  dir.revokeAnswer = SessionCall::Unsupported;
  const auto out = login_flow::sign_out(store, dir.deps());
  Credential after;
  std::vector<PendingRevoke> list = owed(store);
  check("[r5-4] signed out against a server with no logout route: the credential goes",
        out.localDone && !out.serverTold && load(store, &after) == ReadResult::None);
  check("[r5-4] A 404 IS NOT A DEVICE ENDED: the sign-out is still owed",
        out.owed && list.size() == 1 && list[0].deviceId == c.deviceId &&
            list[0].revokeToken == c.revokeToken);
  check("[r5-4] ...and stays owed for as long as the server has no such route",
        login_flow::settle_owed_sign_outs(store, dir.deps()) == 1 && owed(store).size() == 1);
  // The newer server is back.
  dir.revokeAnswer = SessionCall::Ok;
  dir.revoked.clear();
  check("[r5-4] when a server that has the route is back, it is told, and the debt is settled",
        login_flow::settle_owed_sign_outs(store, dir.deps()) == 0 && owed(store).empty() &&
            dir.revoked.size() == 1 && dir.revoked[0] == c.deviceId &&
            dir.revokeTokens.back() == c.revokeToken);

  // The same for a sign-in that is not kept.
  const Store other(fresh_dir());
  FakeDirectory d2;
  const Deps deps2 = d2.deps();
  const uint64_t g = login_flow::begin_sign_in(other, deps2);
  (void)login_flow::sign_out(other, deps2);
  d2.revokeAnswer = SessionCall::Unsupported;
  const DeviceSignIn late = signed_in("r5-4-late");
  (void)login_flow::remember_sign_in(other, deps2, g, "tester", late);
  list = owed(other);
  check("[r5-4] a device that is not kept and meets a 404 is owed a sign-out too",
        list.size() == 1 && list[0].deviceId == late.deviceId);

  // 401 is different: the directory does not know the token. There is nothing more to send.
  const Store third(fresh_dir());
  FakeDirectory d3;
  const Credential c3 = credential("r5-4-401");
  secret(c3);
  seed(third, c3);
  d3.revokeAnswer = SessionCall::Rejected;
  const auto out3 = login_flow::sign_out(third, d3.deps());
  check("[r5-4] a 401 still means there is nothing more to send",
        out3.localDone && !out3.owed && owed(third).empty());
  keep_log(dir);
  keep_log(d2);
  keep_log(d3);
}

void test_r5_5_no_sign_in_without_the_store() {
  {
    // Account A is stored. Somebody signs in as B while the store cannot be had.
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Credential a = credential("r5-5-account-a");
    secret(a);
    seed(store, a);
    Store::Lock held = store.Acquire(1000);
    check("[r5-5] with the store held by somebody else a sign-in does not begin",
          login_flow::begin_sign_in(store, dir.deps(300)) == 0);
    // ...and an answer brought to it anyway is not used.
    DeviceSignIn plain;
    plain.sessionToken = "r5-5-plain-session-44d0";
    gSecrets.push_back(plain.sessionToken);
    check("[r5-5] ...and an answer with no generation is not a sign-in",
          login_flow::remember_sign_in(store, dir.deps(300), 0, "other", plain) ==
              Remembered::NotSaved);
    keep_log(dir);
  }
  {
    // A directory that issues no device (one from before them). A is stored; B signs in.
    const Store store(fresh_dir());
    FakeDirectory dir;
    const Deps deps = dir.deps();
    const Credential a = credential("r5-5-a");
    secret(a);
    seed(store, a);
    const uint64_t g = login_flow::begin_sign_in(store, deps);
    DeviceSignIn plain;
    plain.sessionToken = "r5-5-b-session-90c3";
    gSecrets.push_back(plain.sessionToken);
    const Remembered r = login_flow::remember_sign_in(store, deps, g, "account-b", plain);
    Credential after;
    const std::vector<PendingRevoke> list = owed(store);
    check("[r5-5] signing in as B with nothing issued: the sign-in stands",
          r == Remembered::NotIssued, login_flow::remembered_name(r));
    check("[r5-5] A'S STORED SIGN-IN IS GONE: the next start does not come back as A",
          load(store, &after) == ReadResult::None &&
              login_flow::come_back(store, deps).outcome == Return::NoCredential &&
              dir.presented.empty());
    check("[r5-5] ...and A's device is owed a sign-out",
          list.size() == 1 && list[0].deviceId == a.deviceId);
    keep_log(dir);
  }
  {
    // The same kind of answer, arriving after another window has signed out.
    const Store store(fresh_dir());
    FakeDirectory mine, theirs;
    const uint64_t g = login_flow::begin_sign_in(store, mine.deps());
    (void)login_flow::sign_out(Store(store.directory()), theirs.deps());
    DeviceSignIn plain;
    plain.sessionToken = "r5-5-late-session-2ab7";
    gSecrets.push_back(plain.sessionToken);
    check("[r5-5] AN ANSWER WITH NOTHING ISSUED IS LATE LIKE ANY OTHER: it is not used",
          login_flow::remember_sign_in(store, mine.deps(), g, "tester", plain) ==
              Remembered::Superseded);
    keep_log(mine);
    keep_log(theirs);
  }
}

void test_r5_6_another_account() {
  const Store store(fresh_dir());
  FakeDirectory dir;
  const Deps deps = dir.deps();
  // This window signed in as account-a. Another window has since signed in as account-b, and
  // what is stored is account-b's.
  Credential b = credential("r5-6-b");
  b.accountId = "account-b";
  secret(b);
  seed(store, b);
  const std::string bytes = read_bytes(store.credential_path());
  const auto back = login_flow::come_back(store, deps, "account-a");
  check("[r5-6] ANOTHER ACCOUNT'S STORED SIGN-IN IS NOT PRESENTED FOR THIS WINDOW",
        back.outcome == Return::OtherAccount && dir.presented.empty() &&
            back.sessionToken.empty(),
        login_flow::return_name(back.outcome));
  check("[r5-6] ...and is left as it is, for the window it belongs to",
        read_bytes(store.credential_path()) == bytes);
  const auto theirs = login_flow::come_back(store, deps, "account-b");
  gSecrets.push_back(theirs.sessionToken);
  check("[r5-6] the window of that account does come back, as that account",
        theirs.outcome == Return::SignedIn && theirs.accountId == "account-b");
  const auto fresh = login_flow::come_back(store, deps);
  gSecrets.push_back(fresh.sessionToken);
  check("[r5-6] a window with nobody signed in yet comes back as whoever is stored, and says who",
        fresh.outcome == Return::SignedIn && fresh.accountId == "account-b");
  Credential after;
  load(store, &after);
  gSecrets.push_back(after.deviceCredential);
  gSecrets.push_back("rotated-1-91aa04f6e2");
  keep_log(dir);
}

// ------------------------------------------------------------------------------ r6
//
// Two more from the review of e7e5037: an answer to a sign-out that arrives after another window
// has signed in, and a list that cannot be read with nothing stored beside it.

void test_r6_1_a_late_sign_out_answer() {
  // Window 1 signs out of A. The sign-out cannot be written down, so the directory is told
  // first -- with the lock let go. While that call is out, window 2 signs in as B and stores it.
  const Store store(fresh_dir());
  FakeDirectory dir;
  Deps deps = dir.deps();
  const Credential a = credential("r6-1-a");
  const Credential b = credential("r6-1-b");
  secret(a);
  secret(b);
  seed(store, a);
  bool storedB = false;
  auto tell = deps.revoke;
  deps.revoke = [&](const std::string& deviceId, const std::string& token, std::string* error) {
    // Window 2, complete, while window 1 waits for the directory.
    Store::Lock lock = store.Acquire(1000);
    storedB = lock.held() && store.Save(lock, b);
    lock = Store::Lock();
    return tell(deviceId, token, error);
  };
  std::string bBytes;
  {
    BrokenJournalWrite broken(store);
    const auto out = login_flow::sign_out(store, deps);
    bBytes = read_bytes(store.credential_path());
    Credential now;
    check("[r6-1] the other window's sign-in was stored while the sign-out waited", storedB);
    check("[r6-1] A LATE ANSWER TO THE SIGN-OUT OF A DOES NOT ERASE B",
          load(store, &now) == ReadResult::Ok && now.deviceId == b.deviceId &&
              now.revokeToken == b.revokeToken,
          now.deviceId);
    check("[r6-1] ...and A's sign-out is done: the directory ended it, and A is not on disk",
          out.localDone && out.serverTold && dir.revoked.size() == 1 &&
              dir.revoked[0] == a.deviceId);
  }
  // Without the other window, the same answer still erases A (the case r5-2 covers).
  {
    const Store alone(fresh_dir());
    FakeDirectory d2;
    const Credential c = credential("r6-1-alone");
    secret(c);
    seed(alone, c);
    BrokenJournalWrite broken(alone);
    const auto out = login_flow::sign_out(alone, d2.deps());
    Credential after;
    check("[r6-1] with nobody in between, A's credential is erased as before",
          out.localDone && load(alone, &after) == ReadResult::None);
    keep_log(d2);
  }
  keep_log(dir);
}

void test_r6_3_an_unreadable_list_with_nothing_stored() {
  // Signed out (nothing stored), and the list of owed sign-outs cannot be read.
  const Store store(fresh_dir());
  FakeDirectory dir;
  const Deps deps = dir.deps();
  {
    Store::Lock lock = store.Acquire(1000);
    store.AddPendingRevoke(lock, PendingRevoke{kOrigin, "r6-3-older-device", "r6-3-older-51d0"});
  }
  gSecrets.push_back("r6-3-older-51d0");
  std::string journal = read_bytes(store.revoke_path());
  journal[journal.size() / 2] = static_cast<char>(journal[journal.size() / 2] ^ 0x41);
  write_bytes(store.revoke_path(), journal);
  Credential none;
  check("[r6-3] nothing is stored, and the list cannot be read",
        load(store, &none) == ReadResult::None);

  // One sign-in, made on purpose.
  const uint64_t g = login_flow::begin_sign_in(store, deps);
  const DeviceSignIn s = signed_in("r6-3-new");
  const Remembered r = login_flow::remember_sign_in(store, deps, g, "tester", s);
  check("[r6-3] the sign-in is stored", r == Remembered::Stored, login_flow::remembered_name(r));
  std::vector<PendingRevoke> list;
  ReadResult listRead;
  {
    Store::Lock lock = store.Acquire(1000);
    listRead = store.LoadPendingRevokes(lock, &list);
  }
  check("[r6-3] THE UNREADABLE LIST IS SET ASIDE BY IT, though there was nothing stored beside it",
        listRead != ReadResult::Unreadable &&
            read_bytes(store.revoke_path() + L".unreadable-1") == journal);

  // The next start.
  const auto back = login_flow::come_back(store, deps);
  check("[r6-3] ...so the next start comes back signed in",
        back.outcome == Return::SignedIn && !back.sessionToken.empty(),
        login_flow::return_name(back.outcome));
  keep_log(dir);
}

void test_nothing_secret_is_logged() {
  size_t lines = 0, leaks = 0;
  std::string first;
  for (const std::string& line : gLogLines) {
    ++lines;
    for (const std::string& s : gSecrets) {
      if (!s.empty() && line.find(s) != std::string::npos) {
        if (++leaks == 1) first = line.substr(0, 80);
      }
    }
  }
  check("[log] the cases above logged something to search", lines >= 20 && gSecrets.size() >= 30,
        std::to_string(lines) + " lines, " + std::to_string(gSecrets.size()) + " secrets");
  check("[log] NO CREDENTIAL, REVOKE TOKEN OR SESSION IS IN ANY LINE", leaks == 0, first);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc == 4 && std::wstring(argv[1]) == L"--hold-store") {
    return hold_store_mode(argv[2], static_cast<DWORD>(_wtoi(argv[3])));
  }
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  const std::wstring self = self_path();
  gRoot = self.substr(0, self.find_last_of(L"\\/")) + L"\\login-flow-test-" +
          std::to_wstring(GetCurrentProcessId());
  if (!CreateDirectoryW(gRoot.c_str(), nullptr)) {
    std::printf("FAIL  could not create the scratch directory\n");
    return 2;
  }

  test_store_files();
  test_store_generation_and_owed();
  test_store_lock();
  test_come_back();
  test_two_at_once();
  test_sign_in();
  test_late_answers();
  test_sign_out();
  test_r5_1_counter_cannot_be_written();
  test_r5_2_sign_out_cannot_be_written_down();
  test_r5_3_the_list_of_owed_sign_outs();
  test_r5_4_a_server_without_the_route();
  test_r5_5_no_sign_in_without_the_store();
  test_r5_6_another_account();
  test_r6_1_a_late_sign_out_answer();
  test_r6_3_an_unreadable_list_with_nothing_stored();
  test_nothing_secret_is_logged();

  remove_case_dirs();
  check("[scratch] what this run wrote is removed", !exists(gRoot));

  if (gFailures == 0) {
    std::printf("login_flow_test: ALL PASS (%d checks)\n", gChecks);
    return 0;
  }
  std::printf("login_flow_test: FAIL (%d of %d failed)\n", gFailures, gChecks);
  return 1;
}

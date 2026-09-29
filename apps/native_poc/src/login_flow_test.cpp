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
                                L"login.cred.gen.tmp", L"login.cred.lock"}) {
      DeleteFileW((dir + L"\\" + name).c_str());
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
  check("[store] sign-outs owed are a bounded list, the oldest dropped first",
        list.size() == login_store::kMaxPendingRevokes && list.front().deviceId == "owed-device-4" &&
            list.back().deviceId == "owed-device-19",
        std::to_string(list.size()));
  const std::string bytes = read_bytes(store.revoke_path());
  check("[store] the list is protected like the credential",
        !bytes.empty() && bytes.find("owed-revoke-token") == std::string::npos &&
            bytes.find("owed-device") == std::string::npos);
  {
    Store::Lock lock = store.Acquire(1000);
    store.AddPendingRevoke(lock, PendingRevoke{kOrigin, "owed-device-10", "replaced-token-77e0"});
    store.RemovePendingRevoke(lock, "owed-device-19");
  }
  list = owed(store);
  bool replaced = false, removed = true;
  int tens = 0;
  for (const PendingRevoke& p : list) {
    if (p.deviceId == "owed-device-10") { ++tens; replaced = p.revokeToken == "replaced-token-77e0"; }
    if (p.deviceId == "owed-device-19") removed = false;
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

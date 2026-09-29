#pragma once

// Where the client keeps what lets it come back signed in.
//
// Three files beside client.txt, and a lock:
//
//   login.cred          the device credential the directory issued at sign-in: which server,
//                       which account, the device id, the credential, the revoke token.
//                       Protected with DPAPI for this Windows user.
//   login.cred.revoke   sign-outs the server has not been told about yet: a device id and its
//                       revoke token each. Protected the same way. A bounded list, because a
//                       sign-out made offline must not be forgotten when the next one is.
//   login.cred.gen      a counter. It moves on every sign-in attempt, every sign-out and every
//                       time the credential is erased, and it is what a late answer is checked
//                       against -- in this process or another.
//   login.cred.lock     held, exclusively, for the whole of every operation on the above.
//
// The password is not among them. What is stored opens this account from this device until it
// is revoked, and it can be revoked for this device alone.
//
// WHAT THE PROTECTION IS AND IS NOT. DPAPI ties the file to the Windows user: another user of
// the machine, or someone with a copy of the disk, cannot read it. A program running AS this
// user can -- that is the same program that could read the session out of this process. It is
// not a defence against that and nothing here claims it is.
//
// WHY A FILE LOCK. Two GNLinkClient windows, or one started the moment another is closing,
// share these files. A refresh rotates the credential, so the slower of two would present the
// one the faster had already replaced -- and the directory ends a device whose replaced
// credential comes back. The files live in the user's profile, which is shared by every logon
// session of that user, so a mutex named in one session would not cover them. The lock is a
// file beside them, opened with no sharing: whoever reaches the files reaches the lock.
//
// Nothing here talks to a server. login_flow.hpp is what a sign-in, a refresh and a sign-out do
// with this.

#include <cstdint>
#include <string>
#include <vector>

namespace remote60::native_poc::login_store {

struct Credential {
  std::string serverOrigin;   // directory_origin_key() of the server that issued it
  std::string accountId;
  std::string deviceId;
  std::string deviceCredential;
  std::string revokeToken;
};

struct PendingRevoke {
  std::string serverOrigin;
  std::string deviceId;
  std::string revokeToken;
};

/** How many sign-outs can be waiting to be told to the server. Past this, none is added. */
constexpr size_t kMaxPendingRevokes = 16;

enum class ReadResult {
  None,        // no file
  Ok,
  Unreadable,  // there is a file and it cannot be used: damaged, or protected for someone else
};

class Store {
 public:
  /** `directory` is where client.txt lives. It is created if it is not there. */
  explicit Store(std::wstring directory);

  /**
   * The exclusive hold every operation needs. Released when it goes out of scope.
   *
   * Waiting is bounded: a caller that cannot have it within `waitMs` is told so and carries on
   * without the store, rather than hanging behind another process's network call.
   */
  class Lock {
   public:
    Lock() = default;
    Lock(Lock&& other) noexcept;
    Lock& operator=(Lock&& other) noexcept;
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    ~Lock();
    bool held() const { return handle_ != nullptr; }

   private:
    friend class Store;
    void* handle_ = nullptr;
  };
  Lock Acquire(uint32_t waitMs) const;

  // Everything below takes the lock it is done under. A lock that is not held does nothing and
  // reports failure -- there is no unlocked path to these files.

  ReadResult Load(const Lock& lock, Credential* out, std::string* why = nullptr) const;
  /** Written beside the file and moved over it, so a reader sees the old one or the new one. */
  bool Save(const Lock& lock, const Credential& credential, std::string* why = nullptr) const;
  /** True when there is no credential afterwards, whether or not there was one before. */
  bool Erase(const Lock& lock) const;

  uint64_t Generation(const Lock& lock) const;
  /** Moves the counter and returns the new value. 0 means it could not be written. */
  uint64_t BumpGeneration(const Lock& lock) const;

  ReadResult LoadPendingRevokes(const Lock& lock, std::vector<PendingRevoke>* out) const;
  /**
   * Adds one, replacing an entry for the same device.
   *
   * Refused -- nothing written, `why` says which -- when the list is full and when it cannot
   * be read. Neither is resolved by dropping or overwriting what is there: every entry is a
   * device still alive at the directory and the only copy of the token that ends it.
   */
  bool AddPendingRevoke(const Lock& lock, const PendingRevoke& pending,
                        std::string* why = nullptr) const;
  bool RemovePendingRevoke(const Lock& lock, const std::string& deviceId) const;
  /**
   * Moves a list that cannot be read out of the way, whole, to `login.cred.revoke.unreadable-N`,
   * so that a new list can be started. Only when it cannot be read, and only by a caller with
   * the user in front of it (a sign-in made on purpose): it is how the store stops refusing.
   */
  bool SetAsidePendingRevokes(const Lock& lock, std::string* movedTo = nullptr) const;

  const std::wstring& directory() const { return directory_; }
  std::wstring credential_path() const;
  std::wstring revoke_path() const;
  std::wstring generation_path() const;
  std::wstring lock_path() const;

 private:
  std::wstring directory_;
};

}  // namespace remote60::native_poc::login_store

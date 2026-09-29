#include "login_flow.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <vector>

namespace remote60::native_poc::login_flow {
namespace {

using login_store::Credential;
using login_store::PendingRevoke;
using login_store::ReadResult;
using login_store::Store;

void say(const Deps& deps, const std::string& line) {
  if (deps.log) deps.log("sign-in store: " + line);
}

/** How a device is named in a log: enough to tell two apart, not enough to use. */
std::string tag(const std::string& deviceId) {
  return deviceId.empty() ? std::string("-") : deviceId.substr(0, 8);
}

void wipe(std::string* text) {
  if (!text->empty()) SecureZeroMemory(text->data(), text->size());
  text->clear();
}

void wipe(Credential* credential) {
  wipe(&credential->deviceCredential);
  wipe(&credential->revokeToken);
}

bool owed_for(const std::vector<PendingRevoke>& owed, const std::string& deviceId) {
  return std::any_of(owed.begin(), owed.end(),
                     [&](const PendingRevoke& p) { return p.deviceId == deviceId; });
}

/**
 * Ends a device the directory issued and this client will not keep. Told now if it can be,
 * written down as owed if it cannot. `lock` may be held or not; the record needs it held.
 */
void end_unkept_device(const Store& store, const Deps& deps, const DeviceSignIn& signedIn,
                       const char* why) {
  std::string error;
  const SessionCall told = deps.revoke ? deps.revoke(signedIn.deviceId, signedIn.revokeToken, &error)
                                       : SessionCall::Unreachable;
  if (told == SessionCall::Ok || told == SessionCall::Rejected ||
      told == SessionCall::Unsupported) {
    // Rejected: it is already ended, or the token is not this device's -- nothing more this
    // client can do either way. Unsupported: that directory issues no devices to end.
    say(deps, std::string("device ") + tag(signedIn.deviceId) + " was not kept (" + why +
                  ") and the directory was told: " + session_call_name(told));
    return;
  }
  Store::Lock lock = store.Acquire(deps.lockWaitMs);
  const bool recorded =
      lock.held() &&
      store.AddPendingRevoke(lock, PendingRevoke{deps.serverOrigin, signedIn.deviceId,
                                                 signedIn.revokeToken});
  say(deps, std::string("device ") + tag(signedIn.deviceId) + " was not kept (" + why +
                "); the directory could not be told (" + session_call_name(told) + ") and that is " +
                (recorded ? "recorded as owed" : "NOT recorded: the store could not be written"));
}

}  // namespace

const char* return_name(Return value) {
  switch (value) {
    case Return::SignedIn: return "signed-in";
    case Return::NoCredential: return "no-credential";
    case Return::SignedOut: return "signed-out";
    case Return::Rejected: return "rejected";
    case Return::Unreadable: return "unreadable";
    case Return::ServerCannot: return "server-cannot";
    case Return::TryLater: return "try-later";
    case Return::Busy: return "busy";
    case Return::NotSaved: return "not-saved";
  }
  return "?";
}

const char* remembered_name(Remembered value) {
  switch (value) {
    case Remembered::Stored: return "stored";
    case Remembered::NotIssued: return "not-issued";
    case Remembered::Superseded: return "superseded";
    case Remembered::NotSaved: return "not-saved";
  }
  return "?";
}

// ------------------------------------------------------------------------------ coming back

ReturnResult come_back(const Store& store, const Deps& deps) {
  ReturnResult result;
  Store::Lock lock = store.Acquire(deps.lockWaitMs);
  if (!lock.held()) {
    result.outcome = Return::Busy;
    result.detail = "the sign-in store is in use by another GNLink window";
    say(deps, "could not be had in time; nothing was sent");
    return result;
  }

  // Read under the lock, every time. What another process stored a moment ago is what is here.
  Credential stored;
  std::string why;
  const ReadResult read = store.Load(lock, &stored, &why);
  if (read == ReadResult::None) {
    result.outcome = Return::NoCredential;
    return result;
  }
  if (read == ReadResult::Unreadable) {
    // Left where it is. Erasing it would destroy the only evidence of what went wrong, and
    // the next sign-in that succeeds writes over it.
    result.outcome = Return::Unreadable;
    result.detail = why;
    say(deps, "the stored credential cannot be used: " + why);
    return result;
  }
  result.accountId = stored.accountId;
  result.deviceId = stored.deviceId;

  if (stored.serverOrigin != deps.serverOrigin) {
    // Issued by another server. Not presented to this one and not erased.
    wipe(&stored);
    result.outcome = Return::NoCredential;
    result.detail = "the stored credential belongs to another server";
    say(deps, "the stored credential was issued by another server; it is kept and not used");
    return result;
  }

  std::vector<PendingRevoke> owed;
  (void)store.LoadPendingRevokes(lock, &owed);
  if (owed_for(owed, stored.deviceId)) {
    // A sign-out got as far as being written down and no further. It is finished here rather
    // than undone: the credential goes, and the directory is told by settle_owed_sign_outs.
    (void)store.BumpGeneration(lock);
    const bool erased = store.Erase(lock);
    wipe(&stored);
    result.outcome = Return::SignedOut;
    result.detail = "this device was signed out";
    say(deps, "device " + tag(result.deviceId) + " has a sign-out owed; its credential is " +
                  (erased ? "erased" : "STILL ON DISK (could not be erased)") +
                  " and it is not used");
    return result;
  }

  DeviceSignIn refreshed;
  std::string error;
  const SessionCall call = deps.refresh(stored.deviceId, stored.deviceCredential, &refreshed,
                                        &error);
  switch (call) {
    case SessionCall::Ok: {
      Credential next = stored;
      next.deviceCredential = refreshed.deviceCredential;
      std::string saveWhy;
      if (!store.Save(lock, next, &saveWhy)) {
        // The directory has replaced the credential and the replacement is not on disk. The
        // one still there is good once more, for a minute; after that this device signs in
        // again. The session is not used: a device that cannot keep its credential is not
        // left signed in on the strength of one it will lose.
        wipe(&next);
        wipe(&stored);
        wipe(&refreshed.deviceCredential);
        wipe(&refreshed.sessionToken);
        result.outcome = Return::NotSaved;
        result.detail = "the new credential could not be stored: " + saveWhy;
        say(deps, "device " + tag(result.deviceId) +
                      ": refreshed, but the new credential could not be stored (" + saveWhy + ")");
        return result;
      }
      wipe(&next);
      wipe(&stored);
      wipe(&refreshed.deviceCredential);
      result.outcome = Return::SignedIn;
      result.sessionToken = std::move(refreshed.sessionToken);
      say(deps, "device " + tag(result.deviceId) + ": signed in without a password");
      return result;
    }
    case SessionCall::Rejected: {
      (void)store.BumpGeneration(lock);
      const bool erased = store.Erase(lock);
      wipe(&stored);
      result.outcome = Return::Rejected;
      result.detail = error;
      say(deps, "device " + tag(result.deviceId) + ": the directory refused the credential; it is " +
                    (erased ? "erased" : "STILL ON DISK (could not be erased)"));
      return result;
    }
    case SessionCall::Unsupported:
      wipe(&stored);
      result.outcome = Return::ServerCannot;
      result.detail = "this server does not issue device credentials";
      say(deps, "the directory has no refresh route; the credential is kept");
      return result;
    case SessionCall::Limited:
    case SessionCall::Failed:
    case SessionCall::Unreachable:
      wipe(&stored);
      result.outcome = Return::TryLater;
      result.detail = error;
      say(deps, std::string("device ") + tag(result.deviceId) + ": no answer about the credential (" +
                    session_call_name(call) + "); it is kept");
      return result;
  }
  return result;
}

// ------------------------------------------------------------------------------ signing in

uint64_t begin_sign_in(const Store& store, const Deps& deps) {
  Store::Lock lock = store.Acquire(deps.lockWaitMs);
  if (!lock.held()) {
    say(deps, "could not be had in time; this sign-in will not be remembered");
    return 0;
  }
  const uint64_t generation = store.BumpGeneration(lock);
  if (generation == 0) say(deps, "the counter could not be written; this sign-in will not be remembered");
  return generation;
}

Remembered remember_sign_in(const Store& store, const Deps& deps, uint64_t generation,
                            const std::string& accountId, const DeviceSignIn& signedIn) {
  if (signedIn.deviceId.empty() || signedIn.deviceCredential.empty() ||
      signedIn.revokeToken.empty()) {
    return Remembered::NotIssued;
  }

  Remembered outcome = Remembered::Superseded;
  const char* why = "something happened to the store after this sign-in began";
  std::string previousDevice;
  {
    Store::Lock lock = store.Acquire(deps.lockWaitMs);
    if (!lock.held() || generation == 0) {
      outcome = Remembered::NotSaved;
      why = "the store could not be had";
    } else if (store.Generation(lock) != generation) {
      outcome = Remembered::Superseded;
    } else {
      // The family this one replaces, if there was one: a sign-in made on purpose starts a
      // new family, and the old one is ended rather than left to expire in ninety days.
      Credential previous;
      if (store.Load(lock, &previous) == ReadResult::Ok &&
          previous.serverOrigin == deps.serverOrigin &&
          previous.deviceId != signedIn.deviceId) {
        previousDevice = previous.deviceId;
        (void)store.AddPendingRevoke(
            lock, PendingRevoke{previous.serverOrigin, previous.deviceId, previous.revokeToken});
      }
      wipe(&previous);

      Credential fresh{deps.serverOrigin, accountId, signedIn.deviceId,
                       signedIn.deviceCredential, signedIn.revokeToken};
      std::string saveWhy;
      if (store.Save(lock, fresh, &saveWhy)) {
        outcome = Remembered::Stored;
      } else {
        outcome = Remembered::NotSaved;
        why = "the credential could not be written";
        say(deps, "device " + tag(signedIn.deviceId) + ": could not be stored (" + saveWhy + ")");
      }
      wipe(&fresh);
    }
  }

  if (outcome == Remembered::Stored) {
    say(deps, "device " + tag(signedIn.deviceId) + ": stored" +
                  (previousDevice.empty() ? std::string()
                                          : "; device " + tag(previousDevice) +
                                                " before it is to be ended"));
    return outcome;
  }
  end_unkept_device(store, deps, signedIn, why);
  return outcome;
}

// ------------------------------------------------------------------------------ signing out

SignOutResult sign_out(const Store& store, const Deps& deps) {
  SignOutResult result;
  Credential stored;
  bool haveCredential = false;
  {
    Store::Lock lock = store.Acquire(deps.lockWaitMs);
    if (!lock.held()) {
      result.detail = "the sign-in store is in use by another GNLink window";
      say(deps, "could not be had in time; the credential was not erased");
      return result;
    }
    // The counter first: from here every answer still in flight is late.
    (void)store.BumpGeneration(lock);
    const ReadResult read = store.Load(lock, &stored);
    haveCredential = read == ReadResult::Ok && stored.serverOrigin == deps.serverOrigin;
    if (haveCredential) {
      // Written down BEFORE the credential goes. If this process dies between the two lines,
      // the credential is still there -- with a record beside it that says it is not to be
      // used, which come_back() reads first.
      std::string why;
      result.owed = store.AddPendingRevoke(
          lock, PendingRevoke{stored.serverOrigin, stored.deviceId, stored.revokeToken}, &why);
      if (!result.owed) {
        say(deps, "device " + tag(stored.deviceId) +
                      ": the sign-out could not be written down (" + why + ")");
      }
    }
    if (read == ReadResult::Ok && !haveCredential) {
      // Another server's. Signing out of this one does not touch it.
      result.localDone = true;
      result.detail = "nothing of this server's was stored";
      wipe(&stored);
      return result;
    }
    result.localDone = store.Erase(lock);
    if (!result.localDone) {
      say(deps, "the credential could not be erased");
      result.detail = "the stored sign-in could not be removed";
    }
  }
  if (!haveCredential) {
    result.detail = "nothing was stored";
    return result;
  }

  std::string error;
  const SessionCall told = deps.revoke(stored.deviceId, stored.revokeToken, &error);
  const std::string device = stored.deviceId;
  wipe(&stored);
  // Rejected: the directory does not accept the revoke token, which is what it answers for a
  // device it no longer has as well as for a wrong token. There is nothing left to send.
  // Unsupported: a directory that issues no devices has none to end.
  if (told == SessionCall::Ok || told == SessionCall::Rejected ||
      told == SessionCall::Unsupported) {
    Store::Lock lock = store.Acquire(deps.lockWaitMs);
    const bool cleared = lock.held() && store.RemovePendingRevoke(lock, device);
    result.serverTold = told == SessionCall::Ok;
    result.owed = !cleared;
    say(deps, "device " + tag(device) + ": signed out; the directory answered " +
                  session_call_name(told));
    return result;
  }
  result.serverTold = false;
  result.detail = "the server could not be told yet";
  say(deps, std::string("device ") + tag(device) + ": signed out here; the directory could not be told (" +
                session_call_name(told) + ") and that is " +
                (result.owed ? "recorded as owed" : "NOT recorded"));
  return result;
}

size_t settle_owed_sign_outs(const Store& store, const Deps& deps) {
  std::vector<PendingRevoke> owed;
  {
    Store::Lock lock = store.Acquire(deps.lockWaitMs);
    if (!lock.held()) return 0;
    if (store.LoadPendingRevokes(lock, &owed) != ReadResult::Ok) return 0;
  }
  size_t remaining = 0;
  for (PendingRevoke& pending : owed) {
    if (pending.serverOrigin != deps.serverOrigin) {
      // Owed to another server. Not sent to this one; kept for when that one is the server.
      ++remaining;
      continue;
    }
    std::string error;
    const SessionCall told = deps.revoke(pending.deviceId, pending.revokeToken, &error);
    if (told == SessionCall::Ok || told == SessionCall::Rejected ||
        told == SessionCall::Unsupported) {
      Store::Lock lock = store.Acquire(deps.lockWaitMs);
      if (!lock.held() || !store.RemovePendingRevoke(lock, pending.deviceId)) ++remaining;
      say(deps, "device " + tag(pending.deviceId) + ": an owed sign-out was settled (" +
                    session_call_name(told) + ")");
    } else {
      ++remaining;
      say(deps, std::string("device ") + tag(pending.deviceId) + ": a sign-out is still owed (" +
                    session_call_name(told) + ")");
    }
    wipe(&pending.revokeToken);
  }
  return remaining;
}

}  // namespace remote60::native_poc::login_flow

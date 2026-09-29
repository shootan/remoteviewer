#include "login_flow.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <vector>

// One rule runs through this file: WHEN SOMETHING COULD NOT BE WRITTEN, THE STEP AFTER IT IS NOT
// TAKEN. The counter, the record of a sign-out owed and the credential are each what makes the
// next step safe, and each write reports whether it happened. None of those reports is dropped.

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
 * Whether the directory's answer means there is nothing left to send for this device.
 *
 * Ok: it ended the device. Rejected (401): it does not accept the revoke token, which is what
 * it answers for a device it no longer has.
 *
 * NOT Unsupported. A 404 from the logout route says the server in front of us has no such
 * route -- a server from before device credentials, put back during a rollback. The device is
 * still in its store, untouched, and alive again the moment the newer server returns. That is
 * not an ended device; it is one that has not been told yet.
 */
bool ended_at_directory(SessionCall call) {
  return call == SessionCall::Ok || call == SessionCall::Rejected;
}

SessionCall tell_directory(const Deps& deps, const std::string& deviceId,
                           const std::string& revokeToken) {
  std::string error;
  return deps.revoke ? deps.revoke(deviceId, revokeToken, &error) : SessionCall::Unreachable;
}

/**
 * Ends a device the directory issued and this client will not keep. Told now if it can be,
 * written down as owed if it cannot -- and if neither, that is said.
 */
void end_unkept_device(const Store& store, const Deps& deps, const DeviceSignIn& signedIn,
                       const std::string& why) {
  const SessionCall told = tell_directory(deps, signedIn.deviceId, signedIn.revokeToken);
  if (ended_at_directory(told)) {
    say(deps, "device " + tag(signedIn.deviceId) + " was not kept (" + why +
                  ") and the directory was told: " + session_call_name(told));
    return;
  }
  Store::Lock lock = store.Acquire(deps.lockWaitMs);
  std::string recordWhy = "the store could not be had";
  const bool recorded =
      lock.held() &&
      store.AddPendingRevoke(lock, PendingRevoke{deps.serverOrigin, signedIn.deviceId,
                                                 signedIn.revokeToken}, &recordWhy);
  say(deps, "device " + tag(signedIn.deviceId) + " was not kept (" + why +
                "); the directory could not be told (" + session_call_name(told) + ") and that is " +
                (recorded ? std::string("recorded as owed")
                          : "NOT recorded (" + recordWhy +
                                "): the device stays alive at the directory until it expires"));
}

/**
 * Takes a stored credential out of use, under the lock: written down as owed, then erased.
 *
 * In that order, and the second only if the first succeeded. What is returned is whether the
 * credential is gone; when the record could not be written, it is exactly as it was found.
 */
bool retire_under_lock(const Store& store, const Store::Lock& lock, const Credential& stored,
                       bool* recorded, std::string* why) {
  *recorded = store.AddPendingRevoke(
      lock, PendingRevoke{stored.serverOrigin, stored.deviceId, stored.revokeToken}, why);
  if (!*recorded) return false;
  if (!store.Erase(lock)) {
    // The record is there and says this credential is not to be used; come_back reads it
    // first. But it is not gone, and that is what is reported.
    if (why) *why = "the credential could not be erased";
    return false;
  }
  return true;
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
    case Return::OtherAccount: return "other-account";
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

ReturnResult come_back(const Store& store, const Deps& deps, const std::string& onlyForAccount) {
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

  if (!onlyForAccount.empty() && stored.accountId != onlyForAccount) {
    // Somebody signed in as another account, in another window, since this one did. What is
    // stored is theirs. It is not presented on this window's behalf: the session it would get
    // is the other account's, and this window would show that account's PCs under the name
    // of its own.
    wipe(&stored);
    result.outcome = Return::OtherAccount;
    result.detail = "another account is signed in on this device";
    say(deps, "the stored sign-in belongs to another account than this window's; it is not used");
    return result;
  }

  std::vector<PendingRevoke> owed;
  if (store.LoadPendingRevokes(lock, &owed) == ReadResult::Unreadable) {
    // Whether this credential was signed out of is written in a list that cannot be read. It
    // may have been. Nothing is presented on the strength of not knowing.
    wipe(&stored);
    result.outcome = Return::Unreadable;
    result.detail = "the list of sign-outs cannot be read";
    say(deps, "the list of owed sign-outs cannot be read; the stored credential is not used");
    return result;
  }
  if (owed_for(owed, stored.deviceId)) {
    // A sign-out got as far as being written down and no further. It is finished here rather
    // than undone -- but only behind the counter: without that on disk, an answer to a
    // sign-in from before the sign-out could still store itself.
    const bool barrier = store.BumpGeneration(lock) != 0;
    const bool erased = barrier && store.Erase(lock);
    wipe(&stored);
    result.outcome = Return::SignedOut;
    result.detail = "this device was signed out";
    say(deps, "device " + tag(result.deviceId) + " has a sign-out owed; its credential is " +
                  (erased ? "erased"
                          : barrier ? "STILL ON DISK (could not be erased)"
                                    : "left on disk (the counter could not be written)") +
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
      // Refused: it opens nothing any more. Erased behind the counter, like every other way a
      // credential leaves; if the counter cannot be written it stays, and is refused again.
      const bool barrier = store.BumpGeneration(lock) != 0;
      const bool erased = barrier && store.Erase(lock);
      wipe(&stored);
      result.outcome = Return::Rejected;
      result.detail = error;
      say(deps, "device " + tag(result.deviceId) + ": the directory refused the credential; it is " +
                    (erased ? "erased"
                            : barrier ? "STILL ON DISK (could not be erased)"
                                      : "left on disk (the counter could not be written)"));
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
      say(deps, std::string("device ") + tag(result.deviceId) +
                    ": no answer about the credential (" + session_call_name(call) +
                    "); it is kept");
      return result;
  }
  return result;
}

// ------------------------------------------------------------------------------ signing in

uint64_t begin_sign_in(const Store& store, const Deps& deps) {
  Store::Lock lock = store.Acquire(deps.lockWaitMs);
  if (!lock.held()) {
    say(deps, "could not be had in time; the sign-in is not made");
    return 0;
  }
  const uint64_t generation = store.BumpGeneration(lock);
  if (generation == 0) say(deps, "the counter could not be written; the sign-in is not made");
  return generation;
}

Remembered remember_sign_in(const Store& store, const Deps& deps, uint64_t generation,
                            const std::string& accountId, const DeviceSignIn& signedIn) {
  const bool issued = !signedIn.deviceId.empty() && !signedIn.deviceCredential.empty() &&
                      !signedIn.revokeToken.empty();

  Remembered outcome = Remembered::Superseded;
  std::string why = "something happened to the store after this sign-in began";
  std::string previousDevice;
  // A device that could not be written down as owed and was ended at the directory instead,
  // on the first pass. The second pass finds it still stored and knows it is dealt with.
  std::string endedAtDirectory;

  for (int pass = 0; pass < 2; ++pass) {
    Store::Lock lock = store.Acquire(deps.lockWaitMs);
    if (!lock.held() || generation == 0) {
      outcome = Remembered::NotSaved;
      why = "the store could not be had";
      break;
    }
    // Asked whether or not the directory issued anything to keep. A sign-in answered after a
    // sign-out is late either way, and its session is not used.
    if (store.Generation(lock) != generation) {
      outcome = Remembered::Superseded;
      break;
    }

    // What is stored now is about to stop being this device's sign-in: a sign-in made on
    // purpose starts a family of its own. The one before it is ended -- written down as owed,
    // or, failing that, ended at the directory first. It is never simply written over, and
    // never simply left: left, it would sign this device in as the account before.
    // The user is here and has just proved who they are. A list of owed sign-outs that cannot
    // be read is set aside -- kept, under another name -- so that a new one can be started.
    // Whether or not anything is stored: a list left unreadable blocks every return after this
    // sign-in, and there being no credential beside it (a sign-out came first) changes nothing.
    {
      std::vector<PendingRevoke> owed;
      if (store.LoadPendingRevokes(lock, &owed) == ReadResult::Unreadable) {
        std::string aside;
        if (store.SetAsidePendingRevokes(lock, &aside)) {
          say(deps, "the list of owed sign-outs could not be read; it is kept as " + aside +
                        " and a new one is started");
        }
      }
    }

    Credential previous;
    const bool hasPrevious = store.Load(lock, &previous) == ReadResult::Ok &&
                             previous.serverOrigin == deps.serverOrigin &&
                             previous.deviceId != signedIn.deviceId;
    if (hasPrevious && previous.deviceId != endedAtDirectory) {
      std::string recordWhy;
      if (!store.AddPendingRevoke(lock, PendingRevoke{previous.serverOrigin, previous.deviceId,
                                                      previous.revokeToken}, &recordWhy)) {
        if (pass == 1) {
          outcome = Remembered::NotSaved;
          why = "the sign-in it replaces could not be written down as owed (" + recordWhy + ")";
          wipe(&previous);
          break;
        }
        say(deps, "device " + tag(previous.deviceId) + ": could not be written down as owed (" +
                      recordWhy + "); the directory is told before it is replaced");
        // Out of the lock for the call, then round again: the generation is asked anew.
        lock = Store::Lock();
        const SessionCall told = tell_directory(deps, previous.deviceId, previous.revokeToken);
        const std::string device = previous.deviceId;
        wipe(&previous);
        if (!ended_at_directory(told)) {
          outcome = Remembered::NotSaved;
          why = "the sign-in it replaces could neither be written down as owed nor ended at "
                "the directory";
          break;
        }
        endedAtDirectory = device;
        continue;
      }
    }
    if (hasPrevious) previousDevice = previous.deviceId;
    wipe(&previous);

    // The counter moves again, on disk, before anything is stored or erased. It makes any
    // second answer to this same sign-in late -- and it is the test of whether the counter
    // can be written at all. A sign-out that found it could not be written did not happen
    // (sign_out), and had no way to make this answer late; so the answer is kept only by a
    // store whose counter works.
    if (store.BumpGeneration(lock) == 0) {
      outcome = Remembered::NotSaved;
      why = "the counter could not be written";
      break;
    }

    if (!issued) {
      // The directory issued nothing to keep. What WAS kept still goes.
      if (hasPrevious && !store.Erase(lock)) {
        outcome = Remembered::NotSaved;
        why = "the sign-in it replaces could not be erased";
        break;
      }
      outcome = Remembered::NotIssued;
      break;
    }

    Credential fresh{deps.serverOrigin, accountId, signedIn.deviceId, signedIn.deviceCredential,
                     signedIn.revokeToken};
    std::string saveWhy;
    if (store.Save(lock, fresh, &saveWhy)) {
      outcome = Remembered::Stored;
    } else {
      outcome = Remembered::NotSaved;
      why = "the credential could not be written (" + saveWhy + ")";
    }
    wipe(&fresh);
    break;
  }

  if (outcome == Remembered::Stored || outcome == Remembered::NotIssued) {
    say(deps, (issued ? "device " + tag(signedIn.deviceId) + ": stored"
                      : std::string("the directory issued no credential; nothing is stored")) +
                  (previousDevice.empty()
                       ? std::string()
                       : "; device " + tag(previousDevice) + " before it is " +
                             (previousDevice == endedAtDirectory ? "ended" : "owed a sign-out")));
    return outcome;
  }
  if (issued) {
    end_unkept_device(store, deps, signedIn, why);
  } else {
    say(deps, "the sign-in is not used: " + why);
  }
  return outcome;
}

void discard_sign_in(const Store& store, const Deps& deps, const DeviceSignIn& signedIn,
                     const std::string& why) {
  if (signedIn.deviceId.empty() || signedIn.revokeToken.empty()) return;
  end_unkept_device(store, deps, signedIn, why);
}

// ------------------------------------------------------------------------------ signing out

SignOutResult sign_out(const Store& store, const Deps& deps) {
  SignOutResult result;
  Credential stored;
  bool recorded = false;
  {
    Store::Lock lock = store.Acquire(deps.lockWaitMs);
    if (!lock.held()) {
      result.detail = "the sign-in store is in use by another GNLink window";
      say(deps, "could not be had in time; the credential was not erased");
      return result;
    }
    // The counter first, and on disk: from here every answer still in flight is late. If it
    // cannot be written there is nothing that would make them late, and a sign-in answered a
    // moment from now would store itself over this sign-out. So nothing else is done.
    if (store.BumpGeneration(lock) == 0) {
      result.detail = "the sign-out could not be recorded";
      say(deps, "the counter could not be written; the sign-out is not made");
      return result;
    }
    std::string why;
    const ReadResult read = store.Load(lock, &stored, &why);
    if (read == ReadResult::None) {
      result.localDone = true;
      result.detail = "nothing was stored";
      return result;
    }
    if (read == ReadResult::Unreadable) {
      // It opens nothing and names no device to end. It goes.
      result.localDone = store.Erase(lock);
      result.detail = result.localDone ? "what was stored could not be read, and is removed"
                                       : "the stored sign-in could not be removed";
      say(deps, "the stored credential could not be read (" + why + "); " +
                    (result.localDone ? "erased" : "COULD NOT BE ERASED"));
      return result;
    }
    if (stored.serverOrigin != deps.serverOrigin) {
      // Another server's. Signing out of this one does not touch it.
      result.localDone = true;
      result.detail = "nothing of this server's was stored";
      wipe(&stored);
      return result;
    }
    // Written down BEFORE the credential goes, and the credential goes only if it was. If
    // this process dies between the two, the credential is still there -- with a record
    // beside it that says it is not to be used, which come_back() reads first.
    result.localDone = retire_under_lock(store, lock, stored, &recorded, &why);
    result.owed = recorded;
    if (!result.localDone) {
      result.detail = why;
      say(deps, "device " + tag(stored.deviceId) + ": could not be signed out on disk (" + why +
                    ")" + (recorded ? "" : "; the directory is told first"));
    }
  }

  const SessionCall told = tell_directory(deps, stored.deviceId, stored.revokeToken);
  const std::string device = stored.deviceId;
  wipe(&stored);

  if (!recorded) {
    // Nothing could be written down. The credential is kept -- it holds the only means of
    // ending the device -- unless the directory itself says the device is ended. Then there
    // is nothing left for it to open, and it may go.
    if (ended_at_directory(told)) {
      // The lock was let go for the call, and another window may have signed in meanwhile and
      // stored a family of its own -- alive at the directory, and this file its only means of
      // being ended. So what is erased is THIS device's credential, read again under the lock,
      // and nothing else. (The counter is not the test: a sign-in begun elsewhere moves it
      // without storing anything, and A's credential is then still here and still to go.)
      Store::Lock lock = store.Acquire(deps.lockWaitMs);
      Credential now;
      const ReadResult again = lock.held() ? store.Load(lock, &now) : ReadResult::Unreadable;
      const bool stillThis = again == ReadResult::Ok && now.serverOrigin == deps.serverOrigin &&
                             now.deviceId == device;
      wipe(&now);
      result.serverTold = told == SessionCall::Ok;
      if (lock.held() && !stillThis && again != ReadResult::Unreadable) {
        // Gone already, or replaced by another sign-in: this device is ended and not on disk.
        result.localDone = true;
        say(deps, "device " + tag(device) + ": the directory ended it (" + session_call_name(told) +
                      "); what is stored now is not it, and is left as it is");
        return result;
      }
      result.localDone = lock.held() && stillThis && store.Erase(lock);
      result.detail = result.localDone ? std::string() : "the stored sign-in could not be removed";
      say(deps, "device " + tag(device) + ": the directory ended it (" + session_call_name(told) +
                    "); the credential is " +
                    (result.localDone ? "erased" : "STILL ON DISK (could not be erased)"));
    } else {
      result.localDone = false;
      result.detail = "the sign-out could not be written down and the server could not be told";
      say(deps, "device " + tag(device) + ": NOT signed out -- nothing could be written down and "
                    "the directory could not be told (" + session_call_name(told) +
                    "); the credential is kept");
    }
    return result;
  }

  if (ended_at_directory(told)) {
    result.serverTold = told == SessionCall::Ok;
    if (!result.localDone) {
      // The credential could not be erased. The record beside it is what keeps it from being
      // used, so the record stays even though the directory has been told.
      result.owed = true;
      say(deps, "device " + tag(device) + ": the directory answered " + session_call_name(told) +
                    "; the record is kept because the credential is still on disk");
      return result;
    }
    Store::Lock lock = store.Acquire(deps.lockWaitMs);
    const bool cleared = lock.held() && store.RemovePendingRevoke(lock, device);
    result.owed = !cleared;
    say(deps, "device " + tag(device) + ": signed out; the directory answered " +
                  session_call_name(told));
    return result;
  }
  result.serverTold = false;
  result.owed = true;
  if (result.detail.empty()) result.detail = "the server could not be told yet";
  say(deps, "device " + tag(device) + ": signed out here; the directory could not be told (" +
                session_call_name(told) + ") and that is recorded as owed");
  return result;
}

size_t settle_owed_sign_outs(const Store& store, const Deps& deps) {
  std::vector<PendingRevoke> owed;
  {
    Store::Lock lock = store.Acquire(deps.lockWaitMs);
    if (!lock.held()) return 0;
    const ReadResult read = store.LoadPendingRevokes(lock, &owed);
    if (read == ReadResult::Unreadable) {
      say(deps, "the list of owed sign-outs cannot be read; nothing is sent and nothing is removed");
      return 1;   // something is owed that cannot be settled: not "none"
    }
    if (read != ReadResult::Ok) return 0;
  }
  size_t remaining = 0;
  for (PendingRevoke& pending : owed) {
    if (pending.serverOrigin != deps.serverOrigin) {
      // Owed to another server. Not sent to this one; kept for when that one is the server.
      ++remaining;
      continue;
    }
    const SessionCall told = tell_directory(deps, pending.deviceId, pending.revokeToken);
    if (ended_at_directory(told)) {
      Store::Lock lock = store.Acquire(deps.lockWaitMs);
      if (!lock.held() || !store.RemovePendingRevoke(lock, pending.deviceId)) ++remaining;
      say(deps, "device " + tag(pending.deviceId) + ": an owed sign-out was settled (" +
                    session_call_name(told) + ")");
    } else {
      ++remaining;
      say(deps, "device " + tag(pending.deviceId) + ": a sign-out is still owed (" +
                    session_call_name(told) + ")");
    }
    wipe(&pending.revokeToken);
  }
  return remaining;
}

}  // namespace remote60::native_poc::login_flow

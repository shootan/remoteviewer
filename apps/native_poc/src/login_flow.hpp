#pragma once

// What a sign-in, a return, and a sign-out do with the stored credential.
//
// The store (login_credential_store.hpp) is files and a lock. The directory calls
// (directory_session_client.hpp) are requests. This is the order they happen in, and the order
// is the feature: each function here is one transaction under the store's lock, so that a
// second GNLinkClient -- or the same one, answering late -- cannot act on a credential the
// first has already replaced, erased or signed out.
//
// The directory calls come in as functions. The product passes the real ones; a test passes
// ones that count, fail and answer late, which is how "the answer arrived after the sign-out"
// gets tested without a server that can be told to be slow.
//
// Nothing here touches a window. Every function blocks -- on the lock, for a bounded time, and
// on the network -- and is meant to be called from a worker.

#include <cstdint>
#include <functional>
#include <string>

#include "directory_session_client.hpp"
#include "login_credential_store.hpp"

namespace remote60::native_poc::login_flow {

struct Deps {
  // directory_origin_key() of the one server this client signs in to. A credential stored for
  // any other origin is not presented and not erased.
  std::string serverOrigin;
  std::function<SessionCall(const std::string& deviceId, const std::string& deviceCredential,
                            DeviceSignIn* out, std::string* error)> refresh;
  std::function<SessionCall(const std::string& deviceId, const std::string& revokeToken,
                            std::string* error)> revoke;
  // Where a line for the client's log goes. Never handed a credential, a revoke token or a
  // session: what is logged is an outcome and the first eight characters of a device id.
  std::function<void(const std::string&)> log;
  // How long to wait for the store. Bounded, so a worker is never parked behind another
  // process for longer than this.
  uint32_t lockWaitMs = 8000;
};

// ------------------------------------------------------------------------------ coming back

enum class Return {
  SignedIn,       // a session, and the next credential is stored
  NoCredential,   // nothing stored (or stored for another server): show the sign-in form
  SignedOut,      // a sign-out was owed for the stored credential: it is finished, not resumed
  Rejected,       // the directory refused the credential: erased, show the sign-in form
  Unreadable,     // the file cannot be used: left where it is, show the sign-in form
  ServerCannot,   // a directory from before device credentials: kept, show the sign-in form
  TryLater,       // no answer about the credential (429, 5xx, unreachable): kept
  Busy,           // the store could not be had in time: kept, nothing was sent
  NotSaved,       // the directory rotated and the new credential could not be written
  OtherAccount,   // what is stored belongs to another account than the caller's: not presented
};
const char* return_name(Return value);

struct ReturnResult {
  Return outcome = Return::NoCredential;
  std::string sessionToken;
  std::string accountId;   // set whenever a credential was read, so the form can show the id
  std::string deviceId;
  std::string detail;      // for the log and the status line; never a secret
};

/**
 * One attempt to come back signed in. Lock, read again, refresh, store, unlock.
 *
 * "Read again" is the point of the lock: what is presented is what is in the file at the
 * moment this process holds it, which may be the credential another process stored a second
 * ago.
 *
 * Which is also why `onlyForAccount` exists. A window that is signed in as one account, and
 * finds its session refused, must not be handed the session of whoever signed in from another
 * window since: given an account, this presents nothing unless what is stored is that
 * account's. Empty means any -- which is right only when no account is signed in here yet.
 *
 * Nothing is presented, either, when the list of sign-outs owed cannot be read: whether the
 * stored credential was signed out of is written there.
 */
ReturnResult come_back(const login_store::Store& store, const Deps& deps,
                       const std::string& onlyForAccount = {});

// ------------------------------------------------------------------------------ signing in

/**
 * Marks the start of a sign-in the user asked for, and returns the generation it runs under.
 *
 * Moving the counter is what makes every answer still in flight -- an earlier sign-in, a
 * refresh, in this process or another -- late.
 *
 * 0 means the store could not be had or the counter could not be written, AND THE SIGN-IN IS
 * NOT TO BE MADE. Going ahead without the store would leave whatever is stored in place: sign
 * in as B over a stored A, restart, and the client comes back as A.
 */
uint64_t begin_sign_in(const login_store::Store& store, const Deps& deps);

enum class Remembered {
  Stored,      // the credential is on disk
  NotIssued,   // the directory issued none: the sign-in stands, there is nothing to store
  Superseded,  // something happened since begin_sign_in: not stored, and the device was ended
  NotSaved,    // it could not be written: the device was ended rather than left unowned
};
const char* remembered_name(Remembered value);

/**
 * Stores what a successful sign-in was given -- if this sign-in is still the latest thing that
 * happened to the store.
 *
 * When it is not (a sign-out, or another sign-in, got there first) the credential is NOT
 * stored and the device it belongs to is ended with its revoke token; if the directory cannot
 * be told, the sign-out is recorded as owed. Either way nothing is left that would sign this
 * device in later without anybody having asked.
 *
 * The credential that was stored before is ended first: written down as owed, or ended at the
 * directory when it cannot be written down. If neither can be done this sign-in is not kept.
 *
 * The same is asked of a sign-in the directory issued nothing for (`NotIssued`): it is checked
 * against the counter like any other, and what was stored before it goes.
 *
 * Only `Stored` and `NotIssued` mean the session may be used.
 */
Remembered remember_sign_in(const login_store::Store& store, const Deps& deps,
                            uint64_t generation, const std::string& accountId,
                            const DeviceSignIn& signedIn);

/**
 * Ends the device a sign-in was issued, for a caller that has decided not to use the sign-in:
 * the window signed out while it was in flight, or what had to follow it failed. Told to the
 * directory now if it can be, written down as owed if it cannot. Does nothing for a sign-in
 * the directory issued no device for.
 */
void discard_sign_in(const login_store::Store& store, const Deps& deps,
                     const DeviceSignIn& signedIn, const std::string& why);

// ------------------------------------------------------------------------------ signing out

struct SignOutResult {
  // The credential is gone from this machine. FALSE MEANS THE SIGN-OUT DID NOT HAPPEN: what
  // is stored is still there and the next start may come back signed in. The caller says so.
  bool localDone = false;
  bool serverTold = false;    // the directory confirmed the device is ended
  bool owed = false;          // ...or it could not be told, and that is recorded
  std::string detail;
};

/**
 * Signs this device out.
 *
 * In this order, each only if the one before it was written: the counter moves; the sign-out
 * is written down as owed; the credential is erased -- all under the lock -- and only then is
 * the directory told. A crash at any point leaves either the credential with a record saying
 * it must not be used, or no credential.
 *
 * When the record cannot be written the credential is kept, because it holds the only means
 * of ending the device; it goes only if the directory itself confirms the device is ended.
 *
 * The record is removed when the directory says the device is ended (200) or that it does not
 * know the token (401). It stays when the directory cannot be reached AND WHEN IT HAS NO SUCH
 * ROUTE (404/405): a server from before device credentials has ended nothing.
 */
SignOutResult sign_out(const login_store::Store& store, const Deps& deps);

/**
 * Tells the directory about every sign-out still owed. Called when the client starts and after
 * a sign-in. Returns how many are still owed afterwards.
 */
size_t settle_owed_sign_outs(const login_store::Store& store, const Deps& deps);

}  // namespace remote60::native_poc::login_flow

#include "secure_unlock_service.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace remote60::native_poc::sealed_unlock_service {

namespace {

uint16_t stage_code(UnlockStage s) { return static_cast<uint16_t>(s); }

SecureUnlockResult make_result(uint32_t requestId, uint32_t jobId, UnlockStage stage, bool terminal, uint32_t win32 = 0) {
  SecureUnlockResult r{};
  r.requestId = requestId;
  r.jobId = jobId;
  r.stage = stage_code(stage);
  r.terminal = terminal ? 1 : 0;
  r.win32Error = win32;
  return r;
}

template <typename... Args>
void log_line(const UnlockPrimitives& p, const char* fmt, Args... args) {
  if (!p.log) return;
  char line[256];
  std::snprintf(line, sizeof(line), fmt, args...);
  p.log(line);
}

}  // namespace

namespace {

// The caller's session as the service sees it now: one read of everything a challenge decision needs.
struct CallerState {
  uint32_t requester = kNoSession;
  uint32_t console = kNoSession;
  bool lockKnown = false;
  bool locked = false;
  bool stateKnown = false;
  bool active = false;
  uint32_t topo = 0;
};

CallerState read_caller_state(const UnlockSession& us, const UnlockPrimitives& p) {
  CallerState s;
  // r15 R14-2: the requester is THIS connection's caller (read from the unlock pipe at connect).
  s.requester = us.caller.session;
  s.console = p.consoleSession();
  s.locked = s.requester != kNoSession && p.sessionIsLocked(s.requester, &s.lockKnown);
  s.topo = p.topologyGeneration();
  s.active = s.requester != kNoSession && p.sessionIsActive(s.requester, &s.stateKnown);
  return s;
}

// lockscreen-cred r1 (contract A7): why a challenge was refused, said in one bounded line -- reason and
// sessions only, never a request's payload. At most this many per unlock-pipe connection.
constexpr uint32_t kRejectLogBudget = 16;
void log_rejected(UnlockSession& us, const UnlockPrimitives& p, const char* purpose, const char* reason,
                  const CallerState& s) {
  if (us.rejectLogs >= kRejectLogBudget) return;
  ++us.rejectLogs;
  log_line(p, "unlock challenge rejected purpose=%s reason=%s requester=%u console=%u lockKnown=%d locked=%d stateKnown=%d active=%d",
           purpose, reason, s.requester, s.console, s.lockKnown ? 1 : 0, s.locked ? 1 : 0, s.stateKnown ? 1 : 0,
           s.active ? 1 : 0);
}

void write_rejected(const SecureUnlockChallengeRequest& req, uint16_t purpose, UnlockStage stage, const Write& write) {
  SecureUnlockChallengeResponse resp{};
  resp.requestId = req.requestId;
  resp.clientSessionCookie = req.clientSessionCookie;
  resp.purpose = purpose;
  resp.stage = stage_code(stage);
  write(&resp, sizeof(resp));
}

// Issues a challenge for `purpose` to a caller whose state was already found eligible.
void issue_challenge(UnlockSession& us, const SecureUnlockChallengeRequest& req, const CallerState& s, uint16_t purpose,
                     const UnlockPrimitives& p, const Write& write);

}  // namespace

void HandleChallengeRequest(UnlockSession& us, const SecureUnlockChallengeRequest& req, const UnlockPrimitives& p,
                            const Write& write) {
  const CallerState s = read_caller_state(us, p);
  const bool consoleUsable = (s.console != kNoSession && s.console != 0u);

  // Only issue for a genuinely locked requester that is DISCONNECTED, with a valid console to
  // reconnect it onto (Codex #370 HIGH 2). Not-locked / still-active / no-console -> RejectedPolicy,
  // so we never move an active RDP session or seal a password toward a UAC prompt / usable desktop.
  // (lockscreen-cred r1: unchanged. The ACTIVE locked console session is HandleLockCredChallengeRequest's.)
  const char* reason = nullptr;
  if (s.requester == kNoSession || s.requester == 0u) reason = "no-requester";
  else if (!s.lockKnown) reason = "lock-unknown";
  else if (!s.locked) reason = "not-locked";
  else if (!consoleUsable) reason = "no-console";
  else if (!s.stateKnown) reason = "state-unknown";
  else if (s.active) reason = "session-active";
  if (reason) {
    log_rejected(us, p, "unlock", reason, s);
    write_rejected(req, kUnlockPurposeUnlock, UnlockStage::RejectedPolicy, write);
    return;
  }
  issue_challenge(us, req, s, kUnlockPurposeUnlock, p, write);
}

void HandleLockCredChallengeRequest(UnlockSession& us, const SecureUnlockChallengeRequest& req,
                                    const UnlockPrimitives& p, const Write& write) {
  const CallerState s = read_caller_state(us, p);
  // lockscreen-cred r1 (Codex A4): the caller's OWN session, which is the console session, ACTIVE (attached
  // to the console -- not disconnected, not RDP) and locked -- all known, none guessed. Nothing else: an
  // unknown state is a refusal, never "probably locked".
  const char* reason = nullptr;
  if (!p.typeLockCred) reason = "not-offered";
  else if (s.requester == kNoSession || s.requester == 0u) reason = "no-requester";
  else if (s.console == kNoSession || s.console == 0u) reason = "no-console";
  else if (s.requester != s.console) reason = "not-console";
  else if (!s.stateKnown) reason = "state-unknown";
  else if (!s.active) reason = "session-not-active";
  else if (!s.lockKnown) reason = "lock-unknown";
  else if (!s.locked) reason = "not-locked";
  // Codex Q1 (a): a typed credential still waiting for its unlock keeps its context -- a new request is
  // refused until it is answered (unlocked / unconfirmed); then a new one may replace the field's content.
  else if (us.lockCredWaiting && p.nowMs() - us.lockCredTypedMs <= kLockCredUnlockWaitMs) reason = "pending";
  if (reason) {
    log_rejected(us, p, "lock-cred", reason, s);
    write_rejected(req, kUnlockPurposeLockCred, UnlockStage::RejectedPolicy, write);
    return;
  }
  issue_challenge(us, req, s, kUnlockPurposeLockCred, p, write);
}

void HandleCapabilities(const SecureUnlockQuery& q, const UnlockPrimitives& p, const Write& write) {
  SecureUnlockCapabilitiesResponse r{};
  r.requestId = q.requestId;
  r.features = p.typeLockCred ? kSecureUnlockFeatureLockCredV1 : 0u;
  write(&r, sizeof(r));
}

namespace {

void issue_challenge(UnlockSession& us, const SecureUnlockChallengeRequest& req, const CallerState& s, uint16_t purpose,
                     const UnlockPrimitives& p, const Write& write) {
  SecureUnlockChallengeResponse resp{};
  resp.requestId = req.requestId;
  resp.clientSessionCookie = req.clientSessionCookie;
  resp.purpose = purpose;
  const uint32_t requester = s.requester;
  const uint32_t console = s.console;
  const uint32_t topo = s.topo;

  // Rate-limit challenge issuance: the capability is advertised on the wire, so an external client can
  // ask; do not let it spin ECDH keygen / WTS queries. (Codex 3rd review.)
  const uint64_t nowChallengeMs = p.nowMs();
  if (us.lastChallengeMs != 0 && nowChallengeMs - us.lastChallengeMs < 500) {
    resp.stage = stage_code(UnlockStage::RejectedPolicy);
    write(&resp, sizeof(resp));
    return;
  }
  us.lastChallengeMs = nowChallengeMs;

  us.hasChallenge = false;
  if (!us.key.Generate()) {
    resp.stage = stage_code(UnlockStage::InternalError);
    write(&resp, sizeof(resp));
    return;
  }
  uint8_t hostPub[su::kPubKeyBytes] = {};
  uint8_t challengeId[su::kChallengeIdBytes] = {};
  if (!us.key.ExportPublic(hostPub) || !su::RandomBytes(us.salt, su::kSaltBytes) ||
      !su::RandomBytes(challengeId, su::kChallengeIdBytes)) {
    resp.stage = stage_code(UnlockStage::InternalError);
    write(&resp, sizeof(resp));
    return;
  }

  su::UnlockContext c{};
  c.protocolVersion = 1;
  c.hostId = p.hostId();
  c.clientSessionCookie = req.clientSessionCookie;
  std::memcpy(c.challengeId, challengeId, su::kChallengeIdBytes);
  c.requestId = req.requestId;
  c.requesterSession = requester;
  c.consoleSession = console;
  c.lockGeneration = topo;  // lock/unlock bumps the topology counter too
  c.topologyGeneration = topo;
  c.issuedMs = p.nowMs();
  c.expiresMs = c.issuedMs + 30000;
  bool accountKnown = false;
  c.accountId = p.accountIdOf(requester, &accountKnown);
  if (!accountKnown) {
    resp.stage = stage_code(UnlockStage::RejectedPolicy);
    write(&resp, sizeof(resp));
    return;
  }
  std::memcpy(c.hostPub, hostPub, su::kPubKeyBytes);
  us.ctx = c;
  us.challenge.Issue(challengeId, req.clientSessionCookie, topo, c.issuedMs, c.expiresMs);
  us.hasChallenge = true;
  us.purpose = purpose;  // lockscreen-cred r1: fixed here, for this challenge's sealed request and its result
  us.badAttempts = 0;

  resp.stage = stage_code(UnlockStage::ChallengeIssued);
  std::memcpy(resp.challengeId, challengeId, su::kChallengeIdBytes);
  std::memcpy(resp.hostPub, hostPub, su::kPubKeyBytes);
  std::memcpy(resp.salt, us.salt, su::kSaltBytes);
  resp.hostId = c.hostId;
  resp.accountId = c.accountId;
  resp.requesterSession = c.requesterSession;
  resp.consoleSession = c.consoleSession;
  resp.lockGeneration = c.lockGeneration;
  resp.topologyGeneration = c.topologyGeneration;
  resp.issuedMs = c.issuedMs;
  resp.expiresMs = c.expiresMs;
  log_line(p, "unlock challenge issued purpose=%s requestId=%u requester=%u console=%u topo=%u",
           purpose == kUnlockPurposeLockCred ? "lock-cred" : "unlock", req.requestId, requester, console, topo);
  write(&resp, sizeof(resp));
}

// lockscreen-cred r1: where the waiting lock-screen credential is now. SessionUnlocked only for an unlock of
// the SAME session seen after the typing (its lock state known and unlocked, and a session event since the
// typing -- the topology moved); a timeout ends it as UnlockUnconfirmed. Both are terminal and cached.
SecureUnlockResult lock_cred_status(UnlockSession& us, const UnlockPrimitives& p) {
  bool known = false;
  const bool locked = p.sessionIsLocked(us.ctx.requesterSession, &known);
  const bool moved = p.topologyGeneration() != us.lockCredTopology;
  UnlockStage stage = UnlockStage::Injected;
  if (known && !locked && moved) {
    stage = UnlockStage::SessionUnlocked;
  } else if (p.nowMs() - us.lockCredTypedMs > kLockCredUnlockWaitMs) {
    stage = UnlockStage::UnlockUnconfirmed;
  }
  if (stage == UnlockStage::Injected) {
    return make_result(us.lockCredRequestId, us.lockCredJobId, stage, false);
  }
  const SecureUnlockResult r = make_result(us.lockCredRequestId, us.lockCredJobId, stage, true);
  us.lockCredWaiting = false;
  us.hasTerminal = true;
  us.terminalRequestId = r.requestId;
  us.terminalCookie = us.lockCredCookie;
  us.terminal = r;
  log_line(p, "lock-cred requestId=%u -> %s session=%u", r.requestId,
           stage == UnlockStage::SessionUnlocked ? "unlocked" : "unlock-unconfirmed", us.ctx.requesterSession);
  return r;
}

}  // namespace

void HandleStatusRequest(UnlockSession& us, const SecureUnlockQuery& q, const UnlockPrimitives& p, const Write& write) {
  // r2 (L1): a TRANSACTION -- this control session's request, never another's with the same id.
  if (us.hasTerminal && us.terminalRequestId == q.requestId && us.terminalCookie == q.clientSessionCookie) {
    write(&us.terminal, sizeof(us.terminal));
    return;
  }
  if (us.lockCredWaiting && us.lockCredRequestId == q.requestId && us.lockCredCookie == q.clientSessionCookie) {
    const SecureUnlockResult r = lock_cred_status(us, p);
    write(&r, sizeof(r));
    return;
  }
  const SecureUnlockResult r = make_result(q.requestId, 0, UnlockStage::RejectedPolicy, true);
  write(&r, sizeof(r));
}

void HandleSealedRequest(UnlockSession& us, const SecureUnlockSealedRequest& req, uint32_t jobId,
                         const UnlockPrimitives& p, const Write& write) {
  // Idempotent duplicate of a finished job (r2 L1: of the same control session).
  if (us.hasTerminal && us.terminalRequestId == req.requestId && us.terminalCookie == req.clientSessionCookie) {
    SecureUnlockResult r = us.terminal;
    r.jobId = jobId;
    write(&r, sizeof(r));
    return;
  }
  // lockscreen-cred r1: a duplicate of a typed lock-screen credential that is still waiting for its unlock is
  // answered with where it is now -- never typed again.
  if (us.lockCredWaiting && us.lockCredRequestId == req.requestId && us.lockCredCookie == req.clientSessionCookie) {
    SecureUnlockResult r = lock_cred_status(us, p);
    r.jobId = us.lockCredJobId;
    write(&r, sizeof(r));
    return;
  }

  auto reject = [&](UnlockStage stage, uint32_t win32 = 0) {
    const SecureUnlockResult r = make_result(req.requestId, jobId, stage, true, win32);
    us.hasTerminal = true;
    us.terminalRequestId = req.requestId;
    us.terminalCookie = req.clientSessionCookie;
    us.terminal = r;
    write(&r, sizeof(r));
  };
  auto rejectNoCache = [&](UnlockStage stage) {  // do not cache: allows a fresh retry after new challenge
    const SecureUnlockResult r = make_result(req.requestId, jobId, stage, true);
    write(&r, sizeof(r));
  };

  if (!us.hasChallenge) { rejectNoCache(UnlockStage::RejectedPolicy); return; }
  // The sealed request's outer requestId must equal the challenge's, or a valid ciphertext could be
  // replayed under a different outer id and cached against it (Codex #370 BLOCKER D).
  if (req.requestId != us.ctx.requestId) { rejectNoCache(UnlockStage::RejectedPolicy); return; }

  const uint32_t topoNow = p.topologyGeneration();
  const auto verdict = us.challenge.Verify(req.challengeId, req.clientSessionCookie, us.ctx.topologyGeneration, p.nowMs());
  if (verdict == su::ChallengeVerdict::AlreadyConsumed) {
    if (us.hasTerminal && us.terminalRequestId == req.requestId && us.terminalCookie == req.clientSessionCookie) {
      SecureUnlockResult r = us.terminal;
      r.jobId = jobId;
      write(&r, sizeof(r));
      return;
    }
    rejectNoCache(UnlockStage::RejectedPolicy);
    return;
  }
  if (verdict == su::ChallengeVerdict::Expired) { rejectNoCache(UnlockStage::Timeout); return; }
  if (verdict != su::ChallengeVerdict::Valid) { rejectNoCache(UnlockStage::RejectedPolicy); return; }
  // Topology changed since issue (session moved/locked/unlocked): refuse before touching crypto.
  if (topoNow != us.ctx.topologyGeneration) { rejectNoCache(UnlockStage::RejectedStaleTopology); return; }

  // Rebuild the exact context (add the client's public point) and derive the key.
  su::UnlockContext c = us.ctx;
  std::memcpy(c.clientPub, req.clientPub, su::kPubKeyBytes);
  // lockscreen-cred r1 (Codex A3): opened under the purpose the challenge was issued for, and only that one.
  const bool lockCred = us.purpose == kUnlockPurposeLockCred;
  const auto kdf = lockCred ? su::BuildLockCredKdfInfo(c) : su::BuildKdfInfo(c);
  const auto aad = lockCred ? su::BuildLockCredAad(c) : su::BuildAad(c);
  uint8_t aesKey[su::kAesKeyBytes] = {};
  if (!su::DeriveAesKey(us.key, req.clientPub, us.salt, kdf.data(), kdf.size(), aesKey)) {
    su::SecureZero(aesKey, sizeof(aesKey));
    rejectNoCache(UnlockStage::InternalError);
    return;
  }
  uint8_t plain[su::kPlaintextBytes] = {};
  const bool opened =
      su::AesGcmOpen(aesKey, req.nonce, aad.data(), aad.size(), req.cipher, su::kPlaintextBytes, req.tag, plain);
  su::SecureZero(aesKey, sizeof(aesKey));
  if (!opened) {
    su::SecureZero(plain, sizeof(plain));
    if (++us.badAttempts >= 5) {
      us.challenge.ClearOutstanding();
      us.hasChallenge = false;
    }
    rejectNoCache(UnlockStage::DecryptFailed);  // invalid tag does NOT consume the challenge
    return;
  }
  // Re-check topology one last time right before we commit + act.
  if (p.topologyGeneration() != us.ctx.topologyGeneration) {
    su::SecureZero(plain, sizeof(plain));
    rejectNoCache(UnlockStage::RejectedStaleTopology);
    return;
  }
  us.challenge.Consume(req.challengeId);  // exactly-once from here
  us.hasChallenge = false;

  uint16_t pw[su::kMaxPasswordUtf16] = {};
  uint16_t pwCount = 0;
  const bool unpacked = su::UnpackPassword(plain, pw, &pwCount);
  su::SecureZero(plain, sizeof(plain));
  if (!unpacked) {
    su::SecureZero(pw, sizeof(pw));
    reject(UnlockStage::InternalError);
    return;
  }

  if (lockCred) {
    // lockscreen-cred r1 (Codex A4/A5): everything it was issued for must still hold right before the typing
    // -- the topology, this pipe's caller and its session, the console, and that session still the console,
    // ACTIVE and locked (each known). Then the agent types it once; a target that is not ready, or a typing
    // that fails, is the answer as it is -- never retried here.
    bool lockKnown = false, stateKnown = false;
    const bool stillLocked = p.sessionIsLocked(us.ctx.requesterSession, &lockKnown);
    const bool stillActive = p.sessionIsActive(us.ctx.requesterSession, &stateKnown);
    if (p.topologyGeneration() != us.ctx.topologyGeneration || !us.callerStillThere || !us.callerStillThere() ||
        us.caller.session != us.ctx.requesterSession || p.consoleSession() != us.ctx.consoleSession ||
        us.ctx.requesterSession != us.ctx.consoleSession || !lockKnown || !stillLocked || !stateKnown || !stillActive) {
      su::SecureZero(pw, sizeof(pw));
      reject(UnlockStage::RejectedStaleTopology);
      return;
    }
    // r2 (Codex L2): the same fence again, asked by the agent right before it touches the field -- this
    // pipe's caller still there (its control session not ended), the topology / session / console / lock /
    // account as issued, the service running, and within the commit deadline. Anything else: nothing typed.
    // r3 (Codex L2-R): and never past the challenge's own expiry -- a sealed request taken just before it does
    // not buy a fresh 20 s (the same boundary as ChallengeState::Verify: past it = expired).
    const uint64_t commitDeadline = (std::min)(us.ctx.expiresMs, p.nowMs() + kLockCredCommitMs);
    const uint64_t accountAtIssue = us.ctx.accountId;
    const std::function<bool()> stillValid = [&us, &p, commitDeadline, accountAtIssue]() {
      if (p.running && !p.running()) return false;
      if (p.nowMs() > commitDeadline) return false;
      if (p.topologyGeneration() != us.ctx.topologyGeneration) return false;
      if (!us.callerStillThere || !us.callerStillThere() || us.caller.session != us.ctx.requesterSession) return false;
      if (p.consoleSession() != us.ctx.consoleSession) return false;
      bool lk = false, sk = false, ak = false;
      const bool stillLockedNow = p.sessionIsLocked(us.ctx.requesterSession, &lk);
      const bool stillActiveNow = p.sessionIsActive(us.ctx.requesterSession, &sk);
      const uint64_t account = p.accountIdOf(us.ctx.requesterSession, &ak);
      return lk && stillLockedNow && sk && stillActiveNow && ak && account == accountAtIssue;
    };
    uint32_t win32 = 0;
    const UnlockStage typed = p.typeLockCred(us.ctx.requesterSession, pw, pwCount, &win32, stillValid);
    su::SecureZero(pw, sizeof(pw));
    if (typed != UnlockStage::Injected) {
      log_line(p, "lock-cred requestId=%u not typed stage=%u win32=%u session=%u", req.requestId,
               static_cast<unsigned>(typed), win32, us.ctx.requesterSession);
      reject(typed == UnlockStage::TargetNotReady || typed == UnlockStage::RejectedStaleTopology ? typed
                                                                                              : UnlockStage::InjectFailed,
             win32);
      return;
    }
    us.lockCredWaiting = true;
    us.lockCredRequestId = req.requestId;
    us.lockCredCookie = req.clientSessionCookie;
    us.lockCredJobId = jobId;
    us.lockCredTopology = p.topologyGeneration();
    us.lockCredTypedMs = p.nowMs();
    log_line(p, "lock-cred requestId=%u typed session=%u", req.requestId, us.ctx.requesterSession);
    const SecureUnlockResult r = make_result(req.requestId, jobId, UnlockStage::Injected, false);
    write(&r, sizeof(r));
    return;
  }

  wchar_t pwz[su::kMaxPasswordUtf16 + 1] = {};
  for (uint16_t i = 0; i < pwCount; ++i) pwz[i] = static_cast<wchar_t>(pw[i]);
  su::SecureZero(pw, sizeof(pw));

  // Third topology fence, right before the irreversible WTS call (Codex #370 HIGH 1).
  if (p.topologyGeneration() != us.ctx.topologyGeneration) {
    su::SecureZero(pwz, sizeof(pwz));
    reject(UnlockStage::RejectedStaleTopology);
    return;
  }
  // Directly re-verify the identity right before the irreversible call. r15 R14-2: the requester is the
  // process on THIS pipe, still there with the session it was issued for -- not "the last input connection".
  if (!us.callerStillThere || !us.callerStillThere() || us.caller.session != us.ctx.requesterSession ||
      p.consoleSession() != us.ctx.consoleSession) {
    su::SecureZero(pwz, sizeof(pwz));
    reject(UnlockStage::RejectedStaleTopology);
    return;
  }
  // Reconnect the disconnected requester session onto the console with the account's password. bWait
  // FALSE returns promptly (Codex #370 BLOCKER 3): we confirm the unlock by polling the lock state.
  DWORD gle = 0;
  const bool ok = p.wtsConnect(us.ctx.requesterSession, us.ctx.consoleSession, pwz, &gle);
  su::SecureZero(pwz, sizeof(pwz));

  if (ok) {
    bool unlocked = false;
    for (int i = 0; i < 20 && (!p.running || p.running()); ++i) {
      bool k = false;
      if (!p.sessionIsLocked(us.ctx.requesterSession, &k) && k) {
        unlocked = true;
        break;
      }
      if (p.sleep) p.sleep(150);
    }
    log_line(p, "unlock WTSConnectSession ok, unlocked=%d requester=%u console=%u", unlocked ? 1 : 0,
             us.ctx.requesterSession, us.ctx.consoleSession);
    reject(unlocked ? UnlockStage::SessionUnlocked : UnlockStage::WtsConnectAccepted);
  } else {
    log_line(p, "unlock WTSConnectSession failed gle=%lu requester=%u console=%u", gle, us.ctx.requesterSession,
             us.ctx.consoleSession);
    // Wrong password / account restriction -> AuthFailed (no retry storm). Others -> InternalError.
    const UnlockStage stage = (gle == ERROR_LOGON_FAILURE || gle == ERROR_ACCOUNT_RESTRICTION ||
                               gle == ERROR_PASSWORD_EXPIRED || gle == ERROR_ACCOUNT_DISABLED)
                                  ? UnlockStage::AuthFailed
                                  : UnlockStage::InternalError;
    reject(stage, gle);
  }
}

}  // namespace remote60::native_poc::sealed_unlock_service

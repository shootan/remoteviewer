#include "secure_unlock_service.hpp"

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

void HandleChallengeRequest(UnlockSession& us, const SecureUnlockChallengeRequest& req, const UnlockPrimitives& p,
                            const Write& write) {
  SecureUnlockChallengeResponse resp{};
  resp.requestId = req.requestId;
  resp.clientSessionCookie = req.clientSessionCookie;

  // r15 R14-2: the requester is THIS connection's caller (read from the unlock pipe at connect).
  const uint32_t requester = us.caller.session;
  const uint32_t console = p.consoleSession();
  bool lockKnown = false;
  const bool locked = requester != kNoSession && p.sessionIsLocked(requester, &lockKnown);
  const uint32_t topo = p.topologyGeneration();
  bool reqStateKnown = false;
  const bool reqActive = requester != kNoSession && p.sessionIsActive(requester, &reqStateKnown);
  const bool consoleUsable = (console != kNoSession && console != 0u);

  // Only issue for a genuinely locked requester that is DISCONNECTED, with a valid console to
  // reconnect it onto (Codex #370 HIGH 2). Not-locked / still-active / no-console -> RejectedPolicy,
  // so we never move an active RDP session or seal a password toward a UAC prompt / usable desktop.
  if (requester == kNoSession || requester == 0u || !lockKnown || !locked || !consoleUsable || !reqStateKnown ||
      reqActive) {
    resp.stage = stage_code(UnlockStage::RejectedPolicy);
    write(&resp, sizeof(resp));
    return;
  }

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
  log_line(p, "unlock challenge issued requestId=%u requester=%u console=%u topo=%u", req.requestId, requester, console,
           topo);
  write(&resp, sizeof(resp));
}

void HandleSealedRequest(UnlockSession& us, const SecureUnlockSealedRequest& req, uint32_t jobId,
                         const UnlockPrimitives& p, const Write& write) {
  // Idempotent duplicate of a finished job.
  if (us.hasTerminal && us.terminalRequestId == req.requestId) {
    SecureUnlockResult r = us.terminal;
    r.jobId = jobId;
    write(&r, sizeof(r));
    return;
  }

  auto reject = [&](UnlockStage stage, uint32_t win32 = 0) {
    const SecureUnlockResult r = make_result(req.requestId, jobId, stage, true, win32);
    us.hasTerminal = true;
    us.terminalRequestId = req.requestId;
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
    if (us.hasTerminal && us.terminalRequestId == req.requestId) {
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
  const auto kdf = su::BuildKdfInfo(c);
  const auto aad = su::BuildAad(c);
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

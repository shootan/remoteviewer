// lockscreen-cred r1: the service's lock-screen credential (secure_unlock_service.cpp), its decisions run with
// the real crypto (the viewer's sealing, as secure_unlock_service_test does it) against stand-ins for the OS:
// session 1 is the console, ACTIVE and locked unless a case says otherwise; the agent's typing is recorded
// instead of done, and WTSConnectSession is recorded instead of called -- no session of this PC moves.
//
// What it pins (Codex A3/A4/A5): the challenge only for the caller's own active locked console session (the
// disconnected-session unlock stays as it was, and still refuses an ACTIVE session); the purpose fixed when the
// challenge is issued -- a blob sealed for another purpose does not open; typed once, never again for a replay;
// "unlocked" only for an unlock of that session seen after the typing; every refusal types nothing.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "sealed_unlock.hpp"
#include "secure_unlock_service.hpp"

namespace su = remote60::native_poc::sealed_unlock;
namespace sus = remote60::native_poc::sealed_unlock_service;
using remote60::native_poc::kUnlockPurposeLockCred;
using remote60::native_poc::kUnlockPurposeUnlock;
using remote60::native_poc::SecureUnlockCapabilitiesResponse;
using remote60::native_poc::SecureUnlockChallengeRequest;
using remote60::native_poc::SecureUnlockChallengeResponse;
using remote60::native_poc::SecureUnlockKind;
using remote60::native_poc::SecureUnlockQuery;
using remote60::native_poc::SecureUnlockResult;
using remote60::native_poc::SecureUnlockSealedRequest;
using remote60::native_poc::UnlockStage;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}

// The OS as the service sees it. Session 1 = console, active, locked.
struct Os {
  std::map<uint32_t, bool> locked{{1, true}, {2, true}};
  std::map<uint32_t, bool> active{{1, true}, {2, false}};
  std::map<uint32_t, uint64_t> account{{1, 0xA1}, {2, 0xA2}};
  uint32_t console = 1;
  uint32_t topo = 7;
  uint64_t now = 100000;
  bool offerLockCred = true;
  UnlockStage typeAnswer = UnlockStage::Injected;
  uint32_t typeWin32 = 0;
  struct Typed {
    uint32_t session;
    std::u16string text;
  };
  std::vector<Typed> typed;
  int wtsCalls = 0;
  std::vector<std::string> log;
  bool runningFlag = true;
  // r2 (Codex L2): what happens between the agent finding its target and touching the field.
  std::function<void(Os&)> beforeCommit;
  int fenceAsked = 0;
  sus::UnlockPrimitives prims() {
    sus::UnlockPrimitives p;
    p.sessionIsLocked = [this](uint32_t s, bool* k) { *k = locked.count(s) > 0; return *k && locked[s]; };
    p.sessionIsActive = [this](uint32_t s, bool* k) { *k = active.count(s) > 0; return active[s]; };
    p.consoleSession = [this] { return console; };
    p.accountIdOf = [this](uint32_t s, bool* k) { *k = account.count(s) > 0; return account[s]; };
    p.hostId = [] { return 0x4057ull; };
    p.topologyGeneration = [this] { return topo; };
    p.nowMs = [this] { return now += 600; };  // past the 500 ms issuance rate limit each time
    p.wtsConnect = [this](uint32_t r, uint32_t, const wchar_t*, DWORD* gle) {
      ++wtsCalls;
      *gle = 0;
      locked[r] = false;  // the reconnect unlocked it
      return true;
    };
    p.running = [this] { return runningFlag; };
    p.sleep = [](uint32_t) {};
    p.log = [this](const char* line) { log.emplace_back(line); };
    if (offerLockCred) {
      p.typeLockCred = [this](uint32_t session, const uint16_t* units, uint16_t count, uint32_t* win32,
                              const std::function<bool()>& stillValid) {
        // The product's agent: finds its target, then asks the service's fence right before the field is
        // touched (lock_cred_channel RunJob / AgentAskGo); a deny types nothing.
        *win32 = typeWin32;
        if (typeAnswer != UnlockStage::Injected) return typeAnswer;
        if (beforeCommit) beforeCommit(*this);
        ++fenceAsked;
        if (!stillValid || !stillValid()) return UnlockStage::RejectedStaleTopology;
        typed.push_back({session, std::u16string(units, units + count)});
        return typeAnswer;
      };
    }
    return p;
  }
};

struct Conn {
  sus::UnlockSession us;
  bool there = true;
  std::vector<uint8_t> last;
  Conn(uint32_t pid, uint32_t session) {
    us.caller.processId = pid;
    us.caller.processStartTime = 1000 + pid;
    us.caller.session = session;
    us.callerStillThere = [this] { return there; };
  }
  sus::Write writer() {
    return [this](const void* d, size_t n) { last.assign(static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n); };
  }
};

SecureUnlockChallengeResponse challenge(Conn& c, Os& os, uint32_t id, bool lockCred) {
  SecureUnlockChallengeRequest req{};
  req.requestId = id;
  req.clientSessionCookie = 0xC00C1E;
  req.kind = static_cast<uint16_t>(lockCred ? SecureUnlockKind::LockCredChallengeRequest : SecureUnlockKind::ChallengeRequest);
  const sus::UnlockPrimitives p = os.prims();
  if (lockCred) {
    sus::HandleLockCredChallengeRequest(c.us, req, p, c.writer());
  } else {
    sus::HandleChallengeRequest(c.us, req, p, c.writer());
  }
  SecureUnlockChallengeResponse r{};
  if (c.last.size() == sizeof(r)) std::memcpy(&r, c.last.data(), sizeof(r));
  return r;
}

enum class SealAs { Unlock, Prelogin, LockCred };

// The viewer's sealing of `password` for the challenge `ch`, under the purpose `as`.
SecureUnlockSealedRequest seal(const SecureUnlockChallengeResponse& ch, const std::u16string& password, SealAs as) {
  SecureUnlockSealedRequest s{};
  su::EcdhKeyPair key;
  uint8_t clientPub[su::kPubKeyBytes] = {};
  key.Generate();
  key.ExportPublic(clientPub);
  su::UnlockContext c{};
  c.protocolVersion = 1;
  c.hostId = ch.hostId;
  c.clientSessionCookie = ch.clientSessionCookie;
  std::memcpy(c.challengeId, ch.challengeId, su::kChallengeIdBytes);
  c.requestId = ch.requestId;
  c.requesterSession = ch.requesterSession;
  c.consoleSession = ch.consoleSession;
  c.lockGeneration = ch.lockGeneration;
  c.topologyGeneration = ch.topologyGeneration;
  c.issuedMs = ch.issuedMs;
  c.expiresMs = ch.expiresMs;
  c.accountId = ch.accountId;
  std::memcpy(c.hostPub, ch.hostPub, su::kPubKeyBytes);
  std::memcpy(c.clientPub, clientPub, su::kPubKeyBytes);
  const auto kdf = as == SealAs::LockCred   ? su::BuildLockCredKdfInfo(c)
                   : as == SealAs::Prelogin ? su::BuildPreloginCredKdfInfo(c)
                                            : su::BuildKdfInfo(c);
  const auto aad = as == SealAs::LockCred   ? su::BuildLockCredAad(c)
                   : as == SealAs::Prelogin ? su::BuildPreloginCredAad(c)
                                            : su::BuildAad(c);
  uint8_t aesKey[su::kAesKeyBytes] = {};
  su::DeriveAesKey(key, ch.hostPub, ch.salt, kdf.data(), kdf.size(), aesKey);
  uint8_t plain[su::kPlaintextBytes] = {};
  std::vector<uint16_t> units(password.begin(), password.end());
  su::PackPassword(units.data(), static_cast<uint16_t>(units.size()), plain);
  su::RandomBytes(s.nonce, su::kNonceBytes);
  su::AesGcmSeal(aesKey, s.nonce, aad.data(), aad.size(), plain, su::kPlaintextBytes, s.cipher, s.tag);
  s.requestId = ch.requestId;
  s.clientSessionCookie = ch.clientSessionCookie;
  std::memcpy(s.challengeId, ch.challengeId, sizeof(s.challengeId));
  std::memcpy(s.clientPub, clientPub, sizeof(s.clientPub));
  return s;
}

SecureUnlockResult sealed(Conn& c, Os& os, const SecureUnlockSealedRequest& s, uint32_t jobId = 1) {
  const sus::UnlockPrimitives p = os.prims();
  sus::HandleSealedRequest(c.us, s, jobId, p, c.writer());
  SecureUnlockResult r{};
  if (c.last.size() == sizeof(r)) std::memcpy(&r, c.last.data(), sizeof(r));
  return r;
}

SecureUnlockResult status(Conn& c, Os& os, uint32_t requestId, uint64_t cookie = 0xC00C1E) {
  SecureUnlockQuery q{};
  q.kind = static_cast<uint16_t>(SecureUnlockKind::StatusRequest);
  q.requestId = requestId;
  q.clientSessionCookie = cookie;
  const sus::UnlockPrimitives p = os.prims();
  sus::HandleStatusRequest(c.us, q, p, c.writer());
  SecureUnlockResult r{};
  if (c.last.size() == sizeof(r)) std::memcpy(&r, c.last.data(), sizeof(r));
  return r;
}

uint16_t st(UnlockStage s) { return static_cast<uint16_t>(s); }
std::string stage_of(const SecureUnlockResult& r) {
  return "stage=" + std::to_string(r.stage) + " terminal=" + std::to_string(r.terminal);
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the lock-screen credential in the service (lockscreen-cred r1)\n\n");

  std::printf("[capabilities]\n");
  {
    Os os;
    SecureUnlockQuery q{};
    q.requestId = 5;
    std::vector<uint8_t> out;
    sus::HandleCapabilities(q, os.prims(), [&](const void* d, size_t n) {
      out.assign(static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n);
    });
    SecureUnlockCapabilitiesResponse r{};
    if (out.size() == sizeof(r)) std::memcpy(&r, out.data(), sizeof(r));
    check("a service that can type into the lock screen lists LockCredV1",
          r.requestId == 5 && (r.features & remote60::native_poc::kSecureUnlockFeatureLockCredV1) != 0);
    Os off;
    off.offerLockCred = false;
    sus::HandleCapabilities(q, off.prims(), [&](const void* d, size_t n) {
      out.assign(static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n);
    });
    std::memcpy(&r, out.data(), sizeof(r));
    check("...and one that cannot lists nothing", r.features == 0);
  }

  std::printf("\n[who gets a lock-credential challenge]\n");
  {
    Os os;
    Conn a(101, 1);
    const SecureUnlockChallengeResponse c = challenge(a, os, 11, true);
    check("own session = console, ACTIVE, locked: issued, purpose LockCred, for that session",
          c.stage == st(UnlockStage::ChallengeIssued) && c.purpose == kUnlockPurposeLockCred && c.requesterSession == 1 &&
              c.consoleSession == 1 && c.accountId == 0xA1);
  }
  {
    Os os;
    Conn a(101, 1);
    const SecureUnlockChallengeResponse c = challenge(a, os, 12, false);
    check("the same ACTIVE locked console session asking the OLD unlock: still refused (reqActive kept)",
          c.stage == st(UnlockStage::RejectedPolicy) && c.purpose == kUnlockPurposeUnlock);
  }
  {
    Os os;
    Conn b(201, 2);  // locked, disconnected
    const SecureUnlockChallengeResponse cl = challenge(b, os, 13, true);
    const SecureUnlockChallengeResponse cu = challenge(b, os, 14, false);
    check("a DISCONNECTED locked session: no lock-credential challenge ...", cl.stage == st(UnlockStage::RejectedPolicy));
    check("... but its old unlock challenge is issued as before (purpose Unlock)",
          cu.stage == st(UnlockStage::ChallengeIssued) && cu.purpose == kUnlockPurposeUnlock);
  }
  struct Refusal {
    const char* what;
    void (*setup)(Os&);
  };
  const Refusal refusals[] = {
      {"not locked (the desktop is in use, or a UAC prompt is up)", [](Os& o) { o.locked[1] = false; }},
      {"lock state unknown (WTS could not say) -- not taken as either", [](Os& o) { o.locked.erase(1); }},
      {"connect state unknown", [](Os& o) { o.active.erase(1); }},
      {"the caller's session is not the console (console is 2)", [](Os& o) { o.console = 2; }},
      {"no console session", [](Os& o) { o.console = 0xffffffffu; }},
      {"the service cannot type into the lock screen", [](Os& o) { o.offerLockCred = false; }},
  };
  for (const Refusal& r : refusals) {
    Os os;
    r.setup(os);
    Conn a(101, 1);
    const SecureUnlockChallengeResponse c = challenge(a, os, 20, true);
    check((std::string("refused: ") + r.what).c_str(), c.stage == st(UnlockStage::RejectedPolicy));
  }
  {
    Os os;
    Conn none(0, sus::kNoSession);
    check("refused: a caller whose session could not be read", challenge(none, os, 21, true).stage == st(UnlockStage::RejectedPolicy));
    const bool logged = !os.log.empty() && os.log.back().find("reason=") != std::string::npos &&
                        os.log.back().find("pw") == std::string::npos;
    check("a refusal is one bounded line with its reason (no payload)", logged, os.log.empty() ? "" : os.log.back());
  }

  std::printf("\n[typed once, under its own purpose]\n");
  {
    Os os;
    Conn a(101, 1);
    const SecureUnlockChallengeResponse c = challenge(a, os, 31, true);
    const SecureUnlockResult r = sealed(a, os, seal(c, u"Lock-pw-가", SealAs::LockCred));
    check("LockCred-sealed: typed into session 1, exactly those units, once; Injected NOT terminal (it waits for the unlock)",
          r.stage == st(UnlockStage::Injected) && r.terminal == 0 && os.typed.size() == 1 && os.typed[0].session == 1 &&
              os.typed[0].text == u"Lock-pw-가" && os.wtsCalls == 0,
          stage_of(r));
    const SecureUnlockResult again = sealed(a, os, seal(c, u"Lock-pw-가", SealAs::LockCred), 2);
    check("a replay of the sealed request: answered with where it is, typed 0 more times",
          again.stage == st(UnlockStage::Injected) && os.typed.size() == 1, stage_of(again));
    const SecureUnlockResult s1 = status(a, os, 31);
    check("status while still locked: Injected, not terminal", s1.stage == st(UnlockStage::Injected) && s1.terminal == 0,
          stage_of(s1));
    os.locked[1] = false;  // the lock state alone, with no session event since the typing
    const SecureUnlockResult s2 = status(a, os, 31);
    check("unlocked but no session event since the typing: not counted as the unlock", s2.stage == st(UnlockStage::Injected),
          stage_of(s2));
    ++os.topo;  // the WTS unlock event
    const SecureUnlockResult s3 = status(a, os, 31);
    check("the unlock of that session after the typing: SessionUnlocked, terminal",
          s3.stage == st(UnlockStage::SessionUnlocked) && s3.terminal == 1, stage_of(s3));
    os.locked[1] = true;
    ++os.topo;
    const SecureUnlockResult s4 = status(a, os, 31);
    check("asked again later: the same cached answer", s4.stage == st(UnlockStage::SessionUnlocked) && s4.terminal == 1);
    check("...and still typed once", os.typed.size() == 1);
  }
  {
    Os os;
    Conn a(101, 1);
    const SecureUnlockChallengeResponse c = challenge(a, os, 32, true);
    (void)sealed(a, os, seal(c, u"pw", SealAs::LockCred));
    os.now += sus::kLockCredUnlockWaitMs + 1000;
    const SecureUnlockResult s = status(a, os, 32);
    check("no unlock within the wait: UnlockUnconfirmed, terminal (typed is not reported as unlocked)",
          s.stage == st(UnlockStage::UnlockUnconfirmed) && s.terminal == 1, stage_of(s));
  }
  {
    // Codex Q1 (a): while a typed credential waits for its unlock, a new request is refused (its context kept);
    // once it has an answer -- here the wait ran out -- a new one is issued.
    Os os;
    Conn a(101, 1);
    const SecureUnlockChallengeResponse c = challenge(a, os, 33, true);
    (void)sealed(a, os, seal(c, u"first", SealAs::LockCred));
    const SecureUnlockChallengeResponse again = challenge(a, os, 34, true);
    check("Q1: a new lock-screen request while the typed one still waits: refused (\"pending\"), nothing typed",
          again.stage == st(UnlockStage::RejectedPolicy) && os.typed.size() == 1 && !os.log.empty() &&
              os.log.back().find("reason=pending") != std::string::npos,
          os.log.empty() ? "" : os.log.back());
    const SecureUnlockResult still = status(a, os, 33);
    check("Q1: ...and the waiting one is untouched", still.stage == st(UnlockStage::Injected) && still.terminal == 0);
    os.now += sus::kLockCredUnlockWaitMs + 1000;
    (void)status(a, os, 33);  // answered: unconfirmed
    const SecureUnlockChallengeResponse next = challenge(a, os, 35, true);
    check("Q1: once it is answered, a new request is issued", next.stage == st(UnlockStage::ChallengeIssued));
  }
  struct Cross {
    const char* what;
    bool lockCredChallenge;
    SealAs as;
  };
  const Cross crosses[] = {
      {"a LockCred challenge, sealed under the Unlock purpose", true, SealAs::Unlock},
      {"a LockCred challenge, sealed under the PreloginCred purpose", true, SealAs::Prelogin},
      {"an Unlock challenge (disconnected session), sealed under the LockCred purpose", false, SealAs::LockCred},
  };
  for (const Cross& x : crosses) {
    Os os;
    Conn a(x.lockCredChallenge ? 101 : 201, x.lockCredChallenge ? 1 : 2);
    const SecureUnlockChallengeResponse c = challenge(a, os, 41, x.lockCredChallenge);
    const SecureUnlockResult r = sealed(a, os, seal(c, u"pw", x.as));
    check((std::string(x.what) + ": DecryptFailed, typed 0, WTSConnectSession 0").c_str(),
          c.stage == st(UnlockStage::ChallengeIssued) && r.stage == st(UnlockStage::DecryptFailed) && os.typed.empty() &&
              os.wtsCalls == 0,
          stage_of(r));
  }
  {
    Os os;
    Conn b(201, 2);
    const SecureUnlockChallengeResponse c = challenge(b, os, 42, false);
    const SecureUnlockResult r = sealed(b, os, seal(c, u"pw", SealAs::Unlock));
    check("control: the old unlock still runs WTSConnectSession once and types nothing",
          r.stage == st(UnlockStage::SessionUnlocked) && os.wtsCalls == 1 && os.typed.empty(), stage_of(r));
  }

  std::printf("\n[nothing typed when what it was issued for no longer holds]\n");
  struct Fence {
    const char* what;
    void (*between)(Os&, Conn&);
    uint16_t want;
  };
  const Fence fences[] = {
      {"a session event between challenge and seal", [](Os& o, Conn&) { ++o.topo; }, st(UnlockStage::RejectedStaleTopology)},
      {"the caller left the pipe", [](Os&, Conn& c) { c.there = false; }, st(UnlockStage::RejectedStaleTopology)},
      {"the session is no longer locked (same topology)", [](Os& o, Conn&) { o.locked[1] = false; },
       st(UnlockStage::RejectedStaleTopology)},
      {"the session is no longer active", [](Os& o, Conn&) { o.active[1] = false; }, st(UnlockStage::RejectedStaleTopology)},
      {"the console moved", [](Os& o, Conn&) { o.console = 2; }, st(UnlockStage::RejectedStaleTopology)},
      {"the challenge expired", [](Os& o, Conn&) { o.now += 60000; }, st(UnlockStage::Timeout)},
  };
  for (const Fence& f : fences) {
    Os os;
    Conn a(101, 1);
    const SecureUnlockChallengeResponse c = challenge(a, os, 51, true);
    f.between(os, a);
    const SecureUnlockResult r = sealed(a, os, seal(c, u"pw", SealAs::LockCred));
    check((std::string(f.what) + ": refused, typed 0").c_str(), r.stage == f.want && r.terminal == 1 && os.typed.empty(),
          stage_of(r));
  }

  std::printf("\n[what the agent said is the answer]\n");
  {
    Os os;
    os.typeAnswer = UnlockStage::TargetNotReady;
    Conn a(101, 1);
    const SecureUnlockChallengeResponse c = challenge(a, os, 61, true);
    const SecureUnlockResult r = sealed(a, os, seal(c, u"pw", SealAs::LockCred));
    check("the password field was not the target: TargetNotReady, terminal", r.stage == st(UnlockStage::TargetNotReady) &&
                                                                                 r.terminal == 1,
          stage_of(r));
    const SecureUnlockResult again = sealed(a, os, seal(c, u"pw", SealAs::LockCred), 2);
    check("...a replay is not typed again: the cached answer", again.stage == st(UnlockStage::TargetNotReady));
  }
  {
    Os os;
    os.typeAnswer = UnlockStage::InjectFailed;
    os.typeWin32 = 5;
    Conn a(101, 1);
    const SecureUnlockChallengeResponse c = challenge(a, os, 62, true);
    const SecureUnlockResult r = sealed(a, os, seal(c, u"pw", SealAs::LockCred));
    check("the typing failed: InjectFailed with its win32, terminal",
          r.stage == st(UnlockStage::InjectFailed) && r.win32Error == 5 && r.terminal == 1, stage_of(r));
  }
  {
    Os os;
    Conn a(101, 1);
    const SecureUnlockResult early = status(a, os, 71);
    check("status of a job that was never typed: RejectedPolicy, terminal",
          early.stage == st(UnlockStage::RejectedPolicy) && early.terminal == 1);
    const SecureUnlockChallengeResponse c = challenge(a, os, 72, true);
    (void)sealed(a, os, seal(c, u"pw", SealAs::LockCred));
    const SecureUnlockResult other = status(a, os, 73);
    check("status of another requestId: RejectedPolicy (not this job's state)", other.stage == st(UnlockStage::RejectedPolicy));
  }
  {
    // The log never carries what was typed.
    Os os;
    Conn a(101, 1);
    const SecureUnlockChallengeResponse c = challenge(a, os, 81, true);
    (void)sealed(a, os, seal(c, u"SECRET-81", SealAs::LockCred));
    bool leaked = false;
    for (const std::string& l : os.log) leaked = leaked || l.find("SECRET") != std::string::npos;
    check("no log line carries the password", !leaked && !os.log.empty());
  }

  std::printf("\n[r2 L2: the request must still hold at the commit -- else nothing is typed]\n");
  struct CommitFence {
    const char* what;
    std::function<void(Os&)> change;
  };
  const CommitFence commits[] = {
      {"a session event (unlock -> relock) while the agent waited", [](Os& o) { ++o.topo; }},
      {"the session was unlocked meanwhile (no event seen yet)", [](Os& o) { o.locked[1] = false; }},
      {"the session is no longer active", [](Os& o) { o.active[1] = false; }},
      {"the console moved", [](Os& o) { o.console = 2; }},
      {"another account is in the session", [](Os& o) { o.account[1] = 0xBEEF; }},
      {"the commit deadline passed (a slow child / target wait)", [](Os& o) { o.now += sus::kLockCredCommitMs + 1000; }},
      {"the service is stopping", [](Os& o) { o.runningFlag = false; }},
  };
  for (const CommitFence& f : commits) {
    Os os;
    os.beforeCommit = f.change;
    Conn a(101, 1);
    const SecureUnlockChallengeResponse c = challenge(a, os, 91, true);
    const SecureUnlockResult r = sealed(a, os, seal(c, u"pw", SealAs::LockCred));
    check((std::string("L2: ") + f.what + ": the fence was asked at the commit, NOTHING typed, RejectedStaleTopology").c_str(),
          os.fenceAsked == 1 && os.typed.empty() && r.stage == st(UnlockStage::RejectedStaleTopology) && r.terminal == 1,
          stage_of(r));
  }
  {
    Os os;
    Conn a(101, 1);
    a.us.callerStillThere = [&a] { return a.there; };
    os.beforeCommit = [&a](Os&) { a.there = false; };  // the host's control session ended: its pipe went
    const SecureUnlockChallengeResponse c = challenge(a, os, 92, true);
    const SecureUnlockResult r = sealed(a, os, seal(c, u"pw", SealAs::LockCred));
    check("L2: the caller (its control session) went away while the agent waited: nothing typed",
          os.typed.empty() && r.stage == st(UnlockStage::RejectedStaleTopology), stage_of(r));
  }
  {
    Os os;
    Conn a(101, 1);
    const SecureUnlockChallengeResponse c = challenge(a, os, 93, true);
    const SecureUnlockResult r = sealed(a, os, seal(c, u"pw", SealAs::LockCred));
    check("L2 control: nothing changed -> the fence allows, typed once", os.fenceAsked == 1 && os.typed.size() == 1 &&
                                                                            r.stage == st(UnlockStage::Injected),
          stage_of(r));
  }

  std::printf("\n[r2 L1: a result belongs to its control session]\n");
  {
    Os os;
    os.typeAnswer = UnlockStage::TargetNotReady;
    Conn a(101, 1);
    const SecureUnlockChallengeResponse c = challenge(a, os, 2, true);
    (void)sealed(a, os, seal(c, u"pw", SealAs::LockCred));
    const SecureUnlockResult mine = status(a, os, 2, 0xC00C1E);
    const SecureUnlockResult other = status(a, os, 2, 0xD00D);
    check("L1: the same requestId from ANOTHER control session gets nothing of this one's final result",
          mine.stage == st(UnlockStage::TargetNotReady) && other.stage == st(UnlockStage::RejectedPolicy),
          "mine " + stage_of(mine) + " other " + stage_of(other));
    os.typeAnswer = UnlockStage::Injected;
    SecureUnlockSealedRequest s = seal(c, u"pw", SealAs::LockCred);
    s.clientSessionCookie = 0xD00D;
    const SecureUnlockResult dup = sealed(a, os, s, 5);
    check("L1: ...nor its cached sealed answer (another session's sealed request is not a 'duplicate' of it)",
          dup.stage != st(UnlockStage::TargetNotReady) && os.typed.empty(), stage_of(dup));
  }

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

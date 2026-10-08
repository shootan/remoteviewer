// prelogin-ui r15 (R14-2): the sealed unlock is bound to the caller on ITS unlock-pipe connection.
//
// r14 served several secure-input clients at once, but the unlock path still read "the requester" from the
// last input connection: host A's unlock was issued for host B's session (or refused for B's state), and an
// input connection ending wiped the value for another one. Here the service's own decisions
// (secure_unlock_service.cpp) run with the real crypto (the viewer's sealing, as prelogin_cred_host_test
// does it) against stand-ins for the OS: sessions 2 and 3 locked/disconnected or not, the console 1, and
// WTSConnectSession recorded instead of called -- no session of this PC moves.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "sealed_unlock.hpp"
#include "secure_unlock_service.hpp"

namespace su = remote60::native_poc::sealed_unlock;
namespace sus = remote60::native_poc::sealed_unlock_service;
using remote60::native_poc::SecureUnlockChallengeRequest;
using remote60::native_poc::SecureUnlockChallengeResponse;
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

// The OS as the service sees it.
struct Os {
  std::map<uint32_t, bool> locked{{2, true}, {3, true}};
  std::map<uint32_t, bool> active{{2, false}, {3, false}};  // both disconnected (the unlock case)
  std::map<uint32_t, uint64_t> account{{2, 0xA2}, {3, 0xB3}};
  uint32_t console = 1;
  uint32_t topo = 7;
  uint64_t now = 100000;
  struct Call {
    uint32_t requester, console;
    std::wstring pw;
  };
  std::vector<Call> wts;
  sus::UnlockPrimitives prims() {
    sus::UnlockPrimitives p;
    p.sessionIsLocked = [this](uint32_t s, bool* k) { *k = locked.count(s) > 0; return *k && locked[s]; };
    p.sessionIsActive = [this](uint32_t s, bool* k) { *k = active.count(s) > 0; return active[s]; };
    p.consoleSession = [this] { return console; };
    p.accountIdOf = [this](uint32_t s, bool* k) { *k = account.count(s) > 0; return account[s]; };
    p.hostId = [] { return 0x4057ull; };
    p.topologyGeneration = [this] { return topo; };
    p.nowMs = [this] { return now += 600; };  // past the 500 ms issuance rate limit each time
    p.wtsConnect = [this](uint32_t r, uint32_t c, const wchar_t* pw, DWORD* gle) {
      wts.push_back({r, c, pw});
      *gle = 0;
      locked[r] = false;  // the reconnect unlocked it
      return true;
    };
    p.running = [] { return true; };
    p.sleep = [](uint32_t) {};
    return p;
  }
};

// One unlock-pipe connection: a caller, as the service read it from the pipe.
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

SecureUnlockChallengeResponse challenge(Conn& c, Os& os, uint32_t id) {
  SecureUnlockChallengeRequest req{};
  req.requestId = id;
  req.clientSessionCookie = 0xC00C1E;
  const sus::UnlockPrimitives p = os.prims();
  sus::HandleChallengeRequest(c.us, req, p, c.writer());
  SecureUnlockChallengeResponse r{};
  if (c.last.size() == sizeof(r)) std::memcpy(&r, c.last.data(), sizeof(r));
  return r;
}

// The viewer's sealing of `password` for the challenge `ch` (as viewer_unlock does it).
SecureUnlockSealedRequest seal(const SecureUnlockChallengeResponse& ch, const std::u16string& password) {
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
  const auto kdf = su::BuildKdfInfo(c);
  const auto aad = su::BuildAad(c);
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

SecureUnlockResult sealed(Conn& c, Os& os, const SecureUnlockSealedRequest& s) {
  const sus::UnlockPrimitives p = os.prims();
  sus::HandleSealedRequest(c.us, s, 1, p, c.writer());
  SecureUnlockResult r{};
  if (c.last.size() == sizeof(r)) std::memcpy(&r, c.last.data(), sizeof(r));
  return r;
}
uint16_t st(UnlockStage s) { return static_cast<uint16_t>(s); }
}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--child") == 0) {  // r16: a caller process that lives a moment
    Sleep(400);
    return 0;
  }
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the sealed unlock, bound to its own caller (prelogin-ui r15, R14-2)\n\n");
  {
    // A (session 2) and B (session 3) both connected; B connected last. A unlocks.
    Os os;
    Conn a(201, 2), b(301, 3);
    const SecureUnlockChallengeResponse ca = challenge(a, os, 11);
    check("R14-2: A's challenge is issued for A's session and A's account -- not the last-connected B's",
          ca.stage == st(UnlockStage::ChallengeIssued) && ca.requesterSession == 2 && ca.accountId == 0xA2,
          "session " + std::to_string(ca.requesterSession));
    const SecureUnlockResult ra = sealed(a, os, seal(ca, u"pw-of-A"));
    check("R14-2: ...and A's password reconnects A's session (WTSConnectSession(2 -> console 1), once)",
          ra.stage == st(UnlockStage::SessionUnlocked) && os.wts.size() == 1 && os.wts[0].requester == 2 &&
              os.wts[0].console == 1 && os.wts[0].pw == L"pw-of-A",
          std::to_string(os.wts.size()) + " calls");
  }
  {
    // B is not eligible (active), A is: A's request does not depend on B.
    Os os;
    os.active[3] = true;
    Conn a(201, 2), b(301, 3);
    const SecureUnlockChallengeResponse cb = challenge(b, os, 21);
    const SecureUnlockChallengeResponse ca = challenge(a, os, 22);
    check("R14-2: B active (not eligible): B is refused, A is issued -- each by its own state",
          cb.stage == st(UnlockStage::RejectedPolicy) && ca.stage == st(UnlockStage::ChallengeIssued) &&
              ca.requesterSession == 2);
  }
  {
    // Two connections from the same Windows session; the first one ends. The second still works.
    Os os;
    Conn a1(201, 2);
    {
      Conn a2(202, 2);
      const SecureUnlockChallengeResponse c2 = challenge(a2, os, 31);
      check("same session, another instance: its own challenge", c2.stage == st(UnlockStage::ChallengeIssued));
    }  // a2's connection is gone
    const SecureUnlockChallengeResponse c1 = challenge(a1, os, 32);
    const SecureUnlockResult r1 = sealed(a1, os, seal(c1, u"pw"));
    check("R14-2: ...after the other connection of that session ended, A still unlocks (nothing shared was wiped)",
          c1.stage == st(UnlockStage::ChallengeIssued) && r1.stage == st(UnlockStage::SessionUnlocked) &&
              os.wts.size() == 1 && os.wts[0].requester == 2);
  }
  {
    // The caller process is no longer on the pipe at the irreversible step.
    Os os;
    Conn a(201, 2);
    const SecureUnlockChallengeResponse ca = challenge(a, os, 41);
    a.there = false;
    const SecureUnlockResult r = sealed(a, os, seal(ca, u"pw"));
    check("R14-2: the caller left the pipe before the final step: refused, WTSConnectSession never called",
          r.stage == st(UnlockStage::RejectedStaleTopology) && os.wts.empty());
  }
  {
    // A real session event between challenge and seal still fences it.
    Os os;
    Conn a(201, 2);
    const SecureUnlockChallengeResponse ca = challenge(a, os, 51);
    ++os.topo;
    const SecureUnlockResult r = sealed(a, os, seal(ca, u"pw"));
    check("a real topology change in between still refuses (the WTS fence is kept)",
          r.stage == st(UnlockStage::RejectedStaleTopology) && os.wts.empty());
  }
  {
    Os os;
    Conn none(0, sus::kNoSession);
    check("a caller whose session could not be read is refused",
          challenge(none, os, 61).stage == st(UnlockStage::RejectedPolicy));
  }
  {
    // r16 R15-3: the caller's liveness from the handle held since the connection -- a process that has exited
    // is gone even while its handle (here: ours) keeps its metadata readable.
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(self) + L"\" --child";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const bool started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                                        &si, &pi) != FALSE;
    Os os;
    Conn a(started ? pi.dwProcessId : 0, 2);
    a.us.caller.process = started ? pi.hProcess : nullptr;
    a.us.callerStillThere = [&a] { return sus::CallerProcessAlive(a.us.caller.process); };
    const bool aliveAtFirst = sus::CallerProcessAlive(a.us.caller.process);
    const SecureUnlockChallengeResponse ca = challenge(a, os, 71);
    if (started) WaitForSingleObject(pi.hProcess, 5000);  // the caller exits; the handle stays open
    DWORD code = 0;
    const bool metadataReadable = started && GetExitCodeProcess(pi.hProcess, &code);
    const SecureUnlockResult r = sealed(a, os, seal(ca, u"pw"));
    check("R15-3: the caller process exited before the final step (its handle still open, metadata readable): "
          "refused, WTSConnectSession 0 times",
          started && aliveAtFirst && metadataReadable && r.stage == st(UnlockStage::RejectedStaleTopology) && os.wts.empty(),
          std::to_string(os.wts.size()) + " calls");
    if (started) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    check("R15-3: no handle: not alive", !sus::CallerProcessAlive(nullptr));
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

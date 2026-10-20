// prelogin-ui r3 (item 5): the pre-login worker's end of "Windows 로그인" (PreloginCredReceiver).
//
// The product class, with its two OS edges replaced: the typing (records the units instead of
// SendInput) and the "is the input target the logon UI" question. The viewer's half is sealed here
// with the same primitives and the same challenge mapping (unlock_wire.hpp) the viewer uses.
//
//   * prelogin purpose: opened, typed once, exactly the units sent; a retransmit types nothing more
//   * an UNLOCK-purpose blob (the lock-screen unlock's sealing) is refused at the tag: nothing typed
//   * the target is not the logon UI: RejectedPolicy after the wait, nothing typed
//   * the target turns up within the wait: typed
//   * stale request id / other session cookie / a replay after use / the rate limit
//   * the control thread is not held while the worker waits for the target
//   * the typing fails: InternalError with its win32 code

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "prelogin_cred_host.hpp"
#include "prelogin_logon_desktop.hpp"
#include "unlock_wire.hpp"

using namespace remote60::native_poc;
namespace su = remote60::native_poc::sealed_unlock;
using remote60::native_poc::prelogin::PreloginCredReceiver;

namespace {

int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
// The detail read AFTER the outcome expression ran (arguments are evaluated in no fixed order).
template <typename F, typename = decltype(std::declval<F>()())>
void check(const char* what, bool ok, F detail) {
  check(what, ok, std::string(detail()));
}

struct Seams {
  std::mutex mu;
  std::vector<std::vector<uint16_t>> typed;
  std::atomic<uint64_t> targetId{7};       // the logon UI's identity; 0 = the input is not at it
  std::atomic<uint64_t> environmentId{1};  // console session + input desktop
  std::atomic<bool> failTyping{false};
  std::atomic<int> targetCalls{0};
  int moveTargetAtCall = 0;            // from this target() call on, another window is in front
  std::function<void(int call)> onTarget;  // r7 H5: something happens at a given target() call
  std::atomic<bool> typedAfterEnd{false};   // r7 H5: a typing began after EndSession had returned
  std::atomic<bool> endReturned{false};
  bool moveTargetDuringTyping = false;  // the typing itself moves it
  PreloginCredReceiver::InjectFn inject() {
    return [this](const uint16_t* u, uint16_t n, uint32_t* err) {
      if (failTyping.load()) {
        *err = 5;
        return false;
      }
      if (moveTargetDuringTyping) targetId = 8;
      if (endReturned.load()) typedAfterEnd = true;
      std::lock_guard<std::mutex> l(mu);
      typed.emplace_back(u, u + n);
      return true;
    };
  }
  PreloginCredReceiver::TargetFn target() {
    return [this]() -> uint64_t {
      const int call = ++targetCalls;
      if (onTarget) onTarget(call);
      if (moveTargetAtCall != 0 && call >= moveTargetAtCall) return 9;
      return targetId.load();
    };
  }
  PreloginCredReceiver::EnvironmentFn environment() {
    return [this]() -> uint64_t { return environmentId.load(); };
  }
  size_t count() {
    std::lock_guard<std::mutex> l(mu);
    return typed.size();
  }
};

ControlUnlockChallengeRequestMessage challenge_req(uint32_t id) {
  ControlUnlockChallengeRequestMessage r{};
  r.requestId = id;
  return r;
}

// The viewer's sealing (viewer_unlock.cpp sealed_exchange), for either purpose.
ControlUnlockSealedRequestMessage seal(const ControlUnlockChallengeMessage& ch, const std::vector<uint16_t>& units,
                                       bool prelogin) {
  ControlUnlockSealedRequestMessage s{};
  su::EcdhKeyPair key;
  uint8_t clientPub[su::kPubKeyBytes] = {};
  key.Generate();
  key.ExportPublic(clientPub);
  const su::UnlockContext c = ContextFromChallenge(ch, clientPub);
  const auto kdf = prelogin ? su::BuildPreloginCredKdfInfo(c) : su::BuildKdfInfo(c);
  const auto aad = prelogin ? su::BuildPreloginCredAad(c) : su::BuildAad(c);
  uint8_t aesKey[su::kAesKeyBytes] = {};
  su::DeriveAesKey(key, ch.hostPub, ch.salt, kdf.data(), kdf.size(), aesKey);
  uint8_t plain[su::kPlaintextBytes] = {};
  su::PackPassword(units.data(), static_cast<uint16_t>(units.size()), plain);
  su::RandomBytes(s.nonce, su::kNonceBytes);
  su::AesGcmSeal(aesKey, s.nonce, aad.data(), aad.size(), plain, su::kPlaintextBytes, s.cipher, s.tag);
  s.requestId = ch.requestId;
  std::memcpy(s.challengeId, ch.challengeId, sizeof(s.challengeId));
  std::memcpy(s.clientPub, clientPub, sizeof(s.clientPub));
  return s;
}

bool wait_result(PreloginCredReceiver& r, uint32_t id, ControlUnlockStatusResultMessage* out, int ms = 5000) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    if (r.PollResult(id, out)) return true;
    Sleep(20);
  }
  return r.PollResult(id, out);
}

const char* stage_word(uint16_t s) {
  switch (static_cast<UnlockStage>(s)) {
    case UnlockStage::Injected: return "Injected";
    case UnlockStage::DecryptFailed: return "DecryptFailed";
    case UnlockStage::RejectedPolicy: return "RejectedPolicy";
    case UnlockStage::RejectedStaleTopology: return "RejectedStaleTopology";
    case UnlockStage::Timeout: return "Timeout";
    case UnlockStage::InternalError: return "InternalError";
    case UnlockStage::ChallengeIssued: return "ChallengeIssued";
    default: return "other";
  }
}

void next_challenge_window() { Sleep(520); }  // the receiver's 500 ms rate limit

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("prelogin credential receiver (prelogin-ui r3, item 5)\n\n");
  const std::vector<uint16_t> secret = {L'T', L'e', L's', L't', 0x00E4, 0xAC00, L'9', L'!', L' ', L'"'};
  const uint64_t cookie = 0xC00C1E5ull;

  {
    std::printf("--- the prelogin purpose: opened, typed once, exactly ---\n");
    Seams seams;
    PreloginCredReceiver r(seams.inject(), seams.target(), seams.environment(), 0x1234, 600);
    std::vector<std::string> lines;
    r.onResult = [&](uint32_t id, uint16_t stage, uint32_t w) {
      lines.push_back(std::to_string(id) + " " + std::to_string(stage) + " " + std::to_string(w));
    };
    const auto ch = r.Challenge(challenge_req(11), cookie);
    check("the challenge is issued", ch.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued), stage_word(ch.status));
    auto req = seal(ch, secret, true);
    const auto t0 = std::chrono::steady_clock::now();
    r.Sealed(req, cookie);
    const auto heldMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    ControlUnlockStatusResultMessage res{};
    check("the result is Injected (typed)", wait_result(r, 11, &res) && res.stage == static_cast<uint16_t>(UnlockStage::Injected),
          [&] { return std::string(stage_word(res.stage)); });
    check("...terminal, no win32", res.terminal == 1 && res.win32Error == 0);
    check("EXACTLY the units sent were typed, once", seams.count() == 1 && seams.typed[0] == secret,
          std::to_string(seams.count()) + " typings");
    check("the control thread was not held for the typing", heldMs < 200, std::to_string(heldMs) + " ms");
    r.Sealed(req, cookie);  // the viewer retransmits
    Sleep(200);
    check("a retransmit of the same request types nothing more and answers the same",
          seams.count() == 1 && r.PollResult(11, &res) && res.stage == static_cast<uint16_t>(UnlockStage::Injected));
    req.requestId = 12;
    r.Sealed(req, cookie);
    check("the same blob again under another request id: refused (no challenge for it), nothing typed",
          wait_result(r, 12, &res, 500) && res.stage == static_cast<uint16_t>(UnlockStage::RejectedStaleTopology) &&
              seams.count() == 1,
          [&] { return std::string(stage_word(res.stage)); });
    check("the result line names request and stage only", lines.size() == 2 && lines[0] == "11 8 0", lines.empty() ? "" : lines[0]);
    check("injections() counts the one typing", r.injections() == 1);
  }

  {
    // r16 (scope cut, user's choice A): the screen saver is NOT served. A worker whose input desktop becomes
    // the screen saver (or Default) leaves (exit 14) and the service starts a NEW worker -- a new receiver
    // with nothing outstanding. A challenge the old worker issued is not honoured by the new one.
    std::printf("\n--- a new worker after the desktop changed: the old challenge is not honoured ---\n");
    Seams seams;
    ControlUnlockChallengeMessage oldCh{};
    {
      PreloginCredReceiver before(seams.inject(), seams.target(), seams.environment(), 0x1234, 600);
      oldCh = before.Challenge(challenge_req(31), cookie);
    }  // the worker left (exit 14)
    PreloginCredReceiver after(seams.inject(), seams.target(), seams.environment(), 0x1234, 600);
    after.Sealed(seal(oldCh, secret, true), cookie);
    ControlUnlockStatusResultMessage res{};
    check("r16: the old worker's challenge, sealed to the new worker: refused, nothing typed",
          oldCh.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued) && wait_result(after, 31, &res, 1000) &&
              res.stage != static_cast<uint16_t>(UnlockStage::Injected) && seams.count() == 0,
          [&] { return std::string(stage_word(res.stage)); });
    const auto fresh = after.Challenge(challenge_req(32), cookie);
    after.Sealed(seal(fresh, secret, true), cookie);
    check("r16: ...a challenge the new worker issued goes through (typed once)",
          wait_result(after, 32, &res) && res.stage == static_cast<uint16_t>(UnlockStage::Injected) && seams.count() == 1,
          [&] { return std::string(stage_word(res.stage)); });
  }

  {
    // r17 R16-1: ONE worker, not ended: its control path sees the screen saver right before an input
    // (PreloginInputRefused, what host_control_session calls), and the desktop is back at the logon one
    // before anything else looks. The worker has left for good (the leave handler ran -- the product's exits
    // 14); a challenge it issued BEFORE is not used after, and none is issued any more.
    std::printf("\n--- the control path saw another desktop: the same worker refuses its old challenge ---\n");
    namespace pl = remote60::native_poc::prelogin;
    pl::PreloginLeftLatch().store(false);
    std::vector<std::wstring> leaves;
    pl::PreloginLeaveHandler() = [&leaves](const std::wstring& n) { leaves.push_back(n); };
    Seams seams;
    PreloginCredReceiver w(seams.inject(), seams.target(), seams.environment(), 0x1234, 600);
    w.stillServing = [] { return !pl::PreloginLeftLatch().load(); };  // as host_control_session binds it
    const auto c = w.Challenge(challenge_req(41), cookie);
    const bool refusedOnSaver = pl::PreloginInputRefused(L"Screen-saver", L"Winlogon");  // the control path's look
    const bool refusedBack = pl::PreloginInputRefused(L"Winlogon", L"Winlogon");          // back at the logon desktop
    w.Sealed(seal(c, secret, true), cookie);
    ControlUnlockStatusResultMessage res{};
    check("r17 R16-1: control saw the screen saver: that input refused, the worker LEFT (leave handler once), and "
          "back at Winlogon it is still refused",
          refusedOnSaver && refusedBack && leaves.size() == 1 && leaves[0] == L"Screen-saver" && pl::PreloginLeftLatch().load(),
          std::to_string(leaves.size()) + " leaves");
    check("r17 R16-1: ...the challenge issued before is refused at Sealed, nothing typed",
          c.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued) && wait_result(w, 41, &res, 1000) &&
              res.stage == static_cast<uint16_t>(UnlockStage::RejectedStaleTopology) && seams.count() == 0,
          [&] { return std::string(stage_word(res.stage)); });
    next_challenge_window();
    check("r17 R16-1: ...and no new challenge is issued by the worker that left",
          w.Challenge(challenge_req(42), cookie).status == static_cast<uint16_t>(UnlockStage::RejectedPolicy));
    pl::PreloginLeftLatch().store(false);  // the rest of this test is a fresh worker
    pl::PreloginLeaveHandler() = nullptr;
  }

  {
    // r18 R17-1: neither the watch nor the control path looks; only the credential's own target lookup does
    // (ProductCredTargetWith, the product's target with its desktop name and logon-UI field stood in). Its
    // first look finds the screen saver, the next one Winlogon with the password field. The first look is the
    // worker's permanent leave: the pending credential is never typed.
    std::printf("\n--- the credential's target lookup saw another desktop: the pending credential is not typed ---\n");
    namespace pl = remote60::native_poc::prelogin;
    pl::PreloginLeftLatch().store(false);
    std::vector<std::wstring> leaves;
    pl::PreloginLeaveHandler() = [&leaves](const std::wstring& n) { leaves.push_back(n); };
    std::atomic<int> looks{0};
    auto desktop = [&looks]() -> std::wstring { return looks.fetch_add(1) == 0 ? L"Screen-saver" : L"Winlogon"; };
    Seams seams;
    PreloginCredReceiver w(seams.inject(), pl::ProductCredTargetWith(desktop, [] { return uint64_t{42}; }, L"Winlogon"),
                           seams.environment(), 0x1234, 1500);
    w.stillServing = [] { return !pl::PreloginLeftLatch().load(); };
    const auto c = w.Challenge(challenge_req(51), cookie);
    w.Sealed(seal(c, secret, true), cookie);
    ControlUnlockStatusResultMessage res{};
    const bool done = wait_result(w, 51, &res, 4000);
    check("r18 R17-1: the target lookup saw the screen saver, then Winlogon with the field: the worker LEFT (once) "
          "and the pending credential was NOT typed",
          done && res.stage != static_cast<uint16_t>(UnlockStage::Injected) && seams.count() == 0 && leaves.size() == 1 &&
              leaves[0] == L"Screen-saver",
          [&] { return std::string(stage_word(res.stage)) + ", " + std::to_string(leaves.size()) + " leaves, " +
                       std::to_string(seams.count()) + " typed"; });
    pl::PreloginLeftLatch().store(false);
    leaves.clear();
    std::atomic<int> waits{0};
    auto field = [&waits]() -> uint64_t { return waits.fetch_add(1) < 3 ? 0 : 42; };  // the field appears later
    Seams s2;
    PreloginCredReceiver w2(s2.inject(), pl::ProductCredTargetWith([] { return std::wstring(L"Winlogon"); }, field, L"Winlogon"),
                            s2.environment(), 0x1234, 1500);
    w2.stillServing = [] { return !pl::PreloginLeftLatch().load(); };
    next_challenge_window();
    const auto c2 = w2.Challenge(challenge_req(52), cookie);
    w2.Sealed(seal(c2, secret, true), cookie);
    check("r18 R17-1: control -- on Winlogon with the password field not there YET: the ordinary wait, then typed once, "
          "no leave",
          wait_result(w2, 52, &res, 4000) && res.stage == static_cast<uint16_t>(UnlockStage::Injected) && s2.count() == 1 &&
              leaves.empty() && !pl::PreloginLeftLatch().load(),
          [&] { return std::string(stage_word(res.stage)); });
    const auto env = pl::ProductCredEnvironmentWith([] { return std::wstring(L"Screen-saver"); }, L"Winlogon");
    (void)env();
    check("r18 R17-1: the environment lookup seeing another desktop is the same leave", pl::PreloginLeftLatch().load() &&
                                                                                           leaves.size() == 1);
    pl::PreloginLeftLatch().store(false);
    pl::PreloginLeaveHandler() = nullptr;
  }

  {
    std::printf("\n--- an UNLOCK-purpose blob (the lock-screen unlock's sealing) ---\n");
    Seams seams;
    PreloginCredReceiver r(seams.inject(), seams.target(), seams.environment(), 0x1234, 600);
    const auto ch = r.Challenge(challenge_req(21), cookie);
    r.Sealed(seal(ch, secret, false), cookie);
    ControlUnlockStatusResultMessage res{};
    check("REFUSED at the tag: DecryptFailed", wait_result(r, 21, &res) && res.stage == static_cast<uint16_t>(UnlockStage::DecryptFailed),
          [&] { return std::string(stage_word(res.stage)); });
    Sleep(200);
    check("...and NOTHING was typed", seams.count() == 0);
    next_challenge_window();
    const auto ch2 = r.Challenge(challenge_req(22), cookie);
    r.Sealed(seal(ch2, secret, true), cookie);
    check("a fresh prelogin-purpose credential after it still goes through",
          wait_result(r, 22, &res) && res.stage == static_cast<uint16_t>(UnlockStage::Injected) && seams.count() == 1);
  }

  {
    std::printf("\n--- the input target is not the logon UI ---\n");
    Seams seams;
    seams.targetId = 0;
    PreloginCredReceiver r(seams.inject(), seams.target(), seams.environment(), 0x1234, 500);
    const auto ch = r.Challenge(challenge_req(31), cookie);
    const auto t0 = std::chrono::steady_clock::now();
    r.Sealed(seal(ch, secret, true), cookie);
    const auto heldMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    check("the control thread is not held while the worker waits", heldMs < 200, std::to_string(heldMs) + " ms");
    ControlUnlockStatusResultMessage res{};
    check("no result while it waits", !r.PollResult(31, &res));
    check("RejectedPolicy after the wait", wait_result(r, 31, &res) && res.stage == static_cast<uint16_t>(UnlockStage::RejectedPolicy),
          [&] { return std::string(stage_word(res.stage)); });
    check("...and NOTHING was typed", seams.count() == 0);

    std::printf("\n--- the target turns up within the wait ---\n");
    next_challenge_window();
    const auto ch2 = r.Challenge(challenge_req(32), cookie);
    r.Sealed(seal(ch2, secret, true), cookie);
    Sleep(250);
    seams.targetId = 7;
    check("typed once it is there", wait_result(r, 32, &res) && res.stage == static_cast<uint16_t>(UnlockStage::Injected) &&
                                        seams.count() == 1 && seams.typed[0] == secret,
          [&] { return std::string(stage_word(res.stage)); });
  }

  {
    std::printf("\n--- binding: request id, session cookie, rate limit, expiry of use ---\n");
    Seams seams;
    PreloginCredReceiver r(seams.inject(), seams.target(), seams.environment(), 0x1234, 300);
    const auto ch = r.Challenge(challenge_req(41), cookie);
    const auto again = r.Challenge(challenge_req(42), cookie);
    check("a second challenge inside 500 ms is refused (RejectedPolicy)",
          again.status == static_cast<uint16_t>(UnlockStage::RejectedPolicy), stage_word(again.status));
    auto req = seal(ch, secret, true);
    r.Sealed(req, cookie + 1);
    ControlUnlockStatusResultMessage res{};
    check("another control session's cookie: refused, nothing typed",
          wait_result(r, 41, &res, 500) && res.stage == static_cast<uint16_t>(UnlockStage::RejectedStaleTopology) && seams.count() == 0,
          [&] { return std::string(stage_word(res.stage)); });
    next_challenge_window();
    const auto ch3 = r.Challenge(challenge_req(43), cookie);
    auto stale = seal(ch3, secret, true);
    stale.requestId = 99;
    r.Sealed(stale, cookie);
    check("a request id the challenge was not issued for: refused",
          wait_result(r, 99, &res, 500) && res.stage == static_cast<uint16_t>(UnlockStage::RejectedStaleTopology) && seams.count() == 0);
    auto tampered = seal(ch3, secret, true);
    tampered.cipher[3] ^= 0x01;
    r.Sealed(tampered, cookie);
    check("a tampered ciphertext: DecryptFailed, nothing typed",
          wait_result(r, 43, &res, 500) && res.stage == static_cast<uint16_t>(UnlockStage::DecryptFailed) && seams.count() == 0);
  }

  {
    std::printf("\n--- the typing fails ---\n");
    Seams seams;
    seams.failTyping = true;
    PreloginCredReceiver r(seams.inject(), seams.target(), seams.environment(), 0x1234, 300);
    const auto ch = r.Challenge(challenge_req(51), cookie);
    r.Sealed(seal(ch, secret, true), cookie);
    ControlUnlockStatusResultMessage res{};
    check("InternalError with the OS's code", wait_result(r, 51, &res) &&
                                                  res.stage == static_cast<uint16_t>(UnlockStage::InternalError) && res.win32Error == 5,
          [&] { return std::string(stage_word(res.stage)); });
    check("...and it is not counted as typed", r.injections() == 0);
  }

  {
    std::printf("\n--- r5 G7: what the password was opened for must still hold when it is typed ---\n");
    // Each case: the target is NOT there when the password arrives, something changes while the worker
    // waits, then the target appears -- and nothing may be typed.
    auto run_case = [&](const char* what, const std::function<void(PreloginCredReceiver&, Seams&)>& meanwhile, UnlockStage want,
                        uint32_t lifetimeMs = 30000) {
      Seams seams;
      seams.targetId = 0;
      PreloginCredReceiver r(seams.inject(), seams.target(), seams.environment(), 0x1234, 2000, lifetimeMs);
      const auto ch = r.Challenge(challenge_req(61), cookie);
      r.Sealed(seal(ch, secret, true), cookie);
      Sleep(250);
      meanwhile(r, seams);
      seams.targetId = 7;  // the logon UI shows up afterwards
      ControlUnlockStatusResultMessage res{};
      const bool got = wait_result(r, 61, &res, 4000);
      Sleep(150);
      check(what, got && res.stage == static_cast<uint16_t>(want) && seams.count() == (want == UnlockStage::Injected ? 1u : 0u),
            [&] { return std::string(stage_word(res.stage)) + ", " + std::to_string(seams.count()) + " typed"; });
    };
    run_case("its control session ended while it waited: dropped, nothing typed",
             [&](PreloginCredReceiver& r, Seams&) { r.EndSession(cookie); }, UnlockStage::RejectedStaleTopology);
    run_case("another control session ending does not touch it (then the target comes: typed)",
             [&](PreloginCredReceiver& r, Seams& s) {
               r.EndSession(cookie + 1);
               (void)s;
             },
             UnlockStage::Injected);
    run_case("a new challenge while it waited: dropped, nothing typed",
             [&](PreloginCredReceiver& r, Seams&) {
               Sleep(400);  // past the challenge rate limit
               r.Challenge(challenge_req(62), cookie);
             },
             UnlockStage::RejectedStaleTopology);
    run_case("the console session / input desktop changed while it waited: dropped, nothing typed",
             [&](PreloginCredReceiver&, Seams& s) { s.environmentId = 99; }, UnlockStage::RejectedStaleTopology);
    run_case("its challenge expired while it waited: Timeout, nothing typed",
             [&](PreloginCredReceiver&, Seams&) { Sleep(600); }, UnlockStage::Timeout, /*lifetimeMs=*/700);
  }
  {
    std::printf("\n--- r5 G7: the target is identified, not just recognised ---\n");
    Seams seams;
    seams.moveTargetAtCall = 2;  // found (call 1), then another logon UI window by the last look (call 2)
    PreloginCredReceiver r(seams.inject(), seams.target(), seams.environment(), 0x1234, 600);
    const auto ch = r.Challenge(challenge_req(71), cookie);
    r.Sealed(seal(ch, secret, true), cookie);
    ControlUnlockStatusResultMessage res{};
    const bool got = wait_result(r, 71, &res);
    check("the target found is not the one in front at the last look: nothing typed (RejectedPolicy)",
          got && res.stage == static_cast<uint16_t>(UnlockStage::RejectedPolicy) && seams.count() == 0,
          [&] { return std::string(stage_word(res.stage)); });
    Seams moved;
    moved.moveTargetDuringTyping = true;
    PreloginCredReceiver r2(moved.inject(), moved.target(), moved.environment(), 0x1234, 600);
    const auto ch2 = r2.Challenge(challenge_req(72), cookie);
    r2.Sealed(seal(ch2, secret, true), cookie);
    ControlUnlockStatusResultMessage res2{};
    const bool got2 = wait_result(r2, 72, &res2);
    check("the target changed DURING the typing: reported as a failure (InternalError 1400), not as \"typed\"",
          got2 && res2.stage == static_cast<uint16_t>(UnlockStage::InternalError) && res2.win32Error == ERROR_INVALID_WINDOW_HANDLE,
          [&] { return std::string(stage_word(res2.stage)) + " " + std::to_string(res2.win32Error); });
    check("...while the typing that did happen is still counted (the units went; the result says it went wrong)",
          r2.injections() == 1);
  }

  {
    std::printf("\n--- r7 H5: the last look and the typing are one step against the session's end ---\n");
    // The session ends exactly at the final look (the second target() call: found, then taken again). The
    // end runs on another thread; the final look waits up to 300 ms for it to return. Before r7 it returned
    // at once (it only set a flag the look had already passed) and the password was typed AFTER it -- for
    // a session that had ended. Now the end waits for the committed step, so no typing begins after it.
    Seams seams;
    std::thread ender;
    PreloginCredReceiver* receiver = nullptr;
    seams.onTarget = [&](int call) {
      if (call != 2) return;
      ender = std::thread([&] {
        receiver->EndSession(cookie);
        seams.endReturned = true;
      });
      const ULONGLONG until = GetTickCount64() + 300;
      while (!seams.endReturned.load() && GetTickCount64() < until) Sleep(5);
    };
    {
      PreloginCredReceiver r(seams.inject(), seams.target(), seams.environment(), 0x1234, 600);
      receiver = &r;
      const auto ch = r.Challenge(challenge_req(81), cookie);
      r.Sealed(seal(ch, secret, true), cookie);
      ControlUnlockStatusResultMessage res{};
      wait_result(r, 81, &res);
      if (ender.joinable()) ender.join();
      check("H5: the session ending at the final look: no typing begins after the end returned",
            seams.targetCalls.load() >= 2 && !seams.typedAfterEnd.load(),
            [&] { return std::to_string(seams.count()) + " typed, " + std::string(stage_word(res.stage)); });
      check("...and once it has returned, the receiver reports no further typing", r.injections() <= 1);
    }
    Seams first;
    PreloginCredReceiver* r2p = nullptr;
    first.onTarget = [&](int call) {
      if (call == 1) r2p->EndSession(cookie);  // ended (and returned) before the committed step
    };
    PreloginCredReceiver r2(first.inject(), first.target(), first.environment(), 0x1234, 600);
    r2p = &r2;
    const auto ch2 = r2.Challenge(challenge_req(82), cookie);
    r2.Sealed(seal(ch2, secret, true), cookie);
    ControlUnlockStatusResultMessage res2{};
    const bool got2 = wait_result(r2, 82, &res2);
    check("H5: the session ended just before the final step (target already found): nothing typed, RejectedStaleTopology",
          got2 && res2.stage == static_cast<uint16_t>(UnlockStage::RejectedStaleTopology) && first.count() == 0,
          [&] { return std::string(stage_word(res2.stage)) + ", " + std::to_string(first.count()) + " typed"; });
  }

  {
    std::printf("\n--- the product's target question, here ---\n");
    // This process is not LogonUI.exe, so the product's answer must be no -- whatever is in front.
    const auto productTarget = remote60::native_poc::prelogin::ProductCredTarget();
    check("ProductCredTarget(): the foreground here is not the logon UI", productTarget() == 0);
  }

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

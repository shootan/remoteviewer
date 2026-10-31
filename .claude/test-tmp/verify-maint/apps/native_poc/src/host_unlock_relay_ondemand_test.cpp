// lockscreen-cred r1/r2 (Codex Q2, L1, L5): the host's unlock relay against a fake service on a real
// single-instance pipe (the service's shape: one instance, message mode), with real HostUnlockRelay objects.
// What it pins:
//   Q2  a capability query lets the pipe go at once; a transaction holds it to its final answer; EndSession
//       lets it go.
//   L1  results belong to (control session, requestId): another session's request 2 after a FINAL request 2
//       is sent to the service and gets its own answer -- never the cached one; an ended session keeps
//       nothing; while one session holds the pipe another session's request is answered "busy" at once and
//       does not touch the held context.
//   L5  a service that never answers does not hold the pipe past the exchange's deadline; EndSession cancels
//       an exchange in flight; a failed exchange makes "supported" unknown again.

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "host_unlock_relay.hpp"
#include "secure_unlock_ipc.hpp"

using namespace remote60::native_poc;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}

struct FakeService {
  std::wstring name;
  std::atomic<bool> run{true};
  std::atomic<int> connections{0};
  std::atomic<bool> connected{false};
  std::atomic<bool> finishJobs{false};
  std::atomic<bool> stall{false};          // read requests, answer nothing
  std::atomic<int> challengeDelayMs{0};    // r3: answer a challenge / a status this late
  std::atomic<int> statusDelayMs{0};
  std::atomic<int> sealedDelayMs{0};       // r4: answer a sealed request this late
  std::atomic<uint16_t> sealedStage{8};    // what a SealedRequest is answered with
  std::atomic<bool> sealedTerminal{false};
  std::atomic<int> sealedSeen{0};
  std::atomic<int> statusSeen{0};
  // lk-ux-retry r1: the LockCredSupersede (kind 9) a retry sends, and how it is answered.
  std::atomic<bool> offerRetry{true};
  std::atomic<uint16_t> supersedeStage{17};  // Superseded
  std::atomic<bool> supersedeTerminal{true};
  std::atomic<bool> stallSupersede{false};
  std::atomic<int> supersedeSeen{0};
  std::atomic<int> supersedeDelayMs{0};  // lk-ux-retry r2: answer a supersede this late
  std::atomic<int> challengeSeen{0};
  std::thread t;
  HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  void Start() {
    name = L"\\\\.\\pipe\\GNLinkTest-relay-ondemand-" + std::to_wstring(GetCurrentProcessId());
    t = std::thread([this] {
      HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                     PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
      while (run.load()) {
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL c = ConnectNamedPipe(pipe, &ov);
        DWORD e = GetLastError();
        bool ok = c || e == ERROR_PIPE_CONNECTED;
        if (!ok && e == ERROR_IO_PENDING) {
          HANDLE w[2] = {ov.hEvent, stopEvent};
          ok = WaitForMultipleObjects(2, w, FALSE, INFINITE) == WAIT_OBJECT_0;
          if (!ok) CancelIo(pipe);
        }
        CloseHandle(ov.hEvent);
        if (!ok) break;
        ++connections;
        connected = true;
        serve(pipe);
        connected = false;
        DisconnectNamedPipe(pipe);
      }
      CloseHandle(pipe);
    });
  }
  bool read_msg(HANDLE pipe, uint8_t* buf, DWORD size, DWORD* got) {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL r = ReadFile(pipe, buf, size, got, &ov);
    if (!r && GetLastError() == ERROR_IO_PENDING) {
      HANDLE w[2] = {ov.hEvent, stopEvent};
      if (WaitForMultipleObjects(2, w, FALSE, INFINITE) != WAIT_OBJECT_0) {
        CancelIo(pipe);
        CloseHandle(ov.hEvent);
        return false;
      }
      r = GetOverlappedResult(pipe, &ov, got, FALSE);
    }
    CloseHandle(ov.hEvent);
    return r && *got >= 8;
  }
  void write_msg(HANDLE pipe, const void* d, DWORD n) {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD w = 0;
    if (!WriteFile(pipe, d, n, &w, &ov) && GetLastError() == ERROR_IO_PENDING) GetOverlappedResult(pipe, &ov, &w, TRUE);
    CloseHandle(ov.hEvent);
  }
  void serve(HANDLE pipe) {
    uint8_t buf[512];
    for (;;) {
      DWORD got = 0;
      if (!read_msg(pipe, buf, sizeof(buf), &got)) return;
      uint16_t kind = 0;
      std::memcpy(&kind, buf + 6, 2);
      uint32_t requestId = 0;
      std::memcpy(&requestId, buf + 8, 4);
      if (kind == static_cast<uint16_t>(SecureUnlockKind::SealedRequest)) ++sealedSeen;
      if (kind == static_cast<uint16_t>(SecureUnlockKind::StatusRequest)) ++statusSeen;
      if (kind == static_cast<uint16_t>(SecureUnlockKind::LockCredSupersede)) ++supersedeSeen;
      if (kind == static_cast<uint16_t>(SecureUnlockKind::LockCredChallengeRequest)) ++challengeSeen;
      if (kind == static_cast<uint16_t>(SecureUnlockKind::LockCredSupersede) && stallSupersede.load()) continue;
      if (stall.load()) continue;  // read it, answer nothing: the relay's deadline must end this
      if (kind == static_cast<uint16_t>(SecureUnlockKind::CapabilitiesRequest)) {
        SecureUnlockCapabilitiesResponse r{};
        r.requestId = requestId;
        r.features = kSecureUnlockFeatureLockCredV1 | (offerRetry.load() ? kSecureUnlockFeatureLockCredRetryV1 : 0u);
        write_msg(pipe, &r, sizeof(r));
      } else if (kind == static_cast<uint16_t>(SecureUnlockKind::LockCredChallengeRequest) ||
                 kind == static_cast<uint16_t>(SecureUnlockKind::ChallengeRequest)) {
        if (challengeDelayMs.load() > 0) Sleep(challengeDelayMs.load());
        SecureUnlockChallengeRequest req{};
        std::memcpy(&req, buf, sizeof(req));
        SecureUnlockChallengeResponse r{};
        r.requestId = requestId;
        r.clientSessionCookie = req.clientSessionCookie;
        r.stage = static_cast<uint16_t>(UnlockStage::ChallengeIssued);
        r.purpose = kind == static_cast<uint16_t>(SecureUnlockKind::LockCredChallengeRequest) ? kUnlockPurposeLockCred
                                                                                                 : kUnlockPurposeUnlock;
        write_msg(pipe, &r, sizeof(r));
      } else if (kind == static_cast<uint16_t>(SecureUnlockKind::SealedRequest)) {
        if (sealedDelayMs.load() > 0) Sleep(sealedDelayMs.load());
        SecureUnlockResult r{};
        r.requestId = requestId;
        r.stage = sealedStage.load();
        r.terminal = sealedTerminal.load() ? 1 : 0;
        write_msg(pipe, &r, sizeof(r));
      } else if (kind == static_cast<uint16_t>(SecureUnlockKind::LockCredSupersede)) {
        if (supersedeDelayMs.load() > 0) Sleep(supersedeDelayMs.load());
        SecureUnlockResult r{};
        r.requestId = requestId;
        r.stage = supersedeStage.load();
        r.terminal = supersedeTerminal.load() ? 1 : 0;
        write_msg(pipe, &r, sizeof(r));
      } else if (kind == static_cast<uint16_t>(SecureUnlockKind::StatusRequest)) {
        if (statusDelayMs.load() > 0) Sleep(statusDelayMs.load());
        SecureUnlockResult r{};
        r.requestId = requestId;
        const bool done = finishJobs.load();
        r.stage = static_cast<uint16_t>(done ? UnlockStage::SessionUnlocked : UnlockStage::Injected);
        r.terminal = done ? 1 : 0;
        write_msg(pipe, &r, sizeof(r));
      } else {
        return;
      }
    }
  }
  void Stop() {
    run = false;
    SetEvent(stopEvent);
    if (t.joinable()) t.join();
    CloseHandle(stopEvent);
  }
};

ServerIdentityFacts trust_any(void* h, const wchar_t*) {
  ServerIdentityFacts f;
  ULONG pid = 0;
  f.serverPidKnown = GetNamedPipeServerProcessId(static_cast<HANDLE>(h), &pid) != FALSE;
  f.serverPid = f.servicePid = pid;
  f.serviceRunning = f.serverImageKnown = f.serviceBinaryKnown = f.imageSameFile = f.finalConsistent = f.serverPidKnown;
  return f;
}

ControlUnlockChallengeRequestMessage challenge_req(uint32_t id) {
  ControlUnlockChallengeRequestMessage m{};
  m.requestId = id;
  return m;
}
ControlUnlockSealedRequestMessage sealed_req(uint32_t id) {
  ControlUnlockSealedRequestMessage m{};
  m.requestId = id;
  return m;
}
template <typename F>
bool wait_until(F f, int ms) {
  for (int i = 0; i < ms / 20; ++i) {
    if (f()) return true;
    Sleep(20);
  }
  return f();
}
long long ms_since(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}
void point(HostUnlockRelay& r, const FakeService& s) {
  r.SetVerificationEndpointForTest(s.name.c_str(), L"GNLinkTestService", trust_any);
  r.SetExchangeDeadlinesForTest(800, 1500);
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the unlock relay: per-transaction pipe, per-session results, bounded I/O (lockscreen-cred r1/r2)\n\n");
  FakeService svc;
  svc.Start();
  Sleep(200);

  std::printf("[Q2: held only for a transaction]\n");
  HostUnlockRelay a, b;
  point(a, svc);
  point(b, svc);
  a.ProbeLockCredAsync();
  check("A: the capability query is answered (LockCredV1)",
        wait_until([&] { return a.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000));
  check("A: ...and the pipe is let go at once (no idle hold)", wait_until([&] { return !svc.connected.load(); }, 2000));
  b.ProbeLockCredAsync();
  wait_until([&] { return b.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
  ControlUnlockChallengeMessage out{};
  const bool bChallenged = b.LockCredChallengeSync(challenge_req(11), 0xB, &out, 5000);
  check("B: after A's probe, B's challenge goes through (the pipe was free)",
        bChallenged && out.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued) && out.reserved == kUnlockPurposeLockCred,
        "status=" + std::to_string(out.status));
  check("B: ...an issued challenge HOLDS the pipe (bound to this connection until its seal)", svc.connected.load());
  ControlUnlockChallengeMessage aOut{};
  const auto tBusy = std::chrono::steady_clock::now();
  const bool aGot = a.LockCredChallengeSync(challenge_req(21), 0xA, &aOut, 3000);  // the product's wait
  const auto busyMs = ms_since(tBusy);
  check("NEGATIVE: while B (another host) holds the pipe for its transaction, A's challenge does not get through",
        !aGot || aOut.status != static_cast<uint16_t>(UnlockStage::ChallengeIssued), "status=" + std::to_string(aOut.status));
  check("UX-R: ...and A is told BUSY (16), within the control thread's 3 s wait -- not a general error",
        aGot && aOut.status == static_cast<uint16_t>(UnlockStage::Busy) && aOut.reserved == kUnlockPurposeLockCred,
        "status=" + std::to_string(aOut.status) + " in " + std::to_string(busyMs) + " ms");
  check("UX-R: ...A's 'supported' is not dropped because another host is busy",
        a.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported);
  b.SealedAsync(sealed_req(11), 0xB);
  ControlUnlockStatusResultMessage res{};
  wait_until([&] { return b.PollResult(0xB, 11, &res); }, 5000);
  check("B: typed (Injected, not final) -- the pipe stays held for the unlock to be seen",
        res.stage == static_cast<uint16_t>(UnlockStage::Injected) && res.terminal == 0 && svc.connected.load());
  svc.finishJobs = true;
  b.RefreshLockCredStatus(0xB, 11, 3000);
  b.PollResult(0xB, 11, &res);
  check("B: the final answer (unlocked) arrives ...", res.stage == static_cast<uint16_t>(UnlockStage::SessionUnlocked) && res.terminal == 1);
  check("B: ...and the pipe is let go", wait_until([&] { return !svc.connected.load(); }, 2000));

  std::printf("\n[L1: results belong to (control session, requestId)]\n");
  {
    HostUnlockRelay h;  // one host, several control sessions over time -- the shared relay
    point(h, svc);
    h.ProbeLockCredAsync();
    wait_until([&] { return h.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
    // Viewer X's request 2 ends final (the field was not ready).
    svc.sealedStage = static_cast<uint16_t>(UnlockStage::TargetNotReady);
    svc.sealedTerminal = true;
    ControlUnlockChallengeMessage c{};
    h.LockCredChallengeSync(challenge_req(2), 0x1111, &c, 5000);
    h.SealedAsync(sealed_req(2), 0x1111);
    ControlUnlockStatusResultMessage rx{};
    wait_until([&] { return h.PollResult(0x1111, 2, &rx); }, 5000);
    const int sealedBefore = svc.sealedSeen.load();
    // A NEW viewer (another control session) starts its numbering at 2 too.
    svc.sealedStage = static_cast<uint16_t>(UnlockStage::Injected);
    svc.sealedTerminal = false;
    ControlUnlockChallengeMessage c2{};
    const bool issued = h.LockCredChallengeSync(challenge_req(2), 0x2222, &c2, 5000);
    ControlUnlockStatusResultMessage early{};
    const bool earlyKnown = h.PollResult(0x2222, 2, &early);
    h.SealedAsync(sealed_req(2), 0x2222);
    ControlUnlockStatusResultMessage ry{};
    wait_until([&] { return h.PollResult(0x2222, 2, &ry); }, 5000);
    check("L1: X's request 2 ended final (TargetNotReady)", rx.terminal == 1 && rx.stage == static_cast<uint16_t>(UnlockStage::TargetNotReady));
    check("L1: Y's request 2 (another session) knows nothing of X's before its own seal",
          issued && !earlyKnown, "earlyKnown=" + std::to_string(earlyKnown));
    check("L1: ...its seal IS sent to the service (not skipped as 'already final')", svc.sealedSeen.load() == sealedBefore + 1,
          std::to_string(svc.sealedSeen.load() - sealedBefore) + " sealed sent");
    check("L1: ...and Y gets ITS answer (typed, waiting), not X's", ry.stage == static_cast<uint16_t>(UnlockStage::Injected) && ry.terminal == 0);
    ControlUnlockStatusResultMessage stillX{};
    check("L1: X's own result is still X's", h.PollResult(0x1111, 2, &stillX) && stillX.stage == static_cast<uint16_t>(UnlockStage::TargetNotReady));
    // Busy: Y holds the pipe (its typed credential waits). Z's request is answered busy at once and does not
    // touch Y's transaction.
    const auto t0 = std::chrono::steady_clock::now();
    ControlUnlockChallengeMessage cz{};
    const bool zGot = h.LockCredChallengeSync(challenge_req(2), 0x3333, &cz, 5000);
    check("L1/UX-R: while Y's transaction holds the pipe, Z's request is answered BUSY (stage 16) at once (not run on Y's connection)",
          zGot && cz.status == static_cast<uint16_t>(UnlockStage::Busy) && ms_since(t0) < 1000,
          std::to_string(ms_since(t0)) + " ms, status=" + std::to_string(cz.status));
    ControlUnlockChallengeMessage cz2{};
    h.ChallengeSync(challenge_req(4), 0x3333, &cz2, 5000);
    check("UX-R: ...the disconnected-session unlock challenge too (Busy, purpose kept)",
          cz2.status == static_cast<uint16_t>(UnlockStage::Busy) && cz2.reserved == kUnlockPurposeUnlock,
          "status=" + std::to_string(cz2.status));
    const int sealedBeforeZ = svc.sealedSeen.load();
    h.SealedAsync(sealed_req(4), 0x3333);
    ControlUnlockStatusResultMessage zs{};
    wait_until([&] { return h.PollResult(0x3333, 4, &zs); }, 3000);
    check("UX-R: ...and a sealed request: Busy, final, ERROR_BUSY -- not sent to the service",
          zs.stage == static_cast<uint16_t>(UnlockStage::Busy) && zs.terminal == 1 && zs.win32Error == ERROR_BUSY &&
              svc.sealedSeen.load() == sealedBeforeZ,
          "stage=" + std::to_string(zs.stage));
    svc.finishJobs = true;
    h.RefreshLockCredStatus(0x2222, 2, 3000);
    h.PollResult(0x2222, 2, &ry);
    check("L1: ...Y's transaction was not disturbed: it ends unlocked", ry.stage == static_cast<uint16_t>(UnlockStage::SessionUnlocked));
    svc.finishJobs = false;
    // An ended session keeps nothing.
    h.EndSession(0x1111);
    Sleep(100);
    ControlUnlockStatusResultMessage gone{};
    check("L1: X's session ended -> nothing of it is kept", !h.PollResult(0x1111, 2, &gone));
    h.EndSession(0x2222);
    h.EndSession(0x3333);
    h.Stop();
  }

  std::printf("\n[L5: bounded, cancellable exchanges]\n");
  {
    wait_until([&] { return !svc.connected.load(); }, 3000);
    HostUnlockRelay h, other;
    point(h, svc);
    point(other, svc);
    h.ProbeLockCredAsync();
    wait_until([&] { return h.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
    // A service that reads the challenge and never answers.
    svc.stall = true;
    auto t0 = std::chrono::steady_clock::now();
    ControlUnlockChallengeMessage c{};
    const bool got = h.LockCredChallengeSync(challenge_req(5), 0x5555, &c, 5000);
    const auto challengeMs = ms_since(t0);
    check("L5: a silent service: the challenge exchange ends at its deadline (not hung), as a failure -- NOT busy (UX-R)",
          got && c.status == static_cast<uint16_t>(UnlockStage::InternalError) && challengeMs < 2500,
          std::to_string(challengeMs) + " ms, status=" + std::to_string(c.status));
    check("L5: ...the relay let the pipe go (the next host can connect)", wait_until([&] { return !svc.connected.load(); }, 2000));
    check("L5: ...and a failed exchange makes 'supported' unknown again (not advertised stale)",
          h.LockCredSupportNow() != HostUnlockRelay::LockCredSupport::Supported);
    // EndSession cancels an exchange in flight (well before its deadline).
    svc.stall = false;
    other.ProbeLockCredAsync();
    wait_until([&] { return other.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
    ControlUnlockChallengeMessage oc{};
    other.LockCredChallengeSync(challenge_req(6), 0x6666, &oc, 5000);
    svc.stall = true;
    other.SealedAsync(sealed_req(6), 0x6666);
    wait_until([&] { return svc.sealedSeen.load() > 0 && svc.connected.load(); }, 2000);
    Sleep(100);  // the sealed exchange is in flight, waiting for an answer that never comes
    t0 = std::chrono::steady_clock::now();
    other.EndSession(0x6666);
    check("L5: EndSession during a silent sealed exchange: cancelled and the pipe let go at once (not at the 1.5 s deadline)",
          wait_until([&] { return !svc.connected.load(); }, 1400) && ms_since(t0) < 1400, std::to_string(ms_since(t0)) + " ms");
    ControlUnlockStatusResultMessage r{};
    check("L5: ...and its late result is kept for nobody", !other.PollResult(0x6666, 6, &r));
    svc.stall = false;
    // After all that, the next host gets through.
    HostUnlockRelay next;
    point(next, svc);
    next.ProbeLockCredAsync();
    wait_until([&] { return next.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
    ControlUnlockChallengeMessage nc{};
    check("L5: the next host's request goes through", next.LockCredChallengeSync(challenge_req(7), 0x7777, &nc, 5000) &&
                                                         nc.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued));
    next.EndSession(0x7777);
    next.Stop();
    other.Stop();
    h.Stop();
  }

  std::printf("\n[r3 UX-R control: no service at all is a failure, not 'busy']\n");
  {
    HostUnlockRelay none;
    none.SetVerificationEndpointForTest(L"\\\\.\\pipe\\GNLinkTest-relay-no-such-service", L"GNLinkTestService", trust_any);
    ControlUnlockChallengeMessage c{};
    const bool got = none.ChallengeSync(challenge_req(9), 0xE1, &c, 5000);
    check("UX-R control: the service's pipe does not exist -> InternalError, NOT Busy",
          got && c.status == static_cast<uint16_t>(UnlockStage::InternalError), "status=" + std::to_string(c.status));
    none.EndSession(0xE1);
    none.Stop();
  }

  std::printf("\n[r3 L5-R: the hold's end holds however full the queue is]\n");
  {
    wait_until([&] { return !svc.connected.load(); }, 3000);
    HostUnlockRelay h;
    point(h, svc);
    h.SetHoldLimitsForTest(1500, 2000);  // the product's 35 s / 130 s, shortened -- the same code
    h.ProbeLockCredAsync();
    wait_until([&] { return h.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
    // Typed phase: Y's credential is typed and waits; Y's own status questions (each answered slowly) and
    // Z's busy requests keep the queue from ever being empty past the hold's end.
    svc.sealedStage = static_cast<uint16_t>(UnlockStage::Injected);
    svc.sealedTerminal = false;
    svc.finishJobs = false;
    ControlUnlockChallengeMessage cy{};
    h.LockCredChallengeSync(challenge_req(31), 0xA1, &cy, 5000);
    h.SealedAsync(sealed_req(31), 0xA1);
    ControlUnlockStatusResultMessage ry{};
    wait_until([&] { return h.PollResult(0xA1, 31, &ry) && ry.stage == static_cast<uint16_t>(UnlockStage::Injected); }, 5000);
    const auto typedAt = std::chrono::steady_clock::now();
    svc.statusDelayMs = 20;
    std::atomic<bool> flood{true};
    std::thread fl([&] {
      uint32_t zid = 1000;
      while (flood.load()) {
        h.RefreshLockCredStatus(0xA1, 31, 0);  // queued, not waited for
        h.SealedAsync(sealed_req(zid++), 0xA2);  // another session: busy
        Sleep(2);
      }
    });
    // The next host (another host process, its own relay) asks once the hold should have ended.
    HostUnlockRelay next;
    point(next, svc);
    while (ms_since(typedAt) < 2300) Sleep(20);
    const int statusAtEnd = svc.statusSeen.load();
    next.ProbeLockCredAsync();
    const bool nextCaps =
        wait_until([&] { return next.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 4000);
    ControlUnlockChallengeMessage nc{};
    const bool nextGot = nextCaps && next.LockCredChallengeSync(challenge_req(1), 0xB1, &nc, 4000);
    const auto releasedAfter = ms_since(typedAt);
    Sleep(300);
    const int statusLater = svc.statusSeen.load();
    flood = false;
    fl.join();
    check("L5-R typed: a never-empty queue (own status + another session's busy) past the hold's end: the next host gets through",
          nextGot && nc.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued),
          "status=" + std::to_string(nc.status) + " at " + std::to_string(releasedAfter) + " ms after typed");
    h.PollResult(0xA1, 31, &ry);
    check("L5-R typed: ...Y's transaction ended with the hold as UnlockUnconfirmed (final), not success",
          ry.terminal == 1 && ry.stage == static_cast<uint16_t>(UnlockStage::UnlockUnconfirmed), "stage=" + std::to_string(ry.stage));
    check("L5-R typed: ...and its queued status questions were NOT asked on a new connection",
          statusLater == statusAtEnd, std::to_string(statusLater - statusAtEnd) + " asked after the end");
    svc.statusDelayMs = 0;
    next.EndSession(0xB1);
    next.Stop();
    h.EndSession(0xA1);
    h.EndSession(0xA2);
    wait_until([&] { return !svc.connected.load(); }, 3000);

    // Challenge phase: Y's challenge holds; Z's busy requests keep the queue busy past the hold's end.
    ControlUnlockChallengeMessage cy2{};
    h.LockCredChallengeSync(challenge_req(32), 0xA3, &cy2, 5000);
    const auto issuedAt = std::chrono::steady_clock::now();
    flood = true;
    std::thread fl2([&] {
      uint32_t zid = 5000;
      while (flood.load()) {
        h.SealedAsync(sealed_req(zid++), 0xA4);
        Sleep(1);
      }
    });
    HostUnlockRelay next2;
    point(next2, svc);
    while (ms_since(issuedAt) < 1800) Sleep(20);
    next2.ProbeLockCredAsync();
    const bool caps2 =
        wait_until([&] { return next2.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 4000);
    ControlUnlockChallengeMessage nc2{};
    const bool got2 = caps2 && next2.LockCredChallengeSync(challenge_req(1), 0xB2, &nc2, 4000);
    flood = false;
    fl2.join();
    check("L5-R challenge: past the challenge hold's end, with the queue busy, the next host gets through",
          got2 && nc2.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued) &&
              cy2.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued),
          "status=" + std::to_string(nc2.status) + " (Y's own challenge status=" + std::to_string(cy2.status) + ")");
    const int sealedBeforeLate = svc.sealedSeen.load();
    h.SealedAsync(sealed_req(32), 0xA3);  // Y's seal, too late
    ControlUnlockStatusResultMessage late{};
    wait_until([&] { return h.PollResult(0xA3, 32, &late); }, 2000);
    check("L5-R challenge: ...Y's seal after its hold ended: Timeout (final), not sent on another connection",
          late.terminal == 1 && late.stage == static_cast<uint16_t>(UnlockStage::Timeout) && svc.sealedSeen.load() == sealedBeforeLate,
          "stage=" + std::to_string(late.stage));
    next2.EndSession(0xB2);
    next2.Stop();
    h.EndSession(0xA3);
    h.EndSession(0xA4);
    wait_until([&] { return !svc.connected.load(); }, 3000);

    // An early end still ends it early: EndSession of the holder.
    ControlUnlockChallengeMessage cy3{};
    h.LockCredChallengeSync(challenge_req(33), 0xA5, &cy3, 5000);
    const auto t0 = std::chrono::steady_clock::now();
    h.EndSession(0xA5);
    check("L5-R: EndSession of the holder lets the pipe go at once (well before the hold's end)",
          wait_until([&] { return !svc.connected.load(); }, 1000) && ms_since(t0) < 1000, std::to_string(ms_since(t0)) + " ms");
    h.Stop();
  }

  std::printf("\n[r4 R1: a repeated sealed request never restarts the typed hold]\n");
  {
    wait_until([&] { return !svc.connected.load(); }, 3000);
    HostUnlockRelay h;
    point(h, svc);
    h.SetHoldLimitsForTest(1500, 2000);  // the product's 35 s / 130 s, shortened -- the same code
    h.ProbeLockCredAsync();
    wait_until([&] { return h.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
    svc.sealedStage = static_cast<uint16_t>(UnlockStage::Injected);  // the service answers a repeat "typed" again
    svc.sealedTerminal = false;
    svc.finishJobs = false;
    ControlUnlockChallengeMessage c{};
    h.LockCredChallengeSync(challenge_req(61), 0xF1, &c, 5000);
    const int sealedBefore = svc.sealedSeen.load();
    h.SealedAsync(sealed_req(61), 0xF1);
    ControlUnlockStatusResultMessage r{};
    wait_until([&] { return h.PollResult(0xF1, 61, &r) && r.stage == static_cast<uint16_t>(UnlockStage::Injected); }, 5000);
    const auto typedAt = std::chrono::steady_clock::now();
    // Repeats of the SAME sealed request while it waits typed: at 0.5 s, 1.0 s, and one answered late (1.3 s + 0.3 s).
    while (ms_since(typedAt) < 500) Sleep(10);
    h.SealedAsync(sealed_req(61), 0xF1);
    while (ms_since(typedAt) < 1000) Sleep(10);
    h.SealedAsync(sealed_req(61), 0xF1);
    while (ms_since(typedAt) < 1300) Sleep(10);
    svc.sealedDelayMs = 300;
    h.SealedAsync(sealed_req(61), 0xF1);
    wait_until([&] { return svc.sealedSeen.load() >= sealedBefore + 4; }, 1500);
    svc.sealedDelayMs = 0;
    const int repeatsSent = svc.sealedSeen.load() - sealedBefore - 1;
    // The FIRST typed deadline (2 s after typed) ends the hold: the next host gets through soon after it.
    HostUnlockRelay next;
    point(next, svc);
    while (ms_since(typedAt) < 2300) Sleep(20);
    next.ProbeLockCredAsync();
    const bool caps =
        wait_until([&] { return next.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 4000);
    ControlUnlockChallengeMessage nc{};
    const bool got = caps && next.LockCredChallengeSync(challenge_req(1), 0xF2, &nc, 4000);
    check("R1: three repeats of the same sealed request reached the service (answered 'typed' again)", repeatsSent == 3,
          std::to_string(repeatsSent) + " repeats sent");
    check("R1: ...the hold still ended at the FIRST typed deadline: the next host gets through (not 'busy')",
          got && nc.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued),
          "status=" + std::to_string(nc.status) + " at " + std::to_string(ms_since(typedAt)) + " ms after typed");
    h.PollResult(0xF1, 61, &r);
    check("R1: ...and the transaction ended with it as UnlockUnconfirmed (final)",
          r.terminal == 1 && r.stage == static_cast<uint16_t>(UnlockStage::UnlockUnconfirmed), "stage=" + std::to_string(r.stage));
    const int sealedAfterEnd = svc.sealedSeen.load();
    h.SealedAsync(sealed_req(61), 0xF1);  // a repeat after the final answer
    Sleep(300);
    check("R1 control: a repeat after the FINAL answer is not sent at all (idempotent, as before)",
          svc.sealedSeen.load() == sealedAfterEnd, std::to_string(svc.sealedSeen.load() - sealedAfterEnd) + " sent");
    next.EndSession(0xF2);
    next.Stop();
    h.EndSession(0xF1);
    h.Stop();
  }

  std::printf("\n[r3 L1-R: an exchange that completes after its session ended keeps nothing]\n");
  {
    wait_until([&] { return !svc.connected.load(); }, 3000);
    HostUnlockRelay h;
    point(h, svc);
    h.ProbeLockCredAsync();
    wait_until([&] { return h.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
    // A live other session with a final result kept.
    svc.sealedStage = static_cast<uint16_t>(UnlockStage::TargetNotReady);
    svc.sealedTerminal = true;
    ControlUnlockChallengeMessage ck{};
    h.LockCredChallengeSync(challenge_req(41), 0xC1, &ck, 5000);
    h.SealedAsync(sealed_req(41), 0xC1);
    ControlUnlockStatusResultMessage kept{};
    wait_until([&] { return h.PollResult(0xC1, 41, &kept); }, 5000);
    const size_t keptBefore = h.KeptEntriesForTest(0xC1);

    // EndSession while the challenge answer is on its way (answered late, the exchange completes).
    svc.challengeDelayMs = 400;
    std::thread ask([&] {
      ControlUnlockChallengeMessage c{};
      h.LockCredChallengeSync(challenge_req(42), 0xC2, &c, 1500);
    });
    Sleep(120);
    h.EndSession(0xC2);
    ask.join();
    Sleep(500);  // the late answer (if any) has landed
    svc.challengeDelayMs = 0;
    check("L1-R: EndSession during the challenge exchange -> nothing of that session is kept afterwards",
          h.KeptEntriesForTest(0xC2) == 0, std::to_string(h.KeptEntriesForTest(0xC2)) + " entries");

    // EndSession while a status answer is on its way. (The cancelled exchange made "supported" unknown: asked anew.)
    h.ProbeLockCredAsync();
    wait_until([&] { return h.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
    svc.sealedStage = static_cast<uint16_t>(UnlockStage::Injected);
    svc.sealedTerminal = false;
    svc.finishJobs = false;
    ControlUnlockChallengeMessage cs{};
    h.LockCredChallengeSync(challenge_req(43), 0xC3, &cs, 5000);
    h.SealedAsync(sealed_req(43), 0xC3);
    ControlUnlockStatusResultMessage rs{};
    wait_until([&] { return h.PollResult(0xC3, 43, &rs); }, 5000);
    svc.statusDelayMs = 400;
    std::thread st([&] { h.RefreshLockCredStatus(0xC3, 43, 1500); });
    Sleep(120);
    h.EndSession(0xC3);
    st.join();
    Sleep(500);
    svc.statusDelayMs = 0;
    check("L1-R: EndSession during the status exchange -> nothing of that session is kept afterwards",
          h.KeptEntriesForTest(0xC3) == 0, std::to_string(h.KeptEntriesForTest(0xC3)) + " entries");
    ControlUnlockStatusResultMessage stillKept{};
    check("L1-R: ...the other live session's result is untouched",
          h.PollResult(0xC1, 41, &stillKept) && stillKept.stage == static_cast<uint16_t>(UnlockStage::TargetNotReady) &&
              h.KeptEntriesForTest(0xC1) == keptBefore);
    HostUnlockRelay next;
    point(next, svc);
    next.ProbeLockCredAsync();
    wait_until([&] { return next.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
    ControlUnlockChallengeMessage nc{};
    check("L1-R: ...and the next host gets through", next.LockCredChallengeSync(challenge_req(1), 0xD1, &nc, 5000) &&
                                                         nc.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued));
    next.EndSession(0xD1);
    next.Stop();
    h.EndSession(0xC1);
    h.Stop();
  }

  std::printf("\n[lk-ux-retry r1: the same session's explicit retry, on the pipe its typed credential holds]\n");
  {
    wait_until([&] { return !svc.connected.load(); }, 3000);
    svc.stall = false;
    svc.statusDelayMs = 0;
    svc.challengeDelayMs = 0;
    svc.sealedDelayMs = 0;
    svc.finishJobs = false;
    svc.sealedStage = static_cast<uint16_t>(UnlockStage::Injected);
    svc.sealedTerminal = false;
    svc.supersedeStage = static_cast<uint16_t>(UnlockStage::Superseded);
    svc.supersedeTerminal = true;
    HostUnlockRelay h;
    point(h, svc);
    h.ProbeLockCredAsync();
    wait_until([&] { return h.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
    check("retry: a service listing LockCredRetryV1 -> the host says it can (pong 0x80000 follows this)",
          h.LockCredRetrySupported());
    // X types a wrong password: typed, waiting, the pipe held for it.
    ControlUnlockChallengeMessage c1{};
    h.LockCredChallengeSync(challenge_req(51), 0x51, &c1, 5000);
    h.SealedAsync(sealed_req(51), 0x51);
    ControlUnlockStatusResultMessage r1{};
    wait_until([&] { return h.PollResult(0x51, 51, &r1); }, 5000);
    check("retry: X's first password typed and waiting (the 10-22 state)",
          r1.stage == static_cast<uint16_t>(UnlockStage::Injected) && r1.terminal == 0 && svc.connected.load());
    // Another host session presses retry naming X's request: Busy, nothing reaches the service.
    const int supBeforeY = svc.supersedeSeen.load();
    const int chBeforeY = svc.challengeSeen.load();
    ControlUnlockChallengeMessage cy{};
    const bool yGot = h.LockCredRetryChallengeSync(challenge_req(52), 51, 0x52, &cy, 3000);
    check("retry: ANOTHER control session's retry naming X's request -> Busy, no supersede, no challenge sent",
          yGot && cy.status == static_cast<uint16_t>(UnlockStage::Busy) && svc.supersedeSeen.load() == supBeforeY &&
              svc.challengeSeen.load() == chBeforeY,
          "status=" + std::to_string(cy.status));
    ControlUnlockStatusResultMessage xStill{};
    check("retry: ...X's wait untouched", h.PollResult(0x51, 51, &xStill) &&
                                              xStill.stage == static_cast<uint16_t>(UnlockStage::Injected) && xStill.terminal == 0);
    // The service refuses to end it (answers, but not terminal): nothing new is asked, the wait stays.
    svc.supersedeStage = static_cast<uint16_t>(UnlockStage::Injected);
    svc.supersedeTerminal = false;
    const int chBeforeRefused = svc.challengeSeen.load();
    ControlUnlockChallengeMessage cr{};
    const bool refusedGot = h.LockCredRetryChallengeSync(challenge_req(53), 51, 0x51, &cr, 3000);
    check("retry: the service does not confirm the end -> SupersedeRefused (18), NO new challenge asked",
          refusedGot && cr.status == static_cast<uint16_t>(UnlockStage::SupersedeRefused) &&
              svc.challengeSeen.load() == chBeforeRefused,
          "status=" + std::to_string(cr.status));
    check("retry: ...and the earlier wait is kept as it was (its pipe still held)",
          svc.connected.load() && h.PollResult(0x51, 51, &xStill) && xStill.stage == static_cast<uint16_t>(UnlockStage::Injected));
    // The explicit retry, confirmed: supersede first, then the new challenge, in one worker step.
    svc.supersedeStage = static_cast<uint16_t>(UnlockStage::Superseded);
    svc.supersedeTerminal = true;
    const int supBefore = svc.supersedeSeen.load();
    const int chBefore = svc.challengeSeen.load();
    const int connBefore = svc.connections.load();
    ControlUnlockChallengeMessage c2{};
    const bool got2 = h.LockCredRetryChallengeSync(challenge_req(54), 51, 0x51, &c2, 3000);
    check("retry: X's own retry -> the earlier wait ended (1 supersede), then a NEW challenge (purpose LockCred)",
          got2 && c2.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued) && c2.reserved == kUnlockPurposeLockCred &&
              svc.supersedeSeen.load() == supBefore + 1 && svc.challengeSeen.load() == chBefore + 1,
          "status=" + std::to_string(c2.status));
    check("retry: ...on the SAME service connection (no reconnect of the pipe)", svc.connections.load() == connBefore,
          std::to_string(svc.connections.load() - connBefore) + " new connections");
    ControlUnlockStatusResultMessage old{};
    check("retry: the earlier request's result is now final: Superseded (its poll ends)",
          h.PollResult(0x51, 51, &old) && old.stage == static_cast<uint16_t>(UnlockStage::Superseded) && old.terminal == 1,
          "stage=" + std::to_string(old.stage));
    const int statusBefore = svc.statusSeen.load();
    h.RefreshLockCredStatus(0x51, 51, 1000);
    check("retry: ...and it is never asked about again (no status to the service for it)",
          svc.statusSeen.load() == statusBefore, std::to_string(svc.statusSeen.load() - statusBefore) + " asked");
    h.SealedAsync(sealed_req(54), 0x51);
    ControlUnlockStatusResultMessage r2{};
    wait_until([&] { return h.PollResult(0x51, 54, &r2); }, 5000);
    check("retry: the new password typed and waiting", r2.stage == static_cast<uint16_t>(UnlockStage::Injected) && r2.terminal == 0);
    // A late retry naming the OLD request again: its wait is long gone -- no supersede, the new one is not ended.
    const int supLate = svc.supersedeSeen.load();
    const int chLate = svc.challengeSeen.load();
    ControlUnlockChallengeMessage cl{};
    h.LockCredRetryChallengeSync(challenge_req(55), 51, 0x51, &cl, 3000);
    check("retry: a retry naming the OLD (ended) request while the new one waits -> SupersedeRefused, no supersede, "
          "no challenge -- the new typed wait is not ended by it",
          cl.status == static_cast<uint16_t>(UnlockStage::SupersedeRefused) && svc.supersedeSeen.load() == supLate &&
              svc.challengeSeen.load() == chLate,
          "status=" + std::to_string(cl.status));
    svc.finishJobs = true;
    h.RefreshLockCredStatus(0x51, 54, 3000);
    ControlUnlockStatusResultMessage done{};
    check("retry: ...and the new attempt ends unlocked", h.PollResult(0x51, 54, &done) &&
                                                             done.stage == static_cast<uint16_t>(UnlockStage::SessionUnlocked));
    svc.finishJobs = false;
    h.EndSession(0x51);
    h.EndSession(0x52);
    wait_until([&] { return !svc.connected.load(); }, 3000);
    // A silent service on the supersede: bounded, refused, nothing typed.
    h.ProbeLockCredAsync();
    wait_until([&] { return h.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
    ControlUnlockChallengeMessage c3{};
    h.LockCredChallengeSync(challenge_req(61), 0x61, &c3, 5000);
    h.SealedAsync(sealed_req(61), 0x61);
    ControlUnlockStatusResultMessage r3{};
    wait_until([&] { return h.PollResult(0x61, 61, &r3); }, 5000);
    svc.stallSupersede = true;
    const int chStall = svc.challengeSeen.load();
    const auto t0 = std::chrono::steady_clock::now();
    ControlUnlockChallengeMessage cs{};
    const bool gotS = h.LockCredRetryChallengeSync(challenge_req(62), 61, 0x61, &cs, 3000);
    check("retry: a service silent on the supersede -> SupersedeRefused within the deadline, no new challenge",
          gotS && cs.status == static_cast<uint16_t>(UnlockStage::SupersedeRefused) && ms_since(t0) < 2500 &&
              svc.challengeSeen.load() == chStall,
          std::to_string(ms_since(t0)) + " ms, status=" + std::to_string(cs.status));
    svc.stallSupersede = false;
    h.EndSession(0x61);
    h.Stop();
  }
  {
    wait_until([&] { return !svc.connected.load(); }, 3000);
    svc.offerRetry = false;
    HostUnlockRelay old;
    point(old, svc);
    old.ProbeLockCredAsync();
    wait_until([&] { return old.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
    check("retry: a service WITHOUT LockCredRetryV1 -> the host does not say it can (no 0x80000)",
          !old.LockCredRetrySupported());
    const int supBefore = svc.supersedeSeen.load();
    const int chBefore = svc.challengeSeen.load();
    ControlUnlockChallengeMessage c{};
    const bool got = old.LockCredRetryChallengeSync(challenge_req(71), 70, 0x71, &c, 3000);
    check("retry: ...and a retry to it is refused here (SupersedeRefused, 18: the viewer keeps following the earlier "
          "wait) -- nothing sent, never 'replaced'",
          got && c.status == static_cast<uint16_t>(UnlockStage::SupersedeRefused) && svc.supersedeSeen.load() == supBefore &&
              svc.challengeSeen.load() == chBefore,
          "status=" + std::to_string(c.status));
    svc.offerRetry = true;
    old.Stop();
  }

  std::printf("\n[lk-ux-retry r2 (Codex N1): one deadline for the whole retry; nothing after its caller answered]\n");
  {
    wait_until([&] { return !svc.connected.load(); }, 3000);
    svc.offerRetry = true;
    svc.stall = false;
    svc.stallSupersede = false;
    svc.finishJobs = false;
    svc.sealedStage = static_cast<uint16_t>(UnlockStage::Injected);
    svc.sealedTerminal = false;
    svc.supersedeStage = static_cast<uint16_t>(UnlockStage::Superseded);
    svc.supersedeTerminal = true;
    HostUnlockRelay h;
    point(h, svc);
    h.ProbeLockCredAsync();
    wait_until([&] { return h.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
    const auto typed_wait = [&](uint64_t cookie, uint32_t id) {
      ControlUnlockChallengeMessage c{};
      h.LockCredChallengeSync(challenge_req(id), cookie, &c, 5000);
      h.SealedAsync(sealed_req(id), cookie);
      ControlUnlockStatusResultMessage r{};
      wait_until([&] { return h.PollResult(cookie, id, &r); }, 5000);
      return r.stage == static_cast<uint16_t>(UnlockStage::Injected) && r.terminal == 0;
    };

    // (1) The worker is busy (a status answer 0.7 s late, inside the test's 0.8 s IPC limit) when the retry is queued; its caller gives up after
    // 300 ms (18). The retry is withdrawn from the queue: it never runs.
    check("N1 (1) setup: A typed and waiting", typed_wait(0x81, 81));
    svc.statusDelayMs = 700;
    std::thread busy([&] { h.RefreshLockCredStatus(0x81, 81, 3000); });
    Sleep(100);
    const int sup1 = svc.supersedeSeen.load(), ch1 = svc.challengeSeen.load();
    ControlUnlockChallengeMessage r1{};
    const auto t1 = std::chrono::steady_clock::now();
    const bool got1 = h.LockCredRetryChallengeSync(challenge_req(82), 81, 0x81, &r1, 300);
    const auto ms1 = ms_since(t1);
    busy.join();
    svc.statusDelayMs = 0;
    Sleep(800);
    ControlUnlockStatusResultMessage a1{};
    check("N1 (1): the caller gives up at its deadline (no answer -> the host says 18)", !got1 && ms1 < 1000,
          std::to_string(ms1) + " ms");
    check("N1 (1): ...and the withdrawn retry never ran: 0 supersede, 0 challenge",
          svc.supersedeSeen.load() == sup1 && svc.challengeSeen.load() == ch1,
          std::to_string(svc.supersedeSeen.load() - sup1) + " supersede, " + std::to_string(svc.challengeSeen.load() - ch1) + " challenge");
    check("N1 (1): ...A still waits (its hold and result as they were); nothing kept for the retry",
          h.PollResult(0x81, 81, &a1) && a1.stage == static_cast<uint16_t>(UnlockStage::Injected) && a1.terminal == 0 &&
              !h.PollResult(0x81, 82, &a1) && svc.connected.load());
    h.EndSession(0x81);
    wait_until([&] { return !svc.connected.load(); }, 3000);
    h.ProbeLockCredAsync();
    wait_until([&] { return h.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);

    // (2) The supersede is answered only after the retry's deadline (the service is slow): the retry's own
    // exchange ends at that deadline -- inside the caller's wait, so the caller hears it: 18, nothing new asked.
    check("N1 (2) setup: A typed and waiting", typed_wait(0x83, 83));
    svc.supersedeDelayMs = 900;
    const int ch2 = svc.challengeSeen.load();
    ControlUnlockChallengeMessage r2{};
    const bool got2 = h.LockCredRetryChallengeSync(challenge_req(84), 83, 0x83, &r2, 300);
    Sleep(1500);
    svc.supersedeDelayMs = 0;
    check("N1 (2): a supersede slower than the retry's deadline -> the caller hears 18 (not ended), and NO fresh challenge",
          got2 && r2.status == static_cast<uint16_t>(UnlockStage::SupersedeRefused) && svc.challengeSeen.load() == ch2,
          "got=" + std::to_string(got2) + " status=" + std::to_string(r2.status) + ", " +
              std::to_string(svc.challengeSeen.load() - ch2) + " challenges");
    ControlUnlockStatusResultMessage none{};
    check("N1 (2): ...nothing kept for the retry, and the pipe is not held for it",
          !h.PollResult(0x83, 84, &none) && wait_until([&] { return !svc.connected.load(); }, 3000));
    h.EndSession(0x83);
    wait_until([&] { return !svc.connected.load(); }, 3000);
    h.ProbeLockCredAsync();  // the failed exchange made "supported" unknown: asked again
    wait_until([&] { return h.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);

    // (2b) The supersede succeeds in time, then the fresh challenge is answered after the retry's deadline:
    // the earlier request keeps its real end (17), the retry is answered 17 too (the earlier wait is over, no
    // new challenge) -- and no challenge of the retry is held.
    check("N1 (2b) setup: A typed and waiting", typed_wait(0x85, 85));
    svc.supersedeDelayMs = 100;
    svc.challengeDelayMs = 600;
    ControlUnlockChallengeMessage r2b{};
    const bool got2b = h.LockCredRetryChallengeSync(challenge_req(86), 85, 0x85, &r2b, 300);
    Sleep(1200);
    svc.supersedeDelayMs = 0;
    svc.challengeDelayMs = 0;
    ControlUnlockStatusResultMessage a2b{};
    const bool aEnded = h.PollResult(0x85, 85, &a2b);
    check("N1 (2b): the earlier wait ended (its own Superseded, final); the retry is answered Superseded (no challenge)",
          got2b && r2b.status == static_cast<uint16_t>(UnlockStage::Superseded) && aEnded &&
              a2b.stage == static_cast<uint16_t>(UnlockStage::Superseded) && a2b.terminal == 1,
          "retry status=" + std::to_string(r2b.status) + " A stage=" + std::to_string(a2b.stage));
    check("N1 (2b): ...and the late challenge of the retry is not held (the pipe goes; nothing kept for it)",
          !h.PollResult(0x85, 86, &none) && wait_until([&] { return !svc.connected.load(); }, 3000));
    h.EndSession(0x85);
    wait_until([&] { return !svc.connected.load(); }, 3000);
    h.ProbeLockCredAsync();
    wait_until([&] { return h.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);

    // (3) The session ends while the supersede is on the pipe: no fresh challenge, nothing of it kept.
    check("N1 (3) setup: A typed and waiting", typed_wait(0x87, 87));
    svc.supersedeDelayMs = 500;
    const int ch3 = svc.challengeSeen.load();
    ControlUnlockChallengeMessage r3{};
    std::thread retry3([&] { (void)h.LockCredRetryChallengeSync(challenge_req(88), 87, 0x87, &r3, 3000); });
    Sleep(150);
    h.EndSession(0x87);
    retry3.join();
    Sleep(800);
    svc.supersedeDelayMs = 0;
    check("N1 (3): the session ended during the supersede -> no fresh challenge, nothing of the session kept",
          svc.challengeSeen.load() == ch3 && h.KeptEntriesForTest(0x87) == 0,
          std::to_string(svc.challengeSeen.load() - ch3) + " challenges, " + std::to_string(h.KeptEntriesForTest(0x87)) + " kept");
    check("N1 (3): ...and the pipe is not held", wait_until([&] { return !svc.connected.load(); }, 3000));
    h.Stop();
  }

  std::printf("\n[lk-ux-retry r3 (Codex N1): the write is started only for a retry still owned and in time; a late answer is not adopted]\n");
  {
    wait_until([&] { return !svc.connected.load(); }, 3000);
    svc.offerRetry = true;
    svc.supersedeDelayMs = 0;
    svc.challengeDelayMs = 0;
    svc.statusDelayMs = 0;
    svc.finishJobs = false;
    svc.sealedStage = static_cast<uint16_t>(UnlockStage::Injected);
    svc.sealedTerminal = false;
    svc.supersedeStage = static_cast<uint16_t>(UnlockStage::Superseded);
    svc.supersedeTerminal = true;
    HostUnlockRelay h;
    point(h, svc);
    // The latch: the worker stops at the named point (after its fence, before the exchange's own gate) until
    // the test lets it go.
    std::atomic<int> at{0};  // 0 none, 1 holding
    std::string holdAt;
    HANDLE go = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    h.SetRetryIoHookForTest([&](const char* what) {
      if (holdAt != what) return;
      at.store(1);
      WaitForSingleObject(go, 10000);
      at.store(0);
    });
    const auto reprobe = [&] {
      h.ProbeLockCredAsync();
      wait_until([&] { return h.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
    };
    const auto typed_wait = [&](uint64_t cookie, uint32_t id) {
      ControlUnlockChallengeMessage c{};
      h.LockCredChallengeSync(challenge_req(id), cookie, &c, 5000);
      h.SealedAsync(sealed_req(id), cookie);
      ControlUnlockStatusResultMessage r{};
      wait_until([&] { return h.PollResult(cookie, id, &r); }, 5000);
      return r.stage == static_cast<uint16_t>(UnlockStage::Injected) && r.terminal == 0;
    };
    reprobe();

    // (4) Held after the supersede, right before the fresh challenge's write; meanwhile the caller gives up (18,
    // its transaction withdrawn). Let go: the write is NOT started -- 0 challenges, no hold; A keeps its 17.
    check("N1 (4) setup: A typed and waiting", typed_wait(0x91, 91));
    ResetEvent(go);
    holdAt = "challenge";
    const int ch4 = svc.challengeSeen.load();
    ControlUnlockChallengeMessage r4{};
    bool got4 = true;
    std::thread caller4([&] { got4 = h.LockCredRetryChallengeSync(challenge_req(92), 91, 0x91, &r4, 300); });
    const bool held4 = wait_until([&] { return at.load() == 1; }, 3000);
    caller4.join();  // the caller's 300 ms ran out while the worker was held
    SetEvent(go);
    Sleep(600);
    ControlUnlockStatusResultMessage a4{};
    check("N1 (4): held between its fence and the write while the caller gave up (no answer -> 18)", held4 && !got4);
    check("N1 (4): ...let go: THE FRESH CHALLENGE IS NEVER WRITTEN, no hold, nothing kept for the retry",
          svc.challengeSeen.load() == ch4 && !h.PollResult(0x91, 92, &a4) && wait_until([&] { return !svc.connected.load(); }, 3000),
          std::to_string(svc.challengeSeen.load() - ch4) + " challenges");
    check("N1 (4): ...and the earlier request keeps its real end (Superseded)",
          h.PollResult(0x91, 91, &a4) && a4.stage == static_cast<uint16_t>(UnlockStage::Superseded) && a4.terminal == 1,
          "stage=" + std::to_string(a4.stage));
    h.EndSession(0x91);
    holdAt.clear();
    wait_until([&] { return !svc.connected.load(); }, 3000);
    reprobe();

    // (5) Held before the supersede's write while the caller gives up: let go -- nothing written at all, the
    // earlier wait as it was (still held, still waiting).
    check("N1 (5) setup: A typed and waiting", typed_wait(0x93, 93));
    ResetEvent(go);
    holdAt = "supersede";
    const int sup5 = svc.supersedeSeen.load(), ch5 = svc.challengeSeen.load();
    ControlUnlockChallengeMessage r5{};
    bool got5 = true;
    std::thread caller5([&] { got5 = h.LockCredRetryChallengeSync(challenge_req(94), 93, 0x93, &r5, 300); });
    const bool held5 = wait_until([&] { return at.load() == 1; }, 3000);
    caller5.join();
    SetEvent(go);
    Sleep(600);
    ControlUnlockStatusResultMessage a5{};
    check("N1 (5): held before the supersede's write while the caller gave up -> let go: 0 supersede, 0 challenge",
          held5 && !got5 && svc.supersedeSeen.load() == sup5 && svc.challengeSeen.load() == ch5,
          std::to_string(svc.supersedeSeen.load() - sup5) + " supersede, " + std::to_string(svc.challengeSeen.load() - ch5) + " challenge");
    check("N1 (5): ...the earlier wait untouched (Injected, its pipe still held)",
          h.PollResult(0x93, 93, &a5) && a5.stage == static_cast<uint16_t>(UnlockStage::Injected) && a5.terminal == 0 &&
              svc.connected.load());
    h.EndSession(0x93);
    holdAt.clear();
    wait_until([&] { return !svc.connected.load(); }, 3000);
    reprobe();

    // (6) Held before the fresh challenge's write while the SESSION ends: nothing written, nothing kept.
    check("N1 (6) setup: A typed and waiting", typed_wait(0x95, 95));
    ResetEvent(go);
    holdAt = "challenge";
    const int ch6 = svc.challengeSeen.load();
    ControlUnlockChallengeMessage r6{};
    std::thread caller6([&] { (void)h.LockCredRetryChallengeSync(challenge_req(96), 95, 0x95, &r6, 3000); });
    const bool held6 = wait_until([&] { return at.load() == 1; }, 3000);
    h.EndSession(0x95);
    SetEvent(go);
    caller6.join();
    Sleep(600);
    check("N1 (6): the session ended while held before the challenge's write -> 0 challenges, nothing of it kept, no hold",
          held6 && svc.challengeSeen.load() == ch6 && h.KeptEntriesForTest(0x95) == 0 &&
              wait_until([&] { return !svc.connected.load(); }, 3000),
          std::to_string(svc.challengeSeen.load() - ch6) + " challenges, " + std::to_string(h.KeptEntriesForTest(0x95)) + " kept");
    holdAt.clear();
    reprobe();

    // (7) The fresh challenge WAS written and answered (issued); the worker is held before adopting it until the
    // retry's own deadline has passed -- while its caller is still waiting (2000 ms wait; the retry ends 250 ms
    // inside it). Let go: the late "issued" is NOT adopted -- the caller hears Superseded (the earlier wait is
    // over, no new challenge), and the pipe is not held for it.
    check("N1 (7) setup: A typed and waiting", typed_wait(0x97, 97));
    ResetEvent(go);
    holdAt = "adopt";
    ControlUnlockChallengeMessage r7{};
    bool got7 = false;
    const auto t7 = std::chrono::steady_clock::now();
    std::thread caller7([&] { got7 = h.LockCredRetryChallengeSync(challenge_req(98), 97, 0x97, &r7, 2000); });
    const bool held7 = wait_until([&] { return at.load() == 1; }, 3000);
    while (ms_since(t7) < 1850) Sleep(10);  // past the retry's deadline (1750), inside the caller's wait (2000)
    SetEvent(go);
    caller7.join();
    Sleep(300);
    ControlUnlockStatusResultMessage a7{};
    const bool noHold7 = wait_until([&] { return !svc.connected.load(); }, 3000);
    check("N1 (7): an issued challenge adopted after the retry's deadline? NO -- the caller hears Superseded, no hold",
          held7 && got7 && r7.status == static_cast<uint16_t>(UnlockStage::Superseded) && noHold7 &&
              !h.PollResult(0x97, 98, &a7),
          "got=" + std::to_string(got7) + " status=" + std::to_string(r7.status) + " noHold=" + std::to_string(noHold7));
    h.EndSession(0x97);
    holdAt.clear();
    wait_until([&] { return !svc.connected.load(); }, 3000);
    reprobe();

    // (8) lk-ux-retry r4 (Codex N1): the fresh challenge's write starts just before the retry's absolute end (held
    // until ~1650 ms of a 1750 ms end; caller waits 2000) and the service answers late (900 ms): the read must end
    // AT the retry's end -- not at the relative budget worked out before -- so the caller hears 17 before its own
    // wait ends, and nothing is held.
    check("N1 (8) setup: A typed and waiting", typed_wait(0x99, 99));
    ResetEvent(go);
    holdAt = "challenge";
    svc.challengeDelayMs = 900;
    ControlUnlockChallengeMessage r8{};
    bool got8 = false;
    long long ms8 = 0;
    const auto t8 = std::chrono::steady_clock::now();
    std::thread caller8([&] {
      got8 = h.LockCredRetryChallengeSync(challenge_req(100), 99, 0x99, &r8, 2000);
      ms8 = ms_since(t8);
    });
    const bool held8 = wait_until([&] { return at.load() == 1; }, 3000);
    while (ms_since(t8) < 1650) Sleep(5);
    SetEvent(go);
    caller8.join();
    svc.challengeDelayMs = 0;
    Sleep(300);
    check("N1 (8): a write started just before the retry's end waits only TO that end: the caller hears 17 in its wait",
          held8 && got8 && r8.status == static_cast<uint16_t>(UnlockStage::Superseded) && ms8 < 1950,
          "got=" + std::to_string(got8) + " status=" + std::to_string(r8.status) + " in " + std::to_string(ms8) + " ms");
    check("N1 (8): ...and nothing is held for it", wait_until([&] { return !svc.connected.load(); }, 3000));
    h.EndSession(0x99);
    holdAt.clear();
    h.SetRetryIoHookForTest(nullptr);
    CloseHandle(go);
    h.Stop();
  }

  a.Stop();
  b.Stop();
  svc.Stop();
  std::printf("\nconnections served: %d\nRESULT: %s  (%d checks, %d failed)\n", svc.connections.load(),
              gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

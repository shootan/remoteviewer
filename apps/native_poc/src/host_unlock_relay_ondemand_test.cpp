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
  std::atomic<uint16_t> sealedStage{8};    // what a SealedRequest is answered with
  std::atomic<bool> sealedTerminal{false};
  std::atomic<int> sealedSeen{0};
  std::atomic<int> statusSeen{0};
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
      if (stall.load()) continue;  // read it, answer nothing: the relay's deadline must end this
      if (kind == static_cast<uint16_t>(SecureUnlockKind::CapabilitiesRequest)) {
        SecureUnlockCapabilitiesResponse r{};
        r.requestId = requestId;
        r.features = kSecureUnlockFeatureLockCredV1;
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
        SecureUnlockResult r{};
        r.requestId = requestId;
        r.stage = sealedStage.load();
        r.terminal = sealedTerminal.load() ? 1 : 0;
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

  a.Stop();
  b.Stop();
  svc.Stop();
  std::printf("\nconnections served: %d\nRESULT: %s  (%d checks, %d failed)\n", svc.connections.load(),
              gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

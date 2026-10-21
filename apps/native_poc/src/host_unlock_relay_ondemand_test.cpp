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
  const bool aGot = a.LockCredChallengeSync(challenge_req(21), 0xA, &aOut, 6000);
  check("NEGATIVE: while B holds the pipe for its transaction, A's challenge does not get through",
        !aGot || aOut.status != static_cast<uint16_t>(UnlockStage::ChallengeIssued), "status=" + std::to_string(aOut.status));
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
    check("L1: while Y's transaction holds the pipe, Z's request is answered BUSY at once (not run on Y's connection)",
          zGot && cz.status == static_cast<uint16_t>(UnlockStage::InternalError) && ms_since(t0) < 1000,
          std::to_string(ms_since(t0)) + " ms");
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
    check("L5: a silent service: the challenge exchange ends at its deadline (not hung), as a failure",
          got && c.status == static_cast<uint16_t>(UnlockStage::InternalError) && challengeMs < 2500,
          std::to_string(challengeMs) + " ms");
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

  a.Stop();
  b.Stop();
  svc.Stop();
  std::printf("\nconnections served: %d\nRESULT: %s  (%d checks, %d failed)\n", svc.connections.load(),
              gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

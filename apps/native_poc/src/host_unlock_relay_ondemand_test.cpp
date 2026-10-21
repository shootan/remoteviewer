// lockscreen-cred r1 (Codex Q2): the host's unlock relay holds the service's ONE unlock pipe only for a
// transaction. Two real HostUnlockRelay objects (two hosts) against a fake service on a real single-instance
// pipe (the service's shape: one instance, message mode). What it pins:
//   - a capability query lets the pipe go at once: host A probes while its session is idle-locked, and host B
//     can then run its challenge (before r1's fix the relay kept every connection for good);
//   - an issued challenge holds the pipe (the service binds it to the connection) -- B cannot connect meanwhile
//     -- and the final result lets it go; so does the control session ending (EndSession).
// Negative control: a relay whose transaction is still open keeps B out (B's challenge fails within its wait).

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

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

// The service's unlock pipe, one instance, answering what the relay asks. Typed lock-screen jobs stay
// non-terminal until `finishJobs`.
struct FakeService {
  std::wstring name;
  std::atomic<bool> run{true};
  std::atomic<int> connections{0};
  std::atomic<bool> connected{false};
  std::atomic<bool> finishJobs{false};
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
      if (kind == static_cast<uint16_t>(SecureUnlockKind::CapabilitiesRequest)) {
        SecureUnlockCapabilitiesResponse r{};
        r.requestId = requestId;
        r.features = kSecureUnlockFeatureLockCredV1;
        write_msg(pipe, &r, sizeof(r));
      } else if (kind == static_cast<uint16_t>(SecureUnlockKind::LockCredChallengeRequest) ||
                 kind == static_cast<uint16_t>(SecureUnlockKind::ChallengeRequest)) {
        SecureUnlockChallengeResponse r{};
        r.requestId = requestId;
        r.stage = static_cast<uint16_t>(UnlockStage::ChallengeIssued);
        r.purpose = kind == static_cast<uint16_t>(SecureUnlockKind::LockCredChallengeRequest) ? kUnlockPurposeLockCred
                                                                                                 : kUnlockPurposeUnlock;
        write_msg(pipe, &r, sizeof(r));
      } else if (kind == static_cast<uint16_t>(SecureUnlockKind::SealedRequest)) {
        SecureUnlockResult r{};
        r.requestId = requestId;
        r.stage = static_cast<uint16_t>(UnlockStage::Injected);
        r.terminal = 0;
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
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the unlock relay holds the service's single pipe only for a transaction (lockscreen-cred r1, Q2)\n\n");
  FakeService svc;
  svc.Start();
  Sleep(200);

  HostUnlockRelay a, b;
  a.SetVerificationEndpointForTest(svc.name.c_str(), L"GNLinkTestService", trust_any);
  b.SetVerificationEndpointForTest(svc.name.c_str(), L"GNLinkTestService", trust_any);

  // 1. Host A, idle at its lock screen, asks what the service can do -- and lets the pipe go.
  a.ProbeLockCredAsync();
  const bool supported = wait_until([&] { return a.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
  check("A: the capability query is answered (LockCredV1)", supported);
  check("A: ...and the pipe is let go at once (no idle hold)", wait_until([&] { return !svc.connected.load(); }, 2000));

  // 2. Host B can now run a whole lock-screen transaction.
  b.ProbeLockCredAsync();
  wait_until([&] { return b.LockCredSupportNow() == HostUnlockRelay::LockCredSupport::Supported; }, 5000);
  ControlUnlockChallengeMessage out{};
  const bool bChallenged = b.LockCredChallengeSync(challenge_req(11), 0xB, &out, 5000);
  check("B: after A's probe, B's challenge goes through (the pipe was free)",
        bChallenged && out.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued) && out.reserved == kUnlockPurposeLockCred,
        "status=" + std::to_string(out.status));
  check("B: ...and an issued challenge HOLDS the pipe (bound to this connection until its seal)", svc.connected.load());

  // 3. NEGATIVE CONTROL: while B's transaction is open, A cannot connect (single instance) -- its challenge fails.
  ControlUnlockChallengeMessage aOut{};
  const auto t0 = std::chrono::steady_clock::now();
  const bool aGot = a.LockCredChallengeSync(challenge_req(21), 0xA, &aOut, 6000);
  const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  check("NEGATIVE: while B holds the pipe for its transaction, A's challenge does not get through",
        !aGot || aOut.status != static_cast<uint16_t>(UnlockStage::ChallengeIssued),
        "got=" + std::to_string(aGot) + " status=" + std::to_string(aOut.status) + " waitedMs=" + std::to_string(waited));

  // 4. B seals -> typed, still waiting for the unlock: still held. Then the final answer lets it go.
  b.SealedAsync(sealed_req(11), 0xB);
  ControlUnlockStatusResultMessage res{};
  wait_until([&] { return b.PollResult(11, &res); }, 5000);
  check("B: typed (Injected, not final) -- the pipe stays held for the unlock to be seen",
        res.stage == static_cast<uint16_t>(UnlockStage::Injected) && res.terminal == 0 && svc.connected.load());
  svc.finishJobs = true;
  b.RefreshLockCredStatus(11, 3000);
  b.PollResult(11, &res);
  check("B: the final answer (unlocked) arrives ...", res.stage == static_cast<uint16_t>(UnlockStage::SessionUnlocked) && res.terminal == 1);
  check("B: ...and the pipe is let go", wait_until([&] { return !svc.connected.load(); }, 2000));

  // 5. A again: a challenge, then its control session ends -- the hold ends with it, not at the deadline.
  svc.finishJobs = false;
  Sleep(600);
  ControlUnlockChallengeMessage a2{};
  const bool aAgain = a.LockCredChallengeSync(challenge_req(31), 0xA2, &a2, 5000);
  check("A: its challenge goes through now", aAgain && a2.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued));
  a.EndSession(0xA2);
  check("A: its control session ended -> the pipe is let go at once", wait_until([&] { return !svc.connected.load(); }, 2000));
  check("...another session's end does not touch a held transaction (control)", [&] {
    ControlUnlockChallengeMessage b2{};
    const bool ok = b.LockCredChallengeSync(challenge_req(41), 0xB2, &b2, 5000);
    b.EndSession(0xDEAD);
    Sleep(300);
    const bool stillHeld = svc.connected.load();
    b.EndSession(0xB2);
    return ok && stillHeld;
  }());

  a.Stop();
  b.Stop();
  svc.Stop();
  std::printf("\nconnections served: %d\nRESULT: %s  (%d checks, %d failed)\n", svc.connections.load(),
              gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

// Pre-publish isolated gate for the TWO product clients (secure-pipe-identity B2): the REAL
// SecureInputBrokerClient and HostUnlockRelay code, pointed at a TEST pipe/service name + an injected
// gatherer, drive their verified-connect path against a test-owned protocol sink. No installed service,
// no screen SendInput, no stored credential. A not-trusted endpoint must get 0 bytes / no handle; a
// mutation that forces trust (the "check removed" variant) delivers -- so the refusal is load-bearing.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "host_unlock_relay.hpp"
#include "poc_protocol.hpp"
#include "secure_input_broker.hpp"
#include "secure_input_protocol.hpp"
#include "trusted_service_pipe.hpp"

using namespace remote60::native_poc;

namespace {
int gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what);
  if (!ok) { ++gFailures; if (!detail.empty()) std::printf("        %s\n", detail.c_str()); }
}

constexpr uint64_t kSyntheticHostId = 0x5EED5EED5EEDull;
constexpr uint64_t kSyntheticExpiresMs = 777777;

// Injected gatherer: the test decides the verdict for its own test endpoint (the real SCM-backed
// gatherer is proven under UAC + in trusted_service_pipe_test; here we drive the CLIENT paths).
std::atomic<bool> g_trusted{false};
ServerIdentityFacts gate_gather(void*, const wchar_t*) {
  ServerIdentityFacts f;
  f.serverPidKnown = true;
  f.serverPid = 100;
  f.serviceRunning = g_trusted.load();
  f.servicePid = g_trusted.load() ? 100u : 101u;  // mismatch when not trusted
  f.serverImageKnown = true;
  f.serviceBinaryKnown = true;
  f.imageSameFile = g_trusted.load();
  f.finalConsistent = g_trusted.load();
  return f;
}

struct Sink {
  // Drain: count input bytes; Unlock: answer a ChallengeRequest with ChallengeIssued + synthetic fields;
  // UnlockCorrupt: answer with a damaged reply (bad magic); UnlockSilent: never answer (then hang up).
  enum class Mode { Drain, Unlock, UnlockCorrupt, UnlockSilent };
  std::wstring name;
  Mode mode = Mode::Drain;
  std::thread th;
  std::atomic<bool> stop{false};
  std::atomic<int> bytes{0};
  void Start(const std::wstring& n, Mode m = Mode::Drain) {
    name = n;
    mode = m;
    th = std::thread([this] {
      while (!stop.load()) {
        HANDLE p = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
                                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 4, 8192, 8192,
                                    0, nullptr);
        if (p == INVALID_HANDLE_VALUE) { Sleep(10); continue; }
        const BOOL c = ConnectNamedPipe(p, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (c && !stop.load()) {
          uint8_t buf[1024];
          DWORD got = 0;
          if (ReadFile(p, buf, sizeof(buf), &got, nullptr) && got > 0) {
            bytes.fetch_add((int)got);
            if ((mode == Mode::Unlock || mode == Mode::UnlockCorrupt) && got >= sizeof(SecureUnlockChallengeRequest)) {
              // r6 D2: a ChallengeIssued reply with fields only this fixture would put there (no real
              // crypto / no stored password), echoing the requestId.
              SecureUnlockChallengeRequest req{};
              std::memcpy(&req, buf, sizeof(req));
              SecureUnlockChallengeResponse resp{};  // magic/size/kind defaulted
              resp.requestId = req.requestId;
              resp.stage = static_cast<uint16_t>(UnlockStage::ChallengeIssued);
              resp.hostId = kSyntheticHostId;
              resp.expiresMs = kSyntheticExpiresMs;
              std::memset(resp.challengeId, 0xA5, sizeof(resp.challengeId));
              if (mode == Mode::UnlockCorrupt) resp.magic ^= 0xFFu;  // the damaged reply
              DWORD wrote = 0;
              WriteFile(p, &resp, sizeof(resp), &wrote, nullptr);
            } else if (mode == Mode::UnlockSilent) {
              Sleep(300);  // the reply never comes; the connection is then dropped
            }
          }
        }
        DisconnectNamedPipe(p);
        CloseHandle(p);
      }
    });
  }
  void Stop() {
    stop.store(true);
    if (th.joinable()) {
      HANDLE h = th.native_handle();
      while (WaitForSingleObject(h, 50) == WAIT_TIMEOUT) {
        HANDLE w = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0,
                               nullptr);
        if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
      }
      th.join();
    }
  }
};
}  // namespace

// THE broker assertion, used identically by the fixed run and the --mutant child: the input broker,
// against this endpoint, must NOT deliver (send fails and the sink gets 0 bytes). In the mutant the
// gatherer is forced Trusted (verification neutralised) so the real broker DELIVERS -> this same
// assertion FAILS -> the mutant process exits non-zero. That is the load-bearing negative control.
bool broker_refuses(const std::wstring& pipe, const wchar_t* svc, Sink& sink, bool verificationNeutralised) {
  g_trusted.store(verificationNeutralised);  // mutant: trusted(check removed); fixed: not-trusted
  sink.bytes.store(0);
  ControlInputEventMessage ev{};
  ev.kind = 1;
  ev.x = 42;
  SecureInputBrokerClient broker;
  broker.SetVerificationEndpointForTest(pipe.c_str(), svc, &gate_gather);
  const bool sent = broker.SendInputEvent(ev, 1920, 1080);
  Sleep(140);
  return !sent && sink.bytes.load() == 0;  // the one assertion
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const bool mutant = argc > 1 && std::string(argv[1]) == "--mutant";
  const std::wstring pipe = L"\\\\.\\pipe\\gnlink-gate-" + std::to_wstring(GetCurrentProcessId());
  const wchar_t* svc = L"GNLinkGateTestService";  // a name no SCM has; the injected gatherer decides
  Sink sink;
  sink.Start(pipe, Sink::Mode::Drain);
  Sleep(100);

  // --- MUTANT child: run ONLY the shared broker assertion with verification neutralised. The broker
  // then delivers, so the assertion is false -> this process exits non-zero. (Driven by the fixed run.)
  if (mutant) {
    const bool held = broker_refuses(pipe, svc, sink, /*verificationNeutralised=*/true);
    check("[mutant] broker_refuses holds with verification removed", held);  // expected to FAIL here
    sink.Stop();
    return gFailures == 0 ? 0 : 1;
  }

  std::printf("secure-pipe client gate (real broker + relay, test endpoint)\n");
  ControlInputEventMessage ev{};
  ev.kind = 1;
  ev.x = 42;
  ev.y = 42;

  // --- input broker: a TRUSTED endpoint DELIVERS the input to the sink ---------------------------
  g_trusted.store(true);
  sink.bytes.store(0);
  {
    SecureInputBrokerClient broker;
    broker.SetVerificationEndpointForTest(pipe.c_str(), svc, &gate_gather);
    const bool sent = broker.SendInputEvent(ev, 1920, 1080);
    Sleep(140);
    check("broker: a TRUSTED endpoint accepts the verified send", sent);
    check("broker: the input reached the sink (trusted path works)", sink.bytes.load() > 0,
          std::to_string(sink.bytes.load()) + " bytes");
  }

  // --- input broker: the SAME assertion holds on the real (not-trusted) reject ------------------
  check("broker: NEGATIVE a NOT-trusted endpoint delivers nothing (send fails, 0 bytes)",
        broker_refuses(pipe, svc, sink, /*verificationNeutralised=*/false));

  // --- mutation: the SAME assertion run with verification removed must FAIL -> the child exits !=0 -
  {
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring cmd = std::wstring(L"\"") + self + L"\" --mutant";
    std::vector<wchar_t> mut(cmd.begin(), cmd.end());
    mut.push_back(L'\0');
    // r6 D2: the child's verdict is read, not assumed: it ended (WAIT_OBJECT_0), its exit code was read, it
    // is exactly 1 (the assertion's failure -- not a hang's STILL_ACTIVE, not a crash's status), and its
    // own output names THAT assertion as failed. A child that does not end is ended and the case fails.
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE outRead = nullptr, outWrite = nullptr;
    CreatePipe(&outRead, &outWrite, &inherit, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = outWrite;
    si.hStdError = outWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    DWORD code = 0;
    const bool launched = CreateProcessW(nullptr, mut.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                          nullptr, nullptr, &si, &pi) != FALSE;
    CloseHandle(outWrite);
    std::string childOut;
    std::thread reader([&] {
      char b[512];
      DWORD n = 0;
      while (ReadFile(outRead, b, sizeof(b), &n, nullptr) && n > 0) childOut.append(b, n);
    });
    DWORD waited = WAIT_FAILED;
    bool codeRead = false;
    if (launched) {
      waited = WaitForSingleObject(pi.hProcess, 15000);
      if (waited != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 99);  // ours: reclaimed, and the case fails below
        WaitForSingleObject(pi.hProcess, 5000);
      }
      codeRead = GetExitCodeProcess(pi.hProcess, &code) != FALSE;
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    reader.join();
    CloseHandle(outRead);
    const bool assertionFailed = childOut.find("FAIL  [mutant] broker_refuses") != std::string::npos;
    check("broker: mutation -- the SAME assertion FAILS with verification removed (ended, exit exactly 1, "
          "that assertion reported failed)",
          launched && waited == WAIT_OBJECT_0 && codeRead && code == 1 && assertionFailed,
          "wait=" + std::to_string(waited) + " exit=" + std::to_string(code) + " assertionFailed=" +
              std::to_string(assertionFailed ? 1 : 0));
  }

  // --- unlock relay: a REAL ChallengeSync round trip over the verified pipe ----------------------
  // r6 D2: the positive is the service's ACTUAL answer arriving through the relay -- ChallengeIssued and
  // the fixture's own synthetic fields -- so a damaged or missing reply (which the relay turns into an
  // InternalError result for the same requestId) does not pass it.
  auto relay_round_trip = [&](Sink::Mode mode, uint32_t requestId, std::string* detail) {
    const std::wstring upipe = L"\\\\.\\pipe\\gnlink-gate-unlock-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                               std::to_wstring(requestId);
    Sink usink;
    usink.Start(upipe, mode);
    Sleep(80);
    g_trusted.store(true);
    HostUnlockRelay relay;
    relay.SetVerificationEndpointForTest(upipe.c_str(), svc, &gate_gather);
    ControlUnlockChallengeRequestMessage req{};
    req.requestId = requestId;
    ControlUnlockChallengeMessage out{};
    const bool got = relay.ChallengeSync(req, /*cookie=*/1234, &out, 6000);
    uint8_t want[sizeof(out.challengeId)];
    std::memset(want, 0xA5, sizeof(want));
    const bool issued = got && out.requestId == requestId &&
                        out.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued) &&
                        out.hostId == kSyntheticHostId && out.expiresMs == kSyntheticExpiresMs &&
                        std::memcmp(out.challengeId, want, sizeof(want)) == 0;
    *detail = "got=" + std::to_string(got) + " status=" + std::to_string(out.status) + " sinkBytes=" +
              std::to_string(usink.bytes.load());
    const bool sinkHeard = usink.bytes.load() > 0;
    relay.Stop();
    usink.Stop();
    return issued && sinkHeard;
  };
  std::string d;
  check("relay: a TRUSTED endpoint's ChallengeIssued, with the fixture's fields, comes back through the real WorkerLoop",
        relay_round_trip(Sink::Mode::Unlock, 7, &d), d);
  check("relay: control -- a DAMAGED reply does not pass that positive", !relay_round_trip(Sink::Mode::UnlockCorrupt, 9, &d), d);
  check("relay: control -- NO reply does not pass that positive", !relay_round_trip(Sink::Mode::UnlockSilent, 10, &d), d);

  // NEGATIVE: within a bound that includes the connect budget (3 s), an EXPLICIT refusal comes back
  // (InternalError for the request) and the fake sink got nothing -- also after the worker is gone.
  {
    const std::wstring upipe = L"\\\\.\\pipe\\gnlink-gate-unlock-" + std::to_wstring(GetCurrentProcessId()) + L"-neg";
    Sink usink;
    usink.Start(upipe, Sink::Mode::Unlock);
    Sleep(80);
    g_trusted.store(false);
    usink.bytes.store(0);
    HostUnlockRelay relay;
    relay.SetVerificationEndpointForTest(upipe.c_str(), svc, &gate_gather);
    ControlUnlockChallengeRequestMessage req{};
    req.requestId = 8;
    ControlUnlockChallengeMessage out{};
    const auto t0 = GetTickCount64();
    const bool got = relay.ChallengeSync(req, 1234, &out, 8000);
    const auto ms = GetTickCount64() - t0;
    check("relay: NEGATIVE a NOT-trusted endpoint is refused explicitly in time (InternalError result, <8 s)",
          got && out.requestId == 8 && out.status == static_cast<uint16_t>(UnlockStage::InternalError),
          "got=" + std::to_string(got) + " status=" + std::to_string(out.status) + " " + std::to_string(ms) + " ms");
    check("relay: NEGATIVE the fake sink received 0 bytes (no ChallengeRequest leaked)",
          usink.bytes.load() == 0, std::to_string(usink.bytes.load()) + " bytes");
    relay.Stop();
    Sleep(300);
    check("relay: NEGATIVE ...and still 0 bytes after the relay's worker has ended", usink.bytes.load() == 0,
          std::to_string(usink.bytes.load()) + " bytes");
    usink.Stop();
  }

  sink.Stop();
  if (gFailures != 0) { std::printf("secure_pipe_client_gate_test: FAIL (%d)\n", gFailures); return 1; }
  std::printf("secure_pipe_client_gate_test: PASS\n");
  return 0;
}

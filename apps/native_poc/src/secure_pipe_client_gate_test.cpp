// Pre-publish isolated gate for the TWO product clients (secure-pipe-identity B2): the REAL
// SecureInputBrokerClient and HostUnlockRelay code, pointed at a TEST pipe/service name + an injected
// gatherer, drive their verified-connect path against a test-owned protocol sink. No installed service,
// no screen SendInput, no stored credential. A not-trusted endpoint must get 0 bytes / no handle; a
// mutation that forces trust (the "check removed" variant) delivers -- so the refusal is load-bearing.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>

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
  std::wstring name;
  std::thread th;
  std::atomic<bool> stop{false};
  std::atomic<int> bytes{0};
  void Start(const std::wstring& n) {
    name = n;
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
          if (ReadFile(p, buf, sizeof(buf), &got, nullptr) && got > 0) bytes.fetch_add((int)got);
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

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("secure-pipe client gate (real broker + relay, test endpoint)\n");

  const std::wstring pipe = L"\\\\.\\pipe\\gnlink-gate-" + std::to_wstring(GetCurrentProcessId());
  const wchar_t* svc = L"GNLinkGateTestService";  // a name no SCM has; the injected gatherer decides
  Sink sink;
  sink.Start(pipe);
  Sleep(100);

  ControlInputEventMessage ev{};  // a synthetic input marker; contents do not matter to the sink
  ev.kind = 1;
  ev.x = 42;
  ev.y = 42;

  // --- input broker: trusted endpoint DELIVERS the input to the sink -----------------------------
  g_trusted.store(true);
  {
    SecureInputBrokerClient broker;
    broker.SetVerificationEndpointForTest(pipe.c_str(), svc, &gate_gather);
    const bool sent = broker.SendInputEvent(ev, 1920, 1080);
    Sleep(120);
    check("broker: a TRUSTED endpoint accepts the verified send", sent);
    check("broker: the input reached the sink (trusted path works)", sink.bytes.load() > 0,
          std::to_string(sink.bytes.load()) + " bytes");
  }

  // --- input broker: NOT-trusted endpoint -> 0 bytes (the product refusal) -----------------------
  sink.bytes.store(0);
  g_trusted.store(false);
  {
    SecureInputBrokerClient broker;
    broker.SetVerificationEndpointForTest(pipe.c_str(), svc, &gate_gather);
    const bool sent = broker.SendInputEvent(ev, 1920, 1080);
    Sleep(120);
    check("broker: NEGATIVE a NOT-trusted endpoint is refused (send fails)", !sent);
    check("broker: NEGATIVE the sink received 0 input bytes (no input leaked to a fake)",
          sink.bytes.load() == 0, std::to_string(sink.bytes.load()) + " bytes");
  }

  // --- mutation (check removed): force the gatherer Trusted against the same endpoint -> DELIVERS.
  // This is the same product send path with verification neutralised; the "0 bytes" property above
  // then FAILS, which is exactly why the verification is load-bearing.
  sink.bytes.store(0);
  g_trusted.store(true);
  {
    SecureInputBrokerClient broker;
    broker.SetVerificationEndpointForTest(pipe.c_str(), svc, &gate_gather);
    (void)broker.SendInputEvent(ev, 1920, 1080);
    Sleep(120);
    check("broker: mutation -- a forced-trusted gatherer DELIVERS (verification is load-bearing)",
          sink.bytes.load() > 0, std::to_string(sink.bytes.load()) + " bytes");
  }

  // --- unlock relay: the verified connect gate (Trusted yields a handle, not-trusted none) -------
  g_trusted.store(true);
  {
    HostUnlockRelay relay;
    relay.SetVerificationEndpointForTest(pipe.c_str(), svc, &gate_gather);
    check("relay: a TRUSTED endpoint yields a verified connection", relay.ProbeConnectForTest());
    relay.Stop();
  }
  g_trusted.store(false);
  {
    HostUnlockRelay relay;
    relay.SetVerificationEndpointForTest(pipe.c_str(), svc, &gate_gather);
    check("relay: NEGATIVE a NOT-trusted endpoint yields no connection", !relay.ProbeConnectForTest());
    relay.Stop();
  }

  sink.Stop();
  if (gFailures != 0) { std::printf("secure_pipe_client_gate_test: FAIL (%d)\n", gFailures); return 1; }
  std::printf("secure_pipe_client_gate_test: PASS\n");
  return 0;
}

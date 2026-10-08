// prelogin-ui r14 (M1): the secure-input pipe serves more than one client, and the host's broker does not
// wait on the input path.
//
// The case: fast user switching. The elevated GNLinkStream of a user whose session is now disconnected keeps
// its connection to the service's one-instance pipe; the console's pre-login worker (or the new user's
// host) then waited ~3 s in its broker on every input -- on the UDP reader thread -- and lost it. Here the
// product's pipe instance (CreateSecureInputPipeInstance), server loop (RunMultiClientPipeServer) and broker
// (SecureInputBrokerClient) run on a test pipe: A holds a connection without sending; B's input still
// arrives at once. The service's per-connection forwarding to its agent is not run here (SYSTEM / G).

#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "secure_input_broker.hpp"
#include "secure_input_pipe_server.hpp"

namespace n = remote60::native_poc;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
bool wait_until(const std::function<bool()>& done, int ms) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    if (done()) return true;
    Sleep(20);
  }
  return done();
}
std::wstring user_sddl() {
  HANDLE tok = nullptr;
  OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
  DWORD need = 0;
  GetTokenInformation(tok, TokenUser, nullptr, 0, &need);
  std::string buf(need, '\0');
  GetTokenInformation(tok, TokenUser, buf.data(), need, &need);
  CloseHandle(tok);
  LPWSTR sid = nullptr;
  ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &sid);
  std::wstring s = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid) + L")";
  LocalFree(sid);
  return s;
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the secure-input pipe with more than one client (prelogin-ui r14, M1)\n\n");
  const std::wstring name = L"\\\\.\\pipe\\GNLinkTest-secure-input-" + std::to_wstring(GetCurrentProcessId());
  const std::wstring sddl = user_sddl();
  std::mutex mu;
  std::vector<uint32_t> keysSeen;  // keyCode of every message the server read, in order
  std::atomic<int> connections{0}, served{0};
  std::atomic<bool> running{true};
  n::PipeConnectionRegistry registry;
  std::thread server([&] {
    n::RunMultiClientPipeServer(
        [&] { return running.load(); }, [&] { return n::CreateSecureInputPipeInstance(name.c_str(), sddl.c_str()); },
        [&](HANDLE pipe) {
          ++connections;
          for (;;) {
            n::SecureInputMessage m{};
            DWORD got = 0;
            if (!ReadFile(pipe, &m, sizeof(m), &got, nullptr) || got != sizeof(m)) break;
            std::lock_guard<std::mutex> l(mu);
            keysSeen.push_back(m.keyCode);
          }
          ++served;
        },
        &registry);
  });
  Sleep(200);

  // A: the disconnected user's streaming host -- connected, one input, then it holds the pipe.
  HANDLE a = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  n::SecureInputMessage am{};
  am.magic = n::kSecureInputMagic;
  am.size = sizeof(am);
  am.keyCode = 0xA;
  DWORD wrote = 0;
  const bool aSent = a != INVALID_HANDLE_VALUE && WriteFile(a, &am, sizeof(am), &wrote, nullptr);
  check("A connects and sends, then keeps its connection", aSent && wait_until([&] {
                                                              std::lock_guard<std::mutex> l(mu);
                                                              return keysSeen.size() == 1;
                                                            }, 2000));

  // B: the console's worker / the new user's host, through the product's broker, while A holds on.
  n::SecureInputBrokerClient b(name);
  n::ControlInputEventMessage key{};
  key.kind = 5;
  key.keyCode = 0xB;
  const auto t0 = std::chrono::steady_clock::now();
  const bool bSent = b.SendInputEvent(key, 1920, 1080);
  const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  const bool bArrived = wait_until([&] {
    std::lock_guard<std::mutex> l(mu);
    return keysSeen.size() == 2 && keysSeen[1] == 0xB;
  }, 2000);
  check("M1: while A holds its connection, B's input is taken at once (r13: one instance -- busy, lost after ~3 s)",
        bSent && bArrived && took < 500, std::to_string(took) + " ms");
  check("M1: ...each on its own connection (2 served at once)", connections.load() == 2,
        std::to_string(connections.load()));

  // The broker on the input path never waits: no service pipe at all.
  {
    n::SecureInputBrokerClient none(L"\\\\.\\pipe\\GNLinkTest-secure-input-absent-" + std::to_wstring(GetCurrentProcessId()));
    const auto s0 = std::chrono::steady_clock::now();
    int ok = 0;
    for (int i = 0; i < 5; ++i) ok += none.SendInputEvent(key, 1920, 1080) ? 1 : 0;
    const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s0).count();
    check("M1: with no service pipe, five inputs fail at once on the input path (r13: ~3 s each, on the UDP reader thread)",
          ok == 0 && spent < 300, std::to_string(spent) + " ms");
  }

  // Stop: the listening instance and A's idle connection both end; every connection thread is joined.
  const auto s0 = std::chrono::steady_clock::now();
  running = false;
  registry.Stop();
  server.join();
  const auto stopMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s0).count();
  check("M1: stop ends the listening instance and A's idle connection; every connection thread joined",
        stopMs < 2000 && served.load() == connections.load() && registry.size() == 0,
        std::to_string(stopMs) + " ms, " + std::to_string(served.load()) + "/" + std::to_string(connections.load()));
  if (a != INVALID_HANDLE_VALUE) CloseHandle(a);
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

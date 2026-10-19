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

#include <algorithm>
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
// r20 merge (secure-pipe-identity): the broker verifies the pipe's server before writing. The test's own server
// stands in for the service through the gatherer seam; `gTrusted` false makes it a confirmed stranger.
std::atomic<bool> gTrusted{true};
std::atomic<int> gGatherCalls{0};
n::ServerIdentityFacts test_gather(void*, const wchar_t*) {
  ++gGatherCalls;
  n::ServerIdentityFacts f;
  f.serverPidKnown = true;
  f.serverPid = 100;
  f.serviceRunning = true;
  f.servicePid = gTrusted.load() ? 100u : 101u;
  f.serverImageKnown = true;
  f.serviceBinaryKnown = true;
  f.imageSameFile = gTrusted.load();
  f.finalConsistent = gTrusted.load();
  return f;
}
const wchar_t kTestService[] = L"GNLinkPipeServerTestService";  // no SCM has it; the gatherer decides
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
        [&] { return running.load(); }, [&](bool first) { return n::CreateSecureInputPipeInstance(name.c_str(), sddl.c_str(), first); },
        [&](HANDLE pipe, HANDLE stop) {
          ++connections;
          for (;;) {
            n::SecureInputMessage m{};
            if (!n::ReadPipeMessage(pipe, &m, sizeof(m), stop)) break;
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
  b.SetVerificationEndpointForTest(name.c_str(), kTestService, &test_gather);
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

  // r20 merge (Codex Q1): the input path's own (re)connect verifies the server too -- a stranger on the name gets
  // nothing, the verification is actually called, and a refusal holds the next try back (cooldown).
  {
    gTrusted = false;
    gGatherCalls = 0;
    n::SecureInputBrokerClient s(name);
    s.SetVerificationEndpointForTest(name.c_str(), kTestService, &test_gather);
    n::ControlInputEventMessage k2 = key;
    k2.keyCode = 0xC;
    const auto f0 = std::chrono::steady_clock::now();
    const bool sent = s.SendInputEvent(k2, 1920, 1080);
    const auto refuseMs =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - f0).count() / 1000.0;
    const int callsAfterFirst = gGatherCalls.load();
    const bool sentAgain = s.SendInputEvent(k2, 1920, 1080);  // inside the cooldown: no new open, no new check
    const int callsInCooldown = gGatherCalls.load();
    Sleep(1100);
    gTrusted = true;
    const auto g0 = std::chrono::steady_clock::now();
    const bool sentAfter = s.SendInputEvent(k2, 1920, 1080);  // after it: verified again, now trusted
    const auto trustMs =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - g0).count() / 1000.0;
    const bool arrived = wait_until([&] {
      std::lock_guard<std::mutex> l(mu);
      return keysSeen.size() == 3 && keysSeen[2] == 0xC;
    }, 2000);
    {
      std::lock_guard<std::mutex> l(mu);
      check("r20 Q1: the input path's connect refuses a server that is not the service (0 bytes written)",
            !sent && !sentAgain && keysSeen.size() == 3 && keysSeen[2] == 0xC && arrived);
    }
    check("r20 Q1: ...the identity check ran on that path (1 call), none inside the cooldown, again after it",
          callsAfterFirst == 1 && callsInCooldown == 1 && gGatherCalls.load() == 2 && sentAfter,
          "calls " + std::to_string(callsAfterFirst) + "/" + std::to_string(callsInCooldown) + "/" +
              std::to_string(gGatherCalls.load()));
    char lat[160];
    std::snprintf(lat, sizeof(lat), "fast-path latency (test gatherer): refused %.2f ms, trusted reconnect+send %.2f ms",
                  refuseMs, trustMs);
    std::printf("INFO  %s\n", lat);
  }
  // r20 (Codex Q1): the same path with the PRODUCT gatherer -- the test's pipe server is no installed service, so
  // the check fails (Unavailable at the SCM lookup) and nothing is written. Its real cost on this thread:
  {
    n::SecureInputBrokerClient real(name);
    real.SetVerificationEndpointForTest(name.c_str(), kTestService, nullptr);
    n::ControlInputEventMessage k3 = key;
    k3.keyCode = 0xD;
    const auto r0 = std::chrono::steady_clock::now();
    const bool sent = real.SendInputEvent(k3, 1920, 1080);
    const auto ms =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - r0).count() / 1000.0;
    Sleep(200);
    std::lock_guard<std::mutex> l(mu);
    check("r20 Q1: with the product's gatherer a non-service server gets 0 bytes", !sent && keysSeen.size() == 3);
    std::printf("INFO  fast-path latency (product gatherer, identity failure): %.2f ms\n", ms);
  }
  // r20 (Codex Q1): a BUSY name (every instance connected -- at the product server only for a moment, it keeps a
  // listener in reserve): the fast path's wait for it is the 1 ms bound, then the cooldown. On a 1-instance pipe.
  {
    const std::wstring bn = L"\\\\.\\pipe\\GNLinkTest-secure-input-busy-" + std::to_wstring(GetCurrentProcessId());
    HANDLE only = CreateNamedPipeW(bn.c_str(), PIPE_ACCESS_INBOUND, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                   1, 0, 4096, 0, nullptr);
    HANDLE taker = CreateFileW(bn.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    n::SecureInputBrokerClient busy(bn);
    busy.SetVerificationEndpointForTest(bn.c_str(), kTestService, &test_gather);
    const auto b0 = std::chrono::steady_clock::now();
    const bool sent = busy.SendInputEvent(key, 1920, 1080);
    const auto ms =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - b0).count() / 1000.0;
    check("r20 Q1: a busy name fails the fast path at once (nothing sent)",
          only != INVALID_HANDLE_VALUE && taker != INVALID_HANDLE_VALUE && !sent && ms < 100.0);
    std::printf("INFO  fast-path latency (busy name): %.2f ms\n", ms);
    if (taker != INVALID_HANDLE_VALUE) CloseHandle(taker);
    if (only != INVALID_HANDLE_VALUE) CloseHandle(only);
  }

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

  // r15 R14-1: Stop at each point of the listening instance's life -- before it is registered, between its
  // registration and its connect, while the connect waits, while a connection's read waits. No client
  // comes; the server must return (every thread joined) within a bound each time. r14's synchronous
  // connect started after a Stop waited for a client for ever.
  auto stop_case = [&](const char* what, int point) {
    const std::wstring nm = L"\\\\.\\pipe\\GNLinkTest-secure-input-stop-" + std::to_wstring(GetCurrentProcessId()) +
                            L"-" + std::to_wstring(point);
    n::PipeConnectionRegistry reg;
    std::atomic<bool> run{true};
    std::atomic<int> conns{0};
    if (point == 0) reg.Stop();                                  // before anything is registered
    if (point == 1) reg.beforeConnect = [&reg] { reg.Stop(); };  // registered, connect not yet begun
    std::atomic<bool> returned{false};
    std::thread srv([&] {
      n::RunMultiClientPipeServer(
          [&] { return run.load(); }, [&](bool first) { return n::CreateSecureInputPipeInstance(nm.c_str(), sddl.c_str(), first); },
          [&](HANDLE pipe, HANDLE stop) {
            ++conns;
            n::SecureInputMessage m{};
            while (n::ReadPipeMessage(pipe, &m, sizeof(m), stop)) {
            }
          },
          &reg);
      returned = true;
    });
    HANDLE c = INVALID_HANDLE_VALUE;
    if (point == 3) {  // a connection whose read waits
      wait_until([&] { return (c = CreateFileW(nm.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr)) != INVALID_HANDLE_VALUE; }, 2000);
      wait_until([&] { return conns.load() == 1; }, 2000);
      Sleep(200);
    }
    if (point == 2) Sleep(300);  // the listening instance's connect is pending
    const auto t0 = std::chrono::steady_clock::now();
    reg.Stop();
    const bool ended = wait_until([&] { return returned.load(); }, 3000);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    if (!ended) {
      check(what, false, "the server did not return within 3 s (a wait nothing can end)");
      std::printf("\nRESULT: FAILED  (%d checks, %d failed)\n", gChecks, gFailures);
      std::fflush(stdout);
      TerminateProcess(GetCurrentProcess(), 1);
    }
    srv.join();
    if (c != INVALID_HANDLE_VALUE) CloseHandle(c);
    check(what, ended && reg.size() == 0, std::to_string(ms) + " ms");
  };
  stop_case("R14-1: Stop before the listening instance is registered: the server returns", 0);
  stop_case("R14-1: Stop between its registration and its connect (the r14 window): the server returns", 1);
  stop_case("R14-1: Stop while its connect waits for a client: the server returns", 2);
  stop_case("R14-1: Stop while a connection's read waits: the connection and the server return", 3);

  // r20 merge (secure-pipe-identity): a name squatted before the server starts -> the server's first instance
  // (FILE_FLAG_FIRST_PIPE_INSTANCE) fails, the server serves nothing and returns; it never joins the name.
  {
    const std::wstring nm = L"\\\\.\\pipe\\GNLinkTest-secure-input-squat-" + std::to_wstring(GetCurrentProcessId());
    HANDLE squat = CreateNamedPipeW(nm.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 0,
                                    64 * 1024, 0, nullptr);
    n::PipeConnectionRegistry reg;
    std::atomic<int> creates{0}, conns{0};
    std::atomic<bool> returned{false};
    std::thread srv([&] {
      n::RunMultiClientPipeServer(
          [] { return true; },
          [&](bool first) {
            ++creates;
            return n::CreateSecureInputPipeInstance(nm.c_str(), sddl.c_str(), first);
          },
          [&](HANDLE, HANDLE) { ++conns; }, &reg);
      returned = true;
    });
    const bool ended = wait_until([&] { return returned.load(); }, 2000);
    if (!ended) reg.Stop();
    srv.join();
    check("r20: a squatted name -> the first instance fails, nothing is served, the server returns",
          squat != INVALID_HANDLE_VALUE && ended && creates.load() == 1 && conns.load() == 0 && reg.size() == 0,
          "creates=" + std::to_string(creates.load()));
    if (squat != INVALID_HANDLE_VALUE) CloseHandle(squat);
  }

  // r20 merge (Codex Q2 evidence 3): the cap -- 7 served + 1 listener. An 8th client is refused while the 7 go on;
  // a slot handed back is taken again; connect/close churn keeps the name ours; STOP still ends everything.
  {
    const std::wstring nm = L"\\\\.\\pipe\\GNLinkTest-secure-input-cap-" + std::to_wstring(GetCurrentProcessId());
    n::PipeConnectionRegistry reg;
    std::atomic<int> served{0}, active{0};
    std::mutex km;
    std::vector<uint32_t> keys;
    std::vector<std::string> notes;
    std::atomic<bool> returned{false};
    std::thread srv([&] {
      n::RunMultiClientPipeServer(
          [] { return true; },
          [&](bool first) { return n::CreateSecureInputPipeInstance(nm.c_str(), sddl.c_str(), first); },
          [&](HANDLE pipe, HANDLE stop) {
            ++served;
            ++active;
            n::SecureInputMessage m{};
            while (n::ReadPipeMessage(pipe, &m, sizeof(m), stop)) {
              std::lock_guard<std::mutex> l(km);
              keys.push_back(m.keyCode);
            }
            --active;
          },
          &reg, [&](const char* line) {
            std::lock_guard<std::mutex> l(km);
            notes.push_back(line);
          });
      returned = true;
    });
    auto open_client = [&]() -> HANDLE {
      HANDLE h = INVALID_HANDLE_VALUE;
      wait_until([&] {
        h = CreateFileW(nm.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY) WaitNamedPipeW(nm.c_str(), 50);
        return h != INVALID_HANDLE_VALUE;
      }, 2000);
      return h;
    };
    auto send_key = [&](HANDLE h, uint32_t k) {
      n::SecureInputMessage m{};
      m.magic = n::kSecureInputMagic;
      m.size = sizeof(m);
      m.keyCode = k;
      DWORD w = 0;
      return h != INVALID_HANDLE_VALUE && WriteFile(h, &m, sizeof(m), &w, nullptr) && w == sizeof(m);
    };
    auto has_key = [&](uint32_t k) {
      std::lock_guard<std::mutex> l(km);
      return std::find(keys.begin(), keys.end(), k) != keys.end();
    };
    std::vector<HANDLE> held;
    for (int i = 0; i < 7; ++i) held.push_back(open_client());
    const bool sevenUp = wait_until([&] { return active.load() == 7; }, 3000);
    // The 8th: it reaches the listener, which refuses it (disconnects) and stays.
    HANDLE eighth = open_client();
    Sleep(300);
    const bool eighthRefused = eighth != INVALID_HANDLE_VALUE && !send_key(eighth, 0x80) && active.load() == 7;
    if (eighth != INVALID_HANDLE_VALUE) CloseHandle(eighth);
    bool sevenStillServed = true;
    for (int i = 0; i < 7; ++i) sevenStillServed = sevenStillServed && send_key(held[i], 0x70 + i);
    sevenStillServed = sevenStillServed && wait_until([&] { return has_key(0x70) && has_key(0x76); }, 2000);
    bool limitNoted = false;
    {
      std::lock_guard<std::mutex> l(km);
      for (const auto& line : notes) limitNoted = limitNoted || line.find("limit reached") != std::string::npos;
    }
    check("r20 Q2: 7 served at once; the 8th is refused while the 7 go on (no server stop, the limit noted)",
          sevenUp && eighthRefused && sevenStillServed && limitNoted && !returned.load(),
          "active=" + std::to_string(active.load()) + " served=" + std::to_string(served.load()));
    // A slot handed back is taken again.
    CloseHandle(held[0]);
    held[0] = INVALID_HANDLE_VALUE;
    wait_until([&] { return active.load() == 6; }, 2000);
    HANDLE again = open_client();
    const bool againOk = send_key(again, 0x88) && wait_until([&] { return has_key(0x88); }, 2000);
    check("r20 Q2: after a slot is handed back a new connection is served", againOk && active.load() == 7,
          "active=" + std::to_string(active.load()));
    if (again != INVALID_HANDLE_VALUE) CloseHandle(again);
    for (HANDLE& h : held) {
      if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
      h = INVALID_HANDLE_VALUE;
    }
    wait_until([&] { return active.load() == 0; }, 3000);
    // Churn: 40 connect-and-close at once (some never get served) -- the name stays ours throughout.
    bool squatRefused = true;
    for (int i = 0; i < 40; ++i) {
      HANDLE c = CreateFileW(nm.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
      if (c != INVALID_HANDLE_VALUE) CloseHandle(c);
      HANDLE sq = CreateNamedPipeW(nm.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                   PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, n::kSecureInputPipeMaxInstances, 0,
                                   64 * 1024, 0, nullptr);
      if (sq != INVALID_HANDLE_VALUE) {
        squatRefused = false;
        CloseHandle(sq);
      }
    }
    HANDLE after = open_client();
    const bool afterOk = send_key(after, 0x99) && wait_until([&] { return has_key(0x99); }, 2000);
    check("r20 Q2: 40 connect/close in a row -- no moment where another process could take the name first, and "
          "the next client is served",
          squatRefused && afterOk && !returned.load());
    const auto s0 = std::chrono::steady_clock::now();
    reg.Stop();
    const bool ended = wait_until([&] { return returned.load(); }, 3000);
    const auto stopMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s0).count();
    if (!ended) {
      check("r20 Q2: STOP ends the capped server", false, "did not return in 3 s");
      std::printf("\nRESULT: FAILED  (%d checks, %d failed)\n", gChecks, gFailures);
      std::fflush(stdout);
      TerminateProcess(GetCurrentProcess(), 1);
    }
    srv.join();
    if (after != INVALID_HANDLE_VALUE) CloseHandle(after);
    check("r20 Q2: STOP ends the capped server with a connection open; every instance gone", reg.size() == 0,
          std::to_string(stopMs) + " ms");
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

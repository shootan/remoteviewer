// Tests for the trusted service-pipe client layer (trusted_service_pipe.hpp, secure-pipe-identity).
//
// ISOLATED: a TEST pipe name, a test-owned fake server, and a SYNTHETIC gatherer -- no real
// GNLinkSecureInput/GNLinkUnlock name is touched and no saved credential is read. The gatherer seam
// lets the test drive the Trusted/Untrusted/Unavailable verdict for a fake server it owns.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>

#include "trusted_service_pipe.hpp"

using namespace remote60::native_poc;

namespace {
int gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what);
  if (!ok) { ++gFailures; if (!detail.empty()) std::printf("        %s\n", detail.c_str()); }
}

// The gatherer seam: the test sets these synthetic facts; the fake server's real identity is irrelevant
// to the verdict (that is the point -- the test controls whether the layer would call it trusted).
std::atomic<int> g_serverPid{0};
std::atomic<int> g_servicePid{0};
std::atomic<bool> g_serviceRunning{false};
std::atomic<bool> g_imageSame{false};
std::atomic<bool> g_imagesKnown{true};
ServerIdentityFacts fixture_gather(void*, const wchar_t*) {
  ServerIdentityFacts f;
  f.serverPidKnown = true;
  f.serverPid = static_cast<uint32_t>(g_serverPid.load());
  f.serviceRunning = g_serviceRunning.load();
  f.servicePid = static_cast<uint32_t>(g_servicePid.load());
  f.serverImageKnown = g_imagesKnown.load();
  f.serviceBinaryKnown = g_imagesKnown.load();
  f.imageSameFile = g_imageSame.load();
  return f;
}

// A fake server that owns the test pipe name and records how many bytes any client sends it.
struct FakeServer {
  std::wstring name;
  std::thread th;
  std::atomic<bool> stop{false};
  std::atomic<int> bytesReceived{0};
  std::atomic<int> connections{0};

  void Start(const std::wstring& n) {
    name = n;
    th = std::thread([this] {
      while (!stop.load()) {
        HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 4, 4096,
                                       4096, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) { Sleep(10); continue; }
        const BOOL c = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (c && !stop.load()) {
          connections.fetch_add(1);
          uint8_t buf[256];
          DWORD got = 0;
          if (ReadFile(pipe, buf, sizeof(buf), &got, nullptr) && got > 0)
            bytesReceived.fetch_add(static_cast<int>(got));
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
      }
    });
  }
  void Stop() {
    stop.store(true);
    // Unblock a parked ConnectNamedPipe by connecting once.
    HANDLE w = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
    if (th.joinable()) th.join();
  }
};
}  // namespace

int main() {
  std::printf("trusted service-pipe identity\n");

  // --- pure ClassifyServerIdentity ---------------------------------------------------------------
  auto facts = [](bool pidK, uint32_t sp, bool run, uint32_t svcp, bool imgK, bool same) {
    ServerIdentityFacts f;
    f.serverPidKnown = pidK; f.serverPid = sp; f.serviceRunning = run; f.servicePid = svcp;
    f.serverImageKnown = imgK; f.serviceBinaryKnown = imgK; f.imageSameFile = same;
    return f;
  };
  check("Trusted: server pid == service pid AND same image file",
        ClassifyServerIdentity(facts(true, 100, true, 100, true, true)) == TrustedPipeStatus::Trusted);
  check("NEGATIVE Untrusted: server pid != service pid (a fake server)",
        ClassifyServerIdentity(facts(true, 777, true, 100, true, true)) == TrustedPipeStatus::Untrusted);
  check("NEGATIVE Unavailable: the service is not running (fake-first while service down)",
        ClassifyServerIdentity(facts(true, 100, false, 0, true, true)) == TrustedPipeStatus::Unavailable);
  check("NEGATIVE Unavailable: server pid could not be read",
        ClassifyServerIdentity(facts(false, 0, true, 100, true, true)) == TrustedPipeStatus::Unavailable);
  check("NEGATIVE Untrusted: pid matches but the image is a different file (pid reuse)",
        ClassifyServerIdentity(facts(true, 100, true, 100, true, false)) == TrustedPipeStatus::Untrusted);
  check("NEGATIVE Unavailable: pid matches but an image could not be read",
        ClassifyServerIdentity(facts(true, 100, true, 100, false, false)) == TrustedPipeStatus::Unavailable);

  const std::wstring pname = L"\\\\.\\pipe\\gnlink-tsp-test-" + std::to_wstring(GetCurrentProcessId());
  FakeServer fake;
  fake.Start(pname);
  Sleep(100);

  // --- fake-first: the layer REFUSES a server it cannot confirm, and writes nothing to it ---------
  {
    g_serviceRunning.store(false);  // no running service -> Unavailable
    TrustedPipeStatus st = TrustedPipeStatus::Trusted;
    HANDLE h = static_cast<HANDLE>(OpenVerifiedServicePipe(
        pname.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_ATTRIBUTE_NORMAL, L"NoSuchTestService", 300,
        &st, &fixture_gather));
    check("fake-first: the verified open REFUSES (no trusted handle)", h == INVALID_HANDLE_VALUE);
    check("fake-first: the verdict is not Trusted", st != TrustedPipeStatus::Trusted,
          trusted_pipe_status_name(st));
    Sleep(100);
    check("fake-first: the fake server received 0 bytes from the client (no input/request leaked)",
          fake.bytesReceived.load() == 0, std::to_string(fake.bytesReceived.load()) + " bytes");
  }

  // --- check-removed control: a client that does NOT verify DOES feed the fake (the vulnerability) --
  {
    (void)WaitNamedPipeW(pname.c_str(), 300);
    HANDLE raw = CreateFileW(pname.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    check("control: an unverified open succeeds against the fake", raw != INVALID_HANDLE_VALUE);
    if (raw != INVALID_HANDLE_VALUE) {
      const char marker[] = "SYNTHETIC-INPUT-MARKER";
      DWORD wrote = 0;
      WriteFile(raw, marker, sizeof(marker), &wrote, nullptr);
      CloseHandle(raw);
      Sleep(150);
      check("control: WITHOUT verification the fake RECEIVES the synthetic marker (check is load-bearing)",
            fake.bytesReceived.load() > 0, std::to_string(fake.bytesReceived.load()) + " bytes");
    }
  }

  // reset the counter for the trusted case
  fake.bytesReceived.store(0);

  // --- normal: a Trusted verdict lets the handle through (not every server is blocked) ------------
  {
    g_serviceRunning.store(true);
    g_serverPid.store(4242);
    g_servicePid.store(4242);
    g_imagesKnown.store(true);
    g_imageSame.store(true);
    TrustedPipeStatus st = TrustedPipeStatus::Unavailable;
    HANDLE h = static_cast<HANDLE>(OpenVerifiedServicePipe(
        pname.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_ATTRIBUTE_NORMAL, L"NoSuchTestService", 300,
        &st, &fixture_gather));
    check("normal: a Trusted server yields a usable handle", h != INVALID_HANDLE_VALUE,
          trusted_pipe_status_name(st));
    if (h != INVALID_HANDLE_VALUE) {
      const char ok[] = "TRUSTED-ROUNDTRIP";
      DWORD wrote = 0;
      WriteFile(h, ok, sizeof(ok), &wrote, nullptr);
      CloseHandle(h);
      Sleep(150);
      check("normal: the trusted roundtrip delivered the bytes", fake.bytesReceived.load() > 0);
    }
  }

  // --- per-handle re-verify: trust does not carry to a new handle --------------------------------
  {
    g_serviceRunning.store(false);  // now untrusted
    TrustedPipeStatus st = TrustedPipeStatus::Trusted;
    HANDLE h = static_cast<HANDLE>(OpenVerifiedServicePipe(
        pname.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_ATTRIBUTE_NORMAL, L"NoSuchTestService", 300,
        &st, &fixture_gather));
    check("re-verify: a NEW handle is checked afresh and refused when no longer trusted",
          h == INVALID_HANDLE_VALUE);
  }

  fake.Stop();
  if (gFailures != 0) { std::printf("trusted_service_pipe_test: FAIL (%d)\n", gFailures); return 1; }
  std::printf("trusted_service_pipe_test: PASS\n");
  return 0;
}

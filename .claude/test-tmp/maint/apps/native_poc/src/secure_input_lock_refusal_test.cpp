// lockscreen-cred r2 (Codex L3), the service's half on a real process: the product service code
// (GNLinkInputServiceLifecycleTest --lifecycle-test, secure_input_service_test_seam.hpp) receives input on its
// secure-input pipe from this process -- the way a host's broker sends it -- and must refuse a plaintext
// character bound for a session that is locked, asked of the session the message actually goes to (resolved
// as the forward resolves it), before anything reaches an agent. Its "locked" is this session standing in
// (REMOTE60_SIS_TEST_FAKE_LOCK=1). Control: the same messages to a run whose session is not locked are not
// refused there. Navigation keys, key ups and the pointer are never refused.
//
// Not shown here: the input AGENT's own last look (it needs a SYSTEM token moved into a session -- the field
// check), and a console fallback (one session only).

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

#include "secure_input_protocol.hpp"

using remote60::native_poc::SecureInputKind;
using remote60::native_poc::SecureInputMessage;

namespace {
int gChecks = 0, gFailures = 0;
void check(const std::string& what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}
bool wait_until(const std::function<bool()>& done, int ms) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    if (done()) return true;
    Sleep(20);
  }
  return done();
}
std::wstring dir_of_self() {
  wchar_t p[MAX_PATH];
  GetModuleFileNameW(nullptr, p, MAX_PATH);
  std::wstring s(p);
  return s.substr(0, s.find_last_of(L'\\') + 1);
}
std::string read_all(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
int count_of(const std::string& all, const std::string& needle) {
  int n = 0;
  for (size_t at = all.find(needle); at != std::string::npos; at = all.find(needle, at + needle.size())) ++n;
  return n;
}

struct Service {
  PROCESS_INFORMATION pi{};
  std::wstring suffix, diag, stopEvent;
  HANDLE stopEv = nullptr;
  bool Start(const std::wstring& tag, bool fakeLock) {
    suffix = std::to_wstring(GetCurrentProcessId()) + L"-" + tag;
    const std::wstring dir = dir_of_self();
    diag = dir + L"sis_lockrefusal_" + tag + L".diag.log";
    DeleteFileW(diag.c_str());
    stopEvent = L"Local\\GNLinkSisTest-" + suffix + L"-stop";
    stopEv = CreateEventW(nullptr, TRUE, FALSE, stopEvent.c_str());
    SetEnvironmentVariableW(L"REMOTE60_SIS_TEST_SUFFIX", suffix.c_str());
    SetEnvironmentVariableW(L"REMOTE60_SIS_TEST_STOP_EVENT", stopEvent.c_str());
    SetEnvironmentVariableW(L"REMOTE60_SIS_TEST_DIAG", diag.c_str());
    SetEnvironmentVariableW(L"REMOTE60_SIS_TEST_FAKE_LOCK", fakeLock ? L"1" : nullptr);
    SetEnvironmentVariableW(L"REMOTE60_SIS_TEST_CREATE_FAIL", nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    std::wstring cmd = L"\"" + dir + L"GNLinkInputServiceLifecycleTest.exe\" --lifecycle-test";
    return CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi) != FALSE;
  }
  std::wstring InputPipe() const { return L"\\\\.\\pipe\\GNLinkSisTest-" + suffix + L"-input"; }
  void Stop() {
    if (stopEv) SetEvent(stopEv);
    if (pi.hProcess) {
      if (WaitForSingleObject(pi.hProcess, 10000) == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 99);  // this test's child
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      pi = PROCESS_INFORMATION{};
    }
    if (stopEv) CloseHandle(stopEv);
    stopEv = nullptr;
  }
};

SecureInputMessage key(uint16_t eventKind, uint32_t vk) {
  SecureInputMessage m{};
  m.kind = static_cast<uint16_t>(SecureInputKind::InputEvent);
  m.eventKind = eventKind;
  m.keyCode = vk;
  return m;
}
SecureInputMessage text(const wchar_t* s) {
  SecureInputMessage m{};
  m.kind = static_cast<uint16_t>(SecureInputKind::InputText);
  for (uint16_t i = 0; s[i] && i < remote60::native_poc::kSecureInputTextMax; ++i) {
    m.text[i] = s[i];
    m.textCount = static_cast<uint16_t>(i + 1);
  }
  return m;
}

// Sends the probe set: text, a letter down, its up, Tab down/up, a mouse move. Returns the diag log after.
std::string probe(Service& s) {
  HANDLE h = INVALID_HANDLE_VALUE;
  const std::wstring name = s.InputPipe();
  wait_until([&] {
    h = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY) WaitNamedPipeW(name.c_str(), 50);
    return h != INVALID_HANDLE_VALUE;
  }, 8000);
  if (h == INVALID_HANDLE_VALUE) return "(no pipe)";
  SecureInputMessage mouse = key(1, 0);
  mouse.x = 5;
  mouse.y = 5;
  for (const SecureInputMessage& m : {text(L"pw"), key(5, 'Q'), key(6, 'Q'), key(5, VK_TAB), key(6, VK_TAB), mouse}) {
    DWORD n = 0;
    WriteFile(h, &m, sizeof(m), &n, nullptr);
  }
  Sleep(1500);
  CloseHandle(h);
  Sleep(300);
  return read_all(s.diag);
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the service refuses plaintext to a locked session, by the session the input goes to (lockscreen-cred r2, L3)\n\n");
  {
    Service s;
    const bool up = s.Start(L"locked", true);
    const std::string log = probe(s);
    s.Stop();
    const int textRefused = count_of(log, "plaintext text to a locked session refused at the service");
    const int keyRefused = count_of(log, "plaintext key to a locked session refused at the service");
    DWORD mine = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &mine);
    check("locked: the service started (product code, test build) and served the pipe", up && log.find("control pipe connected") != std::string::npos);
    check("locked: the text was refused at the service -- for THIS session (the target), lock known and locked",
          textRefused == 1 && log.find("session=" + std::to_string(mine) + " lockKnown=1 locked=1") != std::string::npos,
          "text=" + std::to_string(textRefused));
    check("locked: the letter DOWN was refused; its up, Tab and the pointer were not (2 refusals in all)",
          keyRefused == 1 && textRefused + keyRefused == 2, "key=" + std::to_string(keyRefused));
  }
  {
    Service s;
    const bool up = s.Start(L"unlocked", false);
    const std::string log = probe(s);
    s.Stop();
    check("control: the same input to a session that is NOT locked is not refused at the service",
          up && log.find("control pipe connected") != std::string::npos && log.find("to a locked session refused") == std::string::npos);
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

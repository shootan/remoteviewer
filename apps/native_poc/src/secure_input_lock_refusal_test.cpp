// lockscreen-cred r2 (Codex L3), the service's half on a real process: the product service code
// (GNLinkInputServiceLifecycleTest --lifecycle-test, secure_input_service_test_seam.hpp) receives input on its
// secure-input pipe from this process -- the way a host's broker sends it -- and must refuse a plaintext
// character bound for a session that is locked, asked of the session the message actually goes to (resolved
// as the forward resolves it), before anything reaches an agent. Its "locked" is this session standing in
// (REMOTE60_SIS_TEST_FAKE_LOCK=1). Control: the same messages to a run whose session is not locked are not
// refused there. Navigation keys, key ups and the pointer are never refused.
//
// lk-abcd D (user decision 2026-11-03): plaintext at the lock screen is ALLOWED now (lock_input::kRefuse is false).
// lk-abcd r2 (Codex R3): the input effect boundary is explicit -- the TEST BUILD's forward runs the product routing
// and records what would reach the agent ("sis-test input sink: ...") instead of starting one; the run says so
// ("input effects OFF") and the test reads that BEFORE it sends anything. Then, positively, each message is in the
// sink: the text (its length), the letter down/up, Tab, the pointer, Enter down/up.
// Negative control (REMOTE60_SIS_TEST_REFUSE_LOCKED_PLAINTEXT=1, test build only: the refusal rule as it was): the
// text and the letter are refused -- and the Enter right after them is refused too (lk-abcd A, the service's
// guard); the sink then lacks exactly those, so the sink checks can fail.
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
  bool Start(const std::wstring& tag, bool fakeLock, bool refuseControl = false) {
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
    SetEnvironmentVariableW(L"REMOTE60_SIS_TEST_REFUSE_LOCKED_PLAINTEXT", refuseControl ? L"1" : nullptr);
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
std::string probe(Service& s, bool* offBeforeSend) {
  // lk-abcd r2 (Codex R3): this run's effective input state is read BEFORE anything is sent.
  *offBeforeSend = wait_until([&] {
    return read_all(s.diag).find("sis-test: input effects OFF -- record-only sink") != std::string::npos;
  }, 8000);
  if (!*offBeforeSend) return "(input effects not said to be off: nothing sent)";
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
  for (const SecureInputMessage& m : {text(L"pw"), key(5, 'Q'), key(6, 'Q'), key(5, VK_TAB), key(6, VK_TAB), mouse,
                                      key(5, VK_RETURN), key(6, VK_RETURN)}) {
    DWORD n = 0;
    WriteFile(h, &m, sizeof(m), &n, nullptr);
  }
  Sleep(1500);
  CloseHandle(h);
  Sleep(300);
  return read_all(s.diag);
}
struct Sink {
  int lines = 0, text = 0, letterDown = 0, letterUp = 0, tab = 0, pointer = 0, enterDown = 0, enterUp = 0;
};
Sink sink_of(const std::string& log) {
  Sink k;
  k.lines = count_of(log, "sis-test input sink: ");
  k.text = count_of(log, "sis-test input sink: text event=0 key=0x0 textCount=2");
  k.letterDown = count_of(log, "sis-test input sink: event event=5 key=0x51 ");
  k.letterUp = count_of(log, "sis-test input sink: event event=6 key=0x51 ");
  k.tab = count_of(log, "sis-test input sink: event event=5 key=0x9 ") + count_of(log, "sis-test input sink: event event=6 key=0x9 ");
  k.pointer = count_of(log, "sis-test input sink: event event=1 ");
  k.enterDown = count_of(log, "sis-test input sink: event event=5 key=0xd ");
  k.enterUp = count_of(log, "sis-test input sink: event event=6 key=0xd ");
  return k;
}
std::string sink_text(const Sink& k) {
  return "lines=" + std::to_string(k.lines) + " text=" + std::to_string(k.text) + " Qdown=" + std::to_string(k.letterDown) +
         " Qup=" + std::to_string(k.letterUp) + " tab=" + std::to_string(k.tab) + " ptr=" + std::to_string(k.pointer) +
         " ENTdown=" + std::to_string(k.enterDown) + " ENTup=" + std::to_string(k.enterUp);
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the service and plaintext to a locked session, at a record-only input sink (lockscreen-cred r2 L3; lk-abcd D, r2 R3)\n\n");
  DWORD mine = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &mine);
  {
    Service s;
    const bool up = s.Start(L"locked", true);
    bool off = false;
    const std::string log = probe(s, &off);
    s.Stop();
    const Sink k = sink_of(log);
    check("locked: the service started (product code, test build) and served the pipe", up && log.find("control pipe connected") != std::string::npos);
    check("r2 R3: THE RUN SAID 'input effects OFF -- record-only sink, agent start disabled' BEFORE ANYTHING WAS SENT", off);
    check("r2 R3: ...and no agent was ensured or started (no agent line at all)",
          log.find("agent start refused") == std::string::npos && log.find("lock-cred agent") == std::string::npos &&
              log.find("recreating the agent") == std::string::npos);
    check("lk-abcd D: locked: nothing refused (text, letter, Enter)",
          count_of(log, "to a locked session refused") == 0 && count_of(log, "Enter refused at the service") == 0);
    check("lk-abcd D: locked: THE SINK HAS EVERY MESSAGE -- the text (2 units), the letter down and up, Tab x2, the pointer, Enter down and up",
          k.text == 1 && k.letterDown == 1 && k.letterUp == 1 && k.tab == 2 && k.pointer == 1 && k.enterDown == 1 && k.enterUp == 1 &&
              k.lines == 8,
          sink_text(k));
  }
  {
    Service s;
    const bool up = s.Start(L"refusectl", true, /*refuseControl=*/true);
    bool off = false;
    const std::string log = probe(s, &off);
    s.Stop();
    const Sink k = sink_of(log);
    check("NEGATIVE CONTROL (the old refusal rule, test build): started, effects said OFF first (and the control named)",
          up && off && log.find("locked plaintext REFUSED (negative control)") != std::string::npos);
    check("NEGATIVE CONTROL: the text and the letter down were refused -- for THIS session, lock known and locked",
          count_of(log, "plaintext text to a locked session refused at the service") == 1 &&
              count_of(log, "plaintext key to a locked session refused at the service") == 1 &&
              log.find("session=" + std::to_string(mine) + " lockKnown=1 locked=1") != std::string::npos);
    check("lk-abcd A (service): THE ENTER RIGHT AFTER THE REFUSED CHARACTERS WAS REFUSED TOO",
          count_of(log, "Enter refused at the service") == 1 && k.enterDown == 0);
    check("NEGATIVE CONTROL: the sink lacks exactly those (no text, no letter down, no Enter down); the rest reached it",
          k.text == 0 && k.letterDown == 0 && k.enterDown == 0 && k.letterUp == 1 && k.tab == 2 && k.pointer == 1 && k.enterUp == 1 &&
              k.lines == 5,
          sink_text(k));
  }
  {
    Service s;
    const bool up = s.Start(L"unlocked", false);
    bool off = false;
    const std::string log = probe(s, &off);
    s.Stop();
    const Sink k = sink_of(log);
    check("control: a session that is NOT locked: effects OFF first, nothing refused, all 8 messages in the sink",
          up && off && log.find("to a locked session refused") == std::string::npos && k.lines == 8 && k.text == 1 &&
              k.enterDown == 1,
          sink_text(k));
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

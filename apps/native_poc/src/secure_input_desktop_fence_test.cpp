// prelogin-ui r10 (T2): the SYSTEM input agent's last look before it injects a pre-login message.
//
// The stop point Codex named: the pre-login worker queues a message while the logon desktop ("Winlogon")
// has the input; before the agent takes it off the pipe and attaches, a user logs on and the input desktop
// becomes theirs ("Default"). The agent attaches to the desktop as it is NOW -- so without a fence the
// message lands on the user's desktop. Here the agent's own per-message step (AgentHandleMessage, what
// secure_input_service_main's agent loop calls) runs with the switch made inside the attach.

#include <cstdio>
#include <string>

#include "secure_input_desktop_fence.hpp"

namespace n = remote60::native_poc;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}

struct Desk {
  std::wstring input = L"Winlogon";  // where input goes right now
  std::wstring attached;             // where the agent's thread is
  std::wstring switchBeforeAttach;   // the switch that happens between the queueing and the attach
  int injectedOnDefault = 0;
  int injected = 0;
  n::AgentStep run(const n::SecureInputMessage& m) {
    return n::AgentHandleMessage(
        m,
        [&] {
          if (!switchBeforeAttach.empty()) input = switchBeforeAttach;
          attached = input;  // the agent attaches to the input desktop as it is now
          return true;
        },
        [&] { return attached; },
        [&](const n::SecureInputMessage&) {
          ++injected;
          if (attached == L"Default") ++injectedOnDefault;
          return true;
        });
  }
};
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the SYSTEM input agent's fence for pre-login input (prelogin-ui r10, T2)\n\n");
  n::SecureInputMessage prelogin{};
  prelogin.kind = static_cast<uint16_t>(n::SecureInputKind::InputEvent);
  prelogin.flags = n::kSecureInputFlagLogonDesktopOnly;
  n::SecureInputMessage loggedIn{};
  loggedIn.kind = static_cast<uint16_t>(n::SecureInputKind::InputEvent);

  {
    Desk d;
    d.switchBeforeAttach = L"Default";  // queued at Winlogon; a user logs on before the agent attaches
    const n::AgentStep s = d.run(prelogin);
    check("T2: a pre-login message queued at the logon desktop, the desktop switched to the user's before the attach: "
          "NOT injected", s == n::AgentStep::RefusedNotLogonDesktop && d.injected == 0 && d.injectedOnDefault == 0);
  }
  {
    Desk d;
    check("a pre-login message with the logon desktop still there: injected",
          d.run(prelogin) == n::AgentStep::Injected && d.injected == 1);
  }
  {
    Desk d;
    d.input = L"Default";
    check("a logged-on host's message (no flag) on the user's desktop: injected as before",
          d.run(loggedIn) == n::AgentStep::Injected && d.injectedOnDefault == 1);
    Desk u;
    u.input = L"Winlogon";
    check("...and on a secure desktop (UAC / lock screen): injected as before", u.run(loggedIn) == n::AgentStep::Injected);
  }
  check("the fence by itself: the flag allows only the logon desktop (case-insensitive)",
        n::AgentMayInject(n::kSecureInputFlagLogonDesktopOnly, L"winlogon") &&
            !n::AgentMayInject(n::kSecureInputFlagLogonDesktopOnly, L"Default") &&
            !n::AgentMayInject(n::kSecureInputFlagLogonDesktopOnly, L"") && n::AgentMayInject(0, L"Default"));
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

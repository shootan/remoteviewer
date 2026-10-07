// prelogin-ui r10 (T2): the SYSTEM input agent's last look before it injects a pre-login message.
//
// The stop point Codex named: the pre-login worker queues a message while the logon desktop ("Winlogon")
// has the input; before the agent takes it off the pipe and attaches, a user logs on and the input desktop
// becomes theirs ("Default"). The agent attaches to the desktop as it is NOW -- so without a fence the
// message lands on the user's desktop. Here the agent's own per-message step (AgentHandleMessage, what
// secure_input_service_main's agent loop calls) runs with the switch made inside the attach.

#include <cstdio>
#include <map>
#include <string>

#include <functional>

#include "secure_input_desktop_fence.hpp"
#include "secure_input_session.hpp"

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
// r11 U2: the service's forward, over two console sessions A (1) and B (2), both at their logon screen.
// The agent attaches in the session the forward chose and injects through the same AgentHandleMessage.
struct Sessions {
  uint32_t requester = 1;     // the pre-login worker's session (A), as the pipe said at connect
  bool requesterActive = true;
  bool requesterKnown = true;
  uint32_t console = 1;
  uint32_t genAtConnect = 0;
  uint32_t gen = 0;
  std::map<uint32_t, std::wstring> desktop{{1, L"Winlogon"}, {2, L"Winlogon"}};
  uint32_t agent = n::kNoSession;
  bool nextWriteFails = false;
  std::function<void()> onFirstWriteFail;  // what changes between the failed write and the retry
  std::map<uint32_t, int> injected;
  n::ForwardOutcome forward(const n::SecureInputMessage& m) {
    return n::ForwardSecureInput(
        m,
        [&] {  // the ordinary target, the product's resolve_target_session
          return n::resolve_target_session(requester, requesterKnown, requesterActive, console).sessionId;
        },
        [&] { return n::LogonOnlyTargetSession(requester, console, genAtConnect, gen); },
        [&](uint32_t s) { agent = s; return true; }, [&](uint32_t s) { agent = s; return true; },
        [&](const n::SecureInputMessage& msg) {
          if (nextWriteFails) {
            nextWriteFails = false;
            if (onFirstWriteFail) onFirstWriteFail();
            return false;
          }
          std::wstring attached;
          n::AgentHandleMessage(
              msg, [&] { attached = desktop[agent]; return true; }, [&] { return attached; },
              [&](const n::SecureInputMessage&) { ++injected[agent]; return true; });
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

  // r11 U2: a pre-login message never reaches another session than its worker's.
  {
    Sessions w;
    check("U2: A's worker, A the console at its logon screen: delivered to A",
          w.forward(prelogin) == n::ForwardOutcome::Written && w.injected[1] == 1 && w.injected[2] == 0);
  }
  {
    Sessions w;  // queued in A; before the service forwards it the console is B (also at its logon screen)
    w.console = 2;
    w.requesterActive = false;
    w.gen = 1;
    const n::ForwardOutcome o = w.forward(prelogin);
    check("U2: queued in A, the console switched to B (both Winlogon) before the forward: NOT injected anywhere",
          o == n::ForwardOutcome::LogonBindingLost && w.injected[1] == 0 && w.injected[2] == 0);
  }
  {
    Sessions w;  // the console switch not yet announced (no event yet): the session itself differs
    w.console = 2;
    w.requesterActive = false;
    check("U2: ...also before the console event arrived (the console id alone differs)",
          w.forward(prelogin) == n::ForwardOutcome::LogonBindingLost && w.injected[2] == 0);
  }
  {
    Sessions w;
    w.requester = n::kNoSession;  // the requester's session could not be read
    w.requesterKnown = false;
    w.console = 2;
    check("U2: the requester's session unknown: NOT injected (no console fallback)",
          w.forward(prelogin) == n::ForwardOutcome::LogonBindingLost && w.injected[2] == 0);
  }
  {
    Sessions w;
    w.nextWriteFails = true;  // the agent in A died; before the retry the console became B
    w.onFirstWriteFail = [&w] { w.console = 2; w.requesterActive = false; w.gen = 1; };
    check("U2: the first write failed and the console moved to B before the retry: NOT injected into B",
          w.forward(prelogin) == n::ForwardOutcome::LogonBindingLost && w.injected[2] == 0 && w.injected[1] == 0);
  }
  {
    Sessions w;  // A -> B -> A again: A is the console once more, but in a new attachment
    w.gen = 2;
    check("U2: the console left A and came back after the worker connected: dropped (a new console attachment)",
          w.forward(prelogin) == n::ForwardOutcome::LogonBindingLost && w.injected[1] == 0);
  }
  {
    Sessions w;  // a logged-on host in a disconnected RDP session: its input still goes to the console
    w.requester = 3;
    w.requesterActive = false;
    w.console = 2;
    w.desktop[2] = L"Winlogon";
    check("U2: an ordinary message keeps the console fallback (disconnected requester -> console B)",
          w.forward(loggedIn) == n::ForwardOutcome::Written && w.injected[2] == 1);
    Sessions r;
    r.nextWriteFails = true;
    r.requester = 3;
    r.requesterActive = false;
    r.console = 2;
    check("...and its retry after a failed write re-resolves as before",
          r.forward(loggedIn) == n::ForwardOutcome::Written && r.injected[2] == 1);
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

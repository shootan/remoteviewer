#pragma once

// The SYSTEM input agent's last look before it injects (prelogin-ui r10, T2).
//
// The agent (GNLinkInputService --agent) re-attaches to the CURRENT input desktop for every message and
// injects there -- which is what a logged-on host's secure-desktop input needs (a UAC prompt, a lock
// screen). But a message the pre-login worker queued while the logon screen ("Winlogon") had the input
// may be taken off the pipe after a user has logged on and the input desktop is theirs ("Default"):
// re-attaching then lands it on the user's desktop. The worker's own desktop guard (exit 14) cannot
// reach a message already handed to the agent.
//
// So a pre-login message carries kSecureInputFlagLogonDesktopOnly, and the agent injects it only if the
// desktop it has just attached to IS the logon desktop -- checked after the attach, right before the
// injection, on the message itself. Messages without the flag (a logged-on host's) are unchanged.

#include <windows.h>

#include <string>

#include "secure_input_protocol.hpp"

namespace remote60::native_poc {

constexpr uint16_t kSecureInputFlagLogonDesktopOnly = 0x0001;  // SecureInputMessage::flags
constexpr wchar_t kSecureInputLogonDesktop[] = L"Winlogon";

inline bool AgentMayInject(uint16_t flags, const std::wstring& attachedDesktop) {
  if ((flags & kSecureInputFlagLogonDesktopOnly) == 0) return true;
  return _wcsicmp(attachedDesktop.c_str(), kSecureInputLogonDesktop) == 0;
}

enum class AgentStep { Injected, NotInjected, AttachFailed, RefusedNotLogonDesktop };

// One message, as the agent handles it: attach to the input desktop as it is NOW, then the fence, then the
// injection. `attach` / `desktopName` / `inject` are the agent's own functions (secure_input_service_main),
// or a test's.
template <typename Attach, typename DesktopName, typename Inject>
AgentStep AgentHandleMessage(const SecureInputMessage& message, Attach attach, DesktopName desktopName, Inject inject) {
  if (!attach()) return AgentStep::AttachFailed;
  if (!AgentMayInject(message.flags, desktopName())) return AgentStep::RefusedNotLogonDesktop;
  return inject(message) ? AgentStep::Injected : AgentStep::NotInjected;
}

}  // namespace remote60::native_poc

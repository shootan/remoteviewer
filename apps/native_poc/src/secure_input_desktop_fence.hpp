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

// r11 U2: WHICH SESSION a pre-login message may go to. The desktop fence above only knows a desktop's NAME,
// and every console session has a "Winlogon". An ordinary (logged-on host's) message falls back to the
// console session when its requester is inactive or cannot be told (secure_input_session.hpp) -- right for
// a disconnected RDP host, wrong for the pre-login worker: the console may by then be ANOTHER session at
// its own logon screen, and the operator's input meant for A's would land on B's.
//
// So a pre-login message goes only to the session of the worker that sent it (the pipe's requester, as
// the service read it at connect -- never anything the message says), only while that session is still
// the console, and only within the console attachment it connected in (`consoleGeneration`, bumped on
// every console connect / disconnect): anything else is dropped, never re-aimed.
inline constexpr uint32_t kNoSession = 0xffffffffu;

inline uint32_t LogonOnlyTargetSession(uint32_t requesterSession, uint32_t consoleSession,
                                       uint32_t consoleGenerationAtConnect, uint32_t consoleGenerationNow) {
  if (requesterSession == kNoSession || requesterSession == 0) return kNoSession;
  if (requesterSession != consoleSession) return kNoSession;
  if (consoleGenerationAtConnect != consoleGenerationNow) return kNoSession;
  return requesterSession;
}

enum class ForwardOutcome { Written, NoTarget, LogonBindingLost, WriteFailed };

// The service's forward of one message (secure_input_service_main's forward_to_agent): the target, an agent
// there, the write; a failed write re-resolves the target once and restarts the agent there. For a
// pre-login message both resolutions are LogonOnlyTargetSession -- the retry included, so a session that
// changed between the two cannot receive it. `ordinaryTarget` / `logonTarget` return kNoSession for none.
template <typename OrdinaryTarget, typename LogonTarget, typename Ensure, typename Restart, typename Write>
ForwardOutcome ForwardSecureInput(const SecureInputMessage& message, OrdinaryTarget ordinaryTarget,
                                  LogonTarget logonTarget, Ensure ensureAgentIn, Restart restartAgentIn, Write write) {
  const bool logonOnly = (message.flags & kSecureInputFlagLogonDesktopOnly) != 0;
  const auto pick = [&]() -> uint32_t { return logonOnly ? logonTarget() : ordinaryTarget(); };
  const auto none = [&] { return logonOnly ? ForwardOutcome::LogonBindingLost : ForwardOutcome::NoTarget; };
  uint32_t session = pick();
  if (session == kNoSession) return none();
  if (!ensureAgentIn(session)) return ForwardOutcome::WriteFailed;
  if (write(message)) return ForwardOutcome::Written;
  // A failed write means the agent died; re-resolve rather than reusing the old session, since the reason
  // it died may be that its session went away.
  session = pick();
  if (session == kNoSession) return none();
  if (!restartAgentIn(session)) return ForwardOutcome::WriteFailed;
  return write(message) ? ForwardOutcome::Written : ForwardOutcome::WriteFailed;
}

}  // namespace remote60::native_poc

#pragma once

// Prelogin control-message allowlist (prelogin-service A1, contract section 3).
//
// Before anyone has logged in, the host must expose the MINIMUM control surface: enough to stream the
// secure desktop and let the viewer navigate + deliver a sealed credential, and NOTHING that reaches
// into a user environment that does not exist yet (clipboard, file copy, paste, window enumeration,
// host-side IME composition, arbitrary character/raw-key injection). The policy is DEFAULT-DENY: only
// an explicitly listed MessageType is processed in prelogin; everything else -- including every future
// message type -- is rejected. (Codex r3 section 2: plaintext is limited to navigation; characters go
// through the sealed path; no raw scan/Unicode/clipboard side channel.)
//
// This is LAYER 1 (which message types are even accepted). It is NOT the whole story: within an
// accepted ControlInputEvent, a second payload-level filter (navigation keys + mouse + SAS only, no
// character keys) lives in the input worker -- that is a separate unit and is NOT claimed here. A
// passing allowlist does not by itself prove the character-key filter.
//
// Pure: depends only on the MessageType enum.

#include "poc_protocol.hpp"

namespace remote60::native_poc::prelogin {

// True iff this control message type is permitted while the host is in the prelogin (no interactive
// user) mode. Default-deny: anything not named here is rejected.
inline bool IsControlAllowedInPrelogin(MessageType t) {
  switch (t) {
    // --- media/transport the stream needs ---
    case MessageType::FrameTick:
    case MessageType::Ack:
    case MessageType::EncodedFrameH264:
    // --- heartbeat / capability / version (no user data) ---
    case MessageType::ControlPing:
    case MessageType::ControlPong:
    case MessageType::ControlVersionRequest:
    case MessageType::ControlVersionResponse:
    // --- stream/encoder/capture control (geometry + codec only, no user data) ---
    case MessageType::ControlClientMetrics:
    case MessageType::ControlRequestKeyFrame:
    case MessageType::ControlRuntimeEncoderConfig:
    case MessageType::ControlCaptureModeRequest:
    case MessageType::ControlStreamState:
    case MessageType::ControlDesktopBackendRequest:
    case MessageType::ControlMonitorListRequest:
    case MessageType::ControlMonitorList:
    case MessageType::ControlMonitorSelect:
    case MessageType::ControlClientBandwidth:
    // --- input: mouse + NAVIGATION keys only. The char-vs-navigation filter is enforced on the
    //     ControlInputEvent PAYLOAD in the input worker; ControlInputText (character text) is NOT
    //     here -- characters must travel the sealed credential path. ---
    case MessageType::ControlInputEvent:
    case MessageType::ControlInputAck:
    // --- sealed credential path (the ONLY way characters/credentials enter prelogin) ---
    case MessageType::ControlUnlockChallengeRequest:
    case MessageType::ControlUnlockChallenge:
    case MessageType::ControlUnlockSealedRequest:
    case MessageType::ControlUnlockAccepted:
    case MessageType::ControlUnlockStatusRequest:
    case MessageType::ControlUnlockStatusResult:
    // --- remote reboot (prelogin-service r2): the request is inbound and valid in prelogin; the host
    //     still requires a directory-authenticated session (a SEPARATE gate, not this type filter).
    //     The reply is outbound. ---
    case MessageType::ControlRebootRequest:
    case MessageType::ControlRebootReply:
    // --- r9 R5: Ctrl+Alt+Del (SAS) through the service; the handler requires the authenticated link. ---
    case MessageType::ControlPreloginControl:
    case MessageType::ControlPreloginStatus:
      return true;
    default:
      // Everything else is denied in prelogin: ControlInputText (char text), ControlPhysicalKey (raw
      // vk/scan bypass), ControlImeState* (host IME composition), window enumeration/selection/
      // thumbnail, all clipboard (text/image/bulk), all file copy (+bulk), paste-on-demand, raw BGRA,
      // and any not-yet-defined type.
      return false;
  }
}

}  // namespace remote60::native_poc::prelogin

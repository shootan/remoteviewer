// Tests for the prelogin control-message allowlist (prelogin_control_allowlist.hpp). The allow set is
// checked, but the NEGATIVE controls are the contract: clipboard/file/paste/window-enum/IME/char-text/
// raw-key and any undefined type must be REJECTED in prelogin. (prelogin-service A1 section 3.)

#include "prelogin_control_allowlist.hpp"

#include <cstdio>
#include <initializer_list>

using namespace remote60::native_poc;
using namespace remote60::native_poc::prelogin;

namespace {
int gFailures = 0;
void expect(MessageType t, bool want, const char* name) {
  const bool got = IsControlAllowedInPrelogin(t);
  if (got == want) {
    std::printf("  ok   %-32s %s\n", name, want ? "ALLOW" : "DENY");
  } else {
    std::printf("  FAIL %-32s expected %s got %s\n", name, want ? "ALLOW" : "DENY", got ? "ALLOW" : "DENY");
    ++gFailures;
  }
}
}  // namespace

int main() {
  std::printf("prelogin control allowlist (default-deny)\n");

  std::printf("[allowed: the minimum a pre-login session needs]\n");
  expect(MessageType::FrameTick, true, "FrameTick");
  expect(MessageType::EncodedFrameH264, true, "EncodedFrameH264");
  expect(MessageType::ControlPing, true, "ControlPing");
  expect(MessageType::ControlPong, true, "ControlPong");
  expect(MessageType::ControlInputEvent, true, "ControlInputEvent(mouse+nav)");
  expect(MessageType::ControlRequestKeyFrame, true, "ControlRequestKeyFrame");
  expect(MessageType::ControlCaptureModeRequest, true, "ControlCaptureModeRequest");
  expect(MessageType::ControlMonitorSelect, true, "ControlMonitorSelect");
  expect(MessageType::ControlClientBandwidth, true, "ControlClientBandwidth");
  expect(MessageType::ControlUnlockChallengeRequest, true, "ControlUnlockChallengeRequest(sealed)");
  expect(MessageType::ControlUnlockSealedRequest, true, "ControlUnlockSealedRequest(sealed)");
  expect(MessageType::ControlUnlockStatusResult, true, "ControlUnlockStatusResult(sealed)");

  std::printf("[NEGATIVE: characters/raw key must NOT be plaintext in prelogin]\n");
  expect(MessageType::ControlInputText, false, "ControlInputText(char text)");
  expect(MessageType::ControlPhysicalKey, false, "ControlPhysicalKey(raw vk/scan)");
  expect(MessageType::ControlImeStateRequest, false, "ControlImeStateRequest");
  expect(MessageType::ControlImeStateResponse, false, "ControlImeStateResponse");

  std::printf("[NEGATIVE: no user environment is exposed]\n");
  expect(MessageType::ControlWindowListRequest, false, "ControlWindowListRequest");
  expect(MessageType::ControlWindowSelect, false, "ControlWindowSelect");
  expect(MessageType::ControlWindowThumbnailRequest, false, "ControlWindowThumbnailRequest");
  expect(MessageType::ControlClipboardUpdate, false, "ControlClipboardUpdate");
  expect(MessageType::ControlClipboardData, false, "ControlClipboardData");
  expect(MessageType::ControlClipImageOffer, false, "ControlClipImageOffer");
  expect(MessageType::ClipBulkChunk, false, "ClipBulkChunk");
  expect(MessageType::ControlFileOffer, false, "ControlFileOffer");
  expect(MessageType::ControlFilePrepare, false, "ControlFilePrepare");
  expect(MessageType::FileBulkChunk, false, "FileBulkChunk");
  expect(MessageType::ControlPasteTextApply, false, "ControlPasteTextApply");
  expect(MessageType::ControlPasteApplied, false, "ControlPasteApplied");
  expect(MessageType::RawFrameBgra, false, "RawFrameBgra(debug)");

  std::printf("[NEGATIVE: undefined / reserved-but-not-yet-defined types default-deny]\n");
  // The reserved prelogin band 82..85 is NOT auto-allowed; each must be added explicitly when defined.
  expect(static_cast<MessageType>(82), false, "reserved 82 (undefined)");
  expect(static_cast<MessageType>(85), false, "reserved 85 (undefined)");
  expect(static_cast<MessageType>(9999), false, "arbitrary 9999 (future/garbage)");

  if (gFailures != 0) {
    std::printf("prelogin_control_allowlist_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("prelogin_control_allowlist_test: PASS\n");
  return 0;
}

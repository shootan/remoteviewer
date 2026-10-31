// Paste on demand (t-y4wj64jw): the 80/81 codec (paste_apply_wire.hpp). Pure.

#include "paste_apply_wire.hpp"

#include <iostream>
#include <string>

namespace {

using namespace remote60::native_poc;

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

}  // namespace

int main() {
  check("the numbers the verifier assigned", static_cast<int>(MessageType::ControlPasteTextApply) == 80 &&
                                                 static_cast<int>(MessageType::ControlPasteApplied) == 81);
  check("the capability bit", kCaptureFlagPasteOnDemandV1 == 0x1000u);
  check("...shares no bit with the others",
        (kCaptureFlagPasteOnDemandV1 & (kCaptureFlagFileCopyV1 | kCaptureFlagMouseXButtonsV1 | kCaptureFlagClipboardImageV1 |
                                        kCaptureFlagClipboardTextV1 | kCaptureFlagFrameHeartbeat | kCaptureFlagPeerVersion |
                                        kCaptureFlagHostImePulseStateV2 | kCaptureFlagHostImeV1 |
                                        kCaptureFlagUnlockSealedV1 | kCaptureFlagSecureDesktopActive)) == 0);

  const std::u16string text = u"붙여넣기 test";
  const uint64_t hash = clipboard_fnv1a(text);
  const std::vector<uint8_t> req = build_paste_text_apply(7, 0x1122334455667788ULL, 42, text, hash, 99);
  ControlPasteTextApplyMessage m{};
  std::memcpy(&m, req.data(), sizeof(m));
  check("80: fixed part then the UTF-16 units", req.size() == sizeof(m) + text.size() * 2,
        std::to_string(req.size()));
  check("80: header says the fixed part only", m.header.size == sizeof(ControlPasteTextApplyMessage) &&
                                                  m.header.type == 80 && m.header.magic == kMagic);
  check("80: fields", m.seq == 7 && m.pasteRequestId == 0x1122334455667788ULL && m.localRevision == 42 &&
                          m.utf16Count == text.size() && m.contentHash == hash && m.clientSendQpcUs == 99);
  std::u16string back;
  check("80: the payload parses back to the text",
        clipboard_parse_payload(req.data() + sizeof(m), req.size() - sizeof(m), m.utf16Count, &back) && back == text);
  check("80: precheck accepts it", paste_text_apply_precheck(m) == PasteApplyResult::Applied);
  {
    ControlPasteTextApplyMessage z = m;
    z.pasteRequestId = 0;
    check("80: request id 0 is a bad request", paste_text_apply_precheck(z) == PasteApplyResult::BadRequest);
    z = m;
    z.utf16Count = 0;
    check("80: empty text is a bad request", paste_text_apply_precheck(z) == PasteApplyResult::BadRequest);
    z = m;
    z.utf16Count = kClipboardTextMaxUtf16 + 1;
    check("80: over the cap is too large", paste_text_apply_precheck(z) == PasteApplyResult::TooLarge);
    z.utf16Count = kClipboardTextMaxUtf16;
    check("80: exactly the cap is fine", paste_text_apply_precheck(z) == PasteApplyResult::Applied);
  }

  const ControlPasteAppliedMessage a =
      make_paste_applied(7, 0x1122334455667788ULL, PasteApplyResult::Applied, PasteApplyStage::None, 0, 1234, 5);
  check("81: header", a.header.type == 81 && a.header.size == sizeof(ControlPasteAppliedMessage) && a.header.magic == kMagic);
  check("81: echo + outcome", a.seq == 7 && a.pasteRequestId == 0x1122334455667788ULL && a.result == 0 &&
                                  a.hostClipSeq == 1234 && a.hostUserCopyGen == 5);
  check("81: valid for the request it answers", paste_applied_valid(a, 7, 0x1122334455667788ULL));
  check("81: not for another seq", !paste_applied_valid(a, 8, 0x1122334455667788ULL));
  check("81: not for another request id", !paste_applied_valid(a, 7, 0x1122334455667789ULL));
  {
    ControlPasteAppliedMessage b = a;
    b.header.type = 23;  // an input ack where the answer should be
    check("81: an input ack is not an answer", !paste_applied_valid(b, 7, a.pasteRequestId));
    b = a;
    b.header.size = sizeof(ControlPasteAppliedMessage) + 4;
    check("81: a longer message is not this one", !paste_applied_valid(b, 7, a.pasteRequestId));
    b = a;
    b.result = kPasteApplyResultMax + 1;
    check("81: an unknown result is rejected", !paste_applied_valid(b, 7, a.pasteRequestId));
    b = a;
    b.stage = kPasteApplyStageMax + 1;
    check("81: an unknown stage is rejected", !paste_applied_valid(b, 7, a.pasteRequestId));
    b = a;
    b.header.magic ^= 1;
    check("81: wrong magic is rejected", !paste_applied_valid(b, 7, a.pasteRequestId));
  }
  {
    const ControlPasteAppliedMessage f =
        make_paste_applied(3, 9, PasteApplyResult::Failed, PasteApplyStage::Open, 5, 0, 0);
    check("81: a failure carries where and why", paste_applied_valid(f, 3, 9) && f.result == 1 && f.stage == 1 && f.win32 == 5);
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  (" << gChecks << " checks, "
            << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}

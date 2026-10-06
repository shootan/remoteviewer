#pragma once

// Paste on demand (t-y4wj64jw): the two wire messages of the text path.
//
// Role:    ControlPasteTextApply (80, viewer -> host) carries the text the viewer is about to
//          paste; ControlPasteApplied (81, host -> viewer) answers with the outcome of writing the
//          host's OS clipboard. The answer is the point: the older ControlClipboardUpdate (51) is
//          acknowledged with an input ack before the write has even happened, so it cannot tell a
//          viewer that the paste key may follow. Images and files keep their own messages -- a clip
//          image Published and a file OfferReply Accept already mean "on the clipboard".
// Thread:  none; pure codec.
// Callers: host_control_session.cpp (parse 80, build 81), viewer_paste_exchange.cpp (build 80,
//          parse 81), paste_apply_wire_test.cpp. NOT compiled into the Android client: it never
//          sends 80, and the shared client sources do not include this header.
//
// Layout: header.size describes the FIXED part only and utf16Count UTF-16 code units follow 80 on
// the wire, exactly like 51. Fields are ordered so neither struct has implicit padding; the sizes
// are pinned below.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "clipboard_sync.hpp"
#include "poc_protocol.hpp"

namespace remote60::native_poc {

struct ControlPasteTextApplyMessage {
  MessageHeader header{};       // size = sizeof(*this); utf16Count units follow
  uint32_t seq = 0;
  uint32_t localRevision = 0;   // the viewer's clipboard sequence number at the gesture (diagnostic)
  uint64_t pasteRequestId = 0;  // random, never 0; echoed by 81
  uint32_t utf16Count = 0;      // 1 .. kClipboardTextMaxUtf16
  uint32_t reserved = 0;
  uint64_t contentHash = 0;     // FNV-1a of the text (clipboard_fnv1a), checked by the host
  uint64_t clientSendQpcUs = 0;
};
static_assert(sizeof(ControlPasteTextApplyMessage) == 48, "paste text apply wire drift");

enum class PasteApplyResult : uint8_t {
  Applied = 0,     // the host's clipboard now holds this text (SetClipboardData succeeded)
  Failed = 1,      // it could not be written; stage + win32 say where
  Disabled = 2,    // the host has no clipboard sync running
  TooLarge = 3,    // over kClipboardTextMaxUtf16 (drained, not written)
  BadRequest = 4,  // empty, malformed, or the hash does not match the text
};
constexpr uint8_t kPasteApplyResultMax = 4;

enum class PasteApplyStage : uint8_t {
  None = 0,
  Open = 1,     // OpenClipboard
  Empty = 2,    // EmptyClipboard
  Set = 3,      // GlobalAlloc / GlobalLock / SetClipboardData
  Timeout = 4,  // the clipboard thread did not finish within the host's bound
};
constexpr uint8_t kPasteApplyStageMax = 4;

struct ControlPasteAppliedMessage {
  MessageHeader header{};
  uint32_t seq = 0;              // echo of 80
  uint8_t result = 0;            // PasteApplyResult
  uint8_t stage = 0;             // PasteApplyStage
  uint16_t reserved = 0;
  uint64_t pasteRequestId = 0;   // echo of 80
  uint32_t win32 = 0;            // GetLastError at the failing stage, 0 otherwise
  uint32_t hostClipSeq = 0;      // the host's clipboard sequence number after the write
  uint32_t hostUserCopyGen = 0;  // copies made on the host itself (its own text changes) so far
  uint32_t reserved2 = 0;
};
static_assert(sizeof(ControlPasteAppliedMessage) == 40, "paste applied wire drift");

inline std::vector<uint8_t> build_paste_text_apply(uint32_t seq, uint64_t pasteRequestId, uint32_t localRevision,
                                                   const std::u16string& text, uint64_t hash, uint64_t nowUs) {
  ControlPasteTextApplyMessage msg{};
  msg.header.magic = kMagic;
  msg.header.type = static_cast<uint16_t>(MessageType::ControlPasteTextApply);
  msg.header.size = static_cast<uint16_t>(sizeof(msg));
  msg.seq = seq;
  msg.localRevision = localRevision;
  msg.pasteRequestId = pasteRequestId;
  msg.utf16Count = static_cast<uint32_t>(text.size());
  msg.contentHash = hash;
  msg.clientSendQpcUs = nowUs;
  std::vector<uint8_t> out(sizeof(msg));
  std::memcpy(out.data(), &msg, sizeof(msg));
  clipboard_append_utf16(&out, text);
  return out;
}

inline ControlPasteAppliedMessage make_paste_applied(uint32_t seq, uint64_t pasteRequestId, PasteApplyResult result,
                                                     PasteApplyStage stage, uint32_t win32, uint32_t hostClipSeq,
                                                     uint32_t hostUserCopyGen) {
  ControlPasteAppliedMessage msg{};
  msg.header.magic = kMagic;
  msg.header.type = static_cast<uint16_t>(MessageType::ControlPasteApplied);
  msg.header.size = static_cast<uint16_t>(sizeof(msg));
  msg.seq = seq;
  msg.result = static_cast<uint8_t>(result);
  msg.stage = static_cast<uint8_t>(stage);
  msg.pasteRequestId = pasteRequestId;
  msg.win32 = win32;
  msg.hostClipSeq = hostClipSeq;
  msg.hostUserCopyGen = hostUserCopyGen;
  return msg;
}

/**
 * The viewer's check of an answer. Everything must hold: the type and exact size, the echoed seq and
 * request id, and enums in range. Anything else is a desynchronised link, not a "failed" paste --
 * the caller treats it like every other malformed reply on the control link.
 */
inline bool paste_applied_valid(const ControlPasteAppliedMessage& m, uint32_t seq, uint64_t pasteRequestId) {
  return m.header.magic == kMagic && m.header.type == static_cast<uint16_t>(MessageType::ControlPasteApplied) &&
         m.header.size == sizeof(ControlPasteAppliedMessage) && m.seq == seq && m.pasteRequestId == pasteRequestId &&
         m.result <= kPasteApplyResultMax && m.stage <= kPasteApplyStageMax;
}

/**
 * The host's check of a request's fixed part before the payload is read: what it may answer without
 * writing anything. Applied means "read the payload and write it"; anything else is the answer, and
 * the payload (utf16Count units) must still be drained.
 */
inline PasteApplyResult paste_text_apply_precheck(const ControlPasteTextApplyMessage& m) {
  if (m.pasteRequestId == 0 || m.utf16Count == 0) return PasteApplyResult::BadRequest;
  if (m.utf16Count > kClipboardTextMaxUtf16) return PasteApplyResult::TooLarge;
  return PasteApplyResult::Applied;
}

}  // namespace remote60::native_poc

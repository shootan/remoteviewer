#pragma once

// Clipboard text sync (K1): the transport-independent, Win32-independent core.
//
// Role:    the content hash, the wire codec for the three clipboard messages, and the echo-loop
//          state machine (ClipboardSyncCore). Everything here is pure: no sockets, no windows, no
//          globals -- so the whole contract can be unit-tested without a host or a session.
// Thread:  none of its own. ClipboardSyncCore is not internally synchronised; a caller that touches
//          it from more than one thread (the viewer's UI thread sees local changes, its control
//          thread applies remote ones) guards it with its own mutex.
// Callers: the viewer control/UI paths, the host clipboard hub, and clipboard_sync_core_test.
//
// The echo guard is a single idea: a peer never SENDS content it just applied from the other peer,
// and never APPLIES content it just sent. Both are decided by comparing an FNV-1a hash of the text
// against the last-sent and last-applied hashes, so a change cannot bounce back and forth between
// the two clipboards. That property is the point of the whole feature and is what the test pins.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "poc_protocol.hpp"

namespace remote60::native_poc {

// How often a client asks the host whether its clipboard moved. The host cannot push -- control is
// strict request/response -- so polling is the only way host -> client text arrives. 700ms is
// unnoticeable before a paste and costs almost nothing (a ~28-byte request, a ~48-byte reply).
// Shared by the Windows viewer and the Android session so the two behave the same.
constexpr uint64_t kClipboardPollIntervalUs = 700000;

// FNV-1a over the UTF-16 code units, hashed low byte then high byte so the value does not depend on
// the width or endianness of the platform's character type.
//
// The text type here is std::u16string, deliberately, and it is not a style choice: the wire format
// is UTF-16 code units, and wchar_t is 16-bit on Windows but 32-bit on Android. A std::wstring core
// would have hashed and framed non-ASCII text differently on the two ends of the same session.
inline uint64_t clipboard_fnv1a(const std::u16string& text) {
  uint64_t hash = 1469598103934665603ULL;  // FNV offset basis
  for (const char16_t wc : text) {
    const uint16_t unit = static_cast<uint16_t>(wc);
    hash ^= static_cast<uint8_t>(unit & 0xffu);
    hash *= 1099511628211ULL;  // FNV prime
    hash ^= static_cast<uint8_t>((unit >> 8) & 0xffu);
    hash *= 1099511628211ULL;
  }
  return hash;
}

// --- wire codec -------------------------------------------------------------------------------
//
// Each builder returns the FULL message bytes (fixed struct then the UTF-16 payload, little-endian),
// which is exactly what goes on the control link. The parser reads the payload region that follows
// a fixed header a caller has already read. Payloads are bounded by kClipboardTextMaxUtf16 so a
// malformed or hostile count cannot drive an allocation.

inline void clipboard_append_utf16(std::vector<uint8_t>* out, const std::u16string& text) {
  const size_t base = out->size();
  out->resize(base + text.size() * sizeof(uint16_t));
  for (size_t i = 0; i < text.size(); ++i) {
    const uint16_t unit = static_cast<uint16_t>(text[i]);
    std::memcpy(out->data() + base + i * sizeof(uint16_t), &unit, sizeof(uint16_t));
  }
}

// Read utf16Count code units from the payload region [payload, payload+payloadBytes). Rejects a
// count over the cap or a region too short to hold it -- both are stream errors, not truncation.
inline bool clipboard_parse_payload(const uint8_t* payload, size_t payloadBytes, uint32_t utf16Count,
                                    std::u16string* out) {
  if (!out) return false;
  if (utf16Count > kClipboardTextMaxUtf16) return false;
  if (payloadBytes < static_cast<size_t>(utf16Count) * sizeof(uint16_t)) return false;
  out->resize(utf16Count);
  for (uint32_t i = 0; i < utf16Count; ++i) {
    uint16_t unit = 0;
    std::memcpy(&unit, payload + static_cast<size_t>(i) * sizeof(uint16_t), sizeof(uint16_t));
    (*out)[i] = static_cast<char16_t>(unit);
  }
  return true;
}

inline std::vector<uint8_t> build_clipboard_update(uint32_t seq, const std::u16string& text,
                                                   uint64_t hash, uint64_t nowUs) {
  ControlClipboardUpdateMessage msg{};
  msg.header.magic = kMagic;
  msg.header.type = static_cast<uint16_t>(MessageType::ControlClipboardUpdate);
  msg.header.size = static_cast<uint16_t>(sizeof(msg));
  msg.seq = seq;
  msg.utf16Count = static_cast<uint32_t>(text.size());
  msg.contentHash = hash;
  msg.clientSendQpcUs = nowUs;
  std::vector<uint8_t> out(sizeof(msg));
  std::memcpy(out.data(), &msg, sizeof(msg));
  clipboard_append_utf16(&out, text);
  return out;
}

inline std::vector<uint8_t> build_clipboard_request(uint32_t seq, uint64_t knownGeneration,
                                                    uint64_t nowUs) {
  ControlClipboardRequestMessage msg{};
  msg.header.magic = kMagic;
  msg.header.type = static_cast<uint16_t>(MessageType::ControlClipboardRequest);
  msg.header.size = static_cast<uint16_t>(sizeof(msg));
  msg.seq = seq;
  msg.knownGeneration = knownGeneration;
  msg.clientSendQpcUs = nowUs;
  std::vector<uint8_t> out(sizeof(msg));
  std::memcpy(out.data(), &msg, sizeof(msg));
  return out;
}

// A host reply. hasData false leaves the payload empty and utf16Count 0.
inline std::vector<uint8_t> build_clipboard_data(uint32_t seq, uint64_t generation, bool hasData,
                                                 const std::u16string& text, uint64_t hash,
                                                 uint64_t nowUs) {
  ControlClipboardDataHeader msg{};
  msg.header.magic = kMagic;
  msg.header.type = static_cast<uint16_t>(MessageType::ControlClipboardData);
  msg.header.size = static_cast<uint16_t>(sizeof(msg));
  msg.seq = seq;
  msg.flags = hasData ? kClipboardDataFlagHasData : 0u;
  msg.generation = generation;
  msg.utf16Count = hasData ? static_cast<uint32_t>(text.size()) : 0u;
  msg.contentHash = hasData ? hash : 0u;
  msg.hostSendQpcUs = nowUs;
  std::vector<uint8_t> out(sizeof(msg));
  std::memcpy(out.data(), &msg, sizeof(msg));
  if (hasData) clipboard_append_utf16(&out, text);
  return out;
}

// --- echo-loop state machine ------------------------------------------------------------------

enum class ClipboardLocalDecision {
  Send,          // a genuine new local clipboard, worth sending to the peer
  SkipEmpty,     // empty clipboard; v1 does not sync clears, so it never wipes the peer
  SkipEcho,      // exactly what we last applied from the peer -- sending it back is the loop
  SkipDuplicate, // we already sent this content
  SkipTooLarge,  // over kClipboardTextMaxUtf16; skipped whole, never truncated
};

enum class ClipboardRemoteDecision {
  Apply,         // newer content from the peer, worth putting on the local clipboard
  SkipEmpty,     // empty payload; nothing to apply
  SkipEcho,      // the peer is handing back what we just sent; applying it would restart the loop
  SkipDuplicate, // already applied this content
};

// Not internally synchronised (see the header note). maxUtf16 defaults to the wire cap; a test or
// caller may lower it.
class ClipboardSyncCore {
 public:
  explicit ClipboardSyncCore(uint32_t maxUtf16 = kClipboardTextMaxUtf16) : maxUtf16_(maxUtf16) {}

  /**
   * Records content that is ALREADY on this machine's clipboard and must not be treated as news.
   *
   * Used for what was on the clipboard before the sync started. Without it, whether that content
   * gets published depends on an OS detail -- whether registering a clipboard listener provokes an
   * immediate change notification, which differs between Windows builds. Measured on this machine
   * it does not; seeding the baseline means the answer no longer matters.
   *
   * It counts as "last sent" rather than "last applied" because the effect wanted is the same in
   * both directions: this machine already has it, so it is neither worth sending out nor worth
   * writing back over the top of itself.
   */
  void SeedBaseline(const std::u16string& text) {
    if (text.empty()) return;
    lastSentHash_ = clipboard_fnv1a(text);
    haveSent_ = true;
  }

  // The local clipboard changed to `text`. On Send, `outHash` is the hash to put on the wire and
  // the core remembers it as last-sent. The other decisions leave the core unchanged.
  ClipboardLocalDecision OnLocalChange(const std::u16string& text, uint64_t* outHash) {
    if (text.empty()) return ClipboardLocalDecision::SkipEmpty;
    if (text.size() > maxUtf16_) return ClipboardLocalDecision::SkipTooLarge;
    const uint64_t hash = clipboard_fnv1a(text);
    if (haveApplied_ && hash == lastAppliedHash_) return ClipboardLocalDecision::SkipEcho;
    if (haveSent_ && hash == lastSentHash_) return ClipboardLocalDecision::SkipDuplicate;
    lastSentHash_ = hash;
    haveSent_ = true;
    if (outHash) *outHash = hash;
    return ClipboardLocalDecision::Send;
  }

  // The peer sent `text` (with the hash it computed). On Apply, the core records it as last-applied
  // BEFORE the caller actually writes the OS clipboard, so the change notification that the write
  // provokes is recognised as an echo by the next OnLocalChange and is not sent back.
  ClipboardRemoteDecision OnRemoteData(const std::u16string& text, uint64_t hash) {
    if (text.empty()) return ClipboardRemoteDecision::SkipEmpty;
    if (haveSent_ && hash == lastSentHash_) return ClipboardRemoteDecision::SkipEcho;
    if (haveApplied_ && hash == lastAppliedHash_) return ClipboardRemoteDecision::SkipDuplicate;
    lastAppliedHash_ = hash;
    haveApplied_ = true;
    return ClipboardRemoteDecision::Apply;
  }

  void Reset() {
    haveSent_ = false;
    haveApplied_ = false;
    lastSentHash_ = 0;
    lastAppliedHash_ = 0;
  }

 private:
  uint32_t maxUtf16_;
  bool haveSent_ = false;
  bool haveApplied_ = false;
  uint64_t lastSentHash_ = 0;
  uint64_t lastAppliedHash_ = 0;
};

/**
 * The client half of the sync minus the I/O: when to poll, and what a reply means at the start of
 * a session. Shared by the Windows viewer and the Android session so the two cannot drift, and
 * pure so the session-boundary rules can be tested without a host.
 *
 * It exists because of a bug that only appears ACROSS sessions. The host's clipboard state lives
 * as long as GNLinkStream, not as long as a viewer, so after "copy 'a' on the PC, close the
 * viewer, copy 'b' on the phone, reopen", the host still held 'a' at some generation > 0 while the
 * fresh client started at knownGeneration 0 -- and the first poll therefore dragged the previous
 * session's 'a' over the user's new 'b'.
 *
 * The rule that fixes it: the first poll of a session is a BASELINE. It adopts the host's current
 * generation and throws the contents away, so a session only ever receives changes that happen
 * while it is connected. What the user copied most recently travels the other way instead, by the
 * one-shot initial push -- which is what makes "the machine you just connected from wins" true,
 * and that is the behaviour people expect.
 */
class ClipboardClientPolicy {
 public:
  /** A new session began (or reconnected): re-arm the baseline and the one-shot push. */
  void OnConnected() {
    baselineAdopted_ = false;
    initialPushPending_ = true;
    knownGeneration_ = 0;
    lastPollUs_ = 0;
  }

  /** True exactly once per session, for the caller to push its current clipboard. */
  bool TakeInitialPush() {
    if (!initialPushPending_) return false;
    initialPushPending_ = false;
    return true;
  }

  /** The first poll is due immediately; later ones on the shared interval. */
  bool ShouldPoll(uint64_t nowUs) const {
    if (lastPollUs_ == 0) return true;
    return nowUs - lastPollUs_ >= kClipboardPollIntervalUs;
  }

  void NotePolled(uint64_t nowUs) { lastPollUs_ = nowUs; }

  /**
   * Takes in a poll reply and answers one question: should this text be applied locally?
   *
   * The generation is adopted either way, so the client stays current. False on the baseline poll
   * even when the host offered content -- that content predates this session.
   */
  bool OnPollReply(uint64_t generation, bool hasData) {
    knownGeneration_ = generation;
    if (!baselineAdopted_) {
      baselineAdopted_ = true;
      return false;  // whatever the host had belongs to a session that is over
    }
    return hasData;
  }

  uint64_t knownGeneration() const { return knownGeneration_; }
  bool baseline_adopted() const { return baselineAdopted_; }

 private:
  bool baselineAdopted_ = false;
  bool initialPushPending_ = false;
  uint64_t knownGeneration_ = 0;
  uint64_t lastPollUs_ = 0;
};

}  // namespace remote60::native_poc

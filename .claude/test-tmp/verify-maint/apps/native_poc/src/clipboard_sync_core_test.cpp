// Unit test for the clipboard sync core (K1): the wire codec and the echo-loop state machine.
//
// No Win32, no sockets, no session -- this is the whole transport-independent contract of
// clipboard_sync.hpp. It proves three things the feature stands or falls on:
//   1. the variable-length codec round-trips UTF-16, including Korean and a surrogate pair, and
//      rejects a count that overruns the payload or the cap (never truncates silently);
//   2. a change is not sent back to the peer that sent it -- the negative control simulates two
//      cores handing content across and asserts the bounce stops after one hop;
//   3. duplicates, empties and oversize inputs are skipped for the right reason.
//
// Non-ASCII text is written with \u escapes so the test does not depend on the source encoding.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "clipboard_sync.hpp"

namespace {

using remote60::native_poc::ClipboardLocalDecision;
using remote60::native_poc::ClipboardRemoteDecision;
using remote60::native_poc::ClipboardClientPolicy;
using remote60::native_poc::ClipboardSyncCore;
using remote60::native_poc::ControlClipboardDataHeader;
using remote60::native_poc::ControlClipboardRequestMessage;
using remote60::native_poc::ControlClipboardUpdateMessage;
using remote60::native_poc::MessageHeader;
using remote60::native_poc::MessageType;
using remote60::native_poc::build_clipboard_data;
using remote60::native_poc::build_clipboard_request;
using remote60::native_poc::build_clipboard_update;
using remote60::native_poc::clipboard_fnv1a;
using remote60::native_poc::clipboard_parse_payload;
using remote60::native_poc::kClipboardDataFlagHasData;
using remote60::native_poc::kClipboardPollIntervalUs;
using remote60::native_poc::kClipboardTextMaxUtf16;
using remote60::native_poc::kMagic;

int gPass = 0;
int gFail = 0;

void ok(bool cond, const std::string& what) {
  if (cond) {
    ++gPass;
    std::printf("PASS  %s\n", what.c_str());
  } else {
    ++gFail;
    std::printf("FAIL  %s\n", what.c_str());
  }
}

// Read a fixed header struct T from the front of a message buffer.
template <typename T>
T front(const std::vector<uint8_t>& bytes) {
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  return value;
}

void test_hash() {
  const std::u16string a = u"hello";
  const std::u16string b = u"hello";
  const std::u16string c = u"hellp";
  ok(clipboard_fnv1a(a) == clipboard_fnv1a(b), "equal text hashes equal");
  ok(clipboard_fnv1a(a) != clipboard_fnv1a(c), "one-character difference changes the hash");
  ok(clipboard_fnv1a(u"") == 1469598103934665603ULL, "empty text is the FNV offset basis");
}

void round_trip(const std::u16string& text, const std::string& label) {
  const uint64_t hash = clipboard_fnv1a(text);
  const std::vector<uint8_t> bytes = build_clipboard_update(7, text, hash, 123456);
  ok(bytes.size() == sizeof(ControlClipboardUpdateMessage) + text.size() * 2,
     label + ": buffer is the fixed header plus two bytes per code unit");
  const auto hdr = front<ControlClipboardUpdateMessage>(bytes);
  ok(hdr.header.magic == kMagic, label + ": magic survives");
  ok(hdr.header.type == static_cast<uint16_t>(MessageType::ControlClipboardUpdate),
     label + ": type is ControlClipboardUpdate");
  ok(hdr.header.size == sizeof(ControlClipboardUpdateMessage),
     label + ": header.size is the fixed part only");
  ok(hdr.utf16Count == text.size(), label + ": utf16Count matches");
  ok(hdr.contentHash == hash, label + ": content hash matches");
  std::u16string parsed;
  const uint8_t* payload = bytes.data() + sizeof(ControlClipboardUpdateMessage);
  const size_t payloadBytes = bytes.size() - sizeof(ControlClipboardUpdateMessage);
  ok(clipboard_parse_payload(payload, payloadBytes, hdr.utf16Count, &parsed),
     label + ": payload parses");
  ok(parsed == text, label + ": text round-trips byte for byte");
}

void test_codec() {
  round_trip(u"", "empty");
  round_trip(u"plain ascii clipboard", "ascii");
  round_trip(u"한글 붙여넣기", "korean");            // "한글 붙여넣기"
  round_trip(std::u16string(u"tab\tnewline\r\nend"), "control chars");
  round_trip(std::u16string{u'\xD83D', u'\xDE00'}, "surrogate pair (emoji)");  // U+1F600
  round_trip(std::u16string(kClipboardTextMaxUtf16, u'x'), "at the size cap");

  // The request and data headers frame correctly too.
  const auto req = front<ControlClipboardRequestMessage>(build_clipboard_request(3, 42, 9));
  ok(req.header.type == static_cast<uint16_t>(MessageType::ControlClipboardRequest) &&
         req.knownGeneration == 42,
     "request carries knownGeneration");

  const std::u16string payload = u"from the host";
  const auto data = build_clipboard_data(5, 11, true, payload, clipboard_fnv1a(payload), 0);
  const auto dataHdr = front<ControlClipboardDataHeader>(data);
  ok((dataHdr.flags & kClipboardDataFlagHasData) != 0 && dataHdr.generation == 11 &&
         dataHdr.utf16Count == payload.size(),
     "data reply with content sets the has-data bit and generation");
  std::u16string back;
  ok(clipboard_parse_payload(data.data() + sizeof(ControlClipboardDataHeader),
                             data.size() - sizeof(ControlClipboardDataHeader), dataHdr.utf16Count,
                             &back) &&
         back == payload,
     "data reply payload round-trips");

  const auto empty = build_clipboard_data(6, 11, false, u"", 0, 0);
  const auto emptyHdr = front<ControlClipboardDataHeader>(empty);
  ok((emptyHdr.flags & kClipboardDataFlagHasData) == 0 && emptyHdr.utf16Count == 0 &&
         empty.size() == sizeof(ControlClipboardDataHeader),
     "a no-data reply carries the generation and no payload");
}

void test_codec_boundaries() {
  // A count over the cap is refused rather than allocated.
  std::vector<uint8_t> big(16, 0);
  std::u16string out;
  ok(!clipboard_parse_payload(big.data(), big.size(), kClipboardTextMaxUtf16 + 1, &out),
     "a count over the cap is rejected");
  // A payload region too short for the stated count is a stream error, not a partial read.
  const uint8_t four[4] = {'a', 0, 'b', 0};  // two code units of bytes
  ok(!clipboard_parse_payload(four, sizeof(four), 3, &out),
     "a count longer than the payload is rejected");
  // Exactly enough is accepted.
  ok(clipboard_parse_payload(four, sizeof(four), 2, &out) && out.size() == 2,
     "an exact-length payload is accepted");
}

void test_local_decisions() {
  ClipboardSyncCore core;
  uint64_t hash = 0;
  ok(core.OnLocalChange(u"", &hash) == ClipboardLocalDecision::SkipEmpty,
     "an empty local clipboard is not synced");
  ok(core.OnLocalChange(std::u16string(kClipboardTextMaxUtf16 + 1, u'x'), &hash) ==
         ClipboardLocalDecision::SkipTooLarge,
     "an oversize clipboard is skipped whole");
  ok(core.OnLocalChange(u"first copy", &hash) == ClipboardLocalDecision::Send,
     "a genuine change is sent");
  ok(hash == clipboard_fnv1a(u"first copy"), "and reports the hash to put on the wire");
  ok(core.OnLocalChange(u"first copy", &hash) == ClipboardLocalDecision::SkipDuplicate,
     "the same content is not sent twice");
  ok(core.OnLocalChange(u"second copy", &hash) == ClipboardLocalDecision::Send,
     "a new change after a duplicate is sent");
}

void test_remote_decisions() {
  ClipboardSyncCore core;
  ok(core.OnRemoteData(u"", 0) == ClipboardRemoteDecision::SkipEmpty,
     "an empty remote payload is not applied");
  const std::u16string t = u"host clipboard";
  const uint64_t h = clipboard_fnv1a(t);
  ok(core.OnRemoteData(t, h) == ClipboardRemoteDecision::Apply, "new remote content is applied");
  ok(core.OnRemoteData(t, h) == ClipboardRemoteDecision::SkipDuplicate,
     "the same remote content is not applied twice");
  // Applying arms the echo guard: the OS clipboard write this provokes must not be sent back.
  uint64_t hash = 0;
  ok(core.OnLocalChange(t, &hash) == ClipboardLocalDecision::SkipEcho,
     "the change from applying remote content is recognised as an echo, not re-sent");
}

// The property the whole feature exists for: content does not bounce between two clipboards.
// Two cores stand in for the viewer and the host. A copy on A is sent to B, B applies it, and the
// clipboard write that B's apply causes must NOT come back to A. If it did, A would apply it and
// send it to B, forever.
void test_no_echo_loop() {
  ClipboardSyncCore a;  // the viewer
  ClipboardSyncCore b;  // the host
  uint64_t hashA = 0;

  const std::u16string text = u"one trip only";
  ok(a.OnLocalChange(text, &hashA) == ClipboardLocalDecision::Send, "A sends its new clipboard");

  // B receives it and applies it to its own clipboard.
  ok(b.OnRemoteData(text, hashA) == ClipboardRemoteDecision::Apply, "B applies A's clipboard");

  // Applying fires B's own clipboard listener with the same text. B must not send it back.
  uint64_t hashB = 0;
  ok(b.OnLocalChange(text, &hashB) == ClipboardLocalDecision::SkipEcho,
     "B does NOT send the applied content back -- the loop is broken here");

  // And symmetrically: were B's echo somehow delivered to A, A would refuse to apply its own text.
  ok(a.OnRemoteData(text, hashA) == ClipboardRemoteDecision::SkipEcho,
     "A would refuse to re-apply the content it originated");

  // A genuinely new copy still flows.
  const std::u16string next = u"a second, different copy";
  uint64_t hashA2 = 0;
  ok(a.OnLocalChange(next, &hashA2) == ClipboardLocalDecision::Send, "a later new copy still sends");
  ok(b.OnRemoteData(next, hashA2) == ClipboardRemoteDecision::Apply, "and B applies it");
}

// The clipboard that was already there when the sync started must not be published as news.
void test_baseline_is_not_news() {
  ClipboardSyncCore core;
  uint64_t hash = 0;
  core.SeedBaseline(u"copied before GNLink started");
  ok(core.OnLocalChange(u"copied before GNLink started", &hash) ==
         ClipboardLocalDecision::SkipDuplicate,
     "the clipboard already present at startup is not sent");
  // The baseline must not gag the session: a real copy afterwards still travels.
  ok(core.OnLocalChange(u"copied during the session", &hash) == ClipboardLocalDecision::Send,
     "a copy made during the session is still sent");
  ClipboardSyncCore empty;
  empty.SeedBaseline(u"");
  ok(empty.OnLocalChange(u"anything", &hash) == ClipboardLocalDecision::Send,
     "an empty startup clipboard seeds no baseline");
}

// The session-boundary rules: the first poll adopts the host's generation WITHOUT taking its
// contents, and each session asks once for the local clipboard to be pushed.
void test_client_policy() {
  ClipboardClientPolicy policy;
  policy.OnConnected();

  ok(policy.TakeInitialPush(), "a new session asks for one initial push");
  ok(!policy.TakeInitialPush(), "...and only once");

  ok(policy.ShouldPoll(0), "the first poll is due immediately");
  policy.NotePolled(1000);
  ok(!policy.ShouldPoll(1000 + kClipboardPollIntervalUs - 1), "later polls wait for the interval");
  ok(policy.ShouldPoll(1000 + kClipboardPollIntervalUs), "...and are due once it passes");

  // The host is at generation 5 from a session that has ended, and offers its contents.
  ok(!policy.OnPollReply(5, true),
     "the FIRST reply is a baseline: the host's old clipboard is NOT applied");
  ok(policy.knownGeneration() == 5, "but its generation is adopted, so later changes are noticed");

  // From here the session is live and real changes do apply.
  ok(policy.OnPollReply(6, true), "a change during the session IS applied");
  ok(!policy.OnPollReply(6, false), "a reply with no data applies nothing");
  ok(policy.knownGeneration() == 6, "the generation keeps up");

  // Reconnecting re-arms both, so a second session cannot inherit the first one's host clipboard.
  policy.OnConnected();
  ok(policy.TakeInitialPush(), "reconnecting asks for the push again");
  ok(!policy.OnPollReply(9, true), "and its first reply is a baseline too");
  ok(policy.knownGeneration() == 9, "adopting the newer generation");
}

// The reported bug, start to finish, against the real core and the real policy.
//
// Copy 'a' on the PC during a session, close the viewer, copy 'b' on the client, reopen. The old
// 'a' must not come back and win: what the user copied last is 'b', and 'b' is what should travel.
void test_stale_host_clipboard_does_not_win() {
  const std::u16string a = u"the PC's clipboard from the previous session";
  const std::u16string b = u"what the user copied while GNLink was closed";

  // The host still holds 'a' at generation 1: its state outlives any one viewer.
  const uint64_t hostGeneration = 1;

  // A fresh client connects -- a new process, so it starts at generation 0 knowing nothing.
  ClipboardSyncCore client;
  ClipboardClientPolicy policy;
  policy.OnConnected();

  ok(policy.TakeInitialPush(), "repro: the fresh session asks for its initial push");
  uint64_t pushHash = 0;
  ok(client.OnLocalChange(b, &pushHash) == ClipboardLocalDecision::Send,
     "repro: the client's current clipboard 'b' is what gets sent to the host");

  // The host's first answer still carries 'a', because generation 1 > 0.
  ok(!policy.OnPollReply(hostGeneration, /*hasData=*/true),
     "repro: the stale 'a' is NOT applied over 'b' -- this is the bug, fixed");

  // Negative control: the reply really did carry applicable content, so the test above is not
  // passing merely because there was nothing to apply. Had it been applied, 'a' would have won.
  ok(client.OnRemoteData(a, clipboard_fnv1a(a)) == ClipboardRemoteDecision::Apply,
     "repro (control): that content WAS applicable -- applying it would have overwritten 'b'");
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  test_hash();
  test_codec();
  test_codec_boundaries();
  test_local_decisions();
  test_remote_decisions();
  test_no_echo_loop();
  test_baseline_is_not_news();
  test_client_policy();
  test_stale_host_clipboard_does_not_win();
  std::printf("clipboard_sync_core_test: %s (%d passed, %d failed)\n", gFail == 0 ? "PASS" : "FAIL",
              gPass, gFail);
  return gFail == 0 ? 0 : 1;
}

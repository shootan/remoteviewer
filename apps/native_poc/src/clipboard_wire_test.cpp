// Wire round-trip test for clipboard text sync (K1): the client helpers over a ControlLink, against
// a mock host that parses and replies exactly as host_control_session.cpp's Serve loop does.
//
// The core test proves the codec round-trips in a buffer. This proves the next layer: that
// send_clipboard_update and poll_clipboard drive a ControlLink correctly -- writing the fixed header
// and the variable UTF-16 payload, ending the message, and reading back a reply framed the same way
// the real host frames it. No sockets, no OS clipboard, no session: a LoopbackLink hands each whole
// client message to an inline host function and feeds the reply back.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "clipboard_sync.hpp"
#include "native_video_client_tcp_control.hpp"
#include "udp_control_channel.hpp"

namespace {

using remote60::native_poc::ClipboardClientPolicy;
using remote60::native_poc::ClipboardLocalDecision;
using remote60::native_poc::ClipboardPollReply;
using remote60::native_poc::ClipboardRemoteDecision;
using remote60::native_poc::ClipboardSyncCore;
using remote60::native_poc::ControlClipboardRequestMessage;
using remote60::native_poc::ControlClipboardUpdateMessage;
using remote60::native_poc::ControlInputAckMessage;
using remote60::native_poc::ControlLink;
using remote60::native_poc::MessageHeader;
using remote60::native_poc::MessageType;
using remote60::native_poc::build_clipboard_data;
using remote60::native_poc::clipboard_fnv1a;
using remote60::native_poc::clipboard_parse_payload;
using remote60::native_poc::kMagic;
using remote60::native_poc::poll_clipboard;
using remote60::native_poc::send_clipboard_update;

int gPass = 0;
int gFail = 0;

void ok(bool cond, const std::string& what, const std::string& detail = {}) {
  if (cond) {
    ++gPass;
    std::printf("PASS  %s%s%s\n", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  } else {
    ++gFail;
    std::printf("FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  }
}

// The mock host's clipboard, and what a client update did to it.
struct MockHost {
  uint64_t generation = 0;
  uint64_t hash = 0;
  std::u16string text;
  std::u16string lastApplied;  // the last text a client update pushed
  int updatesApplied = 0;
};

// Processes one whole client message and returns the reply bytes, mirroring the two handlers in
// host_control_session.cpp's Serve loop.
std::vector<uint8_t> host_process(MockHost& host, const std::vector<uint8_t>& msg) {
  MessageHeader header{};
  if (msg.size() < sizeof(header)) return {};
  std::memcpy(&header, msg.data(), sizeof(header));
  const auto type = static_cast<MessageType>(header.type);

  if (type == MessageType::ControlClipboardUpdate) {
    ControlClipboardUpdateMessage upd{};
    std::memcpy(&upd, msg.data(), sizeof(upd));
    std::u16string text;
    clipboard_parse_payload(msg.data() + sizeof(upd), msg.size() - sizeof(upd), upd.utf16Count,
                            &text);
    host.lastApplied = text;
    ++host.updatesApplied;
    // The host answers an update with an input ack (send_input_ack).
    ControlInputAckMessage ack{};
    ack.header.magic = kMagic;
    ack.header.type = static_cast<uint16_t>(MessageType::ControlInputAck);
    ack.header.size = static_cast<uint16_t>(sizeof(ack));
    ack.seq = upd.seq;
    std::vector<uint8_t> out(sizeof(ack));
    std::memcpy(out.data(), &ack, sizeof(ack));
    return out;
  }
  if (type == MessageType::ControlClipboardRequest) {
    ControlClipboardRequestMessage req{};
    std::memcpy(&req, msg.data(), sizeof(req));
    const bool hasData = host.generation > req.knownGeneration && !host.text.empty();
    return build_clipboard_data(req.seq, host.generation, hasData, host.text, host.hash, 0);
  }
  return {};
}

// A ControlLink whose EndMessage feeds the accumulated client message to the mock host and queues
// the host's reply for Read. This is the whole request/response, inline and synchronous.
class LoopbackLink : public ControlLink {
 public:
  explicit LoopbackLink(MockHost* host) : host_(host) {}
  bool Read(void* out, size_t len) override {
    if (inbound_.size() - readPos_ < len) return false;
    std::memcpy(out, inbound_.data() + readPos_, len);
    readPos_ += len;
    return true;
  }
  bool Write(const void* data, size_t len) override {
    const auto* p = static_cast<const uint8_t*>(data);
    outbound_.insert(outbound_.end(), p, p + len);
    return true;
  }
  bool EndMessage() override {
    std::vector<uint8_t> reply = host_process(*host_, outbound_);
    outbound_.clear();
    inbound_.insert(inbound_.end(), reply.begin(), reply.end());
    return true;
  }
  bool Alive() const override { return true; }

 private:
  MockHost* host_;
  std::vector<uint8_t> outbound_;
  std::vector<uint8_t> inbound_;
  size_t readPos_ = 0;
};

void test_poll(const std::u16string& hostText, const std::string& label) {
  MockHost host;
  host.text = hostText;
  host.hash = clipboard_fnv1a(hostText);
  host.generation = 5;
  LoopbackLink link(&host);

  ClipboardPollReply reply;
  ok(poll_clipboard(link, 0, 0, &reply), label + ": a stale client gets a reply");
  ok(reply.hasData, label + ": with data");
  ok(reply.generation == 5, label + ": at the host generation");
  ok(reply.text == hostText, label + ": and the exact host text");
  ok(reply.hash == host.hash, label + ": and its hash");

  // A client already at the host generation is told there is nothing new.
  ClipboardPollReply current;
  ok(poll_clipboard(link, 5, 0, &current), label + ": a current client gets a reply too");
  ok(!current.hasData, label + ": with no data");
  ok(current.generation == 5, label + ": still carrying the generation");
}

void test_update(const std::u16string& viewerText, const std::string& label) {
  MockHost host;
  LoopbackLink link(&host);
  ok(send_clipboard_update(link, 1, viewerText, clipboard_fnv1a(viewerText), 0),
     label + ": the update is acked");
  ok(host.updatesApplied == 1, label + ": the host applied exactly one update");
  ok(host.lastApplied == viewerText, label + ": with the exact viewer text");
}

// The reported bug, over the wire, with the real policy and the real core against a host that
// answers exactly as Serve does.
//
// The core test proves the client refuses the stale reply. This proves the two things that refusal
// is FOR: that the host ends up holding what the client had, and that refusing the first reply does
// not also deafen the session to later host copies.
void test_stale_session_scenario() {
  const std::u16string a = u"the PC's clipboard from a session that is over";
  const std::u16string b = u"what the user copied while GNLink was closed";
  const std::u16string c = u"what the user copies on the PC during THIS session";

  MockHost host;
  host.text = a;
  host.hash = clipboard_fnv1a(a);
  host.generation = 1;  // left over from the previous session; the hub outlives a viewer
  LoopbackLink link(&host);

  ClipboardSyncCore client;
  ClipboardClientPolicy policy;
  policy.OnConnected();

  // (b)/(d) The connecting client is the source: its current clipboard goes out once, and the
  // host really does end up holding it.
  ok(policy.TakeInitialPush(), "scenario: the new session asks for its one initial push");
  uint64_t pushHash = 0;
  ok(client.OnLocalChange(b, &pushHash) == ClipboardLocalDecision::Send,
     "scenario: 'b' is judged worth sending");
  ok(send_clipboard_update(link, 1, b, pushHash, 0), "scenario: 'b' reaches the host");
  ok(host.lastApplied == b, "scenario: THE HOST NOW HOLDS 'b' -- the connecting client won");

  // (a) The host still offers its old 'a', because generation 1 is above the client's 0. The
  // baseline is what stops it landing -- and the reply really did carry content, so this is not
  // passing because there was nothing to refuse.
  ClipboardPollReply first;
  ok(poll_clipboard(link, policy.knownGeneration(), 0, &first), "scenario: the first poll answers");
  ok(first.hasData && first.text == a,
     "scenario (control): the host DID offer the stale 'a' on that first poll");
  ok(!policy.OnPollReply(first.generation, first.hasData),
     "scenario: the baseline refuses it, so 'a' never overwrites 'b'");
  ok(host.lastApplied == b, "scenario: 'b' still stands on the host");

  // (c) The session is live now, so a copy made on the PC while connected must still arrive. A
  // baseline that silenced this would have traded one bug for another.
  host.text = c;
  host.hash = clipboard_fnv1a(c);
  host.generation = 2;
  ClipboardPollReply later;
  ok(poll_clipboard(link, policy.knownGeneration(), 0, &later), "scenario: a later poll answers");
  ok(policy.OnPollReply(later.generation, later.hasData),
     "scenario: a host copy made DURING the session is applied");
  ok(later.text == c, "scenario: and it is the text the host copied");

  // (e) Applying it does not send it back: the loop stays broken across the session boundary too.
  ok(client.OnRemoteData(later.text, later.hash) == ClipboardRemoteDecision::Apply,
     "scenario: the client applies it");
  uint64_t echoHash = 0;
  ok(client.OnLocalChange(c, &echoHash) == ClipboardLocalDecision::SkipEcho,
     "scenario: and does NOT send it straight back");
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  test_poll(u"host clipboard ascii", "poll-ascii");
  test_poll(u"호스트 클립보드", "poll-korean");                 // "호스트 클립보드"
  test_poll(std::u16string{u'x', u'\xD83D', u'\xDE00', u'y'}, "poll-emoji");  // x U+1F600 y

  test_update(u"viewer clipboard ascii", "update-ascii");
  test_update(u"뷰어에서 복사", "update-korean");                // "뷰어에서 복사"
  test_update(std::u16string(4096, u'z'), "update-large");

  test_stale_session_scenario();

  std::printf("clipboard_wire_test: %s (%d passed, %d failed)\n", gFail == 0 ? "PASS" : "FAIL",
              gPass, gFail);
  return gFail == 0 ? 0 : 1;
}

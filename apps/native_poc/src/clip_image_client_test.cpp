// ClipImageClient against a scripted host (a fake ControlLink): the races a real host run cannot
// hit on demand (Codex review of 4528dc9, DECISIONS §6).
//
//   ① a cancel the host answers "already published" / "still verifying" / never answers, a reply
//     naming another transfer, and a newer copy waiting while the older cancel is unsettled --
//     it must not be offered into a Busy host, and an old id's answer must not end it;
//   ② a new copy voids the running transfer at once (before its own package exists), a copy that
//     cannot be read still cancels the older one and says so, and a refused older copy's text does
//     not follow a newer copy;
//   ③ a package the host refuses, and one that never left this PC, each end with an outcome.
//
// The client's own code runs unchanged: Pump reads and writes the link exactly as with the real
// UDP control channel; only the other end is a script.

#include "clip_image_client.hpp"  // first: it brings winsock2.h, which must precede windows.h

#include <objbase.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>


using namespace remote60::native_poc;

namespace {

int g_checks = 0, g_failed = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

/** The host end of the control link, as a script. */
class ScriptedHost : public ControlLink {
 public:
  // What the next answers say. Set by the test between Pumps.
  uint8_t offerVerdict = static_cast<uint8_t>(ClipImageVerdict::Accept);
  ClipImageState cancelAnswer = ClipImageState::Cancelled;
  ClipImageState statusAnswer = ClipImageState::Pulling;
  uint64_t statusIdOverride = 0;  // non-zero: the Status reply names this id instead (a stale answer)
  bool dropNextReply = false;     // the reply never arrives (the link fails)
  std::function<void()> onOffer;  // runs while an offer is "in flight" (before its reply is read)

  struct Seen {
    uint16_t type;
    uint64_t transferId;
    uint8_t reason;
  };
  std::vector<Seen> seen;

  bool Write(const void* data, size_t len) override {
    const auto* p = static_cast<const uint8_t*>(data);
    buf_.insert(buf_.end(), p, p + len);
    return true;
  }
  bool EndMessage() override {
    MessageHeader h{};
    std::memcpy(&h, buf_.data(), sizeof(h));
    reply_.clear();
    rpos_ = 0;
    const auto type = static_cast<MessageType>(h.type);
    if (type == MessageType::ControlClipImageOffer) {
      ControlClipImageOfferMessage m{};
      std::memcpy(&m, buf_.data(), sizeof(m));
      seen.push_back({h.type, m.transferId, 0});
      ControlClipImageOfferReplyMessage r{};
      r.header.type = static_cast<uint16_t>(MessageType::ControlClipImageOfferReply);
      r.header.size = sizeof(r);
      r.seq = m.seq;
      r.verdict = offerVerdict;
      r.transferId = m.transferId;
      r.epochTag = 0x1234000000000001ull;
      r.bulkGen = 7;
      r.pullWindow = 2;
      put(r);
      if (offerVerdict == static_cast<uint8_t>(ClipImageVerdict::Accept)) current_ = m.transferId;
      if (onOffer) onOffer();
    } else if (type == MessageType::ControlClipImageCancel) {
      ControlClipImageCancelMessage m{};
      std::memcpy(&m, buf_.data(), sizeof(m));
      seen.push_back({h.type, m.transferId, m.reason});
      cancelReason_ = m.reason;
      put(status(MessageType::ControlClipImageCancelReply, m.seq, m.transferId, cancelAnswer, m.reason));
    } else if (type == MessageType::ControlClipImageStatus) {
      ControlClipImageStatusMessage m{};
      std::memcpy(&m, buf_.data(), sizeof(m));
      seen.push_back({h.type, m.transferId, 0});
      put(status(MessageType::ControlClipImageStatusReply, m.seq, statusIdOverride ? statusIdOverride : m.transferId,
                 statusAnswer, cancelReason_));
    }
    buf_.clear();
    return true;
  }
  bool Read(void* out, size_t len) override {
    if (dropNextReply) {
      dropNextReply = false;
      return false;
    }
    if (rpos_ + len > reply_.size()) return false;
    std::memcpy(out, reply_.data() + rpos_, len);
    rpos_ += len;
    return true;
  }
  bool Alive() const override { return true; }

  int count(MessageType t) const {
    int n = 0;
    for (const Seen& s : seen) n += s.type == static_cast<uint16_t>(t);
    return n;
  }
  const Seen* last(MessageType t) const {
    for (auto it = seen.rbegin(); it != seen.rend(); ++it) {
      if (it->type == static_cast<uint16_t>(t)) return &*it;
    }
    return nullptr;
  }

 private:
  template <typename T>
  void put(const T& m) {
    const auto* p = reinterpret_cast<const uint8_t*>(&m);
    reply_.insert(reply_.end(), p, p + sizeof(m));
  }
  static ControlClipImageStatusReplyMessage status(MessageType t, uint32_t seq, uint64_t id, ClipImageState s,
                                                   uint8_t reason) {
    ControlClipImageStatusReplyMessage r{};
    r.header.type = static_cast<uint16_t>(t);
    r.header.size = sizeof(r);
    r.seq = seq;
    r.transferId = id;
    r.state = static_cast<uint8_t>(s);
    r.reason = (s == ClipImageState::Cancelled) ? reason : 0;
    return r;
  }
  std::vector<uint8_t> buf_, reply_;
  size_t rpos_ = 0;
  uint64_t current_ = 0;
  uint8_t cancelReason_ = 0;
};

ClipSnapshot dib(uint32_t w, uint32_t h, uint32_t seed, const std::u16string& text = u"") {
  ClipSnapshot s;
  s.kind = ClipSnapshotKind::Dib;
  s.sequence = seed;
  s.text = text;
  s.bytes.resize(sizeof(BITMAPV5HEADER) + static_cast<size_t>(w) * h * 4);
  BITMAPV5HEADER bh{};
  bh.bV5Size = sizeof(bh);
  bh.bV5Width = static_cast<LONG>(w);
  bh.bV5Height = static_cast<LONG>(h);
  bh.bV5Planes = 1;
  bh.bV5BitCount = 32;
  bh.bV5Compression = BI_BITFIELDS;
  bh.bV5RedMask = 0x00FF0000;
  bh.bV5GreenMask = 0x0000FF00;
  bh.bV5BlueMask = 0x000000FF;
  bh.bV5AlphaMask = 0xFF000000;
  bh.bV5CSType = LCS_sRGB;
  std::memcpy(s.bytes.data(), &bh, sizeof(bh));
  uint32_t x = seed | 1u;
  for (size_t o = sizeof(bh); o + 4 <= s.bytes.size(); o += 4) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    const uint32_t px = x | 0xFF000000u;
    std::memcpy(s.bytes.data() + o, &px, 4);
  }
  return s;
}

bool wait_for(const std::function<bool()>& f, int ms) {
  for (int i = 0; i < ms / 10; ++i) {
    if (f()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return f();
}

struct Rig {
  ClipImageClient client;
  ScriptedHost host;
  Rig() {
    client.Start([](const void*, size_t) { return true; }, [] { return uint64_t{0}; }, [] { return false; }, 1200,
                 BulkRateConfig{}, [](const std::string& line) { std::printf("      client: %s\n", line.c_str()); });
    client.SetBulkNegotiated(true);
    client.SetHostSupports(true);
  }
  ~Rig() { client.Stop(); }
  /** Submits a copy and pumps until it is offered (and, with Accept, being served). */
  bool offer(ClipSnapshot s) {
    const uint64_t packaged = client.GetCounters().packaged;
    client.SubmitSnapshot(std::move(s));
    if (!wait_for([&] { return client.GetCounters().packaged > packaged; }, 10000)) return false;
    const int offers = host.count(MessageType::ControlClipImageOffer);
    for (int i = 0; i < 20 && host.count(MessageType::ControlClipImageOffer) == offers; ++i) client.Pump(host);
    return host.count(MessageType::ControlClipImageOffer) > offers;
  }
  void pump_for(int ms) {
    for (int i = 0; i < ms / 20; ++i) {
      client.Pump(host);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  ClipOutcome outcome() const { return client.GetProgress().outcome; }
};

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);

  {
    std::printf("\n--- ① the user cancels a transfer the host has ALREADY published ---\n");
    Rig r;
    check("offered and being served", r.offer(dib(64, 64, 11)) && r.client.Active());
    r.host.cancelAnswer = ClipImageState::Published;
    r.client.CancelByUser();
    check("the cancel goes out", r.client.Pump(r.host) == 1 && r.host.count(MessageType::ControlClipImageCancel) == 1);
    const auto p = r.client.GetProgress();
    check("the outcome is 'it was already there', NOT 'cancelled'", p.outcome == ClipOutcome::CancelTooLate && !p.cancelling);
    check("...counted as published, not cancelled",
          r.client.GetCounters().published == 1 && r.client.GetCounters().cancelled == 0);
  }
  {
    std::printf("\n--- ① cancel while the host is still verifying, and a newer copy waiting ---\n");
    Rig r;
    check("offered and being served", r.offer(dib(64, 64, 21)) && r.client.Active());
    const uint64_t oldId = r.host.last(MessageType::ControlClipImageOffer)->transferId;
    r.host.cancelAnswer = ClipImageState::Verifying;
    r.host.statusAnswer = ClipImageState::Verifying;
    const uint64_t packaged = r.client.GetCounters().packaged;
    r.client.SubmitSnapshot(dib(64, 64, 22));  // the newer copy
    r.client.Pump(r.host);
    check("the older transfer is cancelled at once, as superseded",
          r.host.count(MessageType::ControlClipImageCancel) == 1 &&
              r.host.last(MessageType::ControlClipImageCancel)->reason == static_cast<uint8_t>(ClipImageReason::Superseded));
    check("...and it is not settled: the host is still verifying", r.client.GetProgress().cancelling);
    wait_for([&] { return r.client.GetCounters().packaged > packaged; }, 10000);
    r.pump_for(1200);
    check("the newer copy is NOT offered while the older one is unsettled (it would meet Busy)",
          r.host.count(MessageType::ControlClipImageOffer) == 1);
    check("...the host is asked about the OLDER transfer", r.host.last(MessageType::ControlClipImageStatus) &&
                                                            r.host.last(MessageType::ControlClipImageStatus)->transferId == oldId);
    // A stale answer naming another transfer must settle nothing.
    r.host.statusAnswer = ClipImageState::Cancelled;
    r.host.statusIdOverride = 0x5151515151515151ull;
    r.pump_for(700);
    check("an answer naming another transfer settles nothing", r.client.GetProgress().cancelling &&
                                                               r.host.count(MessageType::ControlClipImageOffer) == 1);
    r.host.statusIdOverride = 0;
    r.pump_for(700);
    check("the host's real answer settles it: cancelled (superseded)",
          !r.client.GetProgress().cancelling && r.outcome() == ClipOutcome::Cancelled &&
              r.client.GetProgress().detail == static_cast<uint8_t>(ClipImageReason::Superseded));
    r.host.statusAnswer = ClipImageState::Pulling;  // the host now serves the newer transfer
    r.pump_for(200);
    const auto* offer = r.host.last(MessageType::ControlClipImageOffer);
    check("...and only then is the newer copy offered", r.host.count(MessageType::ControlClipImageOffer) == 2 && offer &&
                                                            offer->transferId != oldId);
  }
  {
    std::printf("\n--- ① the cancel's answer never arrives ---\n");
    Rig r;
    check("offered and being served", r.offer(dib(64, 64, 31)) && r.client.Active());
    r.client.CancelByUser();
    r.host.dropNextReply = true;
    check("the link fails", r.client.Pump(r.host) == -1);
    check("the outcome is 'not known', NOT 'cancelled'",
          r.outcome() == ClipOutcome::CancelUnconfirmed && !r.client.GetProgress().cancelling);
  }
  {
    std::printf("\n--- ① an ordinary user cancel, confirmed ---\n");
    Rig r;
    check("offered and being served", r.offer(dib(64, 64, 41)) && r.client.Active());
    r.client.CancelByUser();
    r.client.Pump(r.host);
    check("cancelled by the user, and the host said so", r.outcome() == ClipOutcome::Cancelled &&
                                                          r.client.GetProgress().detail ==
                                                              static_cast<uint8_t>(ClipImageReason::User));
  }
  {
    std::printf("\n--- ② a new copy that cannot be read still cancels the older transfer ---\n");
    Rig r;
    check("offered and being served", r.offer(dib(64, 64, 51)) && r.client.Active());
    r.client.CancelForNewerCopy();  // what the viewer does on a genuine new copy, before reading it
    r.client.NoteLocalCopyNotSent(ClipPackageResult::TooLarge);
    r.client.Pump(r.host);
    check("the older transfer is cancelled (superseded) without any new package",
          r.host.count(MessageType::ControlClipImageCancel) == 1 &&
              r.host.last(MessageType::ControlClipImageCancel)->reason == static_cast<uint8_t>(ClipImageReason::Superseded) &&
              r.client.GetCounters().packaged == 1);
    const auto p = r.client.GetProgress();
    check("the last line is about the NEW copy: too large, not sent",
          p.outcome == ClipOutcome::NotSent && p.detail == static_cast<uint8_t>(ClipPackageResult::TooLarge));
  }
  {
    std::printf("\n--- ② / ③ a refused copy's text, then a newer copy ---\n");
    Rig r;
    r.host.offerVerdict = static_cast<uint8_t>(ClipImageVerdict::Disabled);
    check("offered", r.offer(dib(64, 64, 61, u"text of the refused copy")));
    check("the refusal is an outcome with the host's reason",
          r.outcome() == ClipOutcome::Refused && r.client.GetProgress().detail == static_cast<uint8_t>(ClipImageVerdict::Disabled));
    std::u16string t;
    check("its text goes by text sync while it is still the newest copy", r.client.TakeFallbackText(&t) && !t.empty());
    // Again, but a newer copy arrives before the text is taken.
    check("offered again", r.offer(dib(64, 64, 62, u"older text")));
    r.client.CancelForNewerCopy();
    check("a newer copy makes the older refused copy's text void (it would arrive after it)",
          !r.client.TakeFallbackText(&t));
  }
  {
    std::printf("\n--- ② a refusal that arrives after a newer copy was made ---\n");
    Rig r;
    r.host.offerVerdict = static_cast<uint8_t>(ClipImageVerdict::Disabled);
    r.host.onOffer = [&r] {
      r.host.onOffer = nullptr;
      r.client.CancelForNewerCopy();  // the user copies something else while the offer is on the wire
    };
    check("offered", r.offer(dib(64, 64, 63, u"text of the now-older copy")));
    std::u16string t;
    check("the older copy's refusal brings no text after the newer copy", !r.client.TakeFallbackText(&t));
    check("...and no 'refused' line about a copy the user has already replaced", r.outcome() != ClipOutcome::Refused);
  }
  {
    std::printf("\n--- ③ a copy that never leaves this PC ---\n");
    Rig r;
    r.client.SubmitSnapshot(dib(8193, 1, 81));  // one pixel over kClipImageMaxSide
    check("a copy over the size gate ends with 'not sent: too large'",
          wait_for([&] { return r.outcome() == ClipOutcome::NotSent; }, 5000) &&
              r.client.GetProgress().detail == static_cast<uint8_t>(ClipPackageResult::TooLarge));
    r.pump_for(200);
    check("...and nothing is offered", r.host.count(MessageType::ControlClipImageOffer) == 0);
  }
  {
    std::printf("\n--- normal path: offered, served, published ---\n");
    Rig r;
    check("offered and being served", r.offer(dib(64, 64, 71)) && r.client.Active());
    r.host.statusAnswer = ClipImageState::Published;
    r.pump_for(800);
    check("the host's Published ends it as published", r.outcome() == ClipOutcome::Published && !r.client.Active());
  }

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  CoUninitialize();
  return g_failed ? 1 : 0;
}

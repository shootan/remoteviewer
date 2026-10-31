// r2 ③ (t-zdmsd4gb): a repeated offer of the image already being received must not give that image's
// bulk away. The product HostClipImageService with the session's BulkArbiter, its offer handler called
// exactly as the host's control session calls it; no network, no clipboard (a publisher that keeps
// nothing).
//
// The defect (723001c): BulkArbiter::TryAcquire said yes to the holder asking again, the receiver then
// answered the repeated offer Busy, and the refusal released the arbiter -- the bulk read Idle while
// the image was still being received, so a file paste could take it at the same time.
//
// Tags: pure-logic.

#include <cstdio>
#include <cstring>
#include <string>

#include "bulk_arbiter.hpp"
#include "host_clip_image.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0, gFailures = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

class NullPublisher : public HostClipImagePublisher {
 public:
  bool Enabled() const override { return true; }
  uint64_t Sequence() const override { return 100; }
  ClipPublishResult Publish(uint64_t, HGLOBAL png, HGLOBAL dib, const std::u16string&) override {
    if (png) GlobalFree(png);
    if (dib) GlobalFree(dib);
    return ClipPublishResult::Superseded;
  }
};

ControlClipImageOfferMessage offer(uint64_t transferId, uint32_t seq) {
  ControlClipImageOfferMessage m{};
  m.header.magic = kMagic;
  m.header.type = static_cast<uint16_t>(MessageType::ControlClipImageOffer);
  m.header.size = sizeof(m);
  m.seq = seq;
  m.formats = kClipImageFormatPng;
  m.transferId = transferId;
  m.revision = 7;
  m.pngBytes = 4096;
  m.width = 32;
  m.height = 32;
  for (int i = 0; i < 32; ++i) m.sha256[i] = static_cast<uint8_t>(i * 7 + 1);
  return m;
}

}  // namespace

int main() {
  NullPublisher pub;
  HostClipImageService svc(&pub);
  BulkArbiter arbiter;
  svc.SetBulkArbiter(&arbiter);
  svc.SetBulkNegotiated(true);
  svc.Start([](const void*, size_t) { return true; }, 1200);

  const uint64_t A = 0xA11CE;
  const auto r1 = svc.HandleOffer(offer(A, 1), 5);
  check("image A is accepted and holds the session's bulk as Image",
        r1.verdict == static_cast<uint8_t>(ClipImageVerdict::Accept) && arbiter.use() == BulkUse::Image && arbiter.owner() == A,
        "verdict=" + std::to_string(r1.verdict) + " use=" + bulk_use_name(arbiter.use()));

  // The same offer again (a viewer that sent it twice, a stale resend) while A is being received.
  const auto r2 = svc.HandleOffer(offer(A, 2), 5);
  check("the repeated offer of A is refused (Busy)", r2.verdict == static_cast<uint8_t>(ClipImageVerdict::Busy),
        "verdict=" + std::to_string(r2.verdict));
  check("THE BULK IS STILL A's (Image) -- the refusal gave nothing away", arbiter.use() == BulkUse::Image && arbiter.owner() == A,
        std::string("use=") + bulk_use_name(arbiter.use()) + " owner=" + std::to_string(arbiter.owner()));
  check("...so a file paste asking now is Busy (never at the same time as the image)",
        !arbiter.TryAcquire(BulkUse::File, 0xF11E) && arbiter.use() == BulkUse::Image);

  // A different image meanwhile: Busy too, and it does not touch A's hold either.
  const auto r3 = svc.HandleOffer(offer(0xB0B, 3), 5);
  check("another image meanwhile is Busy and A still holds the bulk",
        r3.verdict == static_cast<uint8_t>(ClipImageVerdict::Busy) && arbiter.owner() == A);

  // Negative control: the release this fix restricts is still made for what an offer DID take.
  BulkArbiter fresh;
  HostClipImageService svc2(&pub);
  svc2.SetBulkArbiter(&fresh);
  svc2.SetBulkNegotiated(true);
  svc2.Start([](const void*, size_t) { return true; }, 1200);
  auto bad = offer(0xC0C, 1);
  bad.width = 0;  // refused by the rules after the bulk was taken
  const auto r4 = svc2.HandleOffer(bad, 5);
  check("control: an offer refused after it took the bulk gives it back (Idle)",
        r4.verdict != static_cast<uint8_t>(ClipImageVerdict::Accept) && fresh.use() == BulkUse::Idle,
        "verdict=" + std::to_string(r4.verdict) + " use=" + bulk_use_name(fresh.use()));
  svc.Stop();
  svc2.Stop();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "PASSED", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
ROOT = r'D:\remote\remote'
F = ROOT + r'\apps\native_poc\src\viewer_udp_recovery_test.cpp'
edit(F, [
# 1) rig: key-frame publish observation (exact seq), and polling-miss diagnostics
("""  std::atomic<uint64_t> publishedCount{0};
  std::atomic<uint32_t> lastPublishedSeq{0};
  std::atomic<uint32_t> maxPublishedSeq{0};
  std::atomic<uint64_t> lastPublishedCaptureUs{0};
""",
"""  std::atomic<uint64_t> publishedCount{0};
  std::atomic<uint32_t> lastPublishedSeq{0};
  std::atomic<uint32_t> maxPublishedSeq{0};
  std::atomic<uint64_t> lastPublishedCaptureUs{0};
  // F-22: which key frames were actually published (by seq), so a scenario can assert that one
  // exact IDR was shown rather than "some frame at or past its seq". missedPublishes counts
  // frame versions the 2 ms poll below never saw -- diagnostics for a miss, not a pass.
  std::atomic<uint32_t> lastPublishedKeySeq{0};
  std::atomic<uint64_t> missedPublishes{0};
  std::mutex publishedMu;
  std::vector<uint32_t> publishedKeySeqs;
  bool published_key(uint32_t seq) {
    std::lock_guard<std::mutex> lk(publishedMu);
    return std::find(publishedKeySeqs.begin(), publishedKeySeqs.end(), seq) != publishedKeySeqs.end();
  }
  std::string published_key_seqs() {
    std::lock_guard<std::mutex> lk(publishedMu);
    std::string s;
    for (uint32_t q : publishedKeySeqs) { if (!s.empty()) s += ","; s += std::to_string(q); }
    return s.empty() ? "none" : s;
  }
"""),
("""        if (f.version != seenVersion) {
          seenVersion = f.version;
          ++publishedCount;
          lastPublishedSeq = f.seq;
          if (f.seq > maxPublishedSeq.load()) maxPublishedSeq = f.seq;
          lastPublishedCaptureUs = f.captureUs;
""",
"""        if (f.version != seenVersion) {
          if (seenVersion != 0 && f.version > seenVersion + 1) missedPublishes += f.version - seenVersion - 1;
          seenVersion = f.version;
          ++publishedCount;
          lastPublishedSeq = f.seq;
          if (f.seq > maxPublishedSeq.load()) maxPublishedSeq = f.seq;
          lastPublishedCaptureUs = f.captureUs;
          if (f.key) {
            lastPublishedKeySeq = f.seq;
            std::lock_guard<std::mutex> pl(publishedMu);
            publishedKeySeqs.push_back(f.seq);
          }
"""),
# 2) S12: the good IDR itself (exact seq, as a key frame) is published; it left intact
("""  const KeyOnWire good = send_key_and_wait_on_wire(host, "good IDR");
  const uint32_t goodIdr = good.keySeq;
  CHECK(goodIdr != 0, "a good IDR went out");
""",
"""  const KeyOnWire good = send_key_and_wait_on_wire(host, "good IDR");
  const uint32_t goodIdr = good.keySeq;
  CHECK(goodIdr != 0, "a good IDR went out");
  CHECK(host.corrupted_keys() == 1, "the good IDR left intact (corrupted_keys " + std::to_string(host.corrupted_keys()) + ")");
"""),
("""  const bool shown = pump_until(host, rig, 3000, [&]() {
    return rig.publishedCount.load() > publishedBefore && rig.maxPublishedSeq.load() >= goodIdr;
  });
  CHECK(shown, "the good IDR was decoded and published (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
""",
"""  // Exactly that IDR, published as a key frame -- not "some frame at or past its seq" (a later P
  // would pass that) and never a 0 sentinel (goodIdr != 0 is asserted above).
  const bool shown = pump_until(host, rig, 3000, [&]() { return goodIdr != 0 && rig.published_key(goodIdr); });
  CHECK(shown, "the good IDR seq " + std::to_string(goodIdr) + " itself was decoded and published as a key frame (key seqs published " +
                   rig.published_key_seqs() + ", max seq " + std::to_string(rig.maxPublishedSeq.load()) + ", missed publishes " +
                   std::to_string(rig.missedPublishes.load()) + ")");
  CHECK(rig.publishedCount.load() > publishedBefore, "the good IDR produced output (published " + std::to_string(rig.publishedCount.load() - publishedBefore) + ")");
"""),
])
print('s12 exact seq edit ok')

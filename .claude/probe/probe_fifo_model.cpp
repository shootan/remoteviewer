// Audit probe (untracked): state-tracking MODEL of H264Encoder's three accepted-input FIFOs
// (mf_h264_codec.cpp: push at 1927-1929, overflow trim at 1931-1934, normal output pop at
// 1864-1870, empty-output pop at 1827-1834) driven against the REAL epoch gate
// (host_epoch_gate.hpp, header-only). No MFT involved: the MFT is modelled as a queue that
// returns outputs in input order (the FIFO premise the product relies on).
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "host_epoch_gate.hpp"

using remote60::native_poc::EpochGate;
using remote60::native_poc::EpochVerdict;
using remote60::native_poc::epoch_gate_judge;
using remote60::native_poc::epoch_verdict_name;

namespace {

struct Input { int id; bool key; uint64_t epoch; bool producesBytes; };

struct CodecFifos {
  std::deque<int64_t> ts;      // pendingInputSampleTimesHns_
  std::deque<bool> syn;        // pendingInputSynthetic_
  std::deque<uint64_t> epoch;  // pendingInputEpoch_
  static constexpr size_t kMax = 64;  // kPendingEncoderTimestampMax
  void push(const Input& in) {          // :1927-1929 then :1931-1934
    ts.push_back(in.id); syn.push_back(false); epoch.push_back(in.epoch);
    if (ts.size() > kMax) {
      ts.pop_front();
      if (!syn.empty()) syn.pop_front();
      if (!epoch.empty()) epoch.pop_front();
    }
  }
  // :1827-1834 -- the empty-output branch as written today (epoch NOT popped)
  void empty_output_as_written() {
    if (!ts.empty()) { ts.pop_front(); if (!syn.empty()) syn.pop_front(); }
  }
  // :1864-1870 -- normal output: returns the epoch tag the AU gets (0 when the FIFO is empty)
  uint64_t normal_output() {
    uint64_t tag = 0;
    if (!ts.empty()) {
      if (!epoch.empty()) tag = epoch.front();
      ts.pop_front();
      if (!syn.empty()) syn.pop_front();
      if (!epoch.empty()) epoch.pop_front();
    }
    return tag;
  }
};

void scenario_empty_output() {
  std::printf("[A] F1: one empty output before a flush (as written)\n");
  CodecFifos f; EpochGate g; std::deque<Input> mft;
  uint64_t epochNow = 1; int id = 0; uint64_t t = 0;
  auto submit = [&](bool key, bool bytes) { Input in{++id, key, epochNow, bytes}; f.push(in); mft.push_back(in); };
  auto drain_one = [&]() {
    Input in = mft.front(); mft.pop_front();
    if (!in.producesBytes) { f.empty_output_as_written(); std::printf("  input %d -> EMPTY output (ts/syn popped, epoch kept)\n", in.id); return; }
    const uint64_t tag = f.normal_output();
    const EpochVerdict v = epoch_gate_judge(g, epochNow, tag, in.key, 100, t += 16000);
    std::printf("  input %d (epoch %llu, %s) -> AU tagged epoch %llu -> gate %s\n", in.id,
                static_cast<unsigned long long>(in.epoch), in.key ? "KEY" : "P", static_cast<unsigned long long>(tag), epoch_verdict_name(v));
  };
  submit(true, true); drain_one();               // session IDR
  submit(false, true); drain_one();
  submit(false, false); drain_one();             // the empty output
  submit(false, true); drain_one();
  ++epochNow;                                     // FLUSH (ResetTimelineAnchors)
  std::printf("  -- flush: epochNow=%llu, forced key on the next input --\n", static_cast<unsigned long long>(epochNow));
  submit(true, true); drain_one();               // the new epoch's IDR
  submit(false, true); drain_one();
  submit(true, true); drain_one();               // the re-forced IDR after DropAwaitingKey
  std::printf("  gate: droppedOld=%llu droppedNonKey=%llu keysAccepted=%llu\n",
              static_cast<unsigned long long>(g.droppedOldEpoch), static_cast<unsigned long long>(g.droppedAwaitingKey),
              static_cast<unsigned long long>(g.keysAccepted));
}

void scenario_overflow() {
  std::printf("[B] C2: 65 inputs accepted before any output, a flush in the middle (front-trim as written)\n");
  CodecFifos f; EpochGate g; std::deque<Input> mft;
  uint64_t epochNow = 1; int id = 0; uint64_t t = 0;
  auto submit = [&](bool key) { Input in{++id, key, epochNow, true}; f.push(in); mft.push_back(in); };
  submit(true);                                   // input 1: session IDR (epoch 1)
  for (int i = 0; i < 38; ++i) submit(false);    // inputs 2..39 (epoch 1)
  submit(true);                                   // input 40: a keyframe of epoch 1 (e.g. a viewer request)
  ++epochNow;                                     // FLUSH between 40 and 41
  submit(true);                                   // input 41: the new epoch's forced IDR (epoch 2)
  for (int i = 0; i < 24; ++i) submit(false);    // inputs 42..65 -> 65 entries: overflow trims the front once
  std::printf("  fifo depth after 65 pushes: %zu (front now input %lld)\n", f.ts.size(), static_cast<long long>(f.ts.front()));
  // The MFT now drains in input order.
  for (int k = 0; k < 42; ++k) {
    Input in = mft.front(); mft.pop_front();
    const uint64_t tag = f.normal_output();
    const EpochVerdict v = epoch_gate_judge(g, epochNow, tag, in.key, 100, t += 16000);
    if (in.id >= 38 && in.id <= 42)
      std::printf("  input %d (true epoch %llu, %s) -> tagged %llu -> gate %s\n", in.id, static_cast<unsigned long long>(in.epoch),
                  in.key ? "KEY" : "P", static_cast<unsigned long long>(tag), epoch_verdict_name(v));
  }
  std::printf("  gate: keysAccepted=%llu droppedOld=%llu droppedNonKey=%llu droppedUnknown=%llu\n",
              static_cast<unsigned long long>(g.keysAccepted), static_cast<unsigned long long>(g.droppedOldEpoch),
              static_cast<unsigned long long>(g.droppedAwaitingKey), static_cast<unsigned long long>(g.droppedUnknownEpoch));
}

}  // namespace

int main() {
  scenario_empty_output();
  scenario_overflow();
  return 0;
}

// apk-0232 r2 (Codex codex_apk_prelogin_select_src_r1.md N2): the Android decoder sink's held input frame
// (decoder_pending_slot.hpp -- the PRODUCT class the sink uses) with the PRODUCT selection gate, against a codec
// stand-in whose input buffers are taken away and given back on the test's say. The stand-in is the codec only: it
// takes a frame or not, and, as the sink's TryQueueFrameLocked does, pays an IDR to the gate when one goes in.
// Tags: pure-logic.

#include <cstdio>
#include <string>
#include <vector>

#include "decoder_pending_slot.hpp"
#include "selection_ack_gate.hpp"

using namespace remote60::native_poc;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}

struct Codec {
  bool busy = false;  // no input buffer
  int failNext = 0;   // the next N attempts find no input buffer (then buffers are back)
  std::vector<uint64_t> queued;
  SelectionAckGate* gate = nullptr;
  bool operator()(UdpH264AssembledFrame& f) {
    if (busy) return false;
    if (failNext > 0) {
      --failNext;
      return false;
    }
    queued.push_back(f.header.streamGeneration);
    if ((f.header.flags & 1u) != 0) gate->KeyDelivered(f.header.streamGeneration);
    return true;
  }
  std::string list() const {
    std::string s;
    for (uint64_t g : queued) s += std::to_string(g) + " ";
    return s;
  }
};

UdpH264AssembledFrame frame(uint64_t gen, bool key) {
  UdpH264AssembledFrame f{};
  f.header.streamGeneration = gen;
  f.header.flags = key ? 1u : 0u;
  f.payload.assign(16, key ? 0x65 : 0x41);
  return f;
}

// The sink's order (OnEncodedH264Frame): pump the held frame first, then Admit at the gate, then the slot (Offer).
void deliver(SelectionAckGate& gate, DecoderPendingSlot& slot, Codec& codec, uint64_t gen, bool key, bool* keyReq) {
  slot.Pump(gate, codec);
  if (!gate.Admit(gen, key)) return;
  slot.Offer(frame(gen, key), gate, codec, keyReq);
}
}  // namespace

int main() {
  std::printf("decoder_pending_slot_test (apk-0232 r2)\n\n");
  {
    // Codex N2's order: local A unbound; IDR G held (no input buffer); G's retry fails; IDR H arrives; buffers free.
    SelectionAckGate gate;
    gate.Prepare(5);
    gate.BeginLocal(5);
    DecoderPendingSlot slot;
    Codec codec;
    codec.gate = &gate;
    bool keyReq = false;
    // attempts: G new (fails), G retry (fails), G retry at H's arrival (fails) -- then a buffer is free for H
    codec.failNext = 3;
    deliver(gate, slot, codec, 100, true, &keyReq);  // G: admitted (candidate G), no buffer -> held
    check("G (an IDR of the unbound local selection) is held: no input buffer", slot.has() && codec.queued.empty());
    slot.Pump(gate, codec);  // G's retry: still no buffer
    check("...its retry fails too", slot.has() && codec.queued.empty());
    deliver(gate, slot, codec, 200, true, &keyReq);  // the pump first (G fails again), then H: admitted (candidate H)
    slot.Pump(gate, codec);
    slot.Pump(gate, codec);
    check("G never reaches the codec -- only H does", codec.queued.size() == 1 && codec.queued[0] == 200, codec.list());
    check("...and the selection is bound to H", gate.LocalBoundFor(5) == 200 && gate.expected() == 200);
    deliver(gate, slot, codec, 100, false, &keyReq);
    check("after the bind a delta of G is refused at the gate", codec.queued.size() == 1);
  }
  {
    // A held frame of the PREVIOUS generation when the new selection's answer arrives: dropped at its retry.
    SelectionAckGate gate;
    DecoderPendingSlot slot;
    Codec codec;
    codec.gate = &gate;
    bool keyReq = false;
    codec.busy = true;
    deliver(gate, slot, codec, 300, true, &keyReq);  // no selection yet: admitted, held
    gate.Prepare(6);                                 // a new pick: answer awaited
    codec.busy = false;
    slot.Pump(gate, codec);
    check("a frame held from before a pick is not queued while its answer is awaited", codec.queued.empty() && !slot.has(),
          codec.list());
    gate.OnAck(true, 400, 6);
    deliver(gate, slot, codec, 400, true, &keyReq);
    check("...the answered generation's IDR goes in", codec.queued.size() == 1 && codec.queued[0] == 400, codec.list());
    check("the drops at the moment of queueing are counted", slot.gateDrops() >= 1, std::to_string(slot.gateDrops()));
  }
  {
    // A newer frame never overtakes a held one: a delta while an IDR is held is dropped (an IDR asked for).
    SelectionAckGate gate;
    DecoderPendingSlot slot;
    Codec codec;
    codec.gate = &gate;
    bool keyReq = false;
    codec.busy = true;
    deliver(gate, slot, codec, 500, true, &keyReq);
    codec.busy = false;
    codec.failNext = 1;                              // the held IDR's pump finds no buffer; the delta right after would
    deliver(gate, slot, codec, 500, false, &keyReq); // ...it must not go in ahead of the IDR
    check("a delta never overtakes the held IDR (it is dropped, an IDR asked for)", codec.queued.empty() && keyReq,
          codec.list());
    slot.Pump(gate, codec);
    check("...the held IDR goes in at the next pump", codec.queued.size() == 1 && codec.queued[0] == 500);
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

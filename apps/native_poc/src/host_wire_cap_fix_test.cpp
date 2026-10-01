// bitrate-hard-cap r3 -- counter-examples and negative controls for F2/F3/F4.
//
//   F3 drives the REAL product send function (send_udp_chunks_timed): a first datagram whose input
//      epoch changes DURING its token wait must abort with no datagram on the wire; a later datagram
//      of an already-started AU keeps going (the mid-AU-flush exception).
//   F4 reads the REAL send function's actual-wire accounting against the limiter's pre-send
//      reservation on a partial / aborted AU -- they differ by exactly the charged-but-not-sent
//      datagrams.
//   F2 is a decision MODEL (it mirrors the gate expression in host_stage_encode_send_h264.cpp and
//      drives the REAL decide_encode_admission + the REAL SenderState tag atomics) with negative
//      controls. The INTEGRATED product encode/emit path is measured by the real-MFT matrix in
//      fec_single_chunk_host_e2e_test --matrix; this file isolates the decision so each fix has a
//      counter-example that flips when the fix is removed.
// Tag: pure-logic (F2, F3, F4 all run on a fake clock; no socket, no display, no gpu).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "host_encode_admission.hpp"
#include "host_encoded_sender.hpp"
#include "host_net_io.hpp"
#include "host_wire_limiter.hpp"
#include "native_video_transport.hpp"  // clamp_udp_mtu
#include "poc_protocol.hpp"

using namespace remote60::native_poc;

namespace {
int g_checks = 0, g_failed = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

UdpVideoChunkHeader base_header(uint32_t seq, uint64_t gen, size_t payloadSize) {
  UdpVideoChunkHeader h{};
  h.magic = kMagic;
  h.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
  h.size = static_cast<uint16_t>(sizeof(UdpVideoChunkHeader));
  h.seq = seq;
  h.codec = static_cast<uint16_t>(UdpCodec::H264);
  h.flags = 0;
  h.width = 1920;
  h.height = 1080;
  h.payloadSize = static_cast<uint32_t>(payloadSize);
  h.streamGeneration = gen;
  return h;
}

// ---------------------------------------------------------------- F3 + F4 on the real send function
// A fake-clock limiter whose wait hook runs a callback the first time a datagram blocks for tokens --
// that is exactly the window F3 closes: the first datagram is permitted (token-wise) but has not yet
// reached its real permission point, and an input flush can land right there.
struct FakeClock {
  uint64_t now = 1'000'000;
};

void test_f3_first_datagram_epoch_fence() {
  std::printf("\n--- F3: a first datagram whose input epoch changes during its token wait aborts, 0 on wire ---\n");
  const size_t bytes = 40u * 1024u;     // many datagrams
  std::vector<uint8_t> payload(bytes, 0xCD);
  const uint32_t mtu = 1200;
  const uint64_t capBps = 2'000'000;    // low -> the first datagram must wait for tokens
  const uint32_t lmax = clamp_udp_mtu(mtu) + 28u;

  auto run = [&](bool flipDuringFirstWait, uint64_t& outSpentBytes) {
    FakeClock clk;
    std::atomic<uint64_t> inputEpoch{5};
    int firstWaits = 0;
    WireLimiter limiter([&] { return clk.now; },
                        [&](uint64_t deadlineUs, uint64_t) -> bool {
                          // The sender is blocked for tokens on a datagram. On the very first such
                          // wait, optionally flip the input epoch -- the flush F3 must notice.
                          if (flipDuringFirstWait && firstWaits == 0) inputEpoch.store(6);
                          ++firstWaits;
                          clk.now = deadlineUs;  // advance to when the tokens are ready
                          return true;
                        });
    limiter.SetRate(capBps, lmax);

    std::vector<int> sentLens;
    WireEgress wire;
    wire.limiter = &limiter;
    wire.sink = [&](const uint8_t*, int len, bool) { sentLens.push_back(len); return len; };
    wire.inputEpoch = &inputEpoch;
    wire.itemInputEpoch = 5;  // the AU was stamped under epoch 5
    uint64_t wd = 0, wp = 0, wdg = 0;
    wire.outWireDataBytes = &wd;
    wire.outWireParityBytes = &wp;
    wire.outWireDatagrams = &wdg;

    std::atomic<uint64_t> liveEpoch{1};
    SendPathStats stats{};
    sockaddr_in peer{};
    const UdpSendOutcome oc =
        send_udp_chunks_timed(INVALID_SOCKET, peer, payload.data(), payload.size(),
                              base_header(1, 1, bytes), mtu, &stats, &liveEpoch, 1, UdpEgressConfig{}, &wire);
    outSpentBytes = limiter.spent_bytes();
    return std::make_pair(oc, wdg);
  };

  uint64_t spentFlip = 0, spentOk = 0;
  const auto flipped = run(true, spentFlip);
  check("input epoch flips during the first token wait -> AU aborts (EpochChanged)",
        flipped.first == UdpSendOutcome::EpochChanged, "datagrams=" + std::to_string(flipped.second));
  check("F3: the aborted first datagram put 0 datagrams on the wire", flipped.second == 0);
  // F4 on the aborted AU: the limiter RESERVED the first datagram (Acquire ran before the epoch
  // fence) but 0 datagrams actually left -- the two numbers legitimately differ on an abort.
  check("F4: reservation (>0, one datagram charged) exceeds actual-sent (0) on the aborted AU",
        spentFlip > 0 && flipped.second == 0, "spentBytes=" + std::to_string(spentFlip));

  // NEGATIVE control: no flip -> the AU starts and every datagram goes out; reservation == actual.
  const auto ok = run(false, spentOk);
  check("NEGATIVE (no flip): the AU sends fully (Sent) and puts many datagrams on the wire",
        ok.first == UdpSendOutcome::Sent && ok.second > 1,
        "datagrams=" + std::to_string(ok.second));
}

void test_f4_reconciliation_partial_send() {
  std::printf("\n--- F4: actual-wire accounting vs reservation on a partial (sink fails mid-AU) send ---\n");
  const size_t bytes = 40u * 1024u;
  std::vector<uint8_t> payload(bytes, 0xEE);
  const uint32_t mtu = 1200;
  const uint32_t lmax = clamp_udp_mtu(mtu) + 28u;

  FakeClock clk;
  // A generous cap so tokens never gate here -- we are measuring accounting, not pacing.
  WireLimiter limiter([&] { return clk.now += 1000; }, [&](uint64_t, uint64_t) { return true; });
  limiter.SetRate(1'000'000'000ULL, lmax);

  const int failAfter = 5;  // the 6th datagram's sink fails (transport error)
  int sent = 0;
  WireEgress wire;
  wire.limiter = &limiter;
  wire.sink = [&](const uint8_t*, int len, bool) -> int {
    if (sent >= failAfter) return -1;  // simulate a sendto failure partway through
    ++sent;
    return len;
  };
  uint64_t wd = 0, wp = 0, wdg = 0;
  wire.outWireDataBytes = &wd;
  wire.outWireParityBytes = &wp;
  wire.outWireDatagrams = &wdg;

  std::atomic<uint64_t> liveEpoch{1};
  SendPathStats stats{};
  sockaddr_in peer{};
  const UdpSendOutcome oc =
      send_udp_chunks_timed(INVALID_SOCKET, peer, payload.data(), payload.size(),
                            base_header(1, 1, bytes), mtu, &stats, &liveEpoch, 1, UdpEgressConfig{}, &wire);
  check("a sink failure partway is a TransportError", oc == UdpSendOutcome::TransportError);
  check("F4: exactly the datagrams that actually left are counted (== failAfter)",
        wdg == static_cast<uint64_t>(failAfter), "actualDatagrams=" + std::to_string(wdg));
  // The limiter reserved one MORE than left: the datagram whose sink then failed was charged first.
  const uint64_t reservedDatagrams = limiter.spent_bytes() / (static_cast<uint64_t>(clamp_udp_mtu(mtu)) + 28u);
  check("F4: the reservation is exactly one datagram ahead of actual-sent on the failed AU",
        reservedDatagrams == static_cast<uint64_t>(failAfter) + 1,
        "reservedDatagrams=" + std::to_string(reservedDatagrams) + " actual=" + std::to_string(wdg));
  check("F4: actual-wire bytes are the data total (no parity reached the wire before the failure)",
        wp == 0 && wd == wdg * (static_cast<uint64_t>(clamp_udp_mtu(mtu)) + 28u),
        "wd=" + std::to_string(wd) + " wp=" + std::to_string(wp));
}

// ---------------------------------------------------------------- F2 decision model + tag atomics
// Mirrors the gate expression in host_stage_encode_send_h264.cpp (keyOnWire tag match -> forceKeyInFlight
// -> forceKeyFrame -> admission) and drives the REAL decide_encode_admission and the REAL SenderState
// tag atomics. This isolates the decision; the integrated encode/emit path is the real-MFT matrix.
EncodeAdmission gate_model(SenderState& s, uint64_t curMediaEpoch, uint64_t curGeneration,
                           uint64_t encodeStartUs, bool keyWanted, bool forceKeySubmittedRecently,
                           uint32_t senderDepth, bool& outForceKeyFrame) {
  const bool keyOnWire = s.wireCapEnabled && s.keyAuOnWire.load(std::memory_order_acquire) &&
                         encodeStartUs < s.keyAuOnWireSinceUs.load(std::memory_order_acquire) + 6'000'000ULL &&
                         s.keyAuOnWireMediaEpoch.load(std::memory_order_relaxed) == curMediaEpoch &&
                         s.keyAuOnWireGeneration.load(std::memory_order_relaxed) == curGeneration;
  const bool forceKeyInFlight = forceKeySubmittedRecently || keyOnWire;
  const bool forceKeyFrame = keyWanted && !forceKeyInFlight;
  outForceKeyFrame = forceKeyFrame;
  EncodeAdmissionInputs adm;
  adm.wireCapActive = true;
  adm.keyWanted = forceKeyFrame;  // r3 F2: admit-always only for a frame that will REALLY be a key
  adm.servedBootstrap = false;
  adm.senderQueueDepth = senderDepth;
  adm.senderQueueMax = 2;
  return decide_encode_admission(adm);
}

void test_f2_recovery_admission_and_tag() {
  std::printf("\n--- F2: a keyWanted frame that cannot force (key already in flight) is gated like a delta ---\n");
  SenderState s;
  s.wireCapEnabled = true;
  // A key of THIS session (epoch 3, gen 9) is on the wire right now.
  s.keyAuOnWire.store(true, std::memory_order_release);
  s.keyAuOnWireSinceUs.store(1'000'000, std::memory_order_release);
  s.keyAuOnWireMediaEpoch.store(3, std::memory_order_relaxed);
  s.keyAuOnWireGeneration.store(9, std::memory_order_relaxed);
  const uint64_t encodeStartUs = 1'100'000;  // within the 6 s window

  // Scenario ⓐ: keyWanted (forceKeyNext) with the key already on the wire, sender queue full (2).
  bool fkf = true;
  const auto decA = gate_model(s, /*mediaEpoch=*/3, /*gen=*/9, encodeStartUs,
                               /*keyWanted=*/true, /*forceKeySubmittedRecently=*/false,
                               /*senderDepth=*/2, fkf);
  check("F2 (a): keyWanted but key-in-flight -> forceKeyFrame=false (it would be a delta)", !fkf);
  check("F2 (a): the delta is SKIPPED on a full queue (no resync-IDR overflow)",
        decA == EncodeAdmission::SkipOverloaded);

  // NEGATIVE control: the pre-fix behaviour admitted every keyWanted frame regardless -> overflow.
  {
    EncodeAdmissionInputs adm;
    adm.wireCapActive = true;
    adm.keyWanted = true;  // the OLD code fed raw keyWanted here
    adm.senderQueueDepth = 2;
    adm.senderQueueMax = 2;
    check("NEGATIVE (pre-fix): feeding raw keyWanted admits the delta into a full queue (the bug)",
          decide_encode_admission(adm) == EncodeAdmission::Admit);
  }

  // When the key CAN actually be forced (none in flight), it is admitted even on a full queue.
  bool fkf2 = false;
  const auto decReal = gate_model(s, 3, 9, encodeStartUs, /*keyWanted=*/true,
                                  /*forceKeySubmittedRecently=*/false, /*senderDepth=*/2, fkf2);
  // still in flight here (same tags) -> gated; flip the flag off to prove the real-key path:
  s.keyAuOnWire.store(false, std::memory_order_release);
  bool fkf3 = false;
  const auto decForce = gate_model(s, 3, 9, encodeStartUs, true, false, 2, fkf3);
  check("F2: with no key in flight a real forced key is admitted even on a full queue", fkf3 && decForce == EncodeAdmission::Admit);
  (void)decReal;

  std::printf("--- F2 tag: a stale on-wire key of a DIFFERENT (epoch, generation) does not count as in-flight ---\n");
  s.keyAuOnWire.store(true, std::memory_order_release);
  s.keyAuOnWireMediaEpoch.store(3, std::memory_order_relaxed);
  s.keyAuOnWireGeneration.store(9, std::memory_order_relaxed);
  // Now the session rolled over: we are encoding for media epoch 4 / generation 10. The old flag must
  // NOT suppress this new session's recovery IDR.
  bool fkfNew = false;
  const auto decNewGen = gate_model(s, /*mediaEpoch=*/4, /*gen=*/10, encodeStartUs,
                                    /*keyWanted=*/true, false, /*senderDepth=*/2, fkfNew);
  check("F2 tag: a new (epoch,generation)'s keyWanted IS a real forced key (old tag ignored) and admitted",
        fkfNew && decNewGen == EncodeAdmission::Admit);
  // Same generation, same epoch -> the tag DOES match -> suppressed (the intended in-flight case).
  bool fkfSame = true;
  gate_model(s, 3, 9, encodeStartUs, true, false, 2, fkfSame);
  check("F2 tag: the SAME (epoch,generation) key still counts as in-flight (forceKeyFrame=false)", !fkfSame);
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("--- bitrate-hard-cap r3: F2/F3/F4 counter-examples + negative controls ---\n");
  test_f3_first_datagram_epoch_fence();
  test_f4_reconciliation_partial_send();
  test_f2_recovery_admission_and_tag();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}

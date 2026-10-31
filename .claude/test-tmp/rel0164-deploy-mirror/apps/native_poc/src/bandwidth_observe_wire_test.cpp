// C0 stage 1 on the wire: the message's layout and size, what the host refuses, the negotiation
// rule, and the viewer's scheduler -- which sends nothing unless it is handed a snapshot, which the
// viewer does only for a host that acknowledged the bit (the old-host combination).

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bandwidth_observe_wire.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ",
              detail.c_str());
}

/** Records what a writer puts on the control link. */
class RecordingLink : public ControlLink {
 public:
  std::vector<uint8_t> written;
  bool Read(void*, size_t) override { return false; }
  bool Write(const void* data, size_t len) override {
    const auto* p = static_cast<const uint8_t*>(data);
    written.insert(written.end(), p, p + len);
    return true;
  }
  bool EndMessage() override { return true; }
  bool Alive() const override { return true; }
};

BweReport sample_report() {
  BweReport r;
  r.streamGeneration = 0x1122334455667788ull;
  r.bweBps = 12345678;
  r.usage = BweUsage::Overuse;
  r.rateState = BweRateState::Decrease;
  r.appLimited = true;
  r.staticHold = false;
  r.goodputUniqueBps = 7000000;
  r.wireLoadBps = 7400000;
  r.delayGradientUs = -4321;
  r.thresholdUs = 6000;
  r.lossPm = 12;
  r.delaySamples = 29;
  return r;
}

}  // namespace

int main() {
  // ------------------------------------------------------------------------ layout
  check("the message is 60 bytes", sizeof(ControlClientBandwidthMessage) == 60,
        std::to_string(sizeof(ControlClientBandwidthMessage)));
  check("kind 54 and bit 0x40, next to what they follow",
        static_cast<uint16_t>(MessageType::ControlClientBandwidth) == 54 &&
            kUdpFeatureBandwidthObserve == 0x40u &&
            (kUdpFeatureBandwidthObserve & (kUdpFeatureEncryptedMedia | kUdpFeatureVideoFec |
                                            kUdpFeatureDirectoryAuth | kUdpFeatureVideoFecInterleaved |
                                            kUdpFeatureVideoNack | kUdpFeatureControlResume)) == 0);
  check("the field offsets are the documented ones",
        offsetof(ControlClientBandwidthMessage, seq) == 8 &&
            offsetof(ControlClientBandwidthMessage, flags) == 12 &&
            offsetof(ControlClientBandwidthMessage, usage) == 16 &&
            offsetof(ControlClientBandwidthMessage, lossPm) == 18 &&
            offsetof(ControlClientBandwidthMessage, bweBps) == 20 &&
            offsetof(ControlClientBandwidthMessage, delayGradientUs) == 32 &&
            offsetof(ControlClientBandwidthMessage, delaySamples) == 40 &&
            offsetof(ControlClientBandwidthMessage, streamGeneration) == 44 &&
            offsetof(ControlClientBandwidthMessage, clientSendQpcUs) == 52);

  // ------------------------------------------------------------------------ build, then judge
  const ControlClientBandwidthMessage m = make_client_bandwidth_message(sample_report(), 7, 999);
  check("a built message carries the report exactly",
        m.header.type == 54 && m.header.size == 60 && m.seq == 7 && m.bweBps == 12345678 &&
            m.usage == 2 && m.rateState == 2 && m.flags == kBandwidthFlagAppLimited &&
            m.delayGradientUs == -4321 && m.lossPm == 12 && m.delaySamples == 29 &&
            m.streamGeneration == 0x1122334455667788ull && m.clientSendQpcUs == 999);
  check("...and the host accepts it", client_bandwidth_message_valid(m));
  {
    auto bad = m;
    bad.usage = 3;
    check("the host refuses a usage outside 0..2", !client_bandwidth_message_valid(bad));
    bad = m;
    bad.rateState = 9;
    check("...a rate state outside 0..2", !client_bandwidth_message_valid(bad));
    bad = m;
    bad.lossPm = 1001;
    check("...a loss above 1000 permille", !client_bandwidth_message_valid(bad));
    bad = m;
    bad.flags = 0x4;
    check("...an undefined flag", !client_bandwidth_message_valid(bad));
    bad = m;
    bad.reserved = 1;
    check("...a non-zero reserved field", !client_bandwidth_message_valid(bad));
    bad = m;
    bad.header.size = 59;
    check("...any other size", !client_bandwidth_message_valid(bad));
    bad = m;
    bad.header.type = static_cast<uint16_t>(MessageType::ControlClientMetrics);
    check("...another kind", !client_bandwidth_message_valid(bad));
  }
  {
    BweReport huge = sample_report();
    huge.lossPm = 5000;
    huge.delaySamples = 100000;
    const auto clamped = make_client_bandwidth_message(huge, 1, 1);
    check("the builder clamps loss to 1000 and samples to 65535, so it never builds a refusal",
          clamped.lossPm == 1000 && clamped.delaySamples == 0xFFFF &&
              client_bandwidth_message_valid(clamped));
  }

  // ------------------------------------------------------------------------ negotiation
  check("an old host (no bit in the ack) is never sent one",
        !bandwidth_observe_negotiated(true, kUdpFeatureVideoFec | kUdpFeatureControlResume));
  check("a viewer that did not ask never sends one", !bandwidth_observe_negotiated(false, 0x7Fu));
  check("both sides agreeing is the only yes", bandwidth_observe_negotiated(true, kUdpFeatureBandwidthObserve));
  check("the host reads the request from the Hello bits",
        host_bandwidth_observe_requested(kUdpFeatureBandwidthObserve) &&
            !host_bandwidth_observe_requested(kUdpFeatureControlResume));

  // ------------------------------------------------------------------------ the scheduler
  ClientControlScheduler scheduler;
  WindowPanelStateModel windowPanel;
  StreamStateControl streamState;
  CaptureModeRequestState captureMode;
  KeyframeRequestState keyframe(120000, 300000, 3);
  RuntimeTuneState runtimeTune(300000, 30000000, 250000, 1, 240);
  ClientInputQueue inputQueue;
  ClientControlMetricsSnapshot metrics{};
  scheduler.Reset(1000, 1000);
  ControlOutboundAction action{};
  // Drain the session's first ping so only bandwidth could come out next.
  (void)scheduler.NextAction(1000, metrics, &windowPanel, &streamState, &captureMode, &keyframe,
                             &runtimeTune, &inputQueue, &action);
  scheduler.OnPingCompleted(1000);

  ClientBandwidthSnapshot snapshot;
  snapshot.message = m;
  snapshot.updatedQpcUs = 5000;
  // The old-host combination: the viewer passes no snapshot, whatever the receive thread has.
  bool sentWithout = false;
  for (uint64_t t = 1100; t < 1400; t += 50) {
    ControlOutboundAction a{};
    if (scheduler.NextAction(t, metrics, &windowPanel, &streamState, &captureMode, &keyframe,
                             &runtimeTune, &inputQueue, &a, nullptr, nullptr) &&
        a.kind == ControlOutboundActionKind::ClientBandwidth) {
      sentWithout = true;
    }
  }
  check("without a snapshot the scheduler never sends one (old host / not negotiated)", !sentWithout);

  ControlOutboundAction a1{};
  const bool first = scheduler.NextAction(1500, metrics, &windowPanel, &streamState, &captureMode,
                                          &keyframe, &runtimeTune, &inputQueue, &a1, nullptr, &snapshot);
  check("with one, the new report goes out", first && a1.kind == ControlOutboundActionKind::ClientBandwidth);
  check("...stamped with the scheduler's seq and the send time",
        a1.clientBandwidth.seq == 1 && a1.clientBandwidth.clientSendQpcUs == 1500 &&
            a1.clientBandwidth.bweBps == m.bweBps);
  ControlOutboundAction a2{};
  const bool again = scheduler.NextAction(1550, metrics, &windowPanel, &streamState, &captureMode,
                                          &keyframe, &runtimeTune, &inputQueue, &a2, nullptr, &snapshot);
  check("...and once only: the same report is not sent twice",
        !(again && a2.kind == ControlOutboundActionKind::ClientBandwidth));
  snapshot.updatedQpcUs = 6000;
  ControlOutboundAction a3{};
  (void)scheduler.NextAction(1600, metrics, &windowPanel, &streamState, &captureMode, &keyframe,
                             &runtimeTune, &inputQueue, &a3, nullptr, &snapshot);
  check("the next report goes out with the next seq",
        a3.kind == ControlOutboundActionKind::ClientBandwidth && a3.clientBandwidth.seq == 2);
  check("no reply is expected for it (fire and forget, like metrics)",
        !a3.expectedResponseType.has_value());

  RecordingLink link;
  check("it is written as exactly 60 bytes",
        send_control_action(link, a3) && link.written.size() == 60,
        std::to_string(link.written.size()));
  ControlClientBandwidthMessage back{};
  if (link.written.size() == 60) std::memcpy(&back, link.written.data(), 60);
  check("...that the host accepts as written", client_bandwidth_message_valid(back) && back.seq == 2);

  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED",
              gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}

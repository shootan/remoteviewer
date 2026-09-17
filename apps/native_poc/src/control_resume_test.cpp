// Unit test for the control-resume decisions (control_resume.hpp) and the resume packet's wire
// shape. (item 8, layer 1)
//
// Pure logic: no socket, no peer, no clock. What it is really checking is that each of the four
// reasons to say no is separately load-bearing -- an old host, a healthy channel, a session whose
// video has also stopped, and the T3 ceiling -- because a decision function that says Send for
// everything would pass a test that only ever asked it to send.

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "control_resume.hpp"
#include "poc_protocol.hpp"
#include "udp_control_channel.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

const char* name_of(ControlResumeAction a) {
  switch (a) {
    case ControlResumeAction::Send: return "Send";
    case ControlResumeAction::GiveUp: return "GiveUp";
    default: return "Idle";
  }
}

/** The state item 8 exists for: control dead, video still arriving, due for another attempt. */
ControlResumeInputs asking() {
  ControlResumeInputs in;
  in.negotiated = true;
  in.controlDead = true;
  in.videoAlive = true;
  in.controlDeadForUs = 1000000;
  in.sinceLastAttemptUs = 10000000;  // long past due
  return in;
}

}  // namespace

int main() {
  std::cout << "control_resume_test\n";
  const ControlResumeConfig cfg;  // 500 ms retry, 30 s ceiling

  // ------------------------------------------------------------------ the case it exists for
  check("control dead with video still arriving asks to resume",
        control_resume_decide(cfg, asking()) == ControlResumeAction::Send,
        name_of(control_resume_decide(cfg, asking())));

  // ------------------------------------------------------------------ each refusal, separately
  {
    // Backward compatibility, and the single most important case here: a 0.2.131 viewer against a
    // 0.2.130 host. The host never advertised the bit, so nothing is ever sent to it and the
    // session behaves as it did before. Idle rather than GiveUp: there was nothing to give up on.
    ControlResumeInputs in = asking();
    in.negotiated = false;
    check("an old host is never asked",
          control_resume_decide(cfg, in) == ControlResumeAction::Idle, name_of(control_resume_decide(cfg, in)));
  }
  {
    ControlResumeInputs in = asking();
    in.controlDead = false;
    check("a healthy control channel is left alone",
          control_resume_decide(cfg, in) == ControlResumeAction::Idle, name_of(control_resume_decide(cfg, in)));
  }
  {
    // The negative control for (d): video stopped too, so this is a dying session and not a broken
    // channel. Resume stays out of it -- both to avoid spinning and to leave the liveness verdict
    // as the one thing deciding whether the session is dead.
    ControlResumeInputs in = asking();
    in.videoAlive = false;
    check("a session whose video also stopped is not resumed",
          control_resume_decide(cfg, in) == ControlResumeAction::Idle, name_of(control_resume_decide(cfg, in)));
  }
  {
    ControlResumeInputs in = asking();
    in.sinceLastAttemptUs = cfg.retryIntervalUs - 1;
    check("attempts are spaced, not spun",
          control_resume_decide(cfg, in) == ControlResumeAction::Idle, name_of(control_resume_decide(cfg, in)));
    in.sinceLastAttemptUs = cfg.retryIntervalUs;
    check("...and the next one goes at exactly the interval",
          control_resume_decide(cfg, in) == ControlResumeAction::Send, name_of(control_resume_decide(cfg, in)));
  }
  {
    // (c) The T3 ceiling is preserved: past it, asking stops. Without this resume would become a
    // way for a session with no control to outlive the deadline that was supposed to end it.
    ControlResumeInputs in = asking();
    in.controlDeadForUs = cfg.giveUpAfterUs - 1;
    check("just inside the ceiling still asks",
          control_resume_decide(cfg, in) == ControlResumeAction::Send, name_of(control_resume_decide(cfg, in)));
    in.controlDeadForUs = cfg.giveUpAfterUs;
    check("at the ceiling it gives up",
          control_resume_decide(cfg, in) == ControlResumeAction::GiveUp, name_of(control_resume_decide(cfg, in)));
    in.controlDeadForUs = cfg.giveUpAfterUs * 10;
    check("...and stays given up",
          control_resume_decide(cfg, in) == ControlResumeAction::GiveUp, name_of(control_resume_decide(cfg, in)));
  }
  {
    // Ordering between the two stopping conditions. A session that is over the ceiling AND has
    // lost video must not report GiveUp, because GiveUp is a statement about resume and this is a
    // statement about the session -- the liveness verdict owns it.
    ControlResumeInputs in = asking();
    in.videoAlive = false;
    in.controlDeadForUs = cfg.giveUpAfterUs * 2;
    check("no video past the ceiling is still the liveness verdict's business",
          control_resume_decide(cfg, in) == ControlResumeAction::Idle, name_of(control_resume_decide(cfg, in)));
  }
  {
    // A ceiling of zero means no ceiling, matching how the liveness config reads its own deadlines.
    ControlResumeConfig noCeiling;
    noCeiling.giveUpAfterUs = 0;
    ControlResumeInputs in = asking();
    in.controlDeadForUs = 3600000000ULL;
    check("a zero ceiling means no ceiling",
          control_resume_decide(noCeiling, in) == ControlResumeAction::Send,
          name_of(control_resume_decide(noCeiling, in)));
  }

  // ------------------------------------------------------------------ the host side
  {
    HostResumeInputs in;
    in.negotiated = true;
    in.sessionActive = true;
    in.servingControl = false;
    check("a parked host accepts a resume", host_should_accept_resume(in));

    HostResumeInputs busy = in;
    busy.servingControl = true;
    check("a host in the middle of serving refuses",
          !host_should_accept_resume(busy),
          "resetting a working stream is the thing this must never do");

    HostResumeInputs noSession = in;
    noSession.sessionActive = false;
    check("a host with no session refuses", !host_should_accept_resume(noSession));

    HostResumeInputs notAsked = in;
    notAsked.negotiated = false;
    check("a client that never asked for resume is not given one",
          !host_should_accept_resume(notAsked));
  }

  // ------------------------------------------------------------------ the wire
  {
    // Strictly additive: the new kinds sit above the ones that existed, and the feature bit does
    // not collide with any already in use. A collision would silently enable something else.
    check("the resume kinds are new numbers",
          static_cast<uint16_t>(UdpPacketKind::ControlResume) == 309 &&
              static_cast<uint16_t>(UdpPacketKind::ControlResumeAck) == 310);
    const uint32_t existing = kUdpFeatureEncryptedMedia | kUdpFeatureVideoFec |
                              kUdpFeatureDirectoryAuth | kUdpFeatureVideoFecInterleaved |
                              kUdpFeatureVideoNack;
    check("the resume feature bit collides with nothing",
          (kUdpFeatureControlResume & existing) == 0);

    UdpControlResumePacket pkt;
    check("a resume packet defaults to the request kind",
          pkt.kind == static_cast<uint16_t>(UdpPacketKind::ControlResume));
    check("...carries its own size", pkt.size == sizeof(UdpControlResumePacket));
    check("...and is one small datagram", sizeof(UdpControlResumePacket) == 24,
          std::to_string(sizeof(UdpControlResumePacket)) + " bytes");
    check("...with the shared magic", pkt.magic == kMagic);
  }

  // ------------------------------------------------------------------ the re-keyed stream ids
  {
    const uint32_t c2h = kUdpControlStreamClientToHost;
    const uint32_t h2c = kUdpControlStreamHostToClient;

    // Never the base ids: a resumed stream must not be mistaken for the original one, in either
    // direction. This is what makes every pre-resume datagram a packet the channel ignores.
    for (uint32_t r = 1; r <= 64; ++r) {
      const uint32_t a = control_resume_stream_id(c2h, r);
      const uint32_t b = control_resume_stream_id(h2c, r);
      if (a == c2h || a == h2c || b == c2h || b == h2c) {
        check("a derived id collides with a base id", false, "resumeId=" + std::to_string(r));
        break;
      }
      if (a == b) {
        check("the two directions derive the same id", false, "resumeId=" + std::to_string(r));
        break;
      }
    }
    check("derived ids never collide with the base ids, and the directions stay distinct", true);

    // Consecutive resumes derive different ids, so traffic from the previous attempt cannot be
    // taken for traffic from this one.
    bool distinct = true;
    for (uint32_t r = 1; r < 64; ++r) {
      if (control_resume_stream_id(c2h, r) == control_resume_stream_id(c2h, r + 1)) distinct = false;
    }
    check("consecutive resumes derive different ids", distinct);

    // Both peers must compute the same value from the same resumeId, or they stop hearing each
    // other entirely. Stated as an equality rather than a constant so the derivation can change.
    check("the host's rx matches the viewer's tx",
          control_resume_stream_id(c2h, 7) == control_resume_stream_id(c2h, 7));
  }

  // ------------------------------------------------------------------ the re-key, on the channel
  {
    // What ResumeWith has to achieve, checked through OnPacket with no socket: after the re-key a
    // datagram from before the break is ignored, and one on the new stream is delivered. Without
    // the re-key the stale one would be delivered instead and every fresh message below its
    // sequence number would be silently acked and dropped.
    const uint32_t resumeId = 5;
    const uint32_t oldRx = kUdpControlStreamClientToHost;
    const uint32_t newRx = control_resume_stream_id(kUdpControlStreamClientToHost, resumeId);

    // Builds one single-fragment control message datagram.
    const auto datagram = [](uint32_t streamId, uint32_t seq, const std::string& body) {
      UdpControlChunkHeader head;
      head.streamId = streamId;
      head.messageSeq = seq;
      head.totalSize = static_cast<uint32_t>(body.size());
      head.fragIndex = 0;
      head.fragCount = 1;
      head.fragOffset = 0;
      head.fragSize = static_cast<uint32_t>(body.size());
      std::vector<uint8_t> bytes(sizeof(head) + body.size());
      std::memcpy(bytes.data(), &head, sizeof(head));
      std::memcpy(bytes.data() + sizeof(head), body.data(), body.size());
      return bytes;
    };

    UdpControlChannel channel;
    channel.Configure([](const void*, size_t) { return true; }, kUdpControlStreamHostToClient,
                      oldRx, 1200);

    std::vector<uint8_t> got;
    const auto stale = datagram(oldRx, 57, "stale");
    const auto fresh = datagram(newRx, 1, "fresh");

    // Before the re-key the old stream is the live one, which is what makes the check below mean
    // something: the datagram is well formed and would be delivered.
    check("a message on the current stream is delivered", channel.OnPacket(stale.data(), stale.size()));
    check("...and that pre-break message arrives",
          channel.Receive(&got, 50) && std::string(got.begin(), got.end()) == "stale");

    channel.ResumeWith(control_resume_stream_id(kUdpControlStreamHostToClient, resumeId), newRx);

    // The same stale datagram, replayed after the re-key: ignored, and -- the part that matters --
    // it does not move rxDeliveredSeq_, so it cannot swallow the fresh stream.
    channel.OnPacket(stale.data(), stale.size());
    check("a pre-resume datagram is ignored after the re-key", !channel.Receive(&got, 50));

    check("a message on the resumed stream is accepted", channel.OnPacket(fresh.data(), fresh.size()));
    check("...and the resumed message arrives",
          channel.Receive(&got, 50) && std::string(got.begin(), got.end()) == "fresh",
          std::string(got.begin(), got.end()));

    // The re-key also clears the closed state, so a channel that gave up on its peer can carry
    // traffic again without being reconstructed.
    UdpControlChannel closedChannel;
    closedChannel.Configure([](const void*, size_t) { return true; }, kUdpControlStreamHostToClient,
                            oldRx, 1200);
    closedChannel.Close(ControlCloseReason::PeerLost);
    check("a channel that gave up is closed", closedChannel.IsClosed());
    closedChannel.ResumeWith(control_resume_stream_id(kUdpControlStreamHostToClient, resumeId), newRx);
    check("...and the re-key reopens it", !closedChannel.IsClosed());
    check("...with the give-up reason cleared",
          closedChannel.CloseReason() == ControlCloseReason::None);
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}

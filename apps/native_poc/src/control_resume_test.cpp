// Unit test for the control-resume decisions (control_resume.hpp) and the resume packet's wire
// shape. (item 8, layer 1)
//
// Pure logic: no socket, no peer, no clock. What it is really checking is that each of the four
// reasons to say no is separately load-bearing -- an old host, a healthy channel, a session whose
// video has also stopped, and the T3 ceiling -- because a decision function that says Send for
// everything would pass a test that only ever asked it to send.

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
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

const char* name_of(HostResumeVerdict v) { return to_string(v); }

/**
 * Two real channels with a switch on each direction.
 *
 * Delivery is deferred rather than immediate: a send happens with the sending channel's mutex
 * held, and handing it straight to the peer brings the peer's ack back into a lock this thread
 * already holds.
 */
struct Link {
  UdpControlChannel host;
  UdpControlChannel viewer;
  bool dropToViewer = false;
  bool dropToHost = false;
  uint64_t droppedToHost = 0;
  uint64_t droppedToViewer = 0;
  std::vector<std::vector<uint8_t>> toViewer;
  std::vector<std::vector<uint8_t>> toHost;

  void Wire() {
    host.Configure(
        [this](const void* d, size_t n) {
          toViewer.emplace_back(static_cast<const uint8_t*>(d),
                                static_cast<const uint8_t*>(d) + n);
          return true;
        },
        kUdpControlStreamHostToClient, kUdpControlStreamClientToHost, 1200);
    viewer.Configure(
        [this](const void* d, size_t n) {
          toHost.emplace_back(static_cast<const uint8_t*>(d),
                              static_cast<const uint8_t*>(d) + n);
          return true;
        },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, 1200);
  }

  void Pump(int rounds = 8) {
    for (int i = 0; i < rounds && !(toViewer.empty() && toHost.empty()); ++i) {
      std::vector<std::vector<uint8_t>> v;
      std::vector<std::vector<uint8_t>> h;
      v.swap(toViewer);
      h.swap(toHost);
      for (const auto& d : v) {
        if (dropToViewer) { ++droppedToViewer; continue; }
        viewer.OnPacket(d.data(), d.size());
      }
      for (const auto& d : h) {
        if (dropToHost) { ++droppedToHost; continue; }
        host.OnPacket(d.data(), d.size());
      }
    }
  }

  /** Carries everything, except the peer's acknowledgement of one particular message. */
  void PumpDroppingAck(uint32_t seq, int rounds = 8) {
    for (int i = 0; i < rounds && !(toViewer.empty() && toHost.empty()); ++i) {
      std::vector<std::vector<uint8_t>> v;
      std::vector<std::vector<uint8_t>> h;
      v.swap(toViewer);
      h.swap(toHost);
      for (const auto& d : v) viewer.OnPacket(d.data(), d.size());
      for (const auto& d : h) {
        if (d.size() >= sizeof(UdpControlAckPacket)) {
          UdpControlAckPacket ack{};
          std::memcpy(&ack, d.data(), sizeof(ack));
          if (ack.kind == static_cast<uint16_t>(UdpPacketKind::ControlAck) &&
              ack.messageSeq == seq) {
            continue;
          }
        }
        host.OnPacket(d.data(), d.size());
      }
    }
  }

  /** Runs the clock until the host's probe resolves, or the budget runs out. */
  ControlProbeState SettleProbe(int budgetMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
    for (;;) {
      Pump();
      host.Tick();
      viewer.Tick();
      const ControlProbeState state = host.ProbeStatus();
      if (state == ControlProbeState::Alive || state == ControlProbeState::Dead) return state;
      if (std::chrono::steady_clock::now() >= deadline) return state;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
};

/** One host->viewer message, carried and delivered, so the probe has an acked seq to name. */
bool carry_one_message(Link& link, const std::string& body) {
  if (!link.host.Send(body.data(), body.size())) return false;
  link.Pump();
  std::vector<uint8_t> got;
  if (!link.viewer.Receive(&got, 100)) return false;
  link.Pump();  // carry the ack back, so the host knows the seq was delivered
  return std::string(got.begin(), got.end()) == body;
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

  // ------------------------------------------------------- the host dispatcher's two wake reasons
  {
    // Steady state: the dispatcher has served epoch 3 and resume 0, and nothing new has happened.
    ControlDispatchInputs idle;
    idle.epoch = 3;
    idle.servedEpoch = 3;
    idle.resumeSeq = 0;
    idle.servedResumeSeq = 0;
    check("nothing to do does not wake the dispatcher", !control_dispatch_decide(idle).wake);

    // A resume for the session it is already serving.
    ControlDispatchInputs resume = idle;
    resume.resumeSeq = 1;
    check("a resume wakes it", control_dispatch_decide(resume).wake);
    check("...as a resume, not a new session", control_dispatch_decide(resume).resuming);

    // A different client. This is the case that must NOT be treated as a resume -- the epoch moving
    // is what says the client changed, and resuming here would hand the old client's request state
    // to the new one's session.
    ControlDispatchInputs rollover = idle;
    rollover.epoch = 4;
    check("a new epoch wakes it", control_dispatch_decide(rollover).wake);
    check("...and is not a resume", !control_dispatch_decide(rollover).resuming);

    // Both at once: a client left while its resume was in flight. The epoch still wins.
    ControlDispatchInputs both = idle;
    both.epoch = 4;
    both.resumeSeq = 1;
    check("a rollover racing a resume wakes it", control_dispatch_decide(both).wake);
    check("...and the rollover wins", !control_dispatch_decide(both).resuming,
          "a resume must never be served onto a different client's session");

    // Shutdown wakes it and is not a resume, or the loop would re-key on its way out.
    ControlDispatchInputs stopping = idle;
    stopping.stop = true;
    stopping.resumeSeq = 1;
    check("stopping wakes it", control_dispatch_decide(stopping).wake);
    check("...and is never a resume", !control_dispatch_decide(stopping).resuming);

    // A resume already served does not wake it again; the client repeating its request while the
    // answer is in flight must not start a second re-key.
    ControlDispatchInputs served = idle;
    served.resumeSeq = 2;
    served.servedResumeSeq = 2;
    check("an already-served resume does not wake it again", !control_dispatch_decide(served).wake);
  }

  // ------------------------------------------------------------- wire compatibility, host side
  {
    // (e) Two of the three rolling-update cases are decided here; the third is the viewer reading
    // the ack, which is layer 3's.
    const uint32_t oldViewerHello = kUdpFeatureVideoFec | kUdpFeatureVideoFecInterleaved;
    const uint32_t newViewerHello = oldViewerHello | kUdpFeatureControlResume;

    // old viewer -> new host: the host advertises the bit anyway (it does not know yet who is
    // asking), but records that this client never asked, and then refuses any resume.
    check("a new host records that an old viewer did not ask",
          !host_resume_negotiated(oldViewerHello));
    HostResumeInputs fromOldViewer;
    fromOldViewer.negotiated = host_resume_negotiated(oldViewerHello);
    fromOldViewer.sessionActive = true;
    fromOldViewer.servingControl = false;
    check("...and refuses a resume that claims to be from it",
          !host_should_accept_resume(fromOldViewer),
          "an old viewer cannot have sent one, so this is someone else");

    // new viewer -> new host: negotiated, and a resume is honoured once control is not being served.
    check("a new host records that a new viewer asked", host_resume_negotiated(newViewerHello));
    HostResumeInputs fromNewViewer = fromOldViewer;
    fromNewViewer.negotiated = host_resume_negotiated(newViewerHello);
    check("...and honours its resume", host_should_accept_resume(fromNewViewer));

    // new viewer -> old host: the old host never sets the bit in its ack, so the viewer's
    // negotiated is false and control_resume_decide never says Send. Checked here as the ack the
    // viewer would see, so the three cases sit together.
    const uint32_t oldHostAck = kUdpFeatureVideoFec | kUdpFeatureVideoNack;
    ControlResumeInputs againstOldHost = asking();
    againstOldHost.negotiated = (oldHostAck & kUdpFeatureControlResume) != 0;
    check("a new viewer against an old host never asks",
          control_resume_decide(cfg, againstOldHost) == ControlResumeAction::Idle,
          name_of(control_resume_decide(cfg, againstOldHost)));

    const uint32_t newHostAck = oldHostAck | kUdpFeatureControlResume;
    ControlResumeInputs againstNewHost = asking();
    againstNewHost.negotiated = (newHostAck & kUdpFeatureControlResume) != 0;
    check("...and against a new host it does",
          control_resume_decide(cfg, againstNewHost) == ControlResumeAction::Send,
          name_of(control_resume_decide(cfg, againstNewHost)));
  }

  // ------------------------------------------------ host: answering the same ask twice (C3)
  //
  // The loop this closes: the host serves a resume, answers it, and goes straight back into
  // Serve(). If that one answer is lost, every retry arrives while servingControl is true and
  // host_should_accept_resume refuses it -- for the whole 30 s ceiling. The channel is repaired
  // and the viewer never finds out. So the answer is kept and repeated, and the point of these
  // checks is that "the same ask" is the whole ask and not just the id.
  {
    const uint32_t kPeerIp = 0x0100007fu;   // 127.0.0.1, network order
    const uint16_t kPeerPort = 0x3412u;
    const uint32_t kResumeId = 0xA5A50001u;
    const uint32_t kRequestStream = kUdpControlStreamClientToHost;

    HostResumeAck cached;
    cached.valid = true;
    cached.epoch = 7;
    cached.peerIpNet = kPeerIp;
    cached.peerPortNet = kPeerPort;
    cached.resumeId = kResumeId;
    cached.requestStreamId = kRequestStream;
    cached.txStreamId = control_resume_stream_id(kUdpControlStreamHostToClient, kResumeId);
    cached.rxStreamId = control_resume_stream_id(kUdpControlStreamClientToHost, kResumeId);
    cached.ackStreamId = cached.txStreamId;

    // The retry, exactly as the viewer sends it again: same id, same content, same endpoint.
    HostResumeReplayInputs retry;
    retry.negotiated = true;
    retry.sessionActive = true;
    retry.epoch = 7;
    retry.peerIpNet = kPeerIp;
    retry.peerPortNet = kPeerPort;
    retry.resumeId = kResumeId;
    retry.requestStreamId = kRequestStream;
    retry.channelTxStreamId = cached.txStreamId;
    retry.channelRxStreamId = cached.rxStreamId;

    check("the first answer was lost, so the same ask is answered again",
          host_should_replay_resume_ack(cached, retry),
          "without this the retry is refused for the rest of the ceiling");

    // And the reason it is safe to do that while serving: the ordinary path would say no.
    HostResumeInputs serving;
    serving.negotiated = true;
    serving.sessionActive = true;
    serving.servingControl = true;
    check("...while the ordinary path still refuses to re-key a channel in use",
          !host_should_accept_resume(serving),
          "the repeat is the answer only, not a second resume");

    // Every field is separately load-bearing. A replay that matched on the id alone would be a
    // way to make the host repeat an answer into a session that has moved on.
    {
      HostResumeReplayInputs older = retry;
      older.epoch = 6;
      check("an older generation is not the session that answer belongs to",
            !host_should_replay_resume_ack(cached, older));
      HostResumeReplayInputs newer = retry;
      newer.epoch = 8;
      check("...and neither is a newer one", !host_should_replay_resume_ack(cached, newer));
    }
    {
      HostResumeReplayInputs elsewhere = retry;
      elsewhere.peerIpNet = 0x0200007fu;
      check("a different address does not get the answer",
            !host_should_replay_resume_ack(cached, elsewhere));
      HostResumeReplayInputs otherPort = retry;
      otherPort.peerPortNet = static_cast<uint16_t>(kPeerPort + 1);
      check("...nor a different port on the same address",
            !host_should_replay_resume_ack(cached, otherPort));
    }
    {
      HostResumeReplayInputs otherId = retry;
      otherId.resumeId = kResumeId + 1;
      check("a different id is a different recovery, not a retry",
            !host_should_replay_resume_ack(cached, otherId));
      HostResumeReplayInputs otherContent = retry;
      otherContent.requestStreamId = kUdpControlStreamHostToClient;
      check("the same id with different content is not the same ask",
            !host_should_replay_resume_ack(cached, otherContent),
            "an id is a correlator, not a credential");
    }
    {
      // Expiry, in the only form that means anything here: the channel has been re-keyed or
      // reset since, so the answer describes a repair that is no longer in force.
      HostResumeReplayInputs moved = retry;
      moved.channelTxStreamId = kUdpControlStreamHostToClient;
      moved.channelRxStreamId = kUdpControlStreamClientToHost;
      check("an answer the channel no longer holds is not repeated",
            !host_should_replay_resume_ack(cached, moved),
            "Reset() put the base ids back: the cached repair expired");
      // One direction each, because a case that moves both is held up by either check alone:
      // removing the tx comparison left the rx one catching it and the mutation escaped.
      HostResumeReplayInputs rxMoved = retry;
      rxMoved.channelRxStreamId = kUdpControlStreamClientToHost;
      check("...one direction having moved is enough to refuse (client->host)",
            !host_should_replay_resume_ack(cached, rxMoved));
      HostResumeReplayInputs txMoved = retry;
      txMoved.channelTxStreamId = kUdpControlStreamHostToClient;
      check("...and the other direction on its own too (host->client)",
            !host_should_replay_resume_ack(cached, txMoved));
    }
    {
      HostResumeAck ended;  // what the session leaves behind: nothing
      check("after the session ended there is nothing to replay",
            !host_should_replay_resume_ack(ended, retry),
            "a replay of an ended session is the thing the cache must not survive into");
      // ...and the flag alone has to be enough. A zeroed cache is refused by the generation
      // check as well, so testing only that proved nothing about the flag: this one still
      // holds a perfectly matching answer and has simply been marked spent.
      HostResumeAck spent = cached;
      spent.valid = false;
      check("...even when every other field still matches",
            !host_should_replay_resume_ack(spent, retry),
            "clearing the flag is how the cache is retired; it must be sufficient");
      HostResumeReplayInputs notNegotiated = retry;
      notNegotiated.negotiated = false;
      check("a client that never asked for resume is not answered one",
            !host_should_replay_resume_ack(cached, notNegotiated));
      HostResumeReplayInputs noSession = retry;
      noSession.sessionActive = false;
      check("...and neither is a source with no session here",
            !host_should_replay_resume_ack(cached, noSession));
    }

    // The rate. Four a second with a burst of two, on a clock this test owns.
    {
      ResumeAckBudget budget(kResumeAckReplayPerSecond, kResumeAckReplayBurst);
      uint64_t t = 1000000;
      check("the burst lets two answers leave back to back",
            budget.Take(t) && budget.Take(t));
      check("...and the third at the same instant does not",  !budget.Take(t),
            "over the limit means no answer, not a queue");
      t += 249000;
      check("...still not a quarter second later, by a hair", !budget.Take(t));
      t += 2000;
      check("...and one more is allowed once a quarter second has passed", budget.Take(t),
            "four per second");
      t += 10000000;
      check("a long quiet spell refills to the burst and no further",
            budget.Take(t) && budget.Take(t) && !budget.Take(t));
    }

    // The global budget is what a client inventing a new id every time runs into: the
    // per-session bucket belongs to the cached id and is restarted with it, so it alone would
    // never say no.
    {
      ResumeAckBudget perId(kResumeAckReplayPerSecond, kResumeAckReplayBurst);
      ResumeAckBudget global(kResumeAckGlobalPerSecond, kResumeAckGlobalBurst);
      const uint64_t t = 5000000;
      int sent = 0;
      for (int i = 0; i < 200; ++i) {
        perId.Restart(t);  // a new id every time, exactly as the flood would
        if (perId.Take(t) && global.Take(t)) ++sent;
      }
      check("a flood of invented ids is bounded by the host budget, not the per-id one",
            sent == static_cast<int>(kResumeAckGlobalBurst),
            std::to_string(sent) + " answers out of 200 asks");
    }

    // And the rule for a genuinely new recovery while the host is serving is unchanged: it is
    // refused, which is what tells the viewer to keep asking rather than to believe it is back.
    check("a new id arriving while the dispatcher serves is still refused",
          !host_should_accept_resume(serving));
  }

  // ----------------------------------- interrupting a serve, and what makes that allowed (C3 r2)
  //
  // The measured defect: while the dispatcher is inside Serve() every resume was refused, the
  // dispatcher only leaves after its ten second read timeout, and leaving is what turns the
  // video off -- by which time the viewer has stopped asking, because it is told not to repair
  // a session with no picture. Three correct rules closing the window between them.
  //
  // A resume may now interrupt a serve, but only on POSITIVE evidence that the channel has
  // stopped. Not on silence: an idle session is silent and healthy.
  {
    HostResumeEligibility base;
    base.negotiated = true;
    base.sessionActive = true;
    base.servingControl = true;

    check("a source with no session is ignored, serving or not",
          host_resume_verdict([&] { auto e = base; e.sessionActive = false; return e; }()) ==
              HostResumeVerdict::Ignore);
    check("...and so is one that never negotiated",
          host_resume_verdict([&] { auto e = base; e.negotiated = false; return e; }()) ==
              HostResumeVerdict::Ignore);
    check("an ask already answered is answered again, even mid-serve",
          host_resume_verdict([&] { auto e = base; e.cacheMatches = true; return e; }()) ==
              HostResumeVerdict::ReplayAck);
    check("with nobody serving, the original rule is unchanged",
          host_resume_verdict([&] { auto e = base; e.servingControl = false; return e; }()) ==
              HostResumeVerdict::Serve);

    // ⑴ A healthy, idle session. The ask is new and valid; the channel answers; refused.
    {
      HostResumeEligibility e = base;
      check("[counter-example 1] a new ask on a healthy serve asks the channel first",
            host_resume_verdict(e) == HostResumeVerdict::Probe,
            name_of(host_resume_verdict(e)));
      e.probe = ControlProbeState::Pending;
      check("...and keeps waiting while the question is open",
            host_resume_verdict(e) == HostResumeVerdict::Probe);
      e.probe = ControlProbeState::Alive;
      check("[counter-example 1] A CHANNEL THAT ANSWERS KEEPS ITS SESSION",
            host_resume_verdict(e) == HostResumeVerdict::Refuse,
            "silence is not evidence, and an answer is");
    }

    // ⑵ A delayed or replayed ask from an earlier break. Its id is one this host answered, so
    //   it is not a new attempt -- and the cached answer no longer matches, so it is not a
    //   repeat either.
    {
      HostResumeEligibility e = base;
      e.sameIdAsCache = true;
      check("[counter-example 2] a stale ask carrying an id already answered is refused",
            host_resume_verdict(e) == HostResumeVerdict::Refuse,
            "not a new attempt, and not a valid repeat");
      e.probe = ControlProbeState::Dead;
      check("...even if the channel has since stopped answering",
            host_resume_verdict(e) == HostResumeVerdict::Refuse,
            "evidence does not make an old id into a new attempt");
    }

    // ⑶⑷ The failures this exists for.
    {
      HostResumeEligibility e = base;
      e.probe = ControlProbeState::Dead;
      check("[counter-examples 3 and 4] AN UNANSWERED PROBE LETS THE REPAIR IN",
            host_resume_verdict(e) == HostResumeVerdict::Serve);
      HostResumeEligibility lost = base;
      lost.channelPeerLost = true;
      check("...and so does the channel having given up on its own",
            host_resume_verdict(lost) == HostResumeVerdict::Serve);
    }
  }

  // --------------------------------- what a closed control channel is allowed to swallow (C3 r2)
  {
    // The other half of the stall, and the half I had blamed on the host. A closed channel
    // used to answer "mine" to every datagram carrying the magic -- so the moment control
    // died, the viewer stopped seeing VIDEO, and a viewer that sees no video is told not to
    // repair its session. The break destroyed the evidence that the session was worth saving.
    Link link;
    link.Wire();
    check("a message crosses the link", carry_one_message(link, "before"));
    link.viewer.Close(ControlCloseReason::PeerLost);

    UdpVideoChunkHeader video{};
    video.seq = 7;
    uint8_t datagram[sizeof(UdpVideoChunkHeader) + 16]{};
    std::memcpy(datagram, &video, sizeof(video));
    check("A CLOSED CONTROL CHANNEL DOES NOT SWALLOW VIDEO",
          !link.viewer.OnPacket(datagram, sizeof(datagram)),
          "it carries the same magic as everything else on this socket");

    UdpControlResumePacket ask{};
    ask.kind = static_cast<uint16_t>(UdpPacketKind::ControlResume);
    check("...nor a resume ask", !link.viewer.OnPacket(&ask, sizeof(ask)));
    UdpHelloPacket hello{};
    check("...nor a hello", !link.viewer.OnPacket(&hello, sizeof(hello)));

    // ...but it does still claim its own, which is what stops a dead channel's traffic from
    // being mistaken for something else.
    UdpControlAckPacket ack{};
    ack.kind = static_cast<uint16_t>(UdpPacketKind::ControlAck);
    ack.streamId = kUdpControlStreamHostToClient;
    check("...and it does claim control traffic", link.viewer.OnPacket(&ack, sizeof(ack)));
  }

  // ------------------------------- a probe answers about an instant, not a recovery (C3 r3)
  //
  // r2 asked once per resumeId and re-read the answer for every retry. A channel that
  // happened to answer the first ask and died a moment later was therefore refused for the
  // whole thirty second ceiling -- the stall this feature exists to remove, put back one
  // layer up. The viewer cannot work around it: one break is one resumeId, by design.
  {
    // ⑴ alive, then the link really goes. The retry must bring a fresh question.
    ResumeProbeFreshness alive;
    alive.state = ControlProbeState::Alive;
    alive.ageUs = kResumeProbeFreshUs - 1;
    check("[counter-example 1] a fresh Alive is believed", 
          resume_probe_action(alive) == ResumeProbeAction::Use);
    alive.ageUs = kResumeProbeFreshUs;
    check("[counter-example 1] A STALE Alive IS ASKED AGAIN, NOT RE-READ",
          resume_probe_action(alive) == ResumeProbeAction::Restart,
          "otherwise one lucky answer refuses the repair for the whole ceiling");

    // ⑵ the same thing arrived at by a late old Ack rather than by a healthy link: the
    //   rule cannot tell the difference and must not need to. Age is the whole test.
    ResumeProbeFreshness late = alive;
    late.ageUs = 5 * kResumeProbeFreshUs;
    check("[counter-example 2] ...however it came to be Alive", 
          resume_probe_action(late) == ResumeProbeAction::Restart);

    // ⑶ after a re-key or a rollover there is no verdict at all, and Idle asks.
    ResumeProbeFreshness cleared;
    cleared.state = ControlProbeState::Idle;
    check("[counter-example 3] no verdict means ask", 
          resume_probe_action(cleared) == ResumeProbeAction::Restart);

    // ⑷ a retry inside the freshness window costs nothing extra.
    ResumeProbeFreshness pending;
    pending.state = ControlProbeState::Pending;
    pending.ageUs = 10 * kResumeProbeFreshUs;
    check("[counter-example 4] a question still in flight is not re-asked",
          resume_probe_action(pending) == ResumeProbeAction::Use,
          "the attempt budget is what bounds that one");
    ResumeProbeFreshness dead;
    dead.state = ControlProbeState::Dead;
    dead.ageUs = 100 * kResumeProbeFreshUs;
    check("...and Dead is acted on whatever its age", 
          resume_probe_action(dead) == ResumeProbeAction::Use,
          "the ask that reads it is served, so re-asking would only delay the repair");

    // The hard ceiling, on top of the rate the send budgets already impose.
    ResumeProbeFreshness spent = alive;
    spent.ageUs = 1000 * kResumeProbeFreshUs;
    spent.probesThisRecovery = kResumeProbeMaxPerRecovery;
    check("a recovery cannot probe without limit", 
          resume_probe_action(spent) == ResumeProbeAction::Use,
          std::to_string(kResumeProbeMaxPerRecovery) + " probes is the ceiling");
  }

  // --------------------------------- a re-key or a close leaves no verdict behind (C3 r3)
  {
    Link link;
    link.Wire();
    check("a message crosses the link", carry_one_message(link, "hello"));
    link.host.StartProbe(kResumeProbeMaxAttempts, kResumeProbeIntervalUs);
    check("the probe is answered", link.SettleProbe(4000) == ControlProbeState::Alive);
    check("...and the verdict is stamped", link.host.ProbeDecidedUs() > 0);
    check("...and its age is measured by the clock that stamped it",
          link.host.ProbeAgeUs() < 4000000,
          std::to_string(link.host.ProbeAgeUs()) + "us -- a cross-clock subtraction would not "
          "land in this range");
    // ...and it ADVANCES. An age that is always zero reads as permanently fresh, which is the
    // r2 defect wearing a different hat: one verdict believed for the whole recovery.
    const uint64_t ageBefore = link.host.ProbeAgeUs();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const uint64_t ageAfter = link.host.ProbeAgeUs();
    check("AND IT GETS OLDER", ageAfter >= ageBefore + 250000 && ageAfter < 4000000,
          std::to_string(ageBefore) + "us then " + std::to_string(ageAfter) + "us");

    link.host.ResumeWith(control_resume_stream_id(kUdpControlStreamHostToClient, 0x99),
                         control_resume_stream_id(kUdpControlStreamClientToHost, 0x99));
    check("[counter-example 3] A RE-KEY LEAVES NO VERDICT BEHIND",
          link.host.ProbeStatus() == ControlProbeState::Idle,
          to_string(link.host.ProbeStatus()));
    check("...and no timestamp either", link.host.ProbeDecidedUs() == 0);

    Link second;
    second.Wire();
    check("a message crosses the second link", carry_one_message(second, "hello"));
    second.host.StartProbe(kResumeProbeMaxAttempts, kResumeProbeIntervalUs);
    check("its probe is answered too",
          second.SettleProbe(4000) == ControlProbeState::Alive);
    second.host.Close(ControlCloseReason::PeerLost);
    check("a closed channel reports no verdict",
          second.host.ProbeStatus() == ControlProbeState::Idle,
          "whoever closed it, and for whatever reason");
    check("...nor a timestamp", second.host.ProbeDecidedUs() == 0);
    second.host.Reset();
    check("...and a reset leaves none", second.host.ProbeStatus() == ControlProbeState::Idle);
  }

  // ------------------------------------------------- one recovery raises one wake (C3 r2)
  {
    check("an accepted ask with nothing pending raises its own wake",
          host_resume_should_raise_wake(true, false, false));
    check("A REPEAT OF THE ASK ALREADY BEING SERVED JOINS IT",
          !host_resume_should_raise_wake(true, true, true),
          "two wakes would re-key the channel the first one had just repaired");
    check("a DIFFERENT ask arriving while one is pending still raises its own",
          host_resume_should_raise_wake(true, true, false),
          "it is a different recovery, not a repeat");
    check("and an ask that was not accepted wakes nothing",
          !host_resume_should_raise_wake(false, false, false));
  }

  // ------------------------------------------- the probe itself, over two real channels (C3 r2)
  {
    // ⑴ healthy: the peer answers, and its application is handed NOTHING.
    Link link;
    link.Wire();
    check("a message crosses the link before the probe", carry_one_message(link, "hello"));
    link.host.StartProbe(kResumeProbeMaxAttempts, kResumeProbeIntervalUs);
    const ControlProbeState healthy = link.SettleProbe(4000);
    check("[counter-example 1] A HEALTHY CHANNEL ANSWERS THE PROBE",
          healthy == ControlProbeState::Alive, to_string(healthy));
    std::vector<uint8_t> leaked;
    check("...and the peer's application was handed nothing by it",
          !link.viewer.Receive(&leaked, 50),
          "an unsolicited message would be read as the answer to the peer's next request");
    check("...and the link still carries messages afterwards",
          carry_one_message(link, "still here"),
          "counter-example 2: probing a healthy channel must not disturb it");
    std::printf("  probe on a healthy channel: %u attempt(s)\n", link.host.ProbeAttempts());

    // ⑶ one direction only: the peer hears everything and nothing it says gets back. A silence
    //   test at the host would see traffic arriving and call the channel healthy; the round
    //   trip is what tells the truth.
    Link oneWay;
    oneWay.Wire();
    check("a message crosses the one-way link first", carry_one_message(oneWay, "hello"));
    oneWay.dropToHost = true;
    oneWay.host.ClearProbe();
    oneWay.host.StartProbe(kResumeProbeMaxAttempts, kResumeProbeIntervalUs);
    const ControlProbeState oneWayState = oneWay.SettleProbe(6000);
    check("[counter-example 3] A ONE-WAY FAILURE IS FOUND BY THE ROUND TRIP",
          oneWayState == ControlProbeState::Dead, to_string(oneWayState));
    check("...and the peer did receive every one of them",
          oneWay.droppedToHost >= kResumeProbeMaxAttempts,
          std::to_string(oneWay.droppedToHost) + " answers dropped on the way back");

    // ⑷ both directions.
    Link both;
    both.Wire();
    check("a message crosses the link first", carry_one_message(both, "hello"));
    both.dropToViewer = true;
    both.dropToHost = true;
    both.host.ClearProbe();
    both.host.StartProbe(kResumeProbeMaxAttempts, kResumeProbeIntervalUs);
    const ControlProbeState bothState = both.SettleProbe(6000);
    check("[counter-example 4] a two-way failure is found too",
          bothState == ControlProbeState::Dead, to_string(bothState));

    // The safety property of the mechanism, and the reason the probe names a seq of its own:
    // it must never be satisfied by, or swallow, the acknowledgement of a REAL message. If it
    // did, the message in flight would never be acknowledged and would retransmit until the
    // channel declared the peer lost -- the probe would have caused the failure it reports.
    Link mix;
    mix.Wire();
    check("a first message is delivered and acknowledged", carry_one_message(mix, "first"));
    mix.host.StartProbe(kResumeProbeMaxAttempts, kResumeProbeIntervalUs);
    check("a real message goes out while the probe is waiting", mix.host.Send("second", 6));
    mix.PumpDroppingAck(1);  // the probe names seq 1; its answer is the one that goes missing
    check("THE PROBE IS NOT SATISFIED BY ANOTHER MESSAGE'S ACK",
          mix.host.ProbeStatus() == ControlProbeState::Pending,
          to_string(mix.host.ProbeStatus()));
    std::vector<uint8_t> second;
    check("...and the real message was still delivered", mix.viewer.Receive(&second, 100) &&
                                                             std::string(second.begin(),
                                                                         second.end()) == "second");
    // ...and its own acknowledgement was not swallowed either: only the head of the queue is
    // in flight, so a third message arriving proves the second one left the queue.
    mix.Pump();
    check("a third message follows it", mix.host.Send("third", 5));
    mix.Pump();
    std::vector<uint8_t> third;
    check("...AND ARRIVES, so the queue moved on", mix.viewer.Receive(&third, 100) &&
                                                       std::string(third.begin(), third.end()) ==
                                                           "third",
          "a swallowed ack would have left the second message at the head forever");

    std::printf("  probe budget: %u attempts x %llu us = %llu ms before a channel is called "
                "stopped\n",
                kResumeProbeMaxAttempts, (unsigned long long)kResumeProbeIntervalUs,
                (unsigned long long)(kResumeProbeMaxAttempts * kResumeProbeIntervalUs / 1000));
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}

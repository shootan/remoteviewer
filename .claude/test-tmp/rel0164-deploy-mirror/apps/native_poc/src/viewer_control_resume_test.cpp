// The viewer's half of a control resume, without a host. (item 8, C3, layer 1)
//
// Pure in the sense that matters: no process, no socket, no clock the test does not own. The
// channel is real (UdpControlChannel) because the one thing that must be true after a resume is
// something only the real channel can say -- that the re-key happened once, and that a datagram
// from before it is discarded.
//
// What each group holds:
//   decide wiring     -- an old host is never asked; a break asks; the retry interval is honoured
//   the resumeId      -- one recovery, one id; a new break, a new id, INCLUDING after a success
//   Ack validation    -- every rejection separately, because one clause covering for another is
//                        how a check that looks thorough turns out to be one check
//   idempotence       -- a repeated Ack costs nothing and re-keys nothing
//   in-flight request -- never resent, and a late reply is not an answer
//   the ceiling       -- 30 s from the break, and a mid-recovery retry does not extend it

// NOMINMAX before anything pulls in windows.h: native_socket.hpp calls std::min, and the
// macro turns it into a syntax error three headers deep.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "control_resume.hpp"
#include "poc_protocol.hpp"
#include "udp_control_channel.hpp"
#include "viewer_control_resume.hpp"

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

constexpr uint32_t kPeerIp = 0x0100007fu;
constexpr uint16_t kPeerPort = 0x3412u;
constexpr uint64_t kGeneration = 5;

/** The answer the host would send, as bytes, with every field open to being made wrong. */
std::vector<uint8_t> make_ack(uint32_t resumeId, uint32_t streamId, uint32_t accepted,
                              uint16_t kind = static_cast<uint16_t>(UdpPacketKind::ControlResumeAck),
                              uint32_t magic = kMagic,
                              uint16_t size = sizeof(UdpControlResumePacket),
                              size_t extraBytes = 0) {
  UdpControlResumePacket packet{};
  packet.magic = magic;
  packet.kind = kind;
  packet.size = size;
  packet.streamId = streamId;
  packet.resumeId = resumeId;
  packet.accepted = accepted;
  std::vector<uint8_t> bytes(sizeof(packet) + extraBytes, 0);
  std::memcpy(bytes.data(), &packet, sizeof(packet));
  return bytes;
}

/** The expectation a healthy recovery presents, so each check can be knocked out one at a time. */
ViewerResumeExpectation asking(uint32_t resumeId) {
  ViewerResumeExpectation e;
  e.negotiated = true;
  e.sessionActive = true;
  e.attemptInFlight = true;
  e.rekeyedForThisId = false;
  e.sessionGeneration = kGeneration;
  e.peerIpNet = kPeerIp;
  e.peerPortNet = kPeerPort;
  e.baseRxStreamId = kUdpControlStreamHostToClient;
  e.resumeId = resumeId;
  return e;
}

ViewerResumeAckFacts served(uint32_t resumeId) {
  ViewerResumeAckFacts f;
  f.datagramLen = sizeof(UdpControlResumePacket);
  f.magic = kMagic;
  f.kind = static_cast<uint16_t>(UdpPacketKind::ControlResumeAck);
  f.size = sizeof(UdpControlResumePacket);
  f.streamId = control_resume_stream_id(kUdpControlStreamHostToClient, resumeId);
  f.resumeId = resumeId;
  f.accepted = 1;
  f.peerIpNet = kPeerIp;
  f.peerPortNet = kPeerPort;
  f.sessionGeneration = kGeneration;
  return f;
}

const char* name_of(ViewerResumeAckVerdict v) {
  switch (v) {
    case ViewerResumeAckVerdict::Rekey: return "Rekey";
    case ViewerResumeAckVerdict::AlreadyApplied: return "AlreadyApplied";
    case ViewerResumeAckVerdict::Refused: return "Refused";
    default: return "Ignore";
  }
}

const char* name_of(ControlResumeAction a) {
  switch (a) {
    case ControlResumeAction::Send: return "Send";
    case ControlResumeAction::GiveUp: return "GiveUp";
    default: return "Idle";
  }
}

/**
 * A viewer channel wired to a host channel, so traffic is carried rather than described.
 *
 * Delivery is DEFERRED rather than immediate, and that is not a stylistic choice. A send
 * happens with the sending channel's mutex held, and delivering it straight into the peer
 * makes the peer answer with an ack -- back into the first channel, whose lock this thread is
 * already holding. The first version of this harness did exactly that and the process was
 * killed by the runtime before printing a single line. The channel's own test uses a pump
 * thread for the same reason; this one pumps explicitly so the test keeps its clock.
 */
struct Pair {
  UdpControlChannel viewer;
  UdpControlChannel host;
  std::vector<std::vector<uint8_t>> toHost;
  std::vector<std::vector<uint8_t>> toViewer;

  void Link() {
    viewer.Configure(
        [this](const void* d, size_t n) {
          toHost.emplace_back(static_cast<const uint8_t*>(d),
                              static_cast<const uint8_t*>(d) + n);
          return true;
        },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, 1200);
    host.Configure(
        [this](const void* d, size_t n) {
          toViewer.emplace_back(static_cast<const uint8_t*>(d),
                                static_cast<const uint8_t*>(d) + n);
          return true;
        },
        kUdpControlStreamHostToClient, kUdpControlStreamClientToHost, 1200);
  }

  /** Carries everything in flight, both ways, until the wire is quiet. */
  void Pump(int rounds = 16) {
    for (int i = 0; i < rounds && !(toHost.empty() && toViewer.empty()); ++i) {
      std::vector<std::vector<uint8_t>> h;
      std::vector<std::vector<uint8_t>> v;
      h.swap(toHost);
      v.swap(toViewer);
      for (const auto& d : h) host.OnPacket(d.data(), d.size());
      for (const auto& d : v) viewer.OnPacket(d.data(), d.size());
    }
  }
};

/** The component under test, plus what it sent and said. */
struct Harness {
  Pair pair;
  ViewerControlResume resume;
  std::vector<UdpControlResumePacket> asks;
  std::vector<std::string> logs;
  uint64_t generation = kGeneration;
  uint32_t nextId = 0x1000;
  bool sendOk = true;

  void Start(bool negotiated = true) {
    pair.Link();
    ViewerControlResume::Config cfg;  // 500 ms retry, 30 s ceiling
    resume.Configure(
        &pair.viewer,
        [this](const void* data, size_t len) {
          if (len == sizeof(UdpControlResumePacket)) {
            UdpControlResumePacket p{};
            std::memcpy(&p, data, sizeof(p));
            asks.push_back(p);
          }
          return sendOk;
        },
        [this](const std::string& line) { logs.push_back(line); },
        [this]() { return ++nextId; }, [this]() { return generation; }, cfg, negotiated, kPeerIp,
        kPeerPort);
  }

  void Deliver(const std::vector<uint8_t>& bytes) { resume.OnDatagram(bytes.data(), bytes.size()); }
};

/** One host->viewer control message, carried by the real channel. */
bool send_host_message(Pair& pair, const std::string& body) {
  if (!pair.host.Send(body.data(), body.size())) return false;
  pair.Pump();
  return true;
}

bool viewer_has_message(Pair& pair, const std::string& expect) {
  pair.Pump();
  std::vector<uint8_t> got;
  if (!pair.viewer.Receive(&got, 50)) return false;
  return std::string(got.begin(), got.end()) == expect;
}

}  // namespace

int main() {
  // Unbuffered: a crash mid-suite must not take the evidence of where it got to with it.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::cout.setf(std::ios::unitbuf);
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);
  std::cout << "viewer_control_resume_test\n";

  // ------------------------------------------------------------------ the decision, wired up
  {
    Harness h;
    h.Start(/*negotiated=*/false);
    h.resume.BeginBreak(1000000);
    check("an old host is never asked",
          h.resume.Poll(/*videoAlive=*/true, 2000000) == ControlResumeAction::Idle &&
              h.asks.empty(),
          std::to_string(h.asks.size()) + " datagrams sent");
    check("...and no recovery is even started against one", !h.resume.attempt_in_flight());
  }
  {
    Harness h;
    h.Start();
    check("nothing is sent while control is up",
          h.resume.Poll(true, 1000000) == ControlResumeAction::Idle && h.asks.empty());

    h.resume.BeginBreak(1000000);
    check("a break asks at once", h.resume.Poll(true, 1000000) == ControlResumeAction::Send);
    check("...one datagram", h.asks.size() == 1, std::to_string(h.asks.size()));
    check("...a ControlResume, not an answer",
          h.asks[0].kind == static_cast<uint16_t>(UdpPacketKind::ControlResume));
    check("...carrying the viewer's own base stream",
          h.asks[0].streamId == kUdpControlStreamClientToHost);
    check("...and the id of this recovery", h.asks[0].resumeId == h.resume.resume_id());

    check("it does not ask again immediately",
          h.resume.Poll(true, 1100000) == ControlResumeAction::Idle && h.asks.size() == 1);
    check("...and does at the retry interval",
          h.resume.Poll(true, 1500000) == ControlResumeAction::Send && h.asks.size() == 2);
    check("THE SAME id, because it is the same break",
          h.asks[1].resumeId == h.asks[0].resumeId,
          "a new id each retry is what made the host's cached answer useless");

    // The picture is what says this session is worth repairing.
    check("it stops asking when the video has stopped too",
          h.resume.Poll(/*videoAlive=*/false, 2500000) == ControlResumeAction::Idle &&
              h.asks.size() == 2,
          "that is a session ending, not a channel to repair");
  }

  // ------------------------------------------------------------------ one recovery, one id
  {
    Harness h;
    h.Start();
    h.resume.BeginBreak(1000000);
    const uint32_t first = h.resume.resume_id();
    h.resume.BeginBreak(1200000);  // the worker may call this on every turn
    check("a second call inside the same break keeps the id", h.resume.resume_id() == first);

    // A SUCCESSFUL recovery, then another break. This is the case Codex called out: a new id must
    // not be limited to the give-up path, or the host's cached answer for the previous repair
    // would satisfy the next request.
    h.resume.Finish(true);
    h.resume.BeginBreak(4000000);
    check("the NEXT break gets a new id even though the last one succeeded",
          h.resume.resume_id() != first,
          std::to_string(first) + " then " + std::to_string(h.resume.resume_id()));
    const uint32_t second = h.resume.resume_id();
    h.resume.Finish(false);
    h.resume.BeginBreak(9000000);
    check("...and so does the one after a give-up", h.resume.resume_id() != second);
  }

  // ------------------------------------------------------------------ what an answer must be
  {
    const uint32_t id = 0x777;
    check("a well formed served answer is accepted",
          viewer_check_resume_ack(asking(id), served(id)).verdict == ViewerResumeAckVerdict::Rekey);

    const auto reject = [&](const char* what, ViewerResumeAckFacts f,
                            ViewerResumeAckVerdict want = ViewerResumeAckVerdict::Ignore) {
      const ViewerResumeAckCheck got = viewer_check_resume_ack(asking(id), f);
      check(what, got.verdict == want, std::string(name_of(got.verdict)) + " (" + got.why + ")");
    };

    ViewerResumeAckFacts f = served(id);
    f.datagramLen = sizeof(UdpControlResumePacket) + 1;
    reject("a longer datagram is not this packet", f);
    f = served(id);
    f.datagramLen = sizeof(UdpControlResumePacket) - 1;
    reject("...nor a shorter one", f);
    f = served(id);
    f.magic = kMagic + 1;
    reject("a wrong magic is refused", f);
    f = served(id);
    f.kind = static_cast<uint16_t>(UdpPacketKind::ControlResume);
    reject("our own request reflected back is not an answer", f);
    f = served(id);
    f.size = sizeof(UdpControlResumePacket) - 4;
    reject("a size field that disagrees with the layout is refused", f);
    f = served(id);
    f.peerIpNet = kPeerIp + 1;
    reject("an answer from another address is refused", f);
    f = served(id);
    f.peerPortNet = kPeerPort + 1;
    reject("...and from another port", f);
    f = served(id);
    f.sessionGeneration = kGeneration + 1;
    reject("an answer that arrives after the session was replaced is refused", f);
    f = served(id);
    f.resumeId = id + 1;
    reject("an answer to a different attempt is refused", f);
    f = served(id);
    f.streamId = kUdpControlStreamHostToClient;
    reject("an answer that does not name the derived stream is refused", f,
           ViewerResumeAckVerdict::Ignore);
    f = served(id);
    f.streamId = control_resume_stream_id(kUdpControlStreamClientToHost, id);
    reject("...including one derived from the wrong direction", f);
    f = served(id);
    f.accepted = 0;
    reject("served=0 is a refusal, not an agreement", f, ViewerResumeAckVerdict::Refused);

    ViewerResumeExpectation e = asking(id);
    e.negotiated = false;
    check("an answer to a viewer that never asked is refused",
          viewer_check_resume_ack(e, served(id)).verdict == ViewerResumeAckVerdict::Ignore);
    e = asking(id);
    e.sessionActive = false;
    check("...or to a session that has ended",
          viewer_check_resume_ack(e, served(id)).verdict == ViewerResumeAckVerdict::Ignore);
    e = asking(id);
    e.attemptInFlight = false;
    check("...or when no recovery is running at all",
          viewer_check_resume_ack(e, served(id)).verdict == ViewerResumeAckVerdict::Ignore);
    e = asking(id);
    e.rekeyedForThisId = true;
    check("a repeat of an answer already applied is ignored, not reapplied",
          viewer_check_resume_ack(e, served(id)).verdict == ViewerResumeAckVerdict::AlreadyApplied);
  }

  // ------------------------------------------------------------------ the re-key, once
  {
    Harness h;
    h.Start();
    h.resume.BeginBreak(1000000);
    h.resume.Poll(true, 1000000);
    const uint32_t id = h.resume.resume_id();

    const auto before = h.pair.viewer.StreamIds();
    check("a refusal does not re-key anything",
          (h.Deliver(make_ack(id, kUdpControlStreamHostToClient, 0)), !h.resume.ApplyPendingRekey()),
          "served=0");
    check("...and the channel still holds the base ids",
          h.pair.viewer.StreamIds().tx == before.tx && h.pair.viewer.StreamIds().rx == before.rx);

    // An answer for somebody else's attempt.
    h.Deliver(make_ack(id + 1, control_resume_stream_id(kUdpControlStreamHostToClient, id + 1), 1));
    check("an answer to another attempt does not re-key", !h.resume.ApplyPendingRekey());

    // The real one.
    h.Deliver(make_ack(id, control_resume_stream_id(kUdpControlStreamHostToClient, id), 1));
    check("THE ANSWER RE-KEYS THE CHANNEL", h.resume.ApplyPendingRekey());
    const auto after = h.pair.viewer.StreamIds();
    check("...onto the ids derived from this recovery",
          after.tx == control_resume_stream_id(kUdpControlStreamClientToHost, id) &&
              after.rx == control_resume_stream_id(kUdpControlStreamHostToClient, id));
    check("...and the channel is open again", !h.pair.viewer.IsClosed());
    check("...and it is recorded as done", h.resume.rekeyed());

    // Idempotence, observed rather than asserted: put a message in the channel, then repeat the
    // answer. A second ResumeWith would clear the queues and the message would be gone.
    h.pair.host.ResumeWith(after.rx, after.tx);
    check("a message arrives on the resumed channel", send_host_message(h.pair, "hello-again"));
    const uint64_t duplicatesBefore = h.resume.duplicate_acks();
    h.Deliver(make_ack(id, control_resume_stream_id(kUdpControlStreamHostToClient, id), 1));
    check("a repeated answer is counted as a duplicate",
          h.resume.duplicate_acks() == duplicatesBefore + 1);
    check("...and applies nothing", !h.resume.ApplyPendingRekey());
    check("...so the message that was already delivered survives it",
          viewer_has_message(h.pair, "hello-again"),
          "a second re-key would have cleared the queue under it");
  }

  // ------------------------------------------------------------- an answer that arrives too late
  //
  // Both of these deliver a PERFECTLY VALID answer and then move the world on before the
  // worker gets to it. That gap is the only way to reach these two guards -- validation on the
  // ingress thread cannot see a future in which the recovery is over.
  {
    Harness h;
    h.Start();
    h.resume.BeginBreak(1000000);
    h.resume.Poll(true, 1000000);
    const uint32_t id = h.resume.resume_id();
    h.Deliver(make_ack(id, control_resume_stream_id(kUdpControlStreamHostToClient, id), 1));
    h.resume.Finish(true);  // the worker decided the recovery was over first
    check("an answer verified before the recovery ended re-keys nothing after it",
          !h.resume.ApplyPendingRekey() && !h.resume.rekeyed());
  }
  {
    Harness h;
    h.Start();
    h.resume.BeginBreak(1000000);
    h.resume.Poll(true, 1000000);
    const uint32_t id = h.resume.resume_id();
    h.Deliver(make_ack(id, control_resume_stream_id(kUdpControlStreamHostToClient, id), 1));
    h.resume.EndSession();  // ...or the shell called the whole session off
    check("AND A CANCELLED SESSION RE-KEYS NOTHING, however valid the answer was",
          !h.resume.ApplyPendingRekey() && !h.resume.rekeyed(),
          "cancellation wins over an answer already in hand");
  }

  // ------------------------------------------------------------------ what the re-key discards
  {
    // The reason the stream is re-keyed at all: a datagram delayed across the break carries a
    // sequence number far ahead of the fresh stream, and delivering it would swallow every real
    // message below it. After the re-key it is simply not this stream.
    Harness h;
    h.Start();
    h.resume.BeginBreak(1000000);
    h.resume.Poll(true, 1000000);
    const uint32_t id = h.resume.resume_id();

    // Capture what a pre-break host message looks like on the wire.
    std::vector<std::vector<uint8_t>> wire;
    UdpControlChannel oldHost;
    oldHost.Configure([&](const void* d, size_t n) {
                        wire.emplace_back(static_cast<const uint8_t*>(d),
                                          static_cast<const uint8_t*>(d) + n);
                        return true;
                      },
                      kUdpControlStreamHostToClient, kUdpControlStreamClientToHost, 1200);
    check("the old host had a message in flight", oldHost.Send("stale", 5));

    h.Deliver(make_ack(id, control_resume_stream_id(kUdpControlStreamHostToClient, id), 1));
    check("the channel is re-keyed", h.resume.ApplyPendingRekey());

    for (const auto& datagram : wire) h.pair.viewer.OnPacket(datagram.data(), datagram.size());
    std::vector<uint8_t> got;
    check("A DATAGRAM FROM BEFORE THE BREAK IS NOT DELIVERED",
          !h.pair.viewer.Receive(&got, 50),
          "it belongs to a stream this channel has stopped listening on");
  }

  // ------------------------------------------------------------------ the in-flight request
  {
    ViewerPendingControlRequest pending;
    pending.active = true;
    pending.kind = 7;
    pending.requestId = 42;
    check("a request that was in flight when control broke is never resent",
          !viewer_resume_may_resend(pending),
          "it may not be idempotent, and one user action must not become two");
    check("a reply to it arriving later is late", viewer_resume_reply_is_late(pending, 41));
    check("...and so is one for a request that does not exist",
          viewer_resume_reply_is_late(ViewerPendingControlRequest{}, 42));
    check("the reply to the request actually outstanding is not late",
          !viewer_resume_reply_is_late(pending, 42));
  }

  // ------------------------------------------------------------------ the ceiling is T3's
  {
    Harness h;
    h.Start();
    h.resume.BeginBreak(1000000);
    h.resume.Poll(true, 1000000);
    // Polled once and held. Calling Poll again to describe the result mutates the thing being
    // described -- the second call records an attempt at a later time, and argument evaluation
    // order decides which one the check sees. That is what made this read Send and fail.
    const ControlResumeAction inside = h.resume.Poll(true, 1000000 + 20000000);
    check("it keeps asking well inside the ceiling", inside == ControlResumeAction::Send,
          name_of(inside));
    check("AND GIVES UP AT THIRTY SECONDS",
          h.resume.Poll(true, 1000000 + 30000000) == ControlResumeAction::GiveUp);

    // A re-key that did not produce a working channel starts again under a new id -- and that
    // must not buy the recovery another thirty seconds.
    Harness g;
    g.Start();
    g.resume.BeginBreak(1000000);
    g.resume.Poll(true, 1000000);
    const uint32_t firstId = g.resume.resume_id();
    g.Deliver(make_ack(firstId, control_resume_stream_id(kUdpControlStreamHostToClient, firstId), 1));
    check("the first answer applies", g.resume.ApplyPendingRekey());
    g.resume.RetryWithNewId(25000000);
    check("a failed round trip carries on under a NEW id", g.resume.resume_id() != firstId,
          "the host has cached its answer for the old one and would repeat it forever");
    check("...and the ceiling still runs from the original break",
          g.resume.Poll(true, 1000000 + 30000000) == ControlResumeAction::GiveUp,
          "not from the retry");
  }

  // ------------------------------------------------------------------ cancellation wins
  {
    Harness h;
    h.Start();
    h.resume.BeginBreak(1000000);
    h.resume.Poll(true, 1000000);
    const uint32_t id = h.resume.resume_id();
    h.resume.EndSession();
    check("a session that ended asks for nothing",
          h.resume.Poll(true, 2000000) == ControlResumeAction::Idle && h.asks.size() == 1);
    h.Deliver(make_ack(id, control_resume_stream_id(kUdpControlStreamHostToClient, id), 1));
    check("...and an answer arriving afterwards re-keys nothing",
          !h.resume.ApplyPendingRekey() && !h.resume.rekeyed(),
          "cancellation and session end come first, always");
  }

  // ------------------------------------------------------------------ what it says while doing it
  {
    Harness h;
    h.Start();
    h.resume.BeginBreak(1000000);
    h.resume.Poll(true, 1000000);
    h.resume.Poll(true, 1500000);
    size_t sends = 0;
    for (const auto& line : h.logs) {
      if (line.find("[control-resume] send") != std::string::npos) ++sends;
    }
    check("one line per attempt, not one per turn", sends == 2,
          std::to_string(sends) + " send lines for 2 attempts and 4 polls");
  }

  WSACleanup();
  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  (" << gChecks
            << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}

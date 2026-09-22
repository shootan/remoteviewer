#pragma once

// When to ask for a control-channel resume, and when to honour one. (item 8)
//
// Role:    the two decisions that make control re-establishment safe, as pure functions of state
//          so both can be exercised without a socket, a peer, or a clock.
// Thread:  none of its own. Callers hold whatever lock their own state needs.
// Input:   negotiated capability, whether control is dead, whether video is still arriving, how
//          long control has been dead, and how long since the last attempt.
// Output:  emit an attempt / do nothing / stop trying; and, on the host, accept or refuse.
// Callers: the viewer control thread (ask) and the host UDP reader (honour).
//
// The background is T3 (0f26422): a session whose control channel dies but whose video keeps
// arriving is kept alive for up to 30 s rather than torn down, because the picture proves the
// session is not dead. That was right, and it left a hole -- for those seconds the viewer shows a
// live picture and accepts no input, because nothing rebuilds control. This decides when to try.
//
// Two things it deliberately does NOT do:
//   - It does not try when video has stopped too. That is a session that really is dying, and
//     retrying into it would both spin and blur the one signal the liveness verdict has.
//   - It does not try forever. The 30 s ceiling is T3's, and resume has to stay inside it or it
//     would quietly become a way for a dead session to live on.

#include <cstdint>

// For kUdpFeatureControlResume: the negotiation helper below answers a question about the wire,
// so the wire's own definition is where the answer has to come from.
#include "poc_protocol.hpp"
#include "udp_control_channel.hpp"

namespace remote60::native_poc {

struct ControlResumeConfig {
  // Between attempts. Small enough that a link coming back is noticed promptly, large enough that
  // a link that is not coming back costs one datagram every half second rather than a spin.
  uint64_t retryIntervalUs = 500000;
  // The T3 ceiling. Past this the session is ending anyway, so asking is pointless noise.
  uint64_t giveUpAfterUs = 30000000;
};

struct ControlResumeInputs {
  // Both peers advertised kUdpFeatureControlResume. False against a host that predates it, which
  // is the whole of the backward-compatibility story: an old host is simply never asked.
  bool negotiated = false;
  // The control channel has stopped carrying messages -- closed, or timed out with nothing coming.
  bool controlDead = false;
  // Frames are still arriving. This is what distinguishes a broken channel from a dead session.
  bool videoAlive = false;
  uint64_t controlDeadForUs = 0;
  // Large on the first call after control died, so the first attempt is not delayed.
  uint64_t sinceLastAttemptUs = 0;
};

enum class ControlResumeAction : uint8_t {
  Idle = 0,   // nothing to do right now
  Send,       // emit a resume attempt
  GiveUp,     // stop asking; the ceiling has passed
};

inline ControlResumeAction control_resume_decide(const ControlResumeConfig& config,
                                                 const ControlResumeInputs& in) {
  // An old host never advertised the bit, so it is never asked. Not a failure and not a give-up:
  // there is simply nothing to try, and the session behaves exactly as it did before item 8.
  if (!in.negotiated) return ControlResumeAction::Idle;
  if (!in.controlDead) return ControlResumeAction::Idle;
  // Checked before the ceiling so that a session whose video has also stopped is left entirely to
  // the liveness verdict rather than being declared given-up by this.
  if (!in.videoAlive) return ControlResumeAction::Idle;
  if (config.giveUpAfterUs > 0 && in.controlDeadForUs >= config.giveUpAfterUs) {
    return ControlResumeAction::GiveUp;
  }
  if (in.sinceLastAttemptUs < config.retryIntervalUs) return ControlResumeAction::Idle;
  return ControlResumeAction::Send;
}

/**
 * Whether the host should honour a resume it has just received.
 *
 * The narrow condition is `servingControl`. While the dispatcher is inside Serve() the channel is
 * in use and resetting it would break a working session; the only time a reset is the right answer
 * is when nobody is serving, which is exactly the state a lost peer leaves behind. That is also
 * what keeps this from being a way to disrupt a healthy session: for a stranger's packet to do
 * anything, control has to be broken already.
 */
struct HostResumeInputs {
  bool negotiated = false;      // this client asked for resume in its Hello and the host offered it
  bool sessionActive = false;   // there is a session to resume onto (a client authenticated)
  bool servingControl = false;  // the dispatcher is inside Serve() right now
};

/**
 * The stream id each direction uses after a resume.
 *
 * Resetting the channel restarts its sequence numbers at 1, and that alone is not safe: a datagram
 * from before the break -- delayed, reordered, or simply sitting in a socket buffer -- carries a
 * sequence number far ahead of the fresh stream. UdpControlChannel::HandleData delivers a message
 * the moment it is complete and sets rxDeliveredSeq_ to that number, so one stale datagram would
 * be delivered as real traffic and every genuinely new message below it silently dropped and
 * acked (udp_control_channel.cpp:183). Alive on the wire, deaf above -- again, and this time from
 * the repair rather than the break.
 *
 * So the resumed stream is given a new id, derived from the resumeId both peers agreed on.
 * OnPacket already discards anything whose streamId is not the one it expects, in both directions
 * (:271 and :283), which turns every pre-resume datagram into something the channel ignores
 * without a single change to the data packet's layout.
 *
 * The top bit is always set, so a derived id can never collide with the two base ids (1 and 2),
 * and the low two bits carry the direction, so the two directions stay distinct. The resumeId
 * counts up per resume, so consecutive attempts never derive the same id either.
 */
inline uint32_t control_resume_stream_id(uint32_t baseStreamId, uint32_t resumeId) {
  return 0x80000000u | ((resumeId << 2) & 0x7FFFFFFCu) | (baseStreamId & 0x3u);
}

inline bool host_should_accept_resume(const HostResumeInputs& in) {
  if (!in.negotiated) return false;
  if (!in.sessionActive) return false;
  if (in.servingControl) return false;
  return true;
}

/**
 * Answering the same ask twice, on purpose. (item 8, C3)
 *
 * host_should_accept_resume above refuses while the dispatcher is inside Serve(), and that is
 * right for a NEW request: re-keying a channel that is working is the one thing this must never
 * do. But it leaves a hole that only shows up when the answer is the thing that gets lost. The
 * host serves the resume, sends its Ack, and goes straight back into Serve(); the Ack does not
 * arrive; the viewer asks again -- and now the dispatcher IS serving, so every retry is refused
 * for the rest of the 30 s ceiling. The repair works and the viewer never hears about it.
 *
 * So the host keeps the answer it gave and, for the same ask, gives it again. Not a second
 * resume: no re-key, no mailbox, no deadline moved, nothing in the session touched. The bytes
 * that were already true are simply repeated, which is what makes it safe to do while serving.
 *
 * "The same ask" is checked in full rather than by resumeId alone, because a resumeId is a
 * correlator and not a credential: it is chosen by the client, echoed in the clear, and anyone
 * who can see one can repeat it. What actually bounds this is that every other field has to
 * match too -- the bound endpoint, the session generation, and the request content -- AND that
 * the channel must still be carrying the result the cached answer describes. Once the session
 * has moved on, the cached answer describes something that no longer exists and is refused by
 * the stream ids alone.
 */
struct HostResumeAck {
  bool valid = false;
  uint64_t epoch = 0;            // the control-ready epoch that was being served
  uint32_t peerIpNet = 0;        // the endpoint the session is bound to (network order)
  uint16_t peerPortNet = 0;
  uint32_t resumeId = 0;         // the ask this answers
  uint32_t requestStreamId = 0;  // ...and the rest of that ask: its own tx stream
  uint32_t txStreamId = 0;       // what the channel was re-keyed to, host->client
  uint32_t rxStreamId = 0;       // ...and client->host
  uint32_t ackStreamId = 0;      // the answer's own streamId field, resent unchanged
};

struct HostResumeReplayInputs {
  bool negotiated = false;
  bool sessionActive = false;  // from the bound peer, and a session exists to resume onto
  uint64_t epoch = 0;
  uint32_t peerIpNet = 0;
  uint16_t peerPortNet = 0;
  uint32_t resumeId = 0;
  uint32_t requestStreamId = 0;
  // What the channel holds RIGHT NOW. Not what it was told to hold: the question is whether the
  // result the cached answer promised is still the one in force.
  uint32_t channelTxStreamId = 0;
  uint32_t channelRxStreamId = 0;
};

inline bool host_should_replay_resume_ack(const HostResumeAck& cached,
                                          const HostResumeReplayInputs& in) {
  if (!cached.valid) return false;
  if (!in.negotiated) return false;
  if (!in.sessionActive) return false;
  if (cached.epoch != in.epoch) return false;
  if (cached.peerIpNet != in.peerIpNet) return false;
  if (cached.peerPortNet != in.peerPortNet) return false;
  if (cached.resumeId != in.resumeId) return false;
  if (cached.requestStreamId != in.requestStreamId) return false;
  if (cached.txStreamId != in.channelTxStreamId) return false;
  if (cached.rxStreamId != in.channelRxStreamId) return false;
  return true;
}

/**
 * How often the same answer may be repeated. (item 8, C3)
 *
 * A token bucket in thousandths, so a rate of four per second is exact rather than a division
 * that rounds to nothing at small intervals. The burst is what makes a real recovery work --
 * two answers can leave back to back, which covers the ordinary case of one being lost -- and
 * the rate is what stops a repeated request from turning into a repeated send.
 *
 * There are two of these on the host and they answer different questions. The per-session one
 * bounds how hard one client can make the host repeat itself. The global one bounds the host: a
 * client that invents a new resumeId every time never touches the per-session bucket, because
 * that bucket belongs to the cached id, so the only thing standing between "new id each time"
 * and an unbounded send rate is a budget that does not care which id it was.
 */
class ResumeAckBudget {
 public:
  ResumeAckBudget() = default;
  ResumeAckBudget(uint32_t perSecond, uint32_t burst)
      : perSecond_(perSecond), burst_(burst), tokensMilli_(static_cast<uint64_t>(burst) * 1000) {}

  /** True when one send is allowed now, and takes it. */
  bool Take(uint64_t nowUs) {
    if (!started_) {
      started_ = true;
      lastUs_ = nowUs;
    } else if (nowUs > lastUs_) {
      // us * per-second / 1e6 tokens, expressed in thousandths -- hence / 1000.
      tokensMilli_ += (nowUs - lastUs_) * perSecond_ / 1000;
      const uint64_t cap = static_cast<uint64_t>(burst_) * 1000;
      if (tokensMilli_ > cap) tokensMilli_ = cap;
      lastUs_ = nowUs;
    }
    if (tokensMilli_ < 1000) return false;
    tokensMilli_ -= 1000;
    return true;
  }

  /** A different ask: the budget belongs to the id, so it starts again with the id. */
  void Restart(uint64_t nowUs) {
    started_ = true;
    lastUs_ = nowUs;
    tokensMilli_ = static_cast<uint64_t>(burst_) * 1000;
  }

 private:
  uint32_t perSecond_ = 4;
  uint32_t burst_ = 2;
  uint64_t tokensMilli_ = 2000;
  uint64_t lastUs_ = 0;
  bool started_ = false;
};

/**
 * What to do with a resume that arrives while the dispatcher is serving. (item 8, C3 r2)
 *
 * host_should_accept_resume refuses every one of them, and that was right as far as it went:
 * re-keying a channel that is in use is the one thing this must never do. What it could not
 * see is that the dispatcher stays inside Serve() until its own ten second read timeout, and
 * the moment it leaves is the moment the host stops sending video -- so by the time a resume
 * could be accepted, the viewer has usually stopped asking, because a session with no picture
 * is one it is told not to repair. Three rules that are each correct, closing the window
 * between them. Measured: three repairs in five runs, and the other two never happened.
 *
 * So a resume may now interrupt a serve, on three conditions together -- and the third is the
 * one that matters:
 *
 *   verified   -- everything the old path already required (negotiated, this session, the
 *                 bound endpoint, the request content, the stream ids in force).
 *   new        -- a resumeId this host has not already answered. A repeat of an answered ask
 *                 is the idempotent replay and changes nothing.
 *   stopped    -- POSITIVE evidence that the existing channel has stopped: either it declared
 *                 peer-lost on its own, or a round-trip probe over it went unanswered.
 *
 * "Stopped" is deliberately not "nothing has arrived for a while". An idle session is silent
 * and healthy, and a one-way failure leaves the host hearing the client perfectly while
 * nothing it sends gets back -- so a silence test would both kill healthy sessions and miss
 * the failure it exists to catch. The probe asks instead, and a channel that answers is a
 * channel that keeps its session: the ask is refused and nothing is remembered.
 */
enum class HostResumeVerdict : uint8_t {
  Ignore = 0,  // not this session's: no reply, and nothing kept
  ReplayAck,   // an ask already answered: send that answer again, change nothing
  Serve,       // accept -- re-key and answer
  Probe,       // serving, new ask, no evidence yet: ask the channel; answer served=0 meanwhile
  Refuse,      // serving and the channel answered: served=0, nothing kept
};

inline const char* to_string(HostResumeVerdict v) {
  switch (v) {
    case HostResumeVerdict::ReplayAck: return "replay";
    case HostResumeVerdict::Serve: return "serve";
    case HostResumeVerdict::Probe: return "probe";
    case HostResumeVerdict::Refuse: return "refuse";
    default: return "ignore";
  }
}

struct HostResumeEligibility {
  bool negotiated = false;
  bool sessionActive = false;    // from the bound peer, with a session to resume onto
  bool servingControl = false;   // the dispatcher is inside Serve() right now
  bool cacheMatches = false;     // host_should_replay_resume_ack said yes for this ask
  bool sameIdAsCache = false;    // an answer is remembered, and it is for THIS resumeId
  bool channelPeerLost = false;  // the host's own channel gave up on the peer
  ControlProbeState probe = ControlProbeState::Idle;
};

inline HostResumeVerdict host_resume_verdict(const HostResumeEligibility& in) {
  if (!in.negotiated || !in.sessionActive) return HostResumeVerdict::Ignore;
  // An ask already answered is answered the same way, whatever else is going on.
  if (in.cacheMatches) return HostResumeVerdict::ReplayAck;
  // Nobody is serving: the original rule, unchanged. There is nothing to interrupt.
  if (!in.servingControl) return HostResumeVerdict::Serve;
  // The id is one this host has answered, but the ask no longer matches that answer (the
  // session moved, or the content differs). That is not a new attempt and it is not a repeat.
  if (in.sameIdAsCache) return HostResumeVerdict::Refuse;
  if (in.channelPeerLost) return HostResumeVerdict::Serve;
  if (in.probe == ControlProbeState::Alive) return HostResumeVerdict::Refuse;
  if (in.probe == ControlProbeState::Dead) return HostResumeVerdict::Serve;
  return HostResumeVerdict::Probe;
}

/**
 * Whether an accepted ask should raise its own wake, or join one already in flight.
 * (item 8, C3 r2)
 *
 * The viewer repeats an ask every 500 ms and the dispatcher can take longer than one
 * wait to come back, so the same recovery arrives more than once while it is already
 * being served. Both copies would be honoured and the second would re-key a channel the
 * first had just repaired -- one logical recovery, two repairs, and whatever the first
 * one had started to carry thrown away between them.
 *
 * Written out as a function because "the second copy joins the first" is a rule, and a
 * rule buried in a condition inside a reader loop is one nothing can hold.
 */
inline bool host_resume_should_raise_wake(bool accepted, bool wakePending,
                                          bool pendingIsSameAsk) {
  if (!accepted) return false;
  if (wakePending && pendingIsSameAsk) return false;
  return true;
}

/**
 * The probe budget, fixed by the counter-examples rather than by taste. (item 8, C3 r2)
 *
 * Six asks, a quarter second apart: about a second and a half before a channel is called
 * stopped. The floor is set by the healthy cases -- an idle session must answer the FIRST
 * probe, so the interval has to exceed a real round trip with room to spare, and 250 ms does
 * on any link this product is usable on. The ceiling is set by the host's own ten second read
 * timeout, which is what this exists to get ahead of.
 *
 * Not to be described as "one second of silence". It is six unanswered round trips; silence on
 * its own is not evidence here and the distinction is the whole point of the rule.
 */
constexpr uint32_t kResumeProbeMaxAttempts = 6;
constexpr uint64_t kResumeProbeIntervalUs = 250000;

/**
 * How long a probe verdict may be believed. (item 8, C3 r3)
 *
 * A probe answers about the instant it ran. r2 asked once per resumeId and re-read that answer
 * for every retry -- so a channel that was alive when the first ask arrived and died a moment
 * later was refused for the entire thirty second ceiling, which is the exact stall this
 * feature exists to remove, reintroduced one layer up. The viewer cannot work around it
 * either: one break is one resumeId, by design.
 *
 * Chosen against the viewer's retry cadence, which is 500 ms. Longer than that, so an
 * ordinary retry reuses the verdict instead of making the host ask again -- the first value
 * tried here was shorter, and the effect was that every single retry started a new probe and
 * the reuse path was never taken at all. Short enough that a channel which has died is
 * re-examined within about a second, and settled within that plus the probe budget: call it
 * two and a half seconds against the ten this replaced.
 */
constexpr uint64_t kResumeProbeFreshUs = 750000;

/** A hard ceiling on probes per recovery, independent of the rate the asks arrive at. */
constexpr uint32_t kResumeProbeMaxPerRecovery = 64;

/** What to do with the probe state in hand when another ask for the SAME recovery arrives. */
enum class ResumeProbeAction : uint8_t {
  Use = 0,  // the verdict stands: act on it
  Restart,  // it is stale (or there is none): ask again
};

struct ResumeProbeFreshness {
  ControlProbeState state = ControlProbeState::Idle;
  // How old the verdict is, asked of whatever holds it. Not a pair of timestamps to subtract:
  // the channel stamps with steady_clock and its callers keep time with QPC, and those have
  // different origins -- a difference that reads as a plausible number on this machine and as
  // nonsense elsewhere.
  uint64_t ageUs = 0;
  uint32_t probesThisRecovery = 0;
  uint64_t freshUs = kResumeProbeFreshUs;
  uint32_t maxProbes = kResumeProbeMaxPerRecovery;
};

inline ResumeProbeAction resume_probe_action(const ResumeProbeFreshness& in) {
  // A question still in flight is not re-asked; that is what the attempt budget is for.
  if (in.state == ControlProbeState::Pending) return ResumeProbeAction::Use;
  // Dead is acted on immediately -- the ask that reads it is served -- so its age never
  // matters, and re-asking would only delay a repair that has already been justified.
  if (in.state == ControlProbeState::Dead) return ResumeProbeAction::Use;
  if (in.probesThisRecovery >= in.maxProbes) return ResumeProbeAction::Use;
  if (in.state == ControlProbeState::Idle) return ResumeProbeAction::Restart;
  // Alive, and the only question worth asking about it is how long ago.
  return in.ageUs >= in.freshUs ? ResumeProbeAction::Restart : ResumeProbeAction::Use;
}

// Per session and resumeId: the initial values Codex set for the repeat.
constexpr uint32_t kResumeAckReplayPerSecond = 4;
constexpr uint32_t kResumeAckReplayBurst = 2;
// Per host, across every resume answer it sends for any id. Wide enough that an ordinary
// recovery never reaches it and narrow enough that a flood of invented ids cannot turn the host
// into a generator.
constexpr uint32_t kResumeAckGlobalPerSecond = 32;
constexpr uint32_t kResumeAckGlobalBurst = 16;

/**
 * What the host's control dispatcher should do when something wakes it. (item 8)
 *
 * It used to have one reason to wake -- a new epoch -- and the loop read as such. With resume there
 * are two, and they are not equal: a moving epoch means a DIFFERENT client arrived, and serving the
 * previous client's resume then would hand it the new client's session. So the epoch wins, and the
 * resume is dropped rather than queued behind it.
 *
 * Pulled out as a function because this is the whole of the new control flow, and the alternative
 * is a predicate inside a lambda inside a thread inside a startup routine, where the only way to
 * ask what it does with a stale resume is to run a host.
 */
struct ControlDispatchInputs {
  bool stop = false;
  uint64_t epoch = 0;
  uint64_t servedEpoch = 0;
  uint64_t resumeSeq = 0;
  uint64_t servedResumeSeq = 0;
};

struct ControlDispatchDecision {
  bool wake = false;      // there is something to do (or the host is stopping)
  bool resuming = false;  // ...and it is a resume of the session already being served
};

inline ControlDispatchDecision control_dispatch_decide(const ControlDispatchInputs& in) {
  ControlDispatchDecision out;
  out.wake = in.stop || in.epoch > in.servedEpoch || in.resumeSeq > in.servedResumeSeq;
  out.resuming = out.wake && !in.stop && in.epoch <= in.servedEpoch;
  return out;
}

/** Whether a client's Hello asked for resume. The host advertises it either way. */
inline bool host_resume_negotiated(uint32_t helloFeatures) {
  return (helloFeatures & kUdpFeatureControlResume) != 0;
}

}  // namespace remote60::native_poc

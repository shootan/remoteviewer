#pragma once

// The viewer's side of a control-channel resume: one recovery, driven from one place. (item 8, C3)
//
// Role:    decide when to ask, validate what comes back, and -- once and only once per break --
//          re-key the control channel onto the stream ids the host agreed to.
// Thread:  two, deliberately. The ingress thread calls OnDatagram and does nothing but VALIDATE
//          and record; the control worker calls Poll / ApplyPendingRekey and is the only thing
//          that ever touches the channel. One mutex covers both, so the re-key, the clearing of
//          the closed flag and this object's own state change together or not at all.
// Input:   the negotiated capability, whether video is still arriving, the clock, datagrams.
// Output:  ControlResume datagrams on the media socket; a re-keyed channel; log lines.
// Callers: ControlClient::Run (worker) and the UDP ingress callback (viewer_video_receiver.cpp).
//
// Why it is a component rather than a few lines in the worker loop. The failure this repairs is
// the one T3 left behind: control dies, video keeps arriving, and for thirty seconds the viewer
// shows a live picture that answers nothing. Repairing it means re-keying a channel two other
// threads are using, at a moment when a session teardown may also be in flight, and the way that
// goes wrong is not subtle -- a second worker, or a socket reused after close. So the rule is
// that recovery happens in the worker, once, and everything else only hands it verified facts.
//
// What it deliberately does NOT do:
//   - It does not restart the control thread. The worker stays alive and changes state; a thread
//     that exits and is started again is a race with session teardown for no gain.
//   - It does not resend anything the session had in flight. See ViewerPendingControlRequest.
//   - It does not believe an Ack. An Ack says the host re-keyed; only a round trip on the new
//     channel says the two ends agree, and that is what ends the recovery.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>

#include "control_resume.hpp"
#include "poc_protocol.hpp"
#include "udp_control_channel.hpp"

namespace remote60::native_poc {

/**
 * Where the control worker is. (item 8, C3)
 *
 * Running is the ordinary loop. Resuming is the same thread, same loop, not serving requests and
 * trying to get the channel back. Closed is the end of the session -- reached from either, and
 * from Resuming it means the ceiling passed or the user/shell called the session off.
 */
enum class ControlWorkerState : uint8_t {
  Running = 0,
  Resuming,
  Closed,
};

inline const char* to_string(ControlWorkerState s) {
  switch (s) {
    case ControlWorkerState::Resuming: return "resuming";
    case ControlWorkerState::Closed: return "closed";
    default: return "running";
  }
}

/** What an arriving ControlResumeAck turns out to be. */
enum class ViewerResumeAckVerdict : uint8_t {
  Rekey = 0,       // valid, served, and this recovery has not been applied yet
  AlreadyApplied,  // the same id again after the re-key: idempotent, ignored
  Refused,         // served=0 -- the host is busy; the retry timer handles it
  Ignore,          // does not belong to this session / this attempt / this protocol
};

struct ViewerResumeAckCheck {
  ViewerResumeAckVerdict verdict = ViewerResumeAckVerdict::Ignore;
  const char* why = "";
};

/** What the viewer will accept an answer against. A snapshot; the caller holds the lock. */
struct ViewerResumeExpectation {
  bool negotiated = false;         // the host advertised kUdpFeatureControlResume
  bool sessionActive = false;      // there is a session -- not cancelled, not superseded
  bool attemptInFlight = false;    // a recovery is running
  bool rekeyedForThisId = false;   // ...and its re-key has already been done
  uint64_t sessionGeneration = 0;  // the viewer's connect generation when the attempt began
  uint32_t peerIpNet = 0;
  uint16_t peerPortNet = 0;
  uint32_t baseRxStreamId = 0;  // the viewer's inbound base stream (host->client)
  uint32_t resumeId = 0;        // the attempt in flight
};

/** What arrived, as fields rather than bytes, so every rejection can be exercised on its own. */
struct ViewerResumeAckFacts {
  size_t datagramLen = 0;
  uint32_t magic = 0;
  uint16_t kind = 0;
  uint16_t size = 0;
  uint32_t streamId = 0;
  uint32_t resumeId = 0;
  uint32_t accepted = 0;
  uint32_t peerIpNet = 0;
  uint16_t peerPortNet = 0;
  uint64_t sessionGeneration = 0;
};

/**
 * Whether this answer may re-key the viewer's control channel.
 *
 * Every clause here is a way in for something that is not the host's answer to the question this
 * viewer asked, and they are separate on purpose: a check that only compared resumeId would let a
 * datagram from an abandoned attempt, a superseded session, or a replayed capture do the one
 * thing this component can do.
 */
inline ViewerResumeAckCheck viewer_check_resume_ack(const ViewerResumeExpectation& expect,
                                                    const ViewerResumeAckFacts& in) {
  // Exactly the length, not at least: a longer datagram carrying these bytes at the front is
  // something else, and treating it as an Ack would accept whatever the rest of it is for.
  if (in.datagramLen != sizeof(UdpControlResumePacket)) return {ViewerResumeAckVerdict::Ignore, "length"};
  if (in.magic != kMagic) return {ViewerResumeAckVerdict::Ignore, "magic"};
  // The answer, not the question. A request reflected back -- by a middlebox, by a loop, by
  // anything -- carries our own resumeId and would otherwise look like agreement.
  if (in.kind != static_cast<uint16_t>(UdpPacketKind::ControlResumeAck))
    return {ViewerResumeAckVerdict::Ignore, "kind"};
  if (in.size != sizeof(UdpControlResumePacket)) return {ViewerResumeAckVerdict::Ignore, "size"};
  if (!expect.negotiated) return {ViewerResumeAckVerdict::Ignore, "not-negotiated"};
  if (!expect.sessionActive) return {ViewerResumeAckVerdict::Ignore, "no-session"};
  if (in.peerIpNet != expect.peerIpNet || in.peerPortNet != expect.peerPortNet)
    return {ViewerResumeAckVerdict::Ignore, "peer"};
  // The session this attempt was made for. The viewer's generation moves when the shell calls it
  // off and starts again, and an answer that crosses that boundary belongs to a session that no
  // longer exists.
  if (in.sessionGeneration != expect.sessionGeneration)
    return {ViewerResumeAckVerdict::Ignore, "generation"};
  if (!expect.attemptInFlight) return {ViewerResumeAckVerdict::Ignore, "no-attempt"};
  if (in.resumeId != expect.resumeId) return {ViewerResumeAckVerdict::Ignore, "resume-id"};
  if (in.accepted == 0) return {ViewerResumeAckVerdict::Refused, "served=0"};
  // Ties the answer to this viewer's own base stream as well as to the id. The host derives it
  // the same way; an answer that does not is not answering about this channel.
  if (in.streamId != control_resume_stream_id(expect.baseRxStreamId, expect.resumeId))
    return {ViewerResumeAckVerdict::Ignore, "stream-id"};
  // The host repeats its answer when it thinks the first was lost, so duplicates are normal and
  // must cost nothing. Re-keying twice would restart the sequence numbers under a channel that
  // has already started using them.
  if (expect.rekeyedForThisId) return {ViewerResumeAckVerdict::AlreadyApplied, "duplicate"};
  return {ViewerResumeAckVerdict::Rekey, "ok"};
}

/**
 * One logical recovery: one break, one resumeId, however many datagrams it takes. (item 8, C3)
 *
 * The id identifies the RECOVERY and not the attempt. Every retry inside a break carries the same
 * one, which is what lets the host recognise a repeat and answer it again instead of refusing it
 * while serving. The next break gets a new one -- and that is true of a break that follows a
 * SUCCESSFUL recovery just as much as one that follows a give-up, because reusing the id would
 * let the host's cached answer, which describes the previous repair, satisfy the next request.
 */
struct ViewerResumeEpisode {
  bool active = false;
  bool rekeyed = false;      // ResumeWith has been done for this id
  uint32_t resumeId = 0;
  uint32_t attempts = 0;
  uint64_t generation = 0;   // the viewer session this recovery belongs to
  uint64_t beganUs = 0;      // when control was declared dead, not when the first ask went out
  uint64_t firstSendUs = 0;  // when the first ask went out (reported separately, on purpose)
  uint64_t lastAttemptUs = 0;
};

/** Begins a recovery if one is not already running. Returns the id to use either way. */
inline uint32_t viewer_resume_begin(ViewerResumeEpisode* ep, uint32_t freshId, uint64_t nowUs) {
  if (!ep) return 0;
  if (ep->active) return ep->resumeId;  // same break, same id
  ep->active = true;
  ep->rekeyed = false;
  ep->resumeId = freshId;
  ep->attempts = 0;
  ep->beganUs = nowUs;
  ep->firstSendUs = 0;
  ep->lastAttemptUs = 0;
  return ep->resumeId;
}

/** Ends it, however it ended. The next break begins a new one with a new id. */
inline void viewer_resume_end(ViewerResumeEpisode* ep) {
  if (!ep) return;
  ep->active = false;
  ep->rekeyed = false;
}

/**
 * A control request that was outstanding when the channel broke. (item 8, C3, Codex Q2)
 *
 * It is not resent. The host's mailbox preserving the request is not the same as its reply being
 * redelivered -- a reply already written to the old stream is simply gone -- and a request like a
 * capture-mode change or a window selection is not idempotent, so a viewer that resent one could
 * turn one user action into two. The rule is therefore: fail it to whoever asked, never resend
 * it, and discard any answer that turns up afterwards.
 *
 * The discard is not left to good intentions: the re-key gives the resumed channel new stream
 * ids, so a reply to the old request is dropped by UdpControlChannel::OnPacket before it can be
 * mistaken for an answer to whatever is asked next.
 */
struct ViewerPendingControlRequest {
  bool active = false;
  uint8_t kind = 0;      // ControlOutboundActionKind of the action that was in flight
  uint32_t requestId = 0;
  uint64_t startedUs = 0;
};

/** Never. Stated as a function so that "we do not resend" is a thing a test can hold. */
inline bool viewer_resume_may_resend(const ViewerPendingControlRequest& /* pending */) {
  return false;
}

/** A reply is for the current request only; anything else is late and goes in the bin. */
inline bool viewer_resume_reply_is_late(const ViewerPendingControlRequest& pending,
                                        uint32_t replyRequestId) {
  if (!pending.active) return true;
  return pending.requestId != replyRequestId;
}

/** How a turn of the recovery ended. */
enum class ResumePumpResult : uint8_t {
  Continue = 0,  // still trying
  Resumed,       // the channel is back and has been proved
  GaveUp,        // the ceiling passed
  Cancelled,     // the session ended underneath it
};

/** The parts of a recovery that differ between the viewer and a harness driving it. */
struct ResumePumpHooks {
  std::function<bool()> cancelled;   // the session is over: nothing else matters
  std::function<bool()> videoAlive;  // the picture is why this session is worth repairing
  std::function<uint64_t()> nowUs;
  // Rebuild the link over the re-keyed channel and complete one real exchange on it.
  std::function<bool()> proveChannel;
  // (breakToFirstSendUs, breakToRunningUs) -- the UI, the release-all, the log line.
  std::function<void(uint64_t, uint64_t)> onResumed;
  std::function<void()> idle;  // nothing to do this turn
};

/**
 * The recovery itself.
 *
 * Owned by the control worker. Configure() is called once per session; after that the worker
 * calls BeginBreak / ApplyPendingRekey / Poll / Finish, and the ingress thread calls OnDatagram.
 */
class ViewerControlResume {
 public:
  using SendRaw = std::function<bool(const void*, size_t)>;
  using Log = std::function<void(const std::string&)>;
  using FreshId = std::function<uint32_t()>;
  // The session's generation RIGHT NOW. Read on every answer rather than captured once: the
  // point of the check is that the shell may have called this viewer off and started another
  // one while a recovery was in flight, and a value frozen at configure time could never say so.
  using GenerationNow = std::function<uint64_t()>;

  struct Config {
    ControlResumeConfig decide;  // 500 ms between asks, 30 s ceiling -- T3's, unchanged
    uint32_t baseTxStreamId = kUdpControlStreamClientToHost;
    uint32_t baseRxStreamId = kUdpControlStreamHostToClient;
  };

  void Configure(UdpControlChannel* channel, SendRaw sendRaw, Log log, FreshId freshId,
                 GenerationNow generationNow, Config config, bool negotiated, uint32_t peerIpNet,
                 uint16_t peerPortNet) {
    std::lock_guard<std::mutex> lock(mu_);
    channel_ = channel;
    sendRaw_ = std::move(sendRaw);
    log_ = std::move(log);
    freshId_ = std::move(freshId);
    generationNow_ = std::move(generationNow);
    config_ = config;
    negotiated_ = negotiated;
    peerIpNet_ = peerIpNet;
    peerPortNet_ = peerPortNet;
    sessionActive_ = true;
    episode_ = ViewerResumeEpisode{};
    pendingServedId_ = 0;
    havePendingServed_ = false;
  }

  bool negotiated() const {
    std::lock_guard<std::mutex> lock(mu_);
    return negotiated_;
  }

  /**
   * The session is over (cancelled, superseded, shutting down). Nothing resumes after this.
   *
   * It clears ONE thing, and that is deliberate. Tidying the episode and the pending answer
   * here as well looked safer and was worse: every later guard then had two reasons to refuse,
   * so no test could show that the session flag was the one doing the work -- and a flag no
   * test can hold is a flag that can quietly stop working. Nothing reads the leftover state
   * without checking this flag first.
   */
  void EndSession() {
    std::lock_guard<std::mutex> lock(mu_);
    sessionActive_ = false;
  }

  /**
   * Control is dead and video is still arriving: a recovery starts here.
   *
   * beganUs is when the worker saw the channel fail, which is at or before the moment the
   * watchdog notices. That matters: the ceiling is measured from here, so this recovery gives up
   * no later than T3 would have ended the session, and the thirty second budget is not extended.
   */
  void BeginBreak(uint64_t nowUs) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!sessionActive_ || !negotiated_) return;
    const bool wasActive = episode_.active;
    const uint32_t id = viewer_resume_begin(&episode_, freshId_ ? freshId_() : 0, nowUs);
    // The session this recovery belongs to. An answer that arrives after the shell has replaced
    // this viewer is answering about a session that no longer exists.
    if (!wasActive) episode_.generation = generationNow_ ? generationNow_() : 0;
    if (!wasActive && log_) {
      log_("[control-resume] break resumeId=" + std::to_string(id));
    }
  }

  /**
   * The re-key happened but the round trip did not: start over with a NEW id, same break.
   *
   * The id cannot be reused. The host has cached its answer for it and will happily repeat that
   * answer, which describes a repair this viewer has already applied and which demonstrably did
   * not produce a working channel; asking again under the same id would get the same paper
   * agreement forever. beganUs is kept, so the ceiling still runs from when control actually
   * died rather than restarting with each attempt.
   */
  void RetryWithNewId(uint64_t nowUs) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!sessionActive_ || !episode_.active) return;
    const uint64_t beganUs = episode_.beganUs;
    const uint64_t generation = episode_.generation;
    const uint64_t firstSendUs = episode_.firstSendUs;
    const uint32_t attempts = episode_.attempts;
    const uint32_t previous = episode_.resumeId;
    viewer_resume_end(&episode_);
    viewer_resume_begin(&episode_, freshId_ ? freshId_() : 0, beganUs);
    episode_.generation = generation;
    episode_.beganUs = beganUs;
    episode_.firstSendUs = firstSendUs;
    episode_.attempts = attempts;
    episode_.lastAttemptUs = nowUs;  // the retry interval still applies
    havePendingServed_ = false;
    if (log_) {
      log_("[control-resume] round trip failed after rekey, new resumeId=" +
           std::to_string(episode_.resumeId) + " was=" + std::to_string(previous));
    }
  }

  /**
   * The trigger, next to the recovery it starts. (item 8, C3)
   *
   * Three conditions, and each of them is a different "no": an old host has nothing to answer
   * with, a cancelled session must not be repaired, and a session whose picture has stopped is
   * ending rather than broken. Kept here rather than in the caller so that a harness driving this
   * end to end uses the same three and not three that look like them.
   */
  bool BeginBreakIfPossible(uint64_t nowUs, bool videoAlive, bool cancelled) {
    if (cancelled) return false;
    if (!videoAlive) return false;
    BeginBreak(nowUs);
    return attempt_in_flight();
  }

  /** The recovery ended -- confirmed by a round trip, or given up on. */
  void Finish(bool resumed) {
    std::lock_guard<std::mutex> lock(mu_);
    if (log_ && episode_.active) {
      log_(std::string("[control-resume] ") + (resumed ? "resumed" : "gave-up") +
           " resumeId=" + std::to_string(episode_.resumeId) +
           " attempts=" + std::to_string(episode_.attempts));
    }
    // Same rule as EndSession: the episode being over is the one authority, so that the
    // guard which refuses a stale answer is a guard a test can actually reach. A verified
    // answer left behind here is refused by the next recovery anyway -- its id will not be
    // the one in flight.
    viewer_resume_end(&episode_);
  }

  /**
   * Ingress thread. Validates a datagram and records the verified outcome; never acts on it.
   *
   * Returns true when the datagram was a resume answer -- consumed either way, because an answer
   * that fails validation is still not something the rest of the receive path should see.
   */
  bool OnDatagram(const void* data, size_t len) {
    if (!data || len < sizeof(UdpControlResumePacket)) return false;
    UdpControlResumePacket packet{};
    std::memcpy(&packet, data, sizeof(packet));
    if (packet.magic != kMagic) return false;
    if (packet.kind != static_cast<uint16_t>(UdpPacketKind::ControlResumeAck)) return false;

    ViewerResumeAckFacts facts;
    facts.datagramLen = len;
    facts.magic = packet.magic;
    facts.kind = packet.kind;
    facts.size = packet.size;
    facts.streamId = packet.streamId;
    facts.resumeId = packet.resumeId;
    facts.accepted = packet.accepted;

    std::lock_guard<std::mutex> lock(mu_);
    // The media socket is connected, so the kernel has already refused anything from another
    // endpoint; these are carried through so the rule is checked rather than assumed, and so it
    // survives a path that ever reads the socket unconnected.
    facts.peerIpNet = peerIpNet_;
    facts.peerPortNet = peerPortNet_;
    facts.sessionGeneration = generationNow_ ? generationNow_() : 0;

    const ViewerResumeAckCheck check = viewer_check_resume_ack(Expectation(), facts);
    switch (check.verdict) {
      case ViewerResumeAckVerdict::Rekey:
        havePendingServed_ = true;
        pendingServedId_ = facts.resumeId;
        break;
      case ViewerResumeAckVerdict::Refused:
        ++refusedAcks_;
        break;
      case ViewerResumeAckVerdict::AlreadyApplied:
        ++duplicateAcks_;
        break;
      default:
        ++ignoredAcks_;
        break;
    }
    lastVerdict_ = check;
    return true;
  }

  /**
   * Worker thread. Applies a verified answer: the ONE place the viewer re-keys its channel.
   *
   * The re-key, the clearing of the channel's closed flag (ResumeWith does both) and this
   * object's own "already applied" state all happen under this lock, so a duplicate answer
   * arriving at the same moment cannot find a half-changed state to act on.
   */
  bool ApplyPendingRekey() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!sessionActive_ || !havePendingServed_) return false;
    if (!episode_.active || episode_.rekeyed) {
      havePendingServed_ = false;
      return false;
    }
    if (pendingServedId_ != episode_.resumeId) {
      havePendingServed_ = false;
      return false;
    }
    if (!channel_) return false;
    if (!channel_->ResumeWith(control_resume_stream_id(config_.baseTxStreamId, episode_.resumeId),
                              control_resume_stream_id(config_.baseRxStreamId, episode_.resumeId))) {
      // The viewer is shutting its channel down; there is nothing left to repair. (C5)
      havePendingServed_ = false;
      if (log_) log_("[control-resume] ack served=1 but the channel is shut down; not re-keyed");
      return false;
    }
    episode_.rekeyed = true;
    havePendingServed_ = false;
    if (log_) {
      log_("[control-resume] ack served=1 resumeId=" + std::to_string(episode_.resumeId) +
           " rekeyed=1");
    }
    return true;
  }

  /**
   * Worker thread. One turn of the decision, and the ask if it is due.
   *
   * videoAlive is the same fact T3 uses to keep the session: a resume is asked for exactly while
   * the picture is the reason the session is still here.
   */
  ControlResumeAction Poll(bool videoAlive, uint64_t nowUs) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!sessionActive_) return ControlResumeAction::Idle;
    ControlResumeInputs in;
    in.negotiated = negotiated_;
    in.controlDead = episode_.active;
    in.videoAlive = videoAlive;
    in.controlDeadForUs = episode_.active && nowUs >= episode_.beganUs ? nowUs - episode_.beganUs : 0;
    in.sinceLastAttemptUs =
        episode_.lastAttemptUs == 0 ? config_.decide.retryIntervalUs * 2
                                    : (nowUs >= episode_.lastAttemptUs ? nowUs - episode_.lastAttemptUs : 0);
    const ControlResumeAction action = control_resume_decide(config_.decide, in);
    if (action != ControlResumeAction::Send) return action;

    UdpControlResumePacket ask{};
    ask.kind = static_cast<uint16_t>(UdpPacketKind::ControlResume);
    ask.streamId = config_.baseTxStreamId;
    ask.resumeId = episode_.resumeId;
    ask.accepted = 0;
    const bool sent = sendRaw_ && sendRaw_(&ask, sizeof(ask));
    episode_.lastAttemptUs = nowUs;
    ++episode_.attempts;
    if (episode_.firstSendUs == 0) episode_.firstSendUs = nowUs;
    if (log_) {
      log_("[control-resume] send resumeId=" + std::to_string(episode_.resumeId) +
           " attempt=" + std::to_string(episode_.attempts) + " ok=" + (sent ? "1" : "0"));
    }
    return action;
  }

  /**
   * One turn of a recovery, in order, in one place. (item 8, C3)
   *
   * The sequence -- cancellation first, then apply a verified answer, then prove the new
   * channel, then decide whether to ask again -- is the feature. Leaving it written out
   * in the viewer's worker would mean the only way to exercise it end to end was for a
   * test to write the same sequence again beside it, and a test that reimplements the
   * thing it is checking proves that the test works.
   *
   * So the order lives here and the callers supply the parts that differ: what counts as
   * cancelled, what counts as a live picture, how to prove a channel, and what to do when
   * it comes back. No lock is held across a hook -- proveChannel talks to the network.
   */
  ResumePumpResult Pump(const ResumePumpHooks& hooks) {
    // Cancellation and session end come first, every turn. A recovery must never outlive
    // the session it is recovering.
    if (hooks.cancelled && hooks.cancelled()) {
      Finish(false);
      return ResumePumpResult::Cancelled;
    }
    const uint64_t nowUs = hooks.nowUs ? hooks.nowUs() : 0;

    if (ApplyPendingRekey()) {
      // An Ack is the host's claim about its own end. This is the part that makes it a
      // fact: one real exchange over the ids both sides just changed to.
      if (hooks.proveChannel && hooks.proveChannel()) {
        const uint64_t doneUs = hooks.nowUs ? hooks.nowUs() : nowUs;
        const uint64_t beganUs = began_us();
        const uint64_t firstSendUs = first_send_us();
        if (hooks.onResumed) {
          // Two numbers, because one hides the other: how long until the first ask went
          // out, and how long the whole recovery took.
          hooks.onResumed(firstSendUs > beganUs ? firstSendUs - beganUs : 0,
                          doneUs > beganUs ? doneUs - beganUs : 0);
        }
        Finish(true);
        return ResumePumpResult::Resumed;
      }
      // Re-keyed and still deaf. The id is spent -- the host has cached its answer for it
      // and would repeat that answer forever -- so the recovery carries on under a new
      // one, with the ceiling still running from the original break.
      RetryWithNewId(hooks.nowUs ? hooks.nowUs() : nowUs);
    }

    const ControlResumeAction action =
        Poll(hooks.videoAlive ? hooks.videoAlive() : false, nowUs);
    if (action == ControlResumeAction::GiveUp) {
      Finish(false);
      return ResumePumpResult::GaveUp;
    }
    if (action != ControlResumeAction::Send && hooks.idle) hooks.idle();
    return ResumePumpResult::Continue;
  }

  // --- what happened, for the worker's decisions and for the tests' measurements ---
  bool attempt_in_flight() const {
    std::lock_guard<std::mutex> lock(mu_);
    return episode_.active;
  }
  bool rekeyed() const {
    std::lock_guard<std::mutex> lock(mu_);
    return episode_.rekeyed;
  }
  uint32_t resume_id() const {
    std::lock_guard<std::mutex> lock(mu_);
    return episode_.resumeId;
  }
  uint32_t attempts() const {
    std::lock_guard<std::mutex> lock(mu_);
    return episode_.attempts;
  }
  uint64_t began_us() const {
    std::lock_guard<std::mutex> lock(mu_);
    return episode_.beganUs;
  }
  uint64_t first_send_us() const {
    std::lock_guard<std::mutex> lock(mu_);
    return episode_.firstSendUs;
  }
  uint64_t duplicate_acks() const {
    std::lock_guard<std::mutex> lock(mu_);
    return duplicateAcks_;
  }
  uint64_t refused_acks() const {
    std::lock_guard<std::mutex> lock(mu_);
    return refusedAcks_;
  }
  uint64_t ignored_acks() const {
    std::lock_guard<std::mutex> lock(mu_);
    return ignoredAcks_;
  }
  const char* last_reason() const {
    std::lock_guard<std::mutex> lock(mu_);
    return lastVerdict_.why;
  }

 private:
  ViewerResumeExpectation Expectation() const {  // caller holds mu_
    ViewerResumeExpectation e;
    e.negotiated = negotiated_;
    e.sessionActive = sessionActive_;
    e.attemptInFlight = episode_.active;
    e.rekeyedForThisId = episode_.rekeyed;
    e.sessionGeneration = episode_.generation;
    e.peerIpNet = peerIpNet_;
    e.peerPortNet = peerPortNet_;
    e.baseRxStreamId = config_.baseRxStreamId;
    e.resumeId = episode_.resumeId;
    return e;
  }

  mutable std::mutex mu_;
  UdpControlChannel* channel_ = nullptr;
  SendRaw sendRaw_;
  Log log_;
  FreshId freshId_;
  GenerationNow generationNow_;
  Config config_;
  bool negotiated_ = false;
  bool sessionActive_ = false;
  uint32_t peerIpNet_ = 0;
  uint16_t peerPortNet_ = 0;
  ViewerResumeEpisode episode_;
  bool havePendingServed_ = false;
  uint32_t pendingServedId_ = 0;
  uint64_t duplicateAcks_ = 0;
  uint64_t refusedAcks_ = 0;
  uint64_t ignoredAcks_ = 0;
  ViewerResumeAckCheck lastVerdict_;
};

}  // namespace remote60::native_poc

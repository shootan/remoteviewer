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

}  // namespace remote60::native_poc

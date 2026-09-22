#pragma once

/**
 * Whether the host should answer an arriving punch, as a pure decision.
 *
 * THE PROBLEM. A viewer on the same LAN sends punches to the host's private address; they arrive.
 * The host logs them and sends nothing back. The viewer's PunchAny picks whichever candidate
 * answers first, so the private candidate never wins and the relay -- which answers on a 2500 ms
 * grace -- does. The host's own outbound punches go to the client's PUBLIC tuple, which on that
 * network does not come back to the client.
 *
 * Confirmed from the field log (2026-09-22, connect=58ea9790): punches from a private source
 * reached the host and the host did not answer. Whether the difference between that network and a
 * working one is hairpin NAT is a hypothesis and is not recorded here as fact.
 *
 * THE ANSWER, AND WHY IT IS BOUNDED. Replying to a punch with one punch of the same size is not an
 * amplifier -- one datagram in, one identical datagram out -- but "answer anyone who asks" is
 * still a reflector, and a host that answers forever can be pointed at another host that also
 * answers forever. So the reply is fenced three ways:
 *
 *   1. A WINDOW that only a DIRECTORY event opens: a wake punch from the directory, or a heartbeat
 *      that collected a capability. A punch from a client neither opens the window nor extends it,
 *      which is what stops an endless stream of punches from rearming the host indefinitely.
 *   2. BUDGETS inside that window: per source, per window, and per second.
 *   3. A source map with a hard ceiling, cleared when the window closes, so the map itself cannot
 *      be exhausted by punches from many addresses.
 *
 * Nothing here authenticates anything or touches session state. A punch is an empty packet; the
 * reply is an empty packet. AuthorizePeer's rules are untouched -- a Hello that arrives from a
 * private source is accepted or refused exactly as it was before.
 *
 * The decision is const: `punch_reply_decide` reads the state and answers. Recording a reply, and
 * opening the window, are separate calls. That split is what makes the budget testable without a
 * socket.
 */

#include <cstddef>
#include <cstdint>

namespace remote60::native_poc {

/** How long a directory event keeps the host willing to answer. */
constexpr uint64_t kPunchReplyWindowMs = 10000;
/** Per source, matching the number of punches a client sends in one attempt. */
constexpr uint32_t kPunchReplyPerSource = 25;
/** Across all sources in one window. */
constexpr uint32_t kPunchReplyPerWindow = 200;
/**
 * The least time between two directory refreshes that an ARRIVING PUNCH may cause.
 *
 * A punch is treated as "someone is trying to connect, go ask the directory for their
 * capability", and that is an outbound HTTP request. The flag it sets is cleared when the agent
 * consumes it, so a client punching steadily re-arms it every cycle -- steady punching became
 * steady HTTP, once per consume, for as long as it continued. Showing that a burst of 200 in one
 * cycle causes no extra heartbeat does not answer that: the burst lands inside a single cycle.
 *
 * A wake from the DIRECTORY itself is not throttled: it is rare, and it is the case the
 * interrupt exists for.
 *
 * What distinguishes it is a SOURCE TUPLE FILTER -- the datagram arrived from the address the
 * host observes through -- and that is not authentication. Nothing in the packet is signed or
 * checked against a secret, and a sender that can forge that source address gets the same
 * treatment. Calling it authenticated would claim a property the code does not have; what it
 * actually buys is that an ORDINARY client, which does not know or cannot spoof that tuple,
 * cannot reach the unthrottled path.
 */
constexpr uint64_t kPunchRefreshCooldownMs = 2000;

/** Across all sources in any one second. */
constexpr uint32_t kPunchReplyPerSec = 50;
/**
 * Distinct sources tracked at once. Fixed: no allocation on an arriving-datagram path.
 *
 * Full means full. An entry is never evicted while the window is open, and a source with no
 * entry is refused once every slot is taken -- which is what makes the per-source ceiling a
 * ceiling. The first version evicted the least recently used one, and that quietly turned 25
 * into "25 per residency": a source could spend its 25, be pushed out by 64 others, come back
 * as a fresh entry and spend 25 more. Inside one window the only real limit was the global 200.
 *
 * So the contract is three numbers and one rule:
 *
 *   - at most kPunchReplyPerWindow (200) replies in a window, across everyone
 *   - at most kPunchReplyPerSource (25) replies to any one source in a window
 *   - at most kPunchReplySources (64) sources tracked; once full, a source not already
 *     tracked is refused (reason=budget) rather than displacing one that is
 *
 * The map is cleared when a window opens, so a new window starts with nobody remembered.
 */
constexpr size_t kPunchReplySources = 64;

enum class PunchReplyReason : uint8_t {
  Ok = 0,
  Closed,     // no directory event recently; the host is not expecting anyone
  Budget,     // in the window, but this source or the window or the second is spent
  // The directory itself, 0.0.0.0 or port 0, 224.0.0.0/4, 240.0.0.0/4, or 255.255.255.255.
  // NOT every broadcast: a subnet broadcast such as 192.168.20.255 is an ordinary address
  // here and is answered like any other. See punch_reply_source_reason.
  Source,
  Self,       // our own observed tuple -- answering it would be talking to ourselves
  Malformed,  // wrong length, magic, kind or protocol version
};

inline const char* punch_reply_reason_name(PunchReplyReason reason) {
  switch (reason) {
    case PunchReplyReason::Ok: return "ok";
    case PunchReplyReason::Closed: return "closed";
    case PunchReplyReason::Budget: return "budget";
    case PunchReplyReason::Source: return "src";
    case PunchReplyReason::Self: return "self";
    case PunchReplyReason::Malformed: return "bad";
  }
  return "?";
}

/** An address as it arrives: both fields in network order, compared and never parsed. */
struct PunchReplySource {
  uint32_t ipNetworkOrder = 0;
  uint16_t portNetworkOrder = 0;
};

inline bool punch_reply_same(const PunchReplySource& a, const PunchReplySource& b) {
  return a.ipNetworkOrder == b.ipNetworkOrder && a.portNetworkOrder == b.portNetworkOrder;
}

struct PunchReplySourceEntry {
  uint32_t ipNetworkOrder = 0;
  uint16_t portNetworkOrder = 0;
  uint32_t replies = 0;
  uint64_t lastMs = 0;
  bool used = false;
};

struct PunchReplyState {
  /**
   * Whether a window is open, as its own flag rather than as a sentinel timestamp.
   *
   * The first version used windowOpenedMs == 0 to mean "never opened", which makes a window that
   * opened at time zero indistinguishable from a closed one. In production the clock is unlikely
   * to be exactly zero, and that is precisely the kind of thing that is true until it is not --
   * a test running on a zero-based clock found it immediately.
   */
  bool windowOpen = false;
  uint64_t windowOpenedMs = 0;
  uint32_t repliesInWindow = 0;
  uint64_t secondStartMs = 0;
  uint32_t repliesInSecond = 0;
  PunchReplySourceEntry sources[kPunchReplySources];
  /** Counters for the log; they never gate anything. */
  uint64_t replied = 0;
  uint64_t refused = 0;
  /** Refusals that happened because every source slot was taken. Told apart from an
   *  ordinary budget refusal because they mean something different: not "this peer has had
   *  its share" but "there are more peers than this host is willing to track at once". */
  uint64_t refusedSourcesFull = 0;
};

struct PunchReplyDecision {
  bool reply = false;
  PunchReplyReason reason = PunchReplyReason::Closed;
};

/** True while a directory event is still recent enough to answer under. */
inline bool punch_reply_window_open(const PunchReplyState& state, uint64_t nowMs) {
  if (!state.windowOpen) return false;
  if (nowMs < state.windowOpenedMs) return false;  // clock went backwards: treat as closed
  return nowMs - state.windowOpenedMs < kPunchReplyWindowMs;
}

/** The packet itself, before anything about its source is considered. */
inline bool punch_reply_packet_ok(size_t len, size_t expectedLen, uint32_t magic,
                                  uint32_t expectedMagic, uint16_t kind, uint16_t expectedKind,
                                  uint32_t version, uint32_t expectedVersion) {
  // Exactly the expected length. "At least" would let a longer datagram through, and the reply is
  // built from our own struct -- a mismatch in length is a sender we do not understand.
  return len == expectedLen && magic == expectedMagic && kind == expectedKind &&
         version == expectedVersion;
}

/**
 * Whether an address is one we may answer at all, independent of any budget.
 *
 * `directory` and `self` are compared as given; a zero tuple means "not known", and an unknown
 * self cannot match anything, which is the safe direction: it only ever allows a reply that the
 * budget still has to permit.
 */
inline PunchReplyReason punch_reply_source_reason(const PunchReplySource& source,
                                                  const PunchReplySource& directory,
                                                  const PunchReplySource& self) {
  if (source.ipNetworkOrder == 0 || source.portNetworkOrder == 0) return PunchReplyReason::Source;
  const uint32_t host = (source.ipNetworkOrder >> 24) | ((source.ipNetworkOrder >> 8) & 0xff00u) |
                        ((source.ipNetworkOrder << 8) & 0xff0000u) | (source.ipNetworkOrder << 24);
  // 224.0.0.0/4 multicast, 240.0.0.0/4 reserved, and the all-ones broadcast. A datagram whose
  // source is any of these was not sent by a peer that could receive the answer.
  //
  // That is the whole list, and it is narrower than "broadcast addresses are blocked": a
  // DIRECTED broadcast -- 192.168.20.255, or whatever the local mask makes it -- is not
  // recognisable from the address alone without knowing the mask, so it is not blocked here.
  // The budgets are what bound that case, not this function.
  if ((host & 0xf0000000u) == 0xe0000000u) return PunchReplyReason::Source;
  if ((host & 0xf0000000u) == 0xf0000000u) return PunchReplyReason::Source;
  if (host == 0xffffffffu) return PunchReplyReason::Source;
  if (directory.ipNetworkOrder != 0 && punch_reply_same(source, directory)) {
    return PunchReplyReason::Source;
  }
  // `self` is the PUBLIC tuple the directory last observed, and only that. A punch that
  // arrives from one of this machine's own private addresses is not recognised as self and
  // is answered like any other source -- on a LAN that is the normal case, not an error.
  if (self.ipNetworkOrder != 0 && punch_reply_same(source, self)) return PunchReplyReason::Self;
  return PunchReplyReason::Ok;
}

/**
 * Opens (or re-opens) the window. Only a directory event may call this.
 *
 * Returns true when this call opened a window that was not already open, so the caller can say so
 * once rather than on every wake.
 */
inline bool punch_reply_note_directory_event(PunchReplyState* state, uint64_t nowMs) {
  if (!state) return false;
  const bool wasOpen = punch_reply_window_open(*state, nowMs);
  if (!wasOpen) {
    // A fresh window starts with fresh budgets, and with no memory of who asked last time.
    state->repliesInWindow = 0;
    state->repliesInSecond = 0;
    state->secondStartMs = nowMs;
    for (size_t i = 0; i < kPunchReplySources; ++i) state->sources[i] = PunchReplySourceEntry{};
  }
  state->windowOpen = true;
  state->windowOpenedMs = nowMs;
  return !wasOpen;
}

/** Closes the window and forgets the sources. Used when a run ends, and by tests. */
inline void punch_reply_close(PunchReplyState* state) {
  if (!state) return;
  state->windowOpen = false;
  state->windowOpenedMs = 0;
  state->repliesInWindow = 0;
  state->repliesInSecond = 0;
  state->secondStartMs = 0;
  for (size_t i = 0; i < kPunchReplySources; ++i) state->sources[i] = PunchReplySourceEntry{};
}

/** The entry for a source, or null when it has not been seen in this window. */
inline const PunchReplySourceEntry* punch_reply_find(const PunchReplyState& state,
                                                     const PunchReplySource& source) {
  for (size_t i = 0; i < kPunchReplySources; ++i) {
    const PunchReplySourceEntry& entry = state.sources[i];
    if (entry.used && entry.ipNetworkOrder == source.ipNetworkOrder &&
        entry.portNetworkOrder == source.portNetworkOrder) {
      return &entry;
    }
  }
  return nullptr;
}

/** Whether every source slot is taken. Only meaningful while a window is open. */
inline bool punch_reply_sources_full(const PunchReplyState& state) {
  for (size_t i = 0; i < kPunchReplySources; ++i) {
    if (!state.sources[i].used) return false;
  }
  return true;
}

/**
 * The whole judgement, reading state and changing nothing.
 *
 * Order matters and is the order a reader would want in the log: a malformed packet is not a
 * source problem, a source we may never answer is not a budget problem, and a closed window is
 * not a budget problem either.
 */
inline PunchReplyDecision punch_reply_decide(const PunchReplyState& state,
                                             const PunchReplySource& source,
                                             const PunchReplySource& directory,
                                             const PunchReplySource& self, bool packetOk,
                                             uint64_t nowMs) {
  PunchReplyDecision out;
  if (!packetOk) {
    out.reason = PunchReplyReason::Malformed;
    return out;
  }
  const PunchReplyReason sourceReason = punch_reply_source_reason(source, directory, self);
  if (sourceReason != PunchReplyReason::Ok) {
    out.reason = sourceReason;
    return out;
  }
  if (!punch_reply_window_open(state, nowMs)) {
    out.reason = PunchReplyReason::Closed;
    return out;
  }
  if (state.repliesInWindow >= kPunchReplyPerWindow) {
    out.reason = PunchReplyReason::Budget;
    return out;
  }
  // The per-second ceiling only applies within the second it was counted in.
  // Same reasoning as the window flag: "which second are we in" is answered from the window
  // being open at all, not from a timestamp that could legitimately be zero.
  const bool sameSecond = nowMs >= state.secondStartMs && nowMs - state.secondStartMs < 1000;
  if (sameSecond && state.repliesInSecond >= kPunchReplyPerSec) {
    out.reason = PunchReplyReason::Budget;
    return out;
  }
  const PunchReplySourceEntry* entry = punch_reply_find(state, source);
  if (entry && entry->replies >= kPunchReplyPerSource) {
    out.reason = PunchReplyReason::Budget;
    return out;
  }
  // A source we have not seen needs a slot, and if there is none it is refused. The earlier
  // version reused the least recently used entry, reasoning that a flood of new addresses should
  // not lock out the peer mid-connection -- but eviction resets the count, so the flood could
  // instead be used to RENEW a source's 25. Refusing the newcomer costs a genuine 65th peer its
  // answer inside one ten-second window; letting it in costs the ceiling its meaning.
  if (!entry && punch_reply_sources_full(state)) {
    out.reason = PunchReplyReason::Budget;
    return out;
  }
  out.reply = true;
  out.reason = PunchReplyReason::Ok;
  return out;
}

/** Records a reply that was actually sent. Call only after `decide` allowed it. */
inline void punch_reply_note_reply(PunchReplyState* state, const PunchReplySource& source,
                                   uint64_t nowMs) {
  if (!state) return;
  ++state->replied;
  ++state->repliesInWindow;
  if (nowMs < state->secondStartMs || nowMs - state->secondStartMs >= 1000) {
    state->secondStartMs = nowMs;
    state->repliesInSecond = 0;
  }
  ++state->repliesInSecond;

  for (size_t i = 0; i < kPunchReplySources; ++i) {
    PunchReplySourceEntry& entry = state->sources[i];
    if (entry.used && entry.ipNetworkOrder == source.ipNetworkOrder &&
        entry.portNetworkOrder == source.portNetworkOrder) {
      ++entry.replies;
      entry.lastMs = nowMs;
      return;
    }
  }
  // A free slot, or nothing. `decide` refuses a new source when the map is full, so reaching
  // here with no slot means the caller replied without asking -- record nothing rather than
  // evict somebody, because eviction is exactly what made the per-source ceiling porous.
  for (size_t i = 0; i < kPunchReplySources; ++i) {
    if (!state->sources[i].used) {
      state->sources[i] =
          PunchReplySourceEntry{source.ipNetworkOrder, source.portNetworkOrder, 1, nowMs, true};
      return;
    }
  }
}

/** Records a punch that was not answered, for the counters only. */
inline void punch_reply_note_refusal(PunchReplyState* state) {
  if (state) ++state->refused;
}

/**
 * Records a refusal that happened because the source map was full.
 *
 * Separate from note_refusal so the caller does not have to re-derive why, and separate from the
 * ordinary budget count so that "this host is seeing more than 64 peers in ten seconds" is
 * visible rather than folded into "somebody ran out of replies".
 */
inline void punch_reply_note_sources_full(PunchReplyState* state) {
  if (state) ++state->refusedSourcesFull;
}

}  // namespace remote60::native_poc

#pragma once

// keepalive r1 (Codex .claude/codex_keepalive_r1.md section 2/3): parking a connected session while the
// phone shows its PC list -- the control link stays, the picture stops, nothing reaches the host.
//
// Role:    two pure pieces the shared client session drives from its control loop.
//   ClientHeldInput   -- what the host may still be holding down from this client: keys, mouse buttons and
//                        physical keys whose down was SENT and whose up was not. Parking releases exactly
//                        these; modifier bookkeeping in the UI is not the source of truth (an enqueue is
//                        not a delivery).
//   ClientParkFence   -- the order of a park, each step on a real processing boundary:
//                        Releasing: BeginPark only marks it (and from then the session takes nothing new);
//                        the control loop COLLECTS at its next pass boundary -- where no action is in
//                        flight, so a down that was already being sent has been answered and recorded --
//                        dropping unsent actions and queueing the releases of what is held (r2 K1). It
//                        then waits for the input queue to drain AND nothing to be held (every input is
//                        answered by an InputAck; one more round of releases if something still is)
//                        -> Pausing (ControlStreamState(false) requested only now, because the scheduler
//                        sends StreamState before input)
//                        -> Fencing (the pause was sent; a Ping is forced AFTER it)
//                        -> Parked (that Ping's Pong came back: the host's control handler has taken the
//                        pause, since control is processed in order). A Pong for a Ping sent before the
//                        pause never confirms it.
//                        Not Parked within the budget = Failed; a fence Pong after the budget does not
//                        save it (r2 K3). Parked past the hard limit -- 5 min from BeginPark, the app's
//                        own deadline -- = Expired. Both end the session in the control loop itself, so a
//                        stopped UI cannot keep either alive.
// Thread:  the session's control loop and the app's calls, under the session's park mutex.
// Callers: ClientSessionController (native_video_client_session.cpp); client_park_state_test.cpp.
// Pure, no platform headers.

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "native_video_client_shared_core.hpp"

namespace remote60::native_poc {

enum class ClientParkPhase : uint8_t {
  None = 0,
  Releasing = 1,
  Pausing = 2,
  Fencing = 3,
  Parked = 4,
  Failed = 5,
  Expired = 6,
};

inline const char* client_park_phase_name(ClientParkPhase p) {
  switch (p) {
    case ClientParkPhase::None: return "none";
    case ClientParkPhase::Releasing: return "releasing";
    case ClientParkPhase::Pausing: return "pausing";
    case ClientParkPhase::Fencing: return "fencing";
    case ClientParkPhase::Parked: return "parked";
    case ClientParkPhase::Failed: return "failed";
    case ClientParkPhase::Expired: return "expired";
  }
  return "?";
}

class ClientHeldInput {
 public:
  // An input action that was sent and answered (the control loop calls it after execute succeeded).
  void OnSent(const ControlOutboundAction& a) {
    if (a.kind == ControlOutboundActionKind::InputEvent) {
      const ControlInputEventMessage& e = a.inputEvent;
      lastX_ = e.x;
      lastY_ = e.y;
      if (e.kind == 5 && e.keyCode <= 255) keys_.insert(e.keyCode);
      if (e.kind == 6) keys_.erase(e.keyCode);
      if (e.kind == 2 && IsButton(e.keyCode)) buttons_.insert(e.keyCode);
      if (e.kind == 3) buttons_.erase(e.keyCode);
    } else if (a.kind == ControlOutboundActionKind::PhysicalKey) {
      const ControlPhysicalKeyMessage& k = a.physicalKey;
      if ((k.flags & 0x4u) != 0) return;  // a make-only pulse (Hangul / Hanja) has no up to owe
      const uint32_t id = PhysicalId(k);
      if (k.down) {
        physical_[id] = k;
      } else {
        physical_.erase(id);
      }
    }
  }

  bool Empty() const { return keys_.empty() && buttons_.empty() && physical_.empty(); }
  size_t Count() const { return keys_.size() + buttons_.size() + physical_.size(); }

  // The up for everything held, in the input queue's own sequence.
  std::vector<QueuedControlInputMessage> Releases(ClientInputQueue& q, uint64_t nowUs) const {
    std::vector<QueuedControlInputMessage> out;
    for (uint32_t b : buttons_) out.push_back(make_control_input_event(q, 3, 0, lastX_, lastY_, 0, b, nowUs));
    for (uint32_t k : keys_) out.push_back(make_control_input_event(q, 6, 0, 0, 0, 0, k, nowUs));
    for (const auto& [id, down] : physical_) {
      (void)id;
      QueuedControlInputMessage m{};
      m.type = MessageType::ControlPhysicalKey;
      m.physicalKey = down;
      m.physicalKey.seq = q.NextSequence();
      m.physicalKey.down = 0;
      m.physicalKey.flags = static_cast<uint16_t>(down.flags & 0x1u);  // extended only: no repeat on an up
      m.physicalKey.clientSendQpcUs = nowUs;
      m.generatedUs = nowUs;
      out.push_back(m);
    }
    return out;
  }

  void Reset() {
    keys_.clear();
    buttons_.clear();
    physical_.clear();
    lastX_ = lastY_ = 0;
  }

 private:
  static bool IsButton(uint32_t vk) { return vk == 1 || vk == 2 || vk == 4 || vk == 5 || vk == 6; }
  static uint32_t PhysicalId(const ControlPhysicalKeyMessage& k) {
    return k.scanCode ? 0x10000u + (static_cast<uint32_t>(k.scanCode) << 1) + (k.flags & 1u) : k.vk;
  }
  std::set<uint32_t> keys_;
  std::set<uint32_t> buttons_;
  std::map<uint32_t, ControlPhysicalKeyMessage> physical_;
  int32_t lastX_ = 0, lastY_ = 0;
};

class ClientParkFence {
 public:
  static constexpr uint64_t kConfirmBudgetUs = 2000000ULL;   // Codex section 3: an initial 2 s
  static constexpr uint64_t kParkedLimitUs = 300000000ULL;   // the contract's 5 min, from BeginPark (r2 K3)

  // What the control loop does at a pass boundary (no action in flight), before NextAction.
  struct PassStep {
    bool collect = false;    // drop unsent actions, queue the releases of what is held, cancel other pending
    bool reRelease = false;  // something is still held with the queue empty: queue its releases again
    bool pauseNow = false;   // request ControlStreamState(false) now
    bool pingNow = false;    // send a Ping next (the fence)
    ClientParkPhase phase = ClientParkPhase::None;
  };
  PassStep OnPassBoundary(uint64_t nowUs, bool inputQueueEmpty, bool nothingHeld) {
    PassStep st;
    Tick(nowUs);
    if (phase_ == ClientParkPhase::Releasing) {
      if (!collected_) {
        collected_ = true;
        st.collect = true;
      } else if (inputQueueEmpty && nothingHeld) {
        phase_ = ClientParkPhase::Pausing;
        st.pauseNow = true;
      } else if (inputQueueEmpty) {
        if (reReleases_ < 1) {
          ++reReleases_;
          st.reRelease = true;
        } else {
          phase_ = ClientParkPhase::Failed;  // a release that will not land: the disconnect teardown releases
        }
      }
    }
    st.pingNow = TakePingWanted();
    st.phase = phase_;
    return st;
  }

  // Starts a park now. false (nothing changes) if one is already running or parked.
  bool Begin(uint64_t nowUs) {
    if (phase_ != ClientParkPhase::None && phase_ != ClientParkPhase::Failed && phase_ != ClientParkPhase::Expired) {
      return false;
    }
    phase_ = ClientParkPhase::Releasing;
    beganUs_ = nowUs;
    parkedUs_ = 0;
    fencePingSeq_ = 0;
    wantPing_ = false;
    collected_ = false;
    reReleases_ = 0;
    return true;
  }
  void OnStreamStateSent(bool active) {
    if (phase_ != ClientParkPhase::Pausing || active) return;
    phase_ = ClientParkPhase::Fencing;
    wantPing_ = true;
  }
  // Once, after the pause was sent: the scheduler should send a Ping now.
  bool TakePingWanted() {
    const bool w = wantPing_;
    wantPing_ = false;
    return w;
  }
  void OnPingSent(uint32_t seq) {
    if (phase_ == ClientParkPhase::Fencing && fencePingSeq_ == 0) fencePingSeq_ = seq;
  }
  void OnPong(uint32_t seq, uint64_t nowUs) {
    if (phase_ == ClientParkPhase::Fencing && fencePingSeq_ != 0 && seq == fencePingSeq_) {
      if (nowUs > beganUs_ + kConfirmBudgetUs) {  // r2 K3: too late -- not confirmed, not saved
        phase_ = ClientParkPhase::Failed;
        return;
      }
      phase_ = ClientParkPhase::Parked;
      parkedUs_ = nowUs;
    }
  }
  // Budget and limit, on every pass. Returns the phase after it.
  ClientParkPhase Tick(uint64_t nowUs) {
    const bool confirming = phase_ == ClientParkPhase::Releasing || phase_ == ClientParkPhase::Pausing ||
                            phase_ == ClientParkPhase::Fencing;
    if (confirming && nowUs > beganUs_ + kConfirmBudgetUs) phase_ = ClientParkPhase::Failed;
    if (phase_ == ClientParkPhase::Parked && nowUs > beganUs_ + kParkedLimitUs) phase_ = ClientParkPhase::Expired;
    return phase_;
  }
  // Resumed or abandoned: nothing parked.
  void End() {
    phase_ = ClientParkPhase::None;
    fencePingSeq_ = 0;
    wantPing_ = false;
  }

  ClientParkPhase phase() const { return phase_; }
  // Any phase but None: the session holds input, clipboard, previews and the stream back.
  bool Inhibiting() const { return phase_ != ClientParkPhase::None; }
  uint64_t began_us() const { return beganUs_; }
  uint64_t parked_us() const { return parkedUs_; }
  uint32_t fence_ping_seq() const { return fencePingSeq_; }
  bool collected() const { return collected_; }

 private:
  ClientParkPhase phase_ = ClientParkPhase::None;
  uint64_t beganUs_ = 0;
  uint64_t parkedUs_ = 0;
  uint32_t fencePingSeq_ = 0;
  bool wantPing_ = false;
  bool collected_ = false;
  uint32_t reReleases_ = 0;
};

// r2 K1: what the control loop may still send while a park runs (from its first pass boundary on):
// the fence's own Ping and pause, input (only releases can be queued by then) and the paused report.
// Anything else the scheduler hands out -- a list, a selection, a tune, a capture mode, a backend, a key
// request -- is dropped unsent: a park has no side effect beyond letting go and pausing.
inline bool client_park_allows(ControlOutboundActionKind kind) {
  switch (kind) {
    case ControlOutboundActionKind::Ping:
    case ControlOutboundActionKind::StreamState:
    case ControlOutboundActionKind::InputEvent:
    case ControlOutboundActionKind::InputText:
    case ControlOutboundActionKind::PhysicalKey:
    case ControlOutboundActionKind::Metrics:
      return true;
    default:
      return false;
  }
}

}  // namespace remote60::native_poc

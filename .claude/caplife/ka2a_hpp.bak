#pragma once

// keepalive-r2 ② (contract docs/keepalive_host_standby_contract.md §2.2, §2.6, §2.7, §3): the CLIENT side of one host's
// standby entry -- a pure state machine, no sockets and no clock of its own. The caller owns the host lease's one UDP
// socket (one lease, one socket: §2.7), feeds every Standby* datagram, the HelloAck and the time in, and carries the
// effects out (send this packet / get a fresh one-time capability / the activation is complete / ...).
//
//   Idle --Admit(cap)--> Admitting --Ack Ok--> Parked (Ping keeps the lease) --Activate()--> Activating
//   Activating --Pong(this gen, Granted) AND HelloAck, either order--> Active --SessionEnded()--> Parked
//
// What it never does:
//  - complete an activation on a bare HelloAck, or on a Pong of another entry / another generation (§2.2: the Pong
//    names the activation it answers; a refusal names the generation it refuses);
//  - send an ordinary Hello: a host that never answers a StandbyHello is Unsupported (an older host, or a pre-login
//    worker -- §2.6), and the explicit manual connect is the caller's own, separate path;
//  - keep a lease it cannot prove alive: Pings unanswered past kPingDeadMs (e.g. the relay lost the host's proof after a
//    move, every Punch lost) end the entry -- Lost, then a new admission after a bounded backoff, never "ready";
//  - reuse a generation: every admission and every activation takes the next value of a durable, monotone counter
//    (GenerationAllocator), saved BEFORE it is used, so a restart never sends a smaller one (§2.2, r4 N2-b).
// A cancelled activation is dropped on this side: its late Pong / HelloAck adopt nothing. No Release is sent for it --
// the host may already have granted it, a granted entry refuses Release (Live), and the session the host made ends
// by itself when this side does not take it (peer lost). The next activation uses a larger generation.

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "poc_protocol.hpp"

namespace remote60::native_poc {

// ---- the durable counter (b)

// The platform's store (APK SharedPreferences, PC a file): this install's 16-byte device id and the last generation
// handed out. Load false = nothing usable stored (first run, or damaged): a NEW device id is made and saved.
struct StandbyGenerationStore {
  virtual ~StandbyGenerationStore() = default;
  virtual bool Load(std::array<uint8_t, 16>* deviceId, uint32_t* lastGen) = 0;
  virtual bool Save(const std::array<uint8_t, 16>& deviceId, uint32_t lastGen) = 0;
};

class GenerationAllocator {
 public:
  // `random` fills a fresh device id (the platform's CSPRNG); false = no id can be made.
  GenerationAllocator(StandbyGenerationStore& store, std::function<bool(uint8_t*, size_t)> random)
      : store_(store), random_(std::move(random)) {}

  // The next generation, already saved. false: the store or the random source failed -- then nothing may be sent
  // under a generation that a restart could hand out again.
  bool Next(uint32_t* gen) {
    if (!loaded_ && !LoadOrCreate()) return false;
    if (last_ == UINT32_MAX) return false;  // exhausted: never wraps to a smaller value
    const uint32_t next = last_ + 1;
    if (!store_.Save(deviceId_, next)) return false;
    last_ = next;
    *gen = next;
    return true;
  }
  bool DeviceId(std::array<uint8_t, 16>* out) {
    if (!loaded_ && !LoadOrCreate()) return false;
    *out = deviceId_;
    return true;
  }

 private:
  bool LoadOrCreate() {
    std::array<uint8_t, 16> id{};
    uint32_t last = 0;
    bool zero = true;
    if (store_.Load(&id, &last)) {
      for (uint8_t b : id) zero = zero && b == 0;
    }
    if (zero) {
      // nothing usable: a new device (its old entry, if any, lapses on its lease); generations start over for it
      if (!random_ || !random_(id.data(), id.size())) return false;
      last = 0;
      if (!store_.Save(id, last)) return false;
    }
    deviceId_ = id;
    last_ = last;
    loaded_ = true;
    return true;
  }

  StandbyGenerationStore& store_;
  std::function<bool(uint8_t*, size_t)> random_;
  std::array<uint8_t, 16> deviceId_{};
  uint32_t last_ = 0;
  bool loaded_ = false;
};

// ---- the entry (a)

class StandbyEntryClient {
 public:
  enum class State { Idle, Admitting, Parked, Activating, Active, Unsupported, Waiting };

  // Timing (§2.3: the host's lease is 20 s, renewed by each Ping).
  static constexpr uint64_t kHelloRetryMs = 500;
  static constexpr int kHelloAttempts = 6;            // no answer to these = Unsupported (an older host)
  static constexpr uint64_t kPingIntervalMs = 5000;
  static constexpr uint64_t kPingDeadMs = 16000;      // no Pong this long (< the 20 s lease): the entry is lost
  static constexpr uint64_t kActivateRetryMs = 400;
  static constexpr int kActivateAttempts = 10;        // then the activation fails (the host or the path is gone)

  struct Effect {
    enum Kind {
      Send,                // send `packet` on this lease's socket
      NeedCapability,      // get a fresh one-time capability (directory connect) and call Admit with it
      Activated,           // the activation is complete: the session (HelloAck) is this entry's -- take it
      ActivationFailed,    // the current activation will not complete (`reason`); the entry state says what remains
      Unsupported,         // the host does not answer standby: no automatic connect to it (no legacy Hello)
      Lost,                // the entry is gone (unknown to the host, or its lease unproven): a new admission follows
    };
    Kind kind = Send;
    UdpStandbyPacket packet{};
    std::string reason;
  };
  using Effects = std::vector<Effect>;

  StandbyEntryClient(GenerationAllocator& gens, uint32_t helloFeatures) : gens_(gens), helloFeatures_(helloFeatures) {}

  State state() const { return state_; }
  uint64_t entryId() const { return entryId_; }
  uint32_t activationGen() const { return activateGen_; }

  // Admission with a fresh one-time capability (Idle / Waiting / after Lost). The capability is kept only to repeat the
  // very same Hello (a retransmit is the host's Replay, §2.2) and dropped once answered.
  Effects Admit(const std::string& capability, uint64_t nowMs) {
    Effects fx;
    if (state_ == State::Unsupported || state_ == State::Active || state_ == State::Activating) return fx;
    if (capability.empty() || capability.size() > 32) return fx;  // standby is for directory-authenticated clients
    uint32_t gen = 0;
    std::array<uint8_t, 16> id{};
    if (!gens_.Next(&gen) || !gens_.DeviceId(&id)) {
      Wait(nowMs, "generation store failed", &fx);
      return fx;
    }
    hello_ = UdpStandbyPacket{};
    hello_.size = static_cast<uint16_t>(sizeof(hello_));
    hello_.kind = static_cast<uint16_t>(UdpPacketKind::StandbyHello);
    std::memcpy(hello_.deviceId, id.data(), 16);
    hello_.clientGen = gen;
    hello_.features = helloFeatures_;
    std::memcpy(hello_.authToken, capability.data(), capability.size());
    admitGen_ = gen;
    state_ = State::Admitting;
    helloSent_ = 0;
    SendHello(nowMs, &fx);
    return fx;
  }

  // The user's explicit activation (Parked only). A new, larger generation every time.
  Effects Activate(uint64_t nowMs) {
    Effects fx;
    if (state_ != State::Parked) return fx;
    uint32_t gen = 0;
    if (!gens_.Next(&gen)) {
      fx.push_back(Fx(Effect::ActivationFailed, "generation store failed"));
      return fx;
    }
    activateGen_ = gen;
    gotPong_ = false;
    gotHelloAck_ = false;
    activateSent_ = 0;
    state_ = State::Activating;
    SendActivate(nowMs, &fx);
    return fx;
  }

  // The user gave up waiting. Its late Pong / HelloAck adopt nothing (see the header); the entry stays parked.
  Effects CancelActivation(uint64_t nowMs) {
    Effects fx;
    if (state_ != State::Activating) return fx;
    cancelledGen_ = activateGen_;
    state_ = State::Parked;
    lastPongMs_ = nowMs;  // pinging resumes from now
    nextPingMs_ = nowMs;
    return fx;
  }

  // The client lets the entry go (the app left the screen -- the phone keeps standby only while visible -- the PC was
  // evicted from the recent three, sign-out): a parked entry is released at the host at once (one StandbyRelease, no
  // retry: an unanswered one lapses on the lease anyway) and nothing more is sent. Not while Active / Activating (a
  // granted entry refuses Release -- Live; that session ends by itself). Idle afterwards: a new admission needs a new
  // capability.
  Effects Release(uint64_t /* nowMs */) {
    Effects fx;
    if (state_ == State::Parked) fx.push_back(SendOf(Packet(UdpPacketKind::StandbyRelease, 0)));
    if (state_ == State::Active || state_ == State::Activating) return fx;
    entryId_ = 0;
    std::memset(secret_, 0, sizeof(secret_));
    std::memset(hello_.authToken, 0, sizeof(hello_.authToken));
    if (state_ != State::Unsupported) state_ = State::Idle;  // an older host stays known as such
    return fx;
  }

  // The session this entry activated ended (the caller's session layer): the host parks the entry again (§2.3 S3).
  void SessionEnded(uint64_t nowMs) {
    if (state_ != State::Active) return;
    state_ = State::Parked;
    lastPongMs_ = nowMs;
    nextPingMs_ = nowMs;
  }

  Effects OnStandbyPacket(const UdpStandbyPacket& p, uint64_t nowMs) {
    Effects fx;
    if (p.magic != kMagic || p.size != sizeof(UdpStandbyPacket) || p.version != kUdpProtocolVersion) return fx;
    const auto kind = static_cast<UdpPacketKind>(p.kind);
    if (kind == UdpPacketKind::StandbyAck) {
      if (state_ != State::Admitting || p.clientGen != admitGen_) return fx;  // not the admission in flight
      std::memset(hello_.authToken, 0, sizeof(hello_.authToken));  // answered: the capability is not kept
      if (p.status == kUdpStandbyStatusOk && p.entryId != 0) {
        entryId_ = p.entryId;
        std::memcpy(secret_, p.entrySecret, 16);
        state_ = State::Parked;
        busyBackoff_ = 0;
        lastPongMs_ = nowMs;
        nextPingMs_ = nowMs + kPingIntervalMs;
        return fx;
      }
      // Busy (host full / this device holds the session / no random draw) or Superseded (not newer than this
      // device's entry there): no secret. A new admission needs a new capability -- after a backoff, never at once.
      Wait(nowMs, p.status == kUdpStandbyStatusSuperseded ? "admission superseded" : "host busy", &fx);
      return fx;
    }
    if (kind != UdpPacketKind::StandbyPong || p.entryId != entryId_ || entryId_ == 0) return fx;
    if ((p.features & kUdpStandbyPongActivationGranted) != 0 || p.clientGen != 0) {
      // an activation's answer: only the activation in flight, by its generation
      if (state_ != State::Activating || p.clientGen != activateGen_) return fx;  // late, cancelled or another one
      if (p.status == kUdpStandbyStatusOk && (p.features & kUdpStandbyPongActivationGranted) != 0) {
        gotPong_ = true;
        if (gotHelloAck_) Complete(&fx);
        return fx;
      }
      // refused (Superseded: a stale generation / Unknown: no such entry)
      if (p.status == kUdpStandbyStatusUnknown) {
        Lose(nowMs, "activation: entry unknown to the host", &fx);
      } else {
        state_ = State::Parked;
        lastPongMs_ = nowMs;
        nextPingMs_ = nowMs;
        fx.push_back(Fx(Effect::ActivationFailed, "activation refused (superseded)"));
      }
      return fx;
    }
    // a Ping's answer
    if (p.status == kUdpStandbyStatusOk) {
      lastPongMs_ = nowMs;
    } else if (state_ == State::Parked) {
      Lose(nowMs, "ping: entry unknown to the host", &fx);
    }
    return fx;
  }

  // The HelloAck (DirectoryAuth) that arrived on this lease's socket. Counts only for the activation in flight, and
  // completes it only with that activation's own Pong (in either order).
  Effects OnHelloAck(uint64_t /* nowMs */) {
    Effects fx;
    if (state_ != State::Activating) return fx;  // bare, late or cancelled: adopts nothing
    gotHelloAck_ = true;
    if (gotPong_) Complete(&fx);
    return fx;
  }

  Effects Tick(uint64_t nowMs) {
    Effects fx;
    switch (state_) {
      case State::Admitting:
        if (nowMs >= nextHelloMs_) {
          if (helloSent_ >= kHelloAttempts) {
            // never answered: an older host (or a pre-login worker). No automatic connect to it, no legacy Hello.
            std::memset(hello_.authToken, 0, sizeof(hello_.authToken));
            state_ = State::Unsupported;
            fx.push_back(Fx(Effect::Unsupported, "no answer to StandbyHello"));
          } else {
            SendHello(nowMs, &fx);
          }
        }
        break;
      case State::Parked:
        if (nowMs - lastPongMs_ > kPingDeadMs) {
          Lose(nowMs, "no pong (lease unproven)", &fx);
        } else if (nowMs >= nextPingMs_) {
          fx.push_back(SendOf(Packet(UdpPacketKind::StandbyPing, 0)));
          nextPingMs_ = nowMs + kPingIntervalMs;
        }
        break;
      case State::Activating:
        if (nowMs >= nextActivateMs_) {
          if (activateSent_ >= kActivateAttempts) {
            Lose(nowMs, "activation: no answer", &fx);
            fx.insert(fx.begin(), Fx(Effect::ActivationFailed, "activation: no answer"));
          } else {
            SendActivate(nowMs, &fx);
          }
        }
        break;
      case State::Waiting:
        if (nowMs >= waitUntilMs_) {
          state_ = State::Idle;
          fx.push_back(Fx(Effect::NeedCapability, waitReason_));
        }
        break;
      default:
        break;
    }
    return fx;
  }

  // Backoff after Busy / Superseded / Lost / a store failure: 1, 2, 4, 8, 16, 30 s (§6: retries are bounded).
  static uint64_t BackoffMs(int step) {
    static constexpr uint64_t kSteps[] = {1000, 2000, 4000, 8000, 16000, 30000};
    return kSteps[step < 0 ? 0 : step > 5 ? 5 : step];
  }

 private:
  static Effect Fx(Effect::Kind k, const std::string& reason) {
    Effect e;
    e.kind = k;
    e.reason = reason;
    return e;
  }
  static Effect SendOf(const UdpStandbyPacket& p) {
    Effect e;
    e.kind = Effect::Send;
    e.packet = p;
    return e;
  }
  UdpStandbyPacket Packet(UdpPacketKind kind, uint32_t gen) const {
    UdpStandbyPacket p{};
    p.size = static_cast<uint16_t>(sizeof(p));
    p.kind = static_cast<uint16_t>(kind);
    p.entryId = entryId_;
    std::memcpy(p.entrySecret, secret_, 16);
    p.clientGen = gen;
    return p;
  }
  void SendHello(uint64_t nowMs, Effects* fx) {
    fx->push_back(SendOf(hello_));
    ++helloSent_;
    nextHelloMs_ = nowMs + kHelloRetryMs;
  }
  void SendActivate(uint64_t nowMs, Effects* fx) {
    // a retransmit repeats the SAME generation: the host re-sends its HelloAck while that grant holds (§2.2)
    fx->push_back(SendOf(Packet(UdpPacketKind::StandbyActivate, activateGen_)));
    ++activateSent_;
    nextActivateMs_ = nowMs + kActivateRetryMs;
  }
  void Complete(Effects* fx) {
    state_ = State::Active;
    fx->push_back(Fx(Effect::Activated, "pong granted + hello ack"));
  }
  void Wait(uint64_t nowMs, const std::string& reason, Effects* fx) {
    (void)fx;
    state_ = State::Waiting;
    waitReason_ = reason;
    waitUntilMs_ = nowMs + BackoffMs(busyBackoff_);
    if (busyBackoff_ < 5) ++busyBackoff_;
  }
  void Lose(uint64_t nowMs, const std::string& reason, Effects* fx) {
    entryId_ = 0;
    std::memset(secret_, 0, sizeof(secret_));
    fx->push_back(Fx(Effect::Lost, reason));
    Wait(nowMs, reason, fx);
  }

  GenerationAllocator& gens_;
  const uint32_t helloFeatures_;
  State state_ = State::Idle;
  UdpStandbyPacket hello_{};
  uint32_t admitGen_ = 0;
  int helloSent_ = 0;
  uint64_t nextHelloMs_ = 0;
  uint64_t entryId_ = 0;
  uint8_t secret_[16] = {};
  uint64_t lastPongMs_ = 0;
  uint64_t nextPingMs_ = 0;
  uint32_t activateGen_ = 0;
  uint32_t cancelledGen_ = 0;
  bool gotPong_ = false;
  bool gotHelloAck_ = false;
  int activateSent_ = 0;
  uint64_t nextActivateMs_ = 0;
  int busyBackoff_ = 0;
  uint64_t waitUntilMs_ = 0;
  std::string waitReason_;
};

}  // namespace remote60::native_poc

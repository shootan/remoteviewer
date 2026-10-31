p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_lease.hpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
''', '''#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
''')
rep('''#include "native_video_client_session.hpp"
''', '''#include "native_video_client_session.hpp"
#include "native_video_client_tcp_control.hpp"
''')

# header comment: the r3 contract
rep('''// session's ordinary path. The lease keeps its own handle and does not read it until the session is over; then it goes
// on with the SAME socket -- the same endpoint, which a relay lease is bound to (§2.7) -- and the entry by Ping.''',
'''// session's ordinary path. The lease keeps its own handle and does not read it until the session is over; then it goes
// on with the SAME socket -- the same endpoint, which a relay lease is bound to (§2.7) -- and the entry by Ping.
//
// r3 (Codex codex_keepalive_r2_scope2.md N1, N2) -- one contract for an activation's lifetime:
//  - ActivationArgs gives an OPERATION number; the handshake closure holds `keepAlive` (the runner's slot), so the lease
//    outlives every copy of the arguments whatever the app releases meanwhile.
//  - The caller then does exactly one of: Connect with the arguments (the session owns the duplicated handle and closes
//    it, success or failure), or Abandon(op, handle) (the handle closed here). After the session is over -- Connect
//    failed, or Disconnect RETURNED (its reader has stopped) -- it calls SessionOver(op).
//  - SessionOver / Abandon of another operation (a late call after a new activation) do nothing; SessionOver while the
//    handshake still runs is refused (the session's reader may still be reading this socket's duplicate).
//  - Release while handed off cancels the handshake in flight (it returns false; its late answers adopt nothing) --
//    nothing else is sent: the host's grant, if any, ends with its session.
//  - The HelloAck the activation accepts passes the ordinary handshake's test (udp_hello_ack_acceptable, DirectoryAuth
//    required) AND its declared size; anything else is ignored -- no state, no features. Which grant it belongs to is
//    the Pong's to say (entry + generation): a HelloAck carries no generation, it only completes the activation in
//    flight together with that activation's own Pong (a late one of a cancelled activation is the same session
//    admission's feature answer; it never makes a grant by itself).
// Every method takes the lease's lock; the handshake takes it per step (never across a receive wait).''')

rep('''  bool Open(std::string* error) { return Reopen(error); }

  State state() const { return client_.state(); }
  uint64_t entryId() const { return client_.entryId(); }
  int lostCount() const { return lost_; }
  // ② e: the lease waits for a capability its CapabilityFn could not give now (the APK: the directory call is the app's,
  // Kotlin) -- the owner observes from the lease's socket (ObserveFrom), gets one, and hands it in (Provide).
  bool needsCapability() const { return needsCapability_; }
  const std::vector<std::string>& events() const { return events_; }
  std::string target() const { return ip_ + ":" + std::to_string(port_); }

  // Start (or restart) the admission with a capability from CapabilityFn.
  void Start() { NeedCapability(); }

  // The directory observation from THIS lease's socket (the relay binds a standby lease to that endpoint).
  bool ObserveFrom(const std::string& host, uint16_t port, const std::string& token) {
    return socket_ != kInvalidSocket && !handedOff_ && standby_observe_on(socket_, host, port, token);
  }

  // A capability the owner got for this lease (after needsCapability()): aim, then the admission.
  bool Provide(const StandbyCapability& cap) {
    if (!needsCapability_ || cap.token.empty() || cap.port == 0) return false;''', '''  bool Open(std::string* error) {
    std::lock_guard<std::mutex> lock(mu_);
    return Reopen(error);
  }

  State state() const {
    std::lock_guard<std::mutex> lock(mu_);
    return client_.state();
  }
  uint64_t entryId() const {
    std::lock_guard<std::mutex> lock(mu_);
    return client_.entryId();
  }
  int lostCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return lost_;
  }
  // ② e: the lease waits for a capability its CapabilityFn could not give now (the APK: the directory call is the app's,
  // Kotlin) -- the owner observes from the lease's socket (ObserveFrom), gets one, and hands it in (Provide).
  bool needsCapability() const {
    std::lock_guard<std::mutex> lock(mu_);
    return needsCapability_;
  }
  bool handedOff() const {
    std::lock_guard<std::mutex> lock(mu_);
    return handedOff_;
  }
  // the last kEventsKept notes (r3: bounded -- a long retry loop does not grow it; no secret is ever written to it)
  std::vector<std::string> events() const {
    std::lock_guard<std::mutex> lock(mu_);
    return std::vector<std::string>(events_.begin(), events_.end());
  }
  std::string target() const {
    std::lock_guard<std::mutex> lock(mu_);
    return ip_ + ":" + std::to_string(port_);
  }

  // Start (or restart) the admission with a capability from CapabilityFn.
  void Start() {
    std::lock_guard<std::mutex> lock(mu_);
    NeedCapability();
  }

  // The directory observation from THIS lease's socket (the relay binds a standby lease to that endpoint).
  bool ObserveFrom(const std::string& host, uint16_t port, const std::string& token) {
    std::lock_guard<std::mutex> lock(mu_);
    return socket_ != kInvalidSocket && !handedOff_ && standby_observe_on(socket_, host, port, token);
  }

  // A capability the owner got for this lease (after needsCapability()): aim, then the admission.
  bool Provide(const StandbyCapability& cap) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!needsCapability_ || cap.token.empty() || cap.port == 0) return false;''')

rep('''  void Pump(uint32_t waitMs) {
    if (socket_ == kInvalidSocket || handedOff_) return;
    const uint64_t until = now_() + waitMs;
    do {
      UdpStandbyPacket p{};
      p.magic = 0;
      if (Receive(socket_, 20, &p, nullptr) && p.magic == kMagic) Apply(client_.OnStandbyPacket(p, now_()));
      Apply(client_.Tick(now_()));
    } while (now_() < until);
  }''', '''  void Pump(uint32_t waitMs) {
    const uint64_t until = now_() + waitMs;
    do {
      std::lock_guard<std::mutex> lock(mu_);
      if (socket_ == kInvalidSocket || handedOff_) return;
      UdpStandbyPacket p{};
      p.magic = 0;
      if (Receive(socket_, 20, &p, nullptr) && p.magic == kMagic) Apply(client_.OnStandbyPacket(p, now_()));
      Apply(client_.Tick(now_()));
    } while (now_() < until);
  }''')

rep('''  bool ActivationArgs(ClientSessionConnectArgs* args) {
    if (client_.state() != State::Parked || socket_ == kInvalidSocket || port_ == 0) return false;
    const SocketHandle sessionHandle = Duplicate(socket_);
    if (sessionHandle == kInvalidSocket) {
      events_.push_back("socket duplicate failed");
      return false;
    }''', '''  // `keepAlive` (r3 N2): held by the handshake closure -- the owner of this lease (the runner's slot), so the lease lives
  // as long as any copy of the arguments. *op: this activation's number (SessionOver / Abandon name it).
  bool ActivationArgs(ClientSessionConnectArgs* args, std::shared_ptr<void> keepAlive = nullptr, uint64_t* op = nullptr) {
    std::lock_guard<std::mutex> lock(mu_);
    if (client_.state() != State::Parked || socket_ == kInvalidSocket || port_ == 0 || handedOff_) return false;
    const SocketHandle sessionHandle = Duplicate(socket_);
    if (sessionHandle == kInvalidSocket) {
      Note("socket duplicate failed");
      return false;
    }''')
rep('''    args->preparedUdpSocket = sessionHandle;  // the session's handle; the lease keeps its own
    handedOff_ = true;
    args->udpHandshake = [this](SocketHandle s, const std::atomic<bool>& stop, std::string* error, uint32_t* ackFeatures) {
      return Handshake(s, stop, error, ackFeatures);
    };
    return true;
  }''', '''    args->preparedUdpSocket = sessionHandle;  // the session's handle; the lease keeps its own
    handedOff_ = true;
    cancel_.store(false, std::memory_order_release);
    const uint64_t thisOp = ++op_;
    if (op) *op = thisOp;
    args->udpHandshake = [self = this, keep = std::move(keepAlive)](SocketHandle s, const std::atomic<bool>& stop,
                                                                   std::string* error, uint32_t* ackFeatures) {
      return self->Handshake(s, stop, error, ackFeatures);
    };
    return true;
  }

  // The arguments of operation `op` will not be used: its session handle is closed here and the lease takes its socket
  // back. Another operation's: nothing.
  bool Abandon(uint64_t op, SocketHandle* sessionHandle) {
    std::lock_guard<std::mutex> lock(mu_);
    if (op != op_ || !handedOff_ || handshaking_.load(std::memory_order_acquire)) return false;
    if (sessionHandle) close_socket(sessionHandle);
    handedOff_ = false;
    return true;
  }''')
rep('''  void SessionOver() {
    handedOff_ = false;
    if (client_.state() == State::Active) client_.SessionEnded(now_());
    if (socket_ == kInvalidSocket) {
      std::string error;
      if (!Reopen(&error)) events_.push_back("reopen failed: " + error);
    }
  }

  // The user gave up the activation in flight (Connect still running): its late answers adopt nothing.
  void CancelActivation() { Apply(client_.CancelActivation(now_())); }

  // The app lets this PC's standby go (it leaves the screen, the PC left the recent three, sign-out): a parked entry is
  // released at the host (one datagram); the lease stays open for a later admission. Not while handed off (a session).
  void Release() {
    if (handedOff_) return;
    Apply(client_.Release(now_()));
  }''', '''  // r3 (N2): only for the operation it names, and never while that operation's handshake still runs.
  bool SessionOver(uint64_t op) {
    std::lock_guard<std::mutex> lock(mu_);
    if (op != op_ || !handedOff_ || handshaking_.load(std::memory_order_acquire)) return false;
    handedOff_ = false;
    if (client_.state() == State::Active) client_.SessionEnded(now_());
    if (socket_ == kInvalidSocket) {
      std::string error;
      if (!Reopen(&error)) Note("reopen failed: " + error);
    }
    return true;
  }

  // The user gave up the activation in flight (Connect still running): its handshake stops, its late answers adopt
  // nothing (the operation is dead for good: the next activation is a new operation with a larger generation).
  void CancelActivation() { cancel_.store(true, std::memory_order_release); }

  // The app lets this PC's standby go (it leaves the screen, the PC left the recent three, sign-out): a parked entry is
  // released at the host (one datagram). Handed off: the handshake in flight is cancelled and nothing is sent (a granted
  // entry refuses Release; that session ends by itself).
  void Release() {
    std::lock_guard<std::mutex> lock(mu_);
    if (handedOff_) {
      cancel_.store(true, std::memory_order_release);
      return;
    }
    Apply(client_.Release(now_()));
  }''')

# Receive: the HelloAck test (N1)
rep('''    if (kind == static_cast<uint16_t>(UdpPacketKind::HelloAck) && n >= static_cast<int>(sizeof(UdpHelloPacket))) {
      if (!ack) return false;
      std::memcpy(ack, buf, sizeof(*ack));
      return true;
    }''', '''    if (kind == static_cast<uint16_t>(UdpPacketKind::HelloAck) && n >= static_cast<int>(sizeof(UdpHelloPacket))) {
      if (!ack) return false;
      UdpHelloPacket candidate{};
      std::memcpy(&candidate, buf, sizeof(candidate));
      // r3 (N1): the ordinary handshake's test (DirectoryAuth required: a standby activation is directory-authenticated)
      // and the declared size; a failing one is ignored whole -- no state, no features
      if (!udp_hello_ack_acceptable(candidate, n, /*requireDirectoryAuth=*/true) ||
          candidate.size != static_cast<uint16_t>(sizeof(UdpHelloPacket))) {
        return false;
      }
      *ack = candidate;
      return true;
    }''')

# Handshake: per-step lock, cancel flag, handshaking flag
rep('''  bool Handshake(SocketHandle s, const std::atomic<bool>& stop, std::string* error, uint32_t* ackFeatures) {
    StandbyEntryClient::Effects fx = client_.Activate(now_());''', '''  bool Handshake(SocketHandle s, const std::atomic<bool>& stop, std::string* error, uint32_t* ackFeatures) {
    handshaking_.store(true, std::memory_order_release);
    struct Done {
      std::atomic<bool>& flag;
      ~Done() { flag.store(false, std::memory_order_release); }
    } done{handshaking_};
    StandbyEntryClient::Effects fx;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (cancel_.load(std::memory_order_acquire)) {
        if (error) *error = "standby activation cancelled";
        return false;
      }
      fx = client_.Activate(now_());
    }''')
rep('''          events_.push_back(std::string(e.kind == StandbyEntryClient::Effect::Lost ? "lost: " : "activation failed: ") +
                            e.reason);
          if (e.kind == StandbyEntryClient::Effect::Lost) ++lost_;
        }
      }
    };''', '''          std::lock_guard<std::mutex> lock(mu_);
          Note(std::string(e.kind == StandbyEntryClient::Effect::Lost ? "lost: " : "activation failed: ") + e.reason);
          if (e.kind == StandbyEntryClient::Effect::Lost) ++lost_;
        }
      }
    };''')
rep('''    while (!done && !failed && !stop.load(std::memory_order_acquire)) {
      UdpStandbyPacket sb{};
      UdpHelloPacket ack{};
      sb.magic = 0;
      ack.magic = 0;
      if (Receive(s, 20, &sb, &ack)) {
        if (sb.magic == kMagic) run(client_.OnStandbyPacket(sb, now_()));
        if (ack.magic == kMagic && ack.kind == static_cast<uint16_t>(UdpPacketKind::HelloAck)) {
          features = ack.features;
          run(client_.OnHelloAck(now_()));
        }
      }
      run(client_.Tick(now_()));
    }
    if (done) {
      events_.push_back("activated gen=" + std::to_string(client_.activationGen()));
      if (ackFeatures) *ackFeatures = features;
      return true;
    }
    if (!failed) {
      // stopped (Disconnect / cancel) in the middle: drop this activation
      run(client_.CancelActivation(now_()));
      if (error && error->empty()) *error = "standby activation stopped";
    }
    return false;''', '''    auto step = [&](const std::function<StandbyEntryClient::Effects()>& f) {
      StandbyEntryClient::Effects e;
      {
        std::lock_guard<std::mutex> lock(mu_);
        e = f();
      }
      run(e);
    };
    while (!done && !failed && !stop.load(std::memory_order_acquire) && !cancel_.load(std::memory_order_acquire)) {
      UdpStandbyPacket sb{};
      UdpHelloPacket ack{};
      sb.magic = 0;
      ack.magic = 0;
      if (Receive(s, 20, &sb, &ack)) {  // no lock across the wait
        if (sb.magic == kMagic) step([&] { return client_.OnStandbyPacket(sb, now_()); });
        if (ack.magic == kMagic) {
          features = ack.features;  // passed udp_hello_ack_acceptable + size (Receive)
          step([&] { return client_.OnHelloAck(now_()); });
        }
      }
      step([&] { return client_.Tick(now_()); });
    }
    std::lock_guard<std::mutex> lock(mu_);
    if (done) {
      Note("activated gen=" + std::to_string(client_.activationGen()));
      if (ackFeatures) *ackFeatures = features;
      return true;
    }
    if (!failed) {
      // stopped (Disconnect) or cancelled in the middle: this activation is dead; its late answers adopt nothing
      Apply(client_.CancelActivation(now_()));
      Note("activation cancelled");
      if (error && error->empty()) *error = "standby activation stopped";
    }
    return false;''')
rep('''    if (fx.empty()) {
      if (error) *error = "standby activation: the entry is not parked";
      return false;
    }''', '''    if (fx.empty()) {
      if (error) *error = "standby activation: the entry is not parked";
      return false;
    }''')

# NeedCapability / Apply notes -> bounded
s = s.replace('      events_.push_back("no capability now");', '      Note("no capability now");')
s = s.replace('          events_.push_back("unsupported: " + e.reason);', '          Note("unsupported: " + e.reason);')
s = s.replace('          events_.push_back("lost: " + e.reason);', '          Note("lost: " + e.reason);')
assert 'events_.push_back' not in s, s[s.index('events_.push_back') - 200:s.index('events_.push_back') + 80]

rep('''  StandbyEntryClient client_;
  CapabilityFn capability_;''', '''  static constexpr size_t kEventsKept = 32;
  void Note(const std::string& e) {
    events_.push_back(e);
    while (events_.size() > kEventsKept) events_.pop_front();
  }

  mutable std::mutex mu_;
  std::atomic<bool> cancel_{false};
  std::atomic<bool> handshaking_{false};
  uint64_t op_ = 0;
  StandbyEntryClient client_;
  CapabilityFn capability_;''')
rep('''  std::vector<std::string> events_;
};''', '''  std::deque<std::string> events_;
};''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')

#pragma once

// keepalive-r2 ② (c, d): one host's standby lease on THIS side -- the one UDP socket of the lease (contract §2.7: a
// host lease, one socket; two hosts are never multiplexed on one) and the StandbyEntryClient that decides what goes on
// it. The caller pumps it (Pump) from whatever thread owns the lease; nothing here starts a thread.
//
// Where the lease's datagrams go is the capability's: CapabilityFn gets the lease's socket (a relay standby connect
// observes from THIS socket, so the relay binds its lease to this endpoint) and returns the one-time capability with
// the address to aim at -- the relay's (standbyRelay "reserved") or the host's. Only datagrams from that address are
// read.
//
// Activation is a CONNECT MODE of ClientSessionController, not another session type: ActivationArgs() hands the
// session a DUPLICATE handle of the lease's socket (ClientSessionConnectArgs::preparedUdpSocket, which the session owns
// and closes) with a handshake that sends StandbyActivate instead of the Hello and returns only when this activation's
// own Pong (Granted) AND the HelloAck have arrived, in either order (StandbyEntryClient). Everything after is the
// session's ordinary path. The lease keeps its own handle and does not read it until the session is over; then it goes
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
// Every method takes the lease's lock. The pump's and the handshake's receive waits are outside it (per step), and so
// is ObserveFrom's observation. NOT outside it: Start / a NeedCapability effect call the CapabilityFn under the lock
// (the APK's is empty: it returns at once; a desktop one does its directory calls there).
// One reader at a time (r3 N2-b, Codex codex_keepalive_r2_src_r3.md): ActivationArgs / ObserveFrom register their
// intent under the lock (intents_) and the pump starts no new read while one is pending -- they wait for the read in
// flight only, never for a next one; an observation in progress (observing_) holds the pump and a handoff off too.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "native_socket.hpp"
#include "native_video_client_session.hpp"
#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"
#include "standby_entry_client.hpp"

namespace remote60::native_poc {

// One observation from socket `s`: "OBSERVE <token>" to the directory's observe endpoint until an answer (the same
// datagram the rendezvous sends, on this socket). true = the directory recorded this endpoint for `token`.
inline bool standby_observe_on(SocketHandle s, const std::string& host, uint16_t port, const std::string& token) {
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &to.sin_addr) != 1) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    to.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);
  }
  const std::string probe = "OBSERVE " + token;
  for (int attempt = 0; attempt < 6; ++attempt) {
    sendto(s, probe.data(), static_cast<int>(probe.size()), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < until) {
      fd_set rd;
      FD_ZERO(&rd);
      FD_SET(s, &rd);
      timeval tv{0, 50000};
      if (select(static_cast<int>(s) + 1, &rd, nullptr, nullptr, &tv) <= 0) continue;
      char buf[512];
      sockaddr_in from{};
      socklen_t fl = sizeof(from);
      const int n = static_cast<int>(recvfrom(s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl));
      if (n > 0 && from.sin_addr.s_addr == to.sin_addr.s_addr && from.sin_port == to.sin_port) return true;
    }
  }
  return false;
}

struct StandbyCapability {
  std::string token;  // the one-time capability (/api/connect punchToken)
  std::string ip;     // where this lease's datagrams go: the relay (a standby lease reserved there) or the host
  uint16_t port = 0;
};

class StandbyLease {
 public:
  using State = StandbyEntryClient::State;
  // A fresh capability for this host through the lease's own socket; false = none can be had now.
  using CapabilityFn = std::function<bool(SocketHandle socket, StandbyCapability* out)>;
  using Clock = std::function<uint64_t()>;

  StandbyLease(GenerationAllocator& gens, uint32_t helloFeatures, CapabilityFn capability, Clock nowMs)
      : client_(gens, helloFeatures), capability_(std::move(capability)), now_(std::move(nowMs)) {}
  ~StandbyLease() { close_socket(&socket_); }
  StandbyLease(const StandbyLease&) = delete;
  StandbyLease& operator=(const StandbyLease&) = delete;

  bool Open(std::string* error) {
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
  // r3 (N2): every field read under ONE lock (a coherent status for the UI, not five separate reads)
  struct Snapshot {
    State state = State::Idle;
    uint64_t entryId = 0;
    int lost = 0;
    bool handedOff = false;
    bool needsCapability = false;
    // r2 D1: the old session's tail is still arriving (its video / control from the target reached this socket within
    // kTailQuietMs). An observation of what the host still sends -- not a proof that its grant ended when it stops.
    bool tailActive = false;
    uint64_t strays = 0;  // such datagrams so far (the tail's cost, counted apart)
  };
  static constexpr uint64_t kTailQuietMs = 1500;
  Snapshot snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    Snapshot s;
    s.state = client_.state();
    s.entryId = client_.entryId();
    s.lost = lost_;
    s.handedOff = handedOff_;
    s.needsCapability = needsCapability_;
    s.strays = strays_;
    s.tailActive = !handedOff_ && lastStrayMs_ != 0 && now_() - lastStrayMs_ < kTailQuietMs;
    return s;
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
  // r3 N2-b: the socket is the observation's alone while it runs (outside the lock): no pump read, no handoff.
  bool ObserveFrom(const std::string& host, uint16_t port, const std::string& token) {
    SocketHandle s = kInvalidSocket;
    {
      std::unique_lock<std::mutex> lock(mu_);
      ++intents_;  // the pump starts no new read from here
      idle_.wait(lock, [this] { return !reading_ && !observing_; });  // only the read in flight is waited for
      --intents_;
      if (socket_ == kInvalidSocket || handedOff_) {
        idle_.notify_all();
        return false;
      }
      // no read is running here: the pump starts none while an intent waits, and the one in flight has ended
      if (!UntieForNewCapability()) {
        idle_.notify_all();
        return false;
      }
      observing_ = true;
      s = socket_;
    }
    const bool ok = standby_observe_on(s, host, port, token);
    std::lock_guard<std::mutex> lock(mu_);
    observing_ = false;
    idle_.notify_all();
    return ok;
  }

  // A capability the owner got for this lease (after needsCapability()): aim, then the admission.
  bool Provide(const StandbyCapability& cap) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!needsCapability_ || cap.token.empty() || cap.port == 0) return false;
    needsCapability_ = false;
    ip_ = cap.ip;
    port_ = cap.port;
    Apply(client_.Admit(cap.token, now_()));
    return true;
  }

  // Reads what has arrived for up to `waitMs`, feeds it in, keeps time and does what the entry decides.
  // r3: the wait itself is outside the lock (reading_ set) -- a call never queues behind it.
  void Pump(uint32_t waitMs) {
    const uint64_t until = now_() + waitMs;
    do {
      SocketHandle s = kInvalidSocket;
      sockaddr_in want{};
      bool aimed = false;
      {
        std::unique_lock<std::mutex> lock(mu_);
        // r3 N2-b: a handoff / an observation waiting for the socket goes first -- no new read while one is pending
        while (intents_ > 0 || observing_) {
          if (now_() >= until) return;
          idle_.wait_for(lock, std::chrono::milliseconds(20));
        }
        if (socket_ == kInvalidSocket || handedOff_) return;
        if (capabilityAfterRead_) {  // r2 N2: deferred while a read ran -- no read runs now
          capabilityAfterRead_ = false;
          NeedCapability();
          if (socket_ == kInvalidSocket) return;
        }
        s = socket_;
        aimed = Aim(&want);
        reading_ = true;
        TestPoint(kTestReadStart);
      }
      UdpStandbyPacket p{};
      p.magic = 0;
      bool stray = false;
      const bool got = Receive(s, 20, aimed ? &want : nullptr, &p, nullptr, &stray);
      TestPoint(kTestReadDone);  // outside the lock: a test registers a handoff here, while the read is "in flight"
      std::lock_guard<std::mutex> lock(mu_);
      reading_ = false;
      idle_.notify_all();
      if (stray) {
        ++strays_;
        lastStrayMs_ = now_();
      }
      if (got && p.magic == kMagic) Apply(client_.OnStandbyPacket(p, now_()));
      Apply(client_.Tick(now_()));
    } while (now_() < until);
  }

  // The arguments of the ClientSessionController::Connect that activates this entry: its socket (the session owns it
  // from now on) and the activation handshake. Parked only; else false.
  // `keepAlive` (r3 N2): held by the handshake closure -- the owner of this lease (the runner's slot), so the lease lives
  // as long as any copy of the arguments. *op: this activation's number (SessionOver / Abandon name it).
  bool ActivationArgs(ClientSessionConnectArgs* args, std::shared_ptr<void> keepAlive = nullptr, uint64_t* op = nullptr) {
    std::unique_lock<std::mutex> lock(mu_);
    ++intents_;  // r3 N2-b: the pump starts no new read from here; only the read in flight is waited for
    idle_.wait(lock, [this] { return !reading_ && !observing_; });
    --intents_;
    idle_.notify_all();  // a refusal below lets the pump read again
    if (client_.state() != State::Parked || socket_ == kInvalidSocket || port_ == 0 || handedOff_) return false;
    const SocketHandle sessionHandle = Duplicate(socket_);
    if (sessionHandle == kInvalidSocket) {
      Note("socket duplicate failed");
      return false;
    }
    args->host = ip_;
    args->videoPort = port_;
    args->controlPort = port_;
    args->requireUdpHello = true;
    args->requireTcpControl = false;
    args->controlOverUdp = true;
    args->preparedUdpSocket = sessionHandle;  // the session's handle; the lease keeps its own
    args->preparedSocketShared = true;        // ② e: never shut down by the session (POSIX: it is this socket too)
    handedOff_ = true;
    cancel_.store(false, std::memory_order_release);
    const uint64_t thisOp = op_ = NextOp();  // process-wide: a late call naming an old lease's op never matches a new one
    if (op) *op = thisOp;
    args->udpHandshake = [self = this, keep = std::move(keepAlive), thisOp](SocketHandle s, const std::atomic<bool>& stop,
                                                                           std::string* error, uint32_t* ackFeatures) {
      return self->Handshake(thisOp, s, stop, error, ackFeatures);
    };
    return true;
  }

  // The arguments of operation `op` will not be used: its session handle is closed here and the lease takes its socket
  // back. Another operation's: nothing.
  bool Abandon(uint64_t op, SocketHandle* sessionHandle) {
    std::lock_guard<std::mutex> lock(mu_);
    if (op != op_ || !handedOff_ || handshaking_.load(std::memory_order_acquire)) return false;
    if (sessionHandle) close_socket(sessionHandle);
    handedOff_ = false;  // never connected: the session's worker (which connect()s it) did not start
    return true;
  }

  // The session this lease activated ended (or its Connect failed; the session closed its handle): the entry goes on
  // by Ping on the lease's same socket.
  // r3 (N2): only for the operation it names, and never while that operation's handshake still runs.
  bool SessionOver(uint64_t op) {
    std::lock_guard<std::mutex> lock(mu_);
    if (op != op_ || !handedOff_ || handshaking_.load(std::memory_order_acquire)) return false;
    handedOff_ = false;
    // ② e (AVD ke_r3, ke_r4): the session connect()ed its duplicate to its peer, and that association is the SHARED
    // socket's (a POSIX dup, a WSADuplicateSocket duplicate alike). It is NOT dissolved: Windows cannot un-connect a UDP
    // socket (INADDR_ANY:0 -> WSAEADDRNOTAVAIL, AF_UNSPEC -> WSAEAFNOSUPPORT), and on Linux connect(AF_UNSPEC) also
    // gives up an automatically bound port -- the lease would come back on ANOTHER endpoint, which its relay lease does
    // not know (ke_r4: the next activation went nowhere). Tied, it keeps its endpoint, and its peer is still its own
    // target (Pings, an activation and a release go there). Before it aims anywhere else -- an observation for a new
    // capability, which is a new lease at the relay anyway -- it opens a fresh socket (ObserveFrom).
    peerTied_ = true;
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
  }

 private:
  bool Reopen(std::string* error) {
    close_socket(&socket_);
    if (!initialize_sockets(error)) return false;
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == kInvalidSocket) {
      if (error) *error = "udp socket failed";
      return false;
    }
    sockaddr_in any{};
    any.sin_family = AF_INET;
    if (bind(socket_, reinterpret_cast<const sockaddr*>(&any), sizeof(any)) != 0) {
      if (error) *error = "udp bind failed";
      close_socket(&socket_);
      return false;
    }
    return true;
  }

  // Another handle to the same socket (the same local endpoint); closing one leaves the other.
  static SocketHandle Duplicate(SocketHandle s) {
#if defined(_WIN32)
    WSAPROTOCOL_INFOW info{};
    if (WSADuplicateSocketW(s, GetCurrentProcessId(), &info) != 0) return kInvalidSocket;
    return WSASocketW(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, &info, 0, WSA_FLAG_OVERLAPPED);
#else
    return dup(s);
#endif
  }

  bool Aim(sockaddr_in* to) const {
    if (port_ == 0) return false;
    *to = sockaddr_in{};
    to->sin_family = AF_INET;
    to->sin_port = htons(port_);
    return inet_pton(AF_INET, ip_.c_str(), &to->sin_addr) == 1;
  }

  void SendTo(SocketHandle s, const UdpStandbyPacket& p) {
    sockaddr_in to{};
    if (!Aim(&to)) return;
    sendto(s, reinterpret_cast<const char*>(&p), static_cast<int>(sizeof(p)), 0, reinterpret_cast<const sockaddr*>(&to),
           sizeof(to));
  }

  // One datagram from `want` (the lease's target, read under the lock by the caller; null = none yet: the wait only)
  // within `waitMs`: a standby packet (*sb) or a HelloAck (*ack). Takes no lock.
  // *stray (optional): a datagram from `want` that is neither -- the video / control of a session that used this socket
  // and that the host still sends (r2 D1: the end of a session's tail is observed, never assumed).
  static bool Receive(SocketHandle s, uint32_t waitMs, const sockaddr_in* want, UdpStandbyPacket* sb, UdpHelloPacket* ack,
                      bool* stray = nullptr) {
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(s, &rd);
    timeval tv{};
    tv.tv_sec = static_cast<long>(waitMs / 1000);
    tv.tv_usec = static_cast<long>((waitMs % 1000) * 1000);
    if (select(static_cast<int>(s) + 1, &rd, nullptr, nullptr, &tv) <= 0) return false;
    uint8_t buf[2048];
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);
    const int n = static_cast<int>(
        recvfrom(s, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen));
    if (n < 8) return false;
    if (!want || from.sin_addr.s_addr != want->sin_addr.s_addr || from.sin_port != want->sin_port) return false;
    if (stray) *stray = true;  // from the target; cleared below if it is standby / handshake traffic
    uint32_t magic = 0;
    uint16_t kind = 0;
    std::memcpy(&magic, buf, 4);
    std::memcpy(&kind, buf + 4, 2);
    if (magic != kMagic) return false;
    if (n == static_cast<int>(sizeof(UdpStandbyPacket)) && kind >= static_cast<uint16_t>(UdpPacketKind::StandbyHello) &&
        kind <= static_cast<uint16_t>(UdpPacketKind::StandbyRelease)) {
      if (stray) *stray = false;
      if (!sb) return false;
      std::memcpy(sb, buf, sizeof(*sb));
      return true;
    }
    if (kind == static_cast<uint16_t>(UdpPacketKind::HelloAck) && n >= static_cast<int>(sizeof(UdpHelloPacket))) {
      if (stray) *stray = false;
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
    }
    return false;  // video or control before the HelloAck: not the handshake's
  }

  bool Handshake(uint64_t thisOp, SocketHandle s, const std::atomic<bool>& stop, std::string* error, uint32_t* ackFeatures) {
    struct Done {
      std::atomic<bool>* flag = nullptr;
      ~Done() {
        if (flag) flag->store(false, std::memory_order_release);
      }
    } finished;
    StandbyEntryClient::Effects fx;
    sockaddr_in want{};
    bool aimed = false;
    {
      std::lock_guard<std::mutex> lock(mu_);
      aimed = Aim(&want);
      // its operation still handed off (no Abandon / SessionOver / newer activation came first) and not cancelled; the
      // flag is set under the same lock SessionOver / Abandon test it under
      if (thisOp != op_ || !handedOff_ || cancel_.load(std::memory_order_acquire)) {
        if (error) *error = "standby activation cancelled";
        return false;
      }
      handshaking_.store(true, std::memory_order_release);
      finished.flag = &handshaking_;
      fx = client_.Activate(now_());
    }
    bool done = false, failed = false;
    uint32_t features = 0;
    auto run = [&](const StandbyEntryClient::Effects& effects) {
      for (const auto& e : effects) {
        if (e.kind == StandbyEntryClient::Effect::Send) {
          SendTo(s, e.packet);
        } else if (e.kind == StandbyEntryClient::Effect::Activated) {
          done = true;
        } else if (e.kind == StandbyEntryClient::Effect::ActivationFailed || e.kind == StandbyEntryClient::Effect::Lost) {
          failed = true;
          if (error && error->empty()) *error = "standby activation: " + e.reason;
          std::lock_guard<std::mutex> lock(mu_);
          Note(std::string(e.kind == StandbyEntryClient::Effect::Lost ? "lost: " : "activation failed: ") + e.reason);
          if (e.kind == StandbyEntryClient::Effect::Lost) ++lost_;
        }
      }
    };
    if (fx.empty()) {
      if (error) *error = "standby activation: the entry is not parked";
      return false;
    }
    run(fx);
    // r3 N2-a (Codex codex_keepalive_r2_src_r3.md): what arrives is fed in only while THIS operation is still the
    // lease's -- not cancelled (Release / CancelActivation), not stopped, still handed off -- checked under the same lock
    // a Release sets its cancel under. A packet that comes back from a wait a Release finished during is dropped.
    auto valid = [&] {
      return thisOp == op_ && handedOff_ && !cancel_.load(std::memory_order_acquire) &&
             !stop.load(std::memory_order_acquire);
    };
    bool aborted = false;  // the operation stopped being valid: the handshake ends (nothing more is fed in)
    auto step = [&](const std::function<StandbyEntryClient::Effects()>& f) {
      StandbyEntryClient::Effects e;
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (!valid()) {
          aborted = true;
          return;
        }
        e = f();
      }
      run(e);
    };
    while (!done && !failed && !aborted && !stop.load(std::memory_order_acquire) &&
           !cancel_.load(std::memory_order_acquire)) {
      UdpStandbyPacket sb{};
      UdpHelloPacket ack{};
      sb.magic = 0;
      ack.magic = 0;
      if (Receive(s, 20, aimed ? &want : nullptr, &sb, &ack)) {  // no lock across the wait
        if (sb.magic == kMagic) step([&] { return client_.OnStandbyPacket(sb, now_()); });
        if (ack.magic == kMagic) {
          features = ack.features;  // passed udp_hello_ack_acceptable + size (Receive)
          step([&] { return client_.OnHelloAck(now_()); });
        }
      }
      step([&] { return client_.Tick(now_()); });
    }
    std::lock_guard<std::mutex> lock(mu_);
    // r3 N2-a: success only if the operation is still valid NOW, under the lock -- a Release / stop that finished
    // before this point wins: no true, no features
    if (done && valid()) {
      Note("activated gen=" + std::to_string(client_.activationGen()));
      if (ackFeatures) *ackFeatures = features;
      return true;
    }
    if (done) {
      // completed after it was cancelled: not adopted. The host may hold a grant for it -- that ends with this
      // session's end (Connect fails, its SessionOver ends the entry's activation: the lease pings again).
      Note("activation completed after cancel: not adopted");
      if (error && error->empty()) *error = "standby activation cancelled";
      return false;
    }
    if (!failed) {
      // stopped (Disconnect) or cancelled in the middle: this activation is dead; its late answers adopt nothing
      Apply(client_.CancelActivation(now_()));
      Note("activation cancelled");
      if (error && error->empty()) *error = "standby activation stopped";
    }
    return false;
  }

  void NeedCapability() {
    StandbyCapability cap;
    // r2 (Codex codex_ka_e_src_r1.md N2): a synchronous CapabilityFn observes from the socket it is given -- never one
    // still tied to a session's peer (its directory answer would not come back). The socket is replaced only while no
    // read runs on it (a descriptor closed under a read could come back as the new one's number): a read in flight
    // defers this to the pump's next turn, before it starts another read.
    if (peerTied_ && reading_) {
      capabilityAfterRead_ = true;
      return;
    }
    if (!UntieForNewCapability()) {
      needsCapability_ = true;
      return;
    }
    if (socket_ == kInvalidSocket || !capability_ || !capability_(socket_, &cap) || cap.token.empty() || cap.port == 0) {
      Note("no capability now");
      needsCapability_ = true;  // the owner may hand one in (Provide); the entry waits for it -- nothing is sent
      return;
    }
    needsCapability_ = false;
    ip_ = cap.ip;
    port_ = cap.port;
    Apply(client_.Admit(cap.token, now_()));
  }

  // A new capability is a new lease at the relay (or a new admission at the host) anyway: a socket a session tied to its
  // peer is replaced by a fresh one first -- for every way to one (ObserveFrom for an async owner, NeedCapability for a
  // synchronous CapabilityFn). Steady reuse never comes here: the same endpoint stays. Under mu_. false = no socket.
  bool UntieForNewCapability() {
    if (!peerTied_) return true;
    peerTied_ = false;
    std::string error;
    if (!Reopen(&error)) {
      Note("reopen failed: " + error);
      return false;
    }
    Note("socket reopened (tied to a session's peer)");
    return true;
  }

  void Apply(const StandbyEntryClient::Effects& fx) {
    for (const auto& e : fx) {
      switch (e.kind) {
        case StandbyEntryClient::Effect::Send:
          if (socket_ != kInvalidSocket) SendTo(socket_, e.packet);
          break;
        case StandbyEntryClient::Effect::NeedCapability:
          Note("capability again: " + e.reason);
          NeedCapability();
          break;
        case StandbyEntryClient::Effect::Unsupported:
          Note("unsupported: " + e.reason);
          break;
        case StandbyEntryClient::Effect::Lost:
          ++lost_;
          Note("lost: " + e.reason);
          break;
        default:
          break;
      }
    }
  }

  static uint64_t NextOp() {
    static std::atomic<uint64_t> next{0};
    return next.fetch_add(1, std::memory_order_relaxed) + 1;
  }

  static constexpr size_t kEventsKept = 32;
  void Note(const std::string& e) {
    events_.push_back(e);
    while (events_.size() > kEventsKept) events_.pop_front();
  }

  mutable std::mutex mu_;
  std::condition_variable idle_;  // reading_ / observing_ went false, an intent was withdrawn
  bool reading_ = false;          // the pump waits on socket_ (outside the lock)
  bool observing_ = false;        // ObserveFrom reads socket_ (outside the lock)
  bool peerTied_ = false;         // a session connect()ed this socket to its peer: reopen before aiming elsewhere
  uint64_t strays_ = 0;           // datagrams from the target that were not standby traffic (a session's tail)
  bool capabilityAfterRead_ = false;  // NeedCapability waits for the pump's read in flight (r2 N2)
  uint64_t lastStrayMs_ = 0;
  int intents_ = 0;               // handoffs / observations waiting for the socket: no new pump read meanwhile

 public:
  // TEST BUILD ONLY (REMOTE60_STANDBY_TEST_HOOKS): called at the pump's read start (under the lock, with the pending
  // intents) and read end (outside it). Not compiled into the product (build gate: no such member without the define).
  static constexpr int kTestReadStart = 1;
  static constexpr int kTestReadDone = 2;
#ifdef REMOTE60_STANDBY_TEST_HOOKS
  std::function<void(int point, int intents)> testHook;
  int pendingIntentsForTest() const {
    std::lock_guard<std::mutex> lock(mu_);
    return intents_;
  }
#endif

 private:
  void TestPoint(int point) {
#ifdef REMOTE60_STANDBY_TEST_HOOKS
    if (testHook) testHook(point, point == kTestReadStart ? intents_ + (observing_ ? 1000 : 0) : -1);  // under the lock only
#else
    (void)point;
#endif
  }
  std::atomic<bool> cancel_{false};
  std::atomic<bool> handshaking_{false};
  uint64_t op_ = 0;
  StandbyEntryClient client_;
  CapabilityFn capability_;
  Clock now_;
  SocketHandle socket_ = kInvalidSocket;
  std::string ip_;
  uint16_t port_ = 0;
  bool handedOff_ = false;
  bool needsCapability_ = false;
  int lost_ = 0;
  std::deque<std::string> events_;
};

}  // namespace remote60::native_poc

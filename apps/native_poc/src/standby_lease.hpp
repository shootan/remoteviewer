#pragma once

// keepalive-r2 ② (c, d): one host's standby lease on THIS side -- the one UDP socket of the lease (contract §2.7: a
// host lease, one socket; two hosts are never multiplexed on one) and the StandbyEntryClient that decides what goes on
// it. The caller pumps it (Pump) from whatever thread owns the lease; nothing here starts a thread.
//
// Activation is a CONNECT MODE of ClientSessionController, not another session type: ActivationArgs() hands the
// lease's socket to the session (ClientSessionConnectArgs::preparedUdpSocket, which the session then owns) with a
// handshake that sends StandbyActivate instead of the Hello and returns only when this activation's own Pong
// (Granted) AND the HelloAck have arrived, in either order (StandbyEntryClient). Everything after is the session's
// ordinary path. Until the session ends the lease does not touch the socket.
//
// Limit (said, not hidden): when the session ends -- or a failed activation closes the socket -- the lease opens a
// NEW socket and keeps the entry by Ping (the host follows an entry's endpoint by its secret, §2.2). Directly that is
// enough; through a relay the lease is bound to the old client endpoint, so the Pings die and the entry is re-admitted
// with a new capability (bounded backoff). Keeping the socket across the session is ③'s.

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "native_socket.hpp"
#include "native_video_client_session.hpp"
#include "poc_protocol.hpp"
#include "standby_entry_client.hpp"

namespace remote60::native_poc {

class StandbyLease {
 public:
  using State = StandbyEntryClient::State;
  // A fresh one-time capability for this host (a directory connect), or empty when none can be had now.
  using CapabilityFn = std::function<std::string()>;
  using Clock = std::function<uint64_t()>;

  StandbyLease(GenerationAllocator& gens, uint32_t helloFeatures, CapabilityFn capability, Clock nowMs)
      : client_(gens, helloFeatures), capability_(std::move(capability)), now_(std::move(nowMs)) {}
  ~StandbyLease() { close_socket(&socket_); }
  StandbyLease(const StandbyLease&) = delete;
  StandbyLease& operator=(const StandbyLease&) = delete;

  // The lease's socket, aimed at the host (direct: the host's address; relay: the lease's relay address).
  bool Open(const std::string& hostIp, uint16_t port, std::string* error) {
    hostIp_ = hostIp;
    port_ = port;
    return Reopen(error);
  }

  State state() const { return client_.state(); }
  uint64_t entryId() const { return client_.entryId(); }
  int lostCount() const { return lost_; }
  const std::vector<std::string>& events() const { return events_; }

  // Start (or restart) the admission with a capability from CapabilityFn.
  void Start() { NeedCapability(); }

  // Reads what has arrived for up to `waitMs`, feeds it in, keeps time and does what the entry decides.
  void Pump(uint32_t waitMs) {
    if (socket_ == kInvalidSocket || handedOff_) return;
    const uint64_t until = now_() + waitMs;
    do {
      UdpStandbyPacket p{};
      if (Receive(socket_, 20, &p, nullptr)) Apply(client_.OnStandbyPacket(p, now_()));
      Apply(client_.Tick(now_()));
    } while (now_() < until);
  }

  // The arguments of the ClientSessionController::Connect that activates this entry: its socket (the session owns it
  // from now on) and the activation handshake. Parked only; else false.
  bool ActivationArgs(ClientSessionConnectArgs* args) {
    if (client_.state() != State::Parked || socket_ == kInvalidSocket) return false;
    args->host = hostIp_;
    args->videoPort = port_;
    args->controlPort = port_;
    args->requireUdpHello = true;
    args->requireTcpControl = false;
    args->controlOverUdp = true;
    args->preparedUdpSocket = socket_;
    socket_ = kInvalidSocket;  // the session's now
    handedOff_ = true;
    args->udpHandshake = [this](SocketHandle s, const std::atomic<bool>& stop, std::string* error, uint32_t* ackFeatures) {
      return Handshake(s, stop, error, ackFeatures);
    };
    return true;
  }

  // The session this lease activated ended (or its Connect failed): a new socket, and the entry goes on by Ping.
  void SessionOver() {
    handedOff_ = false;
    if (client_.state() == State::Active) client_.SessionEnded(now_());
    std::string error;
    if (!Reopen(&error)) events_.push_back("reopen failed: " + error);
  }

  // The user gave up the activation in flight (Connect still running): its late answers adopt nothing.
  void CancelActivation() { Apply(client_.CancelActivation(now_())); }

 private:
  bool Reopen(std::string* error) {
    close_socket(&socket_);
    if (!initialize_sockets(error)) return false;
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == kInvalidSocket) {
      if (error) *error = "udp socket failed";
      return false;
    }
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port_);
    if (inet_pton(AF_INET, hostIp_.c_str(), &to.sin_addr) != 1 ||
        connect(socket_, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) != 0) {
      if (error) *error = "udp connect to the host failed";
      close_socket(&socket_);
      return false;
    }
    return true;
  }

  // One datagram within `waitMs`: a standby packet (*sb) or a HelloAck (*ack). false: nothing of either.
  static bool Receive(SocketHandle s, uint32_t waitMs, UdpStandbyPacket* sb, UdpHelloPacket* ack) {
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(s, &rd);
    timeval tv{};
    tv.tv_sec = static_cast<long>(waitMs / 1000);
    tv.tv_usec = static_cast<long>((waitMs % 1000) * 1000);
    if (select(static_cast<int>(s) + 1, &rd, nullptr, nullptr, &tv) <= 0) return false;
    uint8_t buf[2048];
    const int n = static_cast<int>(recv(s, reinterpret_cast<char*>(buf), sizeof(buf), 0));
    if (n < 8) return false;
    uint32_t magic = 0;
    uint16_t kind = 0;
    std::memcpy(&magic, buf, 4);
    std::memcpy(&kind, buf + 4, 2);
    if (magic != kMagic) return false;
    if (n == static_cast<int>(sizeof(UdpStandbyPacket)) && kind >= static_cast<uint16_t>(UdpPacketKind::StandbyHello) &&
        kind <= static_cast<uint16_t>(UdpPacketKind::StandbyRelease)) {
      if (!sb) return false;
      std::memcpy(sb, buf, sizeof(*sb));
      return true;
    }
    if (kind == static_cast<uint16_t>(UdpPacketKind::HelloAck) && n >= static_cast<int>(sizeof(UdpHelloPacket))) {
      if (!ack) return false;
      std::memcpy(ack, buf, sizeof(*ack));
      return true;
    }
    return false;  // video or control before the HelloAck: not the handshake's
  }

  bool Handshake(SocketHandle s, const std::atomic<bool>& stop, std::string* error, uint32_t* ackFeatures) {
    StandbyEntryClient::Effects fx = client_.Activate(now_());
    bool done = false, failed = false;
    uint32_t features = 0;
    auto run = [&](const StandbyEntryClient::Effects& effects) {
      for (const auto& e : effects) {
        if (e.kind == StandbyEntryClient::Effect::Send) {
          send(s, reinterpret_cast<const char*>(&e.packet), static_cast<int>(sizeof(e.packet)), 0);
        } else if (e.kind == StandbyEntryClient::Effect::Activated) {
          done = true;
        } else if (e.kind == StandbyEntryClient::Effect::ActivationFailed || e.kind == StandbyEntryClient::Effect::Lost) {
          failed = true;
          if (error && error->empty()) *error = "standby activation: " + e.reason;
          events_.push_back(std::string(e.kind == StandbyEntryClient::Effect::Lost ? "lost: " : "activation failed: ") +
                            e.reason);
          if (e.kind == StandbyEntryClient::Effect::Lost) ++lost_;
        }
      }
    };
    if (fx.empty()) {
      if (error) *error = "standby activation: the entry is not parked";
      return false;
    }
    run(fx);
    while (!done && !failed && !stop.load(std::memory_order_acquire)) {
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
    return false;
  }

  void NeedCapability() {
    const std::string cap = capability_ ? capability_() : std::string();
    if (cap.empty()) {
      events_.push_back("no capability now");
      return;
    }
    Apply(client_.Admit(cap, now_()));
  }

  void Apply(const StandbyEntryClient::Effects& fx) {
    for (const auto& e : fx) {
      switch (e.kind) {
        case StandbyEntryClient::Effect::Send:
          if (socket_ != kInvalidSocket) {
            send(socket_, reinterpret_cast<const char*>(&e.packet), static_cast<int>(sizeof(e.packet)), 0);
          }
          break;
        case StandbyEntryClient::Effect::NeedCapability:
          NeedCapability();
          break;
        case StandbyEntryClient::Effect::Unsupported:
          events_.push_back("unsupported: " + e.reason);
          break;
        case StandbyEntryClient::Effect::Lost:
          ++lost_;
          events_.push_back("lost: " + e.reason);
          break;
        default:
          break;
      }
    }
  }

  StandbyEntryClient client_;
  CapabilityFn capability_;
  Clock now_;
  SocketHandle socket_ = kInvalidSocket;
  std::string hostIp_;
  uint16_t port_ = 0;
  bool handedOff_ = false;
  int lost_ = 0;
  std::vector<std::string> events_;
};

}  // namespace remote60::native_poc

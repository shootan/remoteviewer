#pragma once

// keepalive-r2 ② e (kept model A, codex_keepalive_r2_kept_model.md): a session that a standby lease activates. The
// app's tap on a PC whose lease is ready makes a NEW session entry (its own controller, its own connect gate) and
// connects it with the lease's activation arguments (StandbyRunner::TakeActivation) -- no directory, no observe, no
// punch, no ordinary Hello. The lease is not a session: it stays outside the session table, and the session gives
// its socket back when it is over.
//
// The one contract (StandbyRunner, r3 N2) at its product call site:
//  - the arguments are used exactly once: Connect (the session owns the duplicated handle), or -- a connect that does
//    not start (its operation went stale first, or Connect refused before its worker) -- Abandon (the lease closes the
//    handle and takes the socket back);
//  - after the session is over -- its Disconnect RETURNED (the worker and the video reader joined) -- SessionOver of
//    exactly that activation: the lease goes on by Ping on the same socket. StandbySessionLink::Over is called right
//    after every Disconnect of the entry (the gate's stale disconnect, CloseIfOwn, the entry's Close); once only.
// Portable: the APK bridge (native_bridge.cpp) and the desktop e2e use this same function.

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "connect_op_gate.hpp"
#include "native_video_client_session.hpp"
#include "standby_runner.hpp"

namespace remote60::native_poc {

// Which activation an entry's session came from (none: an ordinary connect). Over() after its Disconnect returned.
class StandbySessionLink {
 public:
  void Set(StandbyRunner* runner, const std::string& key, uint64_t op) {
    std::lock_guard<std::mutex> lock(mu_);
    runner_ = runner;
    key_ = key;
    op_ = op;
  }
  // The arguments were abandoned (the lease has its socket back): nothing to say later.
  void Clear() {
    std::lock_guard<std::mutex> lock(mu_);
    op_ = 0;
  }
  // The session is over (its Disconnect returned / its Connect failed): that activation's SessionOver, once.
  // true = the lease took it (false: none, already said, or the lease is gone / moved on -- nothing to do).
  bool Over() {
    StandbyRunner* r = nullptr;
    std::string key;
    uint64_t op = 0;
    {
      std::lock_guard<std::mutex> lock(mu_);
      r = runner_;
      key = key_;
      op = op_;
      op_ = 0;
    }
    return r && op != 0 && r->SessionOver(key, op);
  }
  bool active() const {
    std::lock_guard<std::mutex> lock(mu_);
    return op_ != 0;
  }
  std::string key() const {
    std::lock_guard<std::mutex> lock(mu_);
    return key_;
  }

 private:
  mutable std::mutex mu_;
  StandbyRunner* runner_ = nullptr;
  std::string key_;
  uint64_t op_ = 0;
};

struct StandbyAdoptResult {
  uint64_t generation = 0;  // the session's generation (0: not adopted)
  bool taken = false;       // the lease gave activation arguments (false: not ready -- parked and not handed off)
  std::string why;
};

// The connect of `controller` (the entry's) by `key`'s lease, through the entry's gate under operation `owner`.
// `prepare` (optional) runs on the arguments before Connect (the frame sink, the clipboard switch...). `connect`
// (optional, tests) replaces controller.Connect.
inline StandbyAdoptResult standby_adopt(ConnectOpGate& gate, int64_t owner, StandbyRunner& runner, const std::string& key,
                                        ClientSessionController& controller, StandbySessionLink& link,
                                        const std::function<void(ClientSessionConnectArgs&)>& prepare = nullptr,
                                        const std::function<bool(const ClientSessionConnectArgs&)>& connect = nullptr) {
  StandbyAdoptResult out;
  ClientSessionConnectArgs args{};
  uint64_t op = 0;
  if (!runner.TakeActivation(key, &args, &op)) {
    out.why = "standby_not_ready";
    return out;
  }
  out.taken = true;
  if (prepare) prepare(args);
  link.Set(&runner, key, op);
  auto abandon = [&] {
    runner.Abandon(key, op, &args);  // closes the session handle, the lease reads its socket again
    link.Clear();
  };
  ConnectOpGate::Steps steps;
  steps.connect = [&] {
    const bool ok = connect ? connect(args) : controller.Connect(args);
    if (!ok) abandon();  // refused before its worker: the handle is still ours (the handshake never ran)
    return ok;
  };
  steps.generation = [&] { return controller.ConnectionGeneration(); };
  steps.disconnect = [&] {  // made by an operation that went stale meanwhile: never handed over
    controller.Disconnect();
    link.Over();
  };
  steps.closePrepared = abandon;  // stale before the connect: the arguments are never used
  out.generation = gate.Adopt(owner, steps);
  if (out.generation == 0 && out.why.empty()) out.why = gate.IsCurrent(owner) ? "connect_failed" : "stale_operation";
  return out;
}

}  // namespace remote60::native_poc

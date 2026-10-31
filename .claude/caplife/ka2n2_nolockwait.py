SRC = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'


def patch(name, reps):
    p = SRC + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, a[:70], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


patch('standby_lease.hpp', [
("#include <atomic>\n#include <chrono>\n", "#include <atomic>\n#include <chrono>\n#include <condition_variable>\n"),
("// Every method takes the lease's lock; the handshake takes it per step (never across a receive wait).\n",
 """// Every method takes the lease's lock; nothing holds it across a receive wait (the pump and the handshake take it per
// step), so a call from the UI never queues behind a wait. One reader at a time: the pump marks its wait (reading_), and
// ActivationArgs / ObserveFrom wait it out (at most one 20 ms wait) before the socket changes hands.
"""),
("""  bool ObserveFrom(const std::string& host, uint16_t port, const std::string& token) {
    std::lock_guard<std::mutex> lock(mu_);
    return""", """  bool ObserveFrom(const std::string& host, uint16_t port, const std::string& token) {
    std::unique_lock<std::mutex> lock(mu_);
    idle_.wait(lock, [this] { return !reading_; });  // the pump's wait ends first: its reply is not the pump's to read
    return"""),
("""  void Pump(uint32_t waitMs) {
    const uint64_t until = now_() + waitMs;
    do {
      std::lock_guard<std::mutex> lock(mu_);
      if (socket_ == kInvalidSocket || handedOff_) return;
      UdpStandbyPacket p{};
      p.magic = 0;
      if (Receive(socket_, 20, &p, nullptr) && p.magic == kMagic) Apply(client_.OnStandbyPacket(p, now_()));
      Apply(client_.Tick(now_()));
    } while (now_() < until);
  }""", """  // r3: the wait itself is outside the lock (reading_ set) -- a call never queues behind it.
  void Pump(uint32_t waitMs) {
    const uint64_t until = now_() + waitMs;
    do {
      SocketHandle s = kInvalidSocket;
      sockaddr_in want{};
      bool aimed = false;
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (socket_ == kInvalidSocket || handedOff_) return;
        s = socket_;
        aimed = Aim(&want);
        reading_ = true;
      }
      UdpStandbyPacket p{};
      p.magic = 0;
      const bool got = Receive(s, 20, aimed ? &want : nullptr, &p, nullptr);
      std::lock_guard<std::mutex> lock(mu_);
      reading_ = false;
      idle_.notify_all();
      if (got && p.magic == kMagic) Apply(client_.OnStandbyPacket(p, now_()));
      Apply(client_.Tick(now_()));
    } while (now_() < until);
  }"""),
("""  bool ActivationArgs(ClientSessionConnectArgs* args, std::shared_ptr<void> keepAlive = nullptr, uint64_t* op = nullptr) {
    std::lock_guard<std::mutex> lock(mu_);
""", """  bool ActivationArgs(ClientSessionConnectArgs* args, std::shared_ptr<void> keepAlive = nullptr, uint64_t* op = nullptr) {
    std::unique_lock<std::mutex> lock(mu_);
    idle_.wait(lock, [this] { return !reading_; });  // the pump's wait ends first: from here only the session reads
"""),
("""  // One datagram from the lease's target within `waitMs`: a standby packet (*sb) or a HelloAck (*ack).
  bool Receive(SocketHandle s, uint32_t waitMs, UdpStandbyPacket* sb, UdpHelloPacket* ack) const {""",
 """  // One datagram from `want` (the lease's target, read under the lock by the caller; null = none yet: the wait only)
  // within `waitMs`: a standby packet (*sb) or a HelloAck (*ack). Takes no lock.
  static bool Receive(SocketHandle s, uint32_t waitMs, const sockaddr_in* want, UdpStandbyPacket* sb, UdpHelloPacket* ack) {"""),
("""    if (n < 8) return false;
    sockaddr_in want{};
    if (!Aim(&want) || from.sin_addr.s_addr != want.sin_addr.s_addr || from.sin_port != want.sin_port) return false;""",
 """    if (n < 8) return false;
    if (!want || from.sin_addr.s_addr != want->sin_addr.s_addr || from.sin_port != want->sin_port) return false;"""),
("""    StandbyEntryClient::Effects fx;
    {
      std::lock_guard<std::mutex> lock(mu_);
      // its operation still handed off""", """    StandbyEntryClient::Effects fx;
    sockaddr_in want{};
    bool aimed = false;
    {
      std::lock_guard<std::mutex> lock(mu_);
      aimed = Aim(&want);
      // its operation still handed off"""),
("      if (Receive(s, 20, &sb, &ack)) {  // no lock across the wait",
 "      if (Receive(s, 20, aimed ? &want : nullptr, &sb, &ack)) {  // no lock across the wait"),
("""  mutable std::mutex mu_;
  std::atomic<bool> cancel_{false};""", """  mutable std::mutex mu_;
  std::condition_variable idle_;  // reading_ went false
  bool reading_ = false;          // the pump waits on socket_ (outside the lock)
  std::atomic<bool> cancel_{false};"""),
])

patch('standby_runner.hpp', [
("""// Locks: mu_ guards the map only; each slot's mu is held by its pump and by every call on it (order: slot, then the
// lease's own) -- never across the activation handshake, which runs on the session's connect thread and takes only the
// lease's lock, per step.
""", """// Locks: mu_ guards the map only; each slot's mu serializes the calls on it (order: slot, then the lease's own). The
// pump holds no slot lock: the lease locks itself per step and never across a wait, so a call (the UI thread's tap,
// leaving the screen) never queues behind the pump. The handshake runs on the session's connect thread and takes only
// the lease's lock, per step.
"""),
("""      while (!raw->stop.load(std::memory_order_acquire)) {
        bool idle = false;
        {
          std::lock_guard<std::mutex> l(raw->mu);
          raw->lease->Pump(100);  // returns at once while handed off: the session's reader has the socket
          Publish(*raw);
          std::lock_guard<std::mutex> sl(raw->statusMu);
          idle = raw->status.handedOff;
        }
        if (idle) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }""", """      while (!raw->stop.load(std::memory_order_acquire)) {
        raw->lease->Pump(100);  // returns at once while handed off: the session's reader has the socket
        if (Publish(*raw).handedOff) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }"""),
("""  static void Publish(Slot& s) {
    const StandbyLease::Snapshot n = s.lease->snapshot();
    std::lock_guard<std::mutex> l(s.statusMu);
    s.status.exists = true;""", """  // the snapshot is taken under statusMu (order: statusMu, then the lease's): a later Publish never loses to an older one
  static Status Publish(Slot& s) {
    std::lock_guard<std::mutex> l(s.statusMu);
    const StandbyLease::Snapshot n = s.lease->snapshot();
    s.status.exists = true;"""),
("""    s.status.needsCapability = n.needsCapability;
  }""", """    s.status.needsCapability = n.needsCapability;
    return s.status;
  }"""),
])
print('ok')

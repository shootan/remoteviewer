p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_lease.hpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
reps = [
# header comment: precise scope of the no-lock claim
("""// Every method takes the lease's lock; nothing holds it across a receive wait (the pump and the handshake take it per
// step), so a call from the UI never queues behind a wait. One reader at a time: the pump marks its wait (reading_), and
// ActivationArgs / ObserveFrom wait it out (at most one 20 ms wait) before the socket changes hands.
""", """// Every method takes the lease's lock. The pump's and the handshake's receive waits are outside it (per step), and so
// is ObserveFrom's observation. NOT outside it: Start / a NeedCapability effect call the CapabilityFn under the lock
// (the APK's is empty: it returns at once; a desktop one does its directory calls there).
// One reader at a time (r3 N2-b, Codex codex_keepalive_r2_src_r3.md): ActivationArgs / ObserveFrom register their
// intent under the lock (intents_) and the pump starts no new read while one is pending -- they wait for the read in
// flight only, never for a next one; an observation in progress (observing_) holds the pump and a handoff off too.
"""),
("""  bool ObserveFrom(const std::string& host, uint16_t port, const std::string& token) {
    std::unique_lock<std::mutex> lock(mu_);
    idle_.wait(lock, [this] { return !reading_; });  // the pump's wait ends first: its reply is not the pump's to read
    return socket_ != kInvalidSocket && !handedOff_ && standby_observe_on(socket_, host, port, token);
  }""", """  // r3 N2-b: the socket is the observation's alone while it runs (outside the lock): no pump read, no handoff.
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
      observing_ = true;
      s = socket_;
    }
    const bool ok = standby_observe_on(s, host, port, token);
    std::lock_guard<std::mutex> lock(mu_);
    observing_ = false;
    idle_.notify_all();
    return ok;
  }"""),
("""      {
        std::lock_guard<std::mutex> lock(mu_);
        if (socket_ == kInvalidSocket || handedOff_) return;
        s = socket_;
        aimed = Aim(&want);
        reading_ = true;
      }
      UdpStandbyPacket p{};
      p.magic = 0;
      const bool got = Receive(s, 20, aimed ? &want : nullptr, &p, nullptr);
      std::lock_guard<std::mutex> lock(mu_);""", """      {
        std::unique_lock<std::mutex> lock(mu_);
        // r3 N2-b: a handoff / an observation waiting for the socket goes first -- no new read while one is pending
        while (intents_ > 0 || observing_) {
          if (now_() >= until) return;
          idle_.wait_for(lock, std::chrono::milliseconds(20));
        }
        if (socket_ == kInvalidSocket || handedOff_) return;
        s = socket_;
        aimed = Aim(&want);
        reading_ = true;
        TestPoint(kTestReadStart);
      }
      UdpStandbyPacket p{};
      p.magic = 0;
      const bool got = Receive(s, 20, aimed ? &want : nullptr, &p, nullptr);
      TestPoint(kTestReadDone);  // outside the lock: a test registers a handoff here, while the read is "in flight"
      std::lock_guard<std::mutex> lock(mu_);"""),
("""    std::unique_lock<std::mutex> lock(mu_);
    idle_.wait(lock, [this] { return !reading_; });  // the pump's wait ends first: from here only the session reads
""", """    std::unique_lock<std::mutex> lock(mu_);
    ++intents_;  // r3 N2-b: the pump starts no new read from here; only the read in flight is waited for
    idle_.wait(lock, [this] { return !reading_ && !observing_; });
    --intents_;
    idle_.notify_all();  // a refusal below lets the pump read again
"""),
# N2-a: packets fed and the result taken only while the operation is still valid, under the lock
("""    auto step = [&](const std::function<StandbyEntryClient::Effects()>& f) {
      StandbyEntryClient::Effects e;
      {
        std::lock_guard<std::mutex> lock(mu_);
        e = f();
      }
      run(e);
    };""", """    // r3 N2-a (Codex codex_keepalive_r2_src_r3.md): what arrives is fed in only while THIS operation is still the
    // lease's -- not cancelled (Release / CancelActivation), not stopped, still handed off -- checked under the same lock
    // a Release sets its cancel under. A packet that comes back from a wait a Release finished during is dropped.
    auto valid = [&] {
      return thisOp == op_ && handedOff_ && !cancel_.load(std::memory_order_acquire) &&
             !stop.load(std::memory_order_acquire);
    };
    auto step = [&](const std::function<StandbyEntryClient::Effects()>& f) {
      StandbyEntryClient::Effects e;
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (!valid()) return;
        e = f();
      }
      run(e);
    };"""),
("""    std::lock_guard<std::mutex> lock(mu_);
    if (done) {
      Note("activated gen=" + std::to_string(client_.activationGen()));
      if (ackFeatures) *ackFeatures = features;
      return true;
    }
    if (!failed) {""", """    std::lock_guard<std::mutex> lock(mu_);
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
    if (!failed) {"""),
("""  std::condition_variable idle_;  // reading_ went false
  bool reading_ = false;          // the pump waits on socket_ (outside the lock)
""", """  std::condition_variable idle_;  // reading_ / observing_ went false, an intent was withdrawn
  bool reading_ = false;          // the pump waits on socket_ (outside the lock)
  bool observing_ = false;        // ObserveFrom reads socket_ (outside the lock)
  int intents_ = 0;               // handoffs / observations waiting for the socket: no new pump read meanwhile

 public:
  // TEST BUILD ONLY (REMOTE60_STANDBY_TEST_HOOKS): called at the pump's read start (under the lock, with the pending
  // intents) and read end (outside it). Not compiled into the product (build gate: no such member without the define).
  static constexpr int kTestReadStart = 1;
  static constexpr int kTestReadDone = 2;
#ifdef REMOTE60_STANDBY_TEST_HOOKS
  std::function<void(int point, int intents)> testHook;
#endif

 private:
  void TestPoint(int point) {
#ifdef REMOTE60_STANDBY_TEST_HOOKS
    if (testHook) testHook(point, intents_);
#else
    (void)point;
#endif
  }
"""),
]
for a, b in reps:
    assert s.count(a) == 1, a[:70]
    s = s.replace(a, b)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')

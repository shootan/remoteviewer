p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_runner.hpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
rep = [
("""//  - Start(key): a lease for that PC (its capability from the factory), admitted and kept by Ping on its thread.
//  - Status(key): a snapshot the pump thread publishes (no lease lock on the caller's thread -- the UI polls it).
//  - TakeActivation(key, args): the arguments of the ClientSessionController::Connect that activates it (the lease's
//    socket, duplicated, and the activation handshake); the pump stops touching the socket until SessionOver(key).
//  - Release(key): the PC's standby goes (one StandbyRelease if parked), its thread ends, the lease is closed.
// Locks: mu_ guards the map only; each lease has its own mutex, held by its pump and by every call on it -- never
// across the activation handshake, which runs on the session's connect thread while the pump leaves the lease alone.
""", """//  - Start(key): a lease for that PC (its capability from the factory, or handed in: Observe + Provide), admitted and
//    kept by Ping on its thread.
//  - StatusOf(key): one coherent snapshot (StandbyLease::snapshot, one lock) the pump publishes; the UI polls it.
//  - TakeActivation(key, args, &op): the arguments of the ClientSessionController::Connect that activates it (the
//    lease's socket, duplicated, and the activation handshake); the pump stops touching the socket until SessionOver.
//    r3 (Codex codex_keepalive_r2_scope2.md N2): the handshake closure in the arguments holds the slot (shared
//    ownership), so the lease outlives every copy of them even after Release / ReleaseAll removed it here. Then exactly
//    one of: Connect (the session owns the duplicated handle), or Abandon(key, op, args) (closed here). After the
//    session -- Connect failed, or Disconnect RETURNED -- SessionOver(key, op). Another op's SessionOver / Abandon (a
//    late one; a new lease under the same key) does nothing; while the handshake runs both are refused.
//  - Release(key) / ReleaseAll: the pump ends, a parked entry is released at the host (one StandbyRelease), a handshake
//    in flight is cancelled (it returns false; nothing of it is adopted). The slot goes with its last holder.
// Locks: mu_ guards the map only; each slot's mu is held by its pump and by every call on it (order: slot, then the
// lease's own) -- never across the activation handshake, which runs on the session's connect thread and takes only the
// lease's lock, per step.
"""),
("""    slot->thread = std::thread([slot] {
      while (!slot->stop.load(std::memory_order_acquire)) {
        {
          std::lock_guard<std::mutex> l(slot->mu);
          if (!slot->handedOff) {
            slot->lease->Pump(100);
            Publish(*slot);
          }
        }
        if (slot->handedOff) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    });""", """    // raw: Stop joins this thread before the runner lets the slot go (a slot whose last holder is an activation's
    // arguments has no thread left)
    Slot* raw = slot.get();
    slot->thread = std::thread([raw] {
      while (!raw->stop.load(std::memory_order_acquire)) {
        bool idle = false;
        {
          std::lock_guard<std::mutex> l(raw->mu);
          raw->lease->Pump(100);  // returns at once while handed off: the session's reader has the socket
          Publish(*raw);
          std::lock_guard<std::mutex> sl(raw->statusMu);
          idle = raw->status.handedOff;
        }
        if (idle) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    });"""),
("""    Status s;
    s.exists = true;
    s.state = static_cast<StandbyEntryClient::State>(slot->state.load(std::memory_order_acquire));
    s.entryId = slot->entryId.load(std::memory_order_acquire);
    s.lost = slot->lost.load(std::memory_order_acquire);
    s.handedOff = slot->handedOff.load(std::memory_order_acquire);
    s.needsCapability = slot->needsCapability.load(std::memory_order_acquire);
    return s;
  }

  bool TakeActivation(const std::string& key, ClientSessionConnectArgs* args) {
    std::shared_ptr<Slot> slot = Find(key);
    if (!slot) return false;
    std::lock_guard<std::mutex> l(slot->mu);
    if (slot->handedOff || !slot->lease->ActivationArgs(args)) return false;
    slot->handedOff = true;
    slot->state.store(static_cast<int>(StandbyEntryClient::State::Activating), std::memory_order_release);
    return true;
  }
""", """    std::lock_guard<std::mutex> l(slot->statusMu);
    return slot->status;
  }

  bool TakeActivation(const std::string& key, ClientSessionConnectArgs* args, uint64_t* op = nullptr) {
    std::shared_ptr<Slot> slot = Find(key);
    if (!slot) return false;
    std::lock_guard<std::mutex> l(slot->mu);
    if (!slot->lease->ActivationArgs(args, slot, op)) return false;
    Publish(*slot);
    return true;
  }

  // The arguments of `op` will not be used (no Connect): their session handle is closed and the lease takes its socket
  // back. Its lease gone (Release / ReleaseAll came first): the handle is still closed here.
  bool Abandon(const std::string& key, uint64_t op, ClientSessionConnectArgs* args) {
    std::shared_ptr<Slot> slot = Find(key);
    bool ok = false;
    if (slot) {
      std::lock_guard<std::mutex> l(slot->mu);
      ok = slot->lease->Abandon(op, args ? &args->preparedUdpSocket : nullptr);
      Publish(*slot);
    }
    if (args && (ok || !slot)) {
      close_socket(&args->preparedUdpSocket);  // no-op if the lease closed it
      args->udpHandshake = nullptr;            // drops its hold on the slot
    }
    return ok;
  }
"""),
("""  // The session made from TakeActivation ended (or its Connect failed): the lease goes on by Ping.
  void SessionOver(const std::string& key) {
    std::shared_ptr<Slot> slot = Find(key);
    if (!slot) return;
    std::lock_guard<std::mutex> l(slot->mu);
    slot->lease->SessionOver();
    slot->handedOff = false;
    Publish(*slot);
  }""", """  // The session made from TakeActivation `op` is over (Connect failed, or Disconnect returned): the lease goes on by
  // Ping. false: no such lease now, another op, or its handshake still runs.
  bool SessionOver(const std::string& key, uint64_t op) {
    std::shared_ptr<Slot> slot = Find(key);
    if (!slot) return false;
    std::lock_guard<std::mutex> l(slot->mu);
    const bool ok = slot->lease->SessionOver(op);
    Publish(*slot);
    return ok;
  }"""),
("""    std::atomic<bool> stop{false};
    std::atomic<bool> handedOff{false};  // set under mu; read by the pump loop
    std::atomic<int> state{0};
    std::atomic<uint64_t> entryId{0};
    std::atomic<int> lost{0};
    std::atomic<bool> needsCapability{false};
  };

  static void Publish(Slot& s) {
    s.state.store(static_cast<int>(s.lease->state()), std::memory_order_release);
    s.entryId.store(s.lease->entryId(), std::memory_order_release);
    s.lost.store(s.lease->lostCount(), std::memory_order_release);
    s.needsCapability.store(s.lease->needsCapability(), std::memory_order_release);
  }""", """    std::atomic<bool> stop{false};
    mutable std::mutex statusMu;
    Status status;  // under statusMu; written by Publish (under mu)
  };

  static void Publish(Slot& s) {
    const StandbyLease::Snapshot n = s.lease->snapshot();
    std::lock_guard<std::mutex> l(s.statusMu);
    s.status.exists = true;
    s.status.state = n.state;
    s.status.entryId = n.entryId;
    s.status.lost = n.lost;
    s.status.handedOff = n.handedOff;
    s.status.needsCapability = n.needsCapability;
  }"""),
("""    slot->lease->Release();  // parked: one StandbyRelease; handed off: nothing (that session ends by itself)
  }""", """    slot->lease->Release();  // parked: one StandbyRelease; handed off: the handshake in flight is cancelled
    Publish(*slot);
  }"""),
]
for a, b in rep:
    assert s.count(a) == 1, a[:60]
    s = s.replace(a, b)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')

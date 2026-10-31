#pragma once

// keepalive-r2 ② (e): the app's standby leases, one per PC -- each a StandbyLease with its own pump thread (the lease
// itself has none). What the APK bridge (native_bridge.cpp, JNI) and a PC client hold; portable, so the desktop e2e runs
// the same object.
//
//  - Start(key): a lease for that PC (its capability from the factory, or handed in: Observe + Provide), admitted and
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
// Locks: mu_ guards the map only; each slot's mu serializes the calls on it (order: slot, then the lease's own). The
// pump holds no slot lock: the lease locks itself per step and never across a wait, so a call (the UI thread's tap,
// leaving the screen) never queues behind the pump. The handshake runs on the session's connect thread and takes only
// the lease's lock, per step.

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "standby_lease.hpp"

namespace remote60::native_poc {

class StandbyRunner {
 public:
  struct Status {
    bool exists = false;
    StandbyEntryClient::State state = StandbyEntryClient::State::Idle;
    uint64_t entryId = 0;
    int lost = 0;
    bool handedOff = false;
    bool needsCapability = false;  // ② e: the lease waits for a capability the app gets (Observe, then Provide)
    bool tailActive = false;       // r2 D1: the old session's datagrams still arrive (StandbyLease::Snapshot)
    uint64_t strays = 0;
    uint64_t punches = 0;          // r3 D1: the host's Punches, apart from the strays
  };
  // The capability source of one PC's lease (directory observe + standby connect through that lease's socket).
  using CapabilityFactory = std::function<StandbyLease::CapabilityFn(const std::string& key)>;

  StandbyRunner(GenerationAllocator& gens, uint32_t helloFeatures, CapabilityFactory capabilities, StandbyLease::Clock now)
      : gens_(gens), helloFeatures_(helloFeatures), capabilities_(std::move(capabilities)), now_(std::move(now)) {}
  ~StandbyRunner() { ReleaseAll(); }
  StandbyRunner(const StandbyRunner&) = delete;
  StandbyRunner& operator=(const StandbyRunner&) = delete;

  // A lease for `key` (none yet): opened, admitted, pumped. true if one exists now.
  bool Start(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    if (leases_.count(key)) return true;
    auto slot = std::make_shared<Slot>();
    // The generation counter is shared by every lease of this install (one device, one monotone counter).
    slot->lease = std::make_unique<StandbyLease>(gens_, helloFeatures_, capabilities_(key), now_);
    std::string error;
    if (!slot->lease->Open(&error)) return false;
    {
      std::lock_guard<std::mutex> l(slot->mu);
      slot->lease->Start();
      Publish(*slot);
    }
    // raw: Stop joins this thread before the runner lets the slot go (a slot whose last holder is an activation's
    // arguments has no thread left)
    Slot* raw = slot.get();
    slot->thread = std::thread([raw] {
      while (!raw->stop.load(std::memory_order_acquire)) {
        raw->lease->Pump(100);  // returns at once while handed off: the session's reader has the socket
        if (Publish(*raw).handedOff) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    });
    leases_[key] = slot;
    return true;
  }

  Status StatusOf(const std::string& key) const {
    std::shared_ptr<Slot> slot = Find(key);
    if (!slot) return Status{};
    std::lock_guard<std::mutex> l(slot->statusMu);
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

  // ② e (the APK): the directory observation from this PC's lease socket, then the capability the app got for it.
  bool Observe(const std::string& key, const std::string& host, uint16_t port, const std::string& token) {
    std::shared_ptr<Slot> slot = Find(key);
    if (!slot) return false;
    std::lock_guard<std::mutex> l(slot->mu);
    return slot->lease->ObserveFrom(host, port, token);
  }
  bool Provide(const std::string& key, const StandbyCapability& cap) {
    std::shared_ptr<Slot> slot = Find(key);
    if (!slot) return false;
    std::lock_guard<std::mutex> l(slot->mu);
    const bool ok = slot->lease->Provide(cap);
    Publish(*slot);
    return ok;
  }

  // The session made from TakeActivation `op` is over (Connect failed, or Disconnect returned): the lease goes on by
  // Ping. false: no such lease now, another op, or its handshake still runs.
  bool SessionOver(const std::string& key, uint64_t op) {
    std::shared_ptr<Slot> slot = Find(key);
    if (!slot) return false;
    std::lock_guard<std::mutex> l(slot->mu);
    const bool ok = slot->lease->SessionOver(op);
    Publish(*slot);
    return ok;
  }

  void Release(const std::string& key) {
    std::shared_ptr<Slot> slot;
    {
      std::lock_guard<std::mutex> lock(mu_);
      auto it = leases_.find(key);
      if (it == leases_.end()) return;
      slot = it->second;
      leases_.erase(it);
    }
    Stop(slot);
  }

  void ReleaseAll() {
    std::map<std::string, std::shared_ptr<Slot>> all;
    {
      std::lock_guard<std::mutex> lock(mu_);
      all.swap(leases_);
    }
    for (auto& kv : all) Stop(kv.second);
  }

  // r3 (Codex codex_ka_e_src_r2.md R2): the leases of ONE owner -- every key starting with `prefix` (non-empty) -- and
  // no other. An app screen's late cleanup reclaims its own leases and never those of the screen after it, which share
  // this process's runner. How many went.
  size_t ReleaseWhere(const std::string& prefix) {
    if (prefix.empty()) return 0;
    std::vector<std::shared_ptr<Slot>> mine;
    {
      std::lock_guard<std::mutex> lock(mu_);
      for (auto it = leases_.begin(); it != leases_.end();) {
        if (it->first.compare(0, prefix.size(), prefix) == 0) {
          mine.push_back(it->second);
          it = leases_.erase(it);
        } else {
          ++it;
        }
      }
    }
    for (auto& slot : mine) Stop(slot);
    return mine.size();
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return leases_.size();
  }
  // Slots alive in this process, whoever holds them (the runner or an activation's arguments) -- for the tests' lifetime
  // oracle (r3 N2); nothing else reads it.
  static int liveSlots() { return LiveSlots().load(std::memory_order_acquire); }

 private:
  struct Slot {
    std::mutex mu;
    std::unique_ptr<StandbyLease> lease;
    std::thread thread;
    std::atomic<bool> stop{false};
    Slot() { LiveSlots().fetch_add(1, std::memory_order_acq_rel); }
    ~Slot() { LiveSlots().fetch_sub(1, std::memory_order_acq_rel); }
    mutable std::mutex statusMu;
    Status status;  // under statusMu; written by Publish (under mu)
  };

  static std::atomic<int>& LiveSlots() {
    static std::atomic<int> n{0};
    return n;
  }

  // the snapshot is taken under statusMu (order: statusMu, then the lease's): a later Publish never loses to an older one
  static Status Publish(Slot& s) {
    std::lock_guard<std::mutex> l(s.statusMu);
    const StandbyLease::Snapshot n = s.lease->snapshot();
    s.status.exists = true;
    s.status.state = n.state;
    s.status.entryId = n.entryId;
    s.status.lost = n.lost;
    s.status.handedOff = n.handedOff;
    s.status.needsCapability = n.needsCapability;
    s.status.tailActive = n.tailActive;
    s.status.strays = n.strays;
    s.status.punches = n.punches;
    return s.status;
  }

  std::shared_ptr<Slot> Find(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = leases_.find(key);
    return it == leases_.end() ? nullptr : it->second;
  }

  static void Stop(const std::shared_ptr<Slot>& slot) {
    slot->stop.store(true, std::memory_order_release);
    if (slot->thread.joinable()) slot->thread.join();
    std::lock_guard<std::mutex> l(slot->mu);
    slot->lease->Release();  // parked: one StandbyRelease; handed off: the handshake in flight is cancelled
    Publish(*slot);
  }

  GenerationAllocator& gens_;
  const uint32_t helloFeatures_;
  CapabilityFactory capabilities_;
  StandbyLease::Clock now_;
  mutable std::mutex mu_;
  std::map<std::string, std::shared_ptr<Slot>> leases_;
};

}  // namespace remote60::native_poc

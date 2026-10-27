#pragma once

// keepalive-r2 ② (e): the app's standby leases, one per PC -- each a StandbyLease with its own pump thread (the lease
// itself has none). What the APK bridge (native_bridge.cpp, JNI) and a PC client hold; portable, so the desktop e2e runs
// the same object.
//
//  - Start(key): a lease for that PC (its capability from the factory), admitted and kept by Ping on its thread.
//  - Status(key): a snapshot the pump thread publishes (no lease lock on the caller's thread -- the UI polls it).
//  - TakeActivation(key, args): the arguments of the ClientSessionController::Connect that activates it (the lease's
//    socket, duplicated, and the activation handshake); the pump stops touching the socket until SessionOver(key).
//  - Release(key): the PC's standby goes (one StandbyRelease if parked), its thread ends, the lease is closed.
// Locks: mu_ guards the map only; each lease has its own mutex, held by its pump and by every call on it -- never
// across the activation handshake, which runs on the session's connect thread while the pump leaves the lease alone.

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
    slot->thread = std::thread([slot] {
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
    });
    leases_[key] = slot;
    return true;
  }

  Status StatusOf(const std::string& key) const {
    std::shared_ptr<Slot> slot = Find(key);
    if (!slot) return Status{};
    Status s;
    s.exists = true;
    s.state = static_cast<StandbyEntryClient::State>(slot->state.load(std::memory_order_acquire));
    s.entryId = slot->entryId.load(std::memory_order_acquire);
    s.lost = slot->lost.load(std::memory_order_acquire);
    s.handedOff = slot->handedOff.load(std::memory_order_acquire);
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

  // The session made from TakeActivation ended (or its Connect failed): the lease goes on by Ping.
  void SessionOver(const std::string& key) {
    std::shared_ptr<Slot> slot = Find(key);
    if (!slot) return;
    std::lock_guard<std::mutex> l(slot->mu);
    slot->lease->SessionOver();
    slot->handedOff = false;
    Publish(*slot);
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

  size_t size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return leases_.size();
  }

 private:
  struct Slot {
    std::mutex mu;
    std::unique_ptr<StandbyLease> lease;
    std::thread thread;
    std::atomic<bool> stop{false};
    std::atomic<bool> handedOff{false};  // set under mu; read by the pump loop
    std::atomic<int> state{0};
    std::atomic<uint64_t> entryId{0};
    std::atomic<int> lost{0};
  };

  static void Publish(Slot& s) {
    s.state.store(static_cast<int>(s.lease->state()), std::memory_order_release);
    s.entryId.store(s.lease->entryId(), std::memory_order_release);
    s.lost.store(s.lease->lostCount(), std::memory_order_release);
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
    slot->lease->Release();  // parked: one StandbyRelease; handed off: nothing (that session ends by itself)
  }

  GenerationAllocator& gens_;
  const uint32_t helloFeatures_;
  CapabilityFactory capabilities_;
  StandbyLease::Clock now_;
  mutable std::mutex mu_;
  std::map<std::string, std::shared_ptr<Slot>> leases_;
};

}  // namespace remote60::native_poc

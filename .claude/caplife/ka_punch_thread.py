SRC = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'


def patch(name, reps):
    p = SRC + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, a[:80], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


patch('directory_client.hpp', [
("#include <chrono>\n#include <cstdint>\n", "#include <chrono>\n#include <condition_variable>\n#include <cstdint>\n"),
("""  std::thread thread_;
  std::atomic<bool> running_{false};
""", """  std::thread thread_;
  std::atomic<bool> running_{false};
  // keepalive-r2 FINDING (작업용2 report_ka_pc_impl_r1.md; e2e kae_relapse5): the punch trains run on their own thread,
  // beside the agent's cycle -- not in it. A train is kPunchPackets x kPunchIntervalMs (5 s); run inside the cycle it
  // kept the agent from acting on the next wake, so a capability minted meanwhile (a second connect, a standby
  // re-admission) waited for the train and the next cycle -- longer than a client's Hellos last (~3 s): it read as
  // "no answer" / "unsupported". Punch() now hands its targets to this thread and returns.
  struct PunchJob {
    PunchTarget target;
    int left = 0;
  };
  void PunchLoop();
  std::mutex punchMu_;
  std::condition_variable punchCv_;
  std::vector<PunchJob> punchJobs_;
  std::thread punchThread_;
"""),
])

patch('directory_client.cpp', [
("""  refreshRequested_.store(false, std::memory_order_release);
  running_.store(true);
  thread_ = std::thread([this] { Run(); });
  return true;
}

void HostAgent::Stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}""", """  refreshRequested_.store(false, std::memory_order_release);
  running_.store(true);
  thread_ = std::thread([this] { Run(); });
  punchThread_ = std::thread([this] { PunchLoop(); });
  return true;
}

void HostAgent::Stop() {
  if (!running_.exchange(false)) return;
  {
    std::lock_guard<std::mutex> lock(punchMu_);  // the loop checks running_ under this lock: no lost wake
  }
  punchCv_.notify_all();
  if (punchThread_.joinable()) punchThread_.join();
  if (thread_.joinable()) thread_.join();
}

// The punch trains (see punchThread_): every target queued gets one punch per kPunchIntervalMs until it has had
// kPunchPackets -- the same packets, the same spacing as before, now beside the cycle rather than inside it.
void HostAgent::PunchLoop() {
  UdpHelloPacket packet{};
  packet.kind = static_cast<uint16_t>(UdpPacketKind::Punch);
  std::unique_lock<std::mutex> lock(punchMu_);
  while (running_.load()) {
    punchCv_.wait(lock, [this] { return !running_.load() || !punchJobs_.empty(); });
    if (!running_.load()) break;
    std::vector<PunchTarget> now;
    now.reserve(punchJobs_.size());
    for (auto& job : punchJobs_) {
      now.push_back(job.target);
      --job.left;
    }
    punchJobs_.erase(std::remove_if(punchJobs_.begin(), punchJobs_.end(), [](const PunchJob& j) { return j.left <= 0; }),
                     punchJobs_.end());
    lock.unlock();
    for (const auto& t : now) {
      sockaddr_in to{};
      to.sin_family = AF_INET;
      to.sin_addr.s_addr = t.ipv4NetworkOrder;
      to.sin_port = htons(t.port);
      // keepalive-r2 r4 (Codex N1): toward a relay lease's host-facing socket, the punch carries that lease's
      // host proof -- what the relay pins this host's tuple by. Every other punch stays empty (a client's token
      // is never put on the wire here).
      UdpHelloPacket p = packet;
      if (!t.hostProof.empty()) std::memcpy(p.authToken, t.hostProof.data(), (std::min)(t.hostProof.size(), sizeof(p.authToken) - 1));
      send_(&p, sizeof(p), to);
    }
    lock.lock();
    // the spacing, cut short only by Stop()
    punchCv_.wait_for(lock, std::chrono::milliseconds(kPunchIntervalMs), [this] { return !running_.load(); });
  }
}"""),
("""  UdpHelloPacket packet{};
  packet.kind = static_cast<uint16_t>(UdpPacketKind::Punch);
  for (int i = 0; i < kPunchPackets && running_.load(); ++i) {
    for (const auto& t : targets) {
      sockaddr_in to{};
      to.sin_family = AF_INET;
      to.sin_addr.s_addr = t.ipv4NetworkOrder;
      to.sin_port = htons(t.port);
      // keepalive-r2 r4 (Codex N1): toward a relay lease's host-facing socket, the punch carries that lease's
      // host proof -- what the relay pins this host's tuple by. Every other punch stays empty (a client's token
      // is never put on the wire here).
      UdpHelloPacket p = packet;
      if (!t.hostProof.empty()) std::memcpy(p.authToken, t.hostProof.data(), (std::min)(t.hostProof.size(), sizeof(p.authToken) - 1));
      send_(&p, sizeof(p), to);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kPunchIntervalMs));
  }
}""", """  // To the punch thread, and back to the cycle at once (its next wake is not held up by a 5 s train). A target already
  // being punched starts its count over.
  {
    std::lock_guard<std::mutex> lock(punchMu_);
    for (const auto& t : targets) {
      auto same = std::find_if(punchJobs_.begin(), punchJobs_.end(), [&](const PunchJob& j) {
        return j.target.ipv4NetworkOrder == t.ipv4NetworkOrder && j.target.port == t.port && j.target.hostProof == t.hostProof;
      });
      if (same != punchJobs_.end()) {
        same->left = kPunchPackets;
      } else {
        punchJobs_.push_back(PunchJob{t, kPunchPackets});
      }
    }
  }
  punchCv_.notify_all();
}"""),
("""    const auto sleepStart = std::chrono::steady_clock::now();
    const char* wake = "elapsed";
    while (running_.load() && remaining.count() > 0) {""", """    const auto sleepStart = std::chrono::steady_clock::now();
    const char* wake = "elapsed";
    // a wake that came while the work ran past the interval is this cycle's cause, not a leftover for the next one
    if (remaining.count() <= 0 && refreshRequested_.exchange(false, std::memory_order_acq_rel)) wake = "refresh";
    while (running_.load() && remaining.count() > 0) {"""),
])
print('ok')

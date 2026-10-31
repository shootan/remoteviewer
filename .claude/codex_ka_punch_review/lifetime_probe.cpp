// Review only: unmodified method bodies extracted from fixed a7773e6; no network/HTTP.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "directory_observe.hpp"
#include "host_diag_log.hpp"
#include "punch_reply.hpp"
#include "update_endpoint.hpp"
#define private public
#include "directory_client.hpp"
#undef private
#include "poc_protocol.hpp"
namespace remote60::native_poc::directory {
namespace {
constexpr int kPunchPackets = 25;
constexpr int kPunchIntervalMs = 200;
}
HostAgent::~HostAgent() { Stop(); }
void HostAgent::Stop() {
  if (!running_.exchange(false)) return;
  {
    std::lock_guard<std::mutex> lock(punchMu_);  // the loop checks running_ under this lock: no lost wake
  }
  punchCv_.notify_all();
  if (punchThread_.joinable()) punchThread_.join();
  if (thread_.joinable()) thread_.join();
}

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
}

void HostAgent::SetStatus(const std::string& status) {
  std::lock_guard<std::mutex> lock(mu_);
  status_ = status;
}

void HostAgent::Punch(const std::vector<PunchTarget>& targets) {
  if (targets.empty()) return;
  for (const auto& t : targets) {
    in_addr shown{};
    shown.s_addr = t.ipv4NetworkOrder;
    char text[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &shown, text, sizeof(text));
    std::cout << "[native-video-host] directory punch -> " << text << ":" << t.port << "\n";
  }
  SetStatus("punching");

  bool openedReplyWindow = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    const auto now = std::chrono::steady_clock::now();
    authorizedPeers_.erase(
        std::remove_if(authorizedPeers_.begin(), authorizedPeers_.end(),
                       [&](const AuthorizedPeer& peer) { return peer.expiresAt <= now; }),
        authorizedPeers_.end());
    for (const auto& target : targets) {
      if (!target.hostProof.empty()) continue;  // keepalive-r2 r4: a relay socket of ours, not a peer
      authorizedPeers_.push_back(
          AuthorizedPeer{target, now + std::chrono::seconds(30)});
    }
    // The second of the two directory events that may open the reply window. A heartbeat that
    // came back with a capability means a peer asked the directory to connect to us within the
    // last cycle, which is exactly when answering a punch is worth doing.
    const uint64_t nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    if (remote60::native_poc::punch_reply_note_directory_event(&punchReplyState_, nowMs)) {
      openedReplyWindow = true;
    }
  }
  if (openedReplyWindow) {
    std::cout << "[native-video-host][punch-reply] window open for "
              << remote60::native_poc::kPunchReplyWindowMs << "ms (capability collected)\n";
  }

  // To the punch thread, and back to the cycle at once (its next wake is not held up by a 5 s train). A target already
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
}
}
int main() {
  using remote60::native_poc::directory::HostAgent;
  HostAgent a;
  HostAgent::PunchTarget old;
  old.ipv4NetworkOrder = htonl(INADDR_LOOPBACK);
  old.port = 54321;
  old.hostProof = "old-owner-host-proof";
  std::atomic<int> oldSends{0}, newSends{0};
  a.send_ = [&](const void*, size_t, const sockaddr_in&) { ++oldSends; };
  a.running_.store(true);
  a.punchThread_ = std::thread([&] { a.PunchLoop(); });
  a.Punch({old});
  const auto waitFor = [](const std::atomic<int>& n) {
    const ULONGLONG t = GetTickCount64();
    while (n.load() == 0 && GetTickCount64() - t < 1000) Sleep(1);
    return n.load() != 0;
  };
  if (!waitFor(oldSends)) ExitProcess(2);
  a.Stop();
  size_t jobs;
  int left = 0;
  { std::lock_guard<std::mutex> lock(a.punchMu_);
    jobs = a.punchJobs_.size();
    if (jobs) left = a.punchJobs_[0].left;
  }
  // The Start() assignments which restart the punch worker. Full Start/HTTP is not executed.
  a.send_ = [&](const void* p, size_t len, const sockaddr_in& to) {
    auto* packet = static_cast<const remote60::native_poc::UdpHelloPacket*>(p);
    if (len == sizeof(*packet) && ntohs(to.sin_port) == old.port &&
        std::string(packet->authToken) == old.hostProof) ++newSends;
  };
  a.running_.store(true);
  a.punchThread_ = std::thread([&] { a.PunchLoop(); });
  const bool stale = waitFor(newSends);
  a.Stop();
  std::printf("REVIEW pending_jobs_after_stop=%zu packets_left=%d stale_sends_after_worker_restart=%d\n",
              jobs, left, newSends.load());
  return jobs > 0 && stale ? 0 : 1; // 0 = reproduced, not product PASS
}

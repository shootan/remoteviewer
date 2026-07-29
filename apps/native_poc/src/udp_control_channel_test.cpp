// Exercises the reliable control channel against loss, reordering and duplication, since a
// remote session becomes unusable the moment a control message is silently dropped.

#include <atomic>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

#include "udp_control_channel.hpp"

using remote60::native_poc::UdpControlChannel;
using remote60::native_poc::UdpControlLink;

namespace {

int gFailures = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!cond) ++gFailures;
}

/**
 * A lossy, reordering link between two channels. Packets are handed to the peer from a
 * background thread so delivery order is not the send order.
 */
class FakeNetwork {
 public:
  FakeNetwork(double lossRate, uint32_t seed) : lossRate_(lossRate), rng_(seed) {}

  void Attach(UdpControlChannel* a, UdpControlChannel* b) {
    a_ = a;
    b_ = b;
  }

  void SendToB(const void* data, size_t len) { Enqueue(b_, data, len); }
  void SendToA(const void* data, size_t len) { Enqueue(a_, data, len); }

  void Start() {
    running_ = true;
    pump_ = std::thread([this] {
      while (running_) {
        std::vector<std::pair<UdpControlChannel*, std::vector<uint8_t>>> batch;
        {
          std::lock_guard<std::mutex> lock(mu_);
          if (!queue_.empty()) {
            // Draining in reverse deliberately reorders the burst.
            batch.assign(queue_.rbegin(), queue_.rend());
            queue_.clear();
          }
        }
        for (auto& [target, bytes] : batch) target->OnPacket(bytes.data(), bytes.size());
        std::this_thread::sleep_for(std::chrono::microseconds(200));
      }
    });
  }

  void Stop() {
    running_ = false;
    if (pump_.joinable()) pump_.join();
  }

  uint64_t dropped() const { return dropped_.load(); }

 private:
  void Enqueue(UdpControlChannel* target, const void* data, size_t len) {
    {
      std::lock_guard<std::mutex> lock(rngMu_);
      if (std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < lossRate_) {
        ++dropped_;
        return;
      }
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    std::lock_guard<std::mutex> lock(mu_);
    queue_.emplace_back(target, std::vector<uint8_t>(bytes, bytes + len));
  }

  double lossRate_;
  std::mt19937 rng_;
  std::mutex rngMu_;
  std::mutex mu_;
  std::vector<std::pair<UdpControlChannel*, std::vector<uint8_t>>> queue_;
  std::thread pump_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> dropped_{0};
  UdpControlChannel* a_ = nullptr;
  UdpControlChannel* b_ = nullptr;
};

std::vector<uint8_t> pattern(size_t len, uint8_t salt) {
  std::vector<uint8_t> out(len);
  for (size_t i = 0; i < len; ++i) out[i] = static_cast<uint8_t>((i * 31 + salt) & 0xFF);
  return out;
}

void run_case(const char* label, double lossRate, size_t messageBytes, int messageCount) {
  UdpControlChannel client;
  UdpControlChannel host;
  FakeNetwork net(lossRate, 1234u + static_cast<uint32_t>(messageBytes));
  net.Attach(&client, &host);

  client.Configure([&](const void* d, size_t n) { net.SendToB(d, n); return true; },
                   remote60::native_poc::kUdpControlStreamClientToHost,
                   remote60::native_poc::kUdpControlStreamHostToClient, 1200);
  host.Configure([&](const void* d, size_t n) { net.SendToA(d, n); return true; },
                 remote60::native_poc::kUdpControlStreamHostToClient,
                 remote60::native_poc::kUdpControlStreamClientToHost, 1200);
  net.Start();

  std::atomic<bool> hostOk{true};
  std::atomic<int> served{0};
  // The host mirrors each request back with one byte flipped, standing in for a real handler.
  std::thread hostThread([&] {
    UdpControlLink link(&host, 15000);
    for (int i = 0; i < messageCount; ++i) {
      std::vector<uint8_t> request(messageBytes);
      if (!link.Read(request.data(), request.size())) {
        hostOk = false;
        return;
      }
      request[0] = static_cast<uint8_t>(request[0] ^ 0xFF);
      if (!link.Write(request.data(), request.size()) || !link.EndMessage()) {
        hostOk = false;
        return;
      }
      ++served;
    }
  });

  UdpControlLink clientLink(&client, 15000);
  bool allMatched = true;
  for (int i = 0; i < messageCount; ++i) {
    const std::vector<uint8_t> request = pattern(messageBytes, static_cast<uint8_t>(i));
    if (!clientLink.Write(request.data(), request.size()) || !clientLink.EndMessage()) {
      allMatched = false;
      break;
    }
    std::vector<uint8_t> response(messageBytes);
    if (!clientLink.Read(response.data(), response.size())) {
      allMatched = false;
      break;
    }
    std::vector<uint8_t> expected = request;
    expected[0] = static_cast<uint8_t>(expected[0] ^ 0xFF);
    if (response != expected) {
      allMatched = false;
      break;
    }
  }

  hostThread.join();
  net.Stop();

  char detail[256];
  std::snprintf(detail, sizeof(detail), "loss=%.0f%% bytes=%zu served=%d dropped=%llu", lossRate * 100,
                messageBytes, served.load(), static_cast<unsigned long long>(net.dropped()));
  check(label, allMatched && hostOk.load() && served.load() == messageCount, detail);
}

}  // namespace

int main() {
  run_case("small messages, clean link", 0.0, 64, 20);
  run_case("small messages, 10% loss", 0.10, 64, 20);
  run_case("multi-fragment message, clean link", 0.0, 40000, 5);
  run_case("multi-fragment message, 10% loss", 0.10, 40000, 5);
  // A window thumbnail is the worst case the real protocol produces.
  run_case("thumbnail-sized message, 5% loss", 0.05, 320 * 320 * 4, 2);

  std::printf(gFailures == 0 ? "\nRESULT: ALL PASS\n" : "\nRESULT: %d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}

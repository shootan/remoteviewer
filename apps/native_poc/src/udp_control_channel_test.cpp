// Exercises the reliable control channel against loss, reordering and duplication, since a
// remote session becomes unusable the moment a control message is silently dropped.

#include <atomic>
#include <chrono>
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

/**
 * What a host has to do between one client and the next.
 *
 * Sequence numbers are per-channel and every client starts its own at one, so a host that
 * carries a channel across a handover sees the newcomer's first message as one it has already
 * delivered. The channel answers that with an ack and drops the payload -- which looks exactly
 * like a healthy link whose peer has gone deaf, and is why it took a packet capture to find.
 * Reset is what makes the handover real; this pins that, and the close reasons the host now
 * uses to tell a departed client apart from its own shutdown.
 */
void run_handover_case() {
  UdpControlChannel host;
  UdpControlChannel first;
  UdpControlChannel second;
  // Queued rather than delivered inline: a channel sends while holding its own lock, so handing
  // the bytes straight to the peer would re-enter that lock when the peer acknowledges.
  std::vector<std::pair<UdpControlChannel*, std::vector<uint8_t>>> wire;
  auto deliver_to = [&wire](UdpControlChannel* to) {
    return [&wire, to](const void* data, size_t len) {
      const auto* bytes = static_cast<const uint8_t*>(data);
      wire.emplace_back(to, std::vector<uint8_t>(bytes, bytes + len));
      return true;
    };
  };
  auto pump = [&wire]() {
    for (int guard = 0; guard < 64 && !wire.empty(); ++guard) {
      auto batch = std::move(wire);
      wire.clear();
      for (auto& [target, bytes] : batch) target->OnPacket(bytes.data(), bytes.size());
    }
  };
  host.Configure(deliver_to(&first), remote60::native_poc::kUdpControlStreamHostToClient,
                 remote60::native_poc::kUdpControlStreamClientToHost, 1200);
  first.Configure(deliver_to(&host), remote60::native_poc::kUdpControlStreamClientToHost,
                  remote60::native_poc::kUdpControlStreamHostToClient, 1200);
  second.Configure(deliver_to(&host), remote60::native_poc::kUdpControlStreamClientToHost,
                   remote60::native_poc::kUdpControlStreamHostToClient, 1200);

  const std::vector<uint8_t> fromFirst(48, 0xA5);
  std::vector<uint8_t> got;
  const bool firstSent = first.Send(fromFirst.data(), fromFirst.size());
  pump();
  check("the first client's message is delivered",
        firstSent && host.Receive(&got, 100) && got == fromFirst);

  // The first client leaves; its retransmits run out and close the channel. That is the ordinary
  // end of a session, not a reason for the host to stop serving.
  host.Close(remote60::native_poc::ControlCloseReason::PeerLost);
  check("a departed peer is reported as such, not as shutdown",
        host.CloseReason() == remote60::native_poc::ControlCloseReason::PeerLost,
        remote60::native_poc::to_string(host.CloseReason()));

  host.Reset();
  host.Configure(deliver_to(&second), remote60::native_poc::kUdpControlStreamHostToClient,
                 remote60::native_poc::kUdpControlStreamClientToHost, 1200);
  check("the channel reopens for the next session", !host.IsClosed());
  check("and the close reason goes with it",
        host.CloseReason() == remote60::native_poc::ControlCloseReason::None,
        remote60::native_poc::to_string(host.CloseReason()));

  got.clear();
  wire.clear();  // whatever the departed client still had in flight dies with it
  const std::vector<uint8_t> fromSecond(48, 0x5C);
  const bool sent = second.Send(fromSecond.data(), fromSecond.size());
  pump();
  const bool received = host.Receive(&got, 100);
  check("the next client's first message is delivered, not merely acknowledged",
        sent && received && got == fromSecond,
        received ? "" : "acked into silence");
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

/**
 * The control channel's own timing, unchanged by the clipboard-image bulk channel (reviewer
 * condition, t-si5297mp): a channel nobody calls SetTimings on resends an unacknowledged message
 * after 250 ms and NACKs a gap after 90 ms -- and it still does so while ANOTHER instance in the same
 * process runs on bulk timings. Real clock; the windows allow for Tick granularity and scheduling.
 */
void run_default_timing_case() {
  using namespace remote60::native_poc;
  using clock = std::chrono::steady_clock;
  const UdpControlChannel::Timings d;
  check("the default timings are the control channel's (250 ms / 24 / 90 ms / 90 ms)",
        d.retransmitIntervalUs == 250000 && d.maxAttempts == 24 && d.nackDelayUs == 90000 && d.nackIntervalUs == 90000);

  struct Sent {
    double ms;
    uint16_t kind;
  };
  auto run = [&](UdpControlChannel& ch, std::vector<Sent>* log, clock::time_point t0, int ms) {
    while (std::chrono::duration<double, std::milli>(clock::now() - t0).count() < ms) {
      ch.Tick();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    (void)log;
  };
  // Resend: the default instance and a bulk-timed instance side by side.
  std::vector<Sent> dlog, blog;
  clock::time_point t0;
  auto recorder = [&](std::vector<Sent>* log) {
    return [log, &t0](const void* data, size_t len) {
      uint16_t kind = 0;
      if (len >= 6) std::memcpy(&kind, static_cast<const uint8_t*>(data) + 4, 2);
      log->push_back({std::chrono::duration<double, std::milli>(clock::now() - t0).count(), kind});
      return true;
    };
  };
  UdpControlChannel control, bulk;
  control.Configure(recorder(&dlog), 1, 2, 1200);
  bulk.Configure(recorder(&blog), 0x40000005u, 0x40000006u, 1200);
  UdpControlChannel::Timings bt;
  bt.retransmitIntervalUs = 1000000;  // what a bulk instance may set; the control one must not follow
  bulk.SetTimings(bt);
  const std::vector<uint8_t> msg(2000, 7);  // two fragments
  t0 = clock::now();
  control.Send(msg.data(), msg.size());
  bulk.Send(msg.data(), msg.size());
  std::thread tb([&] { run(bulk, &blog, t0, 1300); });
  run(control, &dlog, t0, 400);
  tb.join();
  auto first_resend = [](const std::vector<Sent>& log) {
    // The first two datagrams are the initial send; the next one is the first resend.
    return log.size() > 2 ? log[2].ms : -1.0;
  };
  const double dr = first_resend(dlog), br = first_resend(blog);
  char buf[200];
  std::snprintf(buf, sizeof(buf), "control instance resends at 250 ms (measured %.0f ms) with a bulk instance on 1 s beside it", dr);
  check(buf, dr >= 249.0 && dr < 330.0);
  std::snprintf(buf, sizeof(buf), "the bulk instance keeps its own interval (measured %.0f ms)", br);
  check(buf, br >= 999.0 && br < 1100.0);

  // NACK: the control instance asks for a missing fragment after 90 ms, not sooner.
  std::vector<Sent> rlog;
  UdpControlChannel rx;
  rx.Configure(recorder(&rlog), 2, 1, 1200);
  UdpControlChunkHeader h{};
  h.magic = kMagic;
  h.kind = static_cast<uint16_t>(UdpPacketKind::ControlData);
  h.streamId = 1;
  h.messageSeq = 1;
  h.totalSize = 2000;
  h.fragIndex = 0;
  h.fragCount = 2;
  h.fragOffset = 0;
  h.fragSize = 1000;
  std::vector<uint8_t> packet(sizeof(h) + 1000, 1);
  std::memcpy(packet.data(), &h, sizeof(h));
  t0 = clock::now();
  rx.OnPacket(packet.data(), packet.size());
  run(rx, &rlog, t0, 200);
  double nack = -1.0;
  for (const auto& s : rlog) {
    if (s.kind == static_cast<uint16_t>(UdpPacketKind::ControlNack)) {
      nack = s.ms;
      break;
    }
  }
  std::snprintf(buf, sizeof(buf), "control instance NACKs a gap at 90 ms (measured %.0f ms)", nack);
  check(buf, nack >= 89.0 && nack < 150.0);
}


int main() {
  run_default_timing_case();
  {
    using namespace remote60::native_poc;
    UdpControlChannel channel;
    channel.Configure([](const void*, size_t) { return true; }, 2, 1, 1200);
    auto feed = [&](uint32_t seq, uint32_t total, uint16_t index, uint16_t count, uint32_t offset, uint16_t size) {
      UdpControlChunkHeader header{};
      header.magic = kMagic; header.kind = static_cast<uint16_t>(UdpPacketKind::ControlData);
      header.streamId = 1; header.messageSeq = seq; header.totalSize = total;
      header.fragIndex = index; header.fragCount = count; header.fragOffset = offset; header.fragSize = size;
      std::vector<uint8_t> packet(sizeof(header) + size, 1);
      std::memcpy(packet.data(), &header, sizeof(header));
      channel.OnPacket(packet.data(), packet.size());
    };
    feed(1, 4, 0, 2, 0, 2); feed(1, 4, 1, 2, 1, 2);
    std::vector<uint8_t> out;
    check("overlapping fragments never form a valid message", !channel.Receive(&out, 1) &&
          channel.CloseReason() == ControlCloseReason::MalformedMessage);
    channel.Reset();
    for (uint32_t seq = 1; seq <= 6; ++seq) feed(seq, 8u * 1024u * 1024u, 0, 2, 0, 1);
    check("incomplete control payloads have an aggregate budget", channel.IsClosed() &&
          channel.CloseReason() == ControlCloseReason::ResourceLimit &&
          channel.GetStats().inboundBytes <= 32u * 1024u * 1024u);
  }
  run_handover_case();
  run_case("small messages, clean link", 0.0, 64, 20);
  run_case("small messages, 10% loss", 0.10, 64, 20);
  run_case("multi-fragment message, clean link", 0.0, 40000, 5);
  run_case("multi-fragment message, 10% loss", 0.10, 40000, 5);
  // A window thumbnail is the worst case the real protocol produces.
  run_case("thumbnail-sized message, 5% loss", 0.05, 320 * 320 * 4, 2);

  std::printf(gFailures == 0 ? "\nRESULT: ALL PASS\n" : "\nRESULT: %d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}

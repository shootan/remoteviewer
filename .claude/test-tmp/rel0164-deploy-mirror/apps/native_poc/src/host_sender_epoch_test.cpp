// The sender-side half of the P11 epoch contract, on the REAL sender thread (SenderState::
// StartThread, host_encoded_sender.cpp) over a real loopback UDP socket:
//
//  [A] queued: AUs of the old epoch are already in sender.queue when the flush lands (the host's
//      inputEpoch moves) and the new epoch's IDR is queued behind them -> none of the old AUs
//      starts on the wire; the IDR is the first AU after the flush.
//  [B] in-flight: an old AU is being chunked when the flush lands -> that one AU completes (the
//      documented exception), the next AU on the wire is the IDR, no other old AU follows.
//  [C] the fence is keyed on the epoch, not on streamGeneration or the media epoch: an old-epoch
//      AU carrying the CURRENT generation is still dropped (no relabelling of old pixels as the
//      new target), and an untagged item (epoch 0, raw path) is not fenced.
//
// Build: remote60_host_sender_epoch_test (CMake). Run: prints "...: PASS", exit 0.

#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "host_args.hpp"
#include "host_encoded_sender.hpp"
#include "host_main_loop_mailbox.hpp"
#include "host_session.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

#pragma comment(lib, "ws2_32.lib")

using namespace remote60::native_poc;

namespace {

int gFailures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
      ++gFailures;                                                     \
    }                                                                  \
  } while (0)

struct Wire {
  uint32_t seq = 0;
  uint16_t chunkIndex = 0;
  uint16_t chunkCount = 0;
  bool key = false;
  bool parity = false;  // XOR FEC parity packet (bit4): same seq, not a payload chunk
  uint64_t generation = 0;
};

struct Rig {
  SOCKET tx = INVALID_SOCKET;
  SOCKET rx = INVALID_SOCKET;
  sockaddr_in rxAddr{};
  Args args;
  SessionState clientSession;
  MainLoopMailbox mailbox;
  SenderState sender;
  std::atomic<uint64_t> epoch{1};

  bool Start() {
    tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (tx == INVALID_SOCKET || rx == INVALID_SOCKET) return false;
    sockaddr_in any{};
    any.sin_family = AF_INET;
    any.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    any.sin_port = 0;
    if (bind(tx, reinterpret_cast<sockaddr*>(&any), sizeof(any)) != 0) return false;
    if (bind(rx, reinterpret_cast<sockaddr*>(&any), sizeof(any)) != 0) return false;
    int len = sizeof(rxAddr);
    if (getsockname(rx, reinterpret_cast<sockaddr*>(&rxAddr), &len) != 0) return false;
    const DWORD timeoutMs = 50;
    (void)setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    const int rcvBuf = 4 * 1024 * 1024;
    (void)setsockopt(rx, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));
    clientSession.clientSock = tx;
    sender.peer = rxAddr;
    sender.peerReady = true;
    sender.inputEpochRef = &epoch;  // what host_startup_capture.cpp wires to CaptureState::inputEpoch
    sender.pacePeakBps.store(0, std::memory_order_relaxed);  // no pacing: the wire is loopback
    sender.StartThread(VideoTransport::Udp, true, args, clientSession, mailbox);
    return sender.thread.joinable();
  }

  void Stop() {
    sender.stop.store(true, std::memory_order_release);
    sender.cv.notify_all();
    if (sender.thread.joinable()) sender.thread.join();
    if (tx != INVALID_SOCKET) closesocket(tx);
    if (rx != INVALID_SOCKET) closesocket(rx);
  }

  EncodedSendItem Item(uint32_t seq, bool key, uint64_t inputEpoch, size_t bytes, uint64_t generation = 1) {
    EncodedSendItem item;
    item.bytes.assign(bytes, static_cast<uint8_t>(seq));
    item.keyFrame = key;
    item.frameIntervalUs = 16667;
    item.enqueueUs = qpc_now_us();
    item.mediaEpoch = sender.mediaSessionEpoch.load(std::memory_order_acquire);
    item.inputEpoch = inputEpoch;
    item.udpHdr.magic = kMagic;
    item.udpHdr.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
    item.udpHdr.size = static_cast<uint16_t>(sizeof(item.udpHdr));
    item.udpHdr.seq = seq;
    item.udpHdr.codec = static_cast<uint16_t>(UdpCodec::H264);
    item.udpHdr.flags = key ? 0x1u : 0u;
    item.udpHdr.width = 640;
    item.udpHdr.height = 360;
    item.udpHdr.payloadSize = static_cast<uint32_t>(bytes);
    item.udpHdr.streamGeneration = generation;
    item.udpHdr.captureQpcUs = qpc_now_us();
    return item;
  }

  // Reads everything that arrives within `quietMs` of the last datagram (or `maxMs` overall).
  std::vector<Wire> Drain(uint32_t quietMs, uint32_t maxMs = 3000) {
    std::vector<Wire> out;
    std::vector<uint8_t> buf(4096);
    const auto start = std::chrono::steady_clock::now();
    auto last = start;
    for (;;) {
      const int n = recv(rx, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
      const auto now = std::chrono::steady_clock::now();
      if (n >= static_cast<int>(sizeof(UdpVideoChunkHeader))) {
        UdpVideoChunkHeader h{};
        std::memcpy(&h, buf.data(), sizeof(h));
        if (h.magic == kMagic && h.kind == static_cast<uint16_t>(UdpPacketKind::VideoChunk)) {
          out.push_back(Wire{h.seq, h.chunkIndex, h.chunkCount, (h.flags & 0x1u) != 0, (h.flags & 0x10u) != 0, h.streamGeneration});
          last = now;
        }
      }
      if (now - last > std::chrono::milliseconds(quietMs)) break;
      if (now - start > std::chrono::milliseconds(maxMs)) break;
    }
    return out;
  }
};

// Payload chunks per seq (parity packets excluded).
std::map<uint32_t, size_t> chunks_per_seq(const std::vector<Wire>& w) {
  std::map<uint32_t, size_t> m;
  for (const auto& x : w) {
    if (!x.parity) ++m[x.seq];
  }
  return m;
}

// [A]
void test_queued_old_aus_are_fenced(Rig& rig) {
  std::printf("[A] queued old-epoch AUs behind a flush never start; the IDR is first\n");
  {
    std::lock_guard<std::mutex> lk(rig.sender.mu);
    // The queue holds three old-epoch deltas when the flush lands, and the new IDR behind them.
    for (uint32_t s = 10; s <= 12; ++s) rig.sender.queue.push_back(rig.Item(s, false, 1, 20000));
    rig.epoch.store(2, std::memory_order_release);  // the flush (ResetTimelineAnchors)
    rig.sender.queue.push_back(rig.Item(13, true, 2, 60000));
    rig.sender.queue.push_back(rig.Item(14, false, 2, 20000));
  }
  rig.sender.cv.notify_all();
  const auto wire = rig.Drain(150);
  const auto per = chunks_per_seq(wire);
  CHECK(per.count(10) == 0 && per.count(11) == 0 && per.count(12) == 0);
  CHECK(per.count(13) == 1);
  CHECK(!wire.empty() && wire.front().seq == 13 && wire.front().key);
  CHECK(per.count(14) == 1);
  CHECK(rig.sender.inputEpochDropCount.load() == 3);
  size_t idx13 = 0, idx14 = 0;
  for (size_t i = 0; i < wire.size(); ++i) {
    if (wire[i].seq == 13) idx13 = i;
    if (wire[i].seq == 14 && idx14 == 0) idx14 = i;
  }
  CHECK(idx14 > idx13);  // the delta follows the whole IDR
}

// [B]
void test_in_flight_old_au_completes_then_idr(Rig& rig) {
  std::printf("[B] an old AU already on the wire when the flush lands completes; the IDR is next\n");
  const uint64_t dropsBefore = rig.sender.inputEpochDropCount.load();
  // A large old AU (many chunks) so the flush can land while it is being sent.
  {
    std::lock_guard<std::mutex> lk(rig.sender.mu);
    rig.sender.queue.push_back(rig.Item(20, false, 2, 900000));
  }
  rig.sender.cv.notify_all();
  // Wait until its first chunks are on the wire, then flush and queue the new epoch's IDR plus one
  // more old delta that was still waiting.
  std::vector<Wire> first;
  {
    std::vector<uint8_t> buf(4096);
    for (int i = 0; i < 200 && first.size() < 3; ++i) {
      const int n = recv(rig.rx, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
      if (n >= static_cast<int>(sizeof(UdpVideoChunkHeader))) {
        UdpVideoChunkHeader h{};
        std::memcpy(&h, buf.data(), sizeof(h));
        first.push_back(Wire{h.seq, h.chunkIndex, h.chunkCount, (h.flags & 0x1u) != 0, (h.flags & 0x10u) != 0, h.streamGeneration});
      }
    }
  }
  CHECK(!first.empty() && first.front().seq == 20);
  {
    std::lock_guard<std::mutex> lk(rig.sender.mu);
    rig.sender.queue.push_back(rig.Item(21, false, 2, 20000));  // old, still queued
    rig.epoch.store(3, std::memory_order_release);              // the flush, mid-send of 20
    rig.sender.queue.push_back(rig.Item(22, true, 3, 60000));   // the new epoch's IDR
  }
  rig.sender.cv.notify_all();
  auto wire = rig.Drain(150);
  wire.insert(wire.begin(), first.begin(), first.end());
  const auto per = chunks_per_seq(wire);
  std::printf("  seq 20: %zu of %u payload chunks received; wire records %zu\n", per.count(20) ? per.at(20) : 0,
              static_cast<unsigned>(wire.front().chunkCount), wire.size());
  CHECK(per.count(20) == 1 && per.at(20) == static_cast<size_t>(wire.front().chunkCount));  // completed
  CHECK(per.count(21) == 0);   // the queued old delta never started
  CHECK(per.count(22) == 1);   // the IDR went
  size_t last20 = 0, first22 = wire.size();
  for (size_t i = 0; i < wire.size(); ++i) {
    if (wire[i].seq == 20) last20 = i;
    if (wire[i].seq == 22 && first22 == wire.size()) first22 = i;
  }
  CHECK(first22 > last20);  // nothing of 20 interleaves after the IDR started
  CHECK(rig.sender.inputEpochDropCount.load() == dropsBefore + 1);
}

// [B'] the case [B] is not: the old AU was dequeued and passed the early check, then held by
// pacing; the flush lands BEFORE its first datagram. Its first datagram was never permitted, so
// it must not start -- the in-flight exception covers only an AU already permitted.
void test_flush_between_dequeue_and_first_datagram(Rig& rig) {
  std::printf("[B'] flush after dequeue but before the first datagram: the AU never starts; the IDR is next\n");
  const uint64_t dropsBefore = rig.sender.inputEpochDropCount.load();
  std::mutex hookMu;
  std::condition_variable hookCv;
  bool paused = false;
  bool release = false;
  rig.sender.beforeFirstDatagramHook = [&](const EncodedSendItem& item) {
    if (item.udpHdr.seq != 40) return;
    std::unique_lock<std::mutex> lk(hookMu);
    paused = true;
    hookCv.notify_all();
    hookCv.wait(lk, [&] { return release; });
  };
  {
    std::lock_guard<std::mutex> lk(rig.sender.mu);
    rig.sender.queue.push_back(rig.Item(40, false, 3, 20000));  // current epoch when dequeued
  }
  rig.sender.cv.notify_all();
  {
    std::unique_lock<std::mutex> lk(hookMu);
    CHECK(hookCv.wait_for(lk, std::chrono::seconds(2), [&] { return paused; }));  // held at the permission point
  }
  // The flush lands while the sender holds seq 40 before its first datagram; the IDR is queued.
  {
    std::lock_guard<std::mutex> lk(rig.sender.mu);
    rig.epoch.store(4, std::memory_order_release);
    rig.sender.queue.push_back(rig.Item(41, true, 4, 60000));
  }
  rig.sender.cv.notify_all();
  {
    std::lock_guard<std::mutex> lk(hookMu);
    release = true;
  }
  hookCv.notify_all();
  const auto wire = rig.Drain(150);
  rig.sender.beforeFirstDatagramHook = nullptr;
  const auto per = chunks_per_seq(wire);
  CHECK(per.count(40) == 0);  // never started: not one datagram
  CHECK(per.count(41) == 1);
  CHECK(!wire.empty() && wire.front().seq == 41 && wire.front().key);
  CHECK(rig.sender.inputEpochDropCount.load() == dropsBefore + 1);
}

// [C]
void test_fence_is_the_epoch_not_the_generation(Rig& rig) {
  std::printf("[C] the fence is the epoch: old / untagged / future epochs are dropped whatever the generation says\n");
  const uint64_t dropsBefore = rig.sender.inputEpochDropCount.load();
  {
    std::lock_guard<std::mutex> lk(rig.sender.mu);
    rig.sender.queue.push_back(rig.Item(30, false, 3, 20000, /*generation=*/7));  // old epoch, "new" target id
    rig.sender.queue.push_back(rig.Item(31, false, 0, 20000, 7));                 // untagged: fence active -> dropped
    rig.sender.queue.push_back(rig.Item(33, false, 9, 20000, 7));                 // future epoch: dropped
    rig.sender.queue.push_back(rig.Item(32, true, 4, 60000, 7));                  // current: sent
  }
  rig.sender.cv.notify_all();
  const auto wire = rig.Drain(150);
  const auto per = chunks_per_seq(wire);
  CHECK(per.count(30) == 0);
  CHECK(per.count(31) == 0);
  CHECK(per.count(33) == 0);
  CHECK(per.count(32) == 1);
  CHECK(rig.sender.inputEpochDropCount.load() == dropsBefore + 3);
  // The sent AU keeps the metadata it was encoded with (no relabelling): generation 7 on the wire.
  for (const auto& w : wire) {
    if (w.seq == 32) CHECK(w.generation == 7);
  }
}

// [D] legacy: with the fence inactive (no epoch reference) an untagged item passes.
void test_untagged_passes_only_when_fence_inactive(Rig& rig) {
  std::printf("[D] untagged items pass only while the fence is inactive\n");
  rig.sender.inputEpochRef = nullptr;
  {
    std::lock_guard<std::mutex> lk(rig.sender.mu);
    rig.sender.queue.push_back(rig.Item(50, false, 0, 20000));
  }
  rig.sender.cv.notify_all();
  auto wire = rig.Drain(150);
  CHECK(chunks_per_seq(wire).count(50) == 1);
  rig.sender.inputEpochRef = &rig.epoch;
  {
    std::lock_guard<std::mutex> lk(rig.sender.mu);
    rig.sender.queue.push_back(rig.Item(51, false, 0, 20000));
  }
  rig.sender.cv.notify_all();
  wire = rig.Drain(150);
  CHECK(chunks_per_seq(wire).count(51) == 0);
}

}  // namespace

int main() {
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);
  Rig rig;
  if (!rig.Start()) {
    std::printf("host_sender_epoch_test: could not start the sender rig\n");
    return 2;
  }
  test_queued_old_aus_are_fenced(rig);
  test_in_flight_old_au_completes_then_idr(rig);
  test_flush_between_dequeue_and_first_datagram(rig);
  test_fence_is_the_epoch_not_the_generation(rig);
  test_untagged_passes_only_when_fence_inactive(rig);
  rig.Stop();
  WSACleanup();
  if (gFailures == 0) {
    std::printf("host_sender_epoch_test: PASS\n");
    return 0;
  }
  std::printf("host_sender_epoch_test: FAIL (%d)\n", gFailures);
  return 1;
}

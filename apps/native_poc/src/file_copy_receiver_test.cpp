// FilePullReceiver against a scripted serving side (t-zdmsd4gb step 3: idle/stall A4, stale answers).
// Two real UdpControlChannels in one process -- the receiver's bulk stream and a peer's -- joined by
// function calls, so the peer can answer, answer slowly, answer wrongly or not at all.
//
//   stall     the peer takes the pulls and never answers: the waiting Read fails (Timeout) once the
//             stall bound passes with no verified chunk, the paste's failure is Idle, and every later
//             Read of it fails too.
//   progress  the peer answers one chunk every 0.6 s against a 1 s bound: a transfer that moves is
//             never cut, however long it takes.
//   stale     before each right answer the peer sends one naming a request id not in flight and one of
//             an older bulk generation: both are rejected, nothing of them is handed on, the bytes
//             delivered are the right ones.
//
//   helper    (helper-shell-token r1) FileHelperChannel keeps the CLASS of the last failed helper
//             start -- read from the launcher's `why` -- through the backoff that follows, and drops
//             it once a start succeeds: the viewer says "not installed" only for a helper found missing.
//
// Tags: pure-logic (no sockets, no files).

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "file_copy_paste_parts.hpp"

using namespace remote60::native_poc;
namespace fc = remote60::native_poc::file_copy;
namespace fn = remote60::native_poc::file_copy::net;

namespace {

int gChecks = 0, gFailures = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}

uint8_t byte_at(uint64_t off) { return static_cast<uint8_t>((off * 131 + 7) & 0xFF); }

enum class PeerMode { Silent, Slow, Stale };

// The wire between the two channels: datagrams queued and delivered by a thread of their own, as a
// socket does -- a channel's Send never re-enters the other channel (or itself) on the same stack.
struct Wire {
  std::mutex mu;
  std::condition_variable cv;
  std::deque<std::pair<int, std::vector<uint8_t>>> q;  // 0 = to the receiver, 1 = to the peer
  bool run = true;
  void Push(int to, const void* d, size_t n) {
    std::lock_guard<std::mutex> lock(mu);
    q.emplace_back(to, std::vector<uint8_t>(static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n));
    cv.notify_all();
  }
};

struct Rig {
  FilePullReceiver rx{4};
  UdpControlChannel peer;
  Wire wire;
  std::atomic<bool> run{true};
  std::thread peerThread, wireThread;
  std::mutex mu;
  std::condition_variable cv;
  std::vector<fc::ReadData> answers;
  FilePasteIdentity id{0x1111, 0x2222, 0x3333, 5};
  static constexpr uint32_t kTx = 0x40000000u | (5u << 2) | 3u;  // receiver's pulls
  static constexpr uint32_t kRx = 0x40000000u | (5u << 2) | 0u;  // chunks

  explicit Rig(PeerMode mode, uint64_t stallUs, uint64_t fileSize) {
    rx.SetStallTimeoutUs(stallUs);
    rx.Start([this](uint64_t, const fc::ReadData& d) {
      std::lock_guard<std::mutex> lock(mu);
      answers.push_back(d);
      cv.notify_all();
    });
    wireThread = std::thread([this] {
      std::unique_lock<std::mutex> lock(wire.mu);
      while (wire.run) {
        wire.cv.wait_for(lock, std::chrono::milliseconds(20), [&] { return !wire.q.empty() || !wire.run; });
        while (!wire.q.empty()) {
          auto item = std::move(wire.q.front());
          wire.q.pop_front();
          lock.unlock();
          if (item.first == 0) rx.OnDatagram(item.second.data(), item.second.size());
          else peer.OnPacket(item.second.data(), item.second.size());
          lock.lock();
        }
      }
    });
    peer.Configure([this](const void* d, size_t n) { wire.Push(0, d, n); return true; }, kRx, kTx, 1200);
    rx.Open([this](const void* d, size_t n) { wire.Push(1, d, n); return true; }, kTx, kRx, 1200, id, {fileSize}, 1);
    peerThread = std::thread([this, mode] {
      std::vector<uint8_t> msg;
      while (run.load()) {
        const bool got = peer.Receive(&msg, 20);
        peer.Tick();
        if (!got || mode == PeerMode::Silent) continue;
        fn::Pull p;
        if (!fn::parse_bulk(msg.data(), msg.size(), &p)) continue;
        fn::Chunk c;
        c.epochTag = p.epochTag;
        c.offerId = p.offerId;
        c.pasteOp = p.pasteOp;
        c.bulkGen = p.bulkGen;
        c.fileIndex = p.fileIndex;
        c.requestId = p.requestId;
        c.offset = p.offset;
        c.data.resize(p.length);
        for (uint32_t i = 0; i < p.length; ++i) c.data[i] = byte_at(p.offset + i);
        fn::sha256(c.data.data(), c.data.size(), &c.sha256);
        if (mode == PeerMode::Stale) {
          fn::Chunk wrongId = c;
          wrongId.requestId = p.requestId + 1000;  // not a pull in flight
          std::vector<uint8_t> w = fn::frame_bulk(wrongId);
          peer.Send(w.data(), w.size());
          fn::Chunk oldGen = c;
          oldGen.bulkGen = p.bulkGen - 1;  // an older paste's generation, same request id
          for (auto& b : oldGen.data) b ^= 0xFF;
          fn::sha256(oldGen.data.data(), oldGen.data.size(), &oldGen.sha256);
          w = fn::frame_bulk(oldGen);
          peer.Send(w.data(), w.size());
        }
        if (mode == PeerMode::Slow) std::this_thread::sleep_for(std::chrono::milliseconds(600));
        const std::vector<uint8_t> w = fn::frame_bulk(c);
        peer.Send(w.data(), w.size());
      }
    });
  }
  ~Rig() {
    run.store(false);
    if (peerThread.joinable()) peerThread.join();
    rx.Stop();
    peer.Close();
    {
      std::lock_guard<std::mutex> lock(wire.mu);
      wire.run = false;
      wire.cv.notify_all();
    }
    if (wireThread.joinable()) wireThread.join();
  }
  void Read(uint64_t offset, uint32_t length) {
    fc::ReadRequest r;
    r.offerId = id.offerId;
    r.pasteOp = id.pasteOp;
    r.fileIndex = 0;
    r.offset = offset;
    r.length = length;
    rx.Submit(r, 1);
  }
  bool WaitAnswers(size_t n, int ms) {
    std::unique_lock<std::mutex> lock(mu);
    return cv.wait_for(lock, std::chrono::milliseconds(ms), [&] { return answers.size() >= n; });
  }
};

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("--- stall: the serving side takes pulls and never answers ---\n");
  {
    Rig r(PeerMode::Silent, 1000000, 1u << 20);
    const auto t0 = std::chrono::steady_clock::now();
    r.Read(0, 256 * 1024);
    const bool answered = r.WaitAnswers(1, 5000);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    check("the waiting Read is answered, not held for ever", answered, std::to_string(s) + " s");
    check("...with Timeout, after the 1 s bound (not before)", answered && r.answers[0].status == fc::Status::Timeout && s >= 0.9,
          answered ? "status=" + std::to_string(static_cast<int>(r.answers[0].status)) : "");
    check("...and the paste's failure is Idle (A4)", r.rx.failure() == fn::PasteEndReason::Idle);
    r.Read(256 * 1024, 1024);
    check("a later Read of the stalled paste fails at once", r.WaitAnswers(2, 1000) && r.answers[1].status == fc::Status::ReadError);
  }

  std::printf("\n--- progress: one chunk every 0.6 s against a 1 s bound ---\n");
  {
    Rig r(PeerMode::Slow, 1000000, 4u * 64u * 1024u);
    const auto t0 = std::chrono::steady_clock::now();
    r.Read(0, 4 * 64 * 1024);
    const bool answered = r.WaitAnswers(1, 20000);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    bool same = answered && r.answers[0].status == fc::Status::Ok && r.answers[0].data.size() == 4u * 64u * 1024u;
    for (size_t i = 0; same && i < r.answers[0].data.size(); ++i) same = r.answers[0].data[i] == byte_at(i);
    check("a transfer longer than the bound, but moving, completes", same && s > 1.0,
          std::to_string(s) + " s, status=" + (answered ? std::to_string(static_cast<int>(r.answers[0].status)) : "none"));
    check("...and is not marked stalled", r.rx.failure() == fn::PasteEndReason::None);
  }

  std::printf("\n--- stale: answers not in flight / of an older generation ---\n");
  {
    Rig r(PeerMode::Stale, 30000000, 1u << 20);
    r.Read(0, 256 * 1024);
    const bool answered = r.WaitAnswers(1, 10000);
    bool same = answered && r.answers[0].status == fc::Status::Ok && r.answers[0].data.size() == 256u * 1024u;
    for (size_t i = 0; same && i < r.answers[0].data.size(); ++i) same = r.answers[0].data[i] == byte_at(i);
    const auto c = r.rx.GetCounters();
    check("the Read is answered with exactly the right bytes", same);
    check("...every stale / older-generation answer was rejected (2 per pull)", c.chunksRejected >= 8,
          "rejected=" + std::to_string(c.chunksRejected) + " verified=" + std::to_string(c.chunksVerified));
    check("...and none of them failed the paste or counted as progress beyond the right chunks",
          r.rx.failure() == fn::PasteEndReason::None && c.bytesDelivered == 256u * 1024u);
  }

  std::printf("\n--- helper: the class of the last failed start ---\n");
  {
    // A scripted launcher: what it answers is the test's; the channel only reads the class back.
    std::string next = "missing: stage=helper-exe err=2 (helper-exe-missing)";
    bool succeed = false;
    FileHelperChannel ch;
    FileHelperChannel::Config cfg;
    cfg.backoffFirstMs = 300;
    cfg.backoffMaxMs = 300;
    cfg.launcher = [&](fc::HelperLink*, std::string* why) {
      if (succeed) return true;
      *why = next;
      return false;
    };
    ch.Configure(cfg, [](uint64_t, uint64_t, const fc::PipeFrame&) {}, [](uint64_t, uint64_t) {});
    std::string why;
    check("nothing failed yet: no class", ch.lastLaunchFailure() == fc::LaunchFailure::None);
    check("a start that finds the helper missing fails ...", !ch.Ensure(&why), why);
    check("...and is kept as Missing", ch.lastLaunchFailure() == fc::LaunchFailure::Missing);
    check("during the backoff no start is made ...", !ch.Ensure(&why) && why == "helper start backing off", why);
    check("...and the class is still Missing (not lost to 'backing off')", ch.lastLaunchFailure() == fc::LaunchFailure::Missing);
    Sleep(350);
    next = "token-rejected: stage=shell-token err=0 (not-medium)";
    check("a later start refused for its token ...", !ch.Ensure(&why), why);
    check("...is TokenRejected, not Missing", ch.lastLaunchFailure() == fc::LaunchFailure::TokenRejected);
    Sleep(350);
    next = "some launcher that names no class";
    check("a failure that names no class ...", !ch.Ensure(&why));
    check("...is None: never taken for Missing", ch.lastLaunchFailure() == fc::LaunchFailure::None);
    Sleep(350);
    next = "missing: stage=helper-exe err=2 (helper-exe-missing)";
    (void)ch.Ensure(&why);
    Sleep(350);
    succeed = true;
    (void)ch.Ensure(&why);
    check("a start that succeeds clears the class", ch.lastLaunchFailure() == fc::LaunchFailure::None);
    ch.Stop();
  }

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "PASSED", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

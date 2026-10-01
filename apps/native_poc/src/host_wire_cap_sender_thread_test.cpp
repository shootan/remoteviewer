// bitrate-hard-cap r4 R1 -- the REAL sender thread services NACK replay, idle and during a long AU.
//
// Codex R1: r3 recorded replays correctly but the real SenderState::StartThread never serviced them
// when idle (the cv predicate ignored pending replays) nor during a long cap-paced AU (Drain ran only
// between whole AUs). host_wire_cap_defer_test drove DrainPendingReplays directly and so missed both.
// This spins up the REAL sender thread over a real loopback socket and, WITHOUT ever calling
// DrainPendingReplays itself, injects NACKs through the real reader entry point (RetransmitAu) and
// asserts the replay datagrams actually reach the socket:
//   (a) idle: all queues empty, the sender asleep, a NACK alone must wake it and replay.
//   (b) long AU: during a ~1.4 s cap-paced IDR, a same-FEC-group 2-chunk NACK is replayed BEFORE the
//       original AU finishes (the interleave), not only after.
// Tag: network (loopback).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "host_args.hpp"
#include "host_encoded_sender.hpp"
#include "host_main_loop_mailbox.hpp"
#include "host_net_io.hpp"
#include "host_session.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

using namespace remote60::native_poc;

namespace {
int g_checks = 0, g_failed = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

EncodedSendItem make_item(uint32_t seq, uint64_t gen, const std::vector<uint8_t>& bytes, bool key,
                          uint64_t mediaEpoch, uint32_t fps) {
  EncodedSendItem it;
  it.bytes = bytes;
  it.keyFrame = key;
  it.frameIntervalUs = 1'000'000ULL / (fps ? fps : 60);
  it.mediaEpoch = mediaEpoch;
  it.inputEpoch = 0;
  it.udpHdr.magic = kMagic;
  it.udpHdr.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
  it.udpHdr.size = static_cast<uint16_t>(sizeof(UdpVideoChunkHeader));
  it.udpHdr.seq = seq;
  it.udpHdr.codec = static_cast<uint16_t>(UdpCodec::H264);
  it.udpHdr.flags = key ? 0x1u : 0u;
  it.udpHdr.width = 1920;
  it.udpHdr.height = 1080;
  it.udpHdr.payloadSize = static_cast<uint32_t>(bytes.size());
  it.udpHdr.streamGeneration = gen;
  return it;
}

int drain(SOCKET s) {
  char buf[2048];
  int n = 0;
  for (;;) {
    const int r = recv(s, buf, sizeof(buf), 0);
    if (r <= 0) break;
    ++n;
  }
  return n;
}

void enqueue(SenderState& sender, EncodedSendItem item) {
  {
    std::lock_guard<std::mutex> lk(sender.mu);
    sender.queue.push_back(std::move(item));
  }
  sender.cv.notify_one();
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);
  SOCKET rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in rxAddr{};
  rxAddr.sin_family = AF_INET;
  rxAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  rxAddr.sin_port = 0;
  bind(rx, reinterpret_cast<sockaddr*>(&rxAddr), sizeof(rxAddr));
  int alen = sizeof(rxAddr);
  getsockname(rx, reinterpret_cast<sockaddr*>(&rxAddr), &alen);
  int rcvbuf = 8 * 1024 * 1024;
  setsockopt(rx, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&rcvbuf), sizeof(rcvbuf));
  u_long nb = 1;
  ioctlsocket(rx, FIONBIO, &nb);
  SOCKET tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  const uint32_t mtu = 1200;
  const uint64_t gen = 3;
  const uint64_t kEpoch = 1;

  auto make_sender = [&](uint64_t capBps) -> std::unique_ptr<SenderState> {
    auto s = std::make_unique<SenderState>();
    s->nackEnabled.store(true, std::memory_order_relaxed);
    s->StartWireCap(capBps, mtu, /*enabled=*/true);
    std::lock_guard<std::mutex> lk(s->mu);
    s->peer = rxAddr;
    s->peerReady = true;
    s->udpPeer = rxAddr;
    s->udpPeerReady = true;
    return s;
  };
  auto stop_sender = [&](SenderState& s) {
    s.stop.store(true, std::memory_order_release);
    s.cv.notify_all();
    if (s.wireLimiter) s.wireLimiter->Stop();
    if (s.thread.joinable()) s.thread.join();
  };

  std::printf("--- R1 (a): an idle sender wakes on a NACK alone and replays (no direct Drain) ---\n");
  {
    auto sender = make_sender(6'000'000);
    Args args;
    args.udpMtu = mtu;
    SessionState session;
    session.clientSock = tx;
    MainLoopMailbox mailbox;
    sender->StartThread(VideoTransport::Udp, true, args, session, mailbox);

    // Send one small AU so it is cached + started, then let the stream go idle.
    const uint32_t seq = 10;
    std::vector<uint8_t> smallAu(8u * 1024u, 0xAB);  // ~7 data chunks + parity ('small' is a WinAPI macro)
    enqueue(*sender, make_item(seq, gen, smallAu, /*key=*/true, kEpoch, 60));
    // Wait for the original to be sent (txFrames == 1) and drain it off the socket.
    for (int i = 0; i < 200 && sender->txFrames.load() == 0; ++i) Sleep(5);
    Sleep(30);
    const int original = drain(rx);
    check("the original small AU was sent", sender->txFrames.load() == 1 && original > 0,
          "txFrames=" + std::to_string(sender->txFrames.load()) + " datagrams=" + std::to_string(original));

    // Now idle: the sender is asleep in cv.wait. Inject a NACK through the real reader path only.
    const uint64_t servedBefore = sender->replayServedChunks.load();
    const uint16_t missing[] = {1, 2};
    sender->RetransmitAu(tx, rxAddr, gen, seq, missing, 2);
    // Wait for the sender to wake and replay on its own -- NO DrainPendingReplays call here.
    int replayed = 0;
    for (int i = 0; i < 200 && replayed < 2; ++i) {
      Sleep(5);
      replayed += drain(rx);
    }
    check("an idle sender woke on the NACK alone and replayed the 2 chunks (R1 idle fix)",
          replayed == 2 && sender->replayServedChunks.load() - servedBefore == 2,
          "replayed=" + std::to_string(replayed) + " served=" +
              std::to_string(sender->replayServedChunks.load() - servedBefore));
    stop_sender(*sender);
  }

  std::printf("--- R1 (b): during a long cap-paced IDR, a NACK is replayed BEFORE the AU finishes ---\n");
  {
    auto sender = make_sender(1'500'000);  // ~1.4 s for a 208 KiB IDR
    Args args;
    args.udpMtu = mtu;
    SessionState session;
    session.clientSock = tx;
    MainLoopMailbox mailbox;
    sender->StartThread(VideoTransport::Udp, true, args, session, mailbox);

    const uint32_t seq = 20;
    std::vector<uint8_t> idr(208u * 1024u, 0xCD);  // ~178 data chunks -> long cap-paced send
    enqueue(*sender, make_item(seq, gen, idr, /*key=*/true, kEpoch, 60));

    // Let the send actually start (first datagrams on the wire, AU marked started), then inject a NACK
    // for two chunks of the SAME FEC group while the AU is still going out.
    Sleep(60);
    (void)drain(rx);  // discard the originals so far; we are measuring the replay, not counting all
    const uint64_t servedBefore = sender->replayServedChunks.load();
    const uint16_t missing[] = {0, 1};
    sender->RetransmitAu(tx, rxAddr, gen, seq, missing, 2);

    // Poll until the replay is served; record whether it happened while the AU was STILL sending
    // (txFrames still 0 means the single IDR has not completed yet -> the replay interleaved).
    bool servedDuringSend = false;
    for (int i = 0; i < 600; ++i) {  // up to ~3 s
      Sleep(5);
      (void)drain(rx);
      const uint64_t served = sender->replayServedChunks.load() - servedBefore;
      if (served >= 2) {
        servedDuringSend = (sender->txFrames.load() == 0);  // AU not yet completed
        break;
      }
      if (sender->txFrames.load() >= 1) break;  // the AU finished first
    }
    check("the 2-chunk NACK was replayed during the long IDR send, not only after it (R1 interleave)",
          servedDuringSend && sender->replayServedChunks.load() - servedBefore >= 2,
          "servedDuringSend=" + std::to_string(servedDuringSend ? 1 : 0) +
              " served=" + std::to_string(sender->replayServedChunks.load() - servedBefore) +
              " txFrames=" + std::to_string(sender->txFrames.load()));
    stop_sender(*sender);
  }

  std::printf("--- R1/G3 (c): a NACK at the idle boundary is never lost (hammer the check->sleep edge) ---\n");
  {
    auto sender = make_sender(6'000'000);
    Args args;
    args.udpMtu = mtu;
    SessionState session;
    session.clientSock = tx;
    MainLoopMailbox mailbox;
    sender->StartThread(VideoTransport::Udp, true, args, session, mailbox);
    const uint32_t seq = 30;
    std::vector<uint8_t> au(8u * 1024u, 0xAB);
    enqueue(*sender, make_item(seq, gen, au, true, kEpoch, 60));
    for (int i = 0; i < 200 && sender->txFrames.load() == 0; ++i) Sleep(5);
    Sleep(30);
    (void)drain(rx);

    // Repeatedly inject a single-chunk NACK right as the stream is idle (no Sleep before the inject),
    // so the record lands near the sender's predicate-check/sleep edge. Every one must be served --
    // newReplayWork is raised under sender.mu, so the wake cannot be lost. (G3 lost-wake.)
    int lost = 0;
    const int rounds = 40;
    for (int i = 0; i < rounds; ++i) {
      const uint64_t before = sender->replayServedChunks.load();
      const uint16_t miss[] = {static_cast<uint16_t>(1 + (i % 5))};
      sender->RetransmitAu(tx, rxAddr, gen, seq, miss, 1);
      bool served = false;
      for (int k = 0; k < 100; ++k) {  // up to ~500 ms
        if (sender->replayServedChunks.load() > before) { served = true; break; }
        Sleep(5);
      }
      (void)drain(rx);
      if (!served) ++lost;
    }
    check("G3: every NACK at the idle boundary was served (no lost wake over 40 rounds)", lost == 0,
          "lost=" + std::to_string(lost) + "/" + std::to_string(rounds));
    stop_sender(*sender);
  }

  std::printf("--- G3 (d): the reader (RetransmitAu) is not blocked behind a replay's token wait ---\n");
  {
    auto sender = make_sender(1'500'000);  // low cap -> interleave does blocking token waits
    Args args;
    args.udpMtu = mtu;
    SessionState session;
    session.clientSock = tx;
    MainLoopMailbox mailbox;
    sender->StartThread(VideoTransport::Udp, true, args, session, mailbox);
    const uint32_t seq = 40;
    std::vector<uint8_t> idr(208u * 1024u, 0xCD);  // long cap-paced send -> interleave active
    enqueue(*sender, make_item(seq, gen, idr, true, kEpoch, 60));
    Sleep(80);  // the send is well under way, interleave blocking on tokens

    // Time many RetransmitAu calls (the reader entry point) while the sender interleaves under a low
    // cap. Each must return quickly -- Drain releases pendingReplayMu during its blocking token wait,
    // so the reader never waits behind it. (G3 reader-blocking.)
    uint64_t maxCallUs = 0;
    for (int i = 0; i < 200; ++i) {
      const uint16_t miss[] = {static_cast<uint16_t>(i % 100)};
      const uint64_t t0 = qpc_now_us();
      sender->RetransmitAu(tx, rxAddr, gen, seq, miss, 1);
      const uint64_t dtUs = qpc_now_us() - t0;
      maxCallUs = std::max(maxCallUs, dtUs);
      (void)drain(rx);
    }
    check("G3: the reader's RetransmitAu never blocked behind the interleave token wait (max < 20 ms)",
          maxCallUs < 20000, "maxCallUs=" + std::to_string(maxCallUs));
    stop_sender(*sender);
  }

  closesocket(rx);
  closesocket(tx);
  WSACleanup();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}

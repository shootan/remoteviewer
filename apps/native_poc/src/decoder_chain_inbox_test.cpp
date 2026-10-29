// apk-inbox C (design caplife/design_apk_decoder_inbox_r2.md r3): the chain-aware inbox, the recovery episode and the
// key-request descriptor (decoder_chain_inbox.hpp, decoder_key_request.hpp) -- the decision table, case by case, on
// the pure logic. No codec, no thread, no clock: time is a number passed in. Tag: pure-logic.
//
//   remote60_decoder_chain_inbox_test

#include <cstdio>
#include <string>
#include <vector>

// <> (not ""): the header is taken from the include path -- REMOTE60_CHAIN_INBOX_DIR first (a mutated copy in the
// negative runs), never this file's own directory.
#include <decoder_chain_inbox.hpp>
#include <decoder_key_request.hpp>

using namespace remote60::native_poc;

namespace {
int gFailures = 0, gChecks = 0;
void check(const std::string& name, bool cond, const std::string& detail = {}) {
  ++gChecks;
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
  if (!cond) ++gFailures;
}

struct Au {
  uint64_t gen = 1;
  uint32_t seq = 0;
  bool key = false;
  size_t bytes = 1000;
};
struct AuInfo {
  ChainAuInfo operator()(const Au& a) const { return ChainAuInfo{a.gen, a.seq, a.key, a.bytes}; }
};
using Inbox = ChainInbox<Au, AuInfo>;
constexpr size_t kFrames = 8;
constexpr size_t kBytes = 4u << 20;
Inbox make() { return Inbox(kFrames, kBytes, AuInfo{}); }
Au d(uint32_t seq, size_t bytes = 1000, uint64_t gen = 1) { return Au{gen, seq, false, bytes}; }
Au k(uint32_t seq, size_t bytes = 1500, uint64_t gen = 1) { return Au{gen, seq, true, bytes}; }
std::vector<uint32_t> drain(Inbox& in) {
  std::vector<uint32_t> out;
  Au a;
  while (in.Pop(&a)) out.push_back(a.seq);
  return out;
}
std::string seqs(const std::vector<uint32_t>& v) {
  std::string s;
  for (uint32_t x : v) s += (s.empty() ? "" : ",") + std::to_string(x);
  return s;
}
constexpr uint64_t S = 1000000ULL;  // one second in us
}  // namespace

int main() {
  std::printf("decoder_chain_inbox_test (apk-inbox C: the inbox decision table, the episode, the descriptor)\n");

  std::puts("\n--- inbox: order");
  {
    Inbox in = make();
    bool all = true;
    for (uint32_t s = 1; s <= 8; ++s) all = all && in.Offer(d(s)).admit == ChainAdmit::Admitted;
    check("steady in-order frames within the bounds: all admitted, no loss, no gap", all && in.frames() == 8 &&
          !in.gapOpen() && in.lost() == 0);
    check("the same AU again: Duplicate, not admitted", in.Offer(d(8)).admit == ChainAdmit::Duplicate && in.frames() == 8);
    check("an older AU (reordered): Stale, not admitted", in.Offer(d(5)).admit == ChainAdmit::Stale && in.frames() == 8);
    check("a dropped stale / duplicate AU is no loss (no gap)", !in.gapOpen());
  }
  {
    Inbox in = make();
    const bool a = in.Offer(d(0xFFFFFFFEu)).admit == ChainAdmit::Admitted;
    const bool b = in.Offer(d(0xFFFFFFFFu)).admit == ChainAdmit::Admitted;
    const bool c = in.Offer(d(0u)).admit == ChainAdmit::Admitted;
    const bool e = in.Offer(d(1u)).admit == ChainAdmit::Admitted;
    const bool old = in.Offer(d(0xFFFFFFFFu)).admit == ChainAdmit::Stale;
    check("wrap: ...FFFE, ...FFFF, 0, 1 in order are admitted; ...FFFF after 1 is Stale", a && b && c && e && old,
          seqs(drain(in)));
  }
  {
    Inbox in = make();
    in.Offer(d(100));
    int dropped = 0;
    for (int i = 0; i < 29; ++i) dropped += in.Offer(d(5)).admit != ChainAdmit::Admitted ? 1 : 0;
    const bool stillDropped = in.Offer(d(6)).admit == ChainAdmit::Stale;  // the 30th in a row
    const bool resynced = in.Offer(d(7)).admit == ChainAdmit::Admitted && in.orderResyncs() == 1;
    const bool after = in.Offer(d(8)).admit == ChainAdmit::Admitted && in.Offer(d(8)).admit == ChainAdmit::Duplicate;
    check("safety valve: 30 older AUs in a row are dropped, then the order restarts from the next (counted), and is "
          "kept from there", dropped == 29 && stillDropped && resynced && after);
  }
  {
    Inbox in = make();
    in.Offer(d(10, 1000, 1));
    const bool newGen = in.Offer(k(3, 1500, 2)).admit == ChainAdmit::Admitted;
    check("another generation is not ordered against the last one (the selection gate judges generations)", newGen);
  }

  std::puts("\n--- inbox: overflow, case 3 (no IDR in the inbox)");
  {
    Inbox in = make();
    for (uint32_t s = 1; s <= 8; ++s) in.Offer(d(s));
    const auto r = in.Offer(d(9));
    check("9 deltas, no IDR: everything goes as ONE loss, the gap opens after the newest lost (9)",
          r.loss && r.lostFrames == 9 && in.frames() == 0 && in.gapOpen() && in.gapAfterSeq() == 9,
          "lost=" + std::to_string(r.lostFrames) + " gapAfter=" + std::to_string(in.gapAfterSeq()));
    const auto g = in.Offer(d(10));
    check("a delta after the loss: refused (GapDelta), the loss point moves to it, nothing enqueued",
          g.admit == ChainAdmit::GapDelta && g.loss && in.frames() == 0 && in.gapAfterSeq() == 10);
    const auto key = in.Offer(k(11));
    check("the next IDR: admitted, the gap closes, it is the head", key.admit == ChainAdmit::Admitted && !key.loss &&
          !in.gapOpen() && in.frames() == 1 && in.HoldsKeyAfter(10));
    in.Offer(d(12));
    check("its suffix follows", seqs(drain(in)) == "11,12");
  }

  std::puts("\n--- inbox: overflow, case 1 (an IDR + suffix fits)");
  {
    Inbox in = make();
    for (uint32_t s = 1; s <= 6; ++s) in.Offer(d(s));
    in.Offer(k(7));
    in.Offer(d(8));
    const auto r = in.Offer(d(9));  // 9 > 8
    in.Offer(d(10));
    check("6 old deltas + IDR + 3: the IDR and its whole suffix kept, the old chain skipped -- NO loss, no gap",
          !r.loss && r.skippedFrames == 6 && r.keysLost == 0 && !in.gapOpen() && seqs(drain(in)) == "7,8,9,10",
          "skipped=" + std::to_string(r.skippedFrames));
  }
  {
    Inbox in = make();
    in.Offer(k(1));
    for (uint32_t s = 2; s <= 4; ++s) in.Offer(d(s));
    in.Offer(k(5));
    for (uint32_t s = 6; s <= 9; ++s) in.Offer(d(s));  // 9 frames: key1's suffix 9 does not fit, key5's 5 does
    check("two IDRs, the first's suffix too long: the EARLIEST that fits (5) kept, its suffix whole; the skipped IDR "
          "of the same generation is not 'lost'", seqs(drain(in)) == "5,6,7,8,9" && !in.gapOpen());
  }
  {
    Inbox in = make();
    in.Offer(k(1, 1500, 1));
    in.Offer(d(2, 1000, 1));
    in.Offer(k(3, 1500, 2));
    for (uint32_t s = 4; s <= 8; ++s) in.Offer(d(s, 1000, 2));
    const auto r = in.Offer(d(9, 1000, 2));
    check("an IDR of generation 1 skipped for one of generation 2: reported lost for generation 1 (KeyLost)",
          r.keysLost == 1 && r.keyLostGen == 1 && !r.loss);
  }

  std::puts("\n--- inbox: overflow, case 2 (an IDR, but no suffix fits)");
  {
    Inbox in = make();
    in.Offer(k(1));
    for (uint32_t s = 2; s <= 8; ++s) in.Offer(d(s));
    const auto r = in.Offer(d(9));  // key + 8 deltas = 9
    check("IDR + 8 deltas: the IDR and 7 kept, the newest cut -- a loss at the tail, the gap opens after it",
          r.loss && r.lostFrames == 1 && r.lossSeq == 9 && in.gapOpen() && in.gapAfterSeq() == 9 && in.frames() == 8,
          "frames=" + std::to_string(in.frames()));
    check("a delta after that: refused", in.Offer(d(10)).admit == ChainAdmit::GapDelta);
    check("the next IDR: admitted (the bounds trim the oldest chain for it)", in.Offer(k(11)).admit == ChainAdmit::Admitted);
    const auto out = drain(in);
    check("the IDR is kept and goes in after... nothing older than a decodable chain", !out.empty() && out.back() == 11,
          seqs(out));
  }
  {
    Inbox in = make();
    in.Offer(k(1, 3u << 20));
    const auto r = in.Offer(d(2, 1536u << 10));  // 3 MiB + 1.5 MiB > 4 MiB
    check("bytes: an IDR of 3 MiB and a delta of 1.5 MiB: the delta cut (a loss), the IDR kept, <= 4 MiB",
          r.loss && in.frames() == 1 && in.bytes() <= kBytes && in.gapOpen());
  }

  std::puts("\n--- inbox: oversize");
  {
    Inbox in = make();
    const auto r = in.Offer(k(1, (4u << 20) + 1));
    check("an IDR over the whole byte bound: refused, a loss, its generation's IDR lost, the gap opens",
          r.admit == ChainAdmit::Oversize && r.loss && r.keysLost == 1 && in.gapOpen() && in.frames() == 0);
    check("a delta after it: refused", in.Offer(d(2)).admit == ChainAdmit::GapDelta);
  }

  std::puts("\n--- inbox: a loss seen outside it (MarkLoss)");
  {
    Inbox in = make();
    in.Offer(d(1));
    in.MarkLoss(1);
    check("the decoder lost its chain at 1: the gap opens, the next delta is refused", in.gapOpen() &&
          in.Offer(d(2)).admit == ChainAdmit::GapDelta);
    in.Offer(k(3));
    check("an IDR closes it", !in.gapOpen());
  }
  {
    Inbox in = make();
    in.Offer(d(10));
    in.Offer(k(11));
    in.Offer(d(12));
    in.MarkLoss(10);
    check("a loss at 10 with an IDR (11) already held after it: no gap -- 13 continues 11's chain", !in.gapOpen() &&
          in.Offer(d(13)).admit == ChainAdmit::Admitted);
  }

  std::puts("\n--- episode: schedule");
  {
    KeyRecoveryEpisode e;
    e.OnLoss(kKeyCauseRecovery, 9, 0);
    uint64_t id = 0;
    const bool w0 = e.TakeWant(0, false, &id);
    const bool again = e.TakeWant(10, false, nullptr);
    check("a loss opens ONE episode; its first want at once; no second want until it is sent", w0 && !again &&
          e.state() == KeyRecoveryEpisode::State::Open && e.opened() == 1);
    e.OnSent(id, 0);
    const bool early = e.TakeWant(S - 1, false, nullptr);
    const bool due1 = e.TakeWant(S, false, nullptr);
    e.OnSent(id, S);
    const bool early2 = e.TakeWant(3 * S - 1, false, nullptr);
    const bool due2 = e.TakeWant(3 * S, false, nullptr);
    e.OnSent(id, 3 * S);
    const bool due3 = e.TakeWant(7 * S, false, nullptr);
    e.OnSent(id, 7 * S);
    const bool fifth = e.TakeWant(8 * S, false, nullptr);
    check("sends at t0, +1, +3, +7 s (1 / 2 / 4 s after the previous SEND), nothing earlier, no 5th",
          !early && due1 && !early2 && due2 && due3 && !fifth && e.attempts() == 4 && e.sent() == 4);
    const bool notYet = e.Tick(11 * S - 1, false);
    const bool ex = e.Tick(11 * S, false);
    check("exhausted only after the final window (4 s after the 4th send)", !notYet && ex &&
          e.state() == KeyRecoveryEpisode::State::Exhausted && e.exhausted() == 1);
    e.OnLoss(kKeyCauseRecovery, 30, 12 * S);
    check("a loss while exhausted: no new episode, no want", e.state() == KeyRecoveryEpisode::State::Exhausted &&
          !e.TakeWant(20 * S, false, nullptr) && e.opened() == 1);
    check("a later IDR committed after the loss point ends it (the host's periodic key)", e.OnKeyCommitted(31) &&
          e.state() == KeyRecoveryEpisode::State::Idle);
  }
  {
    KeyRecoveryEpisode e;
    e.OnLoss(kKeyCauseRecovery, 9, 0);
    uint64_t id = 0;
    e.TakeWant(0, false, &id);
    const bool refusedNoSend = !e.TakeWant(500000, false, nullptr) && e.attempts() == 0;
    check("a want the session could not send (limiter): not an attempt, not handed out again early", refusedNoSend);
    check("... handed out again only after kReissueUs", !e.TakeWant(KeyRecoveryEpisode::kReissueUs - 1, false, nullptr) &&
          e.TakeWant(KeyRecoveryEpisode::kReissueUs, false, nullptr) && e.reissued() == 1 && e.attempts() == 0);
  }

  {
    KeyRecoveryEpisode e;
    e.OnLoss(kKeyCauseRecovery, 9, 0);
    uint64_t id = 0;
    e.TakeWant(0, false, &id);
    e.OnWantDropped(id);  // judged moot at the send point (a candidate turned up)
    check("a want dropped as moot: not an attempt, and the next one is not held back for kReissueUs",
          e.attempts() == 0 && e.TakeWant(10, false, nullptr) && e.reissued() == 0);
  }

  std::puts("\n--- episode: a candidate held, lost again, committed");
  {
    KeyRecoveryEpisode e;
    e.OnLoss(kKeyCauseRecovery, 9, 0);
    uint64_t id = 0;
    e.TakeWant(0, false, &id);
    e.OnSent(id, 0);
    check("a usable IDR preserved: no want even when due", !e.TakeWant(5 * S, true, nullptr));
    e.OnLoss(kKeyCauseRecovery, 12, 5 * S);  // that candidate lost again
    check("lost again: the SAME episode resumes with its budget (attempts kept, same id)",
          e.id() == id && e.attempts() == 1 && e.opened() == 1 && e.TakeWant(5 * S, false, nullptr));
    check("an IDR committed at or before the loss point does not end it", !e.OnKeyCommitted(12) && !e.OnKeyCommitted(11));
    check("an IDR committed after it does", e.OnKeyCommitted(13) && e.state() == KeyRecoveryEpisode::State::Idle &&
          e.closed() == 1);
    e.OnLoss(kKeyCauseRecovery, 20, 6 * S);
    check("a loss after that opens a NEW episode (new id, full budget)", e.id() != id && e.attempts() == 0 &&
          e.opened() == 2);
  }
  {
    KeyRecoveryEpisode e;
    e.OnLoss(kKeyCauseRecovery, 9, 0);
    uint64_t id = 0;
    for (uint64_t t : {0ULL, S, 3 * S, 7 * S}) {
      e.TakeWant(t, false, &id);
      e.OnSent(id, t);
    }
    check("past the final window with a candidate held: NOT exhausted", !e.Tick(20 * S, true) &&
          e.state() == KeyRecoveryEpisode::State::Open);
    e.OnLoss(kKeyCauseRecovery, 15, 21 * S);
    check("the candidate lost after the window: exhausted at once (the deadlines are the episode's)",
          e.Tick(21 * S, false) && e.state() == KeyRecoveryEpisode::State::Exhausted);
  }

  std::puts("\n--- episode: invalidation");
  {
    KeyRecoveryEpisode e;
    e.OnLoss(kKeyCauseRecovery, 9, 0);
    uint64_t id = 0;
    e.TakeWant(0, false, &id);
    e.Invalidate();
    check("a new pick: the old episode is not valid, its send counts nothing", !e.StillValid(id) &&
          e.state() == KeyRecoveryEpisode::State::Idle);
    e.OnSent(id, 1);
    check("... (a stale send recorded apart)", e.staleSends() == 1 && e.attempts() == 0);
    e.OnLoss(kKeyCauseRecovery, 30, 2);
    uint64_t id2 = 0;
    check("the next loss is a new episode whose wants are its own", e.TakeWant(2, false, &id2) && id2 != id &&
          e.StillValid(id2) && !e.StillValid(id));
  }

  std::puts("\n--- descriptor");
  {
    DecoderKeyRequest a;
    a.causes = kKeyCauseRecovery;
    a.sinkEpoch = 5;
    a.episodeId = 3;
    DecoderKeyRequest o;
    o.causes = kKeyCauseOwed;
    o.owedToken = 77;
    a.Merge(o);
    check("recovery + owed coalesced: both causes kept with their tokens", a.causes == (kKeyCauseRecovery | kKeyCauseOwed) &&
          a.episodeId == 3 && a.owedToken == 77 && a.sinkEpoch == 5);
    DecoderKeyRequest n;
    n.causes = kKeyCauseCodecUp;
    n.sinkEpoch = 6;
    a.Merge(n);
    check("a newer sink epoch: the older sink-scoped cause (recovery) dropped, owed and the new one kept",
          a.causes == (kKeyCauseOwed | kKeyCauseCodecUp) && a.sinkEpoch == 6);
    DecoderKeyRequest p;
    p.causes = kKeyCausePipeline;
    a.Merge(p);
    check("the pipeline cause rides along", (a.causes & kKeyCausePipeline) != 0 && a.sinkEpoch == 6);
  }

  std::printf("\ndecoder_chain_inbox_test: %d checks, %d failed\nRESULT: %s\n", gChecks, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}

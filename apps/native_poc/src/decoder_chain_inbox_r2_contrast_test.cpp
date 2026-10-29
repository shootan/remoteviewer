// apk-inbox C r2 (Codex codex_apk_inbox_c_src_r1.md N1-a, N3): the two pure counterexamples Codex measured on 5fe90a9,
// written ONLY with what both the r1 and the r2 header have (Offer / Pop / one-argument MarkLoss / counts) -- so the same
// file builds against 5fe90a9's decoder_chain_inbox.hpp (REMOTE60_CHAIN_INBOX_DIR) and must FAIL there, PASS on r2.
// Tag: pure-logic.
//
//   remote60_decoder_chain_inbox_r2_contrast_test

#include <cstdio>
#include <string>

// <> (not ""): the header from the include path -- REMOTE60_CHAIN_INBOX_DIR first (another source in the negative runs).
#include <decoder_chain_inbox.hpp>

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
}  // namespace

int main() {
  std::printf("decoder_chain_inbox_r2_contrast_test (Codex's N1-a / N3 counterexamples)\n");
  {
    Inbox in(8, 4u << 20, AuInfo{});
    in.Offer(Au{1, 12, false});
    in.Offer(Au{1, 13, false});
    in.Offer(Au{1, 14, true});
    in.MarkLoss(11);
    Au first;
    const bool popped = in.Pop(&first);
    check("N1-a: delta12, delta13, IDR14 queued, a loss at 11 -> the first Pop is IDR14 (not the broken delta12)",
          popped && first.seq == 14 && first.key, "first=" + std::to_string(first.seq) + " key=" + std::to_string(first.key));
  }
  {
    Inbox in(8, 4u << 20, AuInfo{});
    in.Offer(Au{1, 100, false});
    int admitted = 0;
    for (int i = 0; i < 40; ++i) admitted += in.Offer(Au{1, 99, false}).admit == ChainAdmit::Admitted ? 1 : 0;
    check("N3: seq 100 then 40 x seq 99: none admitted (no automatic resync)", admitted == 0,
          "admitted=" + std::to_string(admitted));
  }
  std::printf("\ndecoder_chain_inbox_r2_contrast_test: %d checks, %d failed\nRESULT: %s\n", gChecks, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}

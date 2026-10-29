// apk-inbox C r3 (Codex codex_apk_inbox_c_src_r2.md N2): DecoderKeyRequest::Merge when the order asks are PUBLISHED in
// is not the order the sink issued them. Only the API every version has (the fields, Merge), so the same file builds
// against d50415e's decoder_key_request.hpp (REMOTE60_CHAIN_INBOX_DIR) -- where the late older ask overwrote the newer
// epoch / episode -- and must FAIL there. Tag: pure-logic.
//
//   remote60_decoder_key_request_merge_test

#include <cstdio>
#include <string>

// <> (not ""): from the include path -- REMOTE60_CHAIN_INBOX_DIR first (another source in the negative runs).
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
DecoderKeyRequest req(uint32_t causes, uint64_t epoch, uint64_t episode = 0, uint64_t token = 0) {
  DecoderKeyRequest r;
  r.causes = causes;
  r.sinkEpoch = epoch;
  r.episodeId = episode;
  r.owedToken = token;
  return r;
}
std::string show(const DecoderKeyRequest& r) {
  return "causes=" + std::to_string(r.causes) + " epoch=" + std::to_string(r.sinkEpoch) + " episode=" +
         std::to_string(r.episodeId) + " token=" + std::to_string(r.owedToken);
}
// the sink's own judgement, reduced: a sink-scoped cause holds only at the CURRENT epoch (recovery: and episode), owed
// only with the current token; pipeline / legacy always
uint32_t valid(const DecoderKeyRequest& r, uint64_t epoch, uint64_t episode, uint64_t token) {
  uint32_t v = r.causes & (kKeyCausePipeline | kKeyCauseLegacy);
  if (r.sinkEpoch == epoch) {
    v |= r.causes & (kKeyCauseCodecUp | kKeyCauseSelectionAbort);
    if (r.episodeId == episode) v |= r.causes & kKeyCauseRecovery;
  }
  if (r.owedToken != 0 && r.owedToken == token) v |= r.causes & kKeyCauseOwed;
  return v;
}
}  // namespace

int main() {
  std::printf("decoder_key_request_merge_test (C r3: a late, older ask never overwrites a newer one)\n");
  {
    DecoderKeyRequest m = req(kKeyCauseRecovery, 3, 5);
    m.Merge(req(kKeyCauseRecovery, 2, 4));  // Codex (2): epoch 3 published first, the epoch-2 ask after it
    check("a newer epoch, then a late OLDER epoch: the newer epoch / episode stay, still valid",
          m.sinkEpoch == 3 && m.episodeId == 5 && valid(m, 3, 5, 0) == kKeyCauseRecovery, show(m));
  }
  {
    DecoderKeyRequest m = req(kKeyCauseRecovery, 2, 3);
    m.Merge(req(kKeyCauseRecovery, 2, 2));  // Codex (1): epoch 2 episode 3, then a late episode-2 ask
    check("the same epoch, a newer episode, then a late OLDER episode: the newer episode stays, still valid",
          m.episodeId == 3 && valid(m, 2, 3, 0) == kKeyCauseRecovery, show(m));
  }
  {
    DecoderKeyRequest m = req(kKeyCauseRecovery, 2, 4);
    m.Merge(req(kKeyCauseRecovery, 3, 5));  // the issue order
    DecoderKeyRequest n = req(kKeyCauseRecovery, 2, 2);
    n.Merge(req(kKeyCauseRecovery, 2, 3));
    check("published in the issue order: the newer one wins (epoch 3 / ep 5; episode 3)",
          m.sinkEpoch == 3 && m.episodeId == 5 && n.episodeId == 3 && valid(m, 3, 5, 0) && valid(n, 2, 3, 0), show(m));
  }
  {
    DecoderKeyRequest m = req(kKeyCauseRecovery | kKeyCauseOwed, 2, 3, 7);
    m.Merge(req(kKeyCauseRecovery | kKeyCauseOwed, 2, 3, 7));
    check("a duplicate changes nothing", m.causes == (kKeyCauseRecovery | kKeyCauseOwed) && m.sinkEpoch == 2 &&
          m.episodeId == 3 && m.owedToken == 7, show(m));
  }
  {
    DecoderKeyRequest m = req(kKeyCauseRecovery, 2, 3);
    m.Merge(req(kKeyCauseCodecUp | kKeyCauseRecovery | kKeyCausePipeline, 2, 2));
    check("mixed, the same epoch: the late ask's older recovery goes, its codec-up (same epoch) and pipeline stay",
          m.episodeId == 3 && valid(m, 2, 3, 0) == (kKeyCauseRecovery | kKeyCauseCodecUp | kKeyCausePipeline), show(m));
  }
  {
    DecoderKeyRequest m = req(kKeyCauseCodecUp, 3);
    m.Merge(req(kKeyCauseRecovery | kKeyCausePipeline, 2, 9));
    check("NOT promoted: a late epoch-2 recovery is not carried onto epoch 3; its pipeline cause stays",
          m.causes == (kKeyCauseCodecUp | kKeyCausePipeline) && m.sinkEpoch == 3 && m.episodeId == 0, show(m));
  }
  {
    DecoderKeyRequest m = req(kKeyCausePipeline, 0);
    m.Merge(req(kKeyCauseRecovery, 2, 4));
    check("a session-only ask, then a sink ask: the sink ask's own identity is taken",
          m.causes == (kKeyCausePipeline | kKeyCauseRecovery) && m.sinkEpoch == 2 && m.episodeId == 4, show(m));
  }
  {
    DecoderKeyRequest m = req(kKeyCauseOwed, 0, 0, 7);
    m.Merge(req(kKeyCauseOwed, 0, 0, 6));
    DecoderKeyRequest n = req(kKeyCauseOwed, 0, 0, 6);
    n.Merge(req(kKeyCauseOwed, 0, 0, 7));
    check("owed: a late older token never replaces the newer duty's (either order -> token 7, valid)",
          m.owedToken == 7 && n.owedToken == 7 && valid(m, 0, 0, 7) == kKeyCauseOwed && valid(n, 0, 0, 7) == kKeyCauseOwed,
          show(m) + " | " + show(n));
  }
  std::printf("\ndecoder_key_request_merge_test: %d checks, %d failed\nRESULT: %s\n", gChecks, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}

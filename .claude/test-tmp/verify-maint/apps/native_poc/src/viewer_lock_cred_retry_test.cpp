// lk-ux-retry r3 (Codex N2-b): what the viewer believes from the host's answer to a retry (89). The product's own
// exchange (viewer_unlock.cpp run_lock_cred_exchange: the real sealing) against a scripted host on a fake control
// link -- the host's answer is chosen per case, so a WRONG answer (another purpose, another request id) can be
// given exactly. What it pins: "the earlier wait is over" (progress.challengeIssued / supersededAnswer) is set only
// from an answer that passed every check; a wrong-purpose answer leaves it unset and seals nothing; the seal's
// leaving is told apart from its answer (progress.sealSent).
// Tags: pure-logic (no host, no window, no network).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "poc_protocol.hpp"
#include "sealed_unlock.hpp"
#include "udp_control_channel.hpp"
#include "viewer_unlock.hpp"

using namespace remote60::native_poc;
namespace su = remote60::native_poc::sealed_unlock;
using remote60::native_poc::viewer::LockCredProgress;
using remote60::native_poc::viewer::run_lock_cred_exchange;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}

// The scripted host: every message the viewer ends is handed to `answer`, which may queue the reply bytes.
class FakeLink : public ControlLink {
 public:
  std::function<void(const std::vector<uint8_t>& msg, FakeLink& link)> answer;
  std::vector<uint16_t> sentTypes;
  void Reply(const void* d, size_t n) {
    inbox_.emplace_back(static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n);
  }
  bool Read(void* out, size_t len) override {
    if (inbox_.empty()) return false;
    auto& m = inbox_.front();
    if (rpos_ + len > m.size()) return false;
    std::memcpy(out, m.data() + rpos_, len);
    rpos_ += len;
    if (rpos_ == m.size()) {
      inbox_.pop_front();
      rpos_ = 0;
    }
    return true;
  }
  bool Write(const void* data, size_t len) override {
    out_.insert(out_.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + len);
    return true;
  }
  bool EndMessage() override {
    MessageHeader h{};
    if (out_.size() >= sizeof(h)) std::memcpy(&h, out_.data(), sizeof(h));
    sentTypes.push_back(h.type);
    std::vector<uint8_t> msg;
    msg.swap(out_);
    if (answer) answer(msg, *this);
    return true;
  }
  bool Alive() const override { return true; }
  bool Sent(MessageType t) const {
    for (uint16_t s : sentTypes)
      if (s == static_cast<uint16_t>(t)) return true;
    return false;
  }

 private:
  std::vector<uint8_t> out_;
  std::deque<std::vector<uint8_t>> inbox_;
  size_t rpos_ = 0;
};

su::EcdhKeyPair gHostKey;

// The host's 41 for the retry (89) in `msg`: `status`, `purpose`, the request id echoed (+ `idSkew`).
void answer_41(const std::vector<uint8_t>& msg, FakeLink& link, uint16_t status, uint16_t purpose, uint32_t idSkew = 0) {
  ControlLockCredRetryChallengeRequestMessage req{};
  std::memcpy(&req, msg.data(), (std::min)(msg.size(), sizeof(req)));
  ControlUnlockChallengeMessage ch{};
  ch.header.magic = kMagic;
  ch.header.type = static_cast<uint16_t>(MessageType::ControlUnlockChallenge);
  ch.header.size = static_cast<uint16_t>(sizeof(ch));
  ch.requestId = req.requestId + idSkew;
  ch.status = status;
  ch.reserved = purpose;
  if (status == static_cast<uint16_t>(UnlockStage::ChallengeIssued)) {
    gHostKey.ExportPublic(ch.hostPub);
    su::RandomBytes(ch.salt, sizeof(ch.salt));
    su::RandomBytes(ch.challengeId, sizeof(ch.challengeId));
    ch.hostId = 0x4057;
    ch.accountId = 0xA1;
    ch.requesterSession = 1;
    ch.consoleSession = 1;
    ch.issuedMs = 1000;
    ch.expiresMs = 31000;
  }
  link.Reply(&ch, sizeof(ch));
}

struct Outcome {
  bool done = false;
  uint16_t stage = 0;
  bool waiting = false;
  LockCredProgress progress;
  bool sealed = false;
};

Outcome run_retry(const std::function<void(const std::vector<uint8_t>&, FakeLink&)>& host) {
  FakeLink link;
  link.answer = host;
  Outcome o;
  uint32_t win32 = 0, rid = 0;
  std::string status;
  o.done = run_lock_cred_exchange(link, L"retry-pw", &o.stage, &win32, &o.waiting, &rid, &status, /*supersedes=*/5,
                                  &o.progress);
  o.sealed = link.Sent(MessageType::ControlUnlockSealedRequest);
  return o;
}
std::string describe(const Outcome& o) {
  return "done=" + std::to_string(o.done) + " stage=" + std::to_string(o.stage) + " issued=" +
         std::to_string(o.progress.challengeIssued) + " superseded=" + std::to_string(o.progress.supersededAnswer) +
         " sealSent=" + std::to_string(o.progress.sealSent) + " sealed=" + std::to_string(o.sealed);
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("what the viewer believes from a retry's answer (lk-ux-retry r3, Codex N2-b)\n\n");
  gHostKey.Generate();
  const uint16_t lockCred = kUnlockPurposeLockCred, unlock = kUnlockPurposeUnlock;
  const uint16_t issued = static_cast<uint16_t>(UnlockStage::ChallengeIssued);
  const uint16_t superseded = static_cast<uint16_t>(UnlockStage::Superseded);

  {
    const Outcome o = run_retry([&](const std::vector<uint8_t>& m, FakeLink& l) {
      MessageHeader h{};
      std::memcpy(&h, m.data(), sizeof(h));
      if (h.type == static_cast<uint16_t>(MessageType::ControlLockCredRetryChallengeRequest)) answer_41(m, l, issued, unlock);
    });
    check("an ISSUED challenge of another purpose (same request id): the earlier wait is NOT believed over, nothing sealed",
          !o.done && !o.progress.challengeIssued && !o.progress.supersededAnswer && !o.sealed, describe(o));
  }
  {
    const Outcome o = run_retry([&](const std::vector<uint8_t>& m, FakeLink& l) {
      MessageHeader h{};
      std::memcpy(&h, m.data(), sizeof(h));
      if (h.type == static_cast<uint16_t>(MessageType::ControlLockCredRetryChallengeRequest)) answer_41(m, l, superseded, unlock);
    });
    check("a Superseded of another purpose: not believed (the earlier wait is kept), nothing sealed",
          !o.done && !o.progress.supersededAnswer && !o.progress.challengeIssued && !o.sealed, describe(o));
  }
  {
    const Outcome o = run_retry([&](const std::vector<uint8_t>& m, FakeLink& l) {
      MessageHeader h{};
      std::memcpy(&h, m.data(), sizeof(h));
      if (h.type == static_cast<uint16_t>(MessageType::ControlLockCredRetryChallengeRequest)) answer_41(m, l, issued, lockCred, 1);
    });
    check("an issued challenge for ANOTHER request id: not believed, nothing sealed",
          !o.done && !o.progress.challengeIssued && !o.sealed, describe(o));
  }
  {
    const Outcome o = run_retry([&](const std::vector<uint8_t>& m, FakeLink& l) {
      MessageHeader h{};
      std::memcpy(&h, m.data(), sizeof(h));
      if (h.type == static_cast<uint16_t>(MessageType::ControlLockCredRetryChallengeRequest)) answer_41(m, l, superseded, lockCred);
    });
    check("control: a Superseded of the lock-screen purpose IS believed (the earlier wait over, no new challenge)",
          o.done && o.stage == superseded && o.progress.supersededAnswer && !o.progress.challengeIssued && !o.sealed,
          describe(o));
  }
  {
    // The right challenge: sealed and sent -- and then the host says nothing more (its answer never comes).
    const Outcome o = run_retry([&](const std::vector<uint8_t>& m, FakeLink& l) {
      MessageHeader h{};
      std::memcpy(&h, m.data(), sizeof(h));
      if (h.type == static_cast<uint16_t>(MessageType::ControlLockCredRetryChallengeRequest)) answer_41(m, l, issued, lockCred);
    });
    check("control: the right challenge is believed and the new password sealed and SENT (its answer not come: told apart)",
          !o.done && o.progress.challengeIssued && o.progress.sealSent && o.sealed, describe(o));
  }

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

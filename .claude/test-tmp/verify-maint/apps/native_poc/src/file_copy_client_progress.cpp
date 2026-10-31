// FileCopyClient, continued: the user's Cancel, the recorded result the transfer bar reads, and the
// pre-emption of a running image (D5 / D6). Moved here verbatim from file_copy_client.cpp to keep that
// file under 1,000 lines (t-zdmsd4gb r3); the class is one, see file_copy_client.hpp.

#include "file_copy_client.hpp"

#include <string>

namespace remote60::native_poc {

namespace fc = remote60::native_poc::file_copy;
namespace fn = remote60::native_poc::file_copy::net;

void FileCopyClient::CancelPaste() {
  std::lock_guard<std::mutex> lock(mu_);
  if (cancelPending_) return;
  if (paste_.active) {
    // P->R: the host ends it (its reads fail, the consumer stops); here it ends when the host says so.
    cancelKey_ = PasteKey{paste_.offerId, paste_.pasteOp, fn::PasteEndReason::Cancelled};
    cancelPending_ = true;
    endQueue_.push_back(cancelKey_);
    Log("paste cancelled by the user (P->R): asking the host");
  } else if (recv_.active) {
    // R->P: nothing more is read here (the consumer's reads fail); the host is told and confirms.
    cancelKey_ = PasteKey{recv_.offerId, recv_.pasteOp, fn::PasteEndReason::Cancelled};
    cancelPending_ = true;
    EndReceiveLocked(fn::PasteEndReason::Cancelled);  // queues the End
    Log("paste cancelled by the user (R->P)");
  }
}

void FileCopyClient::RecordResultLocked(bool toRemote, fn::PasteState state, fn::PasteEndReason reason, uint16_t refused,
                                        uint32_t files, uint64_t bytes, uint64_t startUs) {
  ++result_.finished;
  result_.lastToRemote = toRemote;
  result_.lastState = static_cast<uint8_t>(state);
  result_.lastReason = static_cast<uint8_t>(reason);
  result_.lastRefused = refused;
  result_.lastFiles = files;
  result_.lastBytes = bytes;
  const uint64_t now = BulkPacer::NowUs();
  result_.lastElapsedMs = startUs && now > startUs ? (now - startUs) / 1000 : 0;
  ReleasePreemptLocked();
}

void FileCopyClient::WaitForBulkLocked(bool toRemote, const PasteKey& k) {
  waitBulk_.on = true;
  waitBulk_.toRemote = toRemote;
  waitBulk_.key = k;
  waitBulk_.deadlineUs = BulkPacer::NowUs() + kFileBulkSwitchBudgetUs;
  if (arbiter_ && arbiter_->use() == BulkUse::Image && preempt_ && !preemptActive_) {
    preemptActive_ = true;
    revisionAtPreempt_ = remote_.revision;
    const bool stopping = preempt_();
    Log(std::string("paste waits for the bulk: an image is being stopped for it (") + (stopping ? "running" : "settling") + ")");
  } else {
    Log("paste waits for the bulk");
  }
}

void FileCopyClient::ReleasePreemptLocked() {
  if (!preemptActive_) return;
  preemptActive_ = false;
  // The stopped image may go again only if nothing it depends on moved meanwhile (D5).
  const bool mayResume = allowed_.load() && Usable() && remote_.revision == revisionAtPreempt_;
  if (after_) after_(mayResume);
}

FileCopyClient::Progress FileCopyClient::GetProgress() const {
  std::lock_guard<std::mutex> lock(mu_);
  Progress p = result_;
  const uint64_t now = BulkPacer::NowUs();
  p.cancelling = cancelPending_;
  if (paste_.active) {
    p.sending = true;
    p.files = paste_.files;
    p.bytesTotal = paste_.bytesTotal;
    p.bytesDone = server_.GetCounters().bytesServed - paste_.servedAtStart;
    p.elapsedMs = now > paste_.startUs ? (now - paste_.startUs) / 1000 : 0;
  } else if (recv_.active) {
    p.receiving = true;
    p.files = recv_.files;
    p.bytesTotal = recv_.bytesTotal;
    p.bytesDone = receiver_.bytesDelivered();
    p.elapsedMs = now > recv_.startUs ? (now - recv_.startUs) / 1000 : 0;
  }
  return p;
}

}  // namespace remote60::native_poc

#pragma once

// One bulk transfer per session: who holds the session's bulk -- an image, a file paste, or nobody --
// and the stop in between. (t-zdmsd4gb r1, debate "공통 상태·신뢰 경계")
//
// Role:    Idle / Image / File / Stopping. A transfer takes the bulk only from Idle (TryAcquire);
//          anyone else is Busy -- never swapped in silently. The holder gives it back when its
//          transfer is over AND its channel is released (Release). A holder being stopped for
//          someone else goes Stopping first (BeginStop): the bulk is then nobody's to take until the
//          stopped transfer's terminal state is confirmed and it Releases (D5: "양단 terminal/채널
//          해제 확인 후 전환" -- a cancel merely sent is not a released channel).
// Streams: image and file transfers never share stream ids (file pastes use the two bulk stream
//          bases the image path does not, bulk_stream_is_file), so this is about the link, not about
//          telling datagrams apart.
// Thread:  any; one mutex.
// Callers: the viewer's image and file clients (one arbiter per viewer session), the host's image
//          and file services (one per client session).

#include <cstdint>
#include <mutex>

namespace remote60::native_poc {

enum class BulkUse : uint8_t { Idle = 0, Image = 1, File = 2, Stopping = 3 };

inline const char* bulk_use_name(BulkUse u) {
  switch (u) {
    case BulkUse::Idle: return "idle";
    case BulkUse::Image: return "image";
    case BulkUse::File: return "file";
    case BulkUse::Stopping: return "stopping";
  }
  return "?";
}

class BulkArbiter {
 public:
  /**
   * Takes the bulk for `use` (Image / File) as `owner` (a transfer id / paste op, non-zero), from Idle
   * only. True means THIS call took it -- so a caller that then refuses may Release it. A holder
   * asking again gets false like anyone else: a repeated offer of the transfer already running must
   * not be able to give the running transfer's bulk away (r2 ③).
   */
  bool TryAcquire(BulkUse use, uint64_t owner) {
    if ((use != BulkUse::Image && use != BulkUse::File) || owner == 0) return false;
    std::lock_guard<std::mutex> lock(mu_);
    if (use_ != BulkUse::Idle) return false;
    use_ = use;
    owner_ = owner;
    stoppedFrom_ = BulkUse::Idle;
    return true;
  }

  /** The holder's transfer is over and its channel released. Only the holder may. */
  bool Release(uint64_t owner) {
    std::lock_guard<std::mutex> lock(mu_);
    if (use_ == BulkUse::Idle || owner_ != owner || owner == 0) return false;
    use_ = BulkUse::Idle;
    owner_ = 0;
    stoppedFrom_ = BulkUse::Idle;
    return true;
  }

  /** The holder is being stopped for someone else: nobody may take the bulk until it Releases. */
  bool BeginStop(uint64_t owner) {
    std::lock_guard<std::mutex> lock(mu_);
    if ((use_ != BulkUse::Image && use_ != BulkUse::File) || owner_ != owner || owner == 0) return false;
    stoppedFrom_ = use_;
    use_ = BulkUse::Stopping;
    return true;
  }

  BulkUse use() const {
    std::lock_guard<std::mutex> lock(mu_);
    return use_;
  }
  uint64_t owner() const {
    std::lock_guard<std::mutex> lock(mu_);
    return owner_;
  }
  /** While Stopping: what the holder was (Image / File). */
  BulkUse stopped_from() const {
    std::lock_guard<std::mutex> lock(mu_);
    return stoppedFrom_;
  }
  bool held_by(uint64_t owner) const {
    std::lock_guard<std::mutex> lock(mu_);
    return owner != 0 && owner_ == owner && use_ != BulkUse::Idle;
  }

  /** The session ended: whatever held the bulk is gone with it. */
  void Reset() {
    std::lock_guard<std::mutex> lock(mu_);
    use_ = BulkUse::Idle;
    owner_ = 0;
    stoppedFrom_ = BulkUse::Idle;
  }

 private:
  mutable std::mutex mu_;
  BulkUse use_ = BulkUse::Idle;
  uint64_t owner_ = 0;
  BulkUse stoppedFrom_ = BulkUse::Idle;
};

}  // namespace remote60::native_poc

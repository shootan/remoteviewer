// See file_copy_paste_parts.hpp.

#include "file_copy_paste_parts.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace remote60::native_poc {

namespace fc = remote60::native_poc::file_copy;
namespace fn = remote60::native_poc::file_copy::net;

// ------------------------------------------------------------------------------ FileHelperChannel

void FileHelperChannel::Configure(Config config, FrameOfFn onFrame, GoneOfFn onGone) {
  std::lock_guard<std::mutex> lock(mu_);
  config_ = std::move(config);
  onFrameOf_ = std::move(onFrame);
  onGoneOf_ = std::move(onGone);
}

void FileHelperChannel::SetOwner(uint64_t owner) {
  std::shared_ptr<fc::HelperLink> dying;
  {
    std::lock_guard<std::mutex> s(sendMu_);
    if (owner_.load() == owner) return;
    owner_.store(owner);
    if (cur_.link) {
      dying = std::move(prev_.link);  // a helper two sessions back: gone now
      prev_ = std::move(cur_);
      cur_ = Helper{};
      instance_.store(0);
    }
  }
  if (dying) dying->Close();
}

bool FileHelperChannel::EnsureAs(uint64_t owner, std::string* why, bool* stale, uint64_t* instance) {
  std::lock_guard<std::mutex> lock(mu_);
  return EnsureLocked(owner, why, stale, instance);
}

bool FileHelperChannel::Ensure(std::string* why, uint64_t* instance) {
  std::lock_guard<std::mutex> lock(mu_);
  return EnsureLocked(owner_.load(), why, nullptr, instance);
}

bool FileHelperChannel::EnsureLocked(uint64_t owner, std::string* why, bool* stale, uint64_t* instance) {
  if (stale) *stale = false;
  if (instance) *instance = 0;
  {
    std::lock_guard<std::mutex> s(sendMu_);
    if (owner_.load() != owner) {
      *why = "the session ended";
      if (stale) *stale = true;
      return false;
    }
    if (cur_.link && cur_.link->pipe_open() && cur_.link->helper_alive()) {
      if (instance) *instance = cur_.instance;
      return true;
    }
  }
  const uint64_t now = GetTickCount64();
  if (now < nextLaunchMs_) {
    *why = "helper start backing off";
    return false;
  }
  ++launches_;
  // Started on a link of its own, under the channel's mu_ only (no host lock, no sendMu_; a start
  // takes seconds); only once it is up is it checked, under sendMu_, that its owner still owns the
  // channel -- and only then does it become the channel's helper, with a fresh instance number. A
  // previous helper (of this or an earlier owner) is closed for good here, as before: its Job goes
  // and so does its process. Its reader is not joined here: it may be inside a callback; it drains
  // on its own, and its frames and "gone" carry the old instance number, which nobody takes.
  auto fresh = std::make_shared<fc::HelperLink>();
  if (!config_.launcher || !config_.launcher(fresh.get(), why)) {
    fresh->Close();
    lastLaunchFailure_.store(static_cast<uint8_t>(fc::launch_failure_of(*why)));
    backoffMs_ = backoffMs_ ? (std::min)(backoffMs_ * 2, config_.backoffMaxMs) : config_.backoffFirstMs;
    nextLaunchMs_ = now + backoffMs_;
    ++launchFailures_;
    return false;
  }
  std::shared_ptr<fc::HelperLink> dying, dyingCur;
  std::thread oldReader;
  bool adopted = false;
  lastLaunchFailure_.store(static_cast<uint8_t>(fc::LaunchFailure::None));
  const uint64_t inst = ++nextInstance_;
  {
    std::lock_guard<std::mutex> s(sendMu_);
    if (owner_.load() == owner) {
      dying = std::move(prev_.link);
      // The replaced helper of this same owner (it died, or its pipe failed) goes now too, as it did
      // before; one of another owner (after SetOwner) is prev_ and Retire's to end.
      if (cur_.link && cur_.owner == owner) dyingCur = cur_.link;
      prev_ = std::move(cur_);
      cur_ = Helper{fresh, owner, inst};
      instance_.store(inst);
      if (instance) *instance = inst;
      adopted = true;
    }
  }
  if (!adopted) {
    // The owner moved on while the helper was starting: this helper was started for a session that
    // has ended. It never published anything; it goes at once.
    fresh->Close();
    *why = "the session ended";
    if (stale) *stale = true;
    return false;
  }
  if (dying) dying->Close();  // its reader (if still there) sees the pipe go and ends
  if (dyingCur) dyingCur->Close();
  oldReader = std::move(prevReader_);
  if (oldReader.joinable()) oldReader.join();  // under mu_ only: never under a caller's lock
  prevReader_ = std::move(reader_);
  reader_ = std::thread([this, owner, inst, fresh] { ReaderLoop(owner, inst, fresh); });
  backoffMs_ = 0;
  nextLaunchMs_ = 0;
  return true;
}

bool FileHelperChannel::SendAs(uint64_t owner, const fc::PipeFrame& f, bool* stale, uint64_t* instance) {
  std::lock_guard<std::mutex> s(sendMu_);
  if (instance) *instance = 0;
  if (owner_.load() != owner) {
    if (stale) *stale = true;
    return false;
  }
  if (stale) *stale = false;
  if (!(cur_.link && cur_.link->pipe_open() && cur_.link->Send(f))) return false;
  if (instance) *instance = cur_.instance;  // whom it went to: the answer to take is this one's
  return true;
}

bool FileHelperChannel::SendTo(uint64_t instance, const fc::PipeFrame& f) {
  std::lock_guard<std::mutex> s(sendMu_);
  if (!cur_.link || cur_.instance != instance) {
    ++droppedSends_;  // that helper is gone or replaced: what was meant for it goes to nobody
    return false;
  }
  if (cur_.link->pipe_open() && cur_.link->Send(f)) return true;
  ++failedSends_;  // still the current helper, but its pipe is closed: it is gone
  return false;
}

DWORD FileHelperChannel::CurrentPid() const {
  std::lock_guard<std::mutex> s(sendMu_);
  return cur_.link ? cur_.link->helper_pid() : 0;
}

bool FileHelperChannel::Running() const {
  std::lock_guard<std::mutex> s(sendMu_);
  return cur_.link && cur_.link->pipe_open() && cur_.link->helper_alive();
}

void FileHelperChannel::ReaderLoop(uint64_t owner, uint64_t instance, std::shared_ptr<fc::HelperLink> link) {
  // Every frame and the "gone" carry this helper's tag; the receiver decides under its own lock
  // whether this helper is still its current one (r8: no untagged, unbound check here).
  for (;;) {
    fc::PipeFrame f;
    if (!link->Receive(&f, 100)) {
      if (GetLastError() == WAIT_TIMEOUT) continue;
      break;  // the pipe is gone
    }
    if (onFrameOf_) onFrameOf_(owner, instance, f);
  }
  if (onGoneOf_) onGoneOf_(owner, instance);
}

void FileHelperChannel::Retire(uint64_t owner, bool sendShutdown) {
  std::lock_guard<std::mutex> s(sendMu_);
  for (Helper* h : {&cur_, &prev_}) {
    if (!h->link || h->owner != owner) continue;
    if (sendShutdown && h->link->pipe_open()) (void)h->link->Send(fc::encode_shutdown());
    h->link->ClosePipe();
  }
}

void FileHelperChannel::Disconnect() {
  std::lock_guard<std::mutex> s(sendMu_);
  if (cur_.link) cur_.link->ClosePipe();
}

void FileHelperChannel::Stop() {
  std::thread a, b;
  {
    std::lock_guard<std::mutex> lock(mu_);
    {
      std::lock_guard<std::mutex> s(sendMu_);
      if (cur_.link) cur_.link->Close();
      if (prev_.link) prev_.link->Close();
    }
    a = std::move(reader_);
    b = std::move(prevReader_);
  }
  if (a.joinable()) a.join();
  if (b.joinable()) b.join();
}

// ------------------------------------------------------------------------------ FilePullReceiver

void FilePullReceiver::Start(AnswerFn answer) {
  if (running_.exchange(true)) return;
  answer_ = std::move(answer);
  thread_ = std::thread([this] { Loop(); });
}

void FilePullReceiver::Stop() {
  if (!running_.exchange(false)) return;
  Close(fc::Status::Aborted);
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void FilePullReceiver::Open(SendFn send, uint32_t txStreamId, uint32_t rxStreamId, uint32_t mtuBytes,
                            const FilePasteIdentity& id, const std::vector<uint64_t>& sizes, uint64_t helperInstance) {
  std::lock_guard<std::mutex> lock(mu_);
  id_ = id;
  helperInstance_ = helperInstance;
  sizes_ = sizes;
  coverage_.clear();
  for (uint64_t s : sizes) coverage_.emplace_back(s);
  jobs_.clear();
  outstanding_.clear();
  completed_.clear();
  waiting_.clear();
  aheadOn_ = false;
  ahead_.clear();
  nextRequestId_ = 1;
  bytesDelivered_ = 0;
  lastProgressUs_ = BulkPacer::NowUs();
  failure_ = fn::PasteEndReason::None;
  bulk_.Reset();
  bulk_.Configure(std::move(send), txStreamId, rxStreamId, mtuBytes);
  open_ = true;
  cv_.notify_all();
}

void FilePullReceiver::Close(fc::Status why) {
  std::vector<fc::ReadData> answers;
  uint64_t inst = 0;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!open_) return;
    inst = helperInstance_;
    FailAllLocked(why, &answers);
    bulk_.Close(ControlCloseReason::SessionRollover);
    open_ = false;
  }
  if (answer_) {
    for (const fc::ReadData& d : answers) answer_(inst, d);
  }
}

bool FilePullReceiver::IsOpen() const {
  std::lock_guard<std::mutex> lock(mu_);
  return open_;
}

fn::PasteEndReason FilePullReceiver::failure() const {
  std::lock_guard<std::mutex> lock(mu_);
  return failure_;
}

uint64_t FilePullReceiver::bytesDelivered() const {
  std::lock_guard<std::mutex> lock(mu_);
  return bytesDelivered_;
}

std::vector<bool> FilePullReceiver::wholeFileVerified() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<bool> v;
  for (const auto& c : coverage_) v.push_back(c.whole_file_verified());
  return v;
}

FilePullReceiver::Counters FilePullReceiver::GetCounters() const {
  std::lock_guard<std::mutex> lock(mu_);
  return counters_;
}

void FilePullReceiver::Submit(const fc::ReadRequest& m, uint64_t instance) {
  std::vector<fc::ReadData> answers;
  uint64_t inst = instance;  // a refusal answers the asker; a served read answers the paste's helper
  {
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.readsRequested;
    fc::ReadData refuse;
    refuse.offerId = m.offerId;
    refuse.pasteOp = m.pasteOp;
    refuse.fileIndex = m.fileIndex;
    refuse.offset = m.offset;
    const bool mine = open_ && id_.offerId == m.offerId && id_.pasteOp == m.pasteOp && m.fileIndex < sizes_.size();
    if (!mine) {
      refuse.status = fc::Status::UnknownId;
    } else if (!fn::range_ok(sizes_[m.fileIndex], m.offset, m.length, fc::kMaxChunkBytes)) {
      refuse.status = fc::Status::BadRequest;
    } else if (m.length == 0) {
      refuse.status = fc::Status::Ok;  // nothing to fetch (end of file)
    } else if (failure_ == fn::PasteEndReason::Verification || failure_ == fn::PasteEndReason::Idle) {
      refuse.status = fc::Status::ReadError;  // a chunk of this paste failed its check: no Read succeeds after it
    } else {
      if (waiting_.empty()) lastProgressUs_ = BulkPacer::NowUs();  // the clock runs while someone waits
      waiting_.push_back(m);
      ProcessLocked(&answers);
      refuse.status = fc::Status::Ok;
      refuse.pasteOp = 0;  // no refusal
      inst = helperInstance_;
    }
    if (refuse.pasteOp != 0) {
      if (refuse.status != fc::Status::Ok) ++counters_.readsFailed;
      answers.push_back(std::move(refuse));
    }
  }
  if (answer_) {
    for (const fc::ReadData& d : answers) answer_(inst, d);
  }
}

void FilePullReceiver::ResetAheadLocked(uint32_t fileIndex, uint64_t offset) {
  // Pulls already in flight for the old position still arrive; they are verified and dropped.
  aheadOn_ = true;
  aheadFile_ = fileIndex;
  aheadStart_ = offset;
  ahead_.clear();
  aheadIssuedEnd_ = offset;
}

void FilePullReceiver::ProcessLocked(std::vector<fc::ReadData>* answers) {
  while (!waiting_.empty()) {
    const fc::ReadRequest& w = waiting_.front();
    if (!aheadOn_ || w.fileIndex != aheadFile_ || w.offset != aheadStart_) ResetAheadLocked(w.fileIndex, w.offset);
    if (ahead_.size() < w.length) break;
    fc::ReadData d;
    d.offerId = w.offerId;
    d.pasteOp = w.pasteOp;
    d.fileIndex = w.fileIndex;
    d.offset = w.offset;
    d.status = fc::Status::Ok;
    d.data.assign(ahead_.begin(), ahead_.begin() + w.length);
    ahead_.erase(ahead_.begin(), ahead_.begin() + w.length);
    aheadStart_ += w.length;
    bytesDelivered_ += w.length;
    counters_.bytesDelivered += w.length;
    ++counters_.readsServed;
    answers->push_back(std::move(d));
    waiting_.pop_front();
  }
  if (!aheadOn_ || aheadFile_ >= sizes_.size()) return;
  // The window: what the next waiting Read needs, plus kAheadBytes past the consumer's position.
  const uint64_t need = waiting_.empty() ? 0 : waiting_.front().length;
  const uint64_t target = (std::min)(sizes_[aheadFile_], aheadStart_ + need + kAheadBytes);
  while (aheadIssuedEnd_ < target) {
    const uint64_t len = (std::min<uint64_t>)(fc::kMaxChunkBytes, target - aheadIssuedEnd_);
    auto job = std::make_unique<Job>();
    job->fileIndex = aheadFile_;
    job->offset = aheadIssuedEnd_;
    job->data.resize(static_cast<size_t>(len));
    for (uint64_t off = 0; off < len; off += fn::kMaxFileChunkBytes) {
      fn::Pull p;
      p.epochTag = id_.epochTag;
      p.offerId = id_.offerId;
      p.pasteOp = id_.pasteOp;
      p.bulkGen = id_.bulkGen;
      p.fileIndex = aheadFile_;
      p.offset = aheadIssuedEnd_ + off;
      p.length = static_cast<uint32_t>((std::min<uint64_t>)(fn::kMaxFileChunkBytes, len - off));
      job->pulls.push_back(p);
    }
    jobs_.push_back(std::move(job));
    aheadIssuedEnd_ += len;
  }
  PumpPullsLocked();
  cv_.notify_all();
}

void FilePullReceiver::PumpPullsLocked() {
  if (!open_) return;
  for (auto& jp : jobs_) {
    Job& j = *jp;
    while (!j.failed && j.nextPull < j.pulls.size() && outstanding_.size() < pullWindow_) {
      fn::Pull& p = j.pulls[j.nextPull];
      p.requestId = nextRequestId_++;
      p.triggerRequestId = fn::kNoTrigger;
      if (!completed_.empty()) {  // one pull carries each completion, oldest first
        p.triggerRequestId = completed_.front();
        completed_.pop_front();
      }
      outstanding_[p.requestId] = {&j, j.nextPull};
      ++j.nextPull;
      const std::vector<uint8_t> w = fn::frame_bulk(p);
      (void)bulk_.Send(w.data(), w.size());
    }
    if (outstanding_.size() >= pullWindow_) break;
  }
}

void FilePullReceiver::FailAllLocked(fc::Status why, std::vector<fc::ReadData>* answers) {
  for (const fc::ReadRequest& w : waiting_) {
    fc::ReadData d;
    d.offerId = w.offerId;
    d.pasteOp = w.pasteOp;
    d.fileIndex = w.fileIndex;
    d.offset = w.offset;
    d.status = why;
    ++counters_.readsFailed;
    answers->push_back(std::move(d));
  }
  waiting_.clear();
  jobs_.clear();
  outstanding_.clear();
  aheadOn_ = false;
  ahead_.clear();
}

void FilePullReceiver::Loop() {
  std::vector<uint8_t> msg;
  while (running_.load()) {
    {
      std::unique_lock<std::mutex> lock(mu_);
      if (!open_) {
        cv_.wait_for(lock, std::chrono::milliseconds(200));
        continue;
      }
    }
    const bool got = bulk_.Receive(&msg, 20);
    bulk_.Tick();
    std::vector<fc::ReadData> answers;
    uint64_t inst = 0;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (!open_) continue;
      inst = helperInstance_;
      fn::Chunk c;
      bool failed = false;
      if (got && fn::parse_bulk(msg.data(), msg.size(), &c)) {
        auto o = outstanding_.find(c.requestId);
        const bool thisPaste = c.epochTag == id_.epochTag && c.offerId == id_.offerId && c.pasteOp == id_.pasteOp &&
                               c.bulkGen == id_.bulkGen;
        if (!thisPaste || o == outstanding_.end()) {
          // Another paste's (an older generation, session or offer) or not a pull in flight: a stale or
          // foreign answer. Nothing of it is used, and it neither ends nor advances this paste (D2) --
          // even when its request id happens to be one in flight.
          ++counters_.chunksRejected;
        } else {
          Job* j = o->second.first;
          const fn::Pull& asked = j->pulls[o->second.second];
          outstanding_.erase(o);
          if (fn::check_chunk(asked, c) != fn::ChunkCheck::Ok) {
            ++counters_.chunksRejected;
            j->failed = true;
            failed = true;
            failure_ = fn::PasteEndReason::Verification;
          } else {
            ++counters_.chunksVerified;
            lastProgressUs_ = BulkPacer::NowUs();
            std::memcpy(j->data.data() + (c.offset - j->offset), c.data.data(), c.data.size());
            ++j->done;
            completed_.push_back(c.requestId);
            while (completed_.size() > 32) completed_.pop_front();  // the serving side remembers 32
          }
        }
      }
      if (failed) {
        // Nothing unchecked is handed on: the Reads waiting fail, the window starts over.
        FailAllLocked(fc::Status::ReadError, &answers);
      } else {
        // Finished ranges, in order: contiguous with the window -> kept; from an old position -> dropped.
        while (!jobs_.empty()) {
          Job& j = *jobs_.front();
          if (j.done != j.pulls.size()) break;
          if (aheadOn_ && j.fileIndex == aheadFile_ && j.offset == aheadStart_ + ahead_.size()) {
            ahead_.insert(ahead_.end(), j.data.begin(), j.data.end());
            // "Whole file verified" counts the verified bytes in the order they are kept (0..size once,
            // ascending), not the order the chunks happened to arrive in.
            if (j.fileIndex < coverage_.size()) coverage_[j.fileIndex].OnVerified(j.offset, j.data.size());
          }
          jobs_.pop_front();
        }
        ProcessLocked(&answers);
      }
      if (open_ && !waiting_.empty() && BulkPacer::NowUs() - lastProgressUs_ > stallUs_.load()) {
        // Waited with no verified chunk for the whole bound: the paste has stalled (A4).
        if (failure_ == fn::PasteEndReason::None) failure_ = fn::PasteEndReason::Idle;
        FailAllLocked(fc::Status::Timeout, &answers);
      }
      if (bulk_.IsClosed()) {
        // The serving side stopped answering for the channel's whole retry budget.
        if (failure_ == fn::PasteEndReason::None) failure_ = fn::PasteEndReason::Session;
        FailAllLocked(fc::Status::ReadError, &answers);
        open_ = false;
      }
      PumpPullsLocked();
    }
    if (answer_) {
      for (const fc::ReadData& d : answers) answer_(inst, d);
    }
  }
}

// ------------------------------------------------------------------------------ FileChunkServer

void FileChunkServer::Begin(const FilePasteIdentity& id, std::vector<uint64_t> sizes, ReadFn read) {
  std::lock_guard<std::mutex> lock(mu_);
  active_ = true;
  id_ = id;
  sizes_ = std::move(sizes);
  read_ = std::move(read);
  sent_.clear();
}

void FileChunkServer::End() {
  std::lock_guard<std::mutex> lock(mu_);
  active_ = false;
  read_ = nullptr;
  sent_.clear();
}

FileChunkServer::Counters FileChunkServer::GetCounters() const {
  std::lock_guard<std::mutex> lock(mu_);
  return counters_;
}

bool FileChunkServer::active() const {
  std::lock_guard<std::mutex> lock(mu_);
  return active_;
}

FilePasteIdentity FileChunkServer::identity() const {
  std::lock_guard<std::mutex> lock(mu_);
  return id_;
}

bool FileChunkServer::OnPull(const std::vector<uint8_t>& msg, uint64_t /*nowUs*/, std::vector<uint8_t>* out,
                             BulkServed* served) {
  fn::Pull p;
  if (!fn::parse_bulk(msg.data(), msg.size(), &p)) return false;
  ReadFn read;
  {
    std::lock_guard<std::mutex> lock(mu_);
    const bool mine = active_ && p.epochTag == id_.epochTag && p.offerId == id_.offerId && p.pasteOp == id_.pasteOp &&
                      p.bulkGen == id_.bulkGen && p.fileIndex < sizes_.size();
    if (!mine || !fn::range_ok(sizes_[p.fileIndex], p.offset, p.length, fn::kMaxFileChunkBytes)) {
      ++counters_.pullsRefused;
      return false;
    }
    read = read_;
  }
  fn::Chunk c;
  c.epochTag = p.epochTag;
  c.offerId = p.offerId;
  c.pasteOp = p.pasteOp;
  c.bulkGen = p.bulkGen;
  c.fileIndex = p.fileIndex;
  c.requestId = p.requestId;
  c.offset = p.offset;
  const fc::Status st = read ? read(p.fileIndex, p.offset, p.length, &c.data) : fc::Status::Aborted;
  if (st != fc::Status::Ok || c.data.size() != p.length || !fn::sha256(c.data.data(), c.data.size(), &c.sha256)) {
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.pullsRefused;
    return false;
  }
  *out = fn::frame_bulk(c);
  served->chunkKey = p.requestId;
  std::lock_guard<std::mutex> lock(mu_);
  if (p.triggerRequestId != fn::kNoTrigger) {
    for (auto it = sent_.begin(); it != sent_.end(); ++it) {
      if (it->first == p.triggerRequestId) {
        served->completedChunk = true;
        served->completedBytes = it->second;
        served->triggerKey = p.triggerRequestId;
        sent_.erase(it);
        break;
      }
    }
  }
  bool again = false;
  for (const auto& s : sent_) again = again || s.first == p.requestId;
  if (!again) {
    sent_.emplace_back(p.requestId, p.length);
    if (sent_.size() > 32) sent_.pop_front();
  }
  ++counters_.chunksServed;
  counters_.bytesServed += p.length;
  return true;
}

bool FileChunkServer::ChunkKeyOf(const uint8_t* message, size_t len, uint64_t* key) {
  // MessageHeader(8) + epoch(8) + offer(8) + paste(8) + gen(4) + file(4) -> requestId at byte 40.
  constexpr size_t kAt = sizeof(MessageHeader) + 8 + 8 + 8 + 4 + 4;
  if (fn::bulk_type(message, len) != static_cast<uint16_t>(fn::FileMsg::Chunk) || len < kAt + 8) return false;
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(message[kAt + i]) << (8 * i);
  *key = v;
  return true;
}

}  // namespace remote60::native_poc

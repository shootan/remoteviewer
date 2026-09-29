// See host_file_copy.hpp.

#include "host_file_copy.hpp"

#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>

namespace remote60::native_poc {

namespace fc = remote60::native_poc::file_copy;
namespace fn = remote60::native_poc::file_copy::net;

namespace {

uint32_t random32() {
  uint32_t v = 0;
  if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&v), sizeof(v), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    v = static_cast<uint32_t>(GetTickCount64() * 2654435761ull) ^ GetCurrentProcessId();
  }
  return v;
}

fn::PasteEndReason map_end(fc::EndReason r) {
  switch (r) {
    case fc::EndReason::Ended: return fn::PasteEndReason::Completed;
    case fc::EndReason::Error: return fn::PasteEndReason::ConsumerError;
    case fc::EndReason::Idle: return fn::PasteEndReason::Idle;
    case fc::EndReason::Cleared: return fn::PasteEndReason::Cancelled;
    case fc::EndReason::Released: return fn::PasteEndReason::ConsumerError;
    case fc::EndReason::Disconnected: return fn::PasteEndReason::Session;
    case fc::EndReason::Superseded: return fn::PasteEndReason::Superseded;
  }
  return fn::PasteEndReason::ConsumerError;
}

}  // namespace

void HostFileCopyService::Log(const std::string& line) {
  if (log_) log_(line);
  else std::cout << "[native-video-host][file-copy] " << line << "\n";
}

void HostFileCopyService::Configure(Config config, BulkArbiter* arbiter, LogFn log) {
  if (running_.load()) return;
  config_ = std::move(config);
  arbiter_ = arbiter;
  log_ = std::move(log);
  random32_ = random32();
  running_.store(true);
  bulkThread_ = std::thread([this] { BulkLoop(); });
}

void HostFileCopyService::StartTransport(SendFn send, uint32_t mtuBytes) {
  std::lock_guard<std::mutex> lock(mu_);
  send_ = std::move(send);
  mtu_ = mtuBytes;
}

void HostFileCopyService::Stop() {
  if (!running_.exchange(false)) return;
  OnSessionEnd(0);
  bulkCv_.notify_all();
  if (bulkThread_.joinable()) bulkThread_.join();
  readerRun_.store(false);
  {
    std::lock_guard<std::mutex> h(helperMu_);
    link_.Close();
  }
  if (reader_.joinable()) reader_.join();
}

bool HostFileCopyService::SendHelper(const fc::PipeFrame& f) {
  std::lock_guard<std::mutex> s(helperSendMu_);
  return link_.pipe_open() && link_.Send(f);
}

bool HostFileCopyService::EnsureHelper(std::string* why) {
  if (link_.pipe_open() && link_.helper_alive()) return true;
  const uint64_t now = GetTickCount64();
  if (now < nextLaunchMs_) {
    *why = "helper start backing off";
    return false;
  }
  readerRun_.store(false);
  if (reader_.joinable()) reader_.join();
  link_.Close();
  {
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.helperLaunches;
  }
  if (!config_.launcher || !config_.launcher(&link_, why)) {
    link_.Close();
    backoffMs_ = backoffMs_ ? (std::min)(backoffMs_ * 2, config_.backoffMaxMs) : config_.backoffFirstMs;
    nextLaunchMs_ = now + backoffMs_;
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.helperLaunchFailures;
    return false;
  }
  backoffMs_ = 0;
  nextLaunchMs_ = 0;
  readerRun_.store(true);
  reader_ = std::thread([this] { ReaderLoop(); });
  return true;
}

void HostFileCopyService::ReaderLoop() {
  while (readerRun_.load() && running_.load()) {
    fc::PipeFrame f;
    if (!link_.Receive(&f, 100)) {
      if (GetLastError() == WAIT_TIMEOUT) continue;
      break;  // the pipe is gone
    }
    switch (f.type) {
      case fc::PipeMsg::PublishResult: {
        fc::PublishResult m;
        if (!fc::decode(f, &m)) break;
        std::lock_guard<std::mutex> lock(mu_);
        publishResult_ = m;
        publishAnswered_ = true;
        publishCv_.notify_all();
        break;
      }
      case fc::PipeMsg::PasteBegin: {
        fc::PasteBegin m;
        if (fc::decode(f, &m)) OnPasteBegin(m);
        break;
      }
      case fc::PipeMsg::ReadRequest: {
        fc::ReadRequest m;
        if (fc::decode(f, &m)) OnReadRequest(m);
        break;
      }
      case fc::PipeMsg::PasteEnd: {
        fc::PasteEnd m;
        if (fc::decode(f, &m)) OnPasteEnd(m);
        break;
      }
      default:
        break;
    }
  }
  // The helper is gone: whatever it was doing is over.
  std::lock_guard<std::mutex> lock(mu_);
  if (paste_.active) EndPasteLocked(fn::PasteState::Failed, fn::PasteEndReason::Session);
  haveBegun_ = false;
  publishCv_.notify_all();
}

const HostFileCopyService::OfferRec* HostFileCopyService::FindOffer(uint64_t offerId) const {
  if (offerId != 0 && offer_.offerId == offerId) return &offer_;
  if (offerId != 0 && retired_.offerId == offerId) return &retired_;
  return nullptr;
}

bool HostFileCopyService::HandleControl(uint16_t type, const std::vector<uint8_t>& body, uint64_t servedEpoch,
                                        uint16_t* replyType, std::vector<uint8_t>* reply) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    const uint64_t tag = (static_cast<uint64_t>(random32_) << 32) | (servedEpoch & 0xFFFFFFFFull);
    if (tag != epochTag_ && epochTag_ != 0) {
      // A new session epoch reached the handlers before OnSessionEnd: nothing of the old one stays.
    }
    epochTag_ = tag;
  }
  switch (static_cast<fn::FileMsg>(type)) {
    case fn::FileMsg::Offer: {
      fn::Offer m;
      if (!fn::parse(body, &m)) return false;
      *replyType = static_cast<uint16_t>(fn::FileMsg::OfferReply);
      *reply = HandleOffer(m);
      return true;
    }
    case fn::FileMsg::PasteQuery: {
      fn::PasteQuery m;
      if (!fn::parse(body, &m)) return false;
      *replyType = static_cast<uint16_t>(fn::FileMsg::PasteQueryReply);
      *reply = HandlePasteQuery(m);
      return true;
    }
    case fn::FileMsg::Prepare: {
      fn::Prepare m;
      if (!fn::parse(body, &m)) return false;
      *replyType = static_cast<uint16_t>(fn::FileMsg::PrepareReply);
      *reply = HandlePrepare(m);
      return true;
    }
    case fn::FileMsg::End: {
      fn::End m;
      if (!fn::parse(body, &m)) return false;
      *replyType = static_cast<uint16_t>(fn::FileMsg::EndReply);
      *reply = HandleEnd(m);
      return true;
    }
    case fn::FileMsg::Status: {
      fn::StatusQuery m;
      if (!fn::parse(body, &m)) return false;
      *replyType = static_cast<uint16_t>(fn::FileMsg::StatusReply);
      *reply = HandleStatus(m);
      return true;
    }
    case fn::FileMsg::OfferQuery: {  // R->P is step 2: answer "no offer", unchanged
      fn::OfferQuery m;
      if (!fn::parse(body, &m)) return false;
      fn::OfferQueryReply r;
      {
        std::lock_guard<std::mutex> lock(mu_);
        r.epochTag = epochTag_;
      }
      r.revision = 0;
      r.unchanged = true;
      *replyType = static_cast<uint16_t>(fn::FileMsg::OfferQueryReply);
      *reply = fn::body(r);
      return true;
    }
    default:
      return false;
  }
}

std::vector<uint8_t> HostFileCopyService::HandleOffer(const fn::Offer& m) {
  fn::OfferReply r;
  r.offerId = m.offerId;
  auto refuse = [&](fn::Verdict v, const std::string& why) {
    r.verdict = v;
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.offersRefused;
    counters_.lastVerdict = static_cast<uint8_t>(v);
    std::ostringstream os;
    os << "offer refused verdict=" << static_cast<int>(v) << " files=" << m.items.size() << " (" << why << ")";
    Log(os.str());
    return fn::body(r);
  };
  {
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.offers;
  }
  if (!file_copy_allowed()) return refuse(fn::Verdict::Disabled, "switched off");
  std::string why;
  // Checked again here: the viewer is not trusted to have done it (names reach the clipboard).
  const fn::Verdict rules = fn::check_offer_items(m.items, &why);
  if (rules != fn::Verdict::Accept) return refuse(rules, why);
  for (size_t i = 0; i < m.items.size(); ++i) {
    if (m.items[i].index != i) return refuse(fn::Verdict::BadRequest, "indices are not the offer order");
  }
  if (m.items.empty()) return refuse(fn::Verdict::BadRequest, "no files");
  std::lock_guard<std::mutex> h(helperMu_);
  if (!EnsureHelper(&why)) return refuse(fn::Verdict::HelperUnavailable, why);
  fc::PublishRemoteFiles pub;
  pub.offerId = m.offerId;
  for (const fn::OfferItem& it : m.items) pub.items.push_back({it.name, it.size, it.mtime, it.attributes});
  {
    std::lock_guard<std::mutex> lock(mu_);
    publishAnswered_ = false;
  }
  if (!SendHelper(fc::encode(pub))) return refuse(fn::Verdict::HelperUnavailable, "the helper pipe failed");
  std::unique_lock<std::mutex> lock(mu_);
  publishCv_.wait_for(lock, std::chrono::milliseconds(config_.publishWaitMs),
                      [&] { return publishAnswered_ && publishResult_.offerId == m.offerId; });
  if (!publishAnswered_ || publishResult_.offerId != m.offerId || publishResult_.status != fc::Status::Ok) {
    lock.unlock();
    return refuse(fn::Verdict::HelperUnavailable, publishAnswered_ ? "the helper refused it" : "the helper did not answer");
  }
  // The new offer is the future; a paste already running on the older one runs on (debate "공통 상태").
  if (offer_.offerId != 0) retired_ = offer_;
  offer_.offerId = m.offerId;
  offer_.items = m.items;
  ++counters_.offersAccepted;
  counters_.lastVerdict = 0;
  r.verdict = fn::Verdict::Accept;
  std::ostringstream os;
  os << "offer published files=" << m.items.size();
  Log(os.str());
  return fn::body(r);
}

void HostFileCopyService::OnPasteBegin(const fc::PasteBegin& m) {
  bool refuse = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.pastesBegun;
    // One paste at a time: a second one fails before any byte (the helper's descriptor), never
    // replaces the running one.
    refuse = paste_.active || haveBegun_ || !FindOffer(m.offerId) || !file_copy_allowed();
    if (!refuse) {
      haveBegun_ = true;
      begunOffer_ = m.offerId;
      begunOp_ = m.pasteOp;
    } else {
      ++counters_.pastesBusy;
    }
  }
  if (refuse) {
    fc::PasteDescriptor d;
    d.offerId = m.offerId;
    d.pasteOp = m.pasteOp;
    d.status = fc::Status::Refused;
    (void)SendHelper(fc::encode(d));
    Log("paste refused: another paste runs, or the offer is gone");
  }
}

std::vector<uint8_t> HostFileCopyService::HandlePasteQuery(const fn::PasteQuery& m) {
  std::lock_guard<std::mutex> lock(mu_);
  fn::PasteQueryReply r;
  r.offerId = m.offerId;
  if (!file_copy_allowed()) {
    r.state = fn::PasteState::Withdrawn;
    r.reason = fn::PasteEndReason::Disabled;
  } else if (haveBegun_) {
    r.offerId = begunOffer_;  // whichever offer it began on (a newer copy may have replaced it)
    r.state = fn::PasteState::Begun;
    r.pasteOp = begunOp_;
  } else if (paste_.active && m.offerId == paste_.offerId) {
    r.state = fn::PasteState::Active;
    r.pasteOp = paste_.pasteOp;
  } else if (lastEnded_.pasteOp != 0 && m.offerId == lastEnded_.offerId) {
    r.state = lastEnded_.state;
    r.pasteOp = lastEnded_.pasteOp;
    r.reason = lastEnded_.reason;
  } else if (m.offerId == offer_.offerId || m.offerId == retired_.offerId) {
    r.state = fn::PasteState::None;
  } else {
    r.state = fn::PasteState::Withdrawn;
  }
  return fn::body(r);
}

std::vector<uint8_t> HostFileCopyService::HandlePrepare(const fn::Prepare& m) {
  fn::PrepareReply r;
  r.direction = m.direction;
  r.offerId = m.offerId;
  r.pasteOp = m.pasteOp;
  fc::PasteDescriptor d;
  d.offerId = m.offerId;
  d.pasteOp = m.pasteOp;
  {
    std::lock_guard<std::mutex> lock(mu_);
    r.epochTag = epochTag_;
    const OfferRec* offer = FindOffer(m.offerId);
    fc::Status failed = fc::Status::Ok;
    if (m.direction != fn::Direction::PtoR || !file_copy_allowed()) {
      r.verdict = !file_copy_allowed() ? fn::Verdict::Disabled : fn::Verdict::BadRequest;
    } else if (!haveBegun_ || begunOffer_ != m.offerId || begunOp_ != m.pasteOp || !offer) {
      r.verdict = fn::Verdict::UnknownId;
    } else if (m.items.size() != offer->items.size()) {
      r.verdict = fn::Verdict::BadRequest;
    } else {
      for (size_t i = 0; i < m.items.size() && failed == fc::Status::Ok; ++i) {
        const fn::PreparedItem& it = m.items[i];
        if (it.index != i) failed = fc::Status::BadRequest;
        else if (it.status != static_cast<uint16_t>(fc::Status::Ok)) failed = static_cast<fc::Status>(it.status);
        else if (it.size != offer->items[i].size) failed = fc::Status::Changed;
      }
      if (failed != fc::Status::Ok) {
        r.verdict = failed == fc::Status::Refused ? fn::Verdict::Busy : fn::Verdict::BadRequest;
      } else if (arbiter_ && !arbiter_->TryAcquire(BulkUse::File, m.pasteOp)) {
        r.verdict = fn::Verdict::Busy;
        failed = fc::Status::Refused;
      } else {
        r.verdict = fn::Verdict::Accept;
      }
    }
    haveBegun_ = false;
    if (r.verdict == fn::Verdict::Accept) {
      paste_ = Paste{};
      paste_.active = true;
      paste_.offerId = m.offerId;
      paste_.pasteOp = m.pasteOp;
      paste_.bulkGen = gens_.Next();
      for (size_t i = 0; i < m.items.size(); ++i) {
        paste_.sizes.push_back(m.items[i].size);
        paste_.coverage.emplace_back(m.items[i].size);
        fc::RemoteFileItem ri;
        ri.name = offer->items[i].name;
        ri.size = m.items[i].size;
        ri.mtime = m.items[i].mtime;
        ri.attributes = offer->items[i].attributes;
        d.items.push_back(ri);
      }
      r.bulkGen = paste_.bulkGen;
      nextRequestId_ = 1;
      completed_.clear();
      bulk_.Reset();
      bulk_.Configure(send_, bulk_stream_id(paste_.bulkGen, kFileBulkStreamHostToClient),
                      bulk_stream_id(paste_.bulkGen, kFileBulkStreamClientToHost), mtu_);
      bulkOpen_ = true;
      ++counters_.pastesPrepared;
      d.status = fc::Status::Ok;
      bulkCv_.notify_all();
    } else {
      d.status = failed != fc::Status::Ok ? failed : fc::Status::Refused;
      if (r.verdict == fn::Verdict::Busy) ++counters_.pastesBusy;
      else ++counters_.pastesFailed;
    }
  }
  // The helper's descriptor: the confirmed sizes / times, or the reason the paste fails before a byte.
  if (r.verdict != fn::Verdict::UnknownId) (void)SendHelper(fc::encode(d));
  std::ostringstream os;
  os << "prepare verdict=" << static_cast<int>(r.verdict) << " files=" << m.items.size();
  Log(os.str());
  return fn::body(r);
}

std::vector<uint8_t> HostFileCopyService::HandleEnd(const fn::End& m) {
  fn::EndReply r;
  r.offerId = m.offerId;
  r.pasteOp = m.pasteOp;
  bool clear = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (m.pasteOp == 0) {
      // Withdraw the offer: off the remote clipboard. A paste already running on it runs on.
      if (offer_.offerId == m.offerId) {
        retired_ = offer_;
        offer_ = OfferRec{};
        clear = true;
      }
      r.state = fn::PasteState::Withdrawn;
    } else if (paste_.active && paste_.pasteOp == m.pasteOp) {
      FailJobsLocked(fc::Status::Aborted);
      EndPasteLocked(fn::PasteState::Failed, m.reason == fn::PasteEndReason::None ? fn::PasteEndReason::Cancelled : m.reason);
      r.state = fn::PasteState::Failed;
    } else {
      r.state = fn::PasteState::None;
    }
  }
  if (clear) {
    fc::ClearRemoteFiles c;
    c.offerId = m.offerId;
    (void)SendHelper(fc::encode(c));
  }
  return fn::body(r);
}

std::vector<uint8_t> HostFileCopyService::HandleStatus(const fn::StatusQuery& m) {
  std::lock_guard<std::mutex> lock(mu_);
  fn::StatusReply r;
  r.offerId = m.offerId;
  r.pasteOp = m.pasteOp;
  if (paste_.active && paste_.pasteOp == m.pasteOp) {
    r.state = fn::PasteState::Active;
    r.bytesDelivered = paste_.bytesDelivered;
  } else if (lastEnded_.pasteOp == m.pasteOp && m.pasteOp != 0) {
    r.state = lastEnded_.state;
    r.reason = lastEnded_.reason;
  } else {
    r.state = fn::PasteState::None;
  }
  return fn::body(r);
}

void HostFileCopyService::OnReadRequest(const fc::ReadRequest& m) {
  fc::ReadData refuse;
  refuse.offerId = m.offerId;
  refuse.pasteOp = m.pasteOp;
  refuse.fileIndex = m.fileIndex;
  refuse.offset = m.offset;
  {
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.readsRequested;
    const bool mine = paste_.active && paste_.offerId == m.offerId && paste_.pasteOp == m.pasteOp &&
                      m.fileIndex < paste_.sizes.size() && file_copy_allowed();
    if (!mine) {
      refuse.status = fc::Status::UnknownId;
    } else if (!fn::range_ok(paste_.sizes[m.fileIndex], m.offset, m.length, fc::kMaxChunkBytes)) {
      refuse.status = fc::Status::BadRequest;
    } else if (m.length == 0) {
      refuse.status = fc::Status::Ok;  // nothing to fetch (end of file)
    } else {
      auto job = std::make_unique<Job>();
      job->req = m;
      job->data.resize(m.length);
      for (uint64_t off = 0; off < m.length; off += fn::kMaxFileChunkBytes) {
        fn::Pull p;
        p.epochTag = epochTag_;
        p.offerId = m.offerId;
        p.pasteOp = m.pasteOp;
        p.bulkGen = paste_.bulkGen;
        p.fileIndex = m.fileIndex;
        p.offset = m.offset + off;
        p.length = static_cast<uint32_t>((std::min<uint64_t>)(fn::kMaxFileChunkBytes, m.length - off));
        job->pulls.push_back(p);
      }
      jobs_.push_back(std::move(job));
      PumpPullsLocked();
      bulkCv_.notify_all();
      return;
    }
    if (refuse.status != fc::Status::Ok) ++counters_.readsFailed;
  }
  (void)SendHelper(fc::encode(refuse));
}

void HostFileCopyService::PumpPullsLocked() {
  if (!bulkOpen_) return;
  for (auto& jp : jobs_) {
    Job& j = *jp;
    while (!j.failed && j.nextPull < j.pulls.size() && outstanding_.size() < config_.pullWindow) {
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
    if (outstanding_.size() >= config_.pullWindow) break;
  }
}

void HostFileCopyService::FailJobsLocked(fc::Status why) {
  for (auto& jp : jobs_) {
    fc::ReadData d;
    d.offerId = jp->req.offerId;
    d.pasteOp = jp->req.pasteOp;
    d.fileIndex = jp->req.fileIndex;
    d.offset = jp->req.offset;
    d.status = why;
    ++counters_.readsFailed;
    // Sent without mu_ held would be nicer; the helper pipe write is bounded (5 s) and rare here.
    (void)SendHelper(fc::encode(d));
  }
  jobs_.clear();
  outstanding_.clear();
}

void HostFileCopyService::EndPasteLocked(fn::PasteState state, fn::PasteEndReason reason) {
  if (!paste_.active) return;
  for (size_t i = 0; i < paste_.coverage.size(); ++i) {
    if (paste_.coverage[i].whole_file_verified()) ++counters_.filesWholeVerified;
    else ++counters_.filesChunkVerified;
  }
  lastEnded_ = Ended{paste_.offerId, paste_.pasteOp, state, reason};
  if (arbiter_) arbiter_->Release(paste_.pasteOp);
  bulk_.Close(ControlCloseReason::SessionRollover);
  bulkOpen_ = false;
  jobs_.clear();
  outstanding_.clear();
  if (state == fn::PasteState::Ended && reason == fn::PasteEndReason::Completed) ++counters_.pastesEnded;
  else ++counters_.pastesFailed;
  counters_.lastEndReason = static_cast<uint8_t>(reason);
  std::ostringstream os;
  os << "paste ended state=" << static_cast<int>(state) << " reason=" << static_cast<int>(reason)
     << " bytes=" << paste_.bytesDelivered << " wholeVerified=" << counters_.filesWholeVerified;
  Log(os.str());
  paste_ = Paste{};
}

void HostFileCopyService::OnPasteEnd(const fc::PasteEnd& m) {
  std::lock_guard<std::mutex> lock(mu_);
  if (haveBegun_ && begunOp_ == m.pasteOp) haveBegun_ = false;
  if (!paste_.active || paste_.pasteOp != m.pasteOp) return;
  fn::PasteEndReason reason = map_end(m.reason);
  if (paste_.failure != fn::PasteEndReason::None && reason != fn::PasteEndReason::Completed) reason = paste_.failure;
  EndPasteLocked(m.reason == fc::EndReason::Ended ? fn::PasteState::Ended : fn::PasteState::Failed, reason);
}

bool HostFileCopyService::OnDatagram(const void* data, size_t len) {
  if (!bulk_datagram_is_file(data, len)) return false;
  (void)bulk_.OnPacket(data, len);  // dropped by the channel unless it is the open stream
  return true;
}

void HostFileCopyService::BulkLoop() {
  std::vector<uint8_t> msg;
  while (running_.load()) {
    {
      std::unique_lock<std::mutex> lock(mu_);
      if (!bulkOpen_) {
        bulkCv_.wait_for(lock, std::chrono::milliseconds(200));
        continue;
      }
    }
    const bool got = bulk_.Receive(&msg, 20);
    bulk_.Tick();
    std::vector<fc::ReadData> answers;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (!bulkOpen_) continue;
      fn::Chunk c;
      if (got && fn::parse_bulk(msg.data(), msg.size(), &c)) {
        auto o = outstanding_.find(c.requestId);
        if (o == outstanding_.end()) {
          ++counters_.chunksRejected;  // not a pull in flight (a stale or foreign answer): nothing of it is used
        } else {
          Job* j = o->second.first;
          const fn::Pull& asked = j->pulls[o->second.second];
          outstanding_.erase(o);
          if (fn::check_chunk(asked, c) != fn::ChunkCheck::Ok) {
            ++counters_.chunksRejected;
            j->failed = true;
            paste_.failure = fn::PasteEndReason::Verification;
          } else {
            ++counters_.chunksVerified;
            std::memcpy(j->data.data() + (c.offset - j->req.offset), c.data.data(), c.data.size());
            ++j->done;
            completed_.push_back(c.requestId);
            while (completed_.size() > 32) completed_.pop_front();  // the viewer remembers 32
            if (asked.fileIndex < paste_.coverage.size()) paste_.coverage[asked.fileIndex].OnVerified(c.offset, c.data.size());
          }
        }
      }
      // Finished reads, in order: verified bytes go to the helper, a failed read fails.
      while (!jobs_.empty()) {
        Job& j = *jobs_.front();
        const bool finished = j.failed || j.done == j.pulls.size();
        if (!finished) break;
        bool inFlight = false;
        for (const auto& o : outstanding_) inFlight = inFlight || o.second.first == &j;
        if (inFlight) {
          for (auto it = outstanding_.begin(); it != outstanding_.end();) {
            it = it->second.first == &j ? outstanding_.erase(it) : std::next(it);
          }
        }
        fc::ReadData d;
        d.offerId = j.req.offerId;
        d.pasteOp = j.req.pasteOp;
        d.fileIndex = j.req.fileIndex;
        d.offset = j.req.offset;
        if (j.failed) {
          d.status = fc::Status::ReadError;
          ++counters_.readsFailed;
        } else {
          d.status = fc::Status::Ok;
          d.data = std::move(j.data);
          paste_.bytesDelivered += d.data.size();
          counters_.bytesDelivered += d.data.size();
          ++counters_.readsServed;
        }
        answers.push_back(std::move(d));
        jobs_.pop_front();
      }
      if (bulk_.IsClosed() && paste_.active) {
        // The viewer stopped answering for the channel's whole retry budget.
        paste_.failure = fn::PasteEndReason::Session;
        for (auto& jp : jobs_) {
          fc::ReadData d;
          d.offerId = jp->req.offerId;
          d.pasteOp = jp->req.pasteOp;
          d.fileIndex = jp->req.fileIndex;
          d.offset = jp->req.offset;
          d.status = fc::Status::ReadError;
          ++counters_.readsFailed;
          answers.push_back(std::move(d));
        }
        jobs_.clear();
        outstanding_.clear();
        bulkOpen_ = false;
      }
      PumpPullsLocked();
    }
    for (const fc::ReadData& d : answers) (void)SendHelper(fc::encode(d));
  }
}

void HostFileCopyService::OnSessionEnd(uint64_t /*newEpoch*/) {
  bool shut = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    FailJobsLocked(fc::Status::Aborted);
    EndPasteLocked(fn::PasteState::Failed, fn::PasteEndReason::Session);
    haveBegun_ = false;
    shut = offer_.offerId != 0 || retired_.offerId != 0;
    offer_ = OfferRec{};
    retired_ = OfferRec{};
    lastEnded_ = Ended{};
  }
  if (shut) {
    // The helper is per session (plan §1): it clears the clipboard and exits.
    (void)SendHelper(fc::encode_shutdown());
  }
  readerRun_.store(false);
  std::lock_guard<std::mutex> h(helperMu_);
  link_.ClosePipe();
}

}  // namespace remote60::native_poc

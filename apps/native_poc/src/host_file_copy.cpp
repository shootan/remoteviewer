// See host_file_copy.hpp.

#include "host_file_copy.hpp"

#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
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

uint64_t random_id() {
  uint64_t v = 0;
  while (v == 0) {
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&v), sizeof(v), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
      v = GetTickCount64() * 6364136223846793005ull + GetCurrentProcessId();
    }
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

uint32_t env_kbps(const char* name, uint32_t fallbackBps) {
  const char* v = std::getenv(name);
  if (!v || !*v) return fallbackBps;
  const unsigned long k = std::strtoul(v, nullptr, 10);
  if (k == 0 || k > 1000000) return fallbackBps;
  return static_cast<uint32_t>(k * 1000);
}

}  // namespace

BulkRateConfig host_file_bulk_rate_config_from_env() {
  BulkRateConfig c;
  // Debate D1: a configured hard ceiling and a conservative start -- no "remaining budget" formula
  // (nothing measures the link's spare capacity). The picture comes first; a paste may get slow.
  c.capBps = env_kbps("REMOTE60_FILE_BULK_CAP_KBPS", 8000000);
  c.startBps = (std::min)(env_kbps("REMOTE60_FILE_BULK_START_KBPS", 256000), c.capBps);
  return c;
}

HostFileCopyService::HostFileCopyService() : receiver_(4) {}

void HostFileCopyService::Log(const std::string& line) {
  if (log_) log_(line);
  else std::cout << "[native-video-host][file-copy] " << line << "\n";
}

void HostFileCopyService::Configure(Config config, BulkArbiter* arbiter, LogFn log) {
  if (running_.load()) return;
  config_ = std::move(config);
  enabled_.store(config_.enabled);
  arbiter_ = arbiter;
  log_ = std::move(log);
  random32_ = random32();
  running_.store(true);
  FileHelperChannel::Config hc;
  hc.launcher = config_.launcher;
  hc.backoffFirstMs = config_.backoffFirstMs;
  hc.backoffMaxMs = config_.backoffMaxMs;
  helper_.Configure(
      hc, [this](uint64_t owner, uint64_t instance, const fc::PipeFrame& f) { OnHelperFrame(owner, instance, f); },
      [this](uint64_t owner, uint64_t instance) { OnHelperGone(owner, instance); });
  receiver_.Start([this](const fc::ReadData& d) { (void)helper_.Send(fc::encode(d)); });
  worker_ = std::thread([this] { WorkerLoop(); });
}

void HostFileCopyService::StartTransport(SendFn send, uint32_t mtuBytes) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    send_ = std::move(send);
    mtu_ = mtuBytes;
  }
  uplink_.Configure(send_, config_.pingRtt ? config_.pingRtt : [] { return uint64_t{0}; },
                    config_.videoBusy ? config_.videoBusy : [] { return false; }, mtuBytes, config_.rate,
                    fn::kMaxFileChunkBytes);
}

void HostFileCopyService::Stop() {
  if (!running_.load()) return;
  OnSessionEnd(0);
  running_.store(false);
  workCv_.notify_all();
  if (worker_.joinable()) worker_.join();
  receiver_.Stop();
  uplink_.Close();
  helper_.Stop();
}

// ------------------------------------------------------------------------------ the helper

// A frame of the helper `instance`, started for `owner` (r6/r7). It is taken only while that very
// helper is the channel's -- decided under mu_ together with what it changes, so a frame of an
// earlier helper that arrives late (its reader was still delivering) changes nothing of the current
// one's: not its publish / pin / read answers (whose ids repeat per session AND per helper), not its
// pastes. "Earlier" is by instance, not just by session: a helper replaced within the same session
// (its pipe failed) is as foreign as one of another session.
void HostFileCopyService::OnHelperFrame(uint64_t owner, uint64_t instance, const fc::PipeFrame& f) {
  if (epochProbe_) epochProbe_(owner, 7);
  switch (f.type) {
    case fc::PipeMsg::PublishResult: {
      fc::PublishResult m;
      if (!fc::decode(f, &m)) break;
      std::lock_guard<std::mutex> lock(mu_);
      if (!FrameAcceptedLocked(owner, instance)) break;
      publishResult_ = m;
      publishAnswered_ = true;
      replyCv_.notify_all();
      break;
    }
    case fc::PipeMsg::PasteBegin: {
      fc::PasteBegin m;
      if (fc::decode(f, &m)) OnPasteBegin(owner, instance, m);
      break;
    }
    case fc::PipeMsg::ReadRequest: {
      fc::ReadRequest m;
      if (!fc::decode(f, &m)) break;
      bool allowed = false;
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (!FrameAcceptedLocked(owner, instance)) break;
        allowed = file_copy_allowed();
        if (allowed) receiver_.Submit(m);  // under mu_: the receiver is opened / closed under it with its paste
      }
      if (!allowed) {  // switched off: nothing more reaches the helper
        fc::ReadData d;
        d.offerId = m.offerId;
        d.pasteOp = m.pasteOp;
        d.fileIndex = m.fileIndex;
        d.offset = m.offset;
        d.status = fc::Status::Aborted;
        (void)helper_.Send(fc::encode(d));
      }
      break;
    }
    case fc::PipeMsg::PasteEnd: {
      fc::PasteEnd m;
      if (fc::decode(f, &m)) OnPasteEnd(owner, instance, m);
      break;
    }
    case fc::PipeMsg::Stats: {
      fc::Stats m;
      if (fc::decode(f, &m)) OnStats(owner, instance, m);
      break;
    }
    case fc::PipeMsg::PinResult: {
      fc::PinResult m;
      if (!fc::decode(f, &m)) break;
      std::lock_guard<std::mutex> lock(mu_);
      if (!FrameAcceptedLocked(owner, instance)) break;
      pinResult_ = std::move(m);
      pinAnswered_ = true;
      replyCv_.notify_all();
      break;
    }
    case fc::PipeMsg::LocalData: {
      fc::LocalData m;
      if (!fc::decode(f, &m)) break;
      std::lock_guard<std::mutex> lock(mu_);
      if (!FrameAcceptedLocked(owner, instance)) break;
      localData_ = std::move(m);
      localAnswered_ = true;
      replyCv_.notify_all();
      break;
    }
    default:
      break;
  }
}

bool HostFileCopyService::FrameAcceptedLocked(uint64_t owner, uint64_t instance) {
  if (owner == helper_.owner() && instance == helper_.instance()) return true;
  ++counters_.staleHelperFrames;
  return false;
}

void HostFileCopyService::OnHelperGone(uint64_t owner, uint64_t instance) {
  // Whatever the helper was doing is over: its clipboard object, its reads, its pins.
  bool closeSend = false;
  BulkKey key;
  {
    std::lock_guard<std::mutex> lock(mu_);
    // The helper of a session that has since ended (torn down with it): the current session's
    // state, paste and helper are not its business (r5).
    if (owner != helper_.owner() || instance != helper_.instance()) {
      Log("an earlier helper went (another session's, or one replaced in this one): the current state stays");
      ++counters_.staleHelperGones;
      return;
    }
    if (paste_.dir != Dir::None) {
      key = KeyOfPasteLocked();
      closeSend = EndPasteLocked(fn::PasteState::Failed, fn::PasteEndReason::Session);
    }
    haveBegun_ = false;
    if (statId_ != 0) {
      statId_ = 0;
      statWanted_ = clipSeq_ != hostOffer_.revision && !clipPaths_.empty();
    }
    replyCv_.notify_all();
  }
  if (closeSend) FinishSendClose(key);
  workCv_.notify_all();
}

void HostFileCopyService::WorkerLoop() {
  std::unique_lock<std::mutex> lock(mu_);
  while (running_.load()) {
    workCv_.wait_for(lock, std::chrono::milliseconds(500), [&] { return !running_.load() || (statWanted_ && statId_ == 0); });
    if (!running_.load()) break;
    if (!statWanted_ || statId_ != 0) continue;
    statWanted_ = false;
    const uint64_t seq = clipSeq_;
    const std::vector<std::wstring> paths = clipPaths_;
    if (paths.empty() || !file_copy_allowed()) continue;
    lock.unlock();
    std::string why;
    const bool up = helper_.Ensure(&why);
    fc::StatFiles s;
    for (const std::wstring& p : paths) s.paths.emplace_back(p.begin(), p.end());
    lock.lock();
    if (seq != clipSeq_) {
      statWanted_ = true;  // it changed again meanwhile: the newer content next
      continue;
    }
    if (!up) {
      // The clipboard names files this service cannot identify now: no stale offer stands for it.
      if (hostOffer_.offerId != 0) hostRetired_ = hostOffer_;
      hostOffer_ = HostOffer{};
      hostOffer_.revision = seq;
      Log("host copy not offered: helper unavailable (" + why + ")");
      continue;
    }
    s.requestId = nextStatId_++;
    statId_ = s.requestId;
    statSeq_ = seq;
    lock.unlock();
    const bool sent = helper_.Send(fc::encode(s));
    lock.lock();
    if (!sent && statId_ == s.requestId) {
      statId_ = 0;
      statWanted_ = true;
    }
  }
}

// ------------------------------------------------------------------------------ R->P: this PC's clipboard

void HostFileCopyService::OnHostClipboard(uint64_t seq, std::vector<std::wstring> paths) {
  std::lock_guard<std::mutex> lock(mu_);
  if (seq == clipSeq_ && paths == clipPaths_) return;
  clipSeq_ = seq;
  // The limit is the copy's (r2 ⑤): more than the limit is not offered at all -- not as whichever
  // files came first. (The monitor hands over one over the limit to say so.)
  const bool tooMany = paths.size() > fc::kMaxFiles;
  if (tooMany) {
    clipPaths_.clear();
    if (hostOffer_.offerId != 0) hostRetired_ = hostOffer_;
    hostOffer_ = HostOffer{};
    hostOffer_.revision = seq;
    ++counters_.hostCopies;
    Log("host copy not offered: more than " + std::to_string(fc::kMaxFiles) + " files");
    return;
  }
  clipPaths_ = std::move(paths);
  if (clipPaths_.empty()) {
    // Anything else on the clipboard: no files to offer. A paste already running is untouched.
    if (hostOffer_.offerId != 0) hostRetired_ = hostOffer_;
    hostOffer_ = HostOffer{};
    hostOffer_.revision = seq;
    return;
  }
  ++counters_.hostCopies;
  if (baselineSet_ && seq > baselineSeq_) {
    statWanted_ = true;  // identified by the worker, as the user (the helper starts only if needed)
    workCv_.notify_all();
  }
}

void HostFileCopyService::OnStats(uint64_t owner, uint64_t instance, const fc::Stats& m) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!FrameAcceptedLocked(owner, instance)) return;
  if (m.requestId != statId_ || statId_ == 0) return;  // an answer nobody waits for
  statId_ = 0;
  HostOffer o;
  o.revision = statSeq_;
  uint64_t excluded = 0;
  for (size_t i = 0; i < m.entries.size() && i < clipPaths_.size(); ++i) {
    const fc::StatEntry& e = m.entries[i];
    if (e.status != fc::Status::Ok || e.name.empty()) {
      ++excluded;
      continue;
    }
    fn::OfferItem it;
    it.index = static_cast<uint32_t>(o.items.size());  // = the pin order
    it.name = e.name;
    it.size = e.size;
    it.mtime = e.mtime;
    it.attributes = e.attributes;
    o.items.push_back(std::move(it));
    HostFile f;
    const std::wstring& p = clipPaths_[i];
    f.path.assign(p.begin(), p.end());
    f.id = e.id;
    f.size = e.size;
    f.mtime = e.mtime;
    o.files.push_back(std::move(f));
  }
  std::string why;
  const fn::Verdict v = o.items.empty() ? fn::Verdict::BadRequest : fn::check_offer_items(o.items, &why);
  if (hostOffer_.offerId != 0) hostRetired_ = hostOffer_;
  counters_.hostFilesExcluded += excluded;
  if (v != fn::Verdict::Accept || statSeq_ != clipSeq_) {
    hostOffer_ = HostOffer{};
    hostOffer_.revision = statSeq_;
    if (statSeq_ != clipSeq_) {
      statWanted_ = true;  // the clipboard moved on while the helper looked
      workCv_.notify_all();
    } else {
      std::ostringstream os;
      os << "host copy not offered: files=" << o.items.size() << " excluded=" << excluded << " (" << why << ")";
      Log(os.str());
    }
    return;
  }
  o.offerId = random_id();
  hostOffer_ = std::move(o);
  ++counters_.hostOffers;
  counters_.hostFilesOffered += hostOffer_.items.size();
  std::ostringstream os;
  os << "host copy offered files=" << hostOffer_.items.size() << " excluded=" << excluded;
  Log(os.str());
}

const HostFileCopyService::HostOffer* HostFileCopyService::FindHostOffer(uint64_t offerId) const {
  if (offerId != 0 && hostOffer_.offerId == offerId) return &hostOffer_;
  if (offerId != 0 && hostRetired_.offerId == offerId) return &hostRetired_;
  return nullptr;
}

std::vector<uint8_t> HostFileCopyService::HandleOfferQuery(const fn::OfferQuery& m, uint64_t epoch) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!Current(epoch)) return {};
  ++counters_.offerQueries;
  fn::OfferQueryReply r;
  r.epochTag = epochTag_;
  if (!file_copy_allowed()) {
    r.revision = 0;
    r.unchanged = m.knownRevision == 0;
    return fn::body(r);
  }
  if (!baselineSet_) {
    baselineSet_ = true;
    baselineSeq_ = clipSeq_;
  }
  if (clipSeq_ <= baselineSeq_ || hostOffer_.revision <= baselineSeq_) {
    // Nothing copied here since this session began (or not identified yet): the baseline, no files.
    if (clipSeq_ > baselineSeq_ && !clipPaths_.empty() && statId_ == 0 && !statWanted_) {
      statWanted_ = true;  // a copy made while connected, not identified yet: now (A1)
      workCv_.notify_all();
    }
    r.revision = baselineSeq_;
    r.unchanged = m.knownRevision == r.revision;
    return fn::body(r);
  }
  // A negotiated session is asking: a copy not identified yet is identified now (A1).
  if (clipSeq_ != hostOffer_.revision && !clipPaths_.empty() && statId_ == 0 && !statWanted_) {
    statWanted_ = true;
    workCv_.notify_all();
  }
  r.revision = hostOffer_.revision;
  r.unchanged = m.knownRevision == r.revision;
  if (!r.unchanged) {
    r.offerId = hostOffer_.offerId;
    r.items = hostOffer_.items;  // empty when the clipboard names no file
  }
  return fn::body(r);
}

std::vector<uint8_t> HostFileCopyService::HandlePrepareRtoP(const fn::Prepare& m, uint64_t epoch) {
  fn::PrepareReply r;
  r.direction = fn::Direction::RtoP;
  r.offerId = m.offerId;
  r.pasteOp = m.pasteOp;
  std::vector<HostFile> files;
  std::vector<fn::OfferItem> items;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!Current(epoch)) return {};
    r.epochTag = epochTag_;
    const HostOffer* offer = FindHostOffer(m.offerId);
    if (!file_copy_allowed()) {
      r.verdict = fn::Verdict::Disabled;
    } else if (!offer) {
      r.verdict = fn::Verdict::UnknownId;
    } else if (!m.items.empty() || m.pasteOp == 0) {
      r.verdict = fn::Verdict::BadRequest;
    } else if (paste_.dir != Dir::None || haveBegun_ || (arbiter_ && !arbiter_->TryAcquire(BulkUse::File, m.pasteOp))) {
      r.verdict = fn::Verdict::Busy;  // one paste per session, either direction; never swapped in
    } else {
      r.verdict = fn::Verdict::Accept;
      files = offer->files;
      items = offer->items;
      paste_ = Paste{};
      paste_.dir = Dir::RtoP;  // reserved: the pin decides
      paste_.epoch = epoch;
      paste_.offerId = m.offerId;
      paste_.pasteOp = m.pasteOp;
      pinAnswered_ = false;
    }
    if (r.verdict != fn::Verdict::Accept) {
      ++counters_.sendRefused;
      counters_.lastSendVerdict = static_cast<uint8_t>(r.verdict);
      std::ostringstream os;
      os << "send prepare refused verdict=" << static_cast<int>(r.verdict);
      Log(os.str());
      return fn::body(r);
    }
  }
  if (epochProbe_) epochProbe_(epoch, 8);
  // Pinned by the user's helper: the same FileId, a writer refused, the size / time of the offer.
  // As this session (r5): an ended session's request starts no helper and pins nothing.
  std::string why;
  bool stale = false;
  bool up = helper_.EnsureAs(epoch, &why, &stale);
  if (up) {
    fc::Pin pin;
    pin.pinId = m.pasteOp;
    pin.leaseMs = kHostFilePinLeaseMs;
    for (const HostFile& f : files) pin.entries.push_back({f.path, f.id, f.size, f.mtime});
    up = helper_.SendAs(epoch, fc::encode(pin), &stale);
    if (!up) why = "the helper pipe failed";
  }
  std::unique_lock<std::mutex> lock(mu_);
  const BulkKey mine{epoch, m.offerId, m.pasteOp};
  if (stale) return StaleLocked(mine);
  if (up) {
    replyCv_.wait_for(lock, std::chrono::milliseconds(config_.pinWaitMs), [&] {
      return (pinAnswered_ && pinResult_.pinId == m.pasteOp) || KeyOfPasteLocked() != mine || !Current(epoch);
    });
  }
  if (!Current(epoch)) return StaleLocked(mine);
  const bool answered = up && pinAnswered_ && pinResult_.pinId == m.pasteOp && KeyOfPasteLocked() == mine;
  bool allOk = answered && pinResult_.entries.size() == files.size();
  std::vector<uint64_t> sizes;
  for (size_t i = 0; i < files.size(); ++i) {
    fn::PreparedItem it;
    it.index = static_cast<uint32_t>(i);
    const fc::PinResultEntry res =
        answered && i < pinResult_.entries.size() ? pinResult_.entries[i] : fc::PinResultEntry{fc::Status::ReadError};
    it.status = static_cast<uint16_t>(res.status);
    it.size = res.size;
    it.mtime = res.mtime;
    it.attributes = items[i].attributes;
    allOk = allOk && res.status == fc::Status::Ok;
    sizes.push_back(res.size);
    r.items.push_back(it);
  }
  if (!allOk) {
    r.verdict = up ? fn::Verdict::BadRequest : fn::Verdict::HelperUnavailable;
    if (KeyOfPasteLocked() == mine) {  // ours, by the whole key (r6): not another session's same op
      paste_ = Paste{};
      if (arbiter_) arbiter_->Release(m.pasteOp);
    }
    ++counters_.sendRefused;
    counters_.lastSendVerdict = static_cast<uint8_t>(r.verdict);
    lock.unlock();
    if (up) (void)helper_.SendAs(epoch, fc::encode(fc::Unpin{m.pasteOp}));
    std::ostringstream os;
    os << "send prepare failed: pinned=" << (answered ? 1 : 0) << " verdict=" << static_cast<int>(r.verdict)
       << (up ? "" : " (" + why + ")");
    Log(os.str());
    return fn::body(r);
  }
  paste_.bulkGen = gens_.Next();
  paste_.sizes = sizes;
  r.bulkGen = paste_.bulkGen;
  sendAborting_ = false;
  sendBytesAtStart_ = server_.GetCounters().bytesServed;
  const FilePasteIdentity id{r.epochTag, m.offerId, m.pasteOp, r.bulkGen};
  const BulkKey key = KeyOfPasteLocked();
  lock.unlock();
  if (epochProbe_) epochProbe_(epoch, 5);
  // The shared sender is begun under its owner key (r5): if the session ended meanwhile (the paste
  // is no longer this one's), nothing is begun and the request is dropped -- a newer session's send
  // is never overwritten.
  if (!BeginSend(key, id, sizes)) {
    (void)helper_.SendAs(epoch, fc::encode(fc::Unpin{m.pasteOp}));
    std::lock_guard<std::mutex> again(mu_);
    return StaleLocked(key);
  }
  {
    std::lock_guard<std::mutex> again(mu_);
    ++counters_.sendPrepared;
    counters_.lastSendVerdict = 0;
  }
  std::ostringstream os;
  os << "send prepared files=" << files.size() << " gen=" << r.bulkGen;
  Log(os.str());
  return fn::body(r);
}

fc::Status HostFileCopyService::ReadLocal(uint64_t epoch, uint64_t pinId, uint32_t index, uint64_t offset, uint32_t length,
                                          std::vector<uint8_t>* out) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (sendAborting_ || !file_copy_allowed()) return fc::Status::Aborted;
    localAnswered_ = false;
  }
  // As the send's session (r5): a read of a send whose session ended reaches no newer helper.
  if (!helper_.SendAs(epoch, fc::encode(fc::ReadLocal{pinId, index, offset, length}))) {
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.localReadFailures;
    return fc::Status::ReadError;
  }
  std::unique_lock<std::mutex> lock(mu_);
  replyCv_.wait_for(lock, std::chrono::milliseconds(config_.readWaitMs), [&] {
    return sendAborting_ ||
           (localAnswered_ && localData_.pinId == pinId && localData_.fileIndex == index && localData_.offset == offset);
  });
  if (sendAborting_) return fc::Status::Aborted;
  if (!localAnswered_ || localData_.pinId != pinId || localData_.fileIndex != index || localData_.offset != offset) {
    ++counters_.localReadFailures;
    return fc::Status::Timeout;
  }
  localAnswered_ = false;
  if (localData_.status != fc::Status::Ok) {
    ++counters_.localReadFailures;
    return localData_.status;
  }
  *out = std::move(localData_.data);
  return fc::Status::Ok;
}

bool HostFileCopyService::BeginSend(const BulkKey& key, const FilePasteIdentity& id, const std::vector<uint64_t>& sizes) {
  std::lock_guard<std::mutex> bulk(bulkMu_);
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!Current(key.epoch) || paste_.dir != Dir::RtoP || KeyOfPasteLocked() != key) return false;
  }
  // One sender at a time: a previous paste's sender whose closer has not come yet (its End got past
  // EndPasteLocked, then its session ended) is closed here, so the new one is never begun on top of
  // it -- and that late closer then finds the sender is not its own and leaves it (FinishSendClose).
  if (sendOwner_ != BulkKey{}) {
    Log("send begin: a previous paste's sender was still open, closed first");
    server_.End();
    uplink_.Close();
    sendOwner_ = BulkKey{};
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!Current(key.epoch) || paste_.dir != Dir::RtoP || KeyOfPasteLocked() != key) return false;
    sendOwner_ = key;
  }
  const uint64_t pinId = key.pasteOp;
  const uint64_t epoch = key.epoch;
  server_.Begin(id, sizes, [this, epoch, pinId](uint32_t index, uint64_t offset, uint32_t length, std::vector<uint8_t>* out) {
    return ReadLocal(epoch, pinId, index, offset, length, out);
  });
  uplink_.ResetRateCounters();
  uplink_.Open(bulk_stream_id(id.bulkGen, kFileBulkStreamHostToClient), bulk_stream_id(id.bulkGen, kFileBulkStreamClientToHost),
               &server_);
  return true;
}

void HostFileCopyService::FinishSendClose(const BulkKey& key) {
  std::lock_guard<std::mutex> bulk(bulkMu_);
  if (sendOwner_ == key) {
    server_.End();
    uplink_.Close();  // joins the serve thread: never under mu_ (its reads take mu_)
    sendOwner_ = BulkKey{};
  } else {
    // Not begun by this paste (reserved, never begun), or begun by a later one (r5): left alone.
    Log("send close: the sender is not this paste's, left as it is");
  }
  if (key.pasteOp != 0) (void)helper_.SendAs(key.epoch, fc::encode(fc::Unpin{key.pasteOp}));
}

std::vector<uint8_t> HostFileCopyService::StaleAs(const BulkKey& key) {
  std::lock_guard<std::mutex> lock(mu_);
  return StaleLocked(key);
}

// The request's session ended while it was on the way (r5): it is answered nothing (HandleControl
// drops it), and what it reserved is released -- but only if the reservation is still ITS OWN, by
// the whole key (r6): paste ops start at 1 in every session, so the new session may well have
// reserved the same number by now, and that one is not touched. (Its own was usually ended already
// by the switch's Teardown, which released the arbiter with it.) Caller holds mu_.
std::vector<uint8_t> HostFileCopyService::StaleLocked(const BulkKey& key) {
  if (key.pasteOp != 0 && paste_.dir != Dir::None && KeyOfPasteLocked() == key) {
    paste_ = Paste{};
    if (arbiter_) arbiter_->Release(key.pasteOp);
  }
  ++counters_.staleHelperSends;
  return {};
}

// ------------------------------------------------------------------------------ control

bool HostFileCopyService::HandleControl(uint16_t type, const std::vector<uint8_t>& body, uint64_t servedEpoch,
                                        uint16_t* replyType, std::vector<uint8_t>* reply) {
  {
    // The epoch only grows (host_session.hpp: fetch_add), but each Serve() captured its own when it
    // started, and a TCP control thread and the UDP dispatcher can both be inside Serve() for a while
    // (host_control_session.cpp, H-28). Decided, switched and recorded under ONE lock (r4): between
    // the decision and the record nothing of another session gets in, and servedEpoch_ never goes
    // down -- a request of the OLDER session, however late, is dropped and its link (out of date
    // anyway) is let go. The session-end hook takes the same lock (OnSessionEnd).
    std::lock_guard<std::mutex> session(sessionMu_);
    bool newSession = false;
    bool first = false;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (servedEpoch_ != 0 && servedEpoch < servedEpoch_) {
        Log("a request of an older control session: dropped, the current session's state stays");
        return false;
      }
      newSession = servedEpoch_ != 0 && servedEpoch > servedEpoch_;
      first = servedEpoch_ == 0;
    }
    if (epochProbe_) epochProbe_(servedEpoch, 1);
    // A request of another control session reached the handlers before its session-end hook: nothing
    // of the old one is carried into it -- not its offers, its paste or its begun state (r2 4).
    if (newSession || first) SwitchToLocked(servedEpoch, newSession);
  }
  if (epochProbe_) epochProbe_(servedEpoch, 2);
  // From here the handler runs without sessionMu_ (a newer session must not wait behind a helper
  // round trip of this one), so each handler asks again under mu_ -- at its first look at the state
  // and after every wait -- that the state is still this request's; if not it answers nothing and
  // the request is dropped like a late one.
  std::vector<uint8_t> answer;
  if (!HandleKnown(type, body, servedEpoch, replyType, &answer)) return false;
  if (answer.empty()) {
    Log("a request of a control session that ended meanwhile: dropped, nothing of it was applied");
    return false;
  }
  *reply = std::move(answer);
  return true;
}

bool HostFileCopyService::HandleKnown(uint16_t type, const std::vector<uint8_t>& body, uint64_t servedEpoch,
                                      uint16_t* replyType, std::vector<uint8_t>* reply) {
  switch (static_cast<fn::FileMsg>(type)) {
    case fn::FileMsg::Offer: {
      fn::Offer m;
      if (!fn::parse(body, &m)) return false;
      *replyType = static_cast<uint16_t>(fn::FileMsg::OfferReply);
      *reply = HandleOffer(m, servedEpoch);
      return true;
    }
    case fn::FileMsg::OfferQuery: {
      fn::OfferQuery m;
      if (!fn::parse(body, &m)) return false;
      *replyType = static_cast<uint16_t>(fn::FileMsg::OfferQueryReply);
      *reply = HandleOfferQuery(m, servedEpoch);
      return true;
    }
    case fn::FileMsg::PasteQuery: {
      fn::PasteQuery m;
      if (!fn::parse(body, &m)) return false;
      *replyType = static_cast<uint16_t>(fn::FileMsg::PasteQueryReply);
      *reply = HandlePasteQuery(m, servedEpoch);
      return true;
    }
    case fn::FileMsg::Prepare: {
      fn::Prepare m;
      if (!fn::parse(body, &m)) return false;
      *replyType = static_cast<uint16_t>(fn::FileMsg::PrepareReply);
      *reply = m.direction == fn::Direction::RtoP ? HandlePrepareRtoP(m, servedEpoch) : HandlePrepare(m, servedEpoch);
      return true;
    }
    case fn::FileMsg::End: {
      fn::End m;
      if (!fn::parse(body, &m)) return false;
      *replyType = static_cast<uint16_t>(fn::FileMsg::EndReply);
      *reply = HandleEnd(m, servedEpoch);
      return true;
    }
    case fn::FileMsg::Status: {
      fn::StatusQuery m;
      if (!fn::parse(body, &m)) return false;
      *replyType = static_cast<uint16_t>(fn::FileMsg::StatusReply);
      *reply = HandleStatus(m, servedEpoch);
      return true;
    }
    default:
      return false;
  }
}

// ------------------------------------------------------------------------------ P->R: the viewer's files

const HostFileCopyService::PeerOffer* HostFileCopyService::FindPeerOffer(uint64_t offerId) const {
  if (offerId != 0 && offer_.offerId == offerId) return &offer_;
  if (offerId != 0 && retired_.offerId == offerId) return &retired_;
  return nullptr;
}

std::vector<uint8_t> HostFileCopyService::HandleOffer(const fn::Offer& m, uint64_t epoch) {
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
    if (!Current(epoch)) return {};
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
  if (epochProbe_) epochProbe_(epoch, 3);
  // As this session (r5): once the channel has moved on to a newer session, this request neither
  // starts a helper for it nor puts anything on its clipboard -- it is dropped, nothing applied.
  bool stale = false;
  if (!helper_.EnsureAs(epoch, &why, &stale)) {
    if (stale) return StaleAs(BulkKey{});
    return refuse(fn::Verdict::HelperUnavailable, why);
  }
  fc::PublishRemoteFiles pub;
  pub.offerId = m.offerId;
  for (const fn::OfferItem& it : m.items) pub.items.push_back({it.name, it.size, it.mtime, it.attributes});
  {
    std::lock_guard<std::mutex> lock(mu_);
    publishAnswered_ = false;
  }
  if (!helper_.SendAs(epoch, fc::encode(pub), &stale)) {
    if (stale) return StaleAs(BulkKey{});
    return refuse(fn::Verdict::HelperUnavailable, "the helper pipe failed");
  }
  if (epochProbe_) epochProbe_(epoch, 4);
  std::unique_lock<std::mutex> lock(mu_);
  replyCv_.wait_for(lock, std::chrono::milliseconds(config_.publishWaitMs),
                    [&] { return (publishAnswered_ && publishResult_.offerId == m.offerId) || !Current(epoch); });
  // The session ended while the helper was answering: that helper went with it (shutdown), and the
  // offer is not the new session's -- nothing to apply, nothing to undo.
  if (!Current(epoch)) return StaleLocked(BulkKey{});
  if (!publishAnswered_ || publishResult_.offerId != m.offerId || publishResult_.status != fc::Status::Ok) {
    const bool answered = publishAnswered_;
    lock.unlock();
    return refuse(fn::Verdict::HelperUnavailable, answered ? "the helper refused it" : "the helper did not answer");
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

void HostFileCopyService::OnPasteBegin(uint64_t owner, uint64_t instance, const fc::PasteBegin& m) {
  bool refuse = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!FrameAcceptedLocked(owner, instance)) return;
    ++counters_.pastesBegun;
    // One paste at a time, either direction: a second one fails before any byte (the helper's
    // descriptor), never replaces the running one.
    refuse = paste_.dir != Dir::None || haveBegun_ || !FindPeerOffer(m.offerId) || !file_copy_allowed();
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
    (void)helper_.Send(fc::encode(d));
    Log("paste refused: another paste runs, or the offer is gone");
  }
}

std::vector<uint8_t> HostFileCopyService::HandlePasteQuery(const fn::PasteQuery& m, uint64_t epoch) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!Current(epoch)) return {};
  fn::PasteQueryReply r;
  r.offerId = m.offerId;
  if (!file_copy_allowed()) {
    r.state = fn::PasteState::Withdrawn;
    r.reason = fn::PasteEndReason::Disabled;
  } else if (haveBegun_) {
    r.offerId = begunOffer_;  // whichever offer it began on (a newer copy may have replaced it)
    r.state = fn::PasteState::Begun;
    r.pasteOp = begunOp_;
  } else if (paste_.dir == Dir::PtoR && m.offerId == paste_.offerId) {
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

std::vector<uint8_t> HostFileCopyService::HandlePrepare(const fn::Prepare& m, uint64_t epoch) {
  fn::PrepareReply r;
  r.direction = m.direction;
  r.offerId = m.offerId;
  r.pasteOp = m.pasteOp;
  fc::PasteDescriptor d;
  d.offerId = m.offerId;
  d.pasteOp = m.pasteOp;
  FilePasteIdentity id;
  std::vector<uint64_t> sizes;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!Current(epoch)) return {};
    r.epochTag = epochTag_;
    const PeerOffer* offer = FindPeerOffer(m.offerId);
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
      } else if (paste_.dir != Dir::None || (arbiter_ && !arbiter_->TryAcquire(BulkUse::File, m.pasteOp))) {
        r.verdict = fn::Verdict::Busy;
        failed = fc::Status::Refused;
      } else {
        r.verdict = fn::Verdict::Accept;
      }
    }
    // Only the begun paste THIS request names leaves the "begun" state (r2 ②): a stale or foreign
    // Prepare refused as UnknownId must not wipe the one that is waiting to be prepared.
    if (haveBegun_ && begunOffer_ == m.offerId && begunOp_ == m.pasteOp) haveBegun_ = false;
    if (r.verdict == fn::Verdict::Accept) {
      paste_ = Paste{};
      paste_.dir = Dir::PtoR;
      paste_.epoch = epoch;
      paste_.offerId = m.offerId;
      paste_.pasteOp = m.pasteOp;
      paste_.bulkGen = gens_.Next();
      for (size_t i = 0; i < m.items.size(); ++i) {
        sizes.push_back(m.items[i].size);
        fc::RemoteFileItem ri;
        ri.name = offer->items[i].name;
        ri.size = m.items[i].size;
        ri.mtime = m.items[i].mtime;
        ri.attributes = offer->items[i].attributes;
        d.items.push_back(ri);
      }
      paste_.sizes = sizes;
      r.bulkGen = paste_.bulkGen;
      id = FilePasteIdentity{r.epochTag, m.offerId, m.pasteOp, r.bulkGen};
      ++counters_.pastesPrepared;
      d.status = fc::Status::Ok;
      // Opened here, under mu_, together with the paste that owns it (r5): it is closed the same
      // way (EndPasteLocked), so no request of another session can open or close it in between.
      receiver_.Open(send_, bulk_stream_id(r.bulkGen, kFileBulkStreamHostToClient),
                     bulk_stream_id(r.bulkGen, kFileBulkStreamClientToHost), mtu_, id, sizes);
    } else {
      d.status = failed != fc::Status::Ok ? failed : fc::Status::Refused;
      if (r.verdict == fn::Verdict::Busy) ++counters_.pastesBusy;
      else ++counters_.pastesFailed;
    }
  }
  // The helper's descriptor: the confirmed sizes / times, or the reason the paste fails before a byte.
  if (r.verdict != fn::Verdict::UnknownId) (void)helper_.SendAs(epoch, fc::encode(d));
  std::ostringstream os;
  os << "prepare verdict=" << static_cast<int>(r.verdict) << " files=" << m.items.size();
  Log(os.str());
  return fn::body(r);
}

void HostFileCopyService::OnPasteEnd(uint64_t owner, uint64_t instance, const fc::PasteEnd& m) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!FrameAcceptedLocked(owner, instance)) return;
  if (paste_.dir == Dir::PtoR && paste_.offerId == m.offerId && paste_.pasteOp == m.pasteOp) {
    // A failed chunk check is the reason, whatever the consumer made of the failed Read.
    const fn::PasteEndReason failure = receiver_.failure();
    const fn::PasteEndReason reason = failure != fn::PasteEndReason::None ? failure : map_end(m.reason);
    (void)EndPasteLocked(reason == fn::PasteEndReason::Completed ? fn::PasteState::Ended : fn::PasteState::Failed, reason);
  } else if (haveBegun_ && begunOffer_ == m.offerId && begunOp_ == m.pasteOp) {
    haveBegun_ = false;  // it ended before the viewer prepared it
    lastEnded_ = Ended{m.offerId, m.pasteOp, fn::PasteState::Failed, map_end(m.reason)};
  }
}

bool HostFileCopyService::EndPasteLocked(fn::PasteState state, fn::PasteEndReason reason) {
  if (paste_.dir == Dir::None) return false;
  const bool rtop = paste_.dir == Dir::RtoP;
  lastEnded_ = Ended{paste_.offerId, paste_.pasteOp, state, reason};
  if (arbiter_) arbiter_->Release(paste_.pasteOp);
  const bool ok = state == fn::PasteState::Ended && reason == fn::PasteEndReason::Completed;
  std::ostringstream os;
  if (rtop) {
    sendAborting_ = true;  // a read in flight gives up; FinishSendClose (unlocked) closes the sender
    replyCv_.notify_all();
    if (ok) ++counters_.sendEnded;
    else ++counters_.sendFailed;
    if (reason == fn::PasteEndReason::Verification) ++counters_.sendVerificationEnds;
    counters_.lastSendEndReason = static_cast<uint8_t>(reason);
    os << "send ended state=" << static_cast<int>(state) << " reason=" << static_cast<int>(reason)
       << " bytes=" << (server_.GetCounters().bytesServed - sendBytesAtStart_);
  } else {
    receiver_.Close(fc::Status::Aborted);
    for (bool whole : receiver_.wholeFileVerified()) {
      if (whole) ++counters_.filesWholeVerified;
      else ++counters_.filesChunkVerified;
    }
    if (ok) ++counters_.pastesEnded;
    else ++counters_.pastesFailed;
    counters_.lastEndReason = static_cast<uint8_t>(reason);
    os << "paste ended state=" << static_cast<int>(state) << " reason=" << static_cast<int>(reason)
       << " bytes=" << receiver_.bytesDelivered() << " wholeVerified=" << counters_.filesWholeVerified;
  }
  Log(os.str());
  paste_ = Paste{};
  return rtop;
}

std::vector<uint8_t> HostFileCopyService::HandleEnd(const fn::End& m, uint64_t epoch) {
  fn::EndReply r;
  r.offerId = m.offerId;
  r.pasteOp = m.pasteOp;
  bool clear = false;
  bool closeSend = false;
  BulkKey key;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!Current(epoch)) return {};
    if (m.pasteOp == 0) {
      // Withdraw the viewer's offer: off this clipboard. A paste already running on it runs on.
      if (offer_.offerId == m.offerId) {
        retired_ = offer_;
        offer_ = PeerOffer{};
        clear = true;
      }
      r.state = fn::PasteState::Withdrawn;
    } else if (paste_.dir != Dir::None && paste_.offerId == m.offerId && paste_.pasteOp == m.pasteOp) {
      const fn::PasteEndReason reason = m.reason == fn::PasteEndReason::None ? fn::PasteEndReason::Cancelled : m.reason;
      r.state = reason == fn::PasteEndReason::Completed ? fn::PasteState::Ended : fn::PasteState::Failed;
      key = KeyOfPasteLocked();
      closeSend = EndPasteLocked(r.state, reason);
    } else if (lastEnded_.pasteOp != 0 && lastEnded_.offerId == m.offerId && lastEnded_.pasteOp == m.pasteOp) {
      r.state = lastEnded_.state;  // already over: how it really ended, not "cancelled" (r3 A-2)
    } else {
      r.state = fn::PasteState::None;
    }
  }
  if (epochProbe_) epochProbe_(epoch, 6);
  if (closeSend) FinishSendClose(key);  // only if this paste still holds the sender (r5)
  if (clear) {
    fc::ClearRemoteFiles c;
    c.offerId = m.offerId;
    (void)helper_.SendAs(epoch, fc::encode(c));
  }
  return fn::body(r);
}

std::vector<uint8_t> HostFileCopyService::HandleStatus(const fn::StatusQuery& m, uint64_t epoch) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!Current(epoch)) return {};
  fn::StatusReply r;
  r.offerId = m.offerId;
  r.pasteOp = m.pasteOp;
  if (paste_.dir != Dir::None && paste_.offerId == m.offerId && paste_.pasteOp == m.pasteOp) {
    r.state = fn::PasteState::Active;
    r.bytesDelivered = paste_.dir == Dir::PtoR ? receiver_.bytesDelivered() : server_.GetCounters().bytesServed;
  } else if (lastEnded_.pasteOp == m.pasteOp && lastEnded_.offerId == m.offerId && m.pasteOp != 0) {
    r.state = lastEnded_.state;
    r.reason = lastEnded_.reason;
  } else {
    r.state = fn::PasteState::None;
  }
  return fn::body(r);
}

// ------------------------------------------------------------------------------ transport / session

bool HostFileCopyService::OnDatagram(const void* data, size_t len) {
  if (!bulk_datagram_is_file(data, len)) return false;
  if (!file_copy_allowed()) return true;  // switched off: no new data is accepted (it is still ours to drop)
  // One paste at a time: the receiver's stream (P->R) or the sender's (R->P); a channel drops what
  // is not its open stream.
  if (receiver_.IsOpen()) (void)receiver_.OnDatagram(data, len);
  else (void)uplink_.OnDatagram(data, len);
  return true;
}

void HostFileCopyService::SetEnabled(bool on) {
  const bool was = enabled_.exchange(on);
  if (was && !on && running_.load()) {
    Log("switched off: the running paste ends, the offers go, the helper shuts down");
    Teardown(fn::PasteEndReason::Disabled, helper_.owner());
  }
}

void HostFileCopyService::OnSessionEnd(uint64_t newEpoch) {
  // Same lock as HandleControl's decision: the end of one session and the first request of the next
  // never interleave. The switch to a new epoch happens ONCE, by whichever of the two comes first
  // (r5): a hook that names the epoch already served, or an older one, is late -- the switch it
  // announces was made by the new session's first request, and the state is now that session's.
  // 0 is Stop: everything goes, no switch.
  std::lock_guard<std::mutex> session(sessionMu_);
  if (newEpoch != 0) {
    std::lock_guard<std::mutex> lock(mu_);
    if (newEpoch <= servedEpoch_) {
      Log("a late session-end hook: the switch was already made, the current session's state stays");
      return;
    }
  }
  if (newEpoch == 0) {
    Teardown(fn::PasteEndReason::Session, helper_.owner());
    return;
  }
  SwitchToLocked(newEpoch, true);
}

// Caller holds sessionMu_. The one place the state changes hands (r5): the helper channel's owner
// moves first (from here on an ended session's Ensure/Send do nothing), then the previous session's
// state, paste and helper go, then the epoch is recorded; waits of the ended session are woken so
// they see it at once.
void HostFileCopyService::SwitchToLocked(uint64_t epoch, bool endPrevious) {
  const uint64_t previous = helper_.owner();
  helper_.SetOwner(epoch);  // from here: no send, start, frame or "gone" of `previous` counts
  if (endPrevious) {
    Log("a new control session: the previous one's file-copy state ends");
    Teardown(fn::PasteEndReason::Session, previous);
  }
  std::lock_guard<std::mutex> lock(mu_);
  if (epoch > servedEpoch_) {
    servedEpoch_ = epoch;
    epochTag_ = (static_cast<uint64_t>(random32_) << 32) | (epoch & 0xFFFFFFFFull);
  }
  replyCv_.notify_all();
}

void HostFileCopyService::SetEpochProbeForTest(std::function<void(uint64_t, int)> probe) {
  std::lock_guard<std::mutex> session(sessionMu_);
  epochProbe_ = std::move(probe);
  Log("TEST PROBE epoch crossing installed");
}

void HostFileCopyService::Teardown(fn::PasteEndReason reason, uint64_t helperOwner) {
  bool shut = false;
  bool closeSend = false;
  BulkKey key;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (paste_.dir != Dir::None) {
      key = KeyOfPasteLocked();
      closeSend = EndPasteLocked(fn::PasteState::Failed, reason);
    }
    haveBegun_ = false;
    shut = offer_.offerId != 0 || retired_.offerId != 0;
    offer_ = PeerOffer{};
    retired_ = PeerOffer{};
    // A new session starts clean; a switch-off keeps how the paste ended, so the viewer can learn it.
    if (reason == fn::PasteEndReason::Session) lastEnded_ = Ended{};
    baselineSet_ = false;  // the next session takes its own
    // This PC's clipboard offer stays what the clipboard says; an identification in flight with the
    // helper that now goes is asked again by the next session.
    if (statId_ != 0) {
      statId_ = 0;
      statWanted_ = false;
    }
  }
  if (closeSend) FinishSendClose(key);
  // The helper is per session (plan §1): told Shutdown when it published something, then its pipe
  // is dropped -- it clears the clipboard and exits. Only THAT session's helper (r6): after the
  // switch the channel already belongs to the new session, whose helper is not touched.
  helper_.Retire(helperOwner, shut);
}

HostFileCopyService::Counters HostFileCopyService::GetCounters() const {
  Counters c;
  {
    std::lock_guard<std::mutex> lock(mu_);
    c = counters_;
  }
  const FilePullReceiver::Counters rc = receiver_.GetCounters();
  c.readsRequested = rc.readsRequested;
  c.readsServed = rc.readsServed;
  c.readsFailed = rc.readsFailed;
  c.chunksVerified = rc.chunksVerified;
  c.chunksRejected = rc.chunksRejected;
  c.bytesDelivered = rc.bytesDelivered;
  const FileChunkServer::Counters sc = server_.GetCounters();
  c.chunksServed = sc.chunksServed;
  c.bytesServed = sc.bytesServed;
  c.pullsRefused = sc.pullsRefused;
  c.helperLaunches = helper_.launches();
  c.helperLaunchFailures = helper_.launchFailures();
  c.sendOpen = server_.active();
  c.sendPasteOp = server_.identity().pasteOp;
  c.sendEpochTag = server_.identity().epochTag;
  return c;
}

}  // namespace remote60::native_poc

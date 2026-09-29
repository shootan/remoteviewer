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
  helper_.Configure(hc, [this](const fc::PipeFrame& f) { OnHelperFrame(f); }, [this] { OnHelperGone(); });
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

void HostFileCopyService::OnHelperFrame(const fc::PipeFrame& f) {
  switch (f.type) {
    case fc::PipeMsg::PublishResult: {
      fc::PublishResult m;
      if (!fc::decode(f, &m)) break;
      std::lock_guard<std::mutex> lock(mu_);
      publishResult_ = m;
      publishAnswered_ = true;
      replyCv_.notify_all();
      break;
    }
    case fc::PipeMsg::PasteBegin: {
      fc::PasteBegin m;
      if (fc::decode(f, &m)) OnPasteBegin(m);
      break;
    }
    case fc::PipeMsg::ReadRequest: {
      fc::ReadRequest m;
      if (!fc::decode(f, &m)) break;
      if (!file_copy_allowed()) {  // switched off: nothing more reaches the helper
        fc::ReadData d;
        d.offerId = m.offerId;
        d.pasteOp = m.pasteOp;
        d.fileIndex = m.fileIndex;
        d.offset = m.offset;
        d.status = fc::Status::Aborted;
        (void)helper_.Send(fc::encode(d));
        break;
      }
      receiver_.Submit(m);
      break;
    }
    case fc::PipeMsg::PasteEnd: {
      fc::PasteEnd m;
      if (fc::decode(f, &m)) OnPasteEnd(m);
      break;
    }
    case fc::PipeMsg::Stats: {
      fc::Stats m;
      if (fc::decode(f, &m)) OnStats(m);
      break;
    }
    case fc::PipeMsg::PinResult: {
      fc::PinResult m;
      if (!fc::decode(f, &m)) break;
      std::lock_guard<std::mutex> lock(mu_);
      pinResult_ = std::move(m);
      pinAnswered_ = true;
      replyCv_.notify_all();
      break;
    }
    case fc::PipeMsg::LocalData: {
      fc::LocalData m;
      if (!fc::decode(f, &m)) break;
      std::lock_guard<std::mutex> lock(mu_);
      localData_ = std::move(m);
      localAnswered_ = true;
      replyCv_.notify_all();
      break;
    }
    default:
      break;
  }
}

void HostFileCopyService::OnHelperGone() {
  // Whatever the helper was doing is over: its clipboard object, its reads, its pins.
  bool closeSend = false;
  uint64_t pin = 0;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (paste_.dir != Dir::None) {
      pin = paste_.pasteOp;
      closeSend = EndPasteLocked(fn::PasteState::Failed, fn::PasteEndReason::Session);
    }
    haveBegun_ = false;
    if (statId_ != 0) {
      statId_ = 0;
      statWanted_ = clipSeq_ != hostOffer_.revision && !clipPaths_.empty();
    }
    replyCv_.notify_all();
  }
  if (closeSend) FinishSendClose(pin);
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

void HostFileCopyService::OnStats(const fc::Stats& m) {
  std::lock_guard<std::mutex> lock(mu_);
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

std::vector<uint8_t> HostFileCopyService::HandleOfferQuery(const fn::OfferQuery& m) {
  std::lock_guard<std::mutex> lock(mu_);
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

std::vector<uint8_t> HostFileCopyService::HandlePrepareRtoP(const fn::Prepare& m) {
  fn::PrepareReply r;
  r.direction = fn::Direction::RtoP;
  r.offerId = m.offerId;
  r.pasteOp = m.pasteOp;
  std::vector<HostFile> files;
  std::vector<fn::OfferItem> items;
  {
    std::lock_guard<std::mutex> lock(mu_);
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
  // Pinned by the user's helper: the same FileId, a writer refused, the size / time of the offer.
  std::string why;
  bool up = helper_.Ensure(&why);
  if (up) {
    fc::Pin pin;
    pin.pinId = m.pasteOp;
    pin.leaseMs = kHostFilePinLeaseMs;
    for (const HostFile& f : files) pin.entries.push_back({f.path, f.id, f.size, f.mtime});
    up = helper_.Send(fc::encode(pin));
    if (!up) why = "the helper pipe failed";
  }
  std::unique_lock<std::mutex> lock(mu_);
  if (up) {
    replyCv_.wait_for(lock, std::chrono::milliseconds(config_.pinWaitMs),
                      [&] { return (pinAnswered_ && pinResult_.pinId == m.pasteOp) || paste_.pasteOp != m.pasteOp; });
  }
  const bool answered = up && pinAnswered_ && pinResult_.pinId == m.pasteOp && paste_.pasteOp == m.pasteOp;
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
    if (paste_.pasteOp == m.pasteOp) paste_ = Paste{};
    if (arbiter_) arbiter_->Release(m.pasteOp);
    ++counters_.sendRefused;
    counters_.lastSendVerdict = static_cast<uint8_t>(r.verdict);
    lock.unlock();
    if (up) (void)helper_.Send(fc::encode(fc::Unpin{m.pasteOp}));
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
  ++counters_.sendPrepared;
  counters_.lastSendVerdict = 0;
  const FilePasteIdentity id{r.epochTag, m.offerId, m.pasteOp, r.bulkGen};
  lock.unlock();
  const uint64_t pinId = m.pasteOp;
  server_.Begin(id, sizes, [this, pinId](uint32_t index, uint64_t offset, uint32_t length, std::vector<uint8_t>* out) {
    return ReadLocal(pinId, index, offset, length, out);
  });
  uplink_.ResetRateCounters();
  uplink_.Open(bulk_stream_id(r.bulkGen, kFileBulkStreamHostToClient), bulk_stream_id(r.bulkGen, kFileBulkStreamClientToHost),
               &server_);
  std::ostringstream os;
  os << "send prepared files=" << files.size() << " gen=" << r.bulkGen;
  Log(os.str());
  return fn::body(r);
}

fc::Status HostFileCopyService::ReadLocal(uint64_t pinId, uint32_t index, uint64_t offset, uint32_t length,
                                          std::vector<uint8_t>* out) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (sendAborting_ || !file_copy_allowed()) return fc::Status::Aborted;
    localAnswered_ = false;
  }
  if (!helper_.Send(fc::encode(fc::ReadLocal{pinId, index, offset, length}))) {
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

void HostFileCopyService::FinishSendClose(uint64_t pinId) {
  server_.End();
  uplink_.Close();
  if (pinId != 0) (void)helper_.Send(fc::encode(fc::Unpin{pinId}));
}

// ------------------------------------------------------------------------------ control

bool HostFileCopyService::HandleControl(uint16_t type, const std::vector<uint8_t>& body, uint64_t servedEpoch,
                                        uint16_t* replyType, std::vector<uint8_t>* reply) {
  bool newSession = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    newSession = servedEpoch_ != 0 && servedEpoch != servedEpoch_;
  }
  // A request of another control session reached the handlers before its session-end hook: nothing of
  // the old one is carried into it -- not its offers, its paste or its begun state (r2 ④).
  if (newSession) {
    Log("a new control session: the previous one's file-copy state ends");
    OnSessionEnd(servedEpoch);
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    servedEpoch_ = servedEpoch;
    epochTag_ = (static_cast<uint64_t>(random32_) << 32) | (servedEpoch & 0xFFFFFFFFull);
  }
  switch (static_cast<fn::FileMsg>(type)) {
    case fn::FileMsg::Offer: {
      fn::Offer m;
      if (!fn::parse(body, &m)) return false;
      *replyType = static_cast<uint16_t>(fn::FileMsg::OfferReply);
      *reply = HandleOffer(m);
      return true;
    }
    case fn::FileMsg::OfferQuery: {
      fn::OfferQuery m;
      if (!fn::parse(body, &m)) return false;
      *replyType = static_cast<uint16_t>(fn::FileMsg::OfferQueryReply);
      *reply = HandleOfferQuery(m);
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
      *reply = m.direction == fn::Direction::RtoP ? HandlePrepareRtoP(m) : HandlePrepare(m);
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
  if (!helper_.Ensure(&why)) return refuse(fn::Verdict::HelperUnavailable, why);
  fc::PublishRemoteFiles pub;
  pub.offerId = m.offerId;
  for (const fn::OfferItem& it : m.items) pub.items.push_back({it.name, it.size, it.mtime, it.attributes});
  {
    std::lock_guard<std::mutex> lock(mu_);
    publishAnswered_ = false;
  }
  if (!helper_.Send(fc::encode(pub))) return refuse(fn::Verdict::HelperUnavailable, "the helper pipe failed");
  std::unique_lock<std::mutex> lock(mu_);
  replyCv_.wait_for(lock, std::chrono::milliseconds(config_.publishWaitMs),
                    [&] { return publishAnswered_ && publishResult_.offerId == m.offerId; });
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

void HostFileCopyService::OnPasteBegin(const fc::PasteBegin& m) {
  bool refuse = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
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

std::vector<uint8_t> HostFileCopyService::HandlePrepare(const fn::Prepare& m) {
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
    } else {
      d.status = failed != fc::Status::Ok ? failed : fc::Status::Refused;
      if (r.verdict == fn::Verdict::Busy) ++counters_.pastesBusy;
      else ++counters_.pastesFailed;
    }
  }
  if (r.verdict == fn::Verdict::Accept) {
    receiver_.Open(send_, bulk_stream_id(r.bulkGen, kFileBulkStreamHostToClient),
                   bulk_stream_id(r.bulkGen, kFileBulkStreamClientToHost), mtu_, id, sizes);
  }
  // The helper's descriptor: the confirmed sizes / times, or the reason the paste fails before a byte.
  if (r.verdict != fn::Verdict::UnknownId) (void)helper_.Send(fc::encode(d));
  std::ostringstream os;
  os << "prepare verdict=" << static_cast<int>(r.verdict) << " files=" << m.items.size();
  Log(os.str());
  return fn::body(r);
}

void HostFileCopyService::OnPasteEnd(const fc::PasteEnd& m) {
  std::lock_guard<std::mutex> lock(mu_);
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

std::vector<uint8_t> HostFileCopyService::HandleEnd(const fn::End& m) {
  fn::EndReply r;
  r.offerId = m.offerId;
  r.pasteOp = m.pasteOp;
  bool clear = false;
  bool closeSend = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
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
      closeSend = EndPasteLocked(r.state, reason);
    } else {
      r.state = fn::PasteState::None;
    }
  }
  if (closeSend) FinishSendClose(m.pasteOp);
  if (clear) {
    fc::ClearRemoteFiles c;
    c.offerId = m.offerId;
    (void)helper_.Send(fc::encode(c));
  }
  return fn::body(r);
}

std::vector<uint8_t> HostFileCopyService::HandleStatus(const fn::StatusQuery& m) {
  std::lock_guard<std::mutex> lock(mu_);
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
    Teardown(fn::PasteEndReason::Disabled);
  }
}

void HostFileCopyService::OnSessionEnd(uint64_t /*newEpoch*/) { Teardown(fn::PasteEndReason::Session); }

void HostFileCopyService::Teardown(fn::PasteEndReason reason) {
  bool shut = false;
  bool closeSend = false;
  uint64_t pin = 0;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (paste_.dir != Dir::None) {
      pin = paste_.pasteOp;
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
  if (closeSend) FinishSendClose(pin);
  if (shut) {
    // The helper is per session (plan §1): it clears the clipboard and exits.
    (void)helper_.Send(fc::encode_shutdown());
  }
  helper_.Disconnect();
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
  return c;
}

}  // namespace remote60::native_poc

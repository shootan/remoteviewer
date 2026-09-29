// See file_copy_client.hpp.

#include "file_copy_client.hpp"

#include <bcrypt.h>

#include <cstring>
#include <iostream>
#include <sstream>

#include "clip_image_core.hpp"
#include "file_copy_net_rules.hpp"

namespace remote60::native_poc {

namespace fc = remote60::native_poc::file_copy;
namespace fn = remote60::native_poc::file_copy::net;

namespace {

uint64_t random_id() {
  uint64_t v = 0;
  while (v == 0) {
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&v), sizeof(v), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
      v = BulkPacer::NowUs() * 6364136223846793005ull + GetCurrentProcessId();
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

}  // namespace

void FileCopyClient::Log(const std::string& line) {
  if (log_) log_(line);
  else std::cout << "[native-video-client][file-copy] " << line << "\n";
}

void FileCopyClient::Start(SendFn send, PingRttFn pingRtt, YieldFn yield, uint32_t mtuBytes, BulkRateConfig rate,
                           BulkArbiter* arbiter, LogFn log) {
  if (running_.load()) return;
  send_ = std::move(send);
  pingRtt_ = std::move(pingRtt);
  yield_ = std::move(yield);
  log_ = std::move(log);
  mtu_ = mtuBytes;
  arbiter_ = arbiter;
  uplink_.Configure(send_, pingRtt_, yield_, mtuBytes, rate, fn::kMaxFileChunkBytes);
  if (R2PEnabled()) {
    FileHelperChannel::Config hc;
    hc.launcher = launcher_;
    helper_.Configure(hc, [this](const fc::PipeFrame& f) { OnHelperFrame(f); }, [this] { OnHelperGone(); });
    receiver_.Start([this](const fc::ReadData& d) { (void)helper_.Send(fc::encode(d)); });
    {
      std::lock_guard<std::mutex> w(workMu_);
      workerRun_ = true;
    }
    worker_ = std::thread([this] { WorkerLoop(); });
  }
  running_.store(true);
}

void FileCopyClient::Stop() {
  if (!running_.exchange(false)) return;
  EndSession();
  {
    std::lock_guard<std::mutex> w(workMu_);
    workerRun_ = false;
  }
  workCv_.notify_all();
  if (worker_.joinable()) worker_.join();
  receiver_.Stop();
  helper_.Stop();
}

void FileCopyClient::Post(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> w(workMu_);
    if (!workerRun_) return;
    work_.push_back(std::move(task));
  }
  workCv_.notify_all();
}

void FileCopyClient::WorkerLoop() {
  std::unique_lock<std::mutex> w(workMu_);
  while (true) {
    workCv_.wait(w, [&] { return !workerRun_ || !work_.empty(); });
    if (!workerRun_) break;
    auto task = std::move(work_.front());
    work_.pop_front();
    w.unlock();
    task();
    w.lock();
  }
}

// ------------------------------------------------------------------------------ P->R: this PC's copy

void FileCopyClient::SubmitLocalFiles(const std::vector<std::wstring>& paths, uint64_t revision) {
  // Identified here, as the user, without holding anything: a writer elsewhere is no reason not to
  // OFFER a file -- the pin at paste time decides.
  std::vector<LocalFile> files;
  std::vector<fn::OfferItem> items;
  uint64_t excluded = 0;
  for (const std::wstring& path : paths) {
    const fc::StatEntry e = fc::stat_source_file(path);
    if (e.status != fc::Status::Ok) {
      ++excluded;
      continue;
    }
    LocalFile f;
    f.path = path;
    f.id = e.id;
    f.size = e.size;
    f.mtime = e.mtime;
    f.attributes = e.attributes;
    fn::OfferItem it;
    it.index = static_cast<uint32_t>(items.size());  // = the pin order
    it.name = e.name;
    it.size = e.size;
    it.mtime = e.mtime;
    it.attributes = e.attributes;
    files.push_back(std::move(f));
    items.push_back(std::move(it));
  }
  std::string why;
  const fn::Verdict v = items.empty() ? fn::Verdict::BadRequest : fn::check_offer_items(items, &why);
  std::lock_guard<std::mutex> lock(mu_);
  ++counters_.submitted;
  counters_.filesExcluded += excluded;
  if (offer_.live || offer_.pending) retired_ = offer_;  // a paste may already have begun on it
  offer_ = OfferState{};
  if (v != fn::Verdict::Accept) {
    counters_.lastVerdict = static_cast<uint8_t>(v);
    std::ostringstream os;
    os << "copy not offered: files=" << items.size() << " excluded=" << excluded << " (" << why << ")";
    Log(os.str());
    return;
  }
  offer_.pending = true;
  offer_.offerId = random_id();
  offer_.revision = revision;
  offer_.files = std::move(files);
  offer_.items = std::move(items);
  counters_.filesOffered += offer_.items.size();
  std::ostringstream os;
  os << "copy of " << offer_.items.size() << " file(s) to offer, excluded=" << excluded;
  Log(os.str());
}

void FileCopyClient::ClearLocalOffer() {
  std::lock_guard<std::mutex> lock(mu_);
  if (offer_.live) withdrawOfferId_ = offer_.offerId;
  if (offer_.live || offer_.pending) retired_ = offer_;
  offer_ = OfferState{};
}

bool FileCopyClient::Exchange(ControlLink& link, fn::FileMsg type, const std::vector<uint8_t>& body, fn::FileMsg replyType,
                              std::vector<uint8_t>* reply) {
  std::vector<uint8_t> msg;
  uint32_t seq = 0;
  {
    std::lock_guard<std::mutex> lock(mu_);
    seq = ++nextSeq_;
  }
  if (!fn::frame_control(type, seq, body, &msg)) return false;
  if (!link.Write(msg.data(), msg.size()) || !link.EndMessage()) return false;
  fn::FileControlHeader h{};
  if (!link.Read(&h, sizeof(h))) return false;
  if (h.header.magic != kMagic || h.header.type != static_cast<uint16_t>(replyType) ||
      h.header.size != sizeof(fn::FileControlHeader) || h.seq != seq || h.payloadBytes > fn::kMaxControlPayload) {
    return false;  // the stream is desynchronised, as for every other exchange on the link
  }
  reply->resize(h.payloadBytes);
  return h.payloadBytes == 0 || link.Read(reply->data(), reply->size());
}

int FileCopyClient::Pump(ControlLink& link) {
  if (!running_.load() || !Usable()) return 0;
  const uint64_t now = BulkPacer::NowUs();
  // One exchange per turn, chosen in this order: what the consumer is waiting on first (an end,
  // a prepare), then this PC's offer, then the two 700 ms questions. Only the chosen one is taken.
  enum class Act { None, End, Prepare, Withdraw, Offer, PasteQuery, OfferQuery } act = Act::None;
  PasteKey key;
  uint64_t withdraw = 0, queryOffer = 0;
  bool queryForPaste = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!endQueue_.empty()) {
      act = Act::End;
      key = endQueue_.front();
      endQueue_.pop_front();
    } else if (!prepareQueue_.empty()) {
      act = Act::Prepare;
      key = prepareQueue_.front();
      prepareQueue_.pop_front();
    } else if (withdrawOfferId_ != 0) {
      act = Act::Withdraw;
      withdraw = withdrawOfferId_;
    } else if (offer_.pending) {
      act = Act::Offer;
    } else if (paste_.active && now >= paste_.nextQueryUs) {
      act = Act::PasteQuery;
      queryOffer = paste_.offerId;
      queryForPaste = true;
      paste_.nextQueryUs = now + kFilePasteQueryIntervalUs;
    } else if (!paste_.active && offer_.live && now >= offer_.nextQueryUs) {
      act = Act::PasteQuery;
      queryOffer = offer_.offerId;
      offer_.nextQueryUs = now + kFilePasteQueryIntervalUs;
    } else if (R2PEnabled() && now >= nextOfferQueryUs_) {
      act = Act::OfferQuery;
      nextOfferQueryUs_ = now + kFileOfferQueryIntervalUs;
    }
  }
  int result = 0;
  switch (act) {
    case Act::End:
      result = SendReceiveEnd(link, key);
      break;
    case Act::Prepare:
      result = PrepareReceive(link, key);
      break;
    case Act::Withdraw: {
      fn::End e{withdraw, 0, fn::PasteEndReason::None};
      std::vector<uint8_t> reply;
      if (!Exchange(link, fn::FileMsg::End, fn::body(e), fn::FileMsg::EndReply, &reply)) return -1;
      std::lock_guard<std::mutex> lock(mu_);
      if (withdrawOfferId_ == withdraw) withdrawOfferId_ = 0;
      result = 1;
      break;
    }
    case Act::Offer:
      result = PumpOffer(link);
      break;
    case Act::PasteQuery:
      result = PumpPasteQuery(link, queryOffer, queryForPaste);
      break;
    case Act::OfferQuery:
      result = PumpOfferQuery(link);
      break;
    case Act::None:
      break;
  }
  bool close = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    close = closeUplinkPending_;
    closeUplinkPending_ = false;
  }
  if (close) {
    server_.End();
    uplink_.Close();
  }
  return result;
}

int FileCopyClient::PumpOffer(ControlLink& link) {
  fn::Offer o;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!offer_.pending) return 0;
    o.offerId = offer_.offerId;
    o.revision = offer_.revision;
    o.items = offer_.items;
  }
  std::vector<uint8_t> raw;
  if (!Exchange(link, fn::FileMsg::Offer, fn::body(o), fn::FileMsg::OfferReply, &raw)) return -1;
  fn::OfferReply r;
  if (!fn::parse(raw, &r)) return -1;
  std::lock_guard<std::mutex> lock(mu_);
  ++counters_.offersSent;
  counters_.lastVerdict = static_cast<uint8_t>(r.verdict);
  if (!offer_.pending || offer_.offerId != o.offerId) return 1;  // replaced meanwhile: the newer goes next
  offer_.pending = false;
  if (r.verdict == fn::Verdict::Accept && r.offerId == o.offerId) {
    offer_.live = true;
    offer_.nextQueryUs = BulkPacer::NowUs() + kFilePasteQueryIntervalUs;
    ++counters_.offersAccepted;
    std::ostringstream os;
    os << "offer accepted files=" << o.items.size();
    Log(os.str());
  } else {
    ++counters_.offersRefused;
    std::ostringstream os;
    os << "offer refused verdict=" << static_cast<int>(r.verdict);
    Log(os.str());
    offer_ = OfferState{};
  }
  return 1;
}

int FileCopyClient::PumpPasteQuery(ControlLink& link, uint64_t offerId, bool forActivePaste) {
  fn::PasteQuery q{offerId};
  std::vector<uint8_t> raw;
  if (!Exchange(link, fn::FileMsg::PasteQuery, fn::body(q), fn::FileMsg::PasteQueryReply, &raw)) return -1;
  fn::PasteQueryReply r;
  if (!fn::parse(raw, &r)) return -1;
  bool prepare = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    counters_.lastPasteState = static_cast<uint8_t>(r.state);
    if (paste_.active && r.pasteOp == paste_.pasteOp && r.offerId == paste_.offerId) {
      if (r.state == fn::PasteState::Ended || r.state == fn::PasteState::Failed || r.state == fn::PasteState::Withdrawn) {
        EndPaste(r.state, r.reason);
      }
      return 1;
    }
    if (r.state == fn::PasteState::Begun && !paste_.active && r.pasteOp != 0 &&
        (r.offerId == offer_.offerId || r.offerId == retired_.offerId)) {
      prepare = true;
    } else if (r.state == fn::PasteState::Withdrawn && !forActivePaste && r.offerId == offer_.offerId) {
      offer_.live = false;  // the host dropped it (another copy on the remote PC, the switch, ...)
    }
  }
  if (prepare) return PreparePaste(link, r.offerId, r.pasteOp);
  return 1;
}

int FileCopyClient::PreparePaste(ControlLink& link, uint64_t offerId, uint64_t pasteOp) {
  std::vector<LocalFile> files;
  {
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.pastesBegun;
    files = offerId == offer_.offerId ? offer_.files : retired_.files;
  }
  fn::Prepare p;
  p.direction = fn::Direction::PtoR;
  p.offerId = offerId;
  p.pasteOp = pasteOp;
  const bool haveBulk = !arbiter_ || arbiter_->TryAcquire(BulkUse::File, pasteOp);
  bool allOk = haveBulk;
  if (haveBulk) {
    // Pinned now: the content at the moment the paste started, a writer refused, a replaced file
    // (another FileId) refused, a size / time that moved refused -- each by the user's own opens.
    std::vector<fc::PinRequestEntry> entries;
    for (const LocalFile& f : files) entries.push_back({std::u16string(f.path.begin(), f.path.end()), f.id, f.size, f.mtime});
    std::vector<fc::PinResultEntry> results;
    {
      std::lock_guard<std::mutex> pin(pinMu_);
      pins_.Pin(pasteOp, kFilePinLeaseMs, entries, &results);
    }
    for (size_t i = 0; i < files.size(); ++i) {
      fn::PreparedItem it;
      it.index = static_cast<uint32_t>(i);
      const fc::PinResultEntry& res = i < results.size() ? results[i] : fc::PinResultEntry{fc::Status::ReadError};
      it.status = static_cast<uint16_t>(res.status);
      it.size = res.size;
      it.mtime = res.mtime;
      it.attributes = files[i].attributes;
      allOk = allOk && res.status == fc::Status::Ok;
      p.items.push_back(it);
    }
  } else {
    // The session's bulk is taken (an image, or a paste the other way): refused (Busy) -- step 3
    // pre-empts instead.
    for (size_t i = 0; i < files.size(); ++i) {
      fn::PreparedItem it;
      it.index = static_cast<uint32_t>(i);
      it.status = static_cast<uint16_t>(fc::Status::Refused);
      p.items.push_back(it);
    }
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.pastesBusy;
  }
  std::vector<uint8_t> raw;
  const bool exchanged = Exchange(link, fn::FileMsg::Prepare, fn::body(p), fn::FileMsg::PrepareReply, &raw);
  fn::PrepareReply r;
  const bool parsed = exchanged && fn::parse(raw, &r);
  std::vector<uint64_t> sizes;
  {
    std::lock_guard<std::mutex> lock(mu_);
    const bool go = parsed && allOk && r.verdict == fn::Verdict::Accept && r.pasteOp == pasteOp && r.offerId == offerId;
    if (!go) {
      {
        std::lock_guard<std::mutex> pin(pinMu_);
        pins_.Unpin(pasteOp);
      }
      if (haveBulk && arbiter_) arbiter_->Release(pasteOp);
      ++counters_.pastesFailed;
      std::ostringstream os;
      os << "paste not prepared: pinned=" << (allOk ? 1 : 0) << " verdict=" << (parsed ? static_cast<int>(r.verdict) : -1);
      Log(os.str());
      return exchanged ? 1 : -1;
    }
    paste_ = PasteRun{};
    paste_.active = true;
    paste_.offerId = offerId;
    paste_.pasteOp = pasteOp;
    paste_.nextQueryUs = BulkPacer::NowUs() + kFilePasteQueryIntervalUs;
    for (const fn::PreparedItem& it : p.items) sizes.push_back(it.size);
    ++counters_.pastesPrepared;
  }
  server_.Begin(FilePasteIdentity{r.epochTag, offerId, pasteOp, r.bulkGen}, sizes,
                [this, pasteOp](uint32_t index, uint64_t offset, uint32_t length, std::vector<uint8_t>* out) {
                  std::lock_guard<std::mutex> pin(pinMu_);
                  return pins_.Read(pasteOp, index, offset, length, out);
                });
  uplink_.ResetRateCounters();
  uplink_.Open(bulk_stream_id(r.bulkGen, kFileBulkStreamClientToHost), bulk_stream_id(r.bulkGen, kFileBulkStreamHostToClient),
               &server_);
  std::ostringstream os;
  os << "paste prepared files=" << p.items.size() << " gen=" << r.bulkGen;
  Log(os.str());
  return 1;
}

void FileCopyClient::EndPaste(fn::PasteState state, fn::PasteEndReason reason) {
  if (!paste_.active) return;
  const uint64_t op = paste_.pasteOp;
  paste_.active = false;
  closeUplinkPending_ = true;
  {
    std::lock_guard<std::mutex> pin(pinMu_);
    pins_.Unpin(op);
  }
  if (arbiter_) arbiter_->Release(op);
  counters_.lastEndReason = static_cast<uint8_t>(reason);
  if (state == fn::PasteState::Ended && reason == fn::PasteEndReason::Completed) ++counters_.pastesEnded;
  else ++counters_.pastesFailed;
  std::ostringstream os;
  os << "paste ended state=" << static_cast<int>(state) << " reason=" << static_cast<int>(reason);
  Log(os.str());
}

// ------------------------------------------------------------------------------ R->P: the remote PC's copy

int FileCopyClient::PumpOfferQuery(ControlLink& link) {
  fn::OfferQuery q;
  {
    std::lock_guard<std::mutex> lock(mu_);
    q.knownRevision = remote_.revision;
    ++counters_.offerQueries;
  }
  std::vector<uint8_t> raw;
  if (!Exchange(link, fn::FileMsg::OfferQuery, fn::body(q), fn::FileMsg::OfferQueryReply, &raw)) return -1;
  fn::OfferQueryReply r;
  if (!fn::parse(raw, &r)) return -1;
  if (r.unchanged) {
    std::lock_guard<std::mutex> lock(mu_);
    remoteBaselineTaken_ = true;
    return 1;
  }
  bool publish = false, clear = false;
  uint64_t clearId = 0;
  fc::PublishRemoteFiles pub;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (remote_.offerId != 0) remoteRetired_ = remote_;  // a paste may have begun on it just before
    remote_ = RemoteOffer{};
    remote_.revision = r.revision;
    if (!remoteBaselineTaken_) {
      remoteBaselineTaken_ = true;  // before this connection: recorded, not published
      return 1;
    }
    std::string why;
    bool ok = r.offerId != 0 && !r.items.empty();
    if (ok) {
      // Checked here too: the host is not trusted to have done it (names reach this clipboard).
      ok = fn::check_offer_items(r.items, &why) == fn::Verdict::Accept;
      for (size_t i = 0; ok && i < r.items.size(); ++i) ok = r.items[i].index == i;
      if (!ok) {
        ++counters_.remoteOffersRefused;
        Log("remote copy not published (" + (why.empty() ? std::string("indices") : why) + ")");
      }
    }
    if (ok) {
      remote_.offerId = r.offerId;
      remote_.items = r.items;
      ++counters_.remoteOffersSeen;
      publish = true;
      pub.offerId = r.offerId;
      for (const fn::OfferItem& it : r.items) pub.items.push_back({it.name, it.size, it.mtime, it.attributes});
    } else if (publishedOfferId_ != 0) {
      // The remote clipboard no longer names files: ours comes off (if it is still ours).
      clear = true;
      clearId = publishedOfferId_;
      publishedOfferId_ = 0;
    }
  }
  if (publish) {
    const size_t files = pub.items.size();
    Post([this, pub, files] {
      std::string why;
      if (!helper_.Ensure(&why) || !helper_.Send(fc::encode(pub))) {
        Log("remote copy not published: helper unavailable (" + why + ")");
        return;
      }
      std::ostringstream os;
      os << "remote copy of " << files << " file(s) to publish";
      Log(os.str());
    });
  } else if (clear) {
    Post([this, clearId] {
      fc::ClearRemoteFiles c;
      c.offerId = clearId;
      if (helper_.Running()) (void)helper_.Send(fc::encode(c));
    });
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.remoteCleared;
  }
  return 1;
}

void FileCopyClient::OnHelperFrame(const fc::PipeFrame& f) {
  switch (f.type) {
    case fc::PipeMsg::PublishResult: {
      fc::PublishResult m;
      if (!fc::decode(f, &m)) break;
      std::lock_guard<std::mutex> lock(mu_);
      if (m.status == fc::Status::Ok && m.offerId == remote_.offerId) {
        publishedOfferId_ = m.offerId;
        ++counters_.remotePublished;
        std::ostringstream os;
        os << "remote copy published files=" << m.count;
        Log(os.str());
      }
      break;
    }
    case fc::PipeMsg::PasteBegin: {
      fc::PasteBegin m;
      if (fc::decode(f, &m)) OnHelperPasteBegin(m);
      break;
    }
    case fc::PipeMsg::ReadRequest: {
      fc::ReadRequest m;
      if (fc::decode(f, &m)) receiver_.Submit(m);
      break;
    }
    case fc::PipeMsg::PasteEnd: {
      fc::PasteEnd m;
      if (fc::decode(f, &m)) OnHelperPasteEnd(m);
      break;
    }
    default:
      break;
  }
}

void FileCopyClient::RefuseDescriptor(uint64_t offerId, uint64_t pasteOp, fc::Status why) {
  fc::PasteDescriptor d;
  d.offerId = offerId;
  d.pasteOp = pasteOp;
  d.status = why;
  (void)helper_.Send(fc::encode(d));
}

void FileCopyClient::OnHelperPasteBegin(const fc::PasteBegin& m) {
  bool refuse = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.recvBegun;
    const bool known = m.offerId != 0 && (m.offerId == remote_.offerId || m.offerId == remoteRetired_.offerId);
    // One paste at a time here too; a second one fails before any byte, never replaces the first.
    refuse = !known || recv_.active || !prepareQueue_.empty() || !Usable();
    if (!refuse) prepareQueue_.push_back(PasteKey{m.offerId, m.pasteOp});
    else ++counters_.recvBusy;
  }
  if (refuse) {
    RefuseDescriptor(m.offerId, m.pasteOp, fc::Status::Refused);
    Log("remote paste refused: another paste runs, or the offer is gone");
  }
}

int FileCopyClient::PrepareReceive(ControlLink& link, const PasteKey& k) {
  std::vector<fn::OfferItem> items;
  {
    std::lock_guard<std::mutex> lock(mu_);
    items = k.offerId == remote_.offerId ? remote_.items : remoteRetired_.items;
    preparingOp_ = k.pasteOp;
  }
  struct Done {
    FileCopyClient* self;
    ~Done() {
      std::lock_guard<std::mutex> lock(self->mu_);
      self->preparingOp_ = 0;
    }
  } done{this};
  if (arbiter_ && !arbiter_->TryAcquire(BulkUse::File, k.pasteOp)) {
    // The session's bulk is taken (an image, or a paste the other way): refused before any byte.
    RefuseDescriptor(k.offerId, k.pasteOp, fc::Status::Refused);
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.recvBusy;
    counters_.lastRecvVerdict = static_cast<uint8_t>(fn::Verdict::Busy);
    Log("remote paste refused: the session's bulk is busy");
    return 0;
  }
  fn::Prepare p;
  p.direction = fn::Direction::RtoP;
  p.offerId = k.offerId;
  p.pasteOp = k.pasteOp;
  std::vector<uint8_t> raw;
  const bool exchanged = Exchange(link, fn::FileMsg::Prepare, fn::body(p), fn::FileMsg::PrepareReply, &raw);
  fn::PrepareReply r;
  const bool parsed = exchanged && fn::parse(raw, &r);
  fc::Status why = fc::Status::Refused;
  bool go = parsed && r.verdict == fn::Verdict::Accept && r.offerId == k.offerId && r.pasteOp == k.pasteOp &&
            r.direction == fn::Direction::RtoP && r.items.size() == items.size() && !items.empty();
  std::vector<uint64_t> sizes;
  fc::PasteDescriptor d;
  d.offerId = k.offerId;
  d.pasteOp = k.pasteOp;
  if (parsed && r.verdict == fn::Verdict::Busy) why = fc::Status::Refused;
  for (size_t i = 0; go && i < r.items.size(); ++i) {
    const fn::PreparedItem& it = r.items[i];
    if (it.index != i) {
      go = false;
      why = fc::Status::BadRequest;
    } else if (it.status != static_cast<uint16_t>(fc::Status::Ok)) {
      go = false;
      why = static_cast<fc::Status>(it.status);
    } else {
      sizes.push_back(it.size);
      d.items.push_back({items[i].name, it.size, it.mtime, items[i].attributes});
    }
  }
  if (!go && parsed) {
    for (const fn::PreparedItem& it : r.items) {
      if (it.status != static_cast<uint16_t>(fc::Status::Ok)) {
        why = static_cast<fc::Status>(it.status);  // the remote pin's own reason (in use, replaced, ...)
        break;
      }
    }
  }
  bool endedMeanwhile = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    counters_.lastRecvVerdict = parsed ? static_cast<uint8_t>(r.verdict) : 0xFF;
    // The consumer may have given up while the host pinned (the helper said PasteEnd, or the switch).
    for (const PasteKey& e : endQueue_) endedMeanwhile = endedMeanwhile || e.pasteOp == k.pasteOp;
    if (go && (endedMeanwhile || !Usable())) go = false;
    if (go) {
      recv_ = RecvRun{true, k.offerId, k.pasteOp};
      ++counters_.recvPrepared;
    } else {
      ++counters_.recvRefused;
    }
  }
  if (!go) {
    if (arbiter_) arbiter_->Release(k.pasteOp);
    if (parsed && r.verdict == fn::Verdict::Accept) {
      // The host pinned for a paste that will not run: it is ended there too.
      std::lock_guard<std::mutex> lock(mu_);
      if (!endedMeanwhile) endQueue_.push_back(PasteKey{k.offerId, k.pasteOp, fn::PasteEndReason::ConsumerError});
    }
    RefuseDescriptor(k.offerId, k.pasteOp, why);
    std::ostringstream os;
    os << "remote paste not prepared: verdict=" << (parsed ? static_cast<int>(r.verdict) : -1)
       << " status=" << static_cast<int>(why);
    Log(os.str());
    return exchanged ? 1 : -1;
  }
  receiver_.Open(send_, bulk_stream_id(r.bulkGen, kFileBulkStreamClientToHost),
                 bulk_stream_id(r.bulkGen, kFileBulkStreamHostToClient), mtu_,
                 FilePasteIdentity{r.epochTag, k.offerId, k.pasteOp, r.bulkGen}, sizes);
  d.status = fc::Status::Ok;
  (void)helper_.Send(fc::encode(d));
  std::ostringstream os;
  os << "remote paste prepared files=" << d.items.size() << " gen=" << r.bulkGen;
  Log(os.str());
  return 1;
}

void FileCopyClient::OnHelperPasteEnd(const fc::PasteEnd& m) {
  std::lock_guard<std::mutex> lock(mu_);
  if (recv_.active && recv_.pasteOp == m.pasteOp) {
    // A failed chunk check is the reason, whatever the consumer made of the failed Read.
    const fn::PasteEndReason failure = receiver_.failure();
    EndReceiveLocked(failure != fn::PasteEndReason::None ? failure : map_end(m.reason));
    return;
  }
  // Ended before it was prepared: the prepare is not sent, or its result is undone.
  for (auto it = prepareQueue_.begin(); it != prepareQueue_.end(); ++it) {
    if (it->pasteOp == m.pasteOp) {
      prepareQueue_.erase(it);
      return;
    }
  }
  // A prepare in flight sees it (and ends the host's pin if the host already made one).
  if (preparingOp_ == m.pasteOp) endQueue_.push_back(PasteKey{m.offerId, m.pasteOp, map_end(m.reason)});
}

void FileCopyClient::EndReceiveLocked(fn::PasteEndReason reason) {
  if (!recv_.active) return;
  const RecvRun run = recv_;
  recv_ = RecvRun{};
  receiver_.Close(fc::Status::Aborted);
  for (bool whole : receiver_.wholeFileVerified()) {
    if (whole) ++counters_.filesWholeVerified;
    else ++counters_.filesChunkVerified;
  }
  if (arbiter_) arbiter_->Release(run.pasteOp);
  if (reason == fn::PasteEndReason::Completed) ++counters_.recvEnded;
  else ++counters_.recvFailed;
  counters_.lastRecvEndReason = static_cast<uint8_t>(reason);
  endQueue_.push_back(PasteKey{run.offerId, run.pasteOp, reason});  // told to the host on the control thread
  std::ostringstream os;
  os << "remote paste ended reason=" << static_cast<int>(reason) << " bytes=" << receiver_.bytesDelivered();
  Log(os.str());
}

int FileCopyClient::SendReceiveEnd(ControlLink& link, const PasteKey& k) {
  fn::End e{k.offerId, k.pasteOp, k.reason};
  std::vector<uint8_t> raw;
  if (!Exchange(link, fn::FileMsg::End, fn::body(e), fn::FileMsg::EndReply, &raw)) return -1;
  fn::EndReply r;
  return fn::parse(raw, &r) ? 1 : -1;
}

void FileCopyClient::OnHelperGone() {
  std::lock_guard<std::mutex> lock(mu_);
  EndReceiveLocked(fn::PasteEndReason::Session);
  publishedOfferId_ = 0;  // the helper cleared what it published when it went
  prepareQueue_.clear();
}

// ------------------------------------------------------------------------------ transport / session

bool FileCopyClient::OnDatagram(const void* data, size_t len) {
  if (!bulk_datagram_is_file(data, len)) return false;
  // One paste at a time: the receiver's stream (R->P) or the uplink's (P->R); a channel drops what
  // is not its open stream.
  if (receiver_.IsOpen()) (void)receiver_.OnDatagram(data, len);
  else (void)uplink_.OnDatagram(data, len);
  return true;
}

void FileCopyClient::EndSession() {
  bool disconnect = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    EndPaste(fn::PasteState::Withdrawn, fn::PasteEndReason::Session);
    offer_ = OfferState{};
    retired_ = OfferState{};
    withdrawOfferId_ = 0;
    closeUplinkPending_ = false;
    EndReceiveLocked(fn::PasteEndReason::Session);
    endQueue_.clear();
    prepareQueue_.clear();
    remote_ = RemoteOffer{};
    remoteRetired_ = RemoteOffer{};
    disconnect = publishedOfferId_ != 0 || R2PEnabled();
    publishedOfferId_ = 0;
    nextOfferQueryUs_ = 0;
    remoteBaselineTaken_ = false;
  }
  server_.End();
  uplink_.Close();
  {
    std::lock_guard<std::mutex> pin(pinMu_);
    pins_.ReleaseAll();
  }
  // The helper is per session: it clears what it published and exits.
  if (disconnect) helper_.Disconnect();
  hostSupports_.store(false);
}

FileCopyClient::Counters FileCopyClient::GetCounters() const {
  Counters c;
  {
    std::lock_guard<std::mutex> lock(mu_);
    c = counters_;
  }
  const FileChunkServer::Counters sc = server_.GetCounters();
  c.chunksServed = sc.chunksServed;
  c.bytesServed = sc.bytesServed;
  c.pullsRefused = sc.pullsRefused;
  const FilePullReceiver::Counters rc = receiver_.GetCounters();
  c.chunksVerified = rc.chunksVerified;
  c.chunksRejected = rc.chunksRejected;
  c.bytesReceived = rc.bytesDelivered;
  c.helperLaunches = helper_.launches();
  c.helperLaunchFailures = helper_.launchFailures();
  return c;
}

}  // namespace remote60::native_poc

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
    // Tagged callbacks (r8): which helper instance spoke is checked under mu_, together with what
    // it changes -- a late frame or "gone" of a helper this viewer has since replaced changes nothing.
    helper_.Configure(
        hc, [this](uint64_t, uint64_t instance, const fc::PipeFrame& f) { OnHelperFrame(instance, f); },
        [this](uint64_t, uint64_t instance) { OnHelperGone(instance); });
    receiver_.Start([this](uint64_t instance, const fc::ReadData& d) {
      if (helperProbe_) helperProbe_(instance, 5);
      (void)helper_.SendTo(instance, fc::encode(d));
    });
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

void FileCopyClient::SetAllowed(bool v) {
  const bool was = allowed_.exchange(v);
  if (!was || v || !running_.load()) return;
  bool disconnect = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (paste_.active) {
      // The host learns it by End (73) -- sent even while disabled, it only ends.
      endQueue_.push_back(PasteKey{paste_.offerId, paste_.pasteOp, fn::PasteEndReason::Disabled});
      EndPaste(fn::PasteState::Failed, fn::PasteEndReason::Disabled);
    }
    EndReceiveLocked(fn::PasteEndReason::Disabled);
    waitBulk_.on = false;
    ReleasePreemptLocked();  // a paste waiting on an image ends here too (switched off: no resume)
    if (offer_.live) withdrawOfferId_ = offer_.offerId;
    offer_ = OfferState{};
    retired_ = OfferState{};
    prepareQueue_.clear();
    disconnect = publishedOfferId_ != 0 || R2PEnabled();
    publishedOfferId_ = 0;
    closeUplinkPending_ = false;
  }
  server_.End();
  uplink_.Close();
  if (disconnect) helper_.Disconnect();  // what it published comes off this clipboard
  Log("switched off: the running paste ended, the offers withdrawn");
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
  // The limit is the COPY's, judged before anything is left out (r2 ⑤): a copy of more than the limit
  // is refused whole, with its reason -- never offered as whichever files happened to come first.
  const bool tooMany = paths.size() > fn::kMaxOfferFiles;
  for (size_t i = 0; !tooMany && i < paths.size(); ++i) {
    const std::wstring& path = paths[i];
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
  fn::Verdict v = fn::Verdict::Accept;
  if (tooMany) {
    v = fn::Verdict::TooMany;
    why = "a copy of " + std::to_string(paths.size()) + " or more files (the limit is " +
          std::to_string(fn::kMaxOfferFiles) + ")";
  } else {
    v = items.empty() ? fn::Verdict::BadRequest : fn::check_offer_items(items, &why);
  }
  std::lock_guard<std::mutex> lock(mu_);
  ++counters_.submitted;
  counters_.filesExcluded += excluded;
  // The newer copy replaces the offer whatever it is: even a copy that cannot be offered takes the
  // older one off the remote clipboard (r2 ⑤). A paste already running on it runs on (①).
  if (offer_.live) withdrawOfferId_ = offer_.offerId;
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
  if (!running_.load() || !bulkNegotiated_.load(std::memory_order_acquire) ||
      !hostSupports_.load(std::memory_order_acquire)) {
    return 0;
  }
  // Switched off: only what ends things goes out (an End, a withdrawal) -- nothing that starts one.
  const bool allowed = allowed_.load(std::memory_order_acquire);
  const uint64_t now = BulkPacer::NowUs();
  // One exchange per turn, chosen in this order: what the consumer is waiting on first (an end,
  // a prepare), then this PC's offer, then the two 700 ms questions. Only the chosen one is taken.
  enum class Act { None, End, Prepare, PrepareAfterWait, PrepareToRemote, Withdraw, Offer, PasteQuery, OfferQuery } act = Act::None;
  PasteKey key;
  uint64_t withdraw = 0, queryOffer = 0;
  bool queryForPaste = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!endQueue_.empty()) {
      act = Act::End;
      key = endQueue_.front();
      endQueue_.pop_front();
    } else if (waitBulk_.on && (!arbiter_ || arbiter_->use() == BulkUse::Idle || now >= waitBulk_.deadlineUs)) {
      // D5: the bulk is free (the image confirmed its end) -- or the budget is spent and the paste
      // is refused before any byte (the prepare below finds it busy). Either way, now.
      act = waitBulk_.toRemote ? Act::PrepareToRemote : Act::PrepareAfterWait;
      key = waitBulk_.key;
      waitBulk_.on = false;
      if (!waitBulk_.toRemote) {  // a PasteEnd (or its helper's gone) now is seen (r3 A-1, r10)
        preparingOp_ = key.pasteOp;
        preparingOffer_ = key.offerId;
        preparingInstance_ = key.instance;
        preparingDead_ = key.instance <= lastGoneInstance_;
      }
    } else if (waitBulk_.on) {
      // waiting for the bulk: the other questions still go out below
      if (!allowed) {
      } else if (paste_.active && now >= paste_.nextQueryUs) {
        act = Act::PasteQuery;
        queryOffer = paste_.offerId;
        queryForPaste = true;
        paste_.nextQueryUs = now + kFilePasteQueryIntervalUs;
      }
    } else if (allowed && !prepareQueue_.empty()) {
      act = Act::Prepare;
      key = prepareQueue_.front();
      prepareQueue_.pop_front();
    } else if (withdrawOfferId_ != 0) {
      act = Act::Withdraw;
      withdraw = withdrawOfferId_;
    } else if (!allowed) {
      // nothing else while switched off
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
    case Act::PrepareAfterWait:
      result = PrepareReceive(link, key, false);  // no second wait: free now, or refused
      break;
    case Act::PrepareToRemote:
      result = PreparePaste(link, key.offerId, key.pasteOp);
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
    ++result_.offered;
    result_.offeredFiles = static_cast<uint32_t>(o.items.size());
    std::ostringstream os;
    os << "offer accepted files=" << o.items.size();
    Log(os.str());
  } else {
    ++counters_.offersRefused;
    if (r.verdict == fn::Verdict::HelperUnavailable) ++result_.noHelper, result_.noHelperHere = false;
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
    // The host no longer knows the running paste's offer at all (switched off, its state gone): the
    // paste is over -- the pins and the bulk are let go, not held for ever (r2 ④).
    if (forActivePaste && paste_.active && r.offerId == paste_.offerId && r.state == fn::PasteState::Withdrawn) {
      EndPaste(fn::PasteState::Withdrawn,
               r.reason != fn::PasteEndReason::None ? r.reason : fn::PasteEndReason::Session);
      return 1;
    }
    if (r.state == fn::PasteState::Begun && !paste_.active && r.pasteOp != 0 &&
        (r.offerId == offer_.offerId || r.offerId == retired_.offerId)) {
      if (waitBulk_.on && waitBulk_.key.pasteOp == r.pasteOp && waitBulk_.key.offerId == r.offerId) return 1;  // waiting
      if (arbiter_ && arbiter_->use() != BulkUse::Idle) {
        WaitForBulkLocked(true, PasteKey{r.offerId, r.pasteOp});
        return 1;
      }
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
      uint16_t why = static_cast<uint16_t>(fc::Status::Refused);  // busy / the host would not
      for (const fn::PreparedItem& it : p.items) {
        if (it.status != static_cast<uint16_t>(fc::Status::Ok)) {
          why = it.status;  // the pin's own reason: replaced, changed, in use, ...
          break;
        }
      }
      RecordResultLocked(true, fn::PasteState::Failed, fn::PasteEndReason::None, why,
                         static_cast<uint32_t>(p.items.size()), 0, 0);
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
    paste_.files = static_cast<uint32_t>(p.items.size());
    for (uint64_t s : sizes) paste_.bytesTotal += s;
    paste_.startUs = BulkPacer::NowUs();
    paste_.servedAtStart = server_.GetCounters().bytesServed;
    ++counters_.pastesPrepared;
  }
  server_.Begin(FilePasteIdentity{r.epochTag, offerId, pasteOp, r.bulkGen}, sizes,
                [this, pasteOp](uint32_t index, uint64_t offset, uint32_t length, std::vector<uint8_t>* out) {
                  if (!allowed_.load(std::memory_order_acquire)) return fc::Status::Aborted;  // switched off
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
  RecordResultLocked(true, state, reason, 0, paste_.files, server_.GetCounters().bytesServed - paste_.servedAtStart,
                     paste_.startUs);
  paste_.active = false;
  if (cancelPending_ && cancelKey_.offerId == paste_.offerId && cancelKey_.pasteOp == op) cancelPending_ = false;
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
      uint64_t inst = 0;
      if (!helper_.Ensure(&why, &inst) || !helper_.SendTo(inst, fc::encode(pub))) {
        Log("remote copy not published: helper unavailable (" + why + ")");
        std::lock_guard<std::mutex> lock(mu_);
        ++result_.noHelper, result_.noHelperHere = true;
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
      if (helper_.Running()) (void)helper_.SendTo(helper_.instance(), fc::encode(c));  // what is published is on the current helper
    });
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.remoteCleared;
  }
  return 1;
}

bool FileCopyClient::HelperCurrentLocked(uint64_t instance, bool gone) {
  if (instance == helper_.instance()) return true;
  if (gone) ++counters_.staleHelperGones;
  else ++counters_.staleHelperFrames;
  return false;
}

void FileCopyClient::SetHelperProbeForTest(std::function<void(uint64_t, int)> probe) {
  std::lock_guard<std::mutex> lock(mu_);
  helperProbe_ = std::move(probe);
  Log("TEST PROBE viewer helper installed");
}

void FileCopyClient::OnHelperFrame(uint64_t instance, const fc::PipeFrame& f) {
  if (helperProbe_) helperProbe_(instance, 1);
  switch (f.type) {
    case fc::PipeMsg::PublishResult: {
      fc::PublishResult m;
      if (!fc::decode(f, &m)) break;
      std::lock_guard<std::mutex> lock(mu_);
      if (!HelperCurrentLocked(instance, false)) break;
      if (m.status == fc::Status::Ok && m.offerId == remote_.offerId) {
        publishedOfferId_ = m.offerId;
        ++counters_.remotePublished;
        ++result_.available;
        result_.availableFiles = m.count;
        std::ostringstream os;
        os << "remote copy published files=" << m.count;
        Log(os.str());
      }
      break;
    }
    case fc::PipeMsg::PasteBegin: {
      fc::PasteBegin m;
      if (fc::decode(f, &m)) OnHelperPasteBegin(instance, m);
      break;
    }
    case fc::PipeMsg::ReadRequest: {
      fc::ReadRequest m;
      if (!fc::decode(f, &m)) break;
      std::lock_guard<std::mutex> lock(mu_);
      if (!HelperCurrentLocked(instance, false)) break;
      receiver_.Submit(m, instance);  // under mu_, like the receiver's open / close
      break;
    }
    case fc::PipeMsg::PasteEnd: {
      fc::PasteEnd m;
      if (fc::decode(f, &m)) OnHelperPasteEnd(instance, m);
      break;
    }
    default:
      break;
  }
}

void FileCopyClient::RefuseDescriptor(uint64_t instance, uint64_t offerId, uint64_t pasteOp, fc::Status why) {
  fc::PasteDescriptor d;
  d.offerId = offerId;
  d.pasteOp = pasteOp;
  d.status = why;
  if (helperProbe_) helperProbe_(instance, 3);
  (void)helper_.SendTo(instance, fc::encode(d));  // to the helper whose PasteBegin this answers, or to nobody
}

void FileCopyClient::OnHelperPasteBegin(uint64_t instance, const fc::PasteBegin& m) {
  bool refuse = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!HelperCurrentLocked(instance, false)) return;
    ++counters_.recvBegun;
    const bool known = m.offerId != 0 && (m.offerId == remote_.offerId || m.offerId == remoteRetired_.offerId);
    // One paste at a time here too; a second one fails before any byte, never replaces the first.
    refuse = !known || recv_.active || !prepareQueue_.empty() || !Usable();
    if (!refuse) prepareQueue_.push_back(PasteKey{m.offerId, m.pasteOp, fn::PasteEndReason::None, instance});
    else ++counters_.recvBusy;
  }
  if (refuse) {
    RefuseDescriptor(instance, m.offerId, m.pasteOp, fc::Status::Refused);
    Log("remote paste refused: another paste runs, or the offer is gone");
  }
}

int FileCopyClient::PrepareReceive(ControlLink& link, const PasteKey& k, bool mayWait) {
  std::vector<fn::OfferItem> items;
  {
    std::lock_guard<std::mutex> lock(mu_);
    items = k.offerId == remote_.offerId ? remote_.items : remoteRetired_.items;
    preparingOp_ = k.pasteOp;
    preparingOffer_ = k.offerId;
    preparingInstance_ = k.instance;
    preparingDead_ = k.instance <= lastGoneInstance_;  // its helper may be gone already
  }
  struct Done {
    FileCopyClient* self;
    ~Done() {
      std::lock_guard<std::mutex> lock(self->mu_);
      self->preparingOp_ = 0;
      self->preparingOffer_ = 0;
      self->preparingInstance_ = 0;
      self->preparingDead_ = false;
    }
  } done{this};
  if (mayWait) {
    std::lock_guard<std::mutex> lock(mu_);
    if (arbiter_ && arbiter_->use() != BulkUse::Idle && !waitBulk_.on) {
      // Taken (an image, or a paste the other way): wait for it, bounded (D5); an image is stopped.
      WaitForBulkLocked(false, k);
      return 0;
    }
  }
  if (arbiter_ && !arbiter_->TryAcquire(BulkUse::File, k.pasteOp)) {
    // The session's bulk is taken (an image, or a paste the other way): refused before any byte.
    RefuseDescriptor(k.instance, k.offerId, k.pasteOp, fc::Status::Refused);
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.recvBusy;
    counters_.lastRecvVerdict = static_cast<uint8_t>(fn::Verdict::Busy);
    // An end like any other: recorded, and a stopped image released (r3 A-1; the budget ran out here).
    RecordResultLocked(false, fn::PasteState::Failed, fn::PasteEndReason::None, static_cast<uint16_t>(fc::Status::Refused),
                       static_cast<uint32_t>(items.size()), 0, 0);
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
    } else if (it.size != items[i].size) {
      // Pinned at the offered size (a moved size is refused there), so any other is a broken answer --
      // and equal sizes keep the sum inside the offer's checked total, no overflow (r3 A-3).
      go = false;
      why = fc::Status::BadRequest;
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
    for (const PasteKey& e : endQueue_) endedMeanwhile = endedMeanwhile || (e.pasteOp == k.pasteOp && e.offerId == k.offerId);
    // The helper this paste is for must still be there and still be the current one (r10): its
    // "gone" during the round trip marked it dead; a successor adopted meanwhile makes it not
    // current. Either way nothing is opened for it -- the host is told (End below).
    const bool helperGone = preparingDead_ || k.instance <= lastGoneInstance_ || k.instance != helper_.instance();
    if (go && (endedMeanwhile || !Usable() || helperGone)) go = false;
    if (go) {
      recv_ = RecvRun{true, k.offerId, k.pasteOp, k.instance};
      recv_.files = static_cast<uint32_t>(sizes.size());
      for (uint64_t s : sizes) recv_.bytesTotal += s;
      recv_.startUs = BulkPacer::NowUs();
      ++counters_.recvPrepared;
      // Opened here, under mu_, with the run it belongs to (r10): a "gone" cannot slip between the
      // decision and the open.
      receiver_.Open(send_, bulk_stream_id(r.bulkGen, kFileBulkStreamClientToHost),
                     bulk_stream_id(r.bulkGen, kFileBulkStreamHostToClient), mtu_,
                     FilePasteIdentity{r.epochTag, k.offerId, k.pasteOp, r.bulkGen}, sizes, k.instance);
    } else {
      ++counters_.recvRefused;
      if (!endedMeanwhile) {
        RecordResultLocked(false, fn::PasteState::Failed, fn::PasteEndReason::None, static_cast<uint16_t>(why),
                           static_cast<uint32_t>(items.size()), 0, 0);
      } else {
        ReleasePreemptLocked();  // the consumer gave up first: its end was not recorded here, the image still is (r3 A-1)
      }
    }
  }
  if (!go) {
    if (arbiter_) arbiter_->Release(k.pasteOp);
    if (parsed && r.verdict == fn::Verdict::Accept) {
      // The host pinned for a paste that will not run: it is ended there too.
      std::lock_guard<std::mutex> lock(mu_);
      if (!endedMeanwhile) endQueue_.push_back(PasteKey{k.offerId, k.pasteOp, fn::PasteEndReason::ConsumerError});
    }
    RefuseDescriptor(k.instance, k.offerId, k.pasteOp, why);
    std::ostringstream os;
    os << "remote paste not prepared: verdict=" << (parsed ? static_cast<int>(r.verdict) : -1)
       << " status=" << static_cast<int>(why);
    Log(os.str());
    return exchanged ? 1 : -1;
  }
  d.status = fc::Status::Ok;
  if (helperProbe_) helperProbe_(k.instance, 4);
  (void)helper_.SendTo(k.instance, fc::encode(d));  // to the helper whose paste this is, or to nobody
  std::ostringstream os;
  os << "remote paste prepared files=" << d.items.size() << " gen=" << r.bulkGen;
  Log(os.str());
  return 1;
}

void FileCopyClient::OnHelperPasteEnd(uint64_t instance, const fc::PasteEnd& m) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!HelperCurrentLocked(instance, false)) return;
  if (recv_.active && recv_.offerId == m.offerId && recv_.pasteOp == m.pasteOp) {
    // A failed chunk check is the reason, whatever the consumer made of the failed Read.
    const fn::PasteEndReason failure = receiver_.failure();
    EndReceiveLocked(failure != fn::PasteEndReason::None ? failure : map_end(m.reason));
    return;
  }
  // Ended while waiting for the bulk: nothing to prepare any more.
  if (waitBulk_.on && !waitBulk_.toRemote && waitBulk_.key.offerId == m.offerId && waitBulk_.key.pasteOp == m.pasteOp) {
    waitBulk_.on = false;
    RecordResultLocked(false, fn::PasteState::Failed, map_end(m.reason), 0, 0, 0, 0);
    return;
  }
  // Ended before it was prepared: the prepare is not sent, or its result is undone.
  for (auto it = prepareQueue_.begin(); it != prepareQueue_.end(); ++it) {
    if (it->offerId == m.offerId && it->pasteOp == m.pasteOp) {
      prepareQueue_.erase(it);
      return;
    }
  }
  // A prepare in flight sees it (and ends the host's pin if the host already made one).
  if (preparingOp_ == m.pasteOp && preparingOffer_ == m.offerId) endQueue_.push_back(PasteKey{m.offerId, m.pasteOp, map_end(m.reason)});
}

void FileCopyClient::EndReceiveLocked(fn::PasteEndReason reason) {
  if (!recv_.active) return;
  const RecvRun run = recv_;
  recv_ = RecvRun{};
  RecordResultLocked(false, reason == fn::PasteEndReason::Completed ? fn::PasteState::Ended : fn::PasteState::Failed, reason,
                     0, run.files, receiver_.bytesDelivered(), run.startUs);
  receiver_.Close(fc::Status::Aborted);
  for (bool whole : receiver_.wholeFileVerified()) {
    if (whole) ++counters_.filesWholeVerified;
    else ++counters_.filesChunkVerified;
  }
  if (arbiter_) arbiter_->Release(run.pasteOp);
  if (reason == fn::PasteEndReason::Completed) ++counters_.recvEnded;
  else ++counters_.recvFailed;
  if (reason == fn::PasteEndReason::Verification) ++counters_.recvVerificationEnds;
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
  if (!fn::parse(raw, &r)) return -1;
  if (r.offerId != k.offerId || r.pasteOp != k.pasteOp) return 1;  // another id's answer changes nothing (r3 A-2)
  std::lock_guard<std::mutex> lock(mu_);
  if (paste_.active && paste_.offerId == k.offerId && paste_.pasteOp == k.pasteOp) {
    // P->R: how it ended is the host's to say -- a paste the consumer completed just before this
    // cancel is "completed", not "cancelled" (r3 A-2). Its kept result, with the reason, comes with
    // the next 700 ms question, asked now; None (unknown to the host) stays unconfirmed until then.
    paste_.nextQueryUs = 0;
    return 1;
  }
  if (cancelPending_ && cancelKey_.offerId == k.offerId && cancelKey_.pasteOp == k.pasteOp) cancelPending_ = false;
  return 1;
}

void FileCopyClient::OnHelperGone(uint64_t instance) {
  if (helperProbe_) helperProbe_(instance, 2);
  std::lock_guard<std::mutex> lock(mu_);
  // Whatever the branch below (r10): remembered by instance, and a prepare in flight for this
  // helper is marked dead -- when its round trip returns, nothing is opened for it.
  if (instance > lastGoneInstance_) lastGoneInstance_ = instance;
  if (preparingInstance_ == instance) preparingDead_ = true;
  // A helper this viewer has since replaced: what the new one published, is receiving or has
  // queued is not the old one's to end (r8) -- but what was the OLD one's ends with it (r9): its
  // paste being received, its wait for the bulk, its queued pastes.
  if (!HelperCurrentLocked(instance, true)) {
    if (recv_.active && recv_.instance == instance) EndReceiveLocked(fn::PasteEndReason::Session);
    if (waitBulk_.on && !waitBulk_.toRemote && waitBulk_.key.instance == instance) waitBulk_.on = false;
    for (auto it = prepareQueue_.begin(); it != prepareQueue_.end();) {
      if (it->instance == instance) it = prepareQueue_.erase(it);
      else ++it;
    }
    return;
  }
  EndReceiveLocked(fn::PasteEndReason::Session);
  if (waitBulk_.on && !waitBulk_.toRemote) waitBulk_.on = false;  // its consumer went with the helper
  ReleasePreemptLocked();
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
    cancelPending_ = false;
    waitBulk_.on = false;
    if (preemptActive_) {
      preemptActive_ = false;
      if (after_) after_(false);  // the session is over: the stopped image does not go again
    }
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
  c.helperSendsDropped = helper_.droppedSends();
  c.helperSendsFailed = helper_.failedSends();
  return c;
}

}  // namespace remote60::native_poc

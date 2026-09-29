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
  running_.store(true);
}

void FileCopyClient::Stop() {
  if (!running_.exchange(false)) return;
  EndSession();
}

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
  uint64_t withdraw = 0;
  bool sendOffer = false;
  uint64_t queryOffer = 0;
  bool queryForPaste = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    withdraw = withdrawOfferId_;
    sendOffer = offer_.pending;
    if (paste_.active && now >= paste_.nextQueryUs) {
      queryOffer = paste_.offerId;
      queryForPaste = true;
      paste_.nextQueryUs = now + kFilePasteQueryIntervalUs;
    } else if (offer_.live && now >= offer_.nextQueryUs) {
      queryOffer = offer_.offerId;
      offer_.nextQueryUs = now + kFilePasteQueryIntervalUs;
    }
  }
  int result = 0;
  if (withdraw) {
    fn::End e{withdraw, 0, fn::PasteEndReason::None};
    std::vector<uint8_t> reply;
    if (!Exchange(link, fn::FileMsg::End, fn::body(e), fn::FileMsg::EndReply, &reply)) return -1;
    std::lock_guard<std::mutex> lock(mu_);
    if (withdrawOfferId_ == withdraw) withdrawOfferId_ = 0;
    result = 1;
  } else if (sendOffer) {
    result = PumpOffer(link);
  } else if (queryOffer) {
    result = PumpPasteQuery(link, queryOffer, queryForPaste);
  }
  bool close = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    close = closeUplinkPending_;
    closeUplinkPending_ = false;
  }
  if (close) uplink_.Close();
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
    // The session's bulk is an image's: this paste is refused (Busy) -- step 3 pre-empts instead.
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
  paste_.epochTag = r.epochTag;
  paste_.bulkGen = r.bulkGen;
  for (const fn::PreparedItem& it : p.items) paste_.sizes.push_back(it.size);
  paste_.nextQueryUs = BulkPacer::NowUs() + kFilePasteQueryIntervalUs;
  ++counters_.pastesPrepared;
  uplink_.ResetRateCounters();
  uplink_.Open(bulk_stream_id(r.bulkGen, kFileBulkStreamClientToHost), bulk_stream_id(r.bulkGen, kFileBulkStreamHostToClient),
               this);
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

bool FileCopyClient::OnPull(const std::vector<uint8_t>& msg, uint64_t /*nowUs*/, std::vector<uint8_t>* out,
                            BulkServed* served) {
  fn::Pull p;
  if (!fn::parse_bulk(msg.data(), msg.size(), &p)) return false;
  uint64_t size = 0;
  {
    std::lock_guard<std::mutex> lock(mu_);
    const bool mine = paste_.active && p.epochTag == paste_.epochTag && p.offerId == paste_.offerId &&
                      p.pasteOp == paste_.pasteOp && p.bulkGen == paste_.bulkGen && p.fileIndex < paste_.sizes.size();
    if (mine) size = paste_.sizes[p.fileIndex];
    if (!mine || !fn::range_ok(size, p.offset, p.length, fn::kMaxFileChunkBytes)) {
      ++counters_.pullsRefused;
      return false;
    }
  }
  fn::Chunk c;
  c.epochTag = p.epochTag;
  c.offerId = p.offerId;
  c.pasteOp = p.pasteOp;
  c.bulkGen = p.bulkGen;
  c.fileIndex = p.fileIndex;
  c.requestId = p.requestId;
  c.offset = p.offset;
  fc::Status st;
  {
    std::lock_guard<std::mutex> pin(pinMu_);
    st = pins_.Read(p.pasteOp, p.fileIndex, p.offset, p.length, &c.data);
  }
  if (st != fc::Status::Ok || c.data.size() != p.length || !fn::sha256(c.data.data(), c.data.size(), &c.sha256)) {
    std::lock_guard<std::mutex> lock(mu_);
    ++counters_.pullsRefused;
    return false;
  }
  *out = fn::frame_bulk(c);
  served->chunkKey = p.requestId;
  std::lock_guard<std::mutex> lock(mu_);
  if (p.triggerRequestId != fn::kNoTrigger) {
    for (auto it = paste_.sent.begin(); it != paste_.sent.end(); ++it) {
      if (it->first == p.triggerRequestId) {
        served->completedChunk = true;
        served->completedBytes = it->second;
        served->triggerKey = p.triggerRequestId;
        paste_.sent.erase(it);
        break;
      }
    }
  }
  bool again = false;
  for (const auto& s : paste_.sent) again = again || s.first == p.requestId;
  if (!again) {
    paste_.sent.emplace_back(p.requestId, p.length);
    if (paste_.sent.size() > 32) paste_.sent.pop_front();
  }
  ++counters_.chunksServed;
  counters_.bytesServed += p.length;
  return true;
}

bool FileCopyClient::ChunkKeyOf(const uint8_t* message, size_t len, uint64_t* key) {
  // MessageHeader(8) + epoch(8) + offer(8) + paste(8) + gen(4) + file(4) -> requestId at byte 40.
  constexpr size_t kAt = sizeof(MessageHeader) + 8 + 8 + 8 + 4 + 4;
  if (fn::bulk_type(message, len) != static_cast<uint16_t>(fn::FileMsg::Chunk) || len < kAt + 8) return false;
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(message[kAt + i]) << (8 * i);
  *key = v;
  return true;
}

bool FileCopyClient::OnDatagram(const void* data, size_t len) {
  if (!bulk_datagram_is_file(data, len)) return false;
  (void)uplink_.OnDatagram(data, len);  // dropped by the channel unless it is the open stream
  return true;
}

void FileCopyClient::EndSession() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    EndPaste(fn::PasteState::Withdrawn, fn::PasteEndReason::Session);
    offer_ = OfferState{};
    retired_ = OfferState{};
    withdrawOfferId_ = 0;
    closeUplinkPending_ = false;
  }
  uplink_.Close();
  {
    std::lock_guard<std::mutex> pin(pinMu_);
    pins_.ReleaseAll();
  }
  hostSupports_.store(false);
}

}  // namespace remote60::native_poc

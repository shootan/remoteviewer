#include "host_unlock_relay.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <cstring>

#include "secure_input_protocol.hpp"  // kSecureInputServiceName
#include "secure_unlock_ipc.hpp"
#include "trusted_service_pipe.hpp"

namespace remote60::native_poc {

namespace {

// Translate the service's challenge response (pipe) into the viewer's challenge message (control).
ControlUnlockChallengeMessage to_control_challenge(const SecureUnlockChallengeResponse& r) {
  ControlUnlockChallengeMessage m{};
  m.header.magic = kMagic;
  m.header.type = static_cast<uint16_t>(MessageType::ControlUnlockChallenge);
  m.header.size = static_cast<uint16_t>(sizeof(m));
  m.requestId = r.requestId;
  m.status = r.stage;
  m.reserved = r.purpose;  // lockscreen-cred r1: the purpose it was issued for
  std::memcpy(m.challengeId, r.challengeId, sizeof(m.challengeId));
  std::memcpy(m.hostPub, r.hostPub, sizeof(m.hostPub));
  std::memcpy(m.salt, r.salt, sizeof(m.salt));
  m.hostId = r.hostId;
  m.clientSessionCookie = r.clientSessionCookie;
  m.accountId = r.accountId;
  m.requesterSession = r.requesterSession;
  m.consoleSession = r.consoleSession;
  m.lockGeneration = r.lockGeneration;
  m.topologyGeneration = r.topologyGeneration;
  m.issuedMs = r.issuedMs;
  m.expiresMs = r.expiresMs;
  return m;
}

ControlUnlockStatusResultMessage to_control_result(const SecureUnlockResult& r) {
  ControlUnlockStatusResultMessage m{};
  m.header.magic = kMagic;
  m.header.type = static_cast<uint16_t>(MessageType::ControlUnlockStatusResult);
  m.header.size = static_cast<uint16_t>(sizeof(m));
  m.requestId = r.requestId;
  m.jobId = r.jobId;
  m.stage = r.stage;
  m.terminal = r.terminal;
  m.win32Error = r.win32Error;
  return m;
}

ControlUnlockStatusResultMessage failed_result(uint32_t requestId, UnlockStage stage, uint32_t win32 = 0) {
  ControlUnlockStatusResultMessage r{};
  r.requestId = requestId;
  r.stage = static_cast<uint16_t>(stage);
  r.terminal = 1;
  r.win32Error = win32;
  return r;
}

constexpr uint64_t kCapsFreshMs = 120000;   // a Supported answer is advertised for this long ...
constexpr uint64_t kCapsRefreshMs = 60000;  // ... and asked again after this
constexpr uint64_t kCapsRetryMs = 60000;    // no answer / Unsupported: asked again after this

ControlUnlockChallengeMessage busy_challenge(uint32_t requestId, uint16_t purpose) {
  ControlUnlockChallengeMessage m{};
  m.requestId = requestId;
  m.status = static_cast<uint16_t>(UnlockStage::Busy);
  m.reserved = purpose;
  return m;
}

}  // namespace

bool HostUnlockRelay::IsLiveLocked(const Command& c, uint32_t requestId) const {
  if (c.txGen == 0) return false;
  auto it = liveTx_.find(TxKey{c.cookie, requestId});
  return it != liveTx_.end() && it->second == c.txGen;
}

size_t HostUnlockRelay::KeptEntriesForTest(uint64_t cookie) const {
  std::lock_guard<std::mutex> lk(mu_);
  size_t n = 0;
  for (const auto& e : challengeResults_) n += e.first.cookie == cookie;
  for (const auto& e : sealedResults_) n += e.first.cookie == cookie;
  for (const auto& e : lockCredRequests_) n += e.first.cookie == cookie;
  for (const auto& e : statusAnswered_) n += e.first.cookie == cookie;
  for (const auto& e : liveTx_) n += e.first.cookie == cookie;
  return n;
}

HostUnlockRelay::~HostUnlockRelay() { Stop(); }

void HostUnlockRelay::Stop() {
  if (running_.exchange(false)) {
    if (cancelEvent_) SetEvent(static_cast<HANDLE>(cancelEvent_));  // an exchange in flight ends now
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
  }
  if (cancelEvent_) {
    CloseHandle(static_cast<HANDLE>(cancelEvent_));
    cancelEvent_ = nullptr;
  }
}

void HostUnlockRelay::EnsureWorker() {
  bool expected = false;
  if (running_.compare_exchange_strong(expected, true)) {
    if (!cancelEvent_) cancelEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    worker_ = std::thread([this] { WorkerLoop(); });
  }
}

void* HostUnlockRelay::ConnectPipe(bool* busy) {
  if (busy) *busy = false;
  const auto start = std::chrono::steady_clock::now();
  const auto deadline = start + std::chrono::seconds(3);
  bool busySinceStart = true;  // r3 UX-R: every attempt so far found the pipe's one instance taken
  do {
    // secure-pipe-identity: verify the pipe's server IS the installed GNLinkSecureInput service BEFORE
    // this handle is used. A local process squatting the name while the service is stopped is refused
    // (Untrusted/Unavailable) -- we do NOT fall back to it, and never send it a ChallengeRequest. Trust
    // is only for this handle; every reconnect runs this again. SQOS IDENTIFICATION is set inside.
    // lockscreen-cred r2 (L5): opened for overlapped I/O, so every exchange has a deadline and can be cancelled.
    TrustedPipeStatus st = TrustedPipeStatus::Unavailable;
    ServerIdentityFacts diag;
    HANDLE pipe = static_cast<HANDLE>(OpenVerifiedServicePipe(
        testPipeName_ ? testPipeName_ : kSecureUnlockPipeName, GENERIC_READ | GENERIC_WRITE,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, testServiceName_ ? testServiceName_ : kSecureInputServiceName, 100,
        &st, testGatherer_, &diag));
    LogTrustChange("unlock", st, diag);  // R3: keep the stage/error locally, once per change
    if (pipe != INVALID_HANDLE_VALUE) {
      DWORD mode = PIPE_READMODE_MESSAGE;
      (void)SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
      return pipe;
    }
    // r3 UX-R: the service's pipe has one instance. Taken for a whole second (not the moment between two of
    // its connections) means another host's transaction holds it -- said as "busy", sooner than the 3 s.
    busySinceStart = busySinceStart && diag.failStage == PipeVerifyStage::Open && diag.failWin32 == ERROR_PIPE_BUSY;
    if (busySinceStart && std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(1000)) {
      if (busy) *busy = true;
      break;
    }
    if (!running_.load()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  } while (std::chrono::steady_clock::now() < deadline && running_.load());
  return INVALID_HANDLE_VALUE;
}

bool HostUnlockRelay::ProbeConnectForTest() {
  // Drive the PRODUCTION ConnectPipe (verify included) once; report whether it produced a trusted
  // handle. running_ must be true for the connect loop to run; restore it afterwards.
  const bool was = running_.exchange(true);
  HANDLE h = static_cast<HANDLE>(ConnectPipe());
  running_.store(was);
  if (h == INVALID_HANDLE_VALUE) return false;
  CloseHandle(h);
  return true;
}

// One request and its one reply of exactly `replySize` bytes, both overlapped, within `deadlineMs`; ended
// early by the cancel event (Stop, or the active transaction's session ending). On anything but Ok the I/O
// in flight is cancelled and waited for here, on the pipe's owner thread -- the caller then closes the pipe.
HostUnlockRelay::Io HostUnlockRelay::Exchange(void* pipeHandle, const void* req, uint32_t reqSize, uint8_t* buf,
                                              uint32_t bufSize, uint32_t replySize, uint32_t deadlineMs) {
  HANDLE pipe = static_cast<HANDLE>(pipeHandle);
  const ULONGLONG deadline = GetTickCount64() + deadlineMs;
  auto run = [&](bool write, void* data, DWORD size, DWORD* done) -> Io {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return Io::Failed;
    BOOL started = write ? WriteFile(pipe, data, size, done, &ov) : ReadFile(pipe, data, size, done, &ov);
    Io result = Io::Ok;
    if (!started) {
      if (GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(ov.hEvent);
        return Io::Failed;
      }
      const ULONGLONG now = GetTickCount64();
      const DWORD left = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
      HANDLE waits[2] = {ov.hEvent, static_cast<HANDLE>(cancelEvent_)};
      const DWORD w = WaitForMultipleObjects(cancelEvent_ ? 2 : 1, waits, FALSE, left);
      if (w == WAIT_OBJECT_0) {
        result = GetOverlappedResult(pipe, &ov, done, FALSE) ? Io::Ok : Io::Failed;
      } else {
        CancelIoEx(pipe, &ov);
        DWORD ignored = 0;
        GetOverlappedResult(pipe, &ov, &ignored, TRUE);  // the I/O is over before anything is closed
        result = (w == WAIT_OBJECT_0 + 1) ? Io::Cancelled : Io::Timeout;
      }
    } else if (!GetOverlappedResult(pipe, &ov, done, FALSE)) {
      result = Io::Failed;
    }
    CloseHandle(ov.hEvent);
    return result;
  };
  DWORD written = 0;
  Io io = run(true, const_cast<void*>(req), reqSize, &written);
  if (io != Io::Ok) return io;
  if (written != reqSize) return Io::Failed;
  DWORD read = 0;
  io = run(false, buf, bufSize, &read);
  if (io != Io::Ok) return io;
  return read == replySize ? Io::Ok : Io::Failed;
}

void HostUnlockRelay::WorkerLoop() {
  HANDLE pipe = INVALID_HANDLE_VALUE;
  uint32_t pipeServerPid = 0;
  // lockscreen-cred r1 (Codex Q2) / r2 (L1, L5): the service's unlock pipe has ONE instance -- while this relay
  // holds it, no other host can unlock. So it is held only for a transaction: a capability query closes it at
  // once; a challenge holds it until its seal (the challenge's life), a typed lock-screen credential until its
  // result (the service's wait); a final answer, a refusal, a failure, a cancel or that deadline closes it.
  // While one control session's transaction holds it, another session's request is answered "busy".
  uint64_t holdUntilMs = 0;   // 0: nothing in flight -- close after the command
  uint64_t heldCookie = 0;    // the control session the held transaction belongs to
  uint32_t heldRequestId = 0; // r3 L5-R: which of its transactions, and in which phase
  uint64_t heldTxGen = 0;
  bool heldTyped = false;
  bool budgetCut = false;  // r3 L5-R: the exchange's deadline was the hold's end, not its own
  auto drop_pipe = [&]() {
    if (pipe != INVALID_HANDLE_VALUE) {
      CloseHandle(pipe);
      pipe = INVALID_HANDLE_VALUE;
    }
    holdUntilMs = 0;
    heldCookie = 0;
    heldRequestId = 0;
    heldTxGen = 0;
    heldTyped = false;
    pipeServerPid = 0;
  };
  // r3 L5-R: the hold's absolute end. The pipe goes back to the service, and the transaction ends with it --
  // it is never continued on another connection (the service's state for it went with this one).
  auto expire_hold = [&]() {
    const TxKey key{heldCookie, heldRequestId};
    Command owner;
    owner.cookie = heldCookie;
    owner.txGen = heldTxGen;
    const bool typed = heldTyped;
    drop_pipe();
    std::lock_guard<std::mutex> lk(mu_);
    if (IsLiveLocked(owner, key.requestId)) {
      auto it = sealedResults_.find(key);
      if (typed) {
        if (it != sealedResults_.end() && !it->second.terminal) {
          it->second = failed_result(key.requestId, UnlockStage::UnlockUnconfirmed);
        }
      } else if (it == sealedResults_.end()) {
        sealedResults_[key] = failed_result(key.requestId, UnlockStage::Timeout);  // its seal never came in time
      }
      lockCredRequests_.erase(key);  // nothing more to ask the service about it
    }
    cv_.notify_all();
  };
  // What is left of the hold for the held session's own exchange (never past the hold's end).
  auto budget = [&](uint32_t normalMs) -> uint32_t {
    budgetCut = false;
    if (holdUntilMs == 0) return normalMs;
    const uint64_t now = GetTickCount64();
    const uint64_t left = holdUntilMs > now ? holdUntilMs - now : 0;
    budgetCut = left < normalMs;
    return static_cast<uint32_t>((std::min)(static_cast<uint64_t>(normalMs), left));
  };
  bool connectBusy = false;  // r3 UX-R: the last connect found the pipe held by another host
  auto ensure_pipe = [&]() -> bool {
    connectBusy = false;
    if (pipe != INVALID_HANDLE_VALUE) return true;
    pipe = static_cast<HANDLE>(ConnectPipe(&connectBusy));
    if (pipe == INVALID_HANDLE_VALUE) return false;
    ULONG pid = 0;
    GetNamedPipeServerProcessId(pipe, &pid);
    pipeServerPid = pid;
    std::lock_guard<std::mutex> lk(mu_);
    if (capsServerPid_ != 0 && capsServerPid_ != pipeServerPid) {
      lockCredSupport_ = LockCredSupport::Unknown;  // another service process: its own answer is needed
      capsCheckedMs_ = 0;
    }
    return true;
  };
  auto hold_for = [&](uint64_t ms, const Command& c, uint32_t requestId, bool typed) {
    holdUntilMs = GetTickCount64() + ms;
    heldCookie = c.cookie;
    heldRequestId = requestId;
    heldTxGen = c.txGen;
    heldTyped = typed;
  };
  auto release_if_idle = [&]() {
    if (holdUntilMs == 0) drop_pipe();
  };
  auto io_failed = [&]() {  // a failed / timed-out / cancelled exchange: the pipe goes, and so does "supported"
    const bool ours = budgetCut;
    drop_pipe();
    if (ours) return;  // our hold ran out -- not an answer of the service
    std::lock_guard<std::mutex> lk(mu_);
    if (lockCredSupport_ == LockCredSupport::Supported) {
      lockCredSupport_ = LockCredSupport::Unknown;
      capsCheckedMs_ = 0;
    }
  };

  while (running_.load()) {
    Command cmd;
    {
      std::unique_lock<std::mutex> lk(mu_);
      if (pipe != INVALID_HANDLE_VALUE && holdUntilMs != 0) {
        const uint64_t now = GetTickCount64();
        const auto left = std::chrono::milliseconds(holdUntilMs > now ? holdUntilMs - now : 0);
        cv_.wait_for(lk, left, [this] { return !running_.load() || !queue_.empty(); });
        // r3 L5-R: the hold's end is checked whether or not something is queued -- a queue that is never
        // empty (other sessions' busy requests, probes, repeated status) does not keep the pipe held.
        if (running_.load() && GetTickCount64() >= holdUntilMs) {
          lk.unlock();
          expire_hold();
          continue;
        }
      } else {
        cv_.wait(lk, [this] { return !running_.load() || !queue_.empty(); });
      }
      if (!running_.load()) break;
      cmd = queue_.front();
      queue_.pop_front();
      if (cancelEvent_) ResetEvent(static_cast<HANDLE>(cancelEvent_));
      activeCookie_.store(cmd.cookie, std::memory_order_release);
    }

    if (cmd.op == Op::Release) {  // the control session ended: its held transaction ends with it
      if (heldCookie != 0 && heldCookie == cmd.cookie) drop_pipe();
      continue;
    }

    const uint32_t cmdRequestId = cmd.op == Op::Sealed   ? cmd.seal.requestId
                                  : cmd.op == Op::Status ? cmd.statusRequestId
                                                         : cmd.challenge.requestId;
    // r3 L1-R: a seal or a status question of no live transaction (its session ended, or it never had a
    // challenge here) goes nowhere -- not to the service, and nothing is kept for it.
    if (cmd.op == Op::Sealed || cmd.op == Op::Status) {
      std::lock_guard<std::mutex> lk(mu_);
      if (!IsLiveLocked(cmd, cmdRequestId)) {
        SecureZeroMemory(&cmd.seal_ipc, sizeof(cmd.seal_ipc));
        continue;
      }
    }
    // L1: another control session's transaction holds the pipe -- this request does not ride on (and
    // overwrite) its context. Answered Busy at once (r3 UX-R: its own stage); a capability query waits.
    if (holdUntilMs != 0 && heldCookie != cmd.cookie && cmd.op != Op::Capabilities) {
      std::lock_guard<std::mutex> lk(mu_);
      const TxKey key{cmd.cookie, cmdRequestId};
      if (IsLiveLocked(cmd, cmdRequestId)) {  // r3 L1-R: a session that ended keeps nothing
        if (cmd.op == Op::Sealed) {
          sealedResults_[key] = failed_result(key.requestId, UnlockStage::Busy, ERROR_BUSY);
        } else if (cmd.op == Op::Status) {
          statusAnswered_[key] = ++statusSeq_;
        } else {
          challengeResults_[key] =
              busy_challenge(key.requestId, cmd.op == Op::LockCredChallenge ? kUnlockPurposeLockCred : kUnlockPurposeUnlock);
        }
      }
      if (cmd.op == Op::Sealed) SecureZeroMemory(&cmd.seal_ipc, sizeof(cmd.seal_ipc));
      cv_.notify_all();
      continue;
    }
    // r3 L5-R: a status question is asked only on the connection its typed transaction is held on; with that
    // connection gone, so is the service's state for it -- unconfirmed, never asked anew on another one.
    if (cmd.op == Op::Status && !(holdUntilMs != 0 && heldTyped && heldCookie == cmd.cookie &&
                                  heldRequestId == cmd.statusRequestId && heldTxGen == cmd.txGen)) {
      std::lock_guard<std::mutex> lk(mu_);
      const TxKey key{cmd.cookie, cmd.statusRequestId};
      if (IsLiveLocked(cmd, cmd.statusRequestId)) {
        auto it = sealedResults_.find(key);
        if (it != sealedResults_.end() && !it->second.terminal) {
          it->second = failed_result(key.requestId, UnlockStage::UnlockUnconfirmed);
        }
        statusAnswered_[key] = ++statusSeq_;
      }
      cv_.notify_all();
      continue;
    }
    if (holdUntilMs != 0 && heldCookie != cmd.cookie && cmd.op == Op::Capabilities) {
      std::lock_guard<std::mutex> lk(mu_);
      capsInFlight_ = false;  // asked again once the pipe is free
      continue;
    }

    if (!ensure_pipe()) {
      // Report a failure so a waiting control thread does not hang forever.
      std::lock_guard<std::mutex> lk(mu_);
      const bool live = IsLiveLocked(cmd, cmdRequestId);  // r3 L1-R
      // r3 UX-R: held by another host's transaction -> Busy, said as that; anything else stays a failure.
      const UnlockStage why = connectBusy ? UnlockStage::Busy : UnlockStage::InternalError;
      if (cmd.op == Op::Sealed) {
        if (live) {
          sealedResults_[TxKey{cmd.cookie, cmd.seal.requestId}] =
              failed_result(cmd.seal.requestId, why, connectBusy ? ERROR_BUSY : 0);
        }
        SecureZeroMemory(&cmd.seal_ipc, sizeof(cmd.seal_ipc));
      } else if (cmd.op == Op::Challenge || cmd.op == Op::LockCredChallenge) {
        ControlUnlockChallengeMessage m{};
        m.requestId = cmd.challenge.requestId;
        m.status = static_cast<uint16_t>(why);
        m.reserved = cmd.op == Op::LockCredChallenge ? kUnlockPurposeLockCred : kUnlockPurposeUnlock;
        if (live) challengeResults_[TxKey{cmd.cookie, cmd.challenge.requestId}] = m;
      } else if (cmd.op == Op::Capabilities) {
        capsInFlight_ = false;
        if (connectBusy) {
          // Another host's transaction holds the pipe: what this service said stays (its age still counts),
          // and it is asked again soon -- not hidden for a minute because another host is busy.
          capsRetryAfterMs_ = GetTickCount64() + 5000;
        } else {
          // unknown: asked again -- after a pause (no service: no 3 s loop per pong)
          capsRetryAfterMs_ = GetTickCount64() + kCapsRetryMs;
          lockCredSupport_ = LockCredSupport::Unknown;
        }
      } else if (live) {
        statusAnswered_[TxKey{cmd.cookie, cmd.statusRequestId}] = ++statusSeq_;
      }
      cv_.notify_all();
      continue;
    }

    uint8_t buf[512];
    if (cmd.op == Op::Capabilities) {
      // lockscreen-cred r1: an older service drops the connection on this kind -- that, too, is its answer.
      SecureUnlockQuery q{};
      q.kind = static_cast<uint16_t>(SecureUnlockKind::CapabilitiesRequest);
      q.requestId = 1;
      SecureUnlockCapabilitiesResponse resp{};
      const Io io = Exchange(pipe, &q, sizeof(q), buf, sizeof(buf), sizeof(resp), shortDeadlineMs_);
      if (io == Io::Ok) std::memcpy(&resp, buf, sizeof(resp));
      const bool valid = io == Io::Ok && resp.magic == kSecureUnlockMagic && resp.size == sizeof(resp) &&
                         resp.kind == static_cast<uint16_t>(SecureUnlockKind::CapabilitiesResponse) && resp.requestId == 1;
      const bool supported = valid && (resp.features & kSecureUnlockFeatureLockCredV1) != 0;
      const uint32_t serverPid = pipeServerPid;
      if (!valid) drop_pipe();
      release_if_idle();  // Q2: a capability query does not keep the pipe
      std::cout << "[native-video-host][lock-cred] service capability "
                << (supported ? "supported" : valid ? "not offered" : "no answer / unknown kind (older service?)") << "\n";
      std::lock_guard<std::mutex> lk(mu_);
      capsInFlight_ = false;
      lockCredSupport_ = supported ? LockCredSupport::Supported : LockCredSupport::Unsupported;
      capsCheckedMs_ = GetTickCount64();
      capsServerPid_ = serverPid;
      if (!supported) capsRetryAfterMs_ = GetTickCount64() + kCapsRetryMs;
      cv_.notify_all();
    } else if (cmd.op == Op::Challenge || cmd.op == Op::LockCredChallenge) {
      SecureUnlockChallengeRequest req{};
      req.kind = static_cast<uint16_t>(cmd.op == Op::LockCredChallenge ? SecureUnlockKind::LockCredChallengeRequest
                                                                        : SecureUnlockKind::ChallengeRequest);
      req.requestId = cmd.challenge.requestId;
      req.clientSessionCookie = cmd.cookie;
      const Io io = Exchange(pipe, &req, sizeof(req), buf, sizeof(buf), sizeof(SecureUnlockChallengeResponse),
                             budget(shortDeadlineMs_));
      ControlUnlockChallengeMessage out{};
      SecureUnlockChallengeResponse resp{};
      if (io == Io::Ok) std::memcpy(&resp, buf, sizeof(resp));
      // Validate the service reply before trusting it (Codex D4): magic/size/kind must match and the
      // requestId must be the one we asked for, or a crossed/malformed reply is rejected.
      // lockscreen-cred r1: and the purpose it was issued for must be the one asked for.
      const uint16_t wantPurpose = cmd.op == Op::LockCredChallenge ? kUnlockPurposeLockCred : kUnlockPurposeUnlock;
      if (io == Io::Ok && resp.magic == kSecureUnlockMagic && resp.size == sizeof(resp) &&
          resp.kind == static_cast<uint16_t>(SecureUnlockKind::ChallengeResponse) &&
          resp.requestId == req.requestId && resp.purpose == wantPurpose && resp.clientSessionCookie == cmd.cookie) {
        out = to_control_challenge(resp);
      } else {
        out.requestId = cmd.challenge.requestId;
        out.status = static_cast<uint16_t>(UnlockStage::InternalError);
        io_failed();
      }
      // Q2: an issued challenge holds the pipe for its seal (the service binds it to this connection) -- no
      // longer than the challenge lives; a refused one lets it go.
      std::lock_guard<std::mutex> lk(mu_);
      const TxKey key{cmd.cookie, cmd.challenge.requestId};
      // r3 L1-R: published only for the transaction still alive -- its session may have ended (or started a
      // newer one) while this exchange ran; then nothing is kept and nothing is held for it.
      const bool live = IsLiveLocked(cmd, cmd.challenge.requestId);
      if (live && out.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued)) {
        hold_for(challengeHoldMs_, cmd, cmd.challenge.requestId, false);
      } else if (!(holdUntilMs != 0 && heldCookie == cmd.cookie && heldRequestId != cmd.challenge.requestId)) {
        // A refusal lets the pipe go -- unless it refused a second request of the session whose OTHER
        // transaction is held here (Q1 "pending"): that one's state in the service lives on this connection.
        drop_pipe();
      }
      if (live) {
        if (cmd.op == Op::LockCredChallenge && out.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued)) {
          lockCredRequests_[key] = true;
        }
        sealedResults_.erase(key);  // a new challenge of this transaction: nothing earlier stands for it
        challengeResults_[key] = out;
      }
      cv_.notify_all();
    } else if (cmd.op == Op::Status) {
      // lockscreen-cred r1: where a typed lock-screen credential is now (r2: of THIS control session).
      SecureUnlockQuery q{};
      q.kind = static_cast<uint16_t>(SecureUnlockKind::StatusRequest);
      q.requestId = cmd.statusRequestId;
      q.clientSessionCookie = cmd.cookie;
      SecureUnlockResult resp{};
      const Io io = Exchange(pipe, &q, sizeof(q), buf, sizeof(buf), sizeof(resp), budget(shortDeadlineMs_));
      if (io == Io::Ok) std::memcpy(&resp, buf, sizeof(resp));
      const bool valid = io == Io::Ok && resp.magic == kSecureUnlockMagic && resp.size == sizeof(resp) &&
                         resp.kind == static_cast<uint16_t>(SecureUnlockKind::Result) && resp.requestId == q.requestId;
      if (!valid) {
        io_failed();
      } else if (resp.terminal) {
        drop_pipe();  // Q2: a final answer ends the transaction
      }
      std::lock_guard<std::mutex> lk(mu_);
      const TxKey key{cmd.cookie, q.requestId};
      // r3 L1-R: only a transaction still alive (its session not ended) -- never recreated by a late answer.
      if (IsLiveLocked(cmd, q.requestId)) {
        if (sealedResults_.count(key) != 0) {
          // The connection the job was on is gone, and with it the job's state in the service: unconfirmed.
          sealedResults_[key] = valid ? to_control_result(resp) : failed_result(q.requestId, UnlockStage::UnlockUnconfirmed);
        }
        statusAnswered_[key] = ++statusSeq_;
      }
      cv_.notify_all();
    } else {
      // r3 L5-R: within what is left of its challenge's hold -- the service's own commit fence ends at that
      // challenge's expiry (30 s), before this hold does (35 s).
      const Io io = Exchange(pipe, &cmd.seal_ipc, sizeof(cmd.seal_ipc), buf, sizeof(buf), sizeof(SecureUnlockResult),
                             budget(sealedDeadlineMs_));
      ControlUnlockStatusResultMessage out{};
      SecureUnlockResult resp{};
      if (io == Io::Ok) std::memcpy(&resp, buf, sizeof(resp));
      if (io == Io::Ok && resp.magic == kSecureUnlockMagic && resp.size == sizeof(resp) &&
          resp.kind == static_cast<uint16_t>(SecureUnlockKind::Result) && resp.requestId == cmd.seal.requestId) {
        out = to_control_result(resp);
      } else {
        out = failed_result(cmd.seal.requestId, io == Io::Cancelled ? UnlockStage::RejectedStaleTopology
                                                                   : UnlockStage::InternalError);
        io_failed();
      }
      SecureZeroMemory(&cmd.seal_ipc, sizeof(cmd.seal_ipc));
      std::lock_guard<std::mutex> lk(mu_);
      const TxKey key{cmd.cookie, cmd.seal.requestId};
      const bool live = IsLiveLocked(cmd, cmd.seal.requestId);
      // Q2: a typed lock-screen credential waits for its unlock on this connection (the service's bounded
      // wait); anything final -- or a session that ended meanwhile (r3 L1-R) -- ends the transaction.
      if (out.terminal || !live) {
        drop_pipe();
      } else if (!(holdUntilMs != 0 && heldTyped && heldCookie == cmd.cookie && heldRequestId == cmd.seal.requestId &&
                   heldTxGen == cmd.txGen)) {
        hold_for(typedHoldMs_, cmd, cmd.seal.requestId, true);
      }
      // else r4 (Codex R1): a repeat of the sealed request this typed transaction already holds the pipe for
      // (the service answers it "typed" again, typing nothing) keeps the FIRST typed deadline -- a repeat never
      // starts the 130 s anew.
      if (live) sealedResults_[key] = out;  // a session that ended meanwhile keeps nothing
      cv_.notify_all();
    }
    activeCookie_.store(0, std::memory_order_release);
  }
  drop_pipe();
}

HostUnlockRelay::LockCredSupport HostUnlockRelay::LockCredSupportNow() const {
  std::lock_guard<std::mutex> lk(mu_);
  if (lockCredSupport_ == LockCredSupport::Supported && GetTickCount64() - capsCheckedMs_ > kCapsFreshMs) {
    return LockCredSupport::Unknown;  // too old to advertise: asked again
  }
  return lockCredSupport_;
}

bool HostUnlockRelay::ChallengeSync(const ControlUnlockChallengeRequestMessage& req,
                                    uint64_t clientSessionCookie, ControlUnlockChallengeMessage* out,
                                    uint32_t timeoutMs) {
  if (!out) return false;
  EnsureWorker();
  const TxKey key{clientSessionCookie, req.requestId};
  {
    std::lock_guard<std::mutex> lk(mu_);
    challengeResults_.erase(key);
    Command cmd;
    cmd.op = Op::Challenge;
    cmd.challenge = req;
    cmd.cookie = clientSessionCookie;
    cmd.txGen = liveTx_[key] = ++txGenSeq_;  // r3 L1-R: a transaction starts with its challenge
    queue_.push_back(cmd);
    // A transaction is remembered from its challenge, so its sealed result has a place to land.
    sealedResults_.erase(key);
  }
  cv_.notify_all();
  std::unique_lock<std::mutex> lk(mu_);
  const bool got = cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return challengeResults_.count(key) != 0; });
  if (!got) return false;
  *out = challengeResults_[key];
  return true;
}

void HostUnlockRelay::SealedAsync(const ControlUnlockSealedRequestMessage& req, uint64_t clientSessionCookie) {
  EnsureWorker();
  Command cmd;
  cmd.op = Op::Sealed;
  cmd.cookie = clientSessionCookie;
  cmd.seal = req;
  // Build the pipe message now so the control thread's copy of the password ciphertext is not held.
  cmd.seal_ipc.requestId = req.requestId;
  cmd.seal_ipc.clientSessionCookie = clientSessionCookie;
  std::memcpy(cmd.seal_ipc.challengeId, req.challengeId, sizeof(cmd.seal_ipc.challengeId));
  std::memcpy(cmd.seal_ipc.clientPub, req.clientPub, sizeof(cmd.seal_ipc.clientPub));
  std::memcpy(cmd.seal_ipc.nonce, req.nonce, sizeof(cmd.seal_ipc.nonce));
  std::memcpy(cmd.seal_ipc.tag, req.tag, sizeof(cmd.seal_ipc.tag));
  std::memcpy(cmd.seal_ipc.cipher, req.cipher, sizeof(cmd.seal_ipc.cipher));
  {
    std::lock_guard<std::mutex> lk(mu_);
    // A repeat of THIS session's sealed request already resolved terminally is a no-op (idempotent);
    // another session's request with the same id is its own (L1).
    const TxKey key{clientSessionCookie, req.requestId};
    auto it = sealedResults_.find(key);
    if (it != sealedResults_.end() && it->second.terminal) {
      SecureZeroMemory(&cmd.seal_ipc, sizeof(cmd.seal_ipc));
      return;
    }
    auto live = liveTx_.find(key);
    cmd.txGen = live != liveTx_.end() ? live->second : 0;  // r3 L1-R: of the transaction its challenge started
    queue_.push_back(cmd);
  }
  cv_.notify_all();
}

bool HostUnlockRelay::PollResult(uint64_t clientSessionCookie, uint32_t requestId, ControlUnlockStatusResultMessage* out) {
  if (!out) return false;
  std::lock_guard<std::mutex> lk(mu_);
  auto it = sealedResults_.find(TxKey{clientSessionCookie, requestId});
  if (it == sealedResults_.end()) return false;
  *out = it->second;
  return true;
}

void HostUnlockRelay::EndSession(uint64_t clientSessionCookie) {
  if (!running_.load()) return;  // no worker: nothing held
  {
    std::lock_guard<std::mutex> lk(mu_);
    // L1: everything kept for this session's transactions goes with it -- and (r3 L1-R) the transactions
    // themselves, so an exchange of it still on the pipe publishes nothing when it completes.
    for (auto* m : {&challengeResults_}) {
      for (auto it = m->begin(); it != m->end();) it = it->first.cookie == clientSessionCookie ? m->erase(it) : std::next(it);
    }
    for (auto it = liveTx_.begin(); it != liveTx_.end();)
      it = it->first.cookie == clientSessionCookie ? liveTx_.erase(it) : std::next(it);
    for (auto it = sealedResults_.begin(); it != sealedResults_.end();)
      it = it->first.cookie == clientSessionCookie ? sealedResults_.erase(it) : std::next(it);
    for (auto it = lockCredRequests_.begin(); it != lockCredRequests_.end();)
      it = it->first.cookie == clientSessionCookie ? lockCredRequests_.erase(it) : std::next(it);
    for (auto it = statusAnswered_.begin(); it != statusAnswered_.end();)
      it = it->first.cookie == clientSessionCookie ? statusAnswered_.erase(it) : std::next(it);
    // Its commands not started yet are dropped; the release goes first, not behind the queue (L5).
    for (auto it = queue_.begin(); it != queue_.end();) {
      if (it->cookie == clientSessionCookie && it->op != Op::Capabilities) {
        SecureZeroMemory(&it->seal_ipc, sizeof(it->seal_ipc));
        it = queue_.erase(it);
      } else {
        ++it;
      }
    }
    Command cmd;
    cmd.op = Op::Release;
    cmd.cookie = clientSessionCookie;
    queue_.push_front(cmd);
    // An exchange of this session on the pipe right now is cancelled; the worker closes the pipe after it.
    if (activeCookie_.load(std::memory_order_acquire) == clientSessionCookie && cancelEvent_) {
      SetEvent(static_cast<HANDLE>(cancelEvent_));
    }
  }
  cv_.notify_all();
}

void HostUnlockRelay::ProbeLockCredAsync() {
  {
    std::lock_guard<std::mutex> lk(mu_);
    const uint64_t now = GetTickCount64();
    if (capsInFlight_) return;
    if (lockCredSupport_ == LockCredSupport::Supported && now - capsCheckedMs_ < kCapsRefreshMs) return;
    if (now < capsRetryAfterMs_) return;  // r3: also after a busy pipe, whatever the answer kept
    capsInFlight_ = true;
  }
  EnsureWorker();
  {
    std::lock_guard<std::mutex> lk(mu_);
    Command cmd;
    cmd.op = Op::Capabilities;
    queue_.push_back(cmd);
  }
  cv_.notify_all();
}

bool HostUnlockRelay::LockCredChallengeSync(const ControlUnlockChallengeRequestMessage& req,
                                            uint64_t clientSessionCookie, ControlUnlockChallengeMessage* out,
                                            uint32_t timeoutMs) {
  if (!out) return false;
  if (LockCredSupportNow() != LockCredSupport::Supported) {
    *out = ControlUnlockChallengeMessage{};
    out->requestId = req.requestId;
    out->status = static_cast<uint16_t>(UnlockStage::RejectedPolicy);
    out->reserved = kUnlockPurposeLockCred;
    return true;
  }
  EnsureWorker();
  const TxKey key{clientSessionCookie, req.requestId};
  {
    std::lock_guard<std::mutex> lk(mu_);
    challengeResults_.erase(key);
    sealedResults_.erase(key);
    lockCredRequests_.erase(key);
    Command cmd;
    cmd.op = Op::LockCredChallenge;
    cmd.challenge = req;
    cmd.cookie = clientSessionCookie;
    cmd.txGen = liveTx_[key] = ++txGenSeq_;  // r3 L1-R
    queue_.push_back(cmd);
  }
  cv_.notify_all();
  std::unique_lock<std::mutex> lk(mu_);
  const bool got = cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return challengeResults_.count(key) != 0; });
  if (!got) return false;
  *out = challengeResults_[key];
  out->reserved = kUnlockPurposeLockCred;
  return true;
}

bool HostUnlockRelay::RefreshLockCredStatus(uint64_t clientSessionCookie, uint32_t requestId, uint32_t timeoutMs) {
  const TxKey key{clientSessionCookie, requestId};
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (lockCredRequests_.count(key) == 0) return false;
    auto it = sealedResults_.find(key);
    if (it == sealedResults_.end() || it->second.terminal) return false;  // nothing typed yet, or final
  }
  EnsureWorker();
  uint64_t before = 0;
  {
    std::lock_guard<std::mutex> lk(mu_);
    before = statusSeq_;
    Command cmd;
    cmd.op = Op::Status;
    cmd.cookie = clientSessionCookie;
    cmd.statusRequestId = requestId;
    auto live = liveTx_.find(key);
    cmd.txGen = live != liveTx_.end() ? live->second : 0;  // r3 L1-R
    queue_.push_back(cmd);
  }
  cv_.notify_all();
  std::unique_lock<std::mutex> lk(mu_);
  return cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
    auto it = statusAnswered_.find(key);
    return it != statusAnswered_.end() && it->second > before;
  });
}

}  // namespace remote60::native_poc

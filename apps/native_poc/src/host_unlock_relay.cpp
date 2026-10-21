#include "host_unlock_relay.hpp"

#include <windows.h>

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

bool write_all(HANDLE pipe, const void* data, DWORD size) {
  DWORD written = 0;
  return WriteFile(pipe, data, size, &written, nullptr) && written == size;
}

}  // namespace

HostUnlockRelay::~HostUnlockRelay() { Stop(); }

void HostUnlockRelay::Stop() {
  if (running_.exchange(false)) {
    cv_.notify_all();
    // Wake the worker whether it is parked on the condition variable (notify above) or blocked in a
    // synchronous ConnectNamedPipe/ReadFile. CancelSynchronousIo only acts while the thread is inside
    // a synchronous wait, so loop until the thread ends. The worker owns and closes its own pipe, so
    // there is no controller-close handle race. (Codex #370 BLOCKER B.)
    HANDLE h = static_cast<HANDLE>(workerThreadHandle_.load(std::memory_order_acquire));
    if (h != nullptr) {
      while (WaitForSingleObject(h, 50) == WAIT_TIMEOUT) (void)CancelSynchronousIo(h);
    }
    if (worker_.joinable()) worker_.join();
    workerThreadHandle_.store(nullptr, std::memory_order_release);
  }
}

void HostUnlockRelay::EnsureWorker() {
  bool expected = false;
  if (running_.compare_exchange_strong(expected, true)) {
    worker_ = std::thread([this] { WorkerLoop(); });
    workerThreadHandle_.store(worker_.native_handle(), std::memory_order_release);
  }
}

void* HostUnlockRelay::ConnectPipe() {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  do {
    // secure-pipe-identity: verify the pipe's server IS the installed GNLinkSecureInput service BEFORE
    // this handle is used. A local process squatting the name while the service is stopped is refused
    // (Untrusted/Unavailable) -- we do NOT fall back to it, and never send it a ChallengeRequest. Trust
    // is only for this handle; every reconnect runs this again. SQOS IDENTIFICATION is set inside.
    TrustedPipeStatus st = TrustedPipeStatus::Unavailable;
    ServerIdentityFacts diag;
    HANDLE pipe = static_cast<HANDLE>(OpenVerifiedServicePipe(
        testPipeName_ ? testPipeName_ : kSecureUnlockPipeName, GENERIC_READ | GENERIC_WRITE,
        FILE_ATTRIBUTE_NORMAL, testServiceName_ ? testServiceName_ : kSecureInputServiceName, 100, &st,
        testGatherer_, &diag));
    LogTrustChange("unlock", st, diag);  // R3: keep the stage/error locally, once per change
    if (pipe != INVALID_HANDLE_VALUE) {
      DWORD mode = PIPE_READMODE_MESSAGE;
      (void)SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
      return pipe;
    }
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

void HostUnlockRelay::WorkerLoop() {
  HANDLE pipe = INVALID_HANDLE_VALUE;
  auto ensure_pipe = [&]() -> bool {
    if (pipe != INVALID_HANDLE_VALUE) return true;
    pipe = static_cast<HANDLE>(ConnectPipe());
    if (pipe != INVALID_HANDLE_VALUE) {
      // lockscreen-cred r1: a new connection may be to another service process: what the old one said
      // about its capabilities is not this one's.
      lockCredSupport_.store(LockCredSupport::Unknown, std::memory_order_release);
    }
    return pipe != INVALID_HANDLE_VALUE;
  };
  auto drop_pipe = [&]() {
    if (pipe != INVALID_HANDLE_VALUE) {
      CloseHandle(pipe);
      pipe = INVALID_HANDLE_VALUE;
    }
  };
  // One request, one reply of exactly `replySize` bytes, or false.
  auto exchange = [&](const void* req, DWORD reqSize, uint8_t* buf, DWORD bufSize, DWORD replySize) -> bool {
    DWORD read = 0;
    return write_all(pipe, req, reqSize) && ReadFile(pipe, buf, bufSize, &read, nullptr) && read == replySize;
  };

  while (running_.load()) {
    Command cmd;
    {
      std::unique_lock<std::mutex> lk(mu_);
      cv_.wait(lk, [this] { return !running_.load() || !queue_.empty(); });
      if (!running_.load()) break;
      cmd = queue_.front();
      queue_.pop_front();
    }

    if (!ensure_pipe()) {
      // Report a failure so a waiting control thread does not hang forever.
      std::lock_guard<std::mutex> lk(mu_);
      if (cmd.op == Op::Sealed) {
        ControlUnlockStatusResultMessage r{};
        r.requestId = cmd.seal.requestId;
        r.stage = static_cast<uint16_t>(UnlockStage::InternalError);
        r.terminal = 1;
        sealedResults_[cmd.seal.requestId] = r;
      } else if (cmd.op == Op::Challenge || cmd.op == Op::LockCredChallenge) {
        ControlUnlockChallengeMessage m{};
        m.requestId = cmd.challenge.requestId;
        m.status = static_cast<uint16_t>(UnlockStage::InternalError);
        challengeResults_[cmd.challenge.requestId] = m;
      } else if (cmd.op == Op::Capabilities) {
        capsInFlight_ = false;  // unknown: asked again on the next probe
      } else {
        statusAnswered_[cmd.statusRequestId] = ++statusSeq_;
      }
      cv_.notify_all();
      continue;
    }

    if (cmd.op == Op::Capabilities) {
      // lockscreen-cred r1: an older service drops the connection on this kind -- that, too, is its answer.
      SecureUnlockQuery q{};
      q.kind = static_cast<uint16_t>(SecureUnlockKind::CapabilitiesRequest);
      q.requestId = 1;
      uint8_t buf[512];
      SecureUnlockCapabilitiesResponse resp{};
      const bool ok = exchange(&q, sizeof(q), buf, sizeof(buf), sizeof(resp));
      if (ok) std::memcpy(&resp, buf, sizeof(resp));
      const bool valid = ok && resp.magic == kSecureUnlockMagic && resp.size == sizeof(resp) &&
                         resp.kind == static_cast<uint16_t>(SecureUnlockKind::CapabilitiesResponse) && resp.requestId == 1;
      const bool supported = valid && (resp.features & kSecureUnlockFeatureLockCredV1) != 0;
      if (!valid) drop_pipe();
      lockCredSupport_.store(supported ? LockCredSupport::Supported : LockCredSupport::Unsupported, std::memory_order_release);
      std::cout << "[native-video-host][lock-cred] service capability "
                << (supported ? "supported" : valid ? "not offered" : "unknown kind refused (older service)") << "\n";
      std::lock_guard<std::mutex> lk(mu_);
      capsInFlight_ = false;
      if (!supported) capsRetryAfterMs_ = GetTickCount64() + 60000;
      cv_.notify_all();
    } else if (cmd.op == Op::Challenge || cmd.op == Op::LockCredChallenge) {
      SecureUnlockChallengeRequest req{};
      req.kind = static_cast<uint16_t>(cmd.op == Op::LockCredChallenge ? SecureUnlockKind::LockCredChallengeRequest
                                                                        : SecureUnlockKind::ChallengeRequest);
      req.requestId = cmd.challenge.requestId;
      req.clientSessionCookie = cmd.cookie;
      uint8_t buf[512];
      const bool ok = exchange(&req, sizeof(req), buf, sizeof(buf), sizeof(SecureUnlockChallengeResponse));
      ControlUnlockChallengeMessage out{};
      SecureUnlockChallengeResponse resp{};
      if (ok) std::memcpy(&resp, buf, sizeof(resp));
      // Validate the service reply before trusting it (Codex D4): magic/size/kind must match and the
      // requestId must be the one we asked for, or a crossed/malformed reply is rejected.
      // lockscreen-cred r1: and the purpose it was issued for must be the one asked for.
      const uint16_t wantPurpose = cmd.op == Op::LockCredChallenge ? kUnlockPurposeLockCred : kUnlockPurposeUnlock;
      if (ok && resp.magic == kSecureUnlockMagic && resp.size == sizeof(resp) &&
          resp.kind == static_cast<uint16_t>(SecureUnlockKind::ChallengeResponse) &&
          resp.requestId == req.requestId && resp.purpose == wantPurpose) {
        out = to_control_challenge(resp);
      } else {
        out.requestId = cmd.challenge.requestId;
        out.status = static_cast<uint16_t>(UnlockStage::InternalError);
        drop_pipe();
      }
      std::lock_guard<std::mutex> lk(mu_);
      if (cmd.op == Op::LockCredChallenge &&
          out.status == static_cast<uint16_t>(UnlockStage::ChallengeIssued)) {
        lockCredRequests_[cmd.challenge.requestId] = true;
      }
      challengeResults_[cmd.challenge.requestId] = out;
      cv_.notify_all();
    } else if (cmd.op == Op::Status) {
      // lockscreen-cred r1: where a typed lock-screen credential is now.
      SecureUnlockQuery q{};
      q.kind = static_cast<uint16_t>(SecureUnlockKind::StatusRequest);
      q.requestId = cmd.statusRequestId;
      uint8_t buf[512];
      SecureUnlockResult resp{};
      const bool ok = exchange(&q, sizeof(q), buf, sizeof(buf), sizeof(resp));
      if (ok) std::memcpy(&resp, buf, sizeof(resp));
      const bool valid = ok && resp.magic == kSecureUnlockMagic && resp.size == sizeof(resp) &&
                         resp.kind == static_cast<uint16_t>(SecureUnlockKind::Result) && resp.requestId == q.requestId;
      if (!valid) drop_pipe();
      std::lock_guard<std::mutex> lk(mu_);
      if (valid) {
        sealedResults_[q.requestId] = to_control_result(resp);
      } else {
        // The connection the job was on is gone; so is the job's state in the service.
        ControlUnlockStatusResultMessage r{};
        r.requestId = q.requestId;
        r.stage = static_cast<uint16_t>(UnlockStage::UnlockUnconfirmed);
        r.terminal = 1;
        sealedResults_[q.requestId] = r;
      }
      statusAnswered_[q.requestId] = ++statusSeq_;
      cv_.notify_all();
    } else {
      uint8_t buf[512];
      const bool ok = exchange(&cmd.seal_ipc, sizeof(cmd.seal_ipc), buf, sizeof(buf), sizeof(SecureUnlockResult));
      ControlUnlockStatusResultMessage out{};
      SecureUnlockResult resp{};
      if (ok) std::memcpy(&resp, buf, sizeof(resp));
      if (ok && resp.magic == kSecureUnlockMagic && resp.size == sizeof(resp) &&
          resp.kind == static_cast<uint16_t>(SecureUnlockKind::Result) &&
          resp.requestId == cmd.seal.requestId) {
        out = to_control_result(resp);
      } else {
        out.requestId = cmd.seal.requestId;
        out.stage = static_cast<uint16_t>(UnlockStage::InternalError);
        out.terminal = 1;
        drop_pipe();
      }
      SecureZeroMemory(&cmd.seal_ipc, sizeof(cmd.seal_ipc));
      std::lock_guard<std::mutex> lk(mu_);
      sealedResults_[cmd.seal.requestId] = out;
      cv_.notify_all();
    }
  }
  drop_pipe();
}

void HostUnlockRelay::ProbeLockCredAsync() {
  {
    std::lock_guard<std::mutex> lk(mu_);
    const LockCredSupport now = lockCredSupport_.load(std::memory_order_acquire);
    if (capsInFlight_ || now == LockCredSupport::Supported) return;
    if (now == LockCredSupport::Unsupported && GetTickCount64() < capsRetryAfterMs_) return;
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
  if (lockCredSupport_.load(std::memory_order_acquire) != LockCredSupport::Supported) {
    *out = ControlUnlockChallengeMessage{};
    out->requestId = req.requestId;
    out->status = static_cast<uint16_t>(UnlockStage::RejectedPolicy);
    out->reserved = kUnlockPurposeLockCred;
    return true;
  }
  EnsureWorker();
  {
    std::lock_guard<std::mutex> lk(mu_);
    Command cmd;
    cmd.op = Op::LockCredChallenge;
    cmd.challenge = req;
    cmd.cookie = clientSessionCookie;
    queue_.push_back(cmd);
  }
  cv_.notify_all();
  std::unique_lock<std::mutex> lk(mu_);
  const bool got = cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
    return challengeResults_.count(req.requestId) != 0;
  });
  if (!got) return false;
  *out = challengeResults_[req.requestId];
  challengeResults_.erase(req.requestId);
  out->reserved = kUnlockPurposeLockCred;
  return true;
}

bool HostUnlockRelay::RefreshLockCredStatus(uint32_t requestId, uint32_t timeoutMs) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (lockCredRequests_.count(requestId) == 0) return false;
    auto it = sealedResults_.find(requestId);
    if (it == sealedResults_.end() || it->second.terminal) return false;  // nothing typed yet, or final
  }
  EnsureWorker();
  uint64_t before = 0;
  {
    std::lock_guard<std::mutex> lk(mu_);
    before = statusSeq_;
    Command cmd;
    cmd.op = Op::Status;
    cmd.statusRequestId = requestId;
    queue_.push_back(cmd);
  }
  cv_.notify_all();
  std::unique_lock<std::mutex> lk(mu_);
  return cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
    auto it = statusAnswered_.find(requestId);
    return it != statusAnswered_.end() && it->second > before;
  });
}

bool HostUnlockRelay::ChallengeSync(const ControlUnlockChallengeRequestMessage& req,
                                    uint64_t clientSessionCookie, ControlUnlockChallengeMessage* out,
                                    uint32_t timeoutMs) {
  if (!out) return false;
  EnsureWorker();
  {
    std::lock_guard<std::mutex> lk(mu_);
    Command cmd;
    cmd.op = Op::Challenge;
    cmd.challenge = req;
    cmd.cookie = clientSessionCookie;
    queue_.push_back(cmd);
  }
  cv_.notify_all();
  std::unique_lock<std::mutex> lk(mu_);
  const bool got = cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
    return challengeResults_.count(req.requestId) != 0;
  });
  if (!got) return false;
  *out = challengeResults_[req.requestId];
  challengeResults_.erase(req.requestId);
  return true;
}

void HostUnlockRelay::SealedAsync(const ControlUnlockSealedRequestMessage& req,
                                  uint64_t clientSessionCookie) {
  EnsureWorker();
  Command cmd;
  cmd.op = Op::Sealed;
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
    // A repeat sealed request for a requestId already resolved terminally is a no-op (idempotent).
    auto it = sealedResults_.find(req.requestId);
    if (it != sealedResults_.end() && it->second.terminal) return;
    queue_.push_back(cmd);
  }
  cv_.notify_all();
}

bool HostUnlockRelay::PollResult(uint32_t requestId, ControlUnlockStatusResultMessage* out) {
  if (!out) return false;
  std::lock_guard<std::mutex> lk(mu_);
  auto it = sealedResults_.find(requestId);
  if (it == sealedResults_.end()) return false;
  *out = it->second;
  return true;
}

}  // namespace remote60::native_poc

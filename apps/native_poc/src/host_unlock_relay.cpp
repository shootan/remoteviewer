#include "host_unlock_relay.hpp"

#include <windows.h>

#include <chrono>
#include <cstring>

#include "secure_unlock_ipc.hpp"

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
    // Unblock a worker parked in ConnectNamedPipe/ReadFile so join() does not hang. (Codex #370 B4.)
    HANDLE p = static_cast<HANDLE>(pipeHandle_.load(std::memory_order_acquire));
    if (p != nullptr && p != INVALID_HANDLE_VALUE) (void)CancelIoEx(p, nullptr);
    if (worker_.joinable()) worker_.join();
  }
}

void HostUnlockRelay::EnsureWorker() {
  bool expected = false;
  if (running_.compare_exchange_strong(expected, true)) {
    worker_ = std::thread([this] { WorkerLoop(); });
  }
}

void* HostUnlockRelay::ConnectPipe() {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  do {
    (void)WaitNamedPipeW(kSecureUnlockPipeName, 100);
    HANDLE pipe = CreateFileW(kSecureUnlockPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) {
      DWORD mode = PIPE_READMODE_MESSAGE;
      (void)SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
      return pipe;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  } while (std::chrono::steady_clock::now() < deadline && running_.load());
  return INVALID_HANDLE_VALUE;
}

void HostUnlockRelay::WorkerLoop() {
  HANDLE pipe = INVALID_HANDLE_VALUE;
  auto ensure_pipe = [&]() -> bool {
    if (pipe != INVALID_HANDLE_VALUE) return true;
    pipe = static_cast<HANDLE>(ConnectPipe());
    pipeHandle_.store(pipe == INVALID_HANDLE_VALUE ? nullptr : pipe, std::memory_order_release);
    return pipe != INVALID_HANDLE_VALUE;
  };
  auto drop_pipe = [&]() {
    if (pipe != INVALID_HANDLE_VALUE) {
      pipeHandle_.store(nullptr, std::memory_order_release);
      CloseHandle(pipe);
      pipe = INVALID_HANDLE_VALUE;
    }
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
      if (cmd.sealed) {
        ControlUnlockStatusResultMessage r{};
        r.requestId = cmd.seal.requestId;
        r.stage = static_cast<uint16_t>(UnlockStage::InternalError);
        r.terminal = 1;
        sealedResults_[cmd.seal.requestId] = r;
      } else {
        ControlUnlockChallengeMessage m{};
        m.requestId = cmd.challenge.requestId;
        m.status = static_cast<uint16_t>(UnlockStage::InternalError);
        challengeResults_[cmd.challenge.requestId] = m;
      }
      cv_.notify_all();
      continue;
    }

    if (!cmd.sealed) {
      SecureUnlockChallengeRequest req{};
      req.requestId = cmd.challenge.requestId;
      req.clientSessionCookie = cmd.cookie;
      uint8_t buf[512];
      DWORD read = 0;
      bool ok = write_all(pipe, &req, sizeof(req)) &&
                ReadFile(pipe, buf, sizeof(buf), &read, nullptr) &&
                read == sizeof(SecureUnlockChallengeResponse);
      ControlUnlockChallengeMessage out{};
      if (ok) {
        SecureUnlockChallengeResponse resp{};
        std::memcpy(&resp, buf, sizeof(resp));
        out = to_control_challenge(resp);
      } else {
        out.requestId = cmd.challenge.requestId;
        out.status = static_cast<uint16_t>(UnlockStage::InternalError);
        drop_pipe();
      }
      std::lock_guard<std::mutex> lk(mu_);
      challengeResults_[cmd.challenge.requestId] = out;
      cv_.notify_all();
    } else {
      uint8_t buf[512];
      DWORD read = 0;
      bool ok = write_all(pipe, &cmd.seal_ipc, sizeof(cmd.seal_ipc)) &&
                ReadFile(pipe, buf, sizeof(buf), &read, nullptr) &&
                read == sizeof(SecureUnlockResult);
      ControlUnlockStatusResultMessage out{};
      if (ok) {
        SecureUnlockResult resp{};
        std::memcpy(&resp, buf, sizeof(resp));
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

bool HostUnlockRelay::ChallengeSync(const ControlUnlockChallengeRequestMessage& req,
                                    uint64_t clientSessionCookie, ControlUnlockChallengeMessage* out,
                                    uint32_t timeoutMs) {
  if (!out) return false;
  EnsureWorker();
  {
    std::lock_guard<std::mutex> lk(mu_);
    Command cmd;
    cmd.sealed = false;
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
  cmd.sealed = true;
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

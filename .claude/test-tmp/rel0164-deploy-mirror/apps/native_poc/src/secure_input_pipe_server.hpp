#pragma once

// The secure-input pipe's server: more than one client at a time (prelogin-ui r14, M1).
//
// The service served ONE client at a time on a one-instance pipe. With fast user switching, the elevated
// GNLinkStream of a user whose session is now disconnected keeps its connection; the console's pre-login
// worker (or the new user's host) then found the pipe busy, waited ~3 s in its broker on every input --
// on the UDP reader thread -- and the input was lost. Here every client is served on its own thread, each
// with what was learned about it at connect (its requester session), and the one agent behind them is
// shared under the caller's lock.
//
// Lifetime (r15, R14-1): every wait -- the listening instance's connect and each connection's read -- is an
// OVERLAPPED operation waited on together with the registry's stop event, a MANUAL-reset event that, once
// set, stays set. r14 used synchronous ConnectNamedPipe/ReadFile and cancelled the I/O pending at the moment
// of Stop: a connect STARTED just after that (between the check and the call, or after an Add that came
// after the Stop) waited for a client for ever. Now a wait that begins after Stop returns at once, and one
// already waiting is woken; the I/O is then cancelled and waited out before its OVERLAPPED goes. The server
// joins every connection thread before it returns; each connection's thread alone closes its handle.

#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace remote60::native_poc {

// r20 merge (Codex codex_prelogin_r20_trust.md Q2): every instance of the name carries the same small cap --
// 8 in all = at most 7 connections being served + the 1 listener kept in reserve. At the cap a further client
// is refused on the listener (Disconnect, re-armed); the server and the connections already served go on.
constexpr DWORD kSecureInputPipeMaxInstances = 8;
constexpr size_t kSecureInputPipeMaxServed = kSecureInputPipeMaxInstances - 1;
// Consecutive failures to create the next listener before serving is stopped on purpose (each one refuses
// the client it would have served and keeps the listener; the next attempt waits for the next client).
constexpr int kSecureInputPipeListenerCreateTries = 3;

// One instance of the secure-input pipe (the service's create_secure_pipe passes its name and SDDL): inbound,
// message mode, local clients only, overlapped (r15) -- and as many instances as there are clients (r14 M1).
// secure-pipe-identity (r20 merge): the server's FIRST instance is created with FILE_FLAG_FIRST_PIPE_INSTANCE --
// if a local process squatted the name while the service was stopped, the create fails and the service does
// not join the squatter's name (fail closed). Later instances join the service's own pipe, whose SY/BA DACL
// lets no one else add an instance; RunMultiClientPipeServer keeps at least one instance open throughout.
inline HANDLE CreateSecureInputPipeInstance(const wchar_t* name, const wchar_t* sddl, bool firstInstance) {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, nullptr)) {
    return INVALID_HANDLE_VALUE;
  }
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.lpSecurityDescriptor = descriptor;
  HANDLE pipe = CreateNamedPipeW(name, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | (firstInstance ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                                 PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                 kSecureInputPipeMaxInstances, 0, 64 * 1024, 0, &security);
  const DWORD error = GetLastError();  // r20: the caller reports why (LocalFree may overwrite it)
  LocalFree(descriptor);
  SetLastError(error);
  return pipe;
}

class PipeConnectionRegistry {
 public:
  PipeConnectionRegistry() : stop_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
  ~PipeConnectionRegistry() {
    if (stop_) CloseHandle(stop_);
  }
  PipeConnectionRegistry(const PipeConnectionRegistry&) = delete;
  PipeConnectionRegistry& operator=(const PipeConnectionRegistry&) = delete;

  void Add(HANDLE h) {
    std::lock_guard<std::mutex> l(mu_);
    pipes_.push_back(h);
  }
  void Remove(HANDLE h) {
    std::lock_guard<std::mutex> l(mu_);
    pipes_.erase(std::remove(pipes_.begin(), pipes_.end(), h), pipes_.end());
  }
  // From any thread (the SCM's STOP handler): every wait ends, now or whenever it would begin.
  void Stop() {
    if (stop_) SetEvent(stop_);
  }
  bool stopped() const { return !stop_ || WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0; }
  HANDLE stopEvent() const { return stop_; }
  size_t size() const {
    std::lock_guard<std::mutex> l(mu_);
    return pipes_.size();
  }
  // Tests: run between the listening instance's registration and its connect.
  std::function<void()> beforeConnect;

 private:
  mutable std::mutex mu_;
  std::vector<HANDLE> pipes_;
  HANDLE stop_;
};

// An overlapped operation on `h`, waited on together with `stop`. True when it completed with success;
// on stop (or failure) the operation is cancelled and waited out before returning false.
template <typename Start>
bool RunPipeIo(HANDLE h, HANDLE stop, Start start, DWORD* transferred = nullptr) {
  OVERLAPPED ov{};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ov.hEvent) return false;
  DWORD n = 0;
  bool ok = false;
  const BOOL began = start(&ov);
  const DWORD err = began ? ERROR_SUCCESS : GetLastError();
  if (began) {
    ok = GetOverlappedResult(h, &ov, &n, FALSE) != FALSE;
  } else if (err == ERROR_PIPE_CONNECTED) {
    ok = true;  // a client connected between CreateNamedPipe and ConnectNamedPipe
  } else if (err == ERROR_IO_PENDING) {
    const HANDLE waits[2] = {ov.hEvent, stop};
    const DWORD w = WaitForMultipleObjects(stop ? 2 : 1, waits, FALSE, INFINITE);
    if (w == WAIT_OBJECT_0) {
      ok = GetOverlappedResult(h, &ov, &n, FALSE) != FALSE;
    } else {
      (void)CancelIoEx(h, &ov);
      (void)GetOverlappedResult(h, &ov, &n, TRUE);  // the cancel is only a request: wait for the completion
      ok = false;
    }
  }
  CloseHandle(ov.hEvent);
  if (transferred) *transferred = n;
  return ok;
}

// One whole message of `bytes` from a connection (the service's read of a SecureInputMessage); false when
// the client went, the message is shorter, or the server is stopping.
inline bool ReadPipeMessage(HANDLE pipe, void* data, DWORD bytes, HANDLE stop) {
  auto* cursor = static_cast<uint8_t*>(data);
  DWORD total = 0;
  while (total < bytes) {
    DWORD got = 0;
    if (!RunPipeIo(pipe, stop,
                   [&](OVERLAPPED* ov) { return ReadFile(pipe, cursor + total, bytes - total, nullptr, ov); }, &got) ||
        got == 0) {
      return false;
    }
    total += got;
  }
  return true;
}

// Accepts clients until `running()` is false or the registry is stopped: `createPipe(firstInstance)` makes the
// next listening instance (INVALID_HANDLE_VALUE ends the server); `serve(pipe, stopEvent)` runs on the
// connection's own thread until that client goes (its reads use ReadPipeMessage with `stopEvent`). Returns
// after every connection thread has ended.
// secure-pipe-identity (r20 merge): the name is never left without an instance of ours while the server runs --
// the next listening instance is created BEFORE a connected one is handed to its thread (which may close it at
// once), and a failed connect re-arms the SAME instance (DisconnectNamedPipe) instead of closing and recreating.
// At kSecureInputPipeMaxServed connections, or when the next listener cannot be created, the client that just
// connected is refused on the listener and the listener is kept (`note` says why); only
// kSecureInputPipeListenerCreateTries create failures in a row stop the server -- it then returns while
// `running()` still holds, which the caller treats as a serving failure. `note` may be empty.
template <typename CreatePipe, typename Serve>
void RunMultiClientPipeServer(const std::function<bool()>& running, CreatePipe createPipe, Serve serve,
                              PipeConnectionRegistry* registry, const std::function<void(const char*)>& note = {}) {
  struct Conn {
    std::thread t;
    std::shared_ptr<std::atomic<bool>> done;
  };
  std::vector<Conn> conns;
  auto reap = [&conns](bool all) {
    for (auto it = conns.begin(); it != conns.end();) {
      if (all || *it->done) {
        if (it->t.joinable()) it->t.join();
        it = conns.erase(it);
      } else {
        ++it;
      }
    }
  };
  const HANDLE stop = registry->stopEvent();
  int listenerCreateFailures = 0;
  HANDLE pipe = (running() && !registry->stopped()) ? createPipe(true) : INVALID_HANDLE_VALUE;
  if (pipe != INVALID_HANDLE_VALUE) registry->Add(pipe);
  while (pipe != INVALID_HANDLE_VALUE) {
    if (registry->beforeConnect) registry->beforeConnect();
    // r15 R14-1: the connect waits on the stop event too -- a Stop at any point before or during it ends it.
    const bool connected = !registry->stopped() && running() &&
                           RunPipeIo(pipe, stop, [pipe](OVERLAPPED* ov) { return ConnectNamedPipe(pipe, ov); });
    if (!running() || registry->stopped()) {
      (void)DisconnectNamedPipe(pipe);
      registry->Remove(pipe);
      CloseHandle(pipe);
      break;
    }
    if (!connected) {
      (void)DisconnectNamedPipe(pipe);  // r20: the same instance listens again -- no close+recreate gap
      (void)WaitForSingleObject(stop, 10);  // a connect that keeps failing is not a busy loop; Stop still ends it
      continue;
    }
    reap(false);
    if (conns.size() >= kSecureInputPipeMaxServed) {
      // r20: the cap -- this client is refused, the listener stays ours, the served ones go on.
      if (note) note("secure input pipe: connection limit reached -> client refused, listener kept");
      (void)DisconnectNamedPipe(pipe);
      continue;
    }
    // r20: the next listening instance first, then the hand-over.
    HANDLE next = createPipe(false);
    if (next == INVALID_HANDLE_VALUE) {
      // The listener is never closed to make room for a plain re-create: refuse this client, keep it.
      (void)DisconnectNamedPipe(pipe);
      if (++listenerCreateFailures >= kSecureInputPipeListenerCreateTries) {
        if (note) note("secure input pipe: next listener create failed repeatedly -> serving stopped");
        registry->Remove(pipe);
        CloseHandle(pipe);
        break;
      }
      if (note) note("secure input pipe: next listener create failed -> client refused, listener kept");
      continue;
    }
    listenerCreateFailures = 0;
    registry->Add(next);
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread t([pipe, serve, registry, done, stop] {
      serve(pipe, stop);
      (void)DisconnectNamedPipe(pipe);
      registry->Remove(pipe);
      CloseHandle(pipe);
      *done = true;
    });
    conns.push_back(Conn{std::move(t), done});
    reap(false);
    pipe = next;
  }
  registry->Stop();  // the connections still being served end too
  reap(true);
}

}  // namespace remote60::native_poc

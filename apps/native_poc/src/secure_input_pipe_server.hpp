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
// Lifetime: the registry knows every instance, listening or connected; Stop() cancels their I/O (the
// listening ConnectNamedPipe and each connection's read return) and the server joins every connection
// thread before it returns. Each connection's thread is the only one that closes its handle.

#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace remote60::native_poc {

// One instance of the secure-input pipe (the service's create_secure_pipe passes its name and SDDL): inbound,
// message mode, local clients only -- and as many instances as there are clients (r14 M1; was 1).
inline HANDLE CreateSecureInputPipeInstance(const wchar_t* name, const wchar_t* sddl) {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, nullptr)) {
    return INVALID_HANDLE_VALUE;
  }
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.lpSecurityDescriptor = descriptor;
  HANDLE pipe = CreateNamedPipeW(name, PIPE_ACCESS_INBOUND,
                                 PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                 PIPE_UNLIMITED_INSTANCES, 0, 64 * 1024, 0, &security);
  LocalFree(descriptor);
  return pipe;
}

class PipeConnectionRegistry {
 public:
  void Add(HANDLE h) {
    std::lock_guard<std::mutex> l(mu_);
    pipes_.push_back(h);
    if (stopped_) wake(h);
  }
  void Remove(HANDLE h) {
    std::lock_guard<std::mutex> l(mu_);
    pipes_.erase(std::remove(pipes_.begin(), pipes_.end(), h), pipes_.end());
  }
  // From any thread (the SCM's STOP handler): every instance's I/O ends; nobody's handle is closed here.
  void Stop() {
    std::lock_guard<std::mutex> l(mu_);
    stopped_ = true;
    for (HANDLE h : pipes_) wake(h);
  }
  bool stopped() const {
    std::lock_guard<std::mutex> l(mu_);
    return stopped_;
  }
  size_t size() const {
    std::lock_guard<std::mutex> l(mu_);
    return pipes_.size();
  }

 private:
  static void wake(HANDLE h) {
    (void)CancelIoEx(h, nullptr);
    (void)DisconnectNamedPipe(h);
  }
  mutable std::mutex mu_;
  std::vector<HANDLE> pipes_;
  bool stopped_ = false;
};

// Accepts clients until `running()` is false or the registry is stopped: `createPipe()` makes the next
// listening instance (INVALID_HANDLE_VALUE ends the server); `serve(pipe)` runs on the connection's own
// thread until that client goes. Returns after every connection thread has ended.
template <typename CreatePipe, typename Serve>
void RunMultiClientPipeServer(const std::function<bool()>& running, CreatePipe createPipe, Serve serve,
                              PipeConnectionRegistry* registry) {
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
  while (running() && !registry->stopped()) {
    HANDLE pipe = createPipe();
    if (pipe == INVALID_HANDLE_VALUE) break;
    registry->Add(pipe);
    const BOOL connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
    if (!connected || !running() || registry->stopped()) {
      registry->Remove(pipe);
      CloseHandle(pipe);
      continue;
    }
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread t([pipe, serve, registry, done] {
      serve(pipe);
      (void)DisconnectNamedPipe(pipe);
      registry->Remove(pipe);
      CloseHandle(pipe);
      *done = true;
    });
    conns.push_back(Conn{std::move(t), done});
    reap(false);
  }
  registry->Stop();  // the connections still being served end too
  reap(true);
}

}  // namespace remote60::native_poc

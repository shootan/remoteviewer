#pragma once

// Client session sockets / directory / epoch state (SocketCloser, SessionState).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "directory_client.hpp"
#include "native_socket.hpp"
#include "udp_control_channel.hpp"

namespace remote60::native_poc {

// main() returns from many places; a destructor is the only way to close these on every path.
struct SocketCloser {
  SOCKET* handle;
  ~SocketCloser() {
    if (handle && *handle != INVALID_SOCKET) {
      closesocket(*handle);
      *handle = INVALID_SOCKET;
    }
  }
};

// Client session plumbing (Phase 1-1 state struct): the media/control sockets and their
// lifetime closers, the directory agent + per-session directory authentication, the session
// epoch (bumped on every new client so stale work is fenced) with its wait, the control-ready
// handshake epoch, the stream-active flag, and the control/reader thread handles.
// thread: sockets are created on the main thread before the threads start; clientSock/lanSock/
// retiredSock swap on the main thread only (the reader thread reads the handle it was given);
// directoryAuth* under directoryAuthMu (reader + control); epoch/controlReadyEpoch/
// streamControlActive are the cross-thread atomics (control/reader -> main).
  // Which of the three things a Hello can be. The caller needs the distinction because a first
  // Hello and its retransmissions are indistinguishable at the endpoint level -- and, behind a
  // relay, so are two entirely different clients.
  enum class DirectoryHello { Rejected, Retransmit, NewSession };

struct SessionState {
  // Directory service credentials (args or REMOTE60_DIRECTORY_* env) and the agent.
  std::string directoryUrl;
  std::string directoryId;
  std::string directoryPw;
  remote60::native_poc::directory::HostAgent directoryAgent;
  // Media sockets. lanSock is the legacy-port listener; retiredSock holds whichever socket the
  // handshake did not choose but that still has an owner (the directory agent keeps
  // heartbeating on it).
  SOCKET listenSock = INVALID_SOCKET;
  SOCKET clientSock = INVALID_SOCKET;
  SOCKET lanSock = INVALID_SOCKET;
  SOCKET retiredSock = INVALID_SOCKET;
  SocketCloser lanCloser{&lanSock};
  SocketCloser retiredCloser{&retiredSock};
  // The port the media socket actually landed on (differs from args.bindPort on a fallback).
  uint16_t mediaBindPort = 0;
  // Per-session directory authentication (token + peer the directory handed us).
  std::atomic<bool> directoryAuthenticated{false};
  std::mutex directoryAuthMu;
  std::string directoryToken;
  uint32_t directoryIpNet = 0;
  // cross-thread: viewer stream state (ControlStreamState) -> main loop.
  std::atomic<bool> streamControlActive{true};
  // Control channel: TCP listener + accepted socket + its thread, or the UDP control dispatcher
  // and the reader thread that owns the UDP peer.
  SOCKET controlListenSock = INVALID_SOCKET;
  // The accepted control socket, and the mutex that makes its LIFETIME -- not just its value --
  // safe to share. An atomic only publishes the handle; it cannot stop the owning thread from
  // closing it between another thread's load and that thread's next Winsock call, and a SOCKET
  // value is reusable the instant it closes. Every use outside the owner (currently only
  // shutdown_host's wake-up shutdown()) holds this mutex, and the owner holds it while closing.
  // (Ledger H-02, second pass.)
  std::mutex controlClientSockMu;
  SOCKET controlClientSock = INVALID_SOCKET;
  std::thread controlThread;
  UdpControlChannel udpControlChannel;
  std::thread udpControlThread;
  std::thread udpReaderThread;
  // Session epoch: bumped per new client; waited on by the main loop until control is ready.
  std::atomic<uint64_t> epoch{1};
  std::atomic<uint64_t> controlReadyEpoch{0};
  std::mutex epochMu;
  std::condition_variable epochCv;

  // --- behaviour (Phase 2-3: former main() lambdas begin_session_epoch / await_control_ready) ---
  // Open a new session epoch for a just-connected client; returns the epoch to wait on.
  uint64_t BeginEpoch() {
    SessionState& clientSession = *this;
    const uint64_t epoch = clientSession.epoch.fetch_add(1, std::memory_order_acq_rel) + 1;
    // Wakes the dispatcher out of its blocking read so it can pick the new epoch up. Reset is
    // deliberately left to that thread: doing it here would clear the queues underneath a
    // session still being served.
    clientSession.udpControlChannel.Close(remote60::native_poc::ControlCloseReason::SessionRollover);
    clientSession.epochCv.notify_all();
    return epoch;
  }
  // Block until the control channel has served that epoch (or the host stops).
  void AwaitControlReady(uint64_t epoch) {
    SessionState& clientSession = *this;
    std::unique_lock<std::mutex> lock(clientSession.epochMu);
    // Bounded: if the dispatcher cannot come back we answer the client anyway, because a session
    // with video and no window list still beats one that never starts.
    clientSession.epochCv.wait_for(lock, std::chrono::milliseconds(1500), [&] {
      return clientSession.controlReadyEpoch.load(std::memory_order_acquire) >= epoch ||
             clientSession.epoch.load(std::memory_order_acquire) > epoch;
    });
  }

  // --- directory Hello classification (Phase 2-12: former main() lambdas classify_directory_hello /
  // authorize_directory_session) ---
  DirectoryHello ClassifyDirectoryHello(const std::string& token, const sockaddr_in& peer) {
    SessionState& clientSession = *this;
    if (token.empty()) return DirectoryHello::Rejected;
    {
      std::lock_guard<std::mutex> lock(clientSession.directoryAuthMu);
      if (!clientSession.directoryToken.empty() && token == clientSession.directoryToken &&
          peer.sin_addr.s_addr == clientSession.directoryIpNet) {
        // A controller reconnect creates a new UDP socket/port. The already-proven opaque
        // capability remains the session credential, while the first authenticated source IP
        // (which can differ from the directory-observed endpoint under hairpin NAT) stays bound.
        // This is also what makes retransmission safe: the capability itself is single-use, so
        // without the cache the client's second Hello would be refused.
        return DirectoryHello::Retransmit;
      }
    }
    if (!clientSession.directoryAgent.AuthorizePeer(token, peer)) return DirectoryHello::Rejected;
    {
      std::lock_guard<std::mutex> lock(clientSession.directoryAuthMu);
      clientSession.directoryToken = token;
      clientSession.directoryIpNet = peer.sin_addr.s_addr;
    }
    return DirectoryHello::NewSession;
  }
  bool AuthorizeDirectorySession(const std::string& token, const sockaddr_in& peer) {
    return ClassifyDirectoryHello(token, peer) != DirectoryHello::Rejected;
  }
};

}  // namespace remote60::native_poc

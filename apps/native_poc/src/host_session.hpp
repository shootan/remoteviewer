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
  std::atomic<SOCKET> controlClientSock{INVALID_SOCKET};
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
};

}  // namespace remote60::native_poc

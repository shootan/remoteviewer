#pragma once

// Host startup / shutdown sequence: the former linear blocks of main(), one function each, in call order.
//
// Role:    Everything main() did before the loop (env config, transport, sockets + handshake, control
//          threads, D3D/WGC/MF bring-up, capture target, encode geometry, encoder init, watchdogs,
//          readback pipeline, capture start) and after it (shutdown). Functions returning int give
//          the exit code main() used to return at that point (0 = go on).
// Thread:  main thread; the start_* functions spawn the control / reader / watchdog threads.
// Input:   HostContext (the state structs main() declares first), plus the few objects the loop never
//          sees (ControlSessionServer, the DXGI watchdog thread + flag); WinsockScope stays in main().
// Output:  configured state, running threads, exit codes.
// Callers: native_video_host_main.cpp only.
//
// Host split refactor Phase 2-12 (HostRuntime assembly): bodies moved verbatim from main().

#include <atomic>
#include <thread>

#include "host_args.hpp"
#include "host_control_session.hpp"
#include "host_main_loop.hpp"
#include "native_video_transport.hpp"

namespace remote60::native_poc {

// Stops and joins the DXGI worker watchdog thread. Declared in main() right after the thread, so it
// runs before res.dxgiCaptureSession (whose progress block the thread reads) is destroyed.
struct DxgiWatchdogJoiner {
  std::atomic<bool>* stopFlag;
  std::thread* th;
  ~DxgiWatchdogJoiner() {
    stopFlag->store(true, std::memory_order_release);
    if (th->joinable()) th->join();
  }
};

void startup_process_setup();                          // log timestamp prefix, latency priority
int startup_configure_from_env(HostContext& hx);       // REMOTE60_NATIVE_* env -> state structs; codec check (11)
int resolve_transport(const Args& args, bool useRaw, bool useH264, VideoTransport& transport);  // (15, 16)
void startup_log_config(HostContext& hx);              // "waiting client" / pacing / input-injection lines
void startup_configure_session(HostContext& hx);       // directory credentials, media bind port
int startup_connect_client(HostContext& hx);           // TCP accept or UDP bind + Hello handshake (2..5)
void startup_configure_control_state(HostContext& hx); // backend request, key-request bucket, input target
void startup_start_control_threads(HostContext& hx, ControlSessionServer& controlServer);
int startup_init_graphics(HostContext& hx);            // WinRT apartment, WGC check (6), MFStartup (12), D3D (7)
int startup_select_capture_target(HostContext& hx);    // window criteria, monitor, capture item, size (8, 9)
void startup_configure_encode_geometry(HostContext& hx);  // encode size, ABR / M9 ladders, frame intervals
int startup_init_encoder(HostContext& hx);             // H.264 encoder init (13), WinRT D3D device
void startup_start_dxgi_watchdog(HostContext& hx, std::atomic<bool>& dxgiWatchdogStop,
                                 std::thread& dxgiWorkerWatchdog);
int startup_create_readback(HostContext& hx);          // publish callback, readback pipeline (10)
int startup_start_capture(HostContext& hx);            // capture session start (10), timing / stats anchors, sender thread
void startup_start_main_loop_watchdog(HostContext& hx, MainLoopWatchdogThread& mainLoopWatchdog);
void shutdown_host(HostContext& hx);                   // stop threads, detach capture, close sockets, MFShutdown

}  // namespace remote60::native_poc

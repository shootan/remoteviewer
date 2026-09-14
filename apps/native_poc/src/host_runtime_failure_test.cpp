// Exercise the actual host main/unwind and real control workers. The connection/graphics
// providers are isolated ports: no directory, capture, input service or installed product is used.
#include "native_socket.hpp"
#include "host_startup.hpp"
#include <cstdio>

namespace remote60::native_poc {
int recoveryTestMode = 0;
int recovery_test_connect(HostContext& host) {
  if (host.transport != VideoTransport::Udp) return 72;
  host.clientSession.clientSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (host.clientSession.clientSock == INVALID_SOCKET ||
      bind(host.clientSession.clientSock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) return 72;
  set_recv_timeout(host.clientSession.clientSock, 25);
  return 0;
}
int recovery_test_graphics(HostContext& host) {
  if (!host.clientSession.udpReaderThread.joinable() || !host.clientSession.udpControlThread.joinable()) return 74;
  if (recoveryTestMode == 1) Sleep(INFINITE);
  if (recoveryTestMode == 2) host.sender.thread = std::thread([] { Sleep(INFINITE); });
  host.capture.windowModeActive.store(true);
  host.capture.targetHwnd.store(0);  // a vanished explicit target, before any graphics provider
  if (restart_capture_session(host) || !host.capture.restartPending || !host.capture.restartRetryAtUs)
    return 75;
  return 73;  // failure after the real control workers were created
}
}
#define startup_connect_client recovery_test_connect
#define startup_init_graphics recovery_test_graphics
#define main recovery_product_host_entry
#include "native_video_host_main.cpp"
#undef main
#undef startup_connect_client
#undef startup_init_graphics

int main(int argc, char** argv) {
  remote60::native_poc::recoveryTestMode = argc > 1 ? (std::string(argv[1]) == "shutdown" ? 2 : 1) : 0;
  _putenv_s("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", "1");
  char exe[]="fixture", transport[]="--transport", udp[]="udp", codec[]="--codec", h264[]="h264";
  char control[]="--control-port", zero[]="0";
  char* args[]={exe, transport, udp, codec, h264, control, zero};
  const uint64_t started = GetTickCount64();
  const int rc = recovery_product_host_entry(7,args);
  const bool ok = rc == 73 && GetTickCount64() - started < 3000;
  std::printf("host_runtime_failure_test: %s rc=%d (real control threads, injected graphics failure)\n",
              ok ? "PASS" : "FAIL",rc);
  return ok ? 0 : 1;
}

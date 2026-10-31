// Audit probe (untracked): does the log uploader spin after a 401 once the queue holds >= batchMaxBytes?
// Links the PRODUCT log_upload.cpp + directory_client.cpp against a loopback server that answers 401.
// LOCALAPPDATA is redirected so the product diag file is written under .claude/probe/tmp_localappdata.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "log_upload.hpp"

using remote60::native_poc::LogUploadConfig;
using remote60::native_poc::log_upload_configure;
using remote60::native_poc::log_upload_enqueue;
using remote60::native_poc::log_upload_status;
using remote60::native_poc::log_upload_stop;

namespace {

struct Server401 {
  SOCKET listenSock = INVALID_SOCKET;
  uint16_t port = 0;
  std::thread th;
  std::atomic<int> requests{0};
  bool Start() {
    listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listenSock, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) return false;
    if (listen(listenSock, 16) != 0) return false;
    int len = sizeof(a);
    getsockname(listenSock, reinterpret_cast<sockaddr*>(&a), &len);
    port = ntohs(a.sin_port);
    th = std::thread([this] {
      for (;;) {
        SOCKET c = accept(listenSock, nullptr, nullptr);
        if (c == INVALID_SOCKET) return;
        std::string raw;
        char buf[4096];
        size_t headerEnd = std::string::npos, contentLength = 0;
        for (;;) {
          const int n = recv(c, buf, sizeof(buf), 0);
          if (n <= 0) break;
          raw.append(buf, static_cast<size_t>(n));
          if (headerEnd == std::string::npos) {
            headerEnd = raw.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
              const std::string h = raw.substr(0, headerEnd + 2);
              size_t p = h.find("Content-Length:");
              if (p != std::string::npos) contentLength = std::strtoul(h.c_str() + p + 15, nullptr, 10);
            }
          }
          if (headerEnd != std::string::npos && raw.size() >= headerEnd + 4 + contentLength) break;
        }
        ++requests;
        const std::string resp = "HTTP/1.1 401 X\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
        send(c, resp.data(), static_cast<int>(resp.size()), 0);
        shutdown(c, SD_BOTH);
        closesocket(c);
      }
    });
    return true;
  }
  void Stop() {
    if (listenSock != INVALID_SOCKET) { closesocket(listenSock); listenSock = INVALID_SOCKET; }
    if (th.joinable()) th.join();
  }
};

double process_cpu_seconds() {
  FILETIME c, e, k, u;
  GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
  auto to100ns = [](const FILETIME& f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
  return static_cast<double>(to100ns(k) + to100ns(u)) / 1e7;
}

uint64_t file_size(const std::string& p) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  return f ? static_cast<uint64_t>(f.tellg()) : 0;
}

void measure(const char* label, const std::string& diagPath, int seconds) {
  const double cpu0 = process_cpu_seconds();
  const uint64_t size0 = file_size(diagPath);
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
  const double cpu1 = process_cpu_seconds();
  const uint64_t size1 = file_size(diagPath);
  std::printf("%s: %d s window -> process CPU %.2f s (%.0f%% of one core), diag grew %llu bytes (%.1f KB/s)\n",
              label, seconds, cpu1 - cpu0, (cpu1 - cpu0) / seconds * 100.0,
              static_cast<unsigned long long>(size1 - size0), (size1 - size0) / 1024.0 / seconds);
}

}  // namespace

int main(int argc, char** argv) {
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);
  const std::string local = argc > 1 ? argv[1] : "D:\\remote\\remote\\.claude\\probe\\tmp_localappdata";
  SetEnvironmentVariableA("LOCALAPPDATA", local.c_str());
  _putenv_s("LOCALAPPDATA", local.c_str());
  _putenv_s("REMOTE60_LOG_UPLOAD", "");
  CreateDirectoryA((local + "\\GNLink").c_str(), nullptr);
  const std::string diagPath = local + "\\GNLink\\log_upload.diag";

  Server401 server;
  if (!server.Start()) { std::printf("server start failed\n"); return 2; }

  LogUploadConfig c;
  c.directoryUrl = "http://127.0.0.1:" + std::to_string(server.port);
  c.hostToken = "PROBE-TOKEN";
  c.device = "probe-device";
  c.identity = "probe-account/probe-device";
  c.flushIntervalMs = 2000;   // product default
  const size_t batchMax = argc > 2 ? static_cast<size_t>(std::atoi(argv[2])) : (192u * 1024u);  // product default 192 KB
  c.batchMaxBytes = batchMax;
  std::string reason;
  if (!log_upload_configure(c, &reason)) { std::printf("configure failed: %s\n", reason.c_str()); return 3; }
  std::printf("configured: %s (batchMaxBytes=%zu flushIntervalMs=%u)\n", reason.c_str(), batchMax, c.flushIntervalMs);

  // One line -> first batch -> 401 -> paused.
  log_upload_enqueue("host", "line before 401");
  for (int i = 0; i < 600; ++i) {
    if (log_upload_status().authRejected) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  auto st = log_upload_status();
  std::printf("after first send: authRejected=%d lastStatus=%u held=%llu serverRequests=%d\n",
              st.authRejected ? 1 : 0, st.lastStatus, static_cast<unsigned long long>(st.heldBatches), server.requests.load());
  if (!st.authRejected) { std::printf("401 pause did not engage; abort\n"); return 4; }

  // Control window: paused with a small queue (< batchMaxBytes).
  log_upload_enqueue("host", "one small line while paused");
  measure("A) paused, queue < batchMaxBytes", diagPath, 3);

  // Fill the queue past batchMaxBytes while still paused (what a live host does within seconds).
  const std::string line(200, 'x');
  size_t queued = 0;
  while (queued < batchMax + 4096) { log_upload_enqueue("host", line); queued += line.size() + 1; }
  std::printf("enqueued ~%zu bytes while paused (server requests so far %d)\n", queued, server.requests.load());
  measure("B) paused, queue >= batchMaxBytes", diagPath, 3);
  st = log_upload_status();
  std::printf("status: authRejected=%d sent=%llu failed=%llu held=%llu dropped=%llu serverRequests=%d\n",
              st.authRejected ? 1 : 0, static_cast<unsigned long long>(st.sentBatches),
              static_cast<unsigned long long>(st.failedSends), static_cast<unsigned long long>(st.heldBatches),
              static_cast<unsigned long long>(st.droppedLines), server.requests.load());

  // Count the diag lines the window produced.
  {
    std::ifstream f(diagPath);
    std::string l; uint64_t idle = 0, total = 0;
    while (std::getline(f, l)) { ++total; if (l.find("idle alive") != std::string::npos) ++idle; }
    std::printf("diag lines total=%llu idle-alive=%llu\n", static_cast<unsigned long long>(total), static_cast<unsigned long long>(idle));
  }
  log_upload_stop();
  server.Stop();
  WSACleanup();
  return 0;
}

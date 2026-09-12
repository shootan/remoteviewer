#include "winhttp_transport.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const uint16_t port = static_cast<uint16_t>(std::strtoul(argv[1], nullptr, 10));
  remote60::native_poc::net::HttpResult result;
  int failures = 0;
  auto check = [&](bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label); if (!ok) ++failures;
  };
  auto request = [&](const char* route, uint32_t timeout) {
    return remote60::native_poc::net::http_exchange("127.0.0.1", port, false, "GET", route, {}, {},
                                                   nullptr, timeout, &result);
  };
  check(request("/ok", 2000) && result.status == 200 && result.body == "{\"ok\":true}",
        "healthy WinHTTP body succeeds unchanged");
  check(!request("/partial", 2000) && result.status == 0 && result.body.empty(),
        "truncated HTTP200 is failure, not a partial success");
  const auto start = std::chrono::steady_clock::now();
  const bool slow = request("/slow", 350);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
  check(!slow && elapsed < 1500 && result.body.empty(), "drip-fed response cannot reset the body deadline");
  std::printf("winhttp_recovery_test: %s elapsedMs=%lld\n", failures ? "FAIL" : "PASS", static_cast<long long>(elapsed));
  return failures ? 1 : 0;
}

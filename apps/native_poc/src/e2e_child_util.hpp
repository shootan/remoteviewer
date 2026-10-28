// A test's own ports and children (2026-10-28, Codex ba2eaf2 SOURCE F, r6). Header-only; its negative controls are in
// e2e_guard_runtime_test.cpp.
//   * e2e_free_loopback_udp_port: a loopback UDP port nobody holds, or 0 -- socket, bind and getsockname each checked.
//   * e2e_port_or_invalid: the port, or the run ends INVALID before any child of it exists. A 0 must never reach a
//     host: its --bind-port 0 leaves the candidate list empty and it falls back to the product's 43000.
//   * e2e_end_child_confirmed: a child ended through this test's own handle -- true only when a bounded wait SAW it
//     end. An end not confirmed is said as such; nothing is called "ended" on the strength of the request alone.
#ifndef REMOTE60_NATIVE_POC_E2E_CHILD_UTIL_HPP
#define REMOTE60_NATIVE_POC_E2E_CHILD_UTIL_HPP

#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <set>
#include <string>

namespace remote60::native_poc::e2e {

inline uint16_t e2e_free_loopback_udp_port() {
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) return 0;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  uint16_t port = 0;
  int n = sizeof(a);
  if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0 &&
      getsockname(s, reinterpret_cast<sockaddr*>(&a), &n) == 0) {
    port = ntohs(a.sin_port);
  }
  closesocket(s);
  return port;
}

inline uint16_t e2e_port_or_invalid(const char* where, const std::function<uint16_t()>& pick = e2e_free_loopback_udp_port) {
  const uint16_t port = pick();
  if (port != 0) return port;
  std::printf("FAIL  SETUP %s: no loopback port could be secured -- not started (a 0 would fall back to the product's "
              "port)\nRESULT: INVALID\n", where);
  std::fflush(stdout);
  std::_Exit(2);
}

inline bool e2e_end_child_confirmed(HANDLE process, UINT code, DWORD waitMs = 10000) {
  if (!process) return false;
  if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) return true;  // already ended (a process handle signalled)
  const BOOL asked = TerminateProcess(process, code);
  if (WaitForSingleObject(process, waitMs) == WAIT_OBJECT_0) return true;
  std::printf("FAIL  a child of this test: terminate %s, its end NOT confirmed within %lu ms\n", asked ? "asked" : "failed",
              static_cast<unsigned long>(waitMs));
  std::fflush(stdout);
  return false;
}

// r7 N2: a safety contradiction (or an injection-gate refusal) ends the WHOLE test: no further launch, nothing of
// this run deleted, RESULT: INVALID, exit 2. `keep` holds the cases whose child's end was not confirmed (said as such).
[[noreturn]] inline void e2e_case_invalid(const std::wstring& runFolder, const std::set<std::wstring>& keep,
                                          const std::wstring& tag, const std::string& why) {
  std::printf("FAIL  RUN %ls: %s -- its child's end %s; no further launch; this run's logs are kept in %ls\n"
              "RESULT: INVALID\n",
              tag.c_str(), why.c_str(), keep.count(tag) ? "NOT confirmed" : "confirmed", runFolder.c_str());
  std::fflush(stdout);
  std::_Exit(2);
}

}  // namespace remote60::native_poc::e2e

#endif

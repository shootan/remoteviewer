// Signs in to a real directory and lists the PCs on the account.
//
// The parsing is unit-tested against captured replies, but that proves nothing about whether the
// requests are shaped the way the server expects -- a missing header or a wrong path fails the
// same way a wrong password does. This exercises the actual exchange, which is the only place
// that question gets answered.
//
//   DirectoryLoginProbe --url http://host:8080 --id <account> --pw <password>

#include <cstdio>
#include <string>

#include "directory_session_client.hpp"
#include "native_socket.hpp"

using namespace remote60::native_poc;

int main(int argc, char** argv) {
  std::string url = "http://223.130.132.180:8080";
  std::string accountId;
  std::string password;
  for (int i = 1; i < argc; ++i) {
    const std::string key = argv[i];
    if (key == "--url" && i + 1 < argc) url = argv[++i];
    else if (key == "--id" && i + 1 < argc) accountId = argv[++i];
    else if (key == "--pw" && i + 1 < argc) password = argv[++i];
  }
  if (accountId.empty() || password.empty()) {
    std::printf("usage: DirectoryLoginProbe --url <http://host:port> --id <account> --pw <password>\n");
    return 2;
  }

  std::string error;
  if (!initialize_sockets(&error)) {
    std::printf("FAIL sockets: %s\n", error.c_str());
    return 1;
  }

  std::string session;
  if (!directory_login(url, accountId, password, &session, &error)) {
    std::printf("FAIL login: %s\n", error.c_str());
    return 1;
  }
  // Length only. The token authorises everything else on the account.
  std::printf("ok login: session token received (%zu chars)\n", session.size());

  std::vector<DirectoryHostEntry> hosts;
  if (!directory_list_hosts(url, session, &hosts, &error)) {
    std::printf("FAIL hosts: %s\n", error.c_str());
    return 1;
  }
  std::printf("ok hosts: %zu\n", hosts.size());
  for (const auto& host : hosts) {
    std::printf("   %-20s %-8s id=%s\n", host.hostName.c_str(),
                host.online ? "online" : "offline", host.hostId.c_str());
  }

  // Deliberately stops short of /api/connect: that queues a capability and wakes the host, which
  // would disturb a session someone is actually using.
  std::printf("\n(connect not attempted; it would poke a live host)\n");
  return 0;
}

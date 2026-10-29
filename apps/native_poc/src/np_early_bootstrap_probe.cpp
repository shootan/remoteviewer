// np-impl r1 TEST TOOL, never shipped: runs the product's directory_session_open -- observe, /api/connect,
// the candidate race -- against a real (test) directory server and prints what it chose, so
// apps/directory/test/np_early_bootstrap_e2e.js can check the whole path across new/old server x
// new/old viewer. The race and its log lines are the product's own; this only supplies the request.
//
//   remote60_np_early_bootstrap_probe <url> <account> <password> <hostId> <relayEarly 0|1>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "directory_session_bootstrap.hpp"
#include "directory_session_client.hpp"
#include "native_socket.hpp"

int main(int argc, char** argv) {
  if (argc != 6) {
    std::fprintf(stderr, "usage: %s <url> <account> <password> <hostId> <relayEarly 0|1>\n", argv[0]);
    return 2;
  }
  std::string err;
  if (!remote60::native_poc::initialize_sockets(&err)) {
    std::fprintf(stderr, "socket init failed: %s\n", err.c_str());
    return 2;
  }
  std::string session;
  remote60::native_poc::directory::ObserveEndpoint advertised;
  if (!remote60::native_poc::directory_login(argv[1], argv[2], argv[3], &session, &err, &advertised)) {
    std::fprintf(stderr, "login failed: %s\n", err.c_str());
    return 3;
  }
  remote60::native_poc::DirectorySessionRequest request{};
  request.url = argv[1];
  request.sessionToken = session;
  request.hostId = argv[4];
  request.advertised = advertised;
  request.relayEarly = std::string(argv[5]) == "1";
  remote60::native_poc::DirectorySessionResult result{};
  if (!remote60::native_poc::directory_session_open(request, &result, &err)) {
    std::cout << "PROBE open=0 error=" << err << std::endl;
    return 4;
  }
  std::cout << "PROBE open=1 answered=" << (result.answered ? 1 : 0) << " chosen=" << result.chosen.ip << ":"
            << result.chosen.port << " kind=" << remote60::native_poc::candidate_kind_name(result.chosen.kind)
            << " relay=" << (result.relay ? 1 : 0) << std::endl;
  remote60::native_poc::close_socket(&result.socket);
  return 0;
}

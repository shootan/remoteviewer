// prelogin-field D4: the directory status GNLinkHost's window keeps through a viewer's connect
// (host_directory_status.hpp). Sequences of the streaming host's own lines, fed the way the window's reader
// feeds them; the badge rule is the window's (status beginning "online" = REACHABLE).

#include <cstdio>
#include <string>
#include <vector>

#include "host_directory_status.hpp"

namespace n = remote60::native_poc;

namespace {
int gChecks = 0, gFailures = 0;
void check(const std::string& what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}
// What the window's reader does with each full line (host_app_main.cpp).
std::string feed(std::string status, const std::vector<std::string>& lines) {
  for (const auto& line : lines) {
    const size_t marker = line.find("directory ");
    if (marker == std::string::npos) continue;
    if (const auto next = n::NextDirectoryStatus(status, line.substr(marker + 10))) status = *next;
  }
  return status;
}
bool reachable(const std::string& s) { return s.rfind("online", 0) == 0; }
}  // namespace

int main() {
  std::printf("GNLinkHost's directory status through a viewer's connect (prelogin-field D4)\n\n");
  const std::string online = feed("starting", {"[native-video-host] directory agent started url=https://x",
                                               "[native-video-host] directory registered hostId=h-1 publicUdpPort=43000",
                                               "[native-video-host] directory online public=1.2.3.4:43000"});
  check("startup: agent started -> registered -> online = REACHABLE", reachable(online), online);

  // The 0.2.160 field sequence: online, then a viewer connects.
  const std::string afterPunch = feed(online, {"[native-video-host] directory peer punch; refreshing capability",
                                               "[native-video-host] directory punch -> 203.0.113.7:51000",
                                               "[native-video-host] directory nat-port-rewritten local=43000 public=61000",
                                               "[native-video-host] directory capability endpoint translated expected=a got=b"});
  check("a viewer connects (peer punch, punch ->, NAT, capability): still REACHABLE", reachable(afterPunch), afterPunch);

  check("a real offline line after that: NOT REACHABLE",
        !reachable(feed(afterPunch, {"[native-video-host] directory directory unreachable; asking again in 2 cycles"})));
  check("token rejected after online: no longer online (SIGN IN AGAIN is the window's next rule)",
        feed(afterPunch, {"[native-video-host] directory host token rejected; re-registering"}) ==
            "host token rejected; re-registering");
  check("disabled after online: replaces it", feed(online, {"[native-video-host] directory disabled: no token"}) == "disabled: no token");
  check("online again after an outage: REACHABLE",
        reachable(feed("directory unreachable", {"[native-video-host] directory online public=1.2.3.4:43000"})));

  // Before online nothing changed: the line still shows (the window said this before).
  check("before online a progress line still shows as before",
        feed("agent started url=x", {"[native-video-host] directory punch -> 1.2.3.4:5"}) == "punch -> 1.2.3.4:5");

  // Negative control: the old rule (every line replaces) would have lost online here.
  std::string old = online;
  for (const char* l : {"[native-video-host] directory punch -> 203.0.113.7:51000"}) {
    const std::string line = l;
    old = line.substr(line.find("directory ") + 10);
  }
  check("negative control: under the old rule the same punch line made it NOT REACHABLE", !reachable(old), old);

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

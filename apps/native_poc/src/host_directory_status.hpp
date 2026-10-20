#pragma once

// prelogin-field D4 (0.2.160 field): what GNLinkHost's window takes as the directory status from a line of its
// streaming host's output ("[native-video-host] directory <rest>").
//
// Every line carrying "directory " used to become the status. Since 27cd2f3 (July) a viewer connecting prints
// "directory punch -> <ip>:<port>", so a PC that was online showed NOT REACHABLE the moment someone connected.
// Progress lines -- they say what the agent is doing, not whether the PC can be reached -- no longer replace an
// "online" status; every other line (offline, errors, sign-in needed, disabled) still does. Before "online"
// nothing changes: the window shows what it showed before.
// Pure; host_app_main.cpp (StreamingSupervisor's reader) and host_directory_status_test.cpp.

#include <optional>
#include <string>

namespace remote60::native_poc {

// The agent's progress lines (directory_client.cpp / host_startup_connect.cpp), by how they begin.
inline bool IsDirectoryProgressLine(const std::string& rest) {
  static const char* const kProgress[] = {
      "punch -> ",                        // a viewer's punch is answered (directory_client.cpp Punch)
      "peer punch",                       // "peer punch; refreshing capability"
      "using machine credential",         // the pre-login worker's credential
      "registered hostId",                // registration done (the heartbeat says online next)
      "nat-port-rewritten",               // NAT mapping noted
      "capability endpoint translated",   // capability endpoint rewritten
      "cache path=",                      // where the cache is
  };
  for (const char* p : kProgress) {
    if (rest.rfind(p, 0) == 0) return true;
  }
  return false;
}

// The status after `rest`, or nullopt when the current one stays.
inline std::optional<std::string> NextDirectoryStatus(const std::string& current, const std::string& rest) {
  if (current.rfind("online", 0) == 0 && IsDirectoryProgressLine(rest)) return std::nullopt;
  return rest;
}

}  // namespace remote60::native_poc

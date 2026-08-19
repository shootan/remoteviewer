// Pins the contract between the interface and the client.
//
// The two sides are written in different languages and compiled at different times, so a field
// that quietly changes shape does not fail at a build step -- it fails as a blank screen. These
// checks are cheap insurance against exactly that.

#include <cstdio>
#include <string>
#include <vector>

#include "client_shell_bridge.hpp"
#include "json_profile.hpp"

using namespace remote60::native_poc;

namespace {

int gFailures = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!cond) ++gFailures;
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main() {
  std::vector<DirectoryHostEntry> hosts;
  hosts.push_back({"id-offline", "Zeta laptop", false, 0});
  hosts.push_back({"id-online-b", "Beta PC", true, 0});
  hosts.push_back({"id-online-a", "Alpha PC", true, 0});

  const std::string json = shell_hosts_json(hosts);
  // Online first, then by name. An offline host cannot be connected to, and burying the useful
  // entries under it is what makes an app feel careless.
  const size_t alpha = json.find("Alpha PC");
  const size_t beta = json.find("Beta PC");
  const size_t zeta = json.find("Zeta laptop");
  check("online hosts come first", alpha < zeta && beta < zeta,
        "alpha=" + std::to_string(alpha) + " beta=" + std::to_string(beta) +
            " zeta=" + std::to_string(zeta));
  check("and are sorted by name among themselves", alpha < beta);
  check("the offline one is still listed", zeta != std::string::npos);

  // A host name is whatever someone typed into Windows, so it can contain anything.
  std::vector<DirectoryHostEntry> awkward;
  awkward.push_back({"id", "He said \"hi\"\\then\nleft", true, 0});
  const std::string escaped = shell_hosts_json(awkward);
  check("a quote in a host name is escaped", contains(escaped, "\\\""));
  check("a backslash is escaped", contains(escaped, "\\\\"));
  check("a newline does not break the line", !contains(escaped, "\n"));

  const std::string status = shell_status_json("connecting", "Office PC");
  std::string state;
  json_profile::json_get_string(status, "state", &state);
  check("status carries its state", state == "connecting", "state=" + state);

  ShellConnectRequest connect{};
  const bool parsedConnect = shell_parse_connect(
      R"({"type":"connect","hostId":"abc123","hostName":"Office PC","bitrateKbps":12000,"fps":60})",
      &connect);
  check("a connect request is read whole",
        parsedConnect && connect.hostId == "abc123" && connect.bitrateKbps == 12000 &&
            connect.fps == 60,
        "id=" + connect.hostId + " kbps=" + std::to_string(connect.bitrateKbps));

  check("a connect without a host is refused",
        !shell_parse_connect(R"({"type":"connect","hostName":"x"})", &connect));
  check("another message type is not mistaken for a connect",
        !shell_parse_connect(R"({"type":"settings","hostId":"abc"})", &connect));

  // A page that changes one field must not reset the others.
  ShellRuntimeSettings settings{};
  settings.bitrateKbps = 8000;
  settings.fps = 30;
  settings.monitorId = 1;
  const bool parsedSettings = shell_parse_settings(R"({"type":"settings","fps":60})", &settings);
  check("a partial settings change leaves the rest alone",
        parsedSettings && settings.fps == 60 && settings.bitrateKbps == 8000 &&
            settings.monitorId == 1,
        "kbps=" + std::to_string(settings.bitrateKbps) + " fps=" + std::to_string(settings.fps) +
            " mon=" + std::to_string(settings.monitorId));

  check("an unknown message has no type we act on",
        shell_message_type(R"({"nothing":1})").empty());

  std::printf(gFailures == 0 ? "\nclient_shell_bridge_test: PASS\n"
                             : "\nclient_shell_bridge_test: %d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}

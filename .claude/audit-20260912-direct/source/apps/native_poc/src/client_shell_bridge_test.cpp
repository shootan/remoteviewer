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

  // The settings file is plain text, so an editor may have left a byte order mark on the first
  // line. It is invisible in the file and invisible in the field, but the url it prefixes
  // resolves to nothing -- and the connect screen comes up blank with nothing to explain it.
  ShellRuntimeSettings restoreSettings{9000, 30, 2};
  const std::string restored =
      shell_restore_json("\xEF\xBB\xBF" "http://server:8080", "demo", restoreSettings);
  std::string restoredServer;
  json_profile::json_get_string(restored, "server", &restoredServer);
  check("a byte order mark is stripped from a restored address",
        restoredServer == "http://server:8080", "server=[" + restoredServer + "]");
  check("the remembered numbers travel with it",
        contains(restored, "\"bitrateKbps\":9000") && contains(restored, "\"fps\":30") &&
            contains(restored, "\"monitorId\":2"),
        restored);

  // ---------------------------------------------------------------- the start-up update check
  //
  // What this decides is not cosmetic. The client checks at start-up, and the check runs on a
  // laptop that is often opened somewhere with no route to the server. An outcome that
  // interrupts the user for that teaches them to dismiss the dialog, and the next one -- the one
  // that matters -- gets dismissed with it.

  {
    const ShellUpdateNotice notice = shell_update_notice("UpdateAvailable", "0.2.105", "");
    check("a newer version is worth saying", notice.show);
    check("and it names the version", contains(notice.text, "0.2.105"), notice.text);
    check("and it is logged too", !notice.logLine.empty(), notice.logLine);
  }
  {
    const ShellUpdateNotice notice = shell_update_notice("UpdateAvailable", "", "");
    check("a newer version with no version string still says something",
          notice.show && !notice.text.empty(), notice.text);
  }
  {
    const ShellUpdateNotice notice = shell_update_notice("UpToDate", "", "");
    check("up to date says nothing to the user", !notice.show);
    check("but it is still logged", !notice.logLine.empty(), notice.logLine);
  }
  {
    // The case the whole policy exists for. A start-up check that cannot reach the server must
    // not become a dialog, and it must not be recorded as "up to date" either.
    const ShellUpdateNotice notice = shell_update_notice("Unreachable", "", "timed out");
    check("an unreachable server does not interrupt the user", !notice.show);
    check("and the log does not call it up to date",
          !contains(notice.logLine, "up to date"), notice.logLine);
    check("and the log keeps the reason", contains(notice.logLine, "timed out"), notice.logLine);
  }
  {
    const ShellUpdateNotice notice = shell_update_notice("NotConfigured", "", "no url");
    check("a build that cannot check does not report a fault", !notice.show);
    check("and the log says why", contains(notice.logLine, "no url"), notice.logLine);
  }
  {
    const ShellUpdateNotice notice = shell_update_notice("Rejected", "", "bad signature");
    check("an answer that did not verify is logged, not shown", !notice.show);
    check("and the log says it was ignored", contains(notice.logLine, "ignored"), notice.logLine);
  }
  {
    // An outcome name this build does not know is not a reason to interrupt anyone.
    const ShellUpdateNotice notice = shell_update_notice("SomethingNewer", "9.9.9", "");
    check("an unrecognised outcome shows nothing", !notice.show);
    check("but leaves a trace", contains(notice.logLine, "SomethingNewer"), notice.logLine);
  }

  std::printf(gFailures == 0 ? "\nclient_shell_bridge_test: PASS\n"
                             : "\nclient_shell_bridge_test: %d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}

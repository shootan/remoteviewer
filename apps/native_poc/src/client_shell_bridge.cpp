#include "client_shell_bridge.hpp"

#include <algorithm>
#include <sstream>

#include "json_profile.hpp"

namespace remote60::native_poc {

using json_profile::json_get_string;
using json_profile::json_get_u32;

namespace {

/** Escaped for JSON, since host names are whatever the user typed on the PC. */
std::string escape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        // Control characters would make the page's JSON.parse throw, taking the whole screen
        // with it rather than one bad name.
        if (static_cast<unsigned char>(c) < 0x20) out += ' ';
        else out += c;
        break;
    }
  }
  // A byte order mark left by a text editor is invisible in the file and invisible in the
  // rendered field, but it makes the value it prefixes wrong -- a url with one in front resolves
  // to nothing. Removed here so every string crossing the boundary is clean.
  const std::string bom = "\xEF\xBB\xBF";
  while (out.rfind(bom, 0) == 0) out.erase(0, bom.size());
  return out;
}

}  // namespace

std::string shell_hosts_json(const std::vector<DirectoryHostEntry>& hosts) {
  // Sorted here rather than in the page: the order is a product decision, and leaving it to
  // whichever screen happens to render the list is how two screens end up disagreeing.
  std::vector<const DirectoryHostEntry*> ordered;
  ordered.reserve(hosts.size());
  for (const auto& host : hosts) ordered.push_back(&host);
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const DirectoryHostEntry* a, const DirectoryHostEntry* b) {
                     if (a->online != b->online) return a->online;
                     return a->hostName < b->hostName;
                   });

  std::ostringstream oss;
  oss << "{\"type\":\"hosts\",\"hosts\":[";
  for (size_t i = 0; i < ordered.size(); ++i) {
    const auto& host = *ordered[i];
    if (i > 0) oss << ",";
    oss << "{\"hostId\":\"" << escape(host.hostId) << "\","
        << "\"hostName\":\"" << escape(host.hostName) << "\","
        << "\"online\":" << (host.online ? "true" : "false") << "}";
  }
  oss << "]}";
  return oss.str();
}

std::string shell_status_json(const std::string& state, const std::string& detail) {
  std::ostringstream oss;
  oss << "{\"type\":\"status\",\"state\":\"" << escape(state) << "\",\"detail\":\""
      << escape(detail) << "\"}";
  return oss.str();
}

std::string shell_update_available_json(const std::string& version, const std::string& text) {
  std::ostringstream oss;
  oss << "{\"type\":\"updateAvailable\",\"version\":\"" << escape(version) << "\",\"text\":\""
      << escape(text) << "\"}";
  return oss.str();
}

std::string shell_update_cleared_json() { return "{\"type\":\"updateCleared\"}"; }

std::string shell_update_busy_json(bool busy) {
  return busy ? "{\"type\":\"updateBusy\",\"busy\":true}"
              : "{\"type\":\"updateBusy\",\"busy\":false}";
}

ShellUpdateNotice shell_update_notice(const std::string& outcome,
                                      const std::string& availableVersion,
                                      const std::string& detail) {
  ShellUpdateNotice notice;
  if (outcome == "UpdateAvailable") {
    notice.show = true;
    notice.text = availableVersion.empty()
                      ? std::string("새 버전이 있습니다.")
                      : "새 버전 " + availableVersion + " 이 있습니다.";
    notice.logLine = "update check: newer version available " + availableVersion;
    return notice;
  }
  if (outcome == "UpToDate") {
    notice.logLine = "update check: up to date";
    return notice;
  }
  if (outcome == "NotConfigured") {
    // Not a failure. This build simply cannot check, and saying so to the user would be
    // reporting a fault that does not exist.
    notice.logLine = "update check: not configured -- " + detail;
    return notice;
  }
  if (outcome == "Unreachable") {
    // Explicitly NOT "up to date". The distinction is kept in the log even though the user is
    // not interrupted with it.
    notice.logLine = "update check: server unreachable, so it is not known whether a newer "
                     "version exists -- " + detail;
    return notice;
  }
  if (outcome == "Rejected") {
    notice.logLine = "update check: the answer did not verify and was ignored -- " + detail;
    return notice;
  }
  notice.logLine = "update check: unrecognised outcome " + outcome;
  return notice;
}

std::string shell_restore_json(const std::string& server, const std::string& accountId,
                               const ShellRuntimeSettings& settings) {
  std::ostringstream oss;
  oss << "{\"type\":\"restore\",\"server\":\"" << escape(server) << "\",\"accountId\":\""
      << escape(accountId) << "\",\"bitrateKbps\":" << settings.bitrateKbps
      << ",\"fps\":" << settings.fps << ",\"monitorId\":" << settings.monitorId << "}";
  return oss.str();
}

std::string shell_message_type(const std::string& json) {
  std::string type;
  if (!json_get_string(json, "type", &type)) return {};
  return type;
}

bool shell_parse_connect(const std::string& json, ShellConnectRequest* out) {
  if (!out || shell_message_type(json) != "connect") return false;
  *out = ShellConnectRequest{};
  if (!json_get_string(json, "hostId", &out->hostId) || out->hostId.empty()) return false;
  json_get_string(json, "hostName", &out->hostName);
  json_get_u32(json, "bitrateKbps", &out->bitrateKbps);
  json_get_u32(json, "fps", &out->fps);
  return true;
}

bool shell_parse_settings(const std::string& json, ShellRuntimeSettings* out) {
  if (!out || shell_message_type(json) != "settings") return false;
  // Left at whatever the caller had when a field is absent, so a page that only changes the fps
  // does not silently reset the bitrate.
  json_get_u32(json, "bitrateKbps", &out->bitrateKbps);
  json_get_u32(json, "fps", &out->fps);
  json_get_u32(json, "monitorId", &out->monitorId);
  return true;
}

}  // namespace remote60::native_poc

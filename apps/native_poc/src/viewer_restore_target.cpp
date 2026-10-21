// See viewer_restore_target.hpp (dxgi-wedge-fix r1).

#include "viewer_restore_target.hpp"

#include <cstdio>
#include <cstdlib>

namespace remote60::native_poc::viewer {

namespace {

constexpr char kLinePrefix[] = "[viewer-restore] target=";
constexpr size_t kMaxName = 64;

std::string to_hex(const std::string& s) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (unsigned char c : s) {
    out.push_back(digits[c >> 4]);
    out.push_back(digits[c & 15]);
  }
  return out;
}

bool from_hex(const std::string& h, std::string* out) {
  if (h.size() % 2 != 0 || h.size() / 2 > kMaxName) return false;
  auto v = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  std::string s;
  for (size_t i = 0; i < h.size(); i += 2) {
    const int hi = v(h[i]), lo = v(h[i + 1]);
    if (hi < 0 || lo < 0) return false;
    const char c = static_cast<char>((hi << 4) | lo);
    if (c == '\0') return false;
    s.push_back(c);
  }
  *out = s;
  return true;
}

bool parse_i32(const std::string& s, int32_t* out) {
  if (s.empty() || s.size() > 11) return false;
  size_t i = 0;
  if (s[0] == '-') i = 1;
  if (i == s.size()) return false;
  long long v = 0;
  for (; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (s[i] - '0');
  }
  if (s[0] == '-') v = -v;
  if (v < INT32_MIN || v > INT32_MAX) return false;
  *out = static_cast<int32_t>(v);
  return true;
}

bool parse_u32(const std::string& s, uint32_t* out) {
  int32_t v = 0;
  if (s.empty() || s[0] == '-' || !parse_i32(s, &v) || v <= 0) return false;
  *out = static_cast<uint32_t>(v);
  return true;
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> parts;
  size_t start = 0;
  for (;;) {
    const size_t at = s.find(sep, start);
    parts.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
    if (at == std::string::npos) break;
    start = at + 1;
  }
  return parts;
}

}  // namespace

const char* RestoreMatch::reason() const {
  switch (kind) {
    case RestoreMatchKind::Matched: return "matched";
    case RestoreMatchKind::Changed: return "monitor_changed";
    case RestoreMatchKind::Ambiguous: return "monitor_ambiguous";
    case RestoreMatchKind::Gone: break;
  }
  return "monitor_gone";
}

std::string FormatRestoreArg(const RestoreTarget& t) {
  if (t.kind == RestoreTarget::Kind::Window) return "window";
  if (!t.hasMonitor || t.monitorName.empty() || t.monitorName.size() > kMaxName) return "desktop";
  char rect[64];
  std::snprintf(rect, sizeof(rect), "%d,%d,%u,%u", t.x, t.y, t.width, t.height);
  return "desktop:" + to_hex(t.monitorName) + ":" + rect + ":" + (t.primary ? "1" : "0");
}

std::optional<RestoreTarget> ParseRestoreArg(const std::string& arg) {
  if (arg.size() > 256) return std::nullopt;
  RestoreTarget t;
  if (arg == "window") {
    t.kind = RestoreTarget::Kind::Window;
    return t;
  }
  if (arg == "desktop") return t;
  const auto parts = split(arg, ':');
  if (parts.size() != 4 || parts[0] != "desktop") return std::nullopt;
  if (!from_hex(parts[1], &t.monitorName) || t.monitorName.empty()) return std::nullopt;
  const auto rect = split(parts[2], ',');
  if (rect.size() != 4 || !parse_i32(rect[0], &t.x) || !parse_i32(rect[1], &t.y) || !parse_u32(rect[2], &t.width) ||
      !parse_u32(rect[3], &t.height)) {
    return std::nullopt;
  }
  if (parts[3] != "0" && parts[3] != "1") return std::nullopt;
  t.primary = parts[3] == "1";
  t.hasMonitor = true;
  return t;
}

std::string FormatRestoreLine(const RestoreTarget& t) { return std::string(kLinePrefix) + FormatRestoreArg(t); }

std::optional<RestoreTarget> ParseRestoreLine(const std::string& line) {
  // What the shell's pump actually receives: log_client_line's form,
  //   "MM-DD HH:MM:SS.mmm [viewer-restore] target=<arg> eventQpcUs=<n>"
  // -- found by the process-level chain test, where a parser that wanted the prefix at column 0 saw
  // nothing and every reconnect fell back to the picker. Before the prefix: nothing, or exactly that
  // timestamp. The argument is the token after "target=", up to the first space.
  const size_t at = line.find(kLinePrefix);
  if (at == std::string::npos) return std::nullopt;
  if (at != 0) {
    const std::string stamp = line.substr(0, at);
    static const char kShape[] = "dd-dd dd:dd:dd.ddd ";
    if (stamp.size() != sizeof(kShape) - 1) return std::nullopt;
    for (size_t i = 0; i < stamp.size(); ++i) {
      const bool digit = stamp[i] >= '0' && stamp[i] <= '9';
      if (kShape[i] == 'd' ? !digit : stamp[i] != kShape[i]) return std::nullopt;
    }
  }
  std::string arg = line.substr(at + sizeof(kLinePrefix) - 1);
  const size_t end = arg.find_first_of(" \r\n");
  if (end != std::string::npos) arg.resize(end);
  return ParseRestoreArg(arg);
}

RestoreMatch MatchRestoreMonitor(const RestoreTarget& t, const std::vector<RestoreMonitor>& monitors) {
  RestoreMatch m;
  int exact = 0, sameName = 0;
  for (const auto& e : monitors) {
    if (e.name != t.monitorName) continue;
    ++sameName;
    if (e.x == t.x && e.y == t.y && e.width == t.width && e.height == t.height) {
      ++exact;
      m.monitorId = e.id;
      m.primary = e.primary;
    }
  }
  if (exact == 1) {
    m.kind = RestoreMatchKind::Matched;
  } else if (exact > 1) {
    m.kind = RestoreMatchKind::Ambiguous;
  } else if (sameName > 0) {
    m.kind = RestoreMatchKind::Changed;
  } else {
    m.kind = RestoreMatchKind::Gone;
  }
  return m;
}

}  // namespace remote60::native_poc::viewer

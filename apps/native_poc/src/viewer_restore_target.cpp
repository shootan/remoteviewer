// See viewer_restore_target.hpp (dxgi-wedge-fix r1, r2 D1/D2).

#include "viewer_restore_target.hpp"

namespace remote60::native_poc::viewer {

namespace {

constexpr char kLinePrefix[] = "[viewer-restore] target=";

}  // namespace

std::string FormatRestoreArg(const RestoreTarget& t) {
  switch (t.kind) {
    case RestoreTarget::Kind::Window: return "window";
    case RestoreTarget::Kind::Monitor: return "monitor";
    case RestoreTarget::Kind::Desktop: break;
  }
  return "desktop";
}

std::optional<RestoreTarget> ParseRestoreArg(const std::string& arg) {
  RestoreTarget t;
  if (arg == "desktop") return t;
  if (arg == "monitor") {
    t.kind = RestoreTarget::Kind::Monitor;
    return t;
  }
  if (arg == "window") {
    t.kind = RestoreTarget::Kind::Window;
    return t;
  }
  return std::nullopt;
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

}  // namespace remote60::native_poc::viewer

#include "updater_options.hpp"

#include <windows.h>

#include <cwchar>

namespace remote60::native_poc::update {
namespace {

std::string narrow(const std::wstring& text) {
  // Arguments that become narrow strings here are urls and platform names, which are ASCII by the
  // rules that already govern them. A byte outside that is refused rather than transcoded.
  std::string out;
  out.reserve(text.size());
  for (wchar_t c : text) {
    if (c > 0x7F) return {};
    out.push_back(static_cast<char>(c));
  }
  return out;
}

bool is_absolute(const std::wstring& path) {
  // A drive-qualified path or a UNC path. A relative path handed to a process that replaces files
  // in Program Files would resolve against whatever directory it happened to start in.
  if (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) return true;
  if (path.size() >= 2 && (path[0] == L'\\' || path[0] == L'/') &&
      (path[1] == L'\\' || path[1] == L'/')) {
    return true;
  }
  return false;
}

bool inside(const std::wstring& child, const std::wstring& parent) {
  if (parent.empty() || child.size() <= parent.size()) return false;
  if (_wcsnicmp(child.c_str(), parent.c_str(), parent.size()) != 0) return false;
  return child[parent.size()] == L'\\' || child[parent.size()] == L'/';
}

bool parse_u32(const std::wstring& text, uint32_t* out) {
  if (text.empty()) return false;
  uint64_t value = 0;
  for (wchar_t c : text) {
    if (c < L'0' || c > L'9') return false;
    value = value * 10 + static_cast<uint64_t>(c - L'0');
    if (value > 0xFFFFFFFFull) return false;
  }
  *out = static_cast<uint32_t>(value);
  return true;
}

}  // namespace

const char* parse_status_name(ParseStatus status) {
  switch (status) {
    case ParseStatus::Ok: return "ok";
    case ParseStatus::UnknownArgument: return "unknown-argument";
    case ParseStatus::MissingValue: return "missing-value";
    case ParseStatus::BadValue: return "bad-value";
  }
  return "unknown";
}

bool UpdaterOptions::validate(std::string* detail) const {
  const auto fail = [detail](const char* why) {
    if (detail) *detail = why;
    return false;
  };

  if (installDir.empty()) return fail("--install-dir is required");
  if (stagingDir.empty()) return fail("--staging-dir is required");
  if (workDir.empty()) return fail("--work-dir is required");
  if (manifestUrl.empty()) return fail("--manifest-url is required");
  if (platform.empty()) return fail("--platform is required");
  if (installedVersion.empty()) return fail("--installed-version is required");
  if (healthLogPath.empty()) return fail("--health-log is required");
  if (logPath.empty()) return fail("--log is required");
  if (serviceName.empty()) return fail("--service-name is required");
  if (registryRoot.empty()) return fail("--registry-root is required");

  if (!is_absolute(installDir)) return fail("--install-dir must be an absolute path");
  if (!is_absolute(stagingDir)) return fail("--staging-dir must be an absolute path");
  if (!is_absolute(workDir)) return fail("--work-dir must be an absolute path");

  // The same rule UpdateEffectsConfig enforces, checked here too so a bad launch fails before
  // anything is fetched rather than after.
  if (inside(stagingDir, installDir)) return fail("--staging-dir is inside --install-dir");
  // And the working copy must not live in the directory being replaced, or the swap would move
  // the running image aside.
  if (inside(workDir, installDir)) return fail("--work-dir is inside --install-dir");

  // https only, enforced at the entry point as well as where the manifest is read. A caller that
  // hands this an http url is told so rather than having it quietly refused three layers down.
  if (manifestUrl.rfind("https://", 0) != 0) return fail("--manifest-url must be https");

  // Checked here so a root this build cannot use fails at the entry point rather than at the
  // registration stage, half way through an update.
  if (!split_registry_root(registryRoot, nullptr, nullptr)) {
    return fail("--registry-root must be HKLM\\... or HKCU\\...");
  }

  return true;
}

ParseResult parse_updater_options(const std::vector<std::wstring>& arguments) {
  ParseResult result;

  const auto need_value = [&](size_t& i, const wchar_t* flag) -> const std::wstring* {
    if (i + 1 >= arguments.size()) {
      result.status = ParseStatus::MissingValue;
      result.detail = std::string(narrow(flag)) + " needs a value";
      return nullptr;
    }
    return &arguments[++i];
  };

  for (size_t i = 0; i < arguments.size(); ++i) {
    const std::wstring& arg = arguments[i];

    if (arg == L"--running-from-copy") {
      result.options.runningFromCopy = true;
      continue;
    }

    const std::wstring* value = nullptr;
    if (arg == L"--install-dir") {
      if (!(value = need_value(i, L"--install-dir"))) return result;
      result.options.installDir = *value;
    } else if (arg == L"--staging-dir") {
      if (!(value = need_value(i, L"--staging-dir"))) return result;
      result.options.stagingDir = *value;
    } else if (arg == L"--work-dir") {
      if (!(value = need_value(i, L"--work-dir"))) return result;
      result.options.workDir = *value;
    } else if (arg == L"--manifest-url") {
      if (!(value = need_value(i, L"--manifest-url"))) return result;
      result.options.manifestUrl = narrow(*value);
      if (result.options.manifestUrl.empty()) {
        result.status = ParseStatus::BadValue;
        result.detail = "--manifest-url must be ascii";
        return result;
      }
    } else if (arg == L"--platform") {
      if (!(value = need_value(i, L"--platform"))) return result;
      result.options.platform = narrow(*value);
    } else if (arg == L"--installed-version") {
      if (!(value = need_value(i, L"--installed-version"))) return result;
      result.options.installedVersion = narrow(*value);
    } else if (arg == L"--health-log") {
      if (!(value = need_value(i, L"--health-log"))) return result;
      result.options.healthLogPath = *value;
    } else if (arg == L"--log") {
      if (!(value = need_value(i, L"--log"))) return result;
      result.options.logPath = *value;
    } else if (arg == L"--service-name") {
      if (!(value = need_value(i, L"--service-name"))) return result;
      result.options.serviceName = *value;
    } else if (arg == L"--registry-root") {
      if (!(value = need_value(i, L"--registry-root"))) return result;
      result.options.registryRoot = *value;
    } else if (arg == L"--ready-event") {
      if (!(value = need_value(i, L"--ready-event"))) return result;
      result.options.readyEventName = *value;
    } else if (arg == L"--parent-pid") {
      if (!(value = need_value(i, L"--parent-pid"))) return result;
      if (!parse_u32(*value, &result.options.parentPid)) {
        result.status = ParseStatus::BadValue;
        result.detail = "--parent-pid must be a number";
        return result;
      }
    } else {
      // Not ignored. A flag this build does not understand, handed to a process that runs as
      // administrator, means the caller asked for something else -- doing most of what they asked
      // is worse than doing none of it.
      result.status = ParseStatus::UnknownArgument;
      result.detail = "unknown argument: " + narrow(arg);
      return result;
    }
  }

  return result;
}

bool split_registry_root(const std::wstring& text, void** hive, std::wstring* subkey) {
  const size_t slash = text.find_first_of(L"\\/");
  if (slash == std::wstring::npos || slash == 0) return false;
  const std::wstring prefix = text.substr(0, slash);
  std::wstring rest = text.substr(slash + 1);
  if (rest.empty()) return false;

  // Only the two hives this product ever registers under. Anything else is refused rather than
  // mapped to a default -- an updater writing to a hive nobody asked for is the failure this
  // whole argument exists to prevent.
  if (_wcsicmp(prefix.c_str(), L"HKLM") == 0 ||
      _wcsicmp(prefix.c_str(), L"HKEY_LOCAL_MACHINE") == 0) {
    if (hive) *hive = HKEY_LOCAL_MACHINE;
  } else if (_wcsicmp(prefix.c_str(), L"HKCU") == 0 ||
             _wcsicmp(prefix.c_str(), L"HKEY_CURRENT_USER") == 0) {
    if (hive) *hive = HKEY_CURRENT_USER;
  } else {
    return false;
  }
  if (subkey) *subkey = rest;
  return true;
}

std::wstring updater_copy_path(const std::wstring& workDir, const std::wstring& selfImagePath) {
  size_t slash = selfImagePath.find_last_of(L"\\/");
  const std::wstring leaf =
      (slash == std::wstring::npos) ? selfImagePath : selfImagePath.substr(slash + 1);
  if (workDir.empty()) return leaf;
  const wchar_t last = workDir[workDir.size() - 1];
  const std::wstring base = (last == L'\\' || last == L'/') ? workDir : workDir + L"\\";
  return base + leaf;
}

}  // namespace remote60::native_poc::update

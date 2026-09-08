#include "update_handoff.hpp"

namespace remote60::native_poc::update {
namespace {

std::wstring widen_ascii(const std::string& text) {
  std::wstring out;
  out.reserve(text.size());
  for (char c : text) out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
  return out;
}

std::wstring to_wide_number(uint64_t value) {
  if (value == 0) return L"0";
  std::wstring digits;
  while (value > 0) {
    digits.insert(digits.begin(), static_cast<wchar_t>(L'0' + (value % 10)));
    value /= 10;
  }
  return digits;
}

}  // namespace

std::vector<std::wstring> updater_arguments(const UpdaterLaunchSpec& spec) {
  std::vector<std::wstring> args;
  const auto add = [&args](const wchar_t* flag, const std::wstring& value) {
    // A flag with an empty value is left out entirely rather than passed as an empty string. The
    // updater requires its arguments and reports which one is missing; sending `--log ""` would
    // turn that clear failure into a puzzling one.
    if (value.empty()) return;
    args.push_back(flag);
    args.push_back(value);
  };

  add(L"--install-dir", spec.installDir);
  add(L"--staging-dir", spec.stagingDir);
  add(L"--work-dir", spec.workDir);
  add(L"--manifest-url", widen_ascii(spec.manifestUrl));
  add(L"--platform", widen_ascii(spec.platform));
  add(L"--installed-version", widen_ascii(spec.installedVersion));
  add(L"--health-log", spec.healthLogPath);
  add(L"--log", spec.logPath);
  add(L"--service-name", spec.serviceName);
  add(L"--registry-root", spec.registryRoot);
  add(L"--ready-event", spec.readyEventName);
  if (spec.parentPid != 0) add(L"--parent-pid", to_wide_number(spec.parentPid));
  return args;
}

std::wstring make_ready_event_name(uint32_t pid, uint64_t tick) {
  return L"Local\\GNLinkUpdateReady-" + to_wide_number(pid) + L"-" + to_wide_number(tick);
}

const char* handoff_verdict_name(HandoffVerdict verdict) {
  switch (verdict) {
    case HandoffVerdict::ExitNow: return "exit-now";
    case HandoffVerdict::KeepRunning: return "keep-running";
  }
  return "unknown";
}

HandoffVerdict handoff_verdict(bool readySignalled, bool updaterExited, bool timedOut,
                               std::string* detail) {
  const auto say = [detail](const char* text) {
    if (detail) *detail = text;
  };

  if (readySignalled) {
    // Checked before the exit, deliberately. An updater that signalled and then exited has done
    // what the signal promised; reading the exit as a failure would keep the host running after
    // it had already agreed to leave.
    say("the updater holds the lock and has a verified download");
    return HandoffVerdict::ExitNow;
  }
  if (updaterExited) {
    say("the updater exited without getting that far, so nothing was changed");
    return HandoffVerdict::KeepRunning;
  }
  if (timedOut) {
    say("the updater did not reach a verified download in time");
    return HandoffVerdict::KeepRunning;
  }
  // Not one of the three. Whatever it is, it is not the one case that permits exiting.
  say("the updater did not signal");
  return HandoffVerdict::KeepRunning;
}

}  // namespace remote60::native_poc::update

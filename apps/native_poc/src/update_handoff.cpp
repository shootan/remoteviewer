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
  // The shape travels with the url. Without it the worker would have to decide from the response,
  // and that decision is exactly the one that cannot be made safely.
  if (spec.derivedEndpoint) args.push_back(L"--manifest-envelope");
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

std::wstring make_ack_event_name(const std::wstring& readyEventName) {
  if (readyEventName.empty()) return {};
  return readyEventName + L".ack";
}

uint32_t remaining_ms(uint64_t startedAt, uint64_t now, uint32_t timeoutMs) {
  // Subtraction first, and in 64 bits. The old form added the timeout to the clock and compared
  // the sum, which overflows a 32-bit tick count on a machine that has been up seven weeks --
  // and then every wait ends immediately, reporting a timeout that never happened.
  if (now <= startedAt) return timeoutMs;  // a clock that went backwards is not evidence of delay
  const uint64_t elapsed = now - startedAt;
  if (elapsed >= timeoutMs) return 0;
  return static_cast<uint32_t>(timeoutMs - elapsed);
}

std::wstring make_alive_mutex_name(const std::wstring& readyEventName) {
  if (readyEventName.empty()) return {};
  return readyEventName + L".alive";
}

const char* handoff_step_name(HandoffStep step) {
  switch (step) {
    case HandoffStep::KeepWaiting: return "KeepWaiting";
    case HandoffStep::ExitNow: return "ExitNow";
    case HandoffStep::KeepRunning: return "KeepRunning";
  }
  return "?";
}

HandoffStep handoff_step(bool readySignalled, bool bootstrapExited, uint32_t bootstrapExitCode,
                         bool timedOut, std::string* detail) {
  const auto say = [detail](const char* text) {
    if (detail) *detail = text;
  };

  if (readySignalled) {
    // First, deliberately. A signal that arrived is decisive whatever else has happened since.
    say("the updater holds the lock and has a verified download");
    return HandoffStep::ExitNow;
  }
  if (bootstrapExited) {
    if (bootstrapExitCode == 0) {
      // The normal path, and the one that used to be read as death. The bootstrap's whole job is
      // to start the working copy and get out of the way -- it cannot stay, because the file it
      // is running from is one of the files the update replaces.
      say("the bootstrap handed over and exited, which is what it is meant to do -- still "
          "waiting for the working copy");
      return HandoffStep::KeepWaiting;
    }
    say("the bootstrap failed before it could hand over, so there is nothing to wait for");
    return HandoffStep::KeepRunning;
  }
  if (timedOut) {
    say("the updater did not reach a verified download in time");
    return HandoffStep::KeepRunning;
  }
  say("the updater did not signal");
  return HandoffStep::KeepRunning;
}

bool may_stop_the_product(bool signalDelivered, bool acknowledged, std::string* detail) {
  const auto say = [detail](const char* text) {
    if (detail) *detail = text;
  };
  if (!signalDelivered) {
    // The event could not be opened. Either nobody was ever waiting, or whoever was has closed it
    // and moved on -- and in both cases stopping the product would be stopping something that is
    // not expecting to be stopped and has nobody to bring it back.
    say("the ready signal could not be delivered -- nothing is waiting for this update");
    return false;
  }
  if (!acknowledged) {
    say("the ready signal was delivered but never answered -- whoever was waiting has given up");
    return false;
  }
  say("the waiting caller acknowledged and is standing down");
  return true;
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

const char* elevation_outcome_name(ElevationOutcome outcome) {
  switch (outcome) {
    case ElevationOutcome::Launched: return "launched";
    case ElevationOutcome::Cancelled: return "cancelled";
    case ElevationOutcome::Failed: return "failed";
  }
  return "unknown";
}

ElevationOutcome elevation_outcome(bool succeeded, uint32_t lastError) {
  if (succeeded) return ElevationOutcome::Launched;
  // 1223 is ERROR_CANCELLED. Written as a literal so this file does not need windows.h -- it is
  // the decision, not the mechanism, and the tests that cover it start no processes.
  if (lastError == 1223u) return ElevationOutcome::Cancelled;
  return ElevationOutcome::Failed;
}

}  // namespace remote60::native_poc::update

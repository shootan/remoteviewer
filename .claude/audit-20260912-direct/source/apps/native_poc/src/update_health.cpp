#include "update_health.hpp"

#include <vector>

namespace remote60::native_poc::update {
namespace {

constexpr char kMarker[] = "[host-app] health ";

std::string trimmed(const std::string& s) {
  size_t begin = 0;
  size_t end = s.size();
  while (begin < end && (s[begin] == ' ' || s[begin] == '\t')) ++begin;
  while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) --end;
  return s.substr(begin, end - begin);
}

/** `key=value` fields separated by spaces. Unknown keys are ignored, as in the manifest format. */
bool field_value(const std::string& line, const std::string& key, std::string* out) {
  const std::string needle = key + "=";
  size_t at = line.find(needle);
  while (at != std::string::npos) {
    // Only a field boundary counts, so `version=` does not match inside `otherversion=`.
    const bool boundary = (at == 0) || line[at - 1] == ' ' || line[at - 1] == '\t';
    if (boundary) {
      const size_t start = at + needle.size();
      size_t end = line.find_first_of(" \t", start);
      if (end == std::string::npos) end = line.size();
      *out = line.substr(start, end - start);
      return true;
    }
    at = line.find(needle, at + 1);
  }
  return false;
}

}  // namespace

const char* directory_health_name(DirectoryHealth value) {
  switch (value) {
    case DirectoryHealth::NotReported: return "not-reported";
    case DirectoryHealth::Pending: return "pending";
    case DirectoryHealth::Ok: return "ok";
    case DirectoryHealth::NotConfigured: return "not-configured";
  }
  return "unknown";
}

const char* health_verdict_name(HealthVerdict verdict) {
  switch (verdict) {
    case HealthVerdict::Healthy: return "healthy";
    case HealthVerdict::Waiting: return "waiting";
    case HealthVerdict::WrongVersion: return "wrong-version";
  }
  return "unknown";
}

HealthSignals scan_health_report(const std::string& text) {
  HealthSignals signals;
  size_t lineStart = 0;
  while (lineStart <= text.size()) {
    size_t lineEnd = text.find('\n', lineStart);
    if (lineEnd == std::string::npos) lineEnd = text.size();
    const std::string line = trimmed(text.substr(lineStart, lineEnd - lineStart));
    lineStart = lineEnd + 1;
    if (line.empty()) {
      if (lineEnd == text.size()) break;
      continue;
    }

    const size_t at = line.find(kMarker);
    if (at == std::string::npos) continue;
    const std::string rest = line.substr(at + sizeof(kMarker) - 1);

    std::string version;
    std::string directory;
    // A line missing either field is not a health report. Half a report is not evidence.
    if (!field_value(rest, "version", &version) || version.empty()) continue;
    if (!field_value(rest, "directory", &directory)) continue;

    DirectoryHealth state = DirectoryHealth::NotReported;
    if (directory == "ok") {
      state = DirectoryHealth::Ok;
    } else if (directory == "pending") {
      state = DirectoryHealth::Pending;
    } else if (directory == "not-configured") {
      state = DirectoryHealth::NotConfigured;
    } else {
      // A state this build does not know. Ignored rather than guessed at -- an unrecognised value
      // is not evidence of health, and treating it as one would make a newer product's report
      // able to satisfy an older updater by accident.
      continue;
    }

    // Last wins: the product reports again when it learns something.
    signals.reported = true;
    signals.reportedVersion = version;
    signals.directory = state;

    if (lineEnd == text.size()) break;
  }
  return signals;
}

HealthVerdict judge_health(const HealthSignals& signals, const std::string& expectedVersion,
                           std::string* detail) {
  const auto say = [detail](const std::string& text) {
    if (detail) *detail = text;
  };

  if (!signals.reported) {
    say("the new build has not reported yet");
    return HealthVerdict::Waiting;
  }
  if (!expectedVersion.empty() && signals.reportedVersion != expectedVersion) {
    say("it reports version " + signals.reportedVersion + ", not " + expectedVersion);
    return HealthVerdict::WrongVersion;
  }
  switch (signals.directory) {
    case DirectoryHealth::Ok:
      say("version " + signals.reportedVersion + ", directory reached");
      return HealthVerdict::Healthy;
    case DirectoryHealth::NotConfigured:
      say("version " + signals.reportedVersion + ", no account signed in so no directory contact "
          "is expected");
      return HealthVerdict::Healthy;
    case DirectoryHealth::Pending:
      say("version " + signals.reportedVersion + ", waiting for the directory");
      return HealthVerdict::Waiting;
    case DirectoryHealth::NotReported:
      break;
  }
  say("the report carried no directory state");
  return HealthVerdict::Waiting;
}

}  // namespace remote60::native_poc::update

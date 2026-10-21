// lockscreen-cred r1: the lock-screen agent's commit sequence and target (logon_ui_target.hpp), with the OS
// questions as stand-ins. Typed only into a target that is there and stays the same; at most once; a target
// that is not there (or not on Winlogon) means nothing typed.

#include <cstdio>
#include <string>
#include <vector>

#include "logon_ui_target.hpp"

namespace lu = remote60::native_poc::logon_ui;
using remote60::native_poc::UnlockStage;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}

struct Run {
  std::vector<uint64_t> targets;  // what target() answers, call by call (the last repeats)
  size_t calls = 0;
  bool injectOk = true;
  uint32_t injectErr = 0;
  int injected = 0;
  std::u16string typed;
  uint32_t slept = 0;
  UnlockStage go(const std::u16string& text, uint32_t waitMs, uint32_t* err, const lu::StillValidFn& valid = nullptr) {
    std::vector<uint16_t> units(text.begin(), text.end());
    return lu::TypeIntoTarget(
        units.data(), static_cast<uint16_t>(units.size()),
        [this] {
          const uint64_t t = targets.empty() ? 0 : targets[calls < targets.size() ? calls : targets.size() - 1];
          ++calls;
          return t;
        },
        [this](const uint16_t* u, uint16_t n, uint32_t* e) {
          ++injected;
          typed.assign(u, u + n);
          *e = injectErr;
          return injectOk;
        },
        [this](uint32_t ms) { slept += ms; }, waitMs, err, valid);
  }
};
std::string stage(UnlockStage s) { return "stage=" + std::to_string(static_cast<int>(s)); }
}  // namespace

int main() {
  std::printf("the lock-screen agent's typing (lockscreen-cred r1)\n\n");
  {
    Run r;
    r.targets = {0};
    uint32_t err = 0;
    const UnlockStage s = r.go(u"pw", 3000, &err);
    check("no target within the wait: TargetNotReady, nothing typed, waited the bound",
          s == UnlockStage::TargetNotReady && r.injected == 0 && r.slept == 3000, stage(s));
  }
  {
    Run r;
    r.targets = {0, 0, 0, 77};
    uint32_t err = 0;
    const UnlockStage s = r.go(u"pw-가", 3000, &err);
    check("the field appears within the wait: typed once, exactly the units", s == UnlockStage::Injected && r.injected == 1 &&
                                                                                  r.typed == u"pw-가",
          stage(s));
  }
  {
    Run r;
    r.targets = {77, 78};
    uint32_t err = 0;
    const UnlockStage s = r.go(u"pw", 3000, &err);
    check("the target changed between the wait and the commit: TargetNotReady, nothing typed",
          s == UnlockStage::TargetNotReady && r.injected == 0, stage(s));
  }
  {
    Run r;
    r.targets = {77, 77, 0};
    uint32_t err = 0;
    const UnlockStage s = r.go(u"pw", 3000, &err);
    check("the target went away while it was typed: InjectFailed (1400), typed once, not again",
          s == UnlockStage::InjectFailed && err == 1400 && r.injected == 1, stage(s));
  }
  {
    Run r;
    r.targets = {77};
    r.injectOk = false;
    r.injectErr = 5;
    uint32_t err = 0;
    const UnlockStage s = r.go(u"pw", 3000, &err);
    check("SendInput refused (or sent part): InjectFailed with its error, not retried",
          s == UnlockStage::InjectFailed && err == 5 && r.injected == 1, stage(s));
  }
  {
    Run r;
    r.targets = {77};
    uint32_t err = 0;
    const UnlockStage s = r.go(u"pw", 3000, &err, [] { return false; });
    check("the job is no longer wanted: nothing typed", s == UnlockStage::TargetNotReady && r.injected == 0, stage(s));
  }

  std::printf("\n[the lock screen's target: Winlogon only]\n");
  int fieldAsked = 0;
  const lu::TargetFn field = [&] {
    ++fieldAsked;
    return 99ull;
  };
  check("input desktop Default (the user's desktop): no target, the field not even asked",
        lu::LockScreenTarget([] { return std::wstring(L"Default"); }, field) == 0 && fieldAsked == 0);
  check("input desktop unreadable: no target", lu::LockScreenTarget([] { return std::wstring(); }, field) == 0);
  check("input desktop Winlogon: the field's identity", lu::LockScreenTarget([] { return std::wstring(L"Winlogon"); }, field) == 99);
  check("...case-insensitively", lu::LockScreenTarget([] { return std::wstring(L"WINLOGON"); }, field) == 99);
  check("a desktop merely starting with Winlogon is not it",
        lu::LockScreenTarget([] { return std::wstring(L"Winlogon2"); }, field) == 0);

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

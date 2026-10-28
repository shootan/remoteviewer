// 2026-10-28 (verifier, after worker2's finding in the ka-pc e2e; Codex codex_test_injection_finding.md rules 1-6):
// GNLinkStream injects input by default (host_args.hpp enableInputInjection = true). A test host must never drive
// THIS PC's input:
//   1. it is started with injection explicitly OFF -- a --config of its own with {"enableInputInjection": false};
//   2. nothing connects to it before THIS launch's own output said the explicit OFF state
//      ("input injection disabled (enableInputInjection=false)") -- not "unsupported mode", not a line of an earlier
//      run, not the argument this test passed;
//   3. anything else (enabled, another disabled reason, a config error, silence) is a setup failure: the test stops
//      at once -- RESULT: INVALID, exit 2 -- before any client exists (its host dies with its job object);
//   4. a contradiction later (injectionEnabled=1 in its input log) makes the run INVALID as well.
// The host still receives, logs and acknowledges input; that is receipt, not an OS injection.
#ifndef REMOTE60_NATIVE_POC_E2E_INJECTION_GUARD_HPP
#define REMOTE60_NATIVE_POC_E2E_INJECTION_GUARD_HPP

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

namespace remote60::native_poc::e2e {

// The explicit OFF state, as host_startup_config.cpp prints it.
inline const char* e2e_injection_off_line() { return "input injection disabled (enableInputInjection=false)"; }

// The host's --config: injection off. A --config profile CLEARS the runtime overrides it can carry
// (json_profile::apply_runtime_env_overrides_from_json -> clear_runtime_env_overrides) and sets only what it names --
// so the ones this test set in its own environment (which the host inherits) are written into it too; without
// forceEncodedExperiment the host refuses h264 and serves no session at all (seen 2026-10-28).
inline bool e2e_write_injection_off_config(const std::wstring& stagingDir) {
  struct Map { const wchar_t* env; const char* json; bool isBool; };
  static const Map kMaps[] = {
      {L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", "forceEncodedExperiment", true},
      {L"REMOTE60_NATIVE_FRAME_GATING_DISABLE", "frameGatingDisable", true},
      {L"REMOTE60_NATIVE_STATIC_SCENE_FPS", "staticSceneFps", false},
      {L"REMOTE60_NATIVE_H264_NO_PACING", "h264NoPacing", true},
      {L"REMOTE60_NATIVE_ABR_DISABLE", "abrDisable", true},
      {L"REMOTE60_NATIVE_CAPTURE_POOL_BUFFERS", "capturePoolBuffers", false},
  };
  std::string json = "{\"enableInputInjection\": false";
  for (const auto& m : kMaps) {
    wchar_t v[64] = {};
    const DWORD n = GetEnvironmentVariableW(m.env, v, 64);
    if (n == 0 || n >= 64) continue;
    if (m.isBool) {
      if (wcscmp(v, L"1") == 0 || _wcsicmp(v, L"true") == 0) json += std::string(", \"") + m.json + "\": true";
    } else {
      const unsigned long u = wcstoul(v, nullptr, 10);
      if (u > 0) json += std::string(", \"") + m.json + "\": " + std::to_string(u);
    }
  }
  json += "}\n";
  std::ofstream cfg(stagingDir + L"host-config.json", std::ios::binary | std::ios::trunc);
  cfg << json;
  return static_cast<bool>(cfg);
}

// The argument for the host's command line -- it writes that config first. A config that could not be written is
// one the host fails to load ("failed to load --config"): the gate below rejects that, so this cannot fail open.
inline std::wstring e2e_injection_off_arg(const std::wstring& stagingDir) {
  e2e_write_injection_off_config(stagingDir);
  return L" --config \"" + stagingDir + L"host-config.json\"";
}

// What a host wrote to a log file so far (shared read: the host keeps writing it).
inline std::string e2e_read_text_file(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return {};
  std::string out;
  char buf[8192];
  DWORD got = 0;
  while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0) out.append(buf, got);
  CloseHandle(h);
  return out;
}

// The judgement on what a host said so far: 1 = the explicit OFF state, 0 = not yet, -1 = never acceptable
// (enabled, another disabled reason, the config refused / not loaded).
inline int e2e_injection_state(const std::string& out, std::string* why) {
  if (out.find("input injection enabled") != std::string::npos) {
    if (why) *why = "the host said input injection ENABLED";
    return -1;
  }
  if (out.find("failed to load --config") != std::string::npos) {
    if (why) *why = "the host could not load its injection-off config";
    return -1;
  }
  const size_t at = out.find("input injection disabled");
  if (at != std::string::npos) {
    const char* want = e2e_injection_off_line();
    if (out.compare(at, std::strlen(want), want) == 0) return 1;
    if (why) *why = "the host is off for another reason, not the explicit setting: " + out.substr(at, 80);
    return -1;
  }
  return 0;
}

// Waits for this launch's own words. true only for the explicit OFF state.
inline bool e2e_wait_injection_off(const std::function<std::string()>& hostOutput, uint32_t budgetMs, std::string* why) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
  for (;;) {
    const int st = e2e_injection_state(hostOutput(), why);
    if (st != 0) return st > 0;
    if (std::chrono::steady_clock::now() >= deadline) {
      if (why) *why = "the host never said its input injection is disabled";
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

// A record-only test host (GNLinkStreamInputRecordTest, os_input_effects.hpp): its own state line, said by THIS
// launch before anything connects -- its dispatch runs (injection "enabled" is its logical state), its OS effects are
// recorded and never performed. Not the same thing as injection off, and not to be confused with it.
inline const char* e2e_os_record_only_line() { return "os-input-record: os input effects=record-only"; }
inline bool e2e_wait_os_record_only(const std::function<std::string()>& hostOutput, uint32_t budgetMs, std::string* why) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
  for (;;) {
    const std::string out = hostOutput();
    if (out.find(e2e_os_record_only_line()) != std::string::npos) return true;
    if (out.find("[native-video-host]") != std::string::npos && out.find("input injection") != std::string::npos) {
      // it said its injection state without the record-only line: a product (effect-performing) host
      if (why) *why = "the host said its injection state but not record-only -- an OS-effect host";
      return false;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      if (why) *why = "the host never said it is record-only";
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

// Fail closed: the test ends here, before any client exists. Its hosts die with their job objects
// (JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) at process exit; nothing of the user's is touched.
[[noreturn]] inline void e2e_injection_setup_failed(const char* where, const std::string& why) {
  std::printf("FAIL  SETUP %s: input injection is not provably off (%s) -- stopped before any client connected\n"
              "RESULT: INVALID\n", where, why.c_str());
  std::fflush(stdout);
  std::_Exit(2);
}

// Rule 4: a host that took input while injection was on is a contradiction -- the run is invalid.
inline bool e2e_injection_contradicted(const std::string& hostOutput) {
  return hostOutput.find("injectionEnabled=1") != std::string::npos ||
         hostOutput.find("input injection enabled") != std::string::npos;
}

// The tests whose very subject is an injection the OS performs (2026-10-28): a host that injects into a window of
// the test's own -- but in window mode a KEY goes through SendInput whenever that window holds the focus
// (host_input_inject.cpp, the kind 5/6 branch). That is this PC's real input path. Such a test refuses to run unless
// the person starting it says so for this run: REMOTE60_ALLOW_REAL_INPUT_TEST=1. Refused = exit 3,
// "RESULT: REFUSED" -- never a pass. Whether it becomes a receipt-only / record-sink test is its owner's decision.
inline bool e2e_real_input_refused(const char* test, const char* why) {
  wchar_t v[8] = {};
  if (GetEnvironmentVariableW(L"REMOTE60_ALLOW_REAL_INPUT_TEST", v, 8) > 0 && wcscmp(v, L"1") == 0) return false;
  std::printf("REFUSED  %s: %s -- this PC's real input path. Not run without REMOTE60_ALLOW_REAL_INPUT_TEST=1 "
              "(e2e_injection_guard.hpp)\nRESULT: REFUSED\n", test, why);
  return true;
}

}  // namespace remote60::native_poc::e2e

#endif

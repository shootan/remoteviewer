// The updater's own assembly, driven end to end.
//
// Every other suite builds an UpdateEffectsConfig by hand and runs the state machine over it. That
// tests the parts. What it does not test is how the UPDATER assembles them -- and all five of the
// defects found in review lived exactly there: the manifest was never fetched, the verified
// version never reached registration, and process identities were captured after the processes
// had already gone. Eight hundred and ninety checks were green throughout.
//
// So this drives UpdaterEffects itself -- the same class the product binary uses, not a
// reconstruction of it -- across an injected boundary. What crosses that boundary is only ever a
// temp directory, an in-memory document, dummy processes and recording stand-ins. In particular
// the SIGNATURE VERIFIER is injected: production compiles in its trust anchor and this test
// supplies its own, so a signed fixture can be exercised without the product's trusted key being
// changed or a build flag altering what production trusts.
//
// Each case below has a control that breaks one wire, because "it passed" and "it would have
// noticed" are different claims and only the second one is worth anything here.
//
// Design: docs/업데이트_배선_계획.md W1, history #462-#463.

#include "update_process_targets.hpp"
#include "updater_effects.hpp"

#include <windows.h>

#include "product_version.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace remote60::native_poc::update;
namespace install = remote60::native_poc::install;

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

std::string narrow(const std::wstring& s) { return std::string(s.begin(), s.end()); }

std::wstring make_temp_dir(const wchar_t* tag) {
  wchar_t base[MAX_PATH]{};
  GetTempPathW(MAX_PATH, base);
  wchar_t path[MAX_PATH]{};
  swprintf(path, MAX_PATH, L"%sgnlink-asm-%lu-%s", base, GetCurrentProcessId(), tag);
  CreateDirectoryW(path, nullptr);
  return path;
}

void remove_tree(const std::wstring& dir) {
  WIN32_FIND_DATAW find{};
  HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &find);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = find.cFileName;
      if (name == L"." || name == L"..") continue;
      const std::wstring full = dir + L"\\" + name;
      if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        remove_tree(full);
      } else {
        DeleteFileW(full.c_str());
      }
    } while (FindNextFileW(h, &find));
    FindClose(h);
  }
  RemoveDirectoryW(dir.c_str());
}

void write_file(const std::wstring& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_file(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

const char* kBody = "PRETEND-PRODUCT-BINARY-v105";

/** A record of everything the assembly did to the outside world, in order. */
struct Recorder {
  std::vector<std::string> events;
  std::string registeredVersion;
  std::string healthExpectedVersion;
  int relaunchCount = 0;

  void note(const std::string& e) { events.push_back(e); }
  bool saw(const std::string& e) const {
    for (const std::string& x : events) {
      if (x == e) return true;
    }
    return false;
  }
  /** Index of the first occurrence, or -1. Used to assert ordering rather than mere presence. */
  int at(const std::string& e) const {
    for (size_t i = 0; i < events.size(); ++i) {
      if (events[i] == e) return static_cast<int>(i);
    }
    return -1;
  }
  std::string joined() const {
    std::string s;
    for (const std::string& e : events) {
      if (!s.empty()) s += " -> ";
      s += e;
    }
    return s;
  }
};

}  // namespace

int main() {
  const std::wstring root = make_temp_dir(L"root");
  const std::wstring install = root + L"\\install";
  const std::wstring staging = root + L"\\staging";
  const std::wstring log = root + L"\\health.log";
  CreateDirectoryW(install.c_str(), nullptr);
  CreateDirectoryW(staging.c_str(), nullptr);

  // The names the assembly will try to replace come from the product's own image list, so the
  // install directory is seeded with all of them.
  for (const std::wstring& name : product_image_names()) {
    write_file(install + L"\\" + name, "old-" + narrow(name));
  }
  CreateDirectoryW((install + L"\\ui").c_str(), nullptr);
  write_file(install + L"\\ui\\shell.html", "old-shell");
  write_file(install + L"\\ui\\macro.html", "old-macro");

  // A manifest describing that same set, at a newer version.
  std::string manifest;
  {
    // sha256 of kBody, computed by the same helper the effects use so the fixture cannot drift.
    const std::wstring probe = root + L"\\probe.bin";
    write_file(probe, kBody);
    const std::string sha = sha256_file_hex(probe);
    const uint64_t size = std::char_traits<char>::length(kBody);
    DeleteFileW(probe.c_str());

    manifest = "schema=2\nreleaseId=r-0.2.105\nplatform=windows\narch=x64\nversion=0.2.105\n";
    const auto add = [&](const std::wstring& name) {
      manifest += "artifact=" + narrow(name) + "|" + std::to_string(size) + "|" + sha +
                  "|https://u.example/" + narrow(name) + "\n";
    };
    for (const std::wstring& name : product_image_names()) add(name);
    add(L"ui\\shell.html");
    add(L"ui\\macro.html");
  }

  UpdaterOptions options;
  options.installDir = install;
  options.stagingDir = staging;
  options.workDir = root + L"\\work";
  options.manifestUrl = "https://updates.example/manifest";
  options.platform = "windows";
  options.installedVersion = "0.2.104";
  options.healthLogPath = log;
  options.logPath = root + L"\\updater.log";
  options.serviceName = L"GNLinkAssemblyTestService";
  options.registryRoot = L"HKCU\\Software\\GNLinkAssemblyTest";
  options.readyEventName = L"Local\\GNLinkAssemblyTestReady";

  // A dummy the assembly can be told was running, so the capture-before-signal ordering has
  // something to capture.
  ProcessTarget target;
  target.pid = GetCurrentProcessId();
  target.imagePath = install + L"\\GNLinkHost.exe";
  target.creationTime = 12345;

  /** Builds a dependency set wired to the recorder. Cases below break one wire at a time. */
  const auto make_deps = [&](Recorder* rec) {
    UpdaterDeps deps;
    deps.log = [](const std::string&) {};
    deps.selfImagePath = root + L"\\work\\GNLinkUpdater.exe";  // outside installDir

    deps.fetchText = [&manifest, rec](const std::string& url, size_t, std::string* body,
                                      std::string*) {
      rec->note("fetchText:" + url);
      if (url.size() > 4 && url.compare(url.size() - 4, 4, ".sig") == 0) {
        *body = std::string(128, 'a');
      } else {
        *body = manifest;
      }
      return true;
    };
    deps.fetchFile = [rec](const std::string& url, const std::wstring& destPath, uint64_t,
                           std::string*) {
      rec->note("fetchFile");
      (void)url;
      write_file(destPath, kBody);
      return true;
    };
    deps.enumerateTargets = [target, rec]() {
      rec->note("enumerate");
      return std::vector<ProcessTarget>{target};
    };
    deps.requestStop = [rec](const ProcessTarget&) {
      rec->note("requestStop");
      return true;
    };
    deps.registrationOps.runProcess = [](const std::wstring&, const std::wstring&) { return 0; };
    deps.registrationOps.createShortcut = [](const std::wstring&, const std::wstring&,
                                             const std::wstring&) { return true; };
    deps.makeRelaunch = [rec](const RelaunchConfig& config,
                              const std::vector<ProcessTarget>& stopped) {
      // Records what the assembly asked for rather than starting anything. The expected version
      // it carries is the value under test.
      rec->healthExpectedVersion = config.expectedVersion;
      rec->note("makeRelaunch:targets=" + std::to_string(stopped.size()));
      RelaunchEffects effects;
      effects.relaunch = [rec]() {
        ++rec->relaunchCount;
        rec->note("relaunch");
        return true;
      };
      effects.healthCheck = [rec]() {
        rec->note("health");
        return true;
      };
      effects.lastOutcomes = []() { return std::vector<RelaunchOutcome>{}; };
      effects.userNotice = []() { return std::string(); };
      effects.lastHealthDetail = []() { return std::string("ok"); };
      return effects;
    };
    // Accepts anything: what is under test here is the wiring, and the signature algorithm has
    // its own suite. Production compiles in the real anchor and is untouched by this.
    deps.verifier = [](const std::string&, const std::vector<uint8_t>&) { return true; };
    deps.signalReady = [rec](const std::wstring&) { rec->note("signalReady"); };
    return deps;
  };

  const auto seed_install = [&]() {
    for (const std::wstring& name : product_image_names()) {
      write_file(install + L"\\" + name, "old-" + narrow(name));
    }
    write_file(install + L"\\ui\\shell.html", "old-shell");
    write_file(install + L"\\ui\\macro.html", "old-macro");
    for (const std::wstring& name : product_image_names()) {
      DeleteFileW((install + L"\\" + name + L".gnlink-old").c_str());
    }
  };

  // ================================================================ the manifest actually arrives

  {
    seed_install();
    Recorder rec;
    UpdaterEffects effects(options, make_deps(&rec));
    std::string why;
    check("the assembly builds", effects.build(&why), why);

    const UpdateOutcome out = effects.run("windows");
    // The defect this replaces: every run ended at the first stage because nothing ever supplied
    // a manifest, and NothingToDo looked like an ordinary quiet answer.
    check("the manifest is actually fetched", rec.saw("fetchText:https://updates.example/manifest"),
          rec.joined());
    check("and its detached signature too",
          rec.saw("fetchText:https://updates.example/manifest.sig"), rec.joined());
    check("so the run gets past the first stage", out.result != UpdateResult::NothingToDo,
          std::string(result_name(out.result)) + " " + out.detail);
    check("the update completes", out.result == UpdateResult::Updated,
          std::string(result_name(out.result)) + " " + out.detail);
    check("and the files really were replaced",
          read_file(install + L"\\GNLinkHost.exe") == kBody);
  }

  // ================================================================ the verified version flows

  {
    seed_install();
    Recorder rec;
    UpdaterEffects effects(options, make_deps(&rec));
    std::string why;
    effects.build(&why);
    effects.run("windows");

    // The defect this replaces: versionToInstall_ was never assigned, so registration and the
    // health check used whatever version the updater binary was compiled as. The manifest says
    // 0.2.105 and the installed version is 0.2.104; neither is this binary's kProductVersion.
    check("the version the manifest carried is the one that was verified",
          effects.verified_version() == "0.2.105", effects.verified_version());
    check("and it is what the health check was told to expect",
          rec.healthExpectedVersion == "0.2.105", rec.healthExpectedVersion);
    check("not the version this binary was compiled as",
          rec.healthExpectedVersion != std::string(narrow(remote60::native_poc::kProductVersion)),
          rec.healthExpectedVersion);
    check("and not the version that was installed before",
          rec.healthExpectedVersion != "0.2.104", rec.healthExpectedVersion);
  }

  // ================================================================ capture before the signal

  {
    seed_install();
    Recorder rec;
    UpdaterEffects effects(options, make_deps(&rec));
    std::string why;
    effects.build(&why);
    effects.run("windows");

    // The subtlest of the five. The moment the waiting caller is released it starts leaving, and
    // a process that has exited cannot be enumerated -- so enumerating afterwards produces a list
    // missing exactly the process this update told to go, and nothing would ever bring it back.
    const int captured = rec.at("enumerate");
    const int signalled = rec.at("signalReady");
    check("identities are captured", captured >= 0, rec.joined());
    check("the caller is released", signalled >= 0, rec.joined());
    check("and the capture happens BEFORE the release", captured >= 0 && signalled >= 0 &&
                                                            captured < signalled,
          rec.joined());
    check("the relaunch is built from what was captured",
          rec.saw("makeRelaunch:targets=1"), rec.joined());
  }

  // ================================================================ controls
  //
  // Each breaks one wire. Without these, everything above could pass for reasons unrelated to the
  // wiring it claims to test.

  {
    seed_install();
    Recorder rec;
    UpdaterDeps deps = make_deps(&rec);
    // As if nothing supplied a manifest -- the original defect.
    deps.fetchText = [rec_ = &rec](const std::string& url, size_t, std::string*, std::string*) {
      rec_->note("fetchText:" + url);
      return false;
    };
    UpdaterEffects effects(options, deps);
    std::string why;
    effects.build(&why);
    const UpdateOutcome out = effects.run("windows");
    check("control: with no manifest the run cannot proceed",
          out.result == UpdateResult::NothingToDo, result_name(out.result));
    check("control: and nothing was replaced",
          read_file(install + L"\\GNLinkHost.exe") != kBody);
    check("control: nothing was verified", effects.verified_version().empty(),
          effects.verified_version());
  }

  {
    seed_install();
    Recorder rec;
    UpdaterDeps deps = make_deps(&rec);
    // As if the artifact could not be downloaded: the run must abandon, and -- since a caller may
    // already have been released -- it must still put back what left.
    deps.fetchFile = [](const std::string&, const std::wstring&, uint64_t, std::string*) {
      return false;
    };
    UpdaterEffects effects(options, deps);
    std::string why;
    effects.build(&why);
    const UpdateOutcome out = effects.run("windows");
    check("control: a failed download abandons before the swap",
          out.result == UpdateResult::AbandonedBeforeSwap ||
              out.result == UpdateResult::AbandonedNotRelaunched,
          result_name(out.result));
    check("control: and the install is untouched",
          read_file(install + L"\\GNLinkHost.exe") != kBody);
  }

  {
    seed_install();
    Recorder rec;
    UpdaterDeps deps = make_deps(&rec);
    // A verifier that refuses. The manifest arrives and is rejected, which must stop everything --
    // this is the check that makes the other four worth having.
    deps.verifier = [](const std::string&, const std::vector<uint8_t>&) { return false; };
    UpdaterEffects effects(options, deps);
    std::string why;
    effects.build(&why);
    const UpdateOutcome out = effects.run("windows");
    check("control: an unverified manifest installs nothing",
          out.result == UpdateResult::NothingToDo, result_name(out.result));
    check("control: and the files are untouched",
          read_file(install + L"\\GNLinkHost.exe") != kBody);
  }

  {
    // A dependency set with a hole must not build. The updater has no defaults, and a missing
    // wire should fail at assembly rather than at some stage in the middle of an update.
    Recorder rec;
    UpdaterDeps deps = make_deps(&rec);
    deps.fetchText = nullptr;
    UpdaterEffects effects(options, deps);
    std::string why;
    check("an incomplete dependency set refuses to build", !effects.build(&why), why);
    check("and names what is missing", why.find("fetchText") != std::string::npos, why);
  }

  // ================================================================ the working copy's order

  {
    // The order is the safety property: judged before created, created before written into,
    // written before launched. A check after the copy would be reporting on a directory that had
    // already received an executable.
    std::vector<std::string> order;
    WorkingCopySteps steps;
    steps.checkDirectory = [&order]() { order.push_back("check"); return true; };
    steps.createDirectory = [&order]() { order.push_back("create"); return true; };
    steps.copySelf = [&order]() { order.push_back("copy"); return true; };
    steps.launch = [&order]() { order.push_back("launch"); return true; };
    check("the working copy is started", prepare_working_copy(steps) == WorkingCopyResult::Started);
    check("in the one order that is safe",
          order.size() == 4 && order[0] == "check" && order[1] == "create" && order[2] == "copy" &&
              order[3] == "launch",
          order.empty() ? "" : order[0]);
  }

  {
    std::vector<std::string> order;
    WorkingCopySteps steps;
    // Refused. Nothing after it may happen -- in particular nothing may be copied there.
    steps.checkDirectory = [&order]() { order.push_back("check"); return false; };
    steps.createDirectory = [&order]() { order.push_back("create"); return true; };
    steps.copySelf = [&order]() { order.push_back("copy"); return true; };
    steps.launch = [&order]() { order.push_back("launch"); return true; };
    check("a refused directory stops everything",
          prepare_working_copy(steps) == WorkingCopyResult::RefusedDirectory);
    check("and nothing is copied into it", order.size() == 1 && order[0] == "check",
          std::to_string(order.size()));
  }

  {
    std::vector<std::string> order;
    WorkingCopySteps steps;
    steps.checkDirectory = [&order]() { order.push_back("check"); return true; };
    steps.createDirectory = [&order]() { order.push_back("create"); return true; };
    steps.copySelf = [&order]() { order.push_back("copy"); return true; };
    // A launch refused by the job guard: the copy exists, and nothing runs.
    steps.launch = [&order]() { order.push_back("launch"); return false; };
    check("a refused launch is reported as one",
          prepare_working_copy(steps) == WorkingCopyResult::LaunchFailed);
  }

  remove_tree(root);
  {
    HKEY parent = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software", 0, KEY_WRITE, &parent) == ERROR_SUCCESS) {
      RegDeleteTreeW(parent, L"GNLinkAssemblyTest");
      RegCloseKey(parent);
    }
  }

  std::cout << (gFailures == 0 ? "RESULT: ALL PASS  (" : "RESULT: FAILED  (") << gChecks
            << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}

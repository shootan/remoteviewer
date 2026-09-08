// A whole release, made of real files, driven end to end.
//
// update_effects_test.cpp already covers each effect in isolation, and it does so with one fixed
// byte string standing in for every artifact. That is enough to check the mechanics and not
// enough to check the thing this suite is for: a release is SEVERAL files, they have different
// contents and different lengths, one of them is the maintenance binary, one of them lives in a
// subdirectory, and every one of them has to end up at its own destination with its own bytes. A
// suite where all the artifacts are identical cannot tell a correct placement from a swapped one.
//
// So the bodies here are the real vector files under apps/shared/update_manifest/files -- the same
// ones the JavaScript and Kotlin manifest suites read -- and their sizes are deliberately
// different (24, 48 and 18 bytes). Copying the wrong file to the wrong place fails on length
// before it fails on content.
//
// The isolation is identical to the sibling suite and for the same reason: GNLinkHost, GNLinkStream
// and GNLinkInputService are running on the machine this was written on.
//
//   * update_process_targets.cpp is NOT linked into this binary, so nothing here can find a
//     process by image name. The only processes stopped are dummies this harness starts.
//   * Every path is a fresh temp directory, and UpdateEffectsConfig has no defaults.
//   * The registry root is under HKCU, named for this process.
//   * No socket is opened; the "server" is a function this file owns.
//
// Design: docs/업데이트_기능_설계.md 3.1-3.6 and 4.1-4.4.

#include "update_effects.hpp"
#include "update_registration_wiring.hpp"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
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

// ------------------------------------------------------------------ filesystem helpers

std::wstring make_temp_dir(const wchar_t* tag) {
  wchar_t base[MAX_PATH]{};
  GetTempPathW(MAX_PATH, base);
  wchar_t unique[MAX_PATH]{};
  swprintf(unique, MAX_PATH, L"%sgnlink-reltest-%lu-%s", base, GetCurrentProcessId(), tag);
  CreateDirectoryW(unique, nullptr);
  return unique;
}

/** Recursive, unlike the sibling suite's -- this one creates subdirectories on purpose. */
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

bool exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

/**
 * A directory that is actually there.
 *
 * Written out rather than inlined because the obvious inline version is wrong in a way that
 * passes: INVALID_FILE_ATTRIBUTES is 0xFFFFFFFF, so `GetFileAttributesW(p) & FILE_ATTRIBUTE_
 * DIRECTORY` is true for a path that does not exist at all. Two assertions here were written that
 * way and only showed it when the folder creation they were checking was deliberately disabled.
 */
bool is_directory(const std::wstring& path) {
  const DWORD attrs = GetFileAttributesW(path.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::string read_file(const std::wstring& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_file(const std::wstring& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }
std::string narrow(const std::wstring& s) { return std::string(s.begin(), s.end()); }

/**
 * Every regular file under `root`, keyed by its path relative to root.
 *
 * "0 install changes" is a claim about the whole directory, not about the files a test remembered
 * to name, so it is checked by comparing two of these. A stray file, a missing one, a changed
 * byte and a leftover .gnlink-old all show up the same way.
 */
std::map<std::wstring, std::string> snapshot(const std::wstring& root,
                                             const std::wstring& prefix = L"") {
  std::map<std::wstring, std::string> out;
  WIN32_FIND_DATAW find{};
  HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &find);
  if (h == INVALID_HANDLE_VALUE) return out;
  do {
    const std::wstring name = find.cFileName;
    if (name == L"." || name == L"..") continue;
    const std::wstring full = root + L"\\" + name;
    const std::wstring rel = prefix.empty() ? name : prefix + L"\\" + name;
    if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      for (const auto& entry : snapshot(full, rel)) out[entry.first] = entry.second;
    } else {
      out[rel] = read_file(full);
    }
  } while (FindNextFileW(h, &find));
  FindClose(h);
  return out;
}

std::string describe_difference(const std::map<std::wstring, std::string>& before,
                                const std::map<std::wstring, std::string>& after) {
  for (const auto& entry : before) {
    auto it = after.find(entry.first);
    if (it == after.end()) return "missing " + narrow(entry.first);
    if (it->second != entry.second) return "changed " + narrow(entry.first);
  }
  for (const auto& entry : after) {
    if (before.find(entry.first) == before.end()) return "extra " + narrow(entry.first);
  }
  return {};
}

// ------------------------------------------------------------------ the dummy the harness stops

class DummyProcess {
 public:
  bool start() {
    wchar_t cmd[] = L"cmd.exe /c pause";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    return CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                          &si, &pi_) != FALSE;
  }
  uint32_t pid() const { return pi_.dwProcessId; }
  void kill() {
    if (pi_.hProcess) {
      TerminateProcess(pi_.hProcess, 0);
      WaitForSingleObject(pi_.hProcess, 2000);
    }
  }
  ~DummyProcess() {
    if (pi_.hProcess) CloseHandle(pi_.hProcess);
    if (pi_.hThread) CloseHandle(pi_.hThread);
  }

 private:
  PROCESS_INFORMATION pi_{};
};

// ------------------------------------------------------------------ releases

/** One file of a release: the name a manifest gives it, and the bytes behind that name. */
struct Body {
  std::string name;   // as written in the manifest, may contain a separator
  std::string bytes;
};

struct Release {
  std::string id;
  std::string version;
  std::vector<Body> bodies;
  std::string document;  // the signed-shaped manifest listing exactly these bodies

  const Body* find(const std::string& name) const {
    for (const Body& b : bodies) {
      if (b.name == name) return &b;
    }
    return nullptr;
  }
};

std::wstring gScratch;  // for hashing bytes that are not on disk yet

std::string sha256_of(const std::string& bytes) {
  const std::wstring probe = gScratch + L"\\hash.tmp";
  write_file(probe, bytes);
  const std::string hex = sha256_file_hex(probe);
  DeleteFileW(probe.c_str());
  return hex;
}

/** Builds the manifest document for a release from the bodies themselves, never from constants. */
void build_document(Release* r) {
  r->document = "schema=2\nreleaseId=" + r->id + "\nplatform=windows\narch=x64\nversion=" +
                r->version + "\n";
  for (const Body& b : r->bodies) {
    r->document += "artifact=" + b.name + "|" + std::to_string(b.bytes.size()) + "|" +
                   sha256_of(b.bytes) + "|https://updates.example/" + r->version + "/" + b.name +
                   "\n";
  }
}

/**
 * The server, and the reason it is a mutable object rather than a fixed table.
 *
 * A fetcher that always serves the release whose manifest was verified cannot express the failure
 * this design exists to prevent: a server whose "latest" moves while an update is in flight. This
 * one serves whatever release it currently points at, so pointing it somewhere else mid-update
 * reproduces exactly that.
 */
struct Server {
  const Release* current = nullptr;
  int fetches = 0;
  /** After this many fetches, switch to `then`. 0 means never. */
  int switchAfter = 0;
  const Release* then = nullptr;
  /** Refuse the fetch of this name outright, as a server that lost the file would. */
  std::string missing;
  /** Write a truncated body for this name and then report failure. */
  std::string truncate;
  /** Serve corrupted bytes for this name, so the hash cannot match. */
  std::string corrupt;

  bool fetch(const ManifestArtifact& artifact, const std::wstring& dest) {
    ++fetches;
    if (switchAfter > 0 && fetches > switchAfter && then) current = then;
    if (!missing.empty() && artifact.name == missing) return false;
    const Body* body = current ? current->find(artifact.name) : nullptr;
    if (!body) return false;
    if (!truncate.empty() && artifact.name == truncate) {
      write_file(dest, body->bytes.substr(0, body->bytes.size() / 2));
      return false;  // wrote something, then failed -- the realistic interrupted transfer
    }
    if (!corrupt.empty() && artifact.name == corrupt) {
      std::string bad = body->bytes;
      if (!bad.empty()) bad[0] = static_cast<char>(bad[0] ^ 0xFF);
      write_file(dest, bad);
      return true;  // the server thinks it succeeded; only the hash disagrees
    }
    write_file(dest, body->bytes);
    return true;
  }
};

}  // namespace

int wmain() {
  const std::wstring install = make_temp_dir(L"install");
  const std::wstring staging = make_temp_dir(L"staging");
  gScratch = make_temp_dir(L"scratch");

  // ---------------------------------------------------------------- the real vector bodies

  const std::wstring vectorDir = widen(GNLINK_UPDATE_VECTOR_DIR);
  const std::string hostBytes = read_file(vectorDir + L"\\GNLinkHost.exe");
  const std::string setupBytes = read_file(vectorDir + L"\\GNLinkSetup.exe");
  const std::string shellBytes = read_file(vectorDir + L"\\ui_shell.html");

  check("vector: GNLinkHost.exe read", hostBytes.size() == 24, std::to_string(hostBytes.size()));
  check("vector: GNLinkSetup.exe read", setupBytes.size() == 48, std::to_string(setupBytes.size()));
  check("vector: ui shell read", shellBytes.size() == 18, std::to_string(shellBytes.size()));
  // The premise of the whole suite: three files that cannot be confused with one another.
  check("vector: the three bodies differ in length",
        hostBytes.size() != setupBytes.size() && setupBytes.size() != shellBytes.size() &&
            hostBytes.size() != shellBytes.size());
  if (hostBytes.empty() || setupBytes.empty() || shellBytes.empty()) {
    std::cout << "RESULT: FAILED  (vector files missing under " << GNLINK_UPDATE_VECTOR_DIR << ")\n";
    return 1;
  }

  // Release A is the shipped vector, byte for byte. The manifest in the vector directory names
  // these same three files at these same three destinations.
  Release a;
  a.id = "r-0.2.105-test";
  a.version = "0.2.105";
  a.bodies = {{"GNLinkHost.exe", hostBytes},
              {"ui\\shell.html", shellBytes},
              {"GNLinkSetup.exe", setupBytes}};
  build_document(&a);

  // Release B is a later build of the same three files. Its bodies are derived rather than
  // shipped -- what matters for the cases that use it is only that no byte of it is a byte of A,
  // so any mixing is visible.
  Release b;
  b.id = "r-0.2.106-test";
  b.version = "0.2.106";
  b.bodies = {{"GNLinkHost.exe", "HOST-BINARY-CONTENT-v106-rebuilt"},
              {"ui\\shell.html", "<html>shell v106</html>"},
              {"GNLinkSetup.exe", "SETUP-BINARY-CONTENT-v106-longer-than-the-others-still"}};
  build_document(&b);

  const std::vector<std::wstring> payload = {L"GNLinkHost.exe", L"ui\\shell.html",
                                             L"GNLinkSetup.exe"};

  // What an install of A's predecessor looks like. Restored before every case.
  const auto seed_install = [&]() {
    remove_tree(install);
    CreateDirectoryW(install.c_str(), nullptr);
    CreateDirectoryW((install + L"\\ui").c_str(), nullptr);
    write_file(install + L"\\GNLinkHost.exe", "HOST-BINARY-CONTENT-v104");
    write_file(install + L"\\ui\\shell.html", "<html>v104</html>");
    write_file(install + L"\\GNLinkSetup.exe", "SETUP-BINARY-CONTENT-v104-and-then-some-padding");
    write_file(install + L"\\Untouched.dat", "not part of any release");
  };

  const std::wstring subkey =
      L"Software\\GNLinkReleaseTest-" + std::to_wstring(GetCurrentProcessId());
  const auto make_target = [&](const std::wstring& version) {
    install::RegistrationTarget t;
    t.installDir = install;
    t.setupPath = install + L"\\GNLinkSetup.exe";
    t.version = version;
    t.productName = L"GNLink (release test)";
    t.clientShortcutName = L"GNLink Release Test";
    t.publisher = L"Chrono Studio";
    t.serviceName = L"GNLinkReleaseTestService";
    t.firewallRuleName = L"GNLinkReleaseTestRule";
    t.uninstallRoot = HKEY_CURRENT_USER;
    t.uninstallSubkey = subkey;
    t.hostExeName = L"GNLinkHost.exe";
    t.clientExeName = L"GNLinkClient.exe";
    t.serviceExeName = L"GNLinkInputService.exe";
    t.streamExeName = L"GNLinkStream.exe";
    return t;
  };
  install::RegistrationOps recordingOps;
  recordingOps.runProcess = [](const std::wstring&, const std::wstring&) { return 0; };
  recordingOps.createShortcut = [](const std::wstring&, const std::wstring&, const std::wstring&) {
    return true;
  };
  const auto read_display_version = [&]() -> std::wstring {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
      return L"<none>";
    }
    wchar_t buffer[256]{};
    DWORD bytes = sizeof(buffer);
    DWORD type = 0;
    const LSTATUS s = RegQueryValueExW(key, L"DisplayVersion", nullptr, &type,
                                       reinterpret_cast<BYTE*>(buffer), &bytes);
    RegCloseKey(key);
    return (s == ERROR_SUCCESS) ? std::wstring(buffer) : L"<none>";
  };

  int stopCount = 0;
  const auto base_config = [&](Server* server) {
    UpdateEffectsConfig c;
    c.installDir = install;
    c.stagingDir = staging;
    c.payloadNames = payload;
    c.lockName = L"Local\\gnlink-release-test-" + std::to_wstring(GetCurrentProcessId());
    c.fetchArtifact = [server](const ManifestArtifact& artifact, const std::wstring& dest) {
      return server->fetch(artifact, dest);
    };
    c.enumerateTargets = []() { return std::vector<ProcessTarget>{}; };
    c.requestStop = [&stopCount](const ProcessTarget&) {
      ++stopCount;
      return true;
    };
    c.registryRoot = L"HKCU\\" + subkey;
    c.serviceName = L"GNLinkReleaseTestService";
    c.captureRegistration = []() { return true; };
    c.registerInstall = []() { return true; };
    c.restoreRegistration = []() { return true; };
    c.relaunch = []() { return true; };
    c.healthCheck = []() { return true; };
    c.quiesceTimeoutMs = 5000;
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    c.updaterImagePath = self;
    return c;
  };

  const auto accept = [](const std::string&, const std::vector<uint8_t>&) { return true; };

  /** The parsed shape of a release's manifest, for the cases that drive the effects directly. */
  const auto fields_for = [&](const Release& r) {
    ManifestFields f;
    f.schema = 2;
    f.releaseId = r.id;
    f.platform = "windows";
    f.arch = "x64";
    f.version = r.version;
    for (const Body& body : r.bodies) {
      ManifestArtifact art;
      art.name = body.name;
      art.size = body.bytes.size();
      art.sha256 = sha256_of(body.bytes);
      art.url = "https://updates.example/" + r.version + "/" + body.name;
      f.artifacts.push_back(art);
    }
    return f;
  };


  // ================================================================ 1. real files, exact places

  {
    // Every artifact of one release lands at its own destination with its own bytes -- including
    // the one in a subdirectory and the maintenance binary, which is a package member rather than
    // something copied in afterwards.
    seed_install();
    Server server;
    server.current = &a;
    // A real process to stop, so that "0 terminations" in the failure cases below means something.
    DummyProcess dummy;
    check("R1: dummy process started", dummy.start());
    ProcessTarget target;
    check("R1: dummy identity captured", capture_process_identity(dummy.pid(), &target));

    UpdateEffectsConfig c = base_config(&server);
    // expectedVersion is deliberately left empty. That check looks for the version as a UTF-16
    // literal inside the artifact, which is a property of a real PE built by this project; these
    // vector bodies are short ASCII stand-ins and carry no such literal. The check has its own
    // cases in update_effects_test.cpp against bodies built for it. What this suite is testing is
    // placement, and inventing a fake UTF-16 body here would trade a real file for a synthetic one
    // and lose the thing that makes the suite worth having.
    bool stopped = false;
    c.enumerateTargets = [&]() {
      return stopped ? std::vector<ProcessTarget>{} : std::vector<ProcessTarget>{target};
    };
    c.requestStop = [&](const ProcessTarget&) {
      ++stopCount;
      stopped = true;
      dummy.kill();
      return true;
    };
    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest(a.document, std::string(128, '0'));

    stopCount = 0;
    const UpdateOutcome out = run_update(e, accept, "windows");
    check("R1: the release installs", out.result == UpdateResult::Updated,
          std::string(result_name(out.result)) + " " + out.detail + " / " + e.last_error());
    // The control for every "0 terminations" assertion further down: on the path that is supposed
    // to stop something, something is stopped.
    check("R1: the successful path did stop a process", stopCount == 1, std::to_string(stopCount));

    check("R1: GNLinkHost.exe is release A's host binary",
          read_file(install + L"\\GNLinkHost.exe") == hostBytes);
    check("R1: ui\\shell.html is release A's shell, in the subdirectory",
          read_file(install + L"\\ui\\shell.html") == shellBytes);
    check("R1: GNLinkSetup.exe is release A's setup",
          read_file(install + L"\\GNLinkSetup.exe") == setupBytes);
    // Three files of three lengths: a swapped placement changes a length, not just content.
    check("R1: the placed lengths are the vector lengths",
          read_file(install + L"\\GNLinkHost.exe").size() == 24 &&
              read_file(install + L"\\ui\\shell.html").size() == 18 &&
              read_file(install + L"\\GNLinkSetup.exe").size() == 48);
    check("R1: a file no release names is left alone",
          read_file(install + L"\\Untouched.dat") == "not part of any release");
    check("R1: no backups survive a committed update",
          !exists(install + L"\\GNLinkHost.exe.gnlink-old") &&
              !exists(install + L"\\GNLinkSetup.exe.gnlink-old") &&
              !exists(install + L"\\ui\\shell.html.gnlink-old"));
    check("R1: the server was asked for each artifact exactly once", server.fetches == 3,
          std::to_string(server.fetches));
  }

  // ================================================================ 2. failures change nothing

  struct FailureCase {
    const char* label;
    std::string missing;
    std::string truncate;
    std::string corrupt;
  };
  const std::vector<FailureCase> failures = {
      {"a file the server no longer has", "ui\\shell.html", "", ""},
      {"a transfer that dies half way", "", "GNLinkSetup.exe", ""},
      {"a body whose hash does not match", "", "", "GNLinkHost.exe"},
  };

  for (const FailureCase& fc : failures) {
    seed_install();
    const auto before = snapshot(install);
    Server server;
    server.current = &a;
    server.missing = fc.missing;
    server.truncate = fc.truncate;
    server.corrupt = fc.corrupt;

    UpdateEffectsConfig c = base_config(&server);
    // A target that WOULD be stopped if the attempt ever reached Quiesce.
    DummyProcess bystander;
    check(std::string("R2 [") + fc.label + "]: bystander started", bystander.start());
    ProcessTarget target;
    check(std::string("R2 [") + fc.label + "]: bystander identity captured",
          capture_process_identity(bystander.pid(), &target));
    c.enumerateTargets = [target]() { return std::vector<ProcessTarget>{target}; };

    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest(a.document, std::string(128, '0'));

    stopCount = 0;
    const UpdateOutcome out = run_update(e, accept, "windows");
    check(std::string("R2 [") + fc.label + "]: the attempt is abandoned before any swap",
          out.result == UpdateResult::AbandonedBeforeSwap,
          std::string(result_name(out.result)) + " / " + e.last_error());
    check(std::string("R2 [") + fc.label + "]: nothing was stopped", stopCount == 0,
          std::to_string(stopCount));
    check(std::string("R2 [") + fc.label + "]: the bystander is still running",
          WaitForSingleObject(OpenProcess(SYNCHRONIZE, FALSE, bystander.pid()), 0) == WAIT_TIMEOUT);
    const std::string diff = describe_difference(before, snapshot(install));
    check(std::string("R2 [") + fc.label + "]: the installation is byte-identical", diff.empty(),
          diff);
    bystander.kill();
  }

  // ================================================================ 3. latest moves mid-update

  {
    // The hazard the releaseId exists for: the manifest is verified for A, and while the files
    // are being fetched the server starts serving B. Fetching each file against whatever is
    // current at that moment would produce an install that is part A and part B.
    seed_install();
    const auto before = snapshot(install);
    Server server;
    server.current = &a;
    server.switchAfter = 1;  // first artifact comes from A, the rest from B
    server.then = &b;

    UpdateEffectsConfig c = base_config(&server);
    DummyProcess bystander;
    check("R3: bystander started", bystander.start());
    ProcessTarget target;
    check("R3: bystander identity captured", capture_process_identity(bystander.pid(), &target));
    c.enumerateTargets = [target]() { return std::vector<ProcessTarget>{target}; };

    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest(a.document, std::string(128, '0'));

    stopCount = 0;
    const UpdateOutcome out = run_update(e, accept, "windows");
    check("R3: the attempt is refused rather than mixed",
          out.result == UpdateResult::AbandonedBeforeSwap,
          std::string(result_name(out.result)) + " / " + e.last_error());
    check("R3: nothing was stopped", stopCount == 0, std::to_string(stopCount));
    const std::string diff = describe_difference(before, snapshot(install));
    check("R3: the installation is byte-identical", diff.empty(), diff);
    // Stated as its own assertion because "unchanged" and "unmixed" are different claims, and the
    // one that matters here is that no file of B reached the installation.
    bool anyB = false;
    for (const auto& entry : snapshot(install)) {
      for (const Body& body : b.bodies) {
        if (entry.second == body.bytes) anyB = true;
      }
    }
    check("R3: no byte of release B is in the installation", !anyB);
    bystander.kill();

    // And the refusal is not terminal: asked honestly for B, the whole of B installs, with no
    // remnant of the abandoned attempt.
    Server honest;
    honest.current = &b;
    UpdateEffectsConfig c2 = base_config(&honest);
    WindowsUpdateEffects e2(c2);
    e2.set_installed_version("0.2.104");
    e2.set_manifest(b.document, std::string(128, '0'));
    const UpdateOutcome out2 = run_update(e2, accept, "windows");
    check("R3: the next honest attempt installs B", out2.result == UpdateResult::Updated,
          std::string(result_name(out2.result)) + " / " + e2.last_error());
    for (const Body& body : b.bodies) {
      check(std::string("R3: ") + body.name + " is release B's",
            read_file(install + L"\\" + widen(body.name)) == body.bytes);
    }
    bool anyA = false;
    for (const auto& entry : snapshot(install)) {
      for (const Body& body : a.bodies) {
        if (entry.second == body.bytes) anyA = true;
      }
    }
    check("R3: and no byte of release A survived into it", !anyA);
  }

  {
    // The mechanism behind "no mixing", asserted directly rather than through its consequence:
    // two releases staged one after the other occupy separate directories, so the files of one
    // are never where an attempt at the other would look for them.
    remove_tree(staging);
    CreateDirectoryW(staging.c_str(), nullptr);

    Server serverA;
    serverA.current = &a;
    UpdateEffectsConfig ca = base_config(&serverA);
    WindowsUpdateEffects ea(ca);
    check("R3: release A stages", ea.Download(fields_for(a)), ea.last_error());

    Server serverB;
    serverB.current = &b;
    UpdateEffectsConfig cb = base_config(&serverB);
    WindowsUpdateEffects eb(cb);
    check("R3: release B stages", eb.Download(fields_for(b)), eb.last_error());

    const auto staged = snapshot(staging);
    int dirsSeen = 0;
    for (const auto& entry : staged) {
      if (entry.first.find(L"\\") != std::wstring::npos) ++dirsSeen;
    }
    check("R3: both releases are staged, six files across two directories", staged.size() == 6,
          std::to_string(staged.size()));
    check("R3: every staged file sits under a release-named directory", dirsSeen == 6,
          std::to_string(dirsSeen));
    // The claim that matters: B's download did not land on top of A's files.
    bool aIntact = false;
    for (const auto& entry : staged) {
      if (entry.second == hostBytes) aIntact = true;
    }
    check("R3: release A's staged bytes were not overwritten by release B's download", aIntact);

    ea.DiscardDownload();
    eb.DiscardDownload();
  }

  // ================================================================ 4. a swap that fails part way

  {
    // One file cannot be moved aside because something else holds it open. Everything already
    // moved has to go back, and the registration has to go back to the version that was there --
    // not to the version this attempt was installing.
    seed_install();
    const auto before = snapshot(install);

    {
      RegistrationEffects seedReg = make_registration_effects(make_target(L"0.2.104"),
                                                              recordingOps);
      check("R4: previous registration seeded", seedReg.apply());
      check("R4: seeded version is 0.2.104", read_display_version() == L"0.2.104",
            narrow(read_display_version()));
    }

    // The third payload name cannot be moved aside: something else has it open with no sharing,
    // which is what a running executable looks like to a swap.
    HANDLE held = CreateFileW((install + L"\\GNLinkSetup.exe").c_str(), GENERIC_READ, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    check("R4: a payload file is held open", held != INVALID_HANDLE_VALUE);

    Server server;
    server.current = &a;
    UpdateEffectsConfig c = base_config(&server);
    RegistrationEffects reg = make_registration_effects(make_target(L"0.2.105"), recordingOps);
    c.captureRegistration = reg.capture;
    c.registerInstall = reg.apply;
    c.restoreRegistration = reg.restore;

    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest(a.document, std::string(128, '0'));

    const UpdateOutcome out = run_update(e, accept, "windows");
    check("R4: the attempt fails at the swap", out.result == UpdateResult::RolledBack,
          std::string(result_name(out.result)) + " / " + e.last_error());
    check("R4: and it fails on the file that is held", e.last_error().find("GNLinkSetup.exe") !=
                                                           std::string::npos,
          e.last_error());

    // Released before the snapshot: a file this process holds with no sharing cannot be read back,
    // and comparing against an unreadable file would pass for the wrong reason.
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);

    const std::string diff = describe_difference(before, snapshot(install));
    check("R4: every existing file is back, byte for byte", diff.empty(), diff);
    check("R4: the two files that DID move aside were restored",
          read_file(install + L"\\GNLinkHost.exe") == "HOST-BINARY-CONTENT-v104" &&
              read_file(install + L"\\ui\\shell.html") == "<html>v104</html>");
    check("R4: no release A bytes were left behind",
          read_file(install + L"\\GNLinkHost.exe") != hostBytes &&
              read_file(install + L"\\ui\\shell.html") != shellBytes);
    check("R4: the registration still names the previous version",
          read_display_version() == L"0.2.104", narrow(read_display_version()));
  }

  {
    // The other half of the same requirement: a failure AFTER the swap and AFTER registration.
    // Here the registry really was moved to 0.2.105, so "restored" is a claim with something
    // behind it -- the files go back to the previous build AND DisplayVersion goes back with them.
    seed_install();
    {
      RegistrationEffects seedReg = make_registration_effects(make_target(L"0.2.104"),
                                                              recordingOps);
      check("R4b: previous registration seeded", seedReg.apply());
    }
    const auto before = snapshot(install);

    Server server;
    server.current = &a;
    UpdateEffectsConfig c = base_config(&server);
    RegistrationEffects reg = make_registration_effects(make_target(L"0.2.105"), recordingOps);
    c.captureRegistration = reg.capture;
    c.registerInstall = reg.apply;
    c.restoreRegistration = reg.restore;
    c.healthCheck = []() { return false; };  // the new build does not come up

    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest(a.document, std::string(128, '0'));

    const UpdateOutcome out = run_update(e, accept, "windows");
    check("R4b: the attempt rolls back", out.result == UpdateResult::RolledBack,
          std::string(result_name(out.result)) + " / " + e.last_error());
    const std::string diff = describe_difference(before, snapshot(install));
    check("R4b: every file is the previous build again", diff.empty(), diff);
    check("R4b: DisplayVersion is the previous version, not the abandoned one",
          read_display_version() == L"0.2.104", narrow(read_display_version()));
    check("R4b: no backup files are left over",
          !exists(install + L"\\GNLinkHost.exe.gnlink-old") &&
              !exists(install + L"\\GNLinkSetup.exe.gnlink-old") &&
              !exists(install + L"\\ui\\shell.html.gnlink-old"));
  }

  {
    // The harder half of a mid-swap failure: phase one has finished, some new files are ALREADY
    // in place, and phase two then fails. Rollback has two different jobs here -- put back the
    // files that were moved aside, and remove the ones that were merely added, which have no
    // backup to restore and would otherwise stay behind as a permanent change from an update
    // that did not happen.
    seed_install();
    const auto before = snapshot(install);

    // A release that also ADDS a file the installation does not have.
    Release added = a;
    added.id = "r-0.2.105-added";
    added.bodies.push_back({"ui\\new_panel.html", "<html>panel added by 0.2.105</html>"});
    build_document(&added);

    Server server;
    server.current = &added;
    UpdateEffectsConfig c = base_config(&server);
    // The added file comes FIRST, and the file whose copy is blocked comes LAST. That order
    // is the whole case. Blocking the added file itself proves nothing: its copy never
    // runs, so there is nothing on disk to clean up and "the added file is not there"
    // holds whether or not Rollback does anything. That is exactly how the first version
    // of this case was written, and disabling the removal in Rollback did not fail it.
    c.payloadNames = {L"ui\\new_panel.html", L"GNLinkHost.exe",
                      L"ui\\shell.html", L"GNLinkSetup.exe"};

    WindowsUpdateEffects e(c);
    e.set_installed_version("0.2.104");
    e.set_manifest(added.document, std::string(128, '0'));

    const ManifestFields fields = fields_for(added);
    check("R4c: the release stages", e.Download(fields), e.last_error());
    check("R4c: the release verifies", e.VerifyDownload(fields), e.last_error());

    // Phase two copies from the staged files. Holding the last one open with no sharing makes its
    // copy fail after the earlier three -- including the added file -- have already been placed;
    // CopyFileW cannot open a source nobody is allowed to read.
    std::wstring stagedBlocked;
    {
      WIN32_FIND_DATAW find{};
      HANDLE h = FindFirstFileW((staging + L"\\*").c_str(), &find);
      if (h != INVALID_HANDLE_VALUE) {
        do {
          const std::wstring name = find.cFileName;
          if (name == L"." || name == L"..") continue;
          if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            stagedBlocked = staging + L"\\" + name + L"\\GNLinkSetup.exe";
          }
        } while (FindNextFileW(h, &find));
        FindClose(h);
      }
    }
    check("R4c: the staged file to block was found", exists(stagedBlocked),
          narrow(stagedBlocked));
    HANDLE held = CreateFileW(stagedBlocked.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0,
                              nullptr);
    check("R4c: it is held with no sharing", held != INVALID_HANDLE_VALUE);

    check("R4c: the swap fails in phase two", !e.Swap(), e.last_error());
    check("R4c: and it names the file it could not place",
          e.last_error().find("could not place") != std::string::npos &&
              e.last_error().find("GNLinkSetup.exe") != std::string::npos,
          e.last_error());

    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    e.DiscardDownload();

    const std::string diff = describe_difference(before, snapshot(install));
    check("R4c: the installation is byte-identical again", diff.empty(), diff);
    // Named separately because this is the part a rollback that only restores backups gets wrong.
    check("R4c: the added file was removed rather than left behind",
          !exists(install + L"\\ui\\new_panel.html"));
    check("R4c: the replaced files are the previous build's",
          read_file(install + L"\\GNLinkHost.exe") == "HOST-BINARY-CONTENT-v104" &&
              read_file(install + L"\\ui\\shell.html") == "<html>v104</html>" &&
              read_file(install + L"\\GNLinkSetup.exe") ==
                  "SETUP-BINARY-CONTENT-v104-and-then-some-padding");
    check("R4c: no backups are left over",
          !exists(install + L"\\GNLinkHost.exe.gnlink-old") &&
              !exists(install + L"\\ui\\shell.html.gnlink-old") &&
              !exists(install + L"\\GNLinkSetup.exe.gnlink-old"));
  }

  // ================================================================ 5. a release adds a folder

  {
    // Every case so far replaces files in folders the installation already has, because that is
    // what an update usually does. A release that adds a file somewhere new is the case that says
    // whether the destination path is created or merely assumed -- and the vector manifest already
    // names a destination in a subdirectory, so this is not a hypothetical shape.
    seed_install();
    const auto before = snapshot(install);

    Release grown = a;
    grown.id = "r-0.2.105-grown";
    grown.bodies.push_back({"plugins\\nested\\thing.dat", "a file in a folder that did not exist"});
    grown.document.clear();
    build_document(&grown);

    const std::vector<std::wstring> grownPayload = {L"GNLinkHost.exe", L"ui\\shell.html",
                                                    L"GNLinkSetup.exe",
                                                    L"plugins\\nested\\thing.dat"};

    {
      Server server;
      server.current = &grown;
      UpdateEffectsConfig c = base_config(&server);
      c.payloadNames = grownPayload;
      WindowsUpdateEffects e(c);
      e.set_installed_version("0.2.104");
      e.set_manifest(grown.document, std::string(128, '0'));

      const UpdateOutcome out = run_update(e, accept, "windows");
      check("R5: the release installs", out.result == UpdateResult::Updated,
            std::string(result_name(out.result)) + " / " + e.last_error());
      check("R5: the folder that did not exist was created",
            is_directory(install + L"\\plugins\\nested"));
      check("R5: and the file is in it",
            read_file(install + L"\\plugins\\nested\\thing.dat") ==
                "a file in a folder that did not exist");
    }

    // The other half: the same release, abandoned. The folder it had to create is not something
    // the previous build had, so leaving it behind would be a change from an update that did not
    // happen -- quieter than a stray file, and still a change.
    seed_install();
    {
      Server server;
      server.current = &grown;
      UpdateEffectsConfig c = base_config(&server);
      c.payloadNames = grownPayload;
      c.healthCheck = []() { return false; };
      WindowsUpdateEffects e(c);
      e.set_installed_version("0.2.104");
      e.set_manifest(grown.document, std::string(128, '0'));

      const UpdateOutcome out = run_update(e, accept, "windows");
      check("R5: the abandoned attempt rolls back", out.result == UpdateResult::RolledBack,
            std::string(result_name(out.result)) + " / " + e.last_error());
      const std::string diff = describe_difference(before, snapshot(install));
      check("R5: the installation is byte-identical again", diff.empty(), diff);
      check("R5: the created folder is gone too",
            !exists(install + L"\\plugins\\nested") && !exists(install + L"\\plugins"));
      check("R5: the folder the installation already had is still there",
            is_directory(install + L"\\ui"));
    }
  }

  // ---------------------------------------------------------------- cleanup

  remove_tree(install);
  remove_tree(staging);
  remove_tree(gScratch);
  {
    HKEY parent = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software", 0, KEY_WRITE, &parent) == ERROR_SUCCESS) {
      RegDeleteTreeW(parent, subkey.substr(std::wstring(L"Software\\").size()).c_str());
      RegCloseKey(parent);
    }
  }

  std::cout << (gFailures == 0 ? "RESULT: ALL PASS  (" : "RESULT: FAILED  (") << gChecks
            << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}

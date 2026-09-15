// Every source CMakeLists names actually exists.
//
// Commits 7dd0ffc through b941f28 do not configure. CMakeLists registered
// remote60_host_two_dispatcher_e2e_test while its source was deliberately left uncommitted, so the
// tree built for me and for nobody else: a clean checkout at any of those five commits fails at
// the generate step with "Cannot find source file", and the whole native_poc build is unavailable.
// It was found by a reviewer checking out a fixed hash, which is the only way it could have been
// found, because the file was sitting in my working directory the entire time.
//
// This is the check that would have caught it. It reads CMakeLists.txt, pulls out everything that
// looks like a source path inside an add_executable block, and asserts the file is there. Cheap,
// and it fails on the commit that breaks rather than on somebody else's afternoon.
//
// Not a substitute for configuring from a clean checkout -- it cannot see a missing include
// directory or a target that needs a library nobody linked. It covers the one mistake that is easy
// to make and invisible locally.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#ifndef REMOTE60_NATIVE_POC_DIR
#define REMOTE60_NATIVE_POC_DIR "apps/native_poc"
#endif

namespace {

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

std::string trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  const size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

bool exists(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return f.good();
}

/**
 * Everything git has under `dir`, or an empty set when git could not be asked.
 *
 * Paths come back relative to `dir` with forward slashes, which is the same shape CMakeLists uses,
 * so they compare directly.
 */
std::set<std::string> tracked_files(const std::string& dir) {
  std::set<std::string> out;
  const std::string cmd = "git -C \"" + dir + "\" ls-files 2>nul";
  FILE* p = _popen(cmd.c_str(), "r");
  if (!p) return out;
  char line[1024];
  while (std::fgets(line, sizeof(line), p)) {
    std::string entry = trim(line);
    if (!entry.empty()) out.insert(entry);
  }
  _pclose(p);
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const std::string root = argc > 1 ? argv[1] : REMOTE60_NATIVE_POC_DIR;
  const std::string listPath = root + "/CMakeLists.txt";

  std::ifstream in(listPath);
  check("CMakeLists.txt could be read", in.good(), listPath);
  if (!in.good()) {
    std::printf("\nRESULT: FAILED  (%d checks, %d failed)\n", gChecks, gFailures);
    return 1;
  }

  // Asked once. A file that is on disk but not in here builds for whoever has it and for nobody
  // else, which is the whole point of checking.
  const std::set<std::string> tracked = tracked_files(root);

  std::vector<std::string> missing;
  std::vector<std::string> untracked;
  int examined = 0;
  std::string line;
  while (std::getline(in, line)) {
    const std::string t = trim(line);
    // Anything that names a file under src/ or tools/. Generated sources live under the binary
    // directory and are written as ${CMAKE_CURRENT_BINARY_DIR}/..., which has no bare prefix and is
    // therefore skipped -- those are produced by the build and cannot be checked from here.
    if (t.rfind("src/", 0) != 0 && t.rfind("tools/", 0) != 0) continue;
    if (t.find('$') != std::string::npos) continue;
    std::string file = t;
    // Trim a trailing paren or quote if the entry closes a command on the same line.
    while (!file.empty() && (file.back() == ')' || file.back() == '"')) file.pop_back();
    if (file.empty()) continue;
    ++examined;
    if (!exists(root + "/" + file)) {
      missing.push_back(file);
    } else if (!tracked.empty() && tracked.find(file) == tracked.end()) {
      untracked.push_back(file);
    }
  }

  // The scan has to have found something, or its silence proves nothing. This is the same trap the
  // shipped-helper gate had: a parser that matches nothing reports a clean bill forever.
  check("the scan found source entries to check", examined > 50,
        std::to_string(examined) + " entries");

  check("every source CMakeLists names exists", missing.empty(),
        missing.empty() ? std::to_string(examined) + " checked"
                        : std::to_string(missing.size()) + " missing, first: " + missing.front());
  for (const std::string& m : missing) std::cout << "        missing: " << m << "\n";

  // Present is not the same as committed, and the difference is invisible from here without
  // asking. Skipped rather than passed when git is unavailable: a lookup that quietly answers
  // "nothing untracked" because it found nothing at all is the failure this file is about.
  if (tracked.empty()) {
    std::cout << "SKIP  whether those sources are committed (git could not be asked)\n";
  } else {
    check("...and every one of them is committed", untracked.empty(),
          untracked.empty()
              ? std::to_string(tracked.size()) + " tracked paths consulted"
              : std::to_string(untracked.size()) + " untracked, first: " + untracked.front());
    for (const std::string& u : untracked) {
      std::cout << "        on disk but not committed: " << u << "\n";
    }
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  ("
            << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}

// Checks the C++ side of the version-comparison contract against the shared vectors.
//
// The point of reading apps/shared/version_compare_vectors.txt rather than hard-coding cases here
// is that the same file is read by the directory server's JS test and the Android client's Kotlin
// test. Three implementations, one set of expected answers -- if they ever drift apart, one of the
// three suites fails rather than the disagreement shipping quietly.
//
// The vectors path comes from CMake (REMOTE60_VERSION_VECTORS_PATH) and can be overridden by
// argv[1], so the test can be pointed at a scratch file while working on it.

#include "version_compare.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef REMOTE60_VERSION_VECTORS_PATH
#define REMOTE60_VERSION_VECTORS_PATH "apps/shared/version_compare_vectors.txt"
#endif

namespace {

using remote60::native_poc::compare_versions;

int gFailures = 0;
int gChecks = 0;

/** Normalises to -1/0/1 so a sign convention change cannot pass unnoticed. */
int sign_of(int v) { return v < 0 ? -1 : (v > 0 ? 1 : 0); }

void check(const std::string& name, bool ok, const std::string& detail) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

std::wstring widen(const std::string& s) {
  // Vectors are ASCII by construction (digits, dots, and a few suffix characters), so a byte-wise
  // widening is exact here and keeps the test free of locale behaviour.
  return std::wstring(s.begin(), s.end());
}

}  // namespace

int main(int argc, char** argv) {
  const std::string path = (argc > 1) ? argv[1] : REMOTE60_VERSION_VECTORS_PATH;
  std::ifstream file(path);
  if (!file) {
    std::cerr << "FAIL  could not open vectors file: " << path << "\n";
    return 2;
  }

  std::string line;
  int lineNo = 0;
  int vectors = 0;
  while (std::getline(file, line)) {
    ++lineNo;
    if (!line.empty() && line.back() == '\r') line.pop_back();  // vectors file may travel as CRLF
    if (line.empty() || line[0] == '#') continue;

    const size_t first = line.find('|');
    const size_t second = (first == std::string::npos) ? std::string::npos : line.find('|', first + 1);
    if (first == std::string::npos || second == std::string::npos) {
      check("vector line " + std::to_string(lineNo), false, "malformed: " + line);
      continue;
    }
    const std::string left = line.substr(0, first);
    const std::string right = line.substr(first + 1, second - first - 1);
    const std::string expectText = line.substr(second + 1);
    const int expect = std::stoi(expectText);
    ++vectors;

    std::ostringstream label;
    label << "\"" << left << "\" vs \"" << right << "\"";

    // Wide, which is what the installer actually passes.
    const int wide = sign_of(compare_versions(widen(left), widen(right)));
    check(label.str() + " (wide)", wide == expect,
          "expected " + std::to_string(expect) + " got " + std::to_string(wide));

    // Narrow, which is what a future updater reading a manifest would pass.
    const int narrow = sign_of(compare_versions(left, right));
    check(label.str() + " (narrow)", narrow == expect,
          "expected " + std::to_string(expect) + " got " + std::to_string(narrow));

    // Reversing the arguments must reverse the sign. This is not in the vectors file because it
    // has to hold for every one of them, and stating it once is stronger than listing pairs.
    const int reversed = sign_of(compare_versions(widen(right), widen(left)));
    check(label.str() + " (antisymmetric)", reversed == -expect,
          "expected " + std::to_string(-expect) + " got " + std::to_string(reversed));
  }

  // A vectors file that silently became empty would otherwise report a clean pass.
  check("vectors file was not empty", vectors > 0, std::to_string(vectors) + " vectors");

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed, "
            << vectors << " vectors from " << path << ")\n";
  return gFailures == 0 ? 0 : 1;
}

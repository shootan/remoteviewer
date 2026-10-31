// The shipped helper carries no test switch.
//
// The deadline work needed a capture that never answers. The obvious way to get one is a hidden
// argument -- --test-stall-ms or similar -- and then a build gate to keep it out of the shipping
// binary. This project took the other road: the host resolves its helper beside the running
// executable, so a test stages a scratch directory and puts whatever it likes there under the name
// GNLinkCapture.exe. Nothing is added to the product, so there is nothing to strip.
//
// That claim should not rest on remembering. This reads the built GNLinkCapture.exe and looks for
// the shapes such a switch would take.
//
// A scanner that finds nothing is indistinguishable from a scanner that is broken, so it also has
// to find things that ARE there: the real switches. If those checks ever stop passing, the absence
// results below mean nothing and the suite says so rather than reporting a clean bill.
//
// ⚠️ Two limits, because a green run here is weaker evidence than it looks.
//
// It matches a LIST OF PATTERNS, and a switch named something not on that list walks past it. The
// list cannot be completed -- there is no enumeration of every name a hidden argument could take.
// So this corroborates; it does not carry the argument. The argument is structural: the stall is
// reached by swapping the file the host resolves as its helper, the product parses no such
// argument at all, and that is checkable by reading gdi_capture_worker_main.cpp's parse_args
// rather than by scanning bytes. This is the cheap check that would notice if that stopped being
// true by accident.
//
// And it reads the two executables it knows about. GNLinkCapture because it is the helper, and
// GNLinkStream because that is where the kill is linked and therefore the only place the fault
// translation unit could land. Anything shipped that is neither is not covered here.

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

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

std::wstring self_dir() {
  std::wstring path(32768, L'\0');
  const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  path.resize(n);
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
}

std::vector<uint8_t> read_all(const std::wstring& path) {
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return {};
  std::vector<uint8_t> out;
  uint8_t buf[65536];
  size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
  std::fclose(f);
  return out;
}

/** Look for the text as plain bytes and as UTF-16LE, which is how wide literals land. */
bool contains(const std::vector<uint8_t>& blob, const std::string& text) {
  if (text.empty() || blob.empty()) return false;
  const std::vector<uint8_t> narrow(text.begin(), text.end());
  if (std::search(blob.begin(), blob.end(), narrow.begin(), narrow.end()) != blob.end()) {
    return true;
  }
  std::vector<uint8_t> wide;
  wide.reserve(text.size() * 2);
  for (char c : text) {
    wide.push_back(static_cast<uint8_t>(c));
    wide.push_back(0);
  }
  return std::search(blob.begin(), blob.end(), wide.begin(), wide.end()) != blob.end();
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  const std::wstring helper = self_dir() + L"GNLinkCapture.exe";
  const std::vector<uint8_t> blob = read_all(helper);
  check("the built GNLinkCapture could be read", !blob.empty(),
        std::to_string(blob.size()) + " bytes");
  if (blob.empty()) {
    std::printf("\nRESULT: FAILED  (%d checks, %d failed)\n", gChecks, gFailures);
    return 1;
  }

  // The scanner has to be able to find a string before its silence means anything. These are the
  // helper's real arguments, parsed in gdi_capture_worker_main.cpp.
  const char* present[] = {"--thumbnail", "--mapping", "--done-event", "--frame-event", "--hwnd"};
  bool scannerWorks = true;
  for (const char* token : present) {
    const bool found = contains(blob, token);
    if (!found) scannerWorks = false;
    check(std::string("the scanner finds the real switch ") + token, found);
  }
  check("...so an absence below is evidence rather than a broken scan", scannerWorks);

  // What a test-only stall would look like. Not an exhaustive list of every possible name -- it
  // cannot be -- but every form this work would plausibly have used.
  const char* forbidden[] = {
      "--test-", "test-stall", "--stall", "stall-ms", "REMOTE60_TEST", "REMOTE60_STALL",
      "--fault", "--inject", "--hang",
      // The fault-injection translation unit the lingering test links in place of the real kill.
      // If this string is in a shipped binary, that file was linked into it.
      "REMOTE60_KILL_FAULT_INJECTION_TU",
  };
  for (const char* token : forbidden) {
    check(std::string("the shipped helper has no ") + token, !contains(blob, token));
  }

  // GNLinkStream is where the kill is linked, so the fault TU could only ever end up there.
  // Checked separately rather than assumed from the helper's result.
  {
    const std::vector<uint8_t> host = read_all(self_dir() + L"GNLinkStream.exe");
    check("the built GNLinkStream could be read", !host.empty(),
          std::to_string(host.size()) + " bytes");
    if (!host.empty()) {
      check("...and it does not link the kill fault-injection TU",
            !contains(host, "REMOTE60_KILL_FAULT_INJECTION_TU"),
            "that file belongs to the lingering test target only");
    }
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  ("
            << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}

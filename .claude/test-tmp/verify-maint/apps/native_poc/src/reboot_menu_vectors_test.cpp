// The shared reboot-menu / prelogin-credential vectors (apps/shared/reboot_menu_vectors.txt) against the
// C++ rule the PC viewer uses (reboot_menu_policy.hpp). The Android JVM test reads the same file
// against its Kotlin rule, so the two viewers cannot drift (prelogin-ui r1, item 8).

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "reboot_menu_policy.hpp"

#ifndef REMOTE60_REBOOT_MENU_VECTORS_PATH
#error "REMOTE60_REBOOT_MENU_VECTORS_PATH must name apps/shared/reboot_menu_vectors.txt"
#endif

namespace {
int gChecks = 0, gFailures = 0;
void check(const std::string& name, bool ok) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name.c_str());
}
}  // namespace

int main() {
  using namespace remote60::native_poc;
  std::ifstream in(REMOTE60_REBOOT_MENU_VECTORS_PATH);
  check("the shared vectors file opens", in.good());
  std::string line;
  int rows = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string flagsText, platform;
    int menuEnabled = 0, oldHost = 0, warning = 0, sealed = 0, note = 0;
    if (!(ss >> flagsText >> platform >> menuEnabled >> oldHost >> warning >> sealed >> note)) {
      check("a malformed row: " + line, false);
      continue;
    }
    ++rows;
    const uint32_t flags = static_cast<uint32_t>(std::strtoul(flagsText.c_str(), nullptr, 16));
    const RebootMenuPolicy m = DecideRebootMenu(flags);
    const PreloginCredPolicy c =
        DecidePreloginCred(platform == "android" ? ViewerPlatform::Android : ViewerPlatform::Pc, flags);
    check(line, m.menuEnabled == (menuEnabled != 0) && m.disabledBecauseOldHost == (oldHost != 0) &&
                    m.showPreloginWarning == (warning != 0) && c.sealedEntryAvailable == (sealed != 0) &&
                    c.showAndroidUpdateNote == (note != 0));
  }
  check("there are rows", rows >= 10);
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}

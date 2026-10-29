// rel-0164 old-APK lane r4: a stand-in EXE for remote60_ka_standby_holder_tool in the orchestration test's DEFAULT
// path case (oa_fixture.ps1 with no -HolderCmd runs "<BuildDir>\remote60_ka_standby_holder_tool.exe" itself -- the
// path the stub cases never took, where r3's single-element `if` expression lost its array). TEST ONLY: no viewer,
// no network. Behaves as stubs\stub_holder.py `ok`: writes <root>\holder_args.txt, standby_state.txt and
// standby_ready.txt, waits for its --stop-file, exits 0.
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <string>

int main(int argc, char** argv) {
  std::map<std::string, std::string> a;
  std::string all;
  for (int i = 1; i < argc; ++i) {
    all += std::string(i > 1 ? " " : "") + argv[i];
    if (i + 1 < argc) a[argv[i]] = argv[i + 1];
  }
  const std::string root = a["--root"], stop = a["--stop-file"];
  if (root.empty() || stop.empty()) {
    std::printf("oa_stub_holder: --root / --stop-file missing\n");
    return 2;
  }
  std::ofstream(root + "\\holder_args.txt") << all << "\n";
  std::ofstream(root + "\\standby_state.txt", std::ios::app) << "2026-01-01T00:00:00.000 mono=1 State state=ready\n";
  std::ofstream(root + "\\standby_ready.txt") << "ready host=oa-pc-a (oa_stub_holder.exe)\n";
  std::printf("oa_stub_holder: ready\n");
  std::fflush(stdout);
  for (int i = 0; i < 3000 && GetFileAttributesA(stop.c_str()) == INVALID_FILE_ATTRIBUTES; ++i) Sleep(200);
  return 0;
}

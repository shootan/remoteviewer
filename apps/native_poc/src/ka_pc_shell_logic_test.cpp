// ka-pc-impl r5 (Codex codex_ka_pc_src_r4.md R1-a): the shell's PC count with a viewer a replacement kept, on the
// shell's OWN functions (client_shell_main.cpp with its entry renamed, REMOTE60_SHELL_TEST_SEAM) and an in-memory
// registry -- no process, no window, no WebView, no network. Its files (client.log, recent_hosts.v1) go under
// REMOTE60_TEST_TMP only.
//
// The situation (Codex): A, B, C open; A's "connected" line had not reached the shell, so pressing A replaced it --
// the old A had in fact connected and stayed open (the connect's cancel does not end an established session), its
// slot moved to gReplacedViewers -- and the replacement then failed to start. A is still open; the map has B and C.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define wWinMain ka_shell_logic_product_entry
#include "client_shell_main.cpp"
#undef wWinMain

#include <cstdio>

namespace {
int gFails = 0;
void check(const char* name, bool ok, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ", detail.c_str());
  if (!ok) ++gFails;
}
ViewerCancelSlot slot_for(const std::string& host, uint64_t op) {
  ViewerCancelSlot s{};
  s.pid = 0;
  s.operation = op;
  s.hostId = host;
  return s;
}
}  // namespace

int main() {
  wchar_t root[1024] = {};
  if (!GetEnvironmentVariableW(L"REMOTE60_TEST_TMP", root, 1024)) {
    std::printf("FAIL  REMOTE60_TEST_TMP is not set\n");
    return 2;
  }
  const std::wstring r(root);
  if (r.find(L"\\.claude\\test-tmp\\") == std::wstring::npos) {
    std::printf("FAIL  REMOTE60_TEST_TMP must be under D:/remote/remote/.claude/test-tmp\n");
    return 2;
  }
  CreateDirectoryW(r.c_str(), nullptr);
  gShellTestDataDir = r + L"\\shelldata";
  gShellTestDirectoryUrl = "http://127.0.0.1:1";  // never contacted
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    gAccountId = "tester";
  }

  // A kept by the replacement; B and C open; nothing kept (no managed viewers)
  gReplacedViewers.push_back(slot_for("A", 1));
  gViewerByHost["B"] = slot_for("B", 2);
  gViewerByHost["C"] = slot_for("C", 3);

  const auto pcs = logical_pcs();
  check("R1-a: the kept-on A counts: three PCs (A, B, C)", pcs.size() == 3 && pcs.count("A"),
        "logical=" + std::to_string(pcs.size()));
  check("R1-a: ...and A is on screen (never let go to make room)", pc_visible("A"));
  std::string refusal;
  const bool admitD = reserve_for_new_pc("D", &refusal);
  check("R1-a: a fourth PC D is refused -- A, B, C are all on screen", !admitD && refusal == "three PCs are in use",
        admitD ? std::string("D admitted (four PCs open)") : refusal);
  check("R1-a: ...and nothing was reserved for it", gStagingHost.empty());
  // two viewers of ONE PC are one PC: a replacement of A that did start is not counted twice
  gViewerByHost["A"] = slot_for("A", 4);
  check("R1-a: A's kept-on viewer and its replacement are ONE PC", logical_pcs().size() == 3);

  std::printf("\nka_pc_shell_logic_test: %s\n", gFails ? "FAILED" : "ALL PASS");
  return gFails ? 1 : 0;
}

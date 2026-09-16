// Opt-in real-OS test for the host clipboard monitor and hub (K1).
//
// SKIP by default, and that is deliberate: it exercises the ACTUAL session clipboard -- it writes
// to it and reads it back -- so a bare regression sweep must never run it, or it would clobber
// whatever the person at the machine has copied. Set REMOTE60_ALLOW_CLIPBOARD_TEST=1 to run it. It
// snapshots the current Unicode clipboard text on the way in and restores it on the way out, so
// even when run it leaves the text clipboard as it found it (a non-text format cannot be restored,
// which is the other reason it is opt-in).
//
// What it proves against the real OS, which nothing else can:
//   1. a genuine local clipboard change is heard and raises the hub's generation;
//   2. applying a peer's clipboard writes the OS clipboard AND does not raise the generation --
//      the echo guard, end to end through a real WM_CLIPBOARDUPDATE;
//   3. a new local change after an apply is heard again.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "clipboard_monitor.hpp"
#include "clipboard_sync.hpp"
#include "clipboard_win32.hpp"

namespace {

int gPass = 0;
int gFail = 0;

void ok(bool cond, const std::string& what, const std::string& detail = {}) {
  if (cond) {
    ++gPass;
    std::printf("PASS  %s%s%s\n", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  } else {
    ++gFail;
    std::printf("FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  }
}

bool allowed() {
  wchar_t value[8]{};
  const DWORD n = GetEnvironmentVariableW(L"REMOTE60_ALLOW_CLIPBOARD_TEST", value, 8);
  return n > 0 && value[0] == L'1';
}

// Wait until predicate holds or the budget runs out, pumping nothing (the monitor pumps its own
// thread; this thread only sleeps).
template <typename Pred>
bool wait_until(Pred pred, int budgetMs) {
  const DWORD deadline = GetTickCount() + static_cast<DWORD>(budgetMs);
  while (GetTickCount() < deadline) {
    if (pred()) return true;
    Sleep(15);
  }
  return pred();
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (!allowed()) {
    std::printf("SKIP  clipboard_monitor_test (touches the real session clipboard)\n");
    std::printf("      Set REMOTE60_ALLOW_CLIPBOARD_TEST=1 to run it; it snapshots and restores\n");
    std::printf("      the clipboard text.\n\nRESULT: SKIPPED\n");
    return 0;
  }

  using remote60::native_poc::HostClipboardHub;
  using remote60::native_poc::clipboard_fnv1a;
  using remote60::native_poc::clipboard_read_unicode_text;
  using remote60::native_poc::clipboard_set_unicode_text;

  // Snapshot what is there so it can be put back.
  std::wstring saved;
  const bool hadText = clipboard_read_unicode_text(nullptr, &saved) && !saved.empty();

  HostClipboardHub hub;
  ok(hub.Start(), "the clipboard monitor starts");
  ok(hub.enabled(), "and reports enabled");
  // Nothing has changed yet, so the hub has captured no generation.
  ok(hub.Get().generation == 0, "no generation before any change");

  // 1. A genuine local change is heard.
  const std::wstring localOne = L"local host copy one";
  ok(clipboard_set_unicode_text(nullptr, localOne), "set a local clipboard value");
  const bool heard = wait_until([&] { return hub.Get().generation >= 1; }, 3000);
  ok(heard, "the monitor heard the local change", "generation=" +
                                                      std::to_string(hub.Get().generation));
  ok(hub.Get().text == localOne, "and captured the text");
  const uint64_t genAfterLocalOne = hub.Get().generation;

  // 2. Applying a peer's clipboard writes the OS clipboard but does NOT raise the generation.
  const std::wstring applied = L"applied from a peer";
  hub.ApplyRemote(applied, clipboard_fnv1a(applied));
  // The write happens on the monitor thread; wait for the OS clipboard to show it.
  const bool wrote = wait_until(
      [&] {
        std::wstring cur;
        return clipboard_read_unicode_text(nullptr, &cur) && cur == applied;
      },
      3000);
  ok(wrote, "ApplyRemote wrote the peer's text to the OS clipboard");
  // Give any (wrongly) generated change notification time to arrive before asserting it did not.
  Sleep(300);
  ok(hub.Get().generation == genAfterLocalOne,
     "applying a peer clipboard did NOT raise the generation (echo suppressed)",
     "generation=" + std::to_string(hub.Get().generation) + " expected=" +
         std::to_string(genAfterLocalOne));

  // 3. A new genuine local change after the apply is heard again.
  const std::wstring localTwo = L"local host copy two";
  ok(clipboard_set_unicode_text(nullptr, localTwo), "set a second local clipboard value");
  const bool heardTwo =
      wait_until([&] { return hub.Get().generation > genAfterLocalOne; }, 3000);
  ok(heardTwo, "the monitor heard the second local change",
     "generation=" + std::to_string(hub.Get().generation));
  ok(hub.Get().text == localTwo, "and captured the second text");

  hub.Stop();

  // Put the clipboard back the way it was found.
  if (hadText) {
    clipboard_set_unicode_text(nullptr, saved);
  } else {
    if (OpenClipboard(nullptr)) {
      EmptyClipboard();
      CloseClipboard();
    }
  }

  std::printf("clipboard_monitor_test: %s (%d passed, %d failed)\n", gFail == 0 ? "PASS" : "FAIL",
              gPass, gFail);
  return gFail == 0 ? 0 : 1;
}

// A window title is cut at a character, never inside one -- and the Android bridge survives one
// that was not.
//
// The defect (2026-09-25): the host copied titles into `char title[96]` with snprintf, which cuts
// at byte 95. This PC's Chrome title was 152 bytes of mixed ASCII and Hangul; the cut left two of
// a Hangul syllable's three bytes, and the phone app's NewStringUTF aborted on every window list.
//
// Cases:
//  - utf8_copy_bounded at the 95-byte limit with 1-, 2-, 3- and 4-byte characters straddling it,
//    exactly 95 bytes, empty input, embedded NUL, and malformed input (lone continuation, truncated
//    sequence, overlong, surrogate, >U+10FFFF, 0xFF) -- output always valid, bounded, NUL-terminated.
//  - utf8_limit_chars (r2, the user's decision): window titles at most 20 characters, the 20th
//    replaced by U+2026 when there were more -- 19/20/21 characters, 1- to 4-byte characters at
//    the 20th, invalid bytes, NUL.
//  - fill_window_entry, the function the host sends the window list with, on a 152-byte Hangul
//    title shaped like the measured one (byte 95 two bytes into a syllable, as measured).
//  - the conversions the Android bridge now uses instead of NewStringUTF / GetStringUTFChars:
//    malformed UTF-8 in -> U+FFFD, emoji <-> surrogate pairs, lone surrogates -> U+FFFD.
//
// Build: remote60_utf8_bounded_test (CMake). Prints PASS/FAIL lines and RESULT, exit 0/1.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "host_window_list_wire.hpp"
#include "string_util.hpp"
#include "utf8_bounded.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& what, bool ok, const std::string& detail = "") {
  ++gChecks;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ",
              detail.c_str());
  if (!ok) ++gFailures;
}

std::string hex(std::string_view s) {
  std::string out;
  char buf[4];
  for (unsigned char c : s) {
    std::snprintf(buf, sizeof(buf), "%02X ", c);
    out += buf;
  }
  return out;
}

/** Copies into a 96-byte field, as every title field on the wire is, and returns what is in it. */
std::string into96(std::string_view src, size_t* written = nullptr, bool* terminated = nullptr) {
  char field[96];
  std::memset(field, 'Z', sizeof(field));  // stale bytes, to see that the rest is cleared
  const size_t n = utf8_copy_bounded(field, sizeof(field), src);
  if (written) *written = n;
  if (terminated) {
    bool zeros = true;
    for (size_t i = n; i < sizeof(field); ++i) zeros = zeros && field[i] == '\0';
    *terminated = zeros;
  }
  return std::string(fixed_field_view(field, sizeof(field)));
}

const std::string kHan = "\xED\x95\x9C";            // U+D55C, 3 bytes
const std::string kEAcute = "\xC3\xA9";             // U+00E9, 2 bytes
const std::string kEmoji = "\xF0\x9F\x98\x80";      // U+1F600, 4 bytes

void boundary_cases() {
  // A character that begins at byte 94 and would end past 95: dropped whole.
  for (const auto& [label, ch] : {std::pair<const char*, std::string>{"2-byte", kEAcute},
                                  {"3-byte", kHan},
                                  {"4-byte", kEmoji}}) {
    const std::string src = std::string(94, 'a') + ch + "tail";
    size_t n = 0;
    bool term = false;
    const std::string out = into96(src, &n, &term);
    check(std::string(label) + " character straddling byte 95 is dropped whole, not split",
          out == std::string(94, 'a') && utf8_is_valid(out), "len=" + std::to_string(out.size()));
    check(std::string(label) + " ...and the rest of the field is zero", term);
  }
  // The same characters ending exactly at byte 95 are kept.
  for (const auto& [label, ch] : {std::pair<const char*, std::string>{"2-byte", kEAcute},
                                  {"3-byte", kHan},
                                  {"4-byte", kEmoji}}) {
    const std::string src = std::string(95 - ch.size(), 'a') + ch + "tail";
    const std::string out = into96(src);
    check(std::string(label) + " character ending exactly at byte 95 is kept",
          out.size() == 95 && out.substr(95 - ch.size()) == ch, "len=" + std::to_string(out.size()));
  }
  // 1-byte: ASCII is cut at 95 exactly, as before.
  check("ASCII longer than the field is cut at 95", into96(std::string(200, 'x')) == std::string(95, 'x'));
  check("exactly 95 bytes fits whole", into96(std::string(95, 'y')) == std::string(95, 'y'));
  check("exactly 96 bytes loses one", into96(std::string(96, 'y')) == std::string(95, 'y'));
  size_t n = 1;
  bool term = false;
  check("empty input gives an empty, terminated field", into96("", &n, &term).empty() && n == 0 && term);
  check("an embedded NUL ends the copy, as %s did",
        into96(std::string("ab\0cd", 5)) == "ab");
  char tiny[1] = {'Z'};
  check("a 1-byte field holds only the terminator", utf8_copy_bounded(tiny, 1, "abc") == 0 && tiny[0] == 0);
  check("a 0-byte field is not written", utf8_copy_bounded(tiny, 0, "abc") == 0);
}

void malformed_cases() {
  struct Case {
    const char* what;
    std::string in;
    std::string want;
  };
  const Case cases[] = {
      {"a lone continuation byte", "a\x80z", "a?z"},
      {"a sequence cut short by the end", "a\xED\x95", "a??"},
      {"a sequence cut short by ASCII", "a\xED\x95z", "a??z"},
      {"an overlong NUL (C0 80)", "a\xC0\x80z", "a??z"},
      {"an overlong 3-byte form (E0 80 AF)", "\xE0\x80\xAF", "???"},
      {"a UTF-16 surrogate encoded as UTF-8 (ED A0 80)", "\xED\xA0\x80", "???"},
      {"above U+10FFFF (F4 90 80 80)", "\xF4\x90\x80\x80", "????"},
      {"0xFF", "\xFF", "?"},
      {"valid text around the damage is kept", kHan + "\x80" + kEmoji, kHan + "?" + kEmoji},
  };
  for (const Case& c : cases) {
    const std::string out = into96(c.in);
    check(std::string("malformed input: ") + c.what + " -> '?' per bad byte, output valid",
          out == c.want && utf8_is_valid(out), hex(out));
  }
  // What the original cut produced, fed back in: the two surviving bytes become "??".
  const std::string cutHan = std::string(93, 'a') + kHan.substr(0, 2);
  check("the old host's half character arriving again is repaired, not passed on",
        utf8_is_valid(into96(cutHan)));
}

/** A title shaped like the one measured on this PC: 152 bytes, ASCII and Hangul mixed. */
std::string measured_shape_title() {
  // "…- Chrome" style: Hangul words separated by spaces, ASCII suffix, total 152 bytes.
  // 12 ASCII bytes first, so byte 95 falls two bytes into a Hangul syllable -- the measured cut
  // left exactly two of its three bytes.
  std::string t = "[GNLink] 12 ";
  const std::string word = kHan + kHan + kHan + kHan;  // 12 bytes
  while (t.size() + word.size() + 1 <= 152 - 16) {
    t += word;
    t += ' ';
  }
  t += " - Google Chrome";
  while (t.size() < 152) t += 'x';
  return t;
}

/** Counts code points of valid UTF-8 (the test's own count, not the function under test). */
size_t count_chars(std::string_view s) {
  size_t n = 0;
  for (unsigned char c : s) {
    if ((c & 0xC0) != 0x80) ++n;
  }
  return n;
}

/** The r2 window-title limit: 20 characters, U+2026 inside them when shortened. */
void limit_cases() {
  const std::string ell(kUtf8Ellipsis);
  check("limit: 21 ASCII -> 19 and an ellipsis",
        utf8_limit_chars(std::string(21, 'a'), 20) == std::string(19, 'a') + ell);
  check("limit: exactly 20 is kept whole, no ellipsis",
        utf8_limit_chars(std::string(20, 'a'), 20) == std::string(20, 'a'));
  check("limit: 19 is kept whole", utf8_limit_chars(std::string(19, 'a'), 20) == std::string(19, 'a'));
  check("limit: empty stays empty", utf8_limit_chars("", 20).empty());

  std::string han21;
  for (int i = 0; i < 21; ++i) han21 += kHan;
  const std::string han = utf8_limit_chars(han21, 20);
  check("limit: 21 Hangul -> 19 Hangul and an ellipsis (60 bytes, fits 96 with room)",
        han == han21.substr(0, 19 * 3) + ell && han.size() == 60 && count_chars(han) == 20,
        std::to_string(han.size()));
  check("limit: 20 Hangul are kept whole (60 bytes)",
        utf8_limit_chars(han21.substr(0, 60), 20) == han21.substr(0, 60));

  // A 1-, 2-, 3- and 4-byte character as the 20th and as the 21st.
  for (const auto& [label, ch] : {std::pair<const char*, std::string>{"1-byte", "b"},
                                  {"2-byte", kEAcute},
                                  {"3-byte", kHan},
                                  {"4-byte", kEmoji}}) {
    const std::string twenty = std::string(19, 'a') + ch;
    check(std::string("limit: a ") + label + " 20th character is kept",
          utf8_limit_chars(twenty, 20) == twenty);
    const std::string cut = utf8_limit_chars(twenty + "z", 20);
    check(std::string("limit: with a 21st, the ") + label + " 20th gives way to the ellipsis",
          cut == std::string(19, 'a') + ell && utf8_is_valid(cut), hex(cut));
  }
  const std::string emoji21 = [] {
    std::string e;
    for (int i = 0; i < 21; ++i) e += kEmoji;
    return e;
  }();
  check("limit: 21 emoji -> 19 whole emoji and an ellipsis, never half a pair",
        utf8_limit_chars(emoji21, 20) == emoji21.substr(0, 19 * 4) + ell);

  check("limit: invalid bytes count as one character each and become '?'",
        utf8_limit_chars("a\x80\xFF" "b", 20) == "a??b");
  check("limit: 25 invalid bytes -> 19 '?' and an ellipsis, valid",
        utf8_limit_chars(std::string(25, '\x80'), 20) == std::string(19, '?') + ell);
  check("limit: an embedded NUL ends the text", utf8_limit_chars(std::string("ab\0cd", 5), 20) == "ab");
  check("limit: a limit of 1 on a long text is just the ellipsis",
        utf8_limit_chars("abc", 1) == ell);

  // Straight into a 96-byte field, as the host does: 20 four-byte characters are 80 bytes, the
  // most a limited title can be, and the field-level cut is never needed -- but it is still there.
  char field[96];
  utf8_copy_bounded(field, sizeof(field), utf8_limit_chars(emoji21, 20));
  const std::string_view f = fixed_field_view(field, sizeof(field));
  check("limit: the longest possible limited title (19 emoji + ellipsis, 79 bytes) fits the field",
        f.size() == 79 && utf8_is_valid(f), std::to_string(f.size()));
}

void window_list_case() {
  const std::string title = measured_shape_title();
  check("the fixture title is 152 bytes of valid UTF-8, as measured",
        title.size() == 152 && utf8_is_valid(title), std::to_string(title.size()));
  // What snprintf would have done: byte 95 lands inside a Hangul character for this shape.
  const bool byteCutSplits = !utf8_is_valid(title.substr(0, 95)) &&
                             utf8_is_valid(title.substr(0, 93));
  check("...and a 95-byte cut of it lands inside a character (the case that crashed)", byteCutSplits);

  WindowListEntry src;
  src.id = 42;
  src.pid = 1234;
  src.width = 1920;
  src.height = -5;
  src.minimized = true;
  src.title = title;
  ControlWindowEntry dst{};
  fill_window_entry(dst, src);
  const std::string_view wire = fixed_field_view(dst.title, sizeof(dst.title));
  check("the host's window-list entry carries valid UTF-8", utf8_is_valid(wire), hex(wire));
  check("...NUL-terminated inside the 96-byte field",
        std::memchr(dst.title, 0, sizeof(dst.title)) != nullptr);
  // r2, the user's limit: 19 characters and U+2026. For this shape the first 19 characters are
  // "[GNLink] 12 " (12), four syllables (12 bytes), a space, two syllables (6 bytes) = 31 bytes.
  check("...as its first 19 characters and an ellipsis (20 characters, 34 bytes)",
        wire == title.substr(0, 31) + std::string(kUtf8Ellipsis), std::to_string(wire.size()));
  check("...and the other fields are filled as before",
        dst.id == 42 && dst.pid == 1234 && dst.width == 1920 && dst.height == 0 && (dst.flags & 1u));

  // The Android side of the same entry: what the bridge will hand to Java.
  const std::u16string java = utf8_to_utf16_lossy(wire);
  bool noReplacement = true;
  for (char16_t c : java) noReplacement = noReplacement && c != u'�';
  check("the bridge converts the host's entry without a single replacement character", noReplacement);
}

void bridge_cases() {
  // What an OLD host still sends: the byte-cut title. The bridge must not die on it.
  const std::string oldHostCut = measured_shape_title().substr(0, 95);
  const std::u16string u = utf8_to_utf16_lossy(oldHostCut);
  bool hasFffd = false;
  bool loneSurrogate = false;
  for (size_t i = 0; i < u.size(); ++i) {
    if (u[i] == u'�') hasFffd = true;
    if (u[i] >= 0xD800 && u[i] <= 0xDFFF) loneSurrogate = true;
  }
  check("an old host's cut title converts, with U+FFFD for the stray bytes", hasFffd && !loneSurrogate);

  const std::u16string emoji = utf8_to_utf16_lossy(kEmoji);
  check("a 4-byte character becomes a surrogate pair (modified UTF-8 cannot carry it)",
        emoji.size() == 2 && emoji[0] == 0xD83D && emoji[1] == 0xDE00);
  check("an embedded NUL survives into UTF-16", utf8_to_utf16_lossy(std::string("a\0b", 3)).size() == 3);

  const std::u16string round = utf8_to_utf16_lossy(kHan + "A" + kEmoji + kEAcute);
  check("Java -> UTF-8 round-trips Hangul, ASCII, emoji and accents",
        utf16_to_utf8_lossy(round.data(), round.size()) == kHan + "A" + kEmoji + kEAcute);
  const char16_t lone[] = {u'a', 0xD83D, u'b', 0xDE00};
  const std::string fromLone = utf16_to_utf8_lossy(lone, 4);
  check("lone surrogates from Java become U+FFFD, output valid",
        fromLone == "a\xEF\xBF\xBD" "b\xEF\xBF\xBD" && utf8_is_valid(fromLone), hex(fromLone));
}

/**
 * The real path end to end on this machine: a window whose title is the measured shape, found by
 * the host's own enumerator (GetWindowTextW -> UTF-8), then put on the wire by fill_window_entry.
 * The enumerator skips this process's own windows, so a child copy of this test owns it -- off
 * screen, never activated, ended through its handle.
 */
void live_enumerator_case(const char* self) {
  const std::string title = measured_shape_title();
  std::string cmd = std::string("\"") + self + "\" --own-window";
  std::vector<char> buf(cmd.begin(), cmd.end());
  buf.push_back('\0');
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    check("live: the window owner starts", false, "CreateProcess " + std::to_string(GetLastError()));
    return;
  }
  CloseHandle(pi.hThread);
  WaitForInputIdle(pi.hProcess, 5000);
  std::vector<WindowListEntry> windows;
  const WindowListEntry* found = nullptr;
  for (int i = 0; i < 50 && !found; ++i) {
    windows = enumerate_shareable_windows();
    for (const auto& w : windows) {
      if (w.pid == pi.dwProcessId) found = &w;
    }
    if (!found) Sleep(100);
  }
  check("live: the host's enumerator lists the test window", found != nullptr,
        std::to_string(windows.size()) + " windows");
  if (found) {
    check("live: the enumerated title is the full 152-byte title", found->title == title,
          std::to_string(found->title.size()));
    ControlWindowEntry dst{};
    fill_window_entry(dst, *found);
    const std::string_view wire = fixed_field_view(dst.title, sizeof(dst.title));
    check("live: what the host would send is 19 characters and an ellipsis, valid UTF-8",
          utf8_is_valid(wire) && wire == title.substr(0, 31) + std::string(kUtf8Ellipsis),
          hex(wire));
  }
  TerminateProcess(pi.hProcess, 0);
  WaitForSingleObject(pi.hProcess, 5000);
  CloseHandle(pi.hProcess);
}

int own_window() {
  WNDCLASSW wc{};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"remote60_utf8_title_probe";
  RegisterClassW(&wc);
  const std::wstring title = utf8_to_wide(measured_shape_title());
  HWND hwnd = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, title.c_str(),
                              WS_OVERLAPPEDWINDOW, -4000, -4000, 300, 200, nullptr, nullptr,
                              wc.hInstance, nullptr);
  if (!hwnd) return 2;
  ShowWindow(hwnd, SW_SHOWNOACTIVATE);
  MSG msg;
  // Ends when the parent terminates it; bounded in case the parent died first.
  const ULONGLONG deadline = GetTickCount64() + 30000;
  while (GetTickCount64() < deadline) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    Sleep(20);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::string(argv[1]) == "--own-window") return own_window();
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // a crash must not take the lines before it
  std::printf("utf8_bounded_test\n");
  boundary_cases();
  malformed_cases();
  limit_cases();
  window_list_case();
  bridge_cases();
  {
    // The Windows viewer (report only, not changed): it draws titles through utf8_to_wide ->
    // MultiByteToWideChar(CP_UTF8, 0). An old host's cut title does not crash it; the stray bytes
    // show as U+FFFD at the end of the card's text.
    const std::wstring w = utf8_to_wide(measured_shape_title().substr(0, 95));
    check("windows viewer: an old host's cut title converts, ending in U+FFFD",
          !w.empty() && w.back() == L'�', std::to_string(w.size()) + " chars");
  }
  live_enumerator_case(argv[0]);
  if (gFailures == 0) {
    std::printf("RESULT: ALL PASS  (%d checks, 0 failed)\n", gChecks);
    return 0;
  }
  std::printf("RESULT: FAILED  (%d checks, %d failed)\n", gChecks, gFailures);
  return 1;
}

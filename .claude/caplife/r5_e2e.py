p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/os_input_record_e2e_test.cpp'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:90], s.count(a))
    s = s.replace(a, b)


rep('''//   * the phone's Tab down/up, a pointer move, a left click and a short text each reach the dispatch
//     ("[input] injected seq=..") AND its record ("[os-input-record] api=..") -- the exact events, the text by its
//     length and hash only;
//   * the display wake at the stream start is recorded, not performed;
//   * this PC saw none of it: the cursor position and the foreground window are the same before and after.
// What it does not show: that Windows would have accepted any of it. That needs a real OS boundary (an approved
// isolated machine), not this PC.''', '''//   * the phone's Tab down/up, a pointer move, a left click and a two-letter text each reach the product's dispatch
//     ("[input] injected seq=N" / "[input-text] injected seq=N") AND the effects recorded for THAT input of THIS
//     session -- tagged "ctx what=.. seq=N epoch=E frag=K", in order (os_input_record_match.hpp; its negative
//     controls: another session, a missing or reordered fragment, an effect under another input -> FAIL);
//     the text unit by unit, by its length and hash only;
//   * the display wake at the stream start is recorded with no input behind it ("ctx none"), not performed.
// That nothing reached this PC rests on the reviewed boundary (os_input_effects.hpp: these calls are recorded INSTEAD
// of made) -- not on comparing the cursor or the foreground before and after, which proves nothing either way.
// What it does not show: that Windows would have accepted any of it. That needs a real OS boundary (an approved
// isolated machine), not this PC.''')
rep('#include "native_video_client_session.hpp"\n', '#include "native_video_client_session.hpp"\n#include "os_input_record_match.hpp"\n')
rep('''  POINT cursor0{};
  GetCursorPos(&cursor0);
  const HWND fg0 = GetForegroundWindow();

''', '')
rep('''    const bool wakeRecorded = wait_until(
        [&] { return log().find("[os-input-record] api=SendInput n=2 mouse flags=0x1 dx=1") != std::string::npos; }, 15000);
    check("the display wake at the stream start: recorded (SendInput n=2, +1/-1), not performed", wakeRecorded);
    check("...and its monitor-power broadcast recorded (PostMessage HWND_BROADCAST WM_SYSCOMMAND)",
          log().find("[os-input-record] api=PostMessage") != std::string::npos && log().find("msg=0x112") != std::string::npos);
''', '''    const bool wakeRecorded = wait_until(
        [&] { return log().find("[os-input-record] ctx none api=SendInput n=2 mouse flags=0x1 dx=1") != std::string::npos; }, 15000);
    check("the display wake at the stream start: recorded with no input behind it (ctx none), not performed", wakeRecorded);
    check("...and its monitor-power broadcast recorded (PostMessage msg=0x112)",
          log().find("[os-input-record] ctx none api=PostMessage") != std::string::npos && log().find("msg=0x112") != std::string::npos);
''')
rep('''    const int disp0 = count(log(), "[native-video-host][input] injected seq=");''',
    '''    const size_t disp0 = e2e::e2e_parse_dispatches(log()).size();
    uint64_t epoch = 0;
    {
      const std::string out = log();
      const size_t at = out.rfind("[native-video-host][control] session epoch=");
      if (at != std::string::npos) epoch = e2e::field_u64(out.substr(at), "session epoch=");
    }
    check("this session's epoch, from the host's own line", epoch != 0, std::to_string(epoch));''')
rep('''    const bool dispatched = wait_until([&] { return count(log(), "[native-video-host][input] injected seq=") >= disp0 + 5; }, 8000);
    const std::string out = log();
    check("all five events reached the product's dispatch ([input] injected seq=..)", dispatched,
          std::to_string(count(out, "[native-video-host][input] injected seq=") - disp0));
    check("...Tab down and up recorded (SendInput vk=9), not sent",
          count(out, "key vk=9 down") >= 1 && count(out, "key vk=9 up") >= 1);
    check("...the pointer recorded (SetCursorPos), the cursor not moved", count(out, "api=SetCursorPos") >= 2);
    check("...the click recorded (SendInput mouse LEFTDOWN 0x2 / LEFTUP 0x4), not sent",
          out.find("mouse flags=0x2 ") != std::string::npos && out.find("mouse flags=0x4 ") != std::string::npos);
    char want[64];
    std::snprintf(want, sizeof(want), "unicode units=2 fnv=%08x", fnv1a(text));
    const bool textRecorded = wait_until([&] { return log().find(want) != std::string::npos; }, 5000);
    check("...the text recorded by its length and hash only (the characters themselves never printed)", textRecorded, want);''',
    '''    const bool dispatched = wait_until([&] { return e2e::e2e_parse_dispatches(log()).size() >= disp0 + 6; }, 8000);
    check("all six reached the product's dispatch (5 x [input], 1 x [input-text] injected seq=..)", dispatched,
          std::to_string(e2e::e2e_parse_dispatches(log()).size() - disp0));
    // the text is sent unit by unit (apply_input_text_message -> one SendInput down/up per UTF-16 unit): one record
    // per unit, each by its own length (1) and hash
    char unitA[48], unitB[48];
    std::snprintf(unitA, sizeof(unitA), "unicode units=1 fnv=%08x", fnv1a(u"A"));
    std::snprintf(unitB, sizeof(unitB), "unicode units=1 fnv=%08x", fnv1a(u"b"));
    const std::vector<e2e::ExpectedInput> want = {
        {"input", {"api=SendInput n=1 key vk=9 down"}},
        {"input", {"api=SendInput n=1 key vk=9 up"}},
        {"input", {"api=SetCursorPos"}},
        {"input", {"api=SetCursorPos", "mouse flags=0x2 "}},
        {"input", {"api=SetCursorPos", "mouse flags=0x4 "}},
        {"text", {unitA, unitB}},
    };
    std::string why;
    const bool matched = wait_until([&] { return e2e::e2e_match_dispatches(log(), disp0, epoch, want, &why); }, 5000);
    check("each input's own effects, of THIS session, in order -- recorded, never performed "
          "(Tab down, Tab up, move, click down, click up, then 'A', 'b' unit by unit)",
          matched, why);''')
rep('''  POINT cursor1{};
  GetCursorPos(&cursor1);
  // (a person moving the mouse meanwhile would fail this check -- it can only fail toward "something moved")
  check("THIS PC saw none of it: the cursor is where it was", cursor0.x == cursor1.x && cursor0.y == cursor1.y,
        std::to_string(cursor0.x) + "," + std::to_string(cursor0.y) + " -> " + std::to_string(cursor1.x) + "," +
            std::to_string(cursor1.y));
  check("...and the foreground window is the same", GetForegroundWindow() == fg0);

''', '')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')

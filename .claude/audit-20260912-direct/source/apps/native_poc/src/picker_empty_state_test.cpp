// What the picker says when it has nothing to list.
//
// The bug this covers shipped in 0.2.121: the middle of the picker drew "no windows to share,
// press refresh" whenever the list was empty, no matter why it was empty. On a failed connect the
// header said the host could not be reached, the middle said there was nothing to share, and the
// refresh button it told the user to press was disabled.
//
// So this test is a table of states, and the point of it is the negative control at the bottom:
// the logic that shipped is reproduced here and run against the same table. If it passed, the
// table would not be measuring anything.
//
// Build: remote60_picker_empty_state_test (CMake). Run: prints "picker_empty_state_test: PASS".

#include <cstdio>

#include "viewer_picker_empty_line.hpp"

using namespace remote60::native_poc;

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool ok, const char* what) {
  ++gChecks;
  if (ok) {
    std::printf("PASS %s\n", what);
  } else {
    std::printf("  FAIL %s\n", what);
    ++gFailures;
  }
}

// One row of the table: the state the picker is in, and what the middle should say.
struct Case {
  const char* name;
  PickerEmptyInputs in;
  PickerEmptyLine want;
};

// `headerExplains` is true exactly when the sub-header is showing one of the tokens that names a
// failure or a wait (control_disconnected, session_lost, control_connect_failed, waiting_control,
// waiting_first_frame) -- see viewer_overlay_draw.cpp.
Case cases[] = {
    // (1) Errors. The header names the failure; the middle stays out of it. Pressing refresh is
    // not possible here -- the button draws disabled -- so suggesting it would be a dead end.
    {"connect failed",
     {/*locked*/ false, /*controlUp*/ false, /*headerExplains*/ true, /*listReceived*/ false},
     PickerEmptyLine::kNone},
    {"control dropped", {false, false, true, false}, PickerEmptyLine::kNone},
    // session_lost deliberately leaves the control channel up (viewer_session_watchdog.cpp:88),
    // so this one is an error WITH controlUp -- the case a plain !controlUp check would miss.
    {"session lost, control still up", {false, true, true, false}, PickerEmptyLine::kNone},

    // (2) Waiting. Nothing has arrived yet, so "there are no windows" is not known to be true.
    {"waiting for the control channel", {false, false, true, false}, PickerEmptyLine::kNone},
    {"list requested, no answer yet", {false, true, true, false}, PickerEmptyLine::kNone},

    // (3) The answer arrived and it was empty. The only state where the sentence is both true and
    // actionable: the host is reachable, so refresh will actually run.
    {"connected, list received, nothing to share",
     {false, true, false, true},
     PickerEmptyLine::kNoWindows},
    // A token can still be set from the previous round when a fresh list lands. The list is newer
    // than the token, so it wins -- otherwise a stale "waiting" would silence a real answer.
    {"list received while a stale token is still set",
     {false, true, true, true},
     PickerEmptyLine::kNoWindows},

    // (4) Locked by host config. The host still enumerates and sends the list when it sets that
    // flag (host_control_session.cpp:88 sets it, the enumeration below runs regardless), so a
    // locked picker with an empty list has nothing to share rather than a withheld list -- an
    // earlier version of this said the list was hidden, which was simply untrue. The header
    // already says the target is fixed, and refresh cannot change a fixed target, so: silence.
    {"target fixed by host config", {true, false, true, false}, PickerEmptyLine::kNone},
    {"target fixed by host config, connected", {true, true, false, true}, PickerEmptyLine::kNone},
};

const int kCaseCount = static_cast<int>(sizeof(cases) / sizeof(cases[0]));

const char* name_of(PickerEmptyLine v) {
  switch (v) {
    case PickerEmptyLine::kNone: return "none";
    case PickerEmptyLine::kNoWindows: return "no-windows";
  }
  return "?";
}

void test_table() {
  std::printf("[empty] each state gets the line that is true in it\n");
  for (const Case& c : cases) {
    const PickerEmptyLine got = picker_empty_line(c.in);
    ++gChecks;
    if (got != c.want) {
      std::printf("  FAIL %s: want %s, got %s\n", c.name, name_of(c.want), name_of(got));
      ++gFailures;
    } else {
      std::printf("PASS %s -> %s\n", c.name, name_of(got));
    }
  }
}

// The logic that shipped in 0.2.121, kept in the shape it had: the list is empty, therefore one
// sentence, regardless of everything else.
PickerEmptyLine shipped_0_2_121(const PickerEmptyInputs&) { return PickerEmptyLine::kNoWindows; }

// The negative control, and the reason the table above is evidence rather than decoration.
//
// A control that cannot fail proves nothing, so this one asserts the specific thing that used to
// be wrong: on every state it gets wrong, the shipped logic produces the refresh suggestion. If
// someone reverts the fix, the table starts failing and this keeps passing, which says plainly
// which of the two is measuring the change.
void test_negative_control() {
  std::printf("[empty] the shipped logic disagrees with the table, and always the same way\n");
  int disagreements = 0;
  for (const Case& c : cases) {
    const PickerEmptyLine old = shipped_0_2_121(c.in);
    if (old != c.want) {
      ++disagreements;
      check(old == PickerEmptyLine::kNoWindows, "shipped logic offers refresh where it is wrong");
    }
  }
  std::printf("  shipped logic differs on %d of %d states\n", disagreements, kCaseCount);
  // Seven of the nine rows: the three errors, the two waits, and both locked rows.
  check(disagreements == 7, "shipped logic is wrong on 7 of 9 states");
}

// The states are actually distinct: a decision that ignored its input could not satisfy the table,
// whichever of the two answers it returned.
void test_no_constant_answer_works() {
  std::printf("[empty] no single answer satisfies the table\n");
  bool sawNone = false, sawNoWindows = false;
  for (const Case& c : cases) {
    if (c.want == PickerEmptyLine::kNone) sawNone = true;
    if (c.want == PickerEmptyLine::kNoWindows) sawNoWindows = true;
  }
  check(sawNone, "some state wants silence");
  check(sawNoWindows, "some state wants the no-windows sentence");
}

}  // namespace

int main() {
  test_table();
  test_negative_control();
  test_no_constant_answer_works();
  if (gFailures != 0) {
    std::printf("picker_empty_state_test: FAIL (%d of %d)\n", gFailures, gChecks);
    return 1;
  }
  std::printf("picker_empty_state_test: PASS (%d checks)\n", gChecks);
  return 0;
}

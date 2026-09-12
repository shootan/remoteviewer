#pragma once

namespace remote60::native_poc {

// Whether the middle of the picker should say "nothing to share, press refresh" when the target
// list is empty.
//
// An empty list is not one situation, and for a long time this screen said that one sentence in
// all of them. On a failed connect it read: header "could not reach the host", middle "no windows
// to share, press refresh", and a refresh button that was disabled -- two explanations that
// disagreed plus an instruction the user could not carry out.
//
// The decision lives here, apart from the drawing, so it can be checked against a table of states
// rather than only photographed. It holds no sentences: this header is included by targets built
// without /utf-8, where a Korean literal does not survive the lexer. The caller maps the answer to
// words at the point where it draws them.
enum class PickerEmptyLine {
  // Say nothing. Either the header already names the error or the wait -- a second, different
  // explanation underneath is how the two came to contradict each other -- or the list being empty
  // is not a fact we have yet.
  kNone,
  // Connected, the host answered, and it has nothing to share. The only state in which the
  // suggestion to press refresh is both true and possible.
  kNoWindows,
};

struct PickerEmptyInputs {
  // The host fixed the target by configuration. Note this does NOT mean the list was withheld:
  // host_control_session.cpp:88 sets the locked flag and then enumerates the windows anyway, so a
  // locked picker with an empty list simply has nothing to share. The header already says the
  // target is fixed, which is the part the user can do nothing about, so the middle stays quiet
  // rather than offering a refresh that cannot change the selection.
  bool selectionLocked = false;
  // The control channel is up. Without it a selection is refused (viewer_picker.cpp
  // begin_pc_target_selection), so nothing on this screen is actionable.
  bool controlUp = false;
  // A status token is on screen naming the failure or the wait.
  bool headerExplains = false;
  // The host answered with a list, which happened to be empty -- as opposed to no answer yet.
  bool listReceived = false;
};

inline PickerEmptyLine picker_empty_line(const PickerEmptyInputs& in) {
  if (in.selectionLocked) return PickerEmptyLine::kNone;
  // A received list outranks a stale token: the answer arrived, so say what arrived.
  if (in.headerExplains && !in.listReceived) return PickerEmptyLine::kNone;
  if (!in.controlUp) return PickerEmptyLine::kNone;
  return PickerEmptyLine::kNoWindows;
}

}  // namespace remote60::native_poc

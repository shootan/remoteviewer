#pragma once

// dxgi-wedge-fix r1 (Codex B6, Bⓑ1): the viewer's half of "an automatic reconnect comes back to the
// same screen". The pure parts -- wire forms and the screen match -- are viewer_restore_target.hpp.
//
// Two jobs:
//   REPORT  -- each time a desktop/screen target is confirmed by the host AND its first frame decoded
//              (the picker's reveal, or a toolbar screen switch's first frame), print
//              "[viewer-restore] target=..." on stdout for the shell. Only those two points report, so a
//              pick still in flight, a failed one, or a value from before this session never does.
//   RESTORE -- started with --restore-target (only the shell's automatic reconnect passes it): once the
//              new host's window list (and, for a screen, its monitor list) is in, select that target
//              through the picker's own transaction (begin_pc_target_selection -- the same gate a click
//              uses, so the picker stays until the first frame of the answered generation). A screen
//              that is gone, changed or ambiguous, a window, a host that cannot select screens, or no
//              list in time: stay on the picker and say why.
//
// Thread: UI thread for everything except restore_note_monitor_switch (control thread) and
// restore_frame_decoded (recv thread), which only touch the atomic generation.

#include "viewer_common.hpp"  // winsock2 before windows, as every viewer header

#include <cstdint>
#include <string>

namespace remote60::native_poc::viewer {

struct ViewerState;

// Startup (before the window): the --restore-target value; empty = no restore.
void restore_init(ViewerState& ctx, const std::string& restoreArg, bool reconnect);
// UI thread: try to make progress (after a window list, a monitor list, or on the 1 s tick).
void restore_step(ViewerState& ctx, HWND hwnd);
// UI thread: a selection was revealed (CommitReveal true).
void restore_on_reveal(ViewerState& ctx);
// UI thread: a toolbar screen switch's first frame decoded (kMsgRestoreReport).
void restore_report_current(ViewerState& ctx, const char* why);
// Control thread: a screen switch outside the picker gate was answered ok with this generation.
void restore_note_monitor_switch(ViewerState& ctx, uint64_t streamGeneration);
// Recv thread: restore_frame_decoded is inline in viewer_restore_frame.hpp.
// For the picker header: the Korean line while restoring / after a failed restore; empty otherwise.
std::string restore_status_line(const ViewerState& ctx, bool controlUp);

}  // namespace remote60::native_poc::viewer

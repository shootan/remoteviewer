#pragma once

// dxgi-wedge-fix r1 (Codex B6, Bⓑ1; r2 D1/D2): the viewer's half of "an automatic reconnect comes back
// to the same screen". The pure parts -- wire forms, what a restore selects, when a target counts as
// seen -- are viewer_restore_target.hpp.
//
// Two jobs:
//   REPORT  -- print "[viewer-restore] target=..." on stdout for the shell when the user has actually
//              SEEN a target: a pick the host confirmed (the picker's reveal) is only a candidate, and it
//              is reported when a frame of its generation is presented (r2 D2). A toolbar screen switch,
//              which no generation tells apart, reports "monitor" at once -- a target never restored
//              automatically, so the early report can only send a reconnect to the picker.
//   RESTORE -- started with --restore-target (only the shell's automatic reconnect passes it): once the
//              new host's window list is in, the logical desktop is selected through the picker's own
//              transaction (begin_pc_target_selection -- the same gate a click uses). An explicitly
//              chosen screen, a window, or a host that locks the target: the picker, with the reason
//              (r2 D1). A restore counts as done when its frame is presented, not when it is revealed.
//
// Thread: UI thread for everything (the present path runs on it too).

#include "viewer_common.hpp"  // winsock2 before windows, as every viewer header

#include <cstdint>
#include <string>

namespace remote60::native_poc::viewer {

struct ViewerState;

// Startup (before the window): the --restore-target value; empty = no restore.
void restore_init(ViewerState& ctx, const std::string& restoreArg, bool reconnect);
// UI thread: try to make progress (after a window list, or on the 1 s tick).
void restore_step(ViewerState& ctx, HWND hwnd);
// UI thread: the picker accepted a pick of `windowId` for sending (begin_pc_target_selection).
void restore_note_pick(ViewerState& ctx, uint64_t windowId);
// UI thread: that pick was revealed (CommitReveal true) with the first decoded frame of `generation`.
void restore_on_reveal(ViewerState& ctx, uint64_t generation);
// UI thread: a frame of `generation` was presented (viewer_present.cpp, picker hidden).
void restore_frame_presented(ViewerState& ctx, uint64_t generation);
// UI thread: the toolbar asked the host for another screen.
void restore_note_toolbar_monitor(ViewerState& ctx);
// For the picker header: the Korean line while restoring / after a failed restore; empty otherwise.
std::string restore_status_line(const ViewerState& ctx, bool controlUp);

}  // namespace remote60::native_poc::viewer

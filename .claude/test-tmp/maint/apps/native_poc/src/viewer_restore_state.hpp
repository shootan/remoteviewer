#pragma once

// dxgi-wedge-fix r1: the restore/report state of viewer_restore.cpp (a member of PickerState).

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

#include "viewer_restore_target.hpp"

namespace remote60::native_poc::viewer {

struct RestoreState {
  enum class Phase { None, Waiting, Requested, Done, Failed };
  // UI thread only.
  std::optional<RestoreTarget> target;  // --restore-target
  Phase phase = Phase::None;
  std::string failReason;
  uint64_t waitingSinceUs = 0;
  bool reconnect = false;  // started by the shell's automatic reconnect ("재연결 중")
  // r2 D2: what the picker was last asked to select (begin_pc_target_selection), armed at its reveal and
  // committed -- reported to the shell -- only when a frame of it is presented.
  std::optional<RestoreTarget> picked;
  RestoreCommitTracker commit;
  // cross-thread: control thread notes the host's window list arrived; the UI thread reads it.
  std::atomic<bool> windowListSeen{false};
};

}  // namespace remote60::native_poc::viewer

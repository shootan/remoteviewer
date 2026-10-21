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
  bool monitorListAsked = false;
  std::string lastReported;
  bool reconnect = false;  // started by the shell's automatic reconnect ("재연결 중")
  bool revealed = false;   // a target has been confirmed and rendered in this viewer (reports may follow)
  // cross-thread: the control thread arms a report after a screen switch outside the picker gate
  // (armed = the generation of the last decoded frame at that moment, +1 so 0 means "not armed");
  // the recv thread posts once a NEWER generation decodes.
  std::atomic<uint64_t> reportAfterGenerationPlusOne{0};
  std::atomic<uint64_t> lastDecodedGeneration{0};
  // cross-thread: control thread notes whether the window list said the host selects screens through
  // the window-select transaction (0x8); the UI thread reads it.
  std::atomic<bool> windowListSeen{false};
};

}  // namespace remote60::native_poc::viewer

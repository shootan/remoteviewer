#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace remote60::host {

// Which DXGI output desktop duplication should attach to, and on which adapter.
//
// Two failures on the same machine motivated pulling this out of the Win32 code so it could be
// tested against configurations that are otherwise only reproducible by physically changing the
// display setup.
//
// The first is RDP. Connecting invalidates duplication with DXGI_ERROR_ACCESS_LOST, and the
// recreate that follows kept the D3D device it already had. But under RDP the desktop lives on
// the Microsoft Remote Display Adapter while the device was created on the physical GPU, and an
// output belonging to another adapter cannot be duplicated -- so recreate could not succeed no
// matter how often it was retried. The device has to be rebuilt on the adapter that owns the
// monitor, which means output selection has to name that adapter.
//
// The second is phantom outputs. When no output matched the target monitor the code fell back to
// the first output it had seen, and only then checked its size -- so on a machine carrying a
// Parsec adapter and a Virtual Display Driver it selected a zero-sized output and failed with
// dxgi_output_size_invalid. Validity has to be part of choosing, not a test applied afterwards.

struct DxgiOutputInfo {
  uint32_t adapterIndex = 0;
  uint32_t outputIndex = 0;
  std::string adapterDescription;
  std::string deviceName;
  // Desktop coordinates. A zero or negative extent means the output is not usable, which is how
  // virtual and indirect display adapters commonly present themselves.
  int32_t left = 0;
  int32_t top = 0;
  int32_t right = 0;
  int32_t bottom = 0;
  bool attachedToDesktop = false;
  // Opaque monitor identity; compared for equality only.
  uint64_t monitorId = 0;
  bool rotatedPortrait = false;
  // Identifies the adapter across enumerations, unlike the index, which shifts when a virtual
  // display driver appears or leaves. Duplication only works on the adapter the device is on.
  uint64_t adapterLuid = 0;

  int32_t width() const { return right - left; }
  int32_t height() const { return bottom - top; }
};

enum class DxgiSelectionReason : uint8_t {
  MonitorMatch,      // the requested monitor was found
  PrimaryFallback,   // no match, but a usable attached output was available
  NoUsableOutput,    // outputs exist, none usable
  NoOutputs,         // nothing enumerated at all
  RotationRejected,  // the only candidate is portrait and the caller requires landscape
};

struct DxgiSelection {
  bool found = false;
  DxgiOutputInfo output;
  DxgiSelectionReason reason = DxgiSelectionReason::NoOutputs;

  /** True when the caller must build its D3D device on a different adapter than it has now. */
  bool needsDeviceOnAdapter(uint32_t currentAdapterIndex) const {
    return found && output.adapterIndex != currentAdapterIndex;
  }
};

inline const char* dxgi_selection_reason_name(DxgiSelectionReason reason) {
  switch (reason) {
    case DxgiSelectionReason::MonitorMatch: return "monitor_match";
    case DxgiSelectionReason::PrimaryFallback: return "primary_fallback";
    case DxgiSelectionReason::NoUsableOutput: return "no_usable_output";
    case DxgiSelectionReason::RotationRejected: return "rotation_rejected";
    default: return "no_outputs";
  }
}

/** An output worth duplicating: attached to the desktop and with a real extent. */
inline bool dxgi_output_is_usable(const DxgiOutputInfo& output) {
  return output.attachedToDesktop && output.width() > 0 && output.height() > 0;
}

/**
 * Picks the output to duplicate across every adapter's outputs.
 *
 * `targetMonitorId` of 0 means "no preference", which selects the first usable output.
 * `landscapeOnly` rejects portrait candidates, matching the encoder's constraint.
 *
 * Usability is a filter, never a post-check: an unusable output is not a candidate at any stage,
 * so the fallback cannot hand back something the caller is about to reject.
 */
inline DxgiSelection select_dxgi_output(const std::vector<DxgiOutputInfo>& outputs,
                                        uint64_t targetMonitorId, bool landscapeOnly) {
  DxgiSelection selection;
  if (outputs.empty()) {
    selection.reason = DxgiSelectionReason::NoOutputs;
    return selection;
  }

  const DxgiOutputInfo* fallback = nullptr;
  bool sawUsable = false;
  bool rejectedForRotation = false;

  for (const DxgiOutputInfo& candidate : outputs) {
    if (!dxgi_output_is_usable(candidate)) continue;
    sawUsable = true;
    if (landscapeOnly && (candidate.rotatedPortrait || candidate.height() > candidate.width())) {
      rejectedForRotation = true;
      continue;
    }
    if (targetMonitorId != 0 && candidate.monitorId == targetMonitorId) {
      selection.found = true;
      selection.output = candidate;
      selection.reason = DxgiSelectionReason::MonitorMatch;
      return selection;
    }
    if (!fallback) fallback = &candidate;
  }

  if (fallback) {
    selection.found = true;
    selection.output = *fallback;
    // Naming the requested monitor and not finding it is different from not having asked, and
    // the distinction matters when the desktop has just moved to another adapter.
    selection.reason = targetMonitorId == 0 ? DxgiSelectionReason::MonitorMatch
                                            : DxgiSelectionReason::PrimaryFallback;
    return selection;
  }

  selection.reason = rejectedForRotation  ? DxgiSelectionReason::RotationRejected
                     : sawUsable          ? DxgiSelectionReason::NoUsableOutput
                                          : DxgiSelectionReason::NoUsableOutput;
  return selection;
}

}  // namespace remote60::host

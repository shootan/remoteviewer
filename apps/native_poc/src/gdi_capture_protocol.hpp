#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace remote60::native_poc::gdi_capture {

constexpr uint32_t kMagic = 0x30494447u;  // "GDI0"
constexpr uint32_t kVersion = 1;
constexpr uint32_t kSlotCount = 3;

enum SlotState : LONG {
  SlotFree = 0,
  SlotWriting = 1,
  SlotReady = 2,
  SlotReading = 3,
};

struct SharedSlot {
  volatile LONG state = SlotFree;
  uint32_t reserved0 = 0;
  uint64_t sequence = 0;
  uint64_t captureQpcUs = 0;
  uint64_t captureCopyUs = 0;
};

struct SharedHeader {
  uint32_t magic = kMagic;
  uint32_t version = kVersion;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  uint32_t frameBytes = 0;
  uint32_t slotCount = kSlotCount;
  uint32_t reserved0 = 0;
  SharedSlot slots[kSlotCount]{};
};

inline size_t align_up(size_t value, size_t alignment) {
  return ((value + alignment - 1) / alignment) * alignment;
}

inline size_t frame_data_offset(uint32_t slot, uint32_t frameBytes) {
  return align_up(sizeof(SharedHeader), 64) + static_cast<size_t>(slot) * frameBytes;
}

inline size_t mapping_bytes(uint32_t frameBytes) {
  return frame_data_offset(kSlotCount, frameBytes);
}

}  // namespace remote60::native_poc::gdi_capture

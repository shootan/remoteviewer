#pragma once

#include <cstdint>

namespace remote60::native_poc {

constexpr uint32_t kSecureInputMagic = 0x53494E50u;  // SINP
constexpr wchar_t kSecureInputPipeName[] = L"\\\\.\\pipe\\GNLinkSecureInput";
constexpr wchar_t kSecureInputServiceName[] = L"GNLinkSecureInput";
constexpr uint16_t kSecureInputTextMax = 64;

enum class SecureInputKind : uint16_t {
  InputEvent = 1,
  InputText = 2,
  Shutdown = 3,
};

#pragma pack(push, 1)
struct SecureInputMessage {
  uint32_t magic = kSecureInputMagic;
  uint16_t size = static_cast<uint16_t>(sizeof(SecureInputMessage));
  uint16_t kind = 0;
  uint32_t inputWidth = 0;
  uint32_t inputHeight = 0;
  uint16_t eventKind = 0;
  uint16_t buttons = 0;
  int32_t x = 0;
  int32_t y = 0;
  int32_t wheelDelta = 0;
  uint32_t keyCode = 0;
  uint16_t textCount = 0;
  uint16_t reserved = 0;
  // Where the client's coordinate space sits on the desktop, in desktop coordinates. Zero
  // width/height means "unknown", which makes the agent fall back to the virtual screen. A
  // monitor left of the primary gives a negative origin, so these are signed.
  int32_t targetOriginX = 0;
  int32_t targetOriginY = 0;
  uint32_t targetWidth = 0;
  uint32_t targetHeight = 0;
  uint16_t text[kSecureInputTextMax] = {};
};
#pragma pack(pop)

}  // namespace remote60::native_poc

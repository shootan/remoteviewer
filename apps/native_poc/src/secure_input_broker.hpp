#pragma once

#include <windows.h>

#include <cstdint>
#include <mutex>
#include <string>

#include "poc_protocol.hpp"
#include "secure_input_protocol.hpp"

namespace remote60::native_poc {

class SecureInputBrokerClient {
 public:
  SecureInputBrokerClient() = default;
  ~SecureInputBrokerClient();
  SecureInputBrokerClient(const SecureInputBrokerClient&) = delete;
  SecureInputBrokerClient& operator=(const SecureInputBrokerClient&) = delete;

  bool EnsureInstalledAndConnected(const std::wstring& serviceExePath, std::string* status);
  bool SendInputEvent(const ControlInputEventMessage& input, uint32_t inputWidth,
                      uint32_t inputHeight);
  bool SendInputText(const ControlInputTextMessage& text, uint32_t inputWidth,
                     uint32_t inputHeight);

  /**
   * Where the client's coordinate space sits on the desktop, in desktop coordinates.
   *
   * Set it when the capture geometry changes. Without it the agent maps onto the virtual screen,
   * which is right for full-desktop capture and wrong for anything narrower -- and the agent has
   * no way to work it out on its own, since it only ever sees a width and a height.
   */
  void SetTargetRect(int32_t originX, int32_t originY, uint32_t width, uint32_t height);
  bool connected() const;

 private:
  bool ConnectLocked();
  bool WriteLocked(const SecureInputMessage& message);
  void apply_target_rect_locked(SecureInputMessage* message) const;

  mutable std::mutex mu_;
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  int32_t targetOriginX_ = 0;
  int32_t targetOriginY_ = 0;
  uint32_t targetWidth_ = 0;
  uint32_t targetHeight_ = 0;
};

std::wstring sibling_executable_path(const wchar_t* fileName);

}  // namespace remote60::native_poc

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
  // Tests: another pipe than the service's (a \\.\pipe\GNLinkTest-... one).
  explicit SecureInputBrokerClient(std::wstring pipeName) : pipeName_(std::move(pipeName)) {}
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
  // r10 T2: every message from now on may be injected only on the logon desktop (the --prelogin worker's).
  void SetLogonDesktopOnly(bool on);
  // r15 L6: the messages from now on were decided on the screen saver, as wakes (kSecureInputFlagScreenSaverWake).
  void SetDecidedOnScreenSaver(bool on);
  /** The rect currently stamped into every message (0x0 = none: the host refuses to send). */
  void GetTargetRect(int32_t* originX, int32_t* originY, uint32_t* width, uint32_t* height) const;
  /**
   * Send with an explicit rect stamped into THIS message under the same lock as the write, so
   * a concurrent SetTargetRect can neither replace nor race it (P9 dispatch path).
   */
  bool SendInputEventWithRect(const ControlInputEventMessage& input, uint32_t inputWidth, uint32_t inputHeight,
                              int32_t originX, int32_t originY, uint32_t width, uint32_t height);
  bool SendInputTextWithRect(const ControlInputTextMessage& text, uint32_t inputWidth, uint32_t inputHeight,
                             int32_t originX, int32_t originY, uint32_t width, uint32_t height);
  bool connected() const;

 private:
  static constexpr uint64_t kReconnectCooldownMs = 1000;
  // r14 M1: `budgetMs` -- the startup connect waits for the service's pipe (3 s); the input path does not
  // wait at all (it runs on the UDP reader thread): one try, and after a failure no new try for
  // kReconnectCooldownMs.
  bool ConnectLocked(uint32_t budgetMs);
  bool WriteLocked(SecureInputMessage message);
  void apply_target_rect_locked(SecureInputMessage* message) const;

  mutable std::mutex mu_;
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  std::wstring pipeName_ = kSecureInputPipeName;
  uint64_t nextConnectMs_ = 0;  // r14 M1: no connect attempt before this (GetTickCount64)
  int32_t targetOriginX_ = 0;
  uint16_t flags_ = 0;  // r10 T2: stamped on every message (SetLogonDesktopOnly)
  int32_t targetOriginY_ = 0;
  uint32_t targetWidth_ = 0;
  uint32_t targetHeight_ = 0;
};

std::wstring sibling_executable_path(const wchar_t* fileName);

}  // namespace remote60::native_poc

#pragma once

#include <windows.h>

#include <cstdint>
#include <mutex>
#include <string>

#include "poc_protocol.hpp"
#include "secure_input_protocol.hpp"
#include "trusted_service_pipe.hpp"

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

  /**
   * TEST ONLY (secure-pipe-identity gate): point the REAL connect+verify+send path at a test pipe/
   * service name and an injected gatherer, so the isolated pre-publish gate exercises this exact code
   * without the installed service. Shipping callers never touch this -- the defaults are the production
   * name/service and the production gatherer, and there is no runtime option to reach it.
   */
  void SetVerificationEndpointForTest(const wchar_t* pipeName, const wchar_t* serviceName,
                                      ServerIdentityGatherer gather) {
    testPipeName_ = pipeName;
    testServiceName_ = serviceName;
    testGatherer_ = gather;
  }

 private:
  static constexpr uint64_t kReconnectCooldownMs = 1000;
  // r14 M1: `budgetMs` -- the startup connect waits for the service's pipe (3 s); the input path (the UDP
  // reader thread) makes one try -- its wait for the pipe is bounded to 1 ms, the server identity check after
  // it takes its own time (r21: measured, not instant) -- and after a failure no new try for kReconnectCooldownMs.
  bool ConnectLocked(uint32_t budgetMs);
  // r20 merge: the one open, for both connect paths -- the server verified before a byte is written.
  bool OpenVerifiedLocked(uint32_t waitMs);
  bool WriteLocked(SecureInputMessage message);
  void apply_target_rect_locked(SecureInputMessage* message) const;

  mutable std::mutex mu_;
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  const wchar_t* testPipeName_ = nullptr;     // nullptr -> pipeName_ (production kSecureInputPipeName)
  const wchar_t* testServiceName_ = nullptr;  // nullptr -> production kSecureInputServiceName
  ServerIdentityGatherer testGatherer_ = nullptr;  // nullptr -> production GatherServerIdentity
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

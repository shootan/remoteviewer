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
  bool ConnectLocked();
  bool WriteLocked(const SecureInputMessage& message);
  void apply_target_rect_locked(SecureInputMessage* message) const;

  mutable std::mutex mu_;
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  const wchar_t* testPipeName_ = nullptr;     // nullptr -> production kSecureInputPipeName
  const wchar_t* testServiceName_ = nullptr;  // nullptr -> production kSecureInputServiceName
  ServerIdentityGatherer testGatherer_ = nullptr;  // nullptr -> production GatherServerIdentity
  int32_t targetOriginX_ = 0;
  int32_t targetOriginY_ = 0;
  uint32_t targetWidth_ = 0;
  uint32_t targetHeight_ = 0;
};

std::wstring sibling_executable_path(const wchar_t* fileName);

}  // namespace remote60::native_poc

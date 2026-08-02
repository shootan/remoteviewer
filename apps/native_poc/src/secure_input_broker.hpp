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
  bool connected() const;

 private:
  bool ConnectLocked();
  bool WriteLocked(const SecureInputMessage& message);

  mutable std::mutex mu_;
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
};

std::wstring sibling_executable_path(const wchar_t* fileName);

}  // namespace remote60::native_poc

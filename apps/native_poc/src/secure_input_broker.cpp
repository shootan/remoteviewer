#include "secure_input_broker.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace remote60::native_poc {

namespace {

bool process_is_elevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION elevation{};
  DWORD bytes = 0;
  const bool elevated = GetTokenInformation(token, TokenElevation, &elevation,
                                            sizeof(elevation), &bytes) &&
                        elevation.TokenIsElevated != 0;
  CloseHandle(token);
  return elevated;
}

std::string win32_error_text(const char* prefix) {
  char line[160]{};
  std::snprintf(line, sizeof(line), "%s error=%lu", prefix,
                static_cast<unsigned long>(GetLastError()));
  return line;
}

bool wait_for_service_state(SC_HANDLE service, DWORD desired, DWORD timeoutMs) {
  const ULONGLONG deadline = GetTickCount64() + timeoutMs;
  SERVICE_STATUS_PROCESS status{};
  DWORD bytes = 0;
  while (GetTickCount64() < deadline) {
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<BYTE*>(&status), sizeof(status), &bytes)) {
      return false;
    }
    if (status.dwCurrentState == desired) return true;
    Sleep(50);
  }
  return false;
}

}  // namespace

SecureInputBrokerClient::~SecureInputBrokerClient() {
  std::lock_guard<std::mutex> lock(mu_);
  if (pipe_ != INVALID_HANDLE_VALUE) CloseHandle(pipe_);
  pipe_ = INVALID_HANDLE_VALUE;
}

std::wstring sibling_executable_path(const wchar_t* fileName) {
  std::vector<wchar_t> path(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) return {};
  std::wstring result(path.data(), length);
  const size_t slash = result.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return {};
  result.resize(slash + 1);
  result += fileName ? fileName : L"";
  return result;
}

bool SecureInputBrokerClient::EnsureInstalledAndConnected(const std::wstring& serviceExePath,
                                                           std::string* status) {
  std::lock_guard<std::mutex> lock(mu_);
  if (pipe_ != INVALID_HANDLE_VALUE) return true;
  if (!process_is_elevated()) {
    if (status) *status = "secure input requires elevated host";
    return false;
  }
  if (serviceExePath.empty() || GetFileAttributesW(serviceExePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
    if (status) *status = "secure input service executable missing";
    return false;
  }

  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
  if (!manager) {
    if (status) *status = win32_error_text("OpenSCManager");
    return false;
  }
  const std::wstring quotedServicePath = L"\"" + serviceExePath + L"\"";
  SC_HANDLE service = OpenServiceW(manager, kSecureInputServiceName,
                                   SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP |
                                       SERVICE_CHANGE_CONFIG | SERVICE_QUERY_CONFIG);
  bool binaryPathChanged = false;
  if (!service) {
    service = CreateServiceW(manager, kSecureInputServiceName, L"GNLink secure desktop input",
                             SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP |
                                 SERVICE_CHANGE_CONFIG | SERVICE_QUERY_CONFIG,
                             SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                             SERVICE_ERROR_NORMAL, quotedServicePath.c_str(), nullptr, nullptr,
                             nullptr, nullptr, nullptr);
  } else {
    DWORD configBytes = 0;
    (void)QueryServiceConfigW(service, nullptr, 0, &configBytes);
    if (configBytes > 0) {
      std::vector<uint8_t> configStorage(configBytes);
      auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(configStorage.data());
      if (QueryServiceConfigW(service, config, configBytes, &configBytes)) {
        const std::wstring configured = config->lpBinaryPathName ? config->lpBinaryPathName : L"";
        binaryPathChanged = _wcsicmp(configured.c_str(), quotedServicePath.c_str()) != 0 &&
                            _wcsicmp(configured.c_str(), serviceExePath.c_str()) != 0;
      }
    }
    (void)ChangeServiceConfigW(service, SERVICE_NO_CHANGE, SERVICE_DEMAND_START,
                               SERVICE_NO_CHANGE, quotedServicePath.c_str(), nullptr, nullptr,
                               nullptr, nullptr, nullptr, nullptr);
  }
  if (!service) {
    if (status) *status = win32_error_text("Create/OpenService");
    CloseServiceHandle(manager);
    return false;
  }

  SERVICE_STATUS_PROCESS serviceStatus{};
  DWORD bytes = 0;
  bool running = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                      reinterpret_cast<BYTE*>(&serviceStatus),
                                      sizeof(serviceStatus), &bytes) &&
                 serviceStatus.dwCurrentState == SERVICE_RUNNING;
  if (running && binaryPathChanged) {
    SERVICE_STATUS stopped{};
    if (ControlService(service, SERVICE_CONTROL_STOP, &stopped)) {
      running = !wait_for_service_state(service, SERVICE_STOPPED, 5000);
    }
  }
  if (!running) {
    if (!StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
      if (status) *status = win32_error_text("StartService");
      CloseServiceHandle(service);
      CloseServiceHandle(manager);
      return false;
    }
    running = wait_for_service_state(service, SERVICE_RUNNING, 5000);
  }
  CloseServiceHandle(service);
  CloseServiceHandle(manager);
  if (!running) {
    if (status) *status = "secure input service start timed out";
    return false;
  }
  if (!ConnectLocked()) {
    if (status) *status = win32_error_text("secure input pipe connect");
    return false;
  }
  if (status) *status = "secure input connected";
  return true;
}

bool SecureInputBrokerClient::ConnectLocked() {
  if (pipe_ != INVALID_HANDLE_VALUE) return true;
  (void)WaitNamedPipeW(kSecureInputPipeName, 3000);
  pipe_ = CreateFileW(kSecureInputPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL, nullptr);
  return pipe_ != INVALID_HANDLE_VALUE;
}

bool SecureInputBrokerClient::WriteLocked(const SecureInputMessage& message) {
  if (!ConnectLocked()) return false;
  DWORD written = 0;
  if (WriteFile(pipe_, &message, sizeof(message), &written, nullptr) &&
      written == sizeof(message)) {
    return true;
  }
  CloseHandle(pipe_);
  pipe_ = INVALID_HANDLE_VALUE;
  if (!ConnectLocked()) return false;
  written = 0;
  return WriteFile(pipe_, &message, sizeof(message), &written, nullptr) &&
         written == sizeof(message);
}

bool SecureInputBrokerClient::SendInputEvent(const ControlInputEventMessage& input,
                                              uint32_t inputWidth,
                                              uint32_t inputHeight) {
  SecureInputMessage message{};
  message.kind = static_cast<uint16_t>(SecureInputKind::InputEvent);
  message.inputWidth = inputWidth;
  message.inputHeight = inputHeight;
  message.eventKind = input.kind;
  message.buttons = input.buttons;
  message.x = input.x;
  message.y = input.y;
  message.wheelDelta = input.wheelDelta;
  message.keyCode = input.keyCode;
  std::lock_guard<std::mutex> lock(mu_);
  return WriteLocked(message);
}

bool SecureInputBrokerClient::SendInputText(const ControlInputTextMessage& text,
                                             uint32_t inputWidth,
                                             uint32_t inputHeight) {
  SecureInputMessage message{};
  message.kind = static_cast<uint16_t>(SecureInputKind::InputText);
  message.inputWidth = inputWidth;
  message.inputHeight = inputHeight;
  message.textCount = std::min<uint16_t>(text.utf16Count, kSecureInputTextMax);
  std::copy_n(text.utf16, message.textCount, message.text);
  std::lock_guard<std::mutex> lock(mu_);
  return WriteLocked(message);
}

bool SecureInputBrokerClient::connected() const {
  std::lock_guard<std::mutex> lock(mu_);
  return pipe_ != INVALID_HANDLE_VALUE;
}

}  // namespace remote60::native_poc

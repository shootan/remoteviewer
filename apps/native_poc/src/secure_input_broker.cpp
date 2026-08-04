#include "secure_input_broker.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
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

  // Connect-only: never create the service and never rewrite its binary path.
  //
  // Registration happens once, from the elevated installer, against the admin-only install
  // directory. When the host could also create or repoint it, running any copy of the product
  // from a user-writable folder silently aimed the LocalSystem service at that copy -- which
  // hands SYSTEM to anyone who can write there, defeating the point of installing under
  // Program Files. Requesting no CREATE/CHANGE rights makes that impossible by construction.
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!manager) {
    if (status) *status = win32_error_text("OpenSCManager");
    return false;
  }
  SC_HANDLE service =
      OpenServiceW(manager, kSecureInputServiceName, SERVICE_QUERY_STATUS | SERVICE_START);
  if (!service) {
    const DWORD err = GetLastError();
    CloseServiceHandle(manager);
    if (status) {
      *status = err == ERROR_SERVICE_DOES_NOT_EXIST
                    ? "secure input service is not installed; run the GNLink installer"
                    : win32_error_text("OpenService");
    }
    return false;
  }

  SERVICE_STATUS_PROCESS serviceStatus{};
  DWORD bytes = 0;
  bool running = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                      reinterpret_cast<BYTE*>(&serviceStatus),
                                      sizeof(serviceStatus), &bytes) &&
                 serviceStatus.dwCurrentState == SERVICE_RUNNING;
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
  // A service may report SERVICE_RUNNING just before its worker creates the named pipe.
  // WaitNamedPipe returns immediately with ERROR_FILE_NOT_FOUND during that small window, so
  // a single call permanently pushed the host onto the non-secure fallback path. Retry both
  // discovery and open until the startup budget expires.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  do {
    (void)WaitNamedPipeW(kSecureInputPipeName, 100);
    pipe_ = CreateFileW(kSecureInputPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe_ != INVALID_HANDLE_VALUE) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  } while (std::chrono::steady_clock::now() < deadline);
  return false;
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
  apply_target_rect_locked(&message);
  return WriteLocked(message);
}

void SecureInputBrokerClient::SetTargetRect(int32_t originX, int32_t originY, uint32_t width,
                                            uint32_t height) {
  std::lock_guard<std::mutex> lock(mu_);
  targetOriginX_ = originX;
  targetOriginY_ = originY;
  targetWidth_ = width;
  targetHeight_ = height;
}

void SecureInputBrokerClient::apply_target_rect_locked(SecureInputMessage* message) const {
  // Left zero when the host has not told us where the capture sits, which makes the agent fall
  // back to the virtual screen rather than guess.
  message->targetOriginX = targetOriginX_;
  message->targetOriginY = targetOriginY_;
  message->targetWidth = targetWidth_;
  message->targetHeight = targetHeight_;
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
  apply_target_rect_locked(&message);
  return WriteLocked(message);
}

bool SecureInputBrokerClient::connected() const {
  std::lock_guard<std::mutex> lock(mu_);
  return pipe_ != INVALID_HANDLE_VALUE;
}

}  // namespace remote60::native_poc

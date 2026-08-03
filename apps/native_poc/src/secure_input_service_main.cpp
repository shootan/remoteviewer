#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "secure_input_protocol.hpp"

namespace {

using remote60::native_poc::SecureInputKind;
using remote60::native_poc::SecureInputMessage;
using remote60::native_poc::kSecureInputMagic;
using remote60::native_poc::kSecureInputPipeName;
using remote60::native_poc::kSecureInputServiceName;

SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
SERVICE_STATUS gStatus{};
std::atomic<bool> gRunning{true};
std::atomic<HANDLE> gClientPipe{INVALID_HANDLE_VALUE};

struct AgentProcess {
  HANDLE process = nullptr;
  HANDLE writePipe = nullptr;
  DWORD sessionId = 0xffffffffu;
};

AgentProcess gAgent;

bool write_exact(HANDLE handle, const void* data, DWORD bytes) {
  const auto* cursor = static_cast<const uint8_t*>(data);
  DWORD total = 0;
  while (total < bytes) {
    DWORD written = 0;
    if (!WriteFile(handle, cursor + total, bytes - total, &written, nullptr) || written == 0) {
      return false;
    }
    total += written;
  }
  return true;
}

bool read_exact(HANDLE handle, void* data, DWORD bytes) {
  auto* cursor = static_cast<uint8_t*>(data);
  DWORD total = 0;
  while (total < bytes) {
    DWORD read = 0;
    if (!ReadFile(handle, cursor + total, bytes - total, &read, nullptr) || read == 0) {
      return false;
    }
    total += read;
  }
  return true;
}

void stop_agent() {
  if (gAgent.writePipe) {
    SecureInputMessage shutdown{};
    shutdown.kind = static_cast<uint16_t>(SecureInputKind::Shutdown);
    (void)write_exact(gAgent.writePipe, &shutdown, sizeof(shutdown));
    CloseHandle(gAgent.writePipe);
  }
  gAgent.writePipe = nullptr;
  if (gAgent.process) {
    if (WaitForSingleObject(gAgent.process, 1500) == WAIT_TIMEOUT) {
      // This is a child created and owned by this service. It cannot be allowed to remain as
      // an orphaned SYSTEM input agent after the broker stops or the console session changes.
      (void)TerminateProcess(gAgent.process, 0);
      (void)WaitForSingleObject(gAgent.process, 500);
    }
    CloseHandle(gAgent.process);
  }
  gAgent.process = nullptr;
  gAgent.sessionId = 0xffffffffu;
}

std::wstring current_executable_path() {
  std::wstring path(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) return {};
  path.resize(length);
  return path;
}

bool start_agent(DWORD sessionId) {
  stop_agent();
  HANDLE readPipe = nullptr;
  HANDLE writePipe = nullptr;
  SECURITY_ATTRIBUTES inherit{};
  inherit.nLength = sizeof(inherit);
  inherit.bInheritHandle = TRUE;
  if (!CreatePipe(&readPipe, &writePipe, &inherit, 0)) return false;
  (void)SetHandleInformation(writePipe, HANDLE_FLAG_INHERIT, 0);

  HANDLE serviceToken = nullptr;
  HANDLE agentToken = nullptr;
  bool ok = OpenProcessToken(GetCurrentProcess(),
                             TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY |
                                 TOKEN_ADJUST_SESSIONID,
                             &serviceToken) &&
            DuplicateTokenEx(serviceToken, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation,
                             TokenPrimary, &agentToken) &&
            SetTokenInformation(agentToken, TokenSessionId, &sessionId, sizeof(sessionId));
  if (serviceToken) CloseHandle(serviceToken);
  if (!ok) {
    if (agentToken) CloseHandle(agentToken);
    CloseHandle(readPipe);
    CloseHandle(writePipe);
    return false;
  }

  const std::wstring exe = current_executable_path();
  wchar_t command[32768]{};
  _snwprintf_s(command, _TRUNCATE, L"\"%s\" --agent %llu", exe.c_str(),
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(readPipe)));
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.lpDesktop = const_cast<wchar_t*>(L"winsta0\\default");
  PROCESS_INFORMATION process{};
  ok = CreateProcessAsUserW(agentToken, nullptr, command, nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                            &startup, &process) != FALSE;
  CloseHandle(agentToken);
  CloseHandle(readPipe);
  if (!ok) {
    CloseHandle(writePipe);
    return false;
  }
  CloseHandle(process.hThread);
  gAgent.process = process.hProcess;
  gAgent.writePipe = writePipe;
  gAgent.sessionId = sessionId;
  return true;
}

bool ensure_agent() {
  const DWORD sessionId = WTSGetActiveConsoleSessionId();
  if (sessionId == 0xffffffffu) return false;
  if (gAgent.process && gAgent.writePipe && gAgent.sessionId == sessionId &&
      WaitForSingleObject(gAgent.process, 0) == WAIT_TIMEOUT) {
    return true;
  }
  return start_agent(sessionId);
}

bool forward_to_agent(const SecureInputMessage& message) {
  if (!ensure_agent()) return false;
  if (write_exact(gAgent.writePipe, &message, sizeof(message))) return true;
  if (!start_agent(WTSGetActiveConsoleSessionId())) return false;
  return write_exact(gAgent.writePipe, &message, sizeof(message));
}

void report_service_status(DWORD state, DWORD error = NO_ERROR) {
  gStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  gStatus.dwCurrentState = state;
  gStatus.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP : 0;
  gStatus.dwWin32ExitCode = error;
  gStatus.dwCheckPoint = 0;
  gStatus.dwWaitHint = 0;
  if (gStatusHandle) SetServiceStatus(gStatusHandle, &gStatus);
}

void WINAPI service_control(DWORD control) {
  if (control != SERVICE_CONTROL_STOP) return;
  report_service_status(SERVICE_STOP_PENDING);
  gRunning.store(false, std::memory_order_release);
  HANDLE pipe = gClientPipe.exchange(INVALID_HANDLE_VALUE, std::memory_order_acq_rel);
  if (pipe != INVALID_HANDLE_VALUE) {
    (void)CancelIoEx(pipe, nullptr);
    (void)DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
  }
}

HANDLE create_secure_pipe() {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"D:P(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1, &descriptor, nullptr)) {
    return INVALID_HANDLE_VALUE;
  }
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.lpSecurityDescriptor = descriptor;
  HANDLE pipe = CreateNamedPipeW(
      kSecureInputPipeName, PIPE_ACCESS_INBOUND, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
                                                    PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
      1, 0, 64 * 1024, 0, &security);
  LocalFree(descriptor);
  return pipe;
}

void WINAPI service_main(DWORD, wchar_t**) {
  gStatusHandle = RegisterServiceCtrlHandlerW(kSecureInputServiceName, service_control);
  if (!gStatusHandle) return;
  report_service_status(SERVICE_START_PENDING);
  gRunning.store(true, std::memory_order_release);
  report_service_status(SERVICE_RUNNING);

  while (gRunning.load(std::memory_order_acquire)) {
    HANDLE pipe = create_secure_pipe();
    if (pipe == INVALID_HANDLE_VALUE) break;
    gClientPipe.store(pipe, std::memory_order_release);
    const BOOL connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
    if (connected) {
      while (gRunning.load(std::memory_order_acquire)) {
        SecureInputMessage message{};
        if (!read_exact(pipe, &message, sizeof(message))) break;
        if (message.magic != kSecureInputMagic || message.size != sizeof(message)) break;
        (void)forward_to_agent(message);
      }
      (void)DisconnectNamedPipe(pipe);
    }
    HANDLE expected = pipe;
    if (gClientPipe.compare_exchange_strong(expected, INVALID_HANDLE_VALUE,
                                            std::memory_order_acq_rel)) {
      CloseHandle(pipe);
    }
  }
  stop_agent();
  report_service_status(SERVICE_STOPPED);
}

bool attach_to_input_desktop(HDESK* ownedDesktop) {
  HDESK next = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS |
                                             DESKTOP_SWITCHDESKTOP);
  if (!next) return false;
  if (!SetThreadDesktop(next)) {
    CloseDesktop(next);
    return false;
  }
  if (*ownedDesktop) CloseDesktop(*ownedDesktop);
  *ownedDesktop = next;
  return true;
}

POINT map_point(const SecureInputMessage& message) {
  const int width = std::max<int>(1, GetSystemMetrics(SM_CXSCREEN));
  const int height = std::max<int>(1, GetSystemMetrics(SM_CYSCREEN));
  const uint32_t inputW = std::max<uint32_t>(1, message.inputWidth);
  const uint32_t inputH = std::max<uint32_t>(1, message.inputHeight);
  POINT point{};
  point.x = static_cast<LONG>((static_cast<int64_t>(std::clamp<int32_t>(message.x, 0,
                                                                        inputW - 1)) *
                               (width - 1)) /
                              std::max<uint32_t>(1, inputW - 1));
  point.y = static_cast<LONG>((static_cast<int64_t>(std::clamp<int32_t>(message.y, 0,
                                                                        inputH - 1)) *
                               (height - 1)) /
                              std::max<uint32_t>(1, inputH - 1));
  return point;
}

DWORD mouse_flag(uint16_t kind, uint32_t key) {
  if (kind == 2) {
    if (key == VK_RBUTTON) return MOUSEEVENTF_RIGHTDOWN;
    if (key == VK_MBUTTON) return MOUSEEVENTF_MIDDLEDOWN;
    return MOUSEEVENTF_LEFTDOWN;
  }
  if (key == VK_RBUTTON) return MOUSEEVENTF_RIGHTUP;
  if (key == VK_MBUTTON) return MOUSEEVENTF_MIDDLEUP;
  return MOUSEEVENTF_LEFTUP;
}

bool inject_message(const SecureInputMessage& message) {
  if (message.kind == static_cast<uint16_t>(SecureInputKind::InputText)) {
    const uint16_t count = std::min<uint16_t>(message.textCount,
                                              remote60::native_poc::kSecureInputTextMax);
    for (uint16_t i = 0; i < count; ++i) {
      INPUT input[2]{};
      input[0].type = INPUT_KEYBOARD;
      input[0].ki.wScan = message.text[i];
      input[0].ki.dwFlags = KEYEVENTF_UNICODE;
      input[1] = input[0];
      input[1].ki.dwFlags |= KEYEVENTF_KEYUP;
      if (SendInput(2, input, sizeof(INPUT)) != 2) return false;
    }
    return true;
  }
  if (message.kind != static_cast<uint16_t>(SecureInputKind::InputEvent)) return false;
  const POINT point = map_point(message);
  // Position with SetCursorPos and send the button separately. Carrying absolute coordinates on
  // the button event is theoretically tidier -- it removes the window in which something else
  // could move the cursor between the two calls -- but the 0..65535 virtual-desktop mapping
  // relies on metrics this SYSTEM agent does not reliably see, and getting them wrong throws
  // every click off-screen, which reads as input being completely dead. Keep the proven path.
  if (message.eventKind >= 1 && message.eventKind <= 4 && !SetCursorPos(point.x, point.y)) {
    return false;
  }
  if (message.eventKind == 1) return true;
  INPUT input{};
  if (message.eventKind == 2 || message.eventKind == 3) {
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = mouse_flag(message.eventKind, message.keyCode);
  } else if (message.eventKind == 4) {
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(static_cast<SHORT>(message.wheelDelta));
  } else if (message.eventKind == 5 || message.eventKind == 6) {
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(message.keyCode);
    input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(message.keyCode, MAPVK_VK_TO_VSC));
    if (message.eventKind == 6) input.ki.dwFlags |= KEYEVENTF_KEYUP;
    switch (message.keyCode) {
      case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
      case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
      case VK_INSERT: case VK_DELETE: case VK_RCONTROL: case VK_RMENU:
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        break;
      default:
        break;
    }
  } else {
    return false;
  }
  return SendInput(1, &input, sizeof(INPUT)) == 1;
}

int run_agent(HANDLE readPipe) {
  HDESK ownedDesktop = nullptr;
  for (;;) {
    SecureInputMessage message{};
    if (!read_exact(readPipe, &message, sizeof(message))) break;
    if (message.magic != kSecureInputMagic || message.size != sizeof(message)) break;
    if (message.kind == static_cast<uint16_t>(SecureInputKind::Shutdown)) break;
    if (!attach_to_input_desktop(&ownedDesktop)) continue;
    (void)inject_message(message);
  }
  if (ownedDesktop) CloseDesktop(ownedDesktop);
  CloseHandle(readPipe);
  return 0;
}

}  // namespace

// Registration lives here, invoked by the elevated installer, so the streaming host never
// needs the rights to create or repoint a LocalSystem service. The image path is whatever this
// binary already is: an installer that put it under Program Files therefore registers an
// admin-only path, and there is no code left that can walk it back out to a writable folder.
int install_service() {
  const std::wstring exe = current_executable_path();
  if (exe.empty()) return 1;
  const std::wstring quoted = L"\"" + exe + L"\"";
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
  if (!manager) return static_cast<int>(GetLastError());
  SC_HANDLE service = OpenServiceW(manager, kSecureInputServiceName, SERVICE_CHANGE_CONFIG);
  if (service) {
    (void)ChangeServiceConfigW(service, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                               SERVICE_ERROR_NORMAL, quoted.c_str(), nullptr, nullptr, nullptr,
                               nullptr, nullptr, nullptr);
  } else {
    service = CreateServiceW(manager, kSecureInputServiceName, L"GNLink secure desktop input",
                             SERVICE_CHANGE_CONFIG, SERVICE_WIN32_OWN_PROCESS,
                             SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, quoted.c_str(),
                             nullptr, nullptr, nullptr, nullptr, nullptr);
  }
  if (!service) {
    const DWORD err = GetLastError();
    CloseServiceHandle(manager);
    return static_cast<int>(err);
  }
  SERVICE_DESCRIPTIONW description{};
  description.lpDescription =
      const_cast<wchar_t*>(L"Delivers GNLink remote input to elevated and secure desktops.");
  (void)ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description);
  CloseServiceHandle(service);
  CloseServiceHandle(manager);
  return 0;
}

int uninstall_service() {
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!manager) return static_cast<int>(GetLastError());
  SC_HANDLE service = OpenServiceW(manager, kSecureInputServiceName,
                                   SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
  if (!service) {
    const DWORD err = GetLastError();
    CloseServiceHandle(manager);
    // Already absent is the desired end state, not a failure.
    return err == ERROR_SERVICE_DOES_NOT_EXIST ? 0 : static_cast<int>(err);
  }
  SERVICE_STATUS status{};
  if (ControlService(service, SERVICE_CONTROL_STOP, &status)) {
    // The SCM refuses DeleteService while the service is still running.
    for (int i = 0; i < 50; ++i) {
      SERVICE_STATUS_PROCESS live{};
      DWORD bytes = 0;
      if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                reinterpret_cast<BYTE*>(&live), sizeof(live), &bytes)) {
        break;
      }
      if (live.dwCurrentState == SERVICE_STOPPED) break;
      Sleep(100);
    }
  }
  const BOOL deleted = DeleteService(service);
  const DWORD err = deleted ? ERROR_SUCCESS : GetLastError();
  CloseServiceHandle(service);
  CloseServiceHandle(manager);
  // Marked-for-delete means it disappears once the last handle closes, which just happened.
  if (!deleted && err != ERROR_SERVICE_MARKED_FOR_DELETE) return static_cast<int>(err);
  return 0;
}

int wmain(int argc, wchar_t** argv) {
  if (argc == 3 && std::wstring(argv[1]) == L"--agent") {
    const uintptr_t value = static_cast<uintptr_t>(_wcstoui64(argv[2], nullptr, 10));
    return run_agent(reinterpret_cast<HANDLE>(value));
  }
  if (argc == 2 && std::wstring(argv[1]) == L"--install-service") return install_service();
  if (argc == 2 && std::wstring(argv[1]) == L"--uninstall-service") return uninstall_service();
  SERVICE_TABLE_ENTRYW table[] = {
      {const_cast<wchar_t*>(kSecureInputServiceName), service_main},
      {nullptr, nullptr},
  };
  return StartServiceCtrlDispatcherW(table) ? 0 : static_cast<int>(GetLastError());
}

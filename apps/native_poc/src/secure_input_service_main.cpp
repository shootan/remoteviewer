#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "secure_input_mapping.hpp"
#include "secure_input_protocol.hpp"
#include "secure_input_session.hpp"

namespace {

using remote60::native_poc::DesktopRect;
using remote60::native_poc::map_client_point;
using remote60::native_poc::SecureInputKind;
using remote60::native_poc::SecureInputMessage;
using remote60::native_poc::kInvalidSessionId;
using remote60::native_poc::kSecureInputMagic;
using remote60::native_poc::kSecureInputPipeName;
using remote60::native_poc::kSecureInputServiceName;
using remote60::native_poc::resolve_target_session;
using remote60::native_poc::session_source_name;

// A service has no console, so everything printed here went nowhere -- which is how the input
// path came to be undiagnosable: the host counts every event as delivered because the injection
// result is discarded, and the only component that knows better could not say so.
//
// %ProgramData% rather than the install directory: the service runs as LocalSystem and the
// install directory is deliberately admin-only, so writing beside the binary would either fail
// or hand a SYSTEM-writable file to a place that is supposed to be read-only at runtime.
std::wstring diag_log_path() {
  wchar_t base[MAX_PATH]{};
  if (GetEnvironmentVariableW(L"ProgramData", base, MAX_PATH) == 0) return {};
  std::wstring dir = std::wstring(base) + L"\\GNLink";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\secure_input.log";
}

void diag(const char* format, ...) {
  static const std::wstring path = diag_log_path();
  if (path.empty()) return;
  char line[1024]{};
  va_list args;
  va_start(args, format);
  _vsnprintf_s(line, _TRUNCATE, format, args);
  va_end(args);

  SYSTEMTIME now{};
  GetLocalTime(&now);
  FILE* file = nullptr;
  if (_wfopen_s(&file, path.c_str(), L"a") != 0 || !file) return;
  // Truncate rather than grow without bound; this is a diagnostic, not an audit trail.
  if (_ftelli64(file) > 2 * 1024 * 1024) {
    fclose(file);
    if (_wfopen_s(&file, path.c_str(), L"w") != 0 || !file) return;
  }
  fprintf(file, "%02d:%02d:%02d.%03d %s\n", now.wHour, now.wMinute, now.wSecond,
          now.wMilliseconds, line);
  fclose(file);
}

SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
SERVICE_STATUS gStatus{};
std::atomic<bool> gRunning{true};
std::atomic<HANDLE> gClientPipe{INVALID_HANDLE_VALUE};
// The session of the process holding the control pipe -- the streaming host. Captured once when
// the pipe connects, because that is the only moment the requester is identifiable.
std::atomic<uint32_t> gRequesterSession{kInvalidSessionId};

struct AgentProcess {
  HANDLE process = nullptr;
  HANDLE writePipe = nullptr;
  DWORD sessionId = 0xffffffffu;
  // The desktop the agent was created on. A thread can move between desktops but the process's
  // association cannot, and SendInput refuses when they disagree -- so a change here means the
  // agent has to be replaced, not redirected.
  std::wstring desktop;
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
  gAgent.desktop.clear();
}

std::wstring current_executable_path() {
  std::wstring path(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) return {};
  path.resize(length);
  return path;
}

bool start_agent(DWORD sessionId, const std::wstring& desktopName) {
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
  // Create the agent on the desktop it will inject into, rather than starting it on Default and
  // moving a thread there afterwards.
  //
  // Measured: with the agent created on Default and SetThreadDesktop'd onto Winlogon, attach
  // succeeded and SetCursorPos worked -- the cursor moved -- but every button event came back
  // from SendInput with ERROR_ACCESS_DENIED. Mouse moves appeared to work only because they
  // return after SetCursorPos without calling SendInput at all, which is what made the failures
  // look like they alternated.
  //
  // A thread can change desktops; the process's association is fixed when it is created, and
  // that is what the input path checks. So the desktop is chosen up front and the agent is
  // recreated when the input desktop changes.
  std::wstring desktopSpec = L"winsta0\\" + desktopName;
  startup.lpDesktop = desktopSpec.data();
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
  gAgent.desktop = desktopName;
  return true;
}

// Resolves the session the agent belongs in and says why, once per change. Silence here is what
// let the wrong-session bug live: the write succeeded, so everything downstream looked healthy.
DWORD target_session() {
  const uint32_t requester = gRequesterSession.load(std::memory_order_acquire);
  const auto choice = resolve_target_session(requester, WTSGetActiveConsoleSessionId());
  static uint32_t reported = kInvalidSessionId;
  if (choice.sessionId != reported) {
    reported = choice.sessionId;
    diag("target session=%u source=%s (requester=%u console=%u)", choice.sessionId,
         session_source_name(choice.source), requester, WTSGetActiveConsoleSessionId());
  }
  return choice.sessionId;
}

// The desktop currently receiving input -- "Default" ordinarily, "Winlogon" while a consent
// prompt or the lock screen is up. The agent has to be created on this one, so it is read here
// rather than left for the agent to discover after the fact.
//
// A failure to open it is itself the answer: only the secure desktop refuses, so that is what it
// must be. Guessing "Default" there would put the agent exactly where it cannot inject.
std::wstring current_input_desktop_name() {
  HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
  if (!desktop) return L"Winlogon";
  wchar_t name[64]{};
  DWORD needed = 0;
  const bool ok = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed) != 0;
  CloseDesktop(desktop);
  return ok ? name : L"Default";
}

bool ensure_agent() {
  const DWORD sessionId = target_session();
  if (sessionId == kInvalidSessionId) return false;
  const std::wstring desktop = current_input_desktop_name();
  if (gAgent.process && gAgent.writePipe && gAgent.sessionId == sessionId &&
      gAgent.desktop == desktop && WaitForSingleObject(gAgent.process, 0) == WAIT_TIMEOUT) {
    return true;
  }
  if (gAgent.process && gAgent.desktop != desktop) {
    char narrowFrom[64]{};
    char narrowTo[64]{};
    size_t converted = 0;
    wcstombs_s(&converted, narrowFrom, gAgent.desktop.c_str(), _TRUNCATE);
    wcstombs_s(&converted, narrowTo, desktop.c_str(), _TRUNCATE);
    diag("input desktop changed %s -> %s, recreating the agent there", narrowFrom, narrowTo);
  }
  return start_agent(sessionId, desktop);
}

bool forward_to_agent(const SecureInputMessage& message) {
  if (!ensure_agent()) return false;
  if (write_exact(gAgent.writePipe, &message, sizeof(message))) return true;
  // A failed write means the agent died; re-resolve rather than reusing the old session, since
  // the reason it died may be that its session went away.
  const DWORD retrySession = target_session();
  if (retrySession == kInvalidSessionId ||
      !start_agent(retrySession, current_input_desktop_name())) {
    return false;
  }
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

DWORD WINAPI service_control(DWORD control, DWORD eventType, LPVOID eventData, LPVOID) {
  // Session changes matter because the agent lives inside one. A session that logs off or
  // disconnects takes the agent's desktop with it, and continuing to write to that agent is how
  // input silently goes nowhere.
  if (control == SERVICE_CONTROL_SESSIONCHANGE) {
    const auto* notification = static_cast<const WTSSESSION_NOTIFICATION*>(eventData);
    if (notification && (eventType == WTS_SESSION_LOGOFF || eventType == WTS_SESSION_LOGON ||
                         eventType == WTS_CONSOLE_DISCONNECT || eventType == WTS_REMOTE_DISCONNECT)) {
      diag("session change event=%lu session=%lu", eventType, notification->dwSessionId);
      if (gAgent.process && gAgent.sessionId == notification->dwSessionId) {
        stop_agent();  // the next message re-resolves and re-creates it where it belongs
      }
    }
    return NO_ERROR;
  }
  if (control != SERVICE_CONTROL_STOP) return NO_ERROR;
  report_service_status(SERVICE_STOP_PENDING);
  gRunning.store(false, std::memory_order_release);
  HANDLE pipe = gClientPipe.exchange(INVALID_HANDLE_VALUE, std::memory_order_acq_rel);
  if (pipe != INVALID_HANDLE_VALUE) {
    (void)CancelIoEx(pipe, nullptr);
    (void)DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
  }
  return NO_ERROR;
}

// The process on the other end of the pipe is the streaming host, so its session is the one whose
// desktop the operator is actually looking at.
uint32_t requester_session_of(HANDLE pipe) {
  ULONG pid = 0;
  if (!GetNamedPipeClientProcessId(pipe, &pid) || pid == 0) return kInvalidSessionId;
  DWORD session = 0;
  if (!ProcessIdToSessionId(pid, &session)) return kInvalidSessionId;
  return session;
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
  // Ex rather than the plain handler so SERVICE_CONTROL_SESSIONCHANGE can be delivered; the
  // plain form cannot receive it, and without it the agent outlives the session it was made for.
  gStatusHandle =
      RegisterServiceCtrlHandlerExW(kSecureInputServiceName, service_control, nullptr);
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
      // Identify the requester before reading anything from it. This is the only point at which
      // the host's session can be learned, and everything downstream depends on it.
      const uint32_t requester = requester_session_of(pipe);
      gRequesterSession.store(requester, std::memory_order_release);
      diag("control pipe connected, requester session=%u", requester);
      // A new requester may live in a different session than the agent already running.
      if (gAgent.process && gAgent.sessionId != target_session()) stop_agent();
      while (gRunning.load(std::memory_order_acquire)) {
        SecureInputMessage message{};
        if (!read_exact(pipe, &message, sizeof(message))) break;
        if (message.magic != kSecureInputMagic || message.size != sizeof(message)) break;
        (void)forward_to_agent(message);
      }
      (void)DisconnectNamedPipe(pipe);
      // The requester is gone; do not keep its session as the answer for whoever connects next.
      gRequesterSession.store(kInvalidSessionId, std::memory_order_release);
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

// Access the agent needs on the input desktop just to see it and follow switches.
constexpr DWORD kDesktopBaseAccess =
    DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_SWITCHDESKTOP;

// SendInput refuses unless BOTH hold: the calling thread is on the current input desktop, and the
// thread's desktop was opened with DESKTOP_JOURNALPLAYBACK. Either failure returns the same
// ERROR_ACCESS_DENIED, which is why the two were indistinguishable for so long.
//
// Measured on 0.2.11: the agent ran as SYSTEM, was created on Winlogon, attached successfully, and
// SetCursorPos moved the cursor on the live consent prompt -- so the desktop was already correct --
// yet every button event came back err=5. The mask above simply never asked for journal playback.
// SetCursorPos does not need it; SendInput does. That single missing bit is the whole reason UAC
// was unclickable, and it is why the driver/registry routes looked like the only options left.
//
// The retry without journal playback is deliberate. A desktop whose DACL withholds that right
// should still get cursor movement rather than lose input entirely, and the caller records which
// of the two it got so a future err=5 cannot be misread as this bug returning.
bool attach_to_input_desktop(HDESK* ownedDesktop, bool* journalPlayback) {
  bool playback = true;
  HDESK next = OpenInputDesktop(0, FALSE, kDesktopBaseAccess | DESKTOP_JOURNALPLAYBACK);
  if (!next) {
    playback = false;
    next = OpenInputDesktop(0, FALSE, kDesktopBaseAccess);
  }
  if (!next) return false;
  if (!SetThreadDesktop(next)) {
    CloseDesktop(next);
    return false;
  }
  if (*ownedDesktop) CloseDesktop(*ownedDesktop);
  *ownedDesktop = next;
  if (journalPlayback) *journalPlayback = playback;
  return true;
}

POINT map_point(const SecureInputMessage& message) {
  // Where the client's pixels live on the desktop. The host sends it when it knows; otherwise
  // fall back to the virtual screen, which is what full-desktop capture covers and what
  // SetCursorPos addresses. The old fallback was the primary monitor alone, which made a prompt
  // on any other display unreachable.
  DesktopRect target{message.targetOriginX, message.targetOriginY,
                     static_cast<int32_t>(message.targetWidth),
                     static_cast<int32_t>(message.targetHeight)};
  if (!target.valid()) {
    target.originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    target.originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    target.width = std::max<int32_t>(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    target.height = std::max<int32_t>(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
  }
  const auto mapped =
      map_client_point(message.x, message.y, message.inputWidth, message.inputHeight, target);
  POINT point{};
  point.x = mapped.x;
  point.y = mapped.y;
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

const char* gDpiAwarenessApplied = "none";

// Every other component that touches screen coordinates declares DPI awareness -- the capture
// worker, the host window, the viewer. The input agent was the one that did not, and it is the
// one that converts a client coordinate into a desktop point.
//
// It matters because the host captures and reports PHYSICAL pixels. A DPI-unaware process is
// handed a virtualised coordinate space instead, so SetCursorPos(x, y) is scaled up by the
// display's factor before it reaches the cursor -- putting every click down and to the right of
// the intended spot, by more the further from the origin it is. At 100% scaling the two spaces
// coincide and this call changes nothing, which is why it can be added without risking the
// machines that already work.
//
// Called before any coordinate is touched; awareness cannot be changed once the process has
// begun interacting with the desktop.
void apply_dpi_awareness() {
  if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
    gDpiAwarenessApplied = "per-monitor-v2";
    return;
  }
  if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE)) {
    gDpiAwarenessApplied = "system";
    return;
  }
  // Pre-1703 fallback. Equivalent to system awareness and enough to stop the virtualisation.
  gDpiAwarenessApplied = SetProcessDPIAware() ? "legacy-system" : "none";
}

// Records the first few failures in full. "inject failed" is not actionable: SetCursorPos and
// SendInput fail for entirely different reasons, and the event kind decides which of them even
// runs. Bounded because a broken session would otherwise write a line per click forever.
void diag_inject_failure(const char* where, const SecureInputMessage& message, DWORD error,
                         long mapped_x, long mapped_y) {
  static std::atomic<int> remaining{40};
  if (remaining.fetch_sub(1, std::memory_order_relaxed) <= 0) return;
  diag("inject FAILED at %s: kind=%u eventKind=%u buttons=%u key=%u in=(%d,%d)/%ux%u "
       "target=(%d,%d)/%ux%u mapped=(%ld,%ld) err=%lu",
       where, message.kind, message.eventKind, message.buttons, message.keyCode, message.x,
       message.y, message.inputWidth, message.inputHeight, message.targetOriginX,
       message.targetOriginY, message.targetWidth, message.targetHeight, mapped_x, mapped_y,
       error);
}

// Records where a click actually landed, for the first few button events only.
//
// Until injection started working there was nothing to record: the only coordinate log ran on
// failure. Now that clicks land, "they land in the wrong place" has three candidate causes that
// the mapped point alone cannot separate -- a wrong target rect from the host, a wrong input
// domain from the client, or DPI virtualisation rewriting the coordinate after we hand it over.
//
// Reading the cursor straight back is what separates them. If GetCursorPos returns the point we
// asked for, the coordinate arithmetic is sound and the inputs to it are what is wrong; if it
// returns something else, the offset is being applied below us. `virt` is logged alongside
// because the agent's own view of the virtual screen is what the fallback path would have used.
void diag_inject_landing(const SecureInputMessage& message, long mapped_x, long mapped_y) {
  static std::atomic<int> remaining{12};
  if (remaining.fetch_sub(1, std::memory_order_relaxed) <= 0) return;
  POINT actual{};
  const bool got = GetCursorPos(&actual) != FALSE;
  diag("inject landed: eventKind=%u in=(%d,%d)/%ux%u target=(%d,%d)/%ux%u mapped=(%ld,%ld) "
       "cursor=%s(%ld,%ld) virt=(%d,%d)/%dx%d dpiAwareness=%s",
       message.eventKind, message.x, message.y, message.inputWidth, message.inputHeight,
       message.targetOriginX, message.targetOriginY, message.targetWidth, message.targetHeight,
       mapped_x, mapped_y, got ? "" : "FAILED", got ? actual.x : 0, got ? actual.y : 0,
       GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
       GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN),
       gDpiAwarenessApplied);
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
      if (SendInput(2, input, sizeof(INPUT)) != 2) {
        diag_inject_failure("SendInput-text", message, GetLastError(), 0, 0);
        return false;
      }
    }
    return true;
  }
  if (message.kind != static_cast<uint16_t>(SecureInputKind::InputEvent)) {
    diag_inject_failure("kind-not-input-event", message, 0, 0, 0);
    return false;
  }
  const POINT point = map_point(message);
  // Position with SetCursorPos and send the button separately. Carrying absolute coordinates on
  // the button event is theoretically tidier -- it removes the window in which something else
  // could move the cursor between the two calls -- but the 0..65535 virtual-desktop mapping
  // relies on metrics this SYSTEM agent does not reliably see, and getting them wrong throws
  // every click off-screen, which reads as input being completely dead. Keep the proven path.
  if (message.eventKind >= 1 && message.eventKind <= 4 && !SetCursorPos(point.x, point.y)) {
    diag_inject_failure("SetCursorPos", message, GetLastError(), point.x, point.y);
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
    diag_inject_failure("unhandled-event-kind", message, 0, point.x, point.y);
    return false;
  }
  if (SendInput(1, &input, sizeof(INPUT)) == 1) {
    // Button events only. Moves return above, and there are enough of them to exhaust the budget
    // before a single click is recorded -- and it is the click whose position is in question.
    diag_inject_landing(message, point.x, point.y);
    return true;
  }
  diag_inject_failure("SendInput", message, GetLastError(), point.x, point.y);
  return false;
}

// Reports what the agent is actually doing, once per change rather than per event.
//
// This is the only place that knows whether an injection landed, and it used to discard the
// answer -- so the host counted every event as delivered and a click that went nowhere was
// indistinguishable from one that worked. Recording the desktop name matters most: "Winlogon"
// proves the agent followed the switch, "Default" while a consent prompt is up proves it did not.
// `journal` is reported because it is the difference between "SendInput can work here" and
// "SendInput will return err=5 no matter what else is right". Without it in the log, a future
// failure on a desktop that withholds the right would look identical to the 0.2.11 bug.
void report_agent_state(const wchar_t* desktopName, bool attached, bool journal, bool injected) {
  static std::wstring lastDesktop;
  static bool lastAttached = true;
  static bool lastJournal = true;
  static bool lastInjected = true;
  const std::wstring current = desktopName ? desktopName : L"?";
  if (current == lastDesktop && attached == lastAttached && journal == lastJournal &&
      injected == lastInjected) {
    return;
  }
  lastDesktop = current;
  lastAttached = attached;
  lastJournal = journal;
  lastInjected = injected;
  char narrow[128]{};
  size_t converted = 0;
  wcstombs_s(&converted, narrow, current.c_str(), _TRUNCATE);
  diag("agent desktop=%s attach=%s journal=%s inject=%s", narrow, attached ? "ok" : "FAILED",
       journal ? "ok" : "DENIED", injected ? "ok" : "FAILED");
}

std::wstring current_desktop_name() {
  HDESK desktop = GetThreadDesktop(GetCurrentThreadId());
  if (!desktop) return L"?";
  wchar_t name[128]{};
  DWORD needed = 0;
  if (!GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed)) return L"?";
  return name;
}

int run_agent(HANDLE readPipe) {
  apply_dpi_awareness();
  HDESK ownedDesktop = nullptr;
  // The desktop this process was CREATED on, before any attach. Distinct from the one it later
  // attaches to, and the only way to confirm from outside which of the two the input path is
  // actually judging.
  {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    char created[128] = "?";
    HDESK own = GetThreadDesktop(GetCurrentThreadId());
    if (own) {
      wchar_t wide[64]{};
      DWORD needed = 0;
      if (GetUserObjectInformationW(own, UOI_NAME, wide, sizeof(wide), &needed)) {
        size_t converted = 0;
        wcstombs_s(&converted, created, wide, _TRUNCATE);
      }
    }
    diag("agent started in session %lu, created on desktop=%s dpiAwareness=%s", session, created,
         gDpiAwarenessApplied);
  }
  for (;;) {
    SecureInputMessage message{};
    if (!read_exact(readPipe, &message, sizeof(message))) break;
    if (message.magic != kSecureInputMagic || message.size != sizeof(message)) break;
    if (message.kind == static_cast<uint16_t>(SecureInputKind::Shutdown)) break;
    // Re-attach every message on purpose: the desktop can change between any two events, and a
    // handle kept from the previous one points at the desktop that is no longer receiving input.
    bool journalPlayback = false;
    if (!attach_to_input_desktop(&ownedDesktop, &journalPlayback)) {
      report_agent_state(L"(attach failed)", false, false, false);
      continue;
    }
    const bool injected = inject_message(message);
    report_agent_state(current_desktop_name().c_str(), true, journalPlayback, injected);
  }
  diag("agent exiting");
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

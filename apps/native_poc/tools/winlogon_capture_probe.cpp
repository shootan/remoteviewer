// Answers one question and then gets deleted: can a SYSTEM process attached to the Winlogon
// desktop actually read those pixels, and with which API?
//
// The secure-desktop capture work (구현계획 U2b) is days of work resting on that assumption.
// docs/잠금화면_사전로그인_설계.md asserts DXGI Desktop Duplication is the only option and that
// GDI cannot do it, but nothing in this repository has ever tested it. Building the full agent
// and finding out at the end is the expensive way to learn the answer.
//
// This is not product code. It is not installed, not shipped, and the service it registers is
// removed before it exits.
//
// Three processes, because reaching the Winlogon desktop needs SYSTEM in the *streamed* session
// and there is no way to get there directly from an elevated user process:
//
//   controller  (elevated user)  registers a temporary service, starts it, waits, removes it
//   service     (SYSTEM, sess 0) retargets its own token at the requested session, spawns:
//   agent       (SYSTEM, sess N) polls the input desktop, captures when it turns non-Default
//
// The agent captures the ordinary desktop first as a control. Without it, two blank bitmaps
// would not distinguish "the secure desktop is unreadable" from "this probe is broken".

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kServiceName[] = L"GNLinkWinlogonProbe";
constexpr DWORD kAgentPollMs = 250;

// ---------------------------------------------------------------------------- shared helpers

std::wstring exe_path() {
  wchar_t buffer[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  return buffer;
}

std::wstring exe_dir() {
  std::wstring path = exe_path();
  const size_t slash = path.find_last_of(L'\\');
  return slash == std::wstring::npos ? path : path.substr(0, slash);
}

std::wstring gLogPath;

void logf(const wchar_t* format, ...) {
  wchar_t line[2048]{};
  va_list args;
  va_start(args, format);
  _vsnwprintf_s(line, _TRUNCATE, format, args);
  va_end(args);

  wprintf(L"%s\n", line);
  if (gLogPath.empty()) return;
  FILE* file = nullptr;
  if (_wfopen_s(&file, gLogPath.c_str(), L"a, ccs=UTF-8") == 0 && file) {
    fwprintf(file, L"%s\n", line);
    fclose(file);
  }
}

std::wstring last_error_text(DWORD error) {
  wchar_t buffer[64]{};
  _snwprintf_s(buffer, _TRUNCATE, L"0x%08lX (%lu)", error, error);
  return buffer;
}

// 32-bit bottom-up BGRA. Deliberately the dumbest possible writer: the output only has to be
// openable by the person reading the answer.
bool write_bmp(const std::wstring& path, const uint8_t* bgra, int width, int height,
               int strideBytes) {
  if (!bgra || width <= 0 || height <= 0) return false;
  BITMAPFILEHEADER fileHeader{};
  BITMAPINFOHEADER infoHeader{};
  const DWORD pixelBytes = static_cast<DWORD>(width) * static_cast<DWORD>(height) * 4u;

  fileHeader.bfType = 0x4D42;  // "BM"
  fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(infoHeader);
  fileHeader.bfSize = fileHeader.bfOffBits + pixelBytes;

  infoHeader.biSize = sizeof(infoHeader);
  infoHeader.biWidth = width;
  infoHeader.biHeight = -height;  // top-down
  infoHeader.biPlanes = 1;
  infoHeader.biBitCount = 32;
  infoHeader.biCompression = BI_RGB;
  infoHeader.biSizeImage = pixelBytes;

  FILE* file = nullptr;
  if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file) return false;
  fwrite(&fileHeader, sizeof(fileHeader), 1, file);
  fwrite(&infoHeader, sizeof(infoHeader), 1, file);
  for (int y = 0; y < height; ++y) {
    fwrite(bgra + static_cast<size_t>(y) * static_cast<size_t>(strideBytes),
           static_cast<size_t>(width) * 4u, 1, file);
  }
  fclose(file);
  return true;
}

// A capture that returns an all-black or all-identical image is a failure wearing a success
// costume, so say how much variety the pixels actually have.
void describe_pixels(const wchar_t* label, const uint8_t* bgra, int width, int height,
                     int strideBytes) {
  if (!bgra) return;
  uint64_t sum = 0;
  uint8_t minValue = 255;
  uint8_t maxValue = 0;
  size_t sampled = 0;
  for (int y = 0; y < height; y += 4) {
    const uint8_t* row = bgra + static_cast<size_t>(y) * static_cast<size_t>(strideBytes);
    for (int x = 0; x < width; x += 4) {
      const uint8_t value = row[static_cast<size_t>(x) * 4u + 1u];  // green
      sum += value;
      if (value < minValue) minValue = value;
      if (value > maxValue) maxValue = value;
      ++sampled;
    }
  }
  const unsigned average = sampled ? static_cast<unsigned>(sum / sampled) : 0u;
  logf(L"      %s pixels: avg=%u min=%u max=%u %s", label, average, minValue, maxValue,
       (minValue == maxValue) ? L"<-- UNIFORM, almost certainly not real content" : L"");
}

// ---------------------------------------------------------------------------- capture attempts

bool capture_via_bitblt(const std::wstring& outPath) {
  const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  const int originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
  if (width <= 0 || height <= 0) {
    logf(L"      BitBlt: virtual screen metrics are %dx%d, nothing to capture", width, height);
    return false;
  }

  // Every GDI object here must be created *after* SetThreadDesktop; handles from the previous
  // desktop are not valid on this one.
  HDC screen = GetDC(nullptr);
  if (!screen) {
    logf(L"      BitBlt: GetDC(NULL) failed err=%s", last_error_text(GetLastError()).c_str());
    return false;
  }

  HDC memory = CreateCompatibleDC(screen);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  bool ok = false;
  if (!bitmap || !bits) {
    logf(L"      BitBlt: CreateDIBSection failed err=%s",
         last_error_text(GetLastError()).c_str());
  } else {
    HGDIOBJ previous = SelectObject(memory, bitmap);
    if (!BitBlt(memory, 0, 0, width, height, screen, originX, originY, SRCCOPY | CAPTUREBLT)) {
      logf(L"      BitBlt: BitBlt failed err=%s", last_error_text(GetLastError()).c_str());
    } else {
      GdiFlush();
      const auto* pixels = static_cast<const uint8_t*>(bits);
      ok = write_bmp(outPath, pixels, width, height, width * 4);
      logf(L"      BitBlt: captured %dx%d -> %s", width, height, ok ? L"OK" : L"write failed");
      describe_pixels(L"BitBlt", pixels, width, height, width * 4);
    }
    SelectObject(memory, previous);
    DeleteObject(bitmap);
  }
  DeleteDC(memory);
  ReleaseDC(nullptr, screen);
  return ok;
}

// "No frame arrived" and "this desktop cannot be read" are different answers, and conflating
// them is how a probe talks a project out of the right implementation. Desktop Duplication only
// produces a frame when something changes, and a UAC prompt sitting still changes nothing.
enum class Outcome { Ok, Failed, Inconclusive };

const wchar_t* outcome_text(Outcome outcome) {
  switch (outcome) {
    case Outcome::Ok: return L"OK";
    case Outcome::Inconclusive: return L"INCONCLUSIVE (no frame arrived; nothing was moving)";
    default: return L"FAIL";
  }
}

Outcome capture_via_dxgi(const std::wstring& outPath, ULONGLONG deadlineTicks) {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  // A fresh device per desktop attach on purpose: reusing one bound to another desktop is the
  // exact mistake the product's duplication recovery already makes.
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                      D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels,
                                 ARRAYSIZE(levels), D3D11_SDK_VERSION, &device, nullptr,
                                 &context);
  if (FAILED(hr)) {
    logf(L"      DXGI: D3D11CreateDevice failed hr=0x%08lX", static_cast<unsigned long>(hr));
    return Outcome::Failed;
  }

  ComPtr<IDXGIDevice> dxgiDevice;
  ComPtr<IDXGIAdapter> adapter;
  if (FAILED(device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter))) {
    logf(L"      DXGI: could not reach the adapter");
    return Outcome::Failed;
  }
  // A DuplicateOutput refusal can be a statement about the display driver rather than about the
  // desktop, so name the adapter -- an indirect/virtual display failing is a different finding.
  DXGI_ADAPTER_DESC adapterDesc{};
  if (SUCCEEDED(adapter->GetDesc(&adapterDesc))) {
    logf(L"      DXGI: adapter = %s", adapterDesc.Description);
  }

  ComPtr<IDXGIOutput> output;
  ComPtr<IDXGIOutput1> output1;
  for (UINT index = 0;; ++index) {
    ComPtr<IDXGIOutput> candidate;
    if (adapter->EnumOutputs(index, &candidate) == DXGI_ERROR_NOT_FOUND) break;
    if (!candidate) continue;
    DXGI_OUTPUT_DESC desc{};
    if (FAILED(candidate->GetDesc(&desc))) continue;
    if (desc.DesktopCoordinates.right <= desc.DesktopCoordinates.left) continue;
    output = candidate;
    break;
  }
  if (!output || FAILED(output.As(&output1))) {
    logf(L"      DXGI: no usable output on this adapter");
    return Outcome::Failed;
  }

  auto duplicate = [&](ComPtr<IDXGIOutputDuplication>* out) {
    out->Reset();
    const HRESULT dupHr = output1->DuplicateOutput(device.Get(), out->GetAddressOf());
    if (FAILED(dupHr) || !*out) {
      logf(L"      DXGI: DuplicateOutput failed hr=0x%08lX%s", static_cast<unsigned long>(dupHr),
           dupHr == E_ACCESSDENIED      ? L" (E_ACCESSDENIED)"
           : dupHr == DXGI_ERROR_UNSUPPORTED ? L" (UNSUPPORTED -- this is about the display "
                                               L"driver, not about the desktop)"
                                             : L"");
      return false;
    }
    return true;
  };

  ComPtr<IDXGIOutputDuplication> duplication;
  if (!duplicate(&duplication)) return Outcome::Failed;

  // Keep asking until the caller's deadline. A still desktop yields nothing at all, so a short
  // fixed attempt count would report failure for a prompt that is simply not animating.
  ComPtr<IDXGIResource> resource;
  DXGI_OUTDUPL_FRAME_INFO frameInfo{};
  bool acquired = false;
  while (!acquired && GetTickCount64() < deadlineTicks) {
    hr = duplication->AcquireNextFrame(250, &frameInfo, &resource);
    if (SUCCEEDED(hr)) {
      acquired = true;
      break;
    }
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
    if (hr == DXGI_ERROR_ACCESS_LOST) {
      // Expected while the desktop is switching. Rebuild and keep waiting.
      logf(L"      DXGI: ACCESS_LOST during acquire, recreating duplication");
      if (!duplicate(&duplication)) return Outcome::Failed;
      continue;
    }
    logf(L"      DXGI: AcquireNextFrame failed hr=0x%08lX", static_cast<unsigned long>(hr));
    return Outcome::Failed;
  }
  if (!acquired) {
    logf(L"      DXGI: DuplicateOutput SUCCEEDED but no frame arrived before the deadline.");
    logf(L"      DXGI: that means the desktop was readable and simply static -- move the mouse "
         L"over the prompt and run again to force a frame.");
    return Outcome::Inconclusive;
  }

  ComPtr<ID3D11Texture2D> frame;
  Outcome outcome = Outcome::Failed;
  if (SUCCEEDED(resource.As(&frame)) && frame) {
    D3D11_TEXTURE2D_DESC desc{};
    frame->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)) && staging) {
      context->CopyResource(staging.Get(), frame.Get());
      D3D11_MAPPED_SUBRESOURCE mapped{};
      if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        const auto* pixels = static_cast<const uint8_t*>(mapped.pData);
        const bool written = write_bmp(outPath, pixels, static_cast<int>(desc.Width),
                                       static_cast<int>(desc.Height),
                                       static_cast<int>(mapped.RowPitch));
        outcome = written ? Outcome::Ok : Outcome::Failed;
        logf(L"      DXGI: captured %ux%u -> %s", desc.Width, desc.Height,
             written ? L"OK" : L"write failed");
        describe_pixels(L"DXGI", pixels, static_cast<int>(desc.Width),
                        static_cast<int>(desc.Height), static_cast<int>(mapped.RowPitch));
        context->Unmap(staging.Get(), 0);
      } else {
        logf(L"      DXGI: Map failed");
      }
    } else {
      logf(L"      DXGI: staging CreateTexture2D failed");
    }
  }
  duplication->ReleaseFrame();
  return outcome;
}

// ---------------------------------------------------------------------------- agent (SYSTEM, target session)

std::wstring desktop_name(HDESK desktop) {
  wchar_t name[256]{};
  DWORD needed = 0;
  if (!GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed)) return L"?";
  return name;
}

// Attaches to whichever desktop currently receives input and captures it both ways.
//
// Runs on a thread of its own every time. SetThreadDesktop refuses if the calling thread owns
// any window or hook, and D3D11/DXGI initialisation can leave a message-only window behind, so
// reusing a thread across two attaches risks the second one failing for a reason that has
// nothing to do with the desktop's permissions.
bool attach_and_capture_on_thread(const std::wstring& outDir, const std::wstring& tag,
                                  DWORD budgetSeconds) {
  bool succeeded = false;
  std::thread worker([&]() {
    // Ask only for what capture needs. GENERIC_ALL expands to every desktop right including the
    // journal ones, and being denied one of those would look identical to the desktop being
    // unreadable.
    const ACCESS_MASK wanted = DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS;
    HDESK input = OpenInputDesktop(0, FALSE, wanted);
    if (!input) {
      const DWORD narrowError = GetLastError();
      input = OpenInputDesktop(0, FALSE, GENERIC_ALL);
      if (!input) {
        logf(L"  [%s] OpenInputDesktop failed: narrow err=%s, GENERIC_ALL err=%s", tag.c_str(),
             last_error_text(narrowError).c_str(), last_error_text(GetLastError()).c_str());
        return;
      }
    }
    const std::wstring name = desktop_name(input);
    logf(L"  [%s] input desktop = \"%s\"", tag.c_str(), name.c_str());

    if (!SetThreadDesktop(input)) {
      logf(L"  [%s] SetThreadDesktop failed err=%s", tag.c_str(),
           last_error_text(GetLastError()).c_str());
      CloseDesktop(input);
      return;
    }

    const ULONGLONG deadline =
        GetTickCount64() + static_cast<ULONGLONG>(budgetSeconds) * 1000ull;
    const bool bitbltOk = capture_via_bitblt(outDir + L"\\" + tag + L"_bitblt.bmp");
    const Outcome dxgi = capture_via_dxgi(outDir + L"\\" + tag + L"_dxgi.bmp", deadline);

    logf(L"  [%s] RESULT desktop=\"%s\" bitblt=%s dxgi=%s", tag.c_str(), name.c_str(),
         bitbltOk ? L"OK" : L"FAIL", outcome_text(dxgi));
    // Inconclusive is not success: retrying may still catch a frame.
    succeeded = bitbltOk || dxgi == Outcome::Ok;
    CloseDesktop(input);
  });
  worker.join();
  return succeeded;
}

int run_agent(const std::wstring& outDir, DWORD seconds) {
  gLogPath = outDir + L"\\probe.log";
  DWORD sessionId = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
  logf(L"agent: running in session %lu as SYSTEM, watching for %lu seconds", sessionId, seconds);

  // Control capture on the ordinary desktop. If this fails too, the probe is broken and the
  // secure-desktop result means nothing.
  logf(L"agent: control capture on the current desktop");
  attach_and_capture_on_thread(outDir, L"control", 8);

  logf(L"agent: now open a UAC prompt (run anything as administrator), leave it up, and MOVE "
       L"THE MOUSE over it -- a completely still screen produces no DXGI frame at all");
  const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ull;
  std::wstring lastSeen;
  bool captured = false;
  int attempts = 0;
  while (GetTickCount64() < deadline && !captured) {
    HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (input) {
      const std::wstring name = desktop_name(input);
      CloseDesktop(input);
      if (name != lastSeen) {
        logf(L"agent: input desktop is now \"%s\"", name.c_str());
        lastSeen = name;
      }
      if (_wcsicmp(name.c_str(), L"Default") != 0 && !name.empty() && name != L"?") {
        wchar_t tag[64]{};
        _snwprintf_s(tag, _TRUNCATE, L"secure%d", ++attempts);
        logf(L"agent: non-Default desktop detected, capturing (attempt %d)", attempts);
        // Keep trying while the prompt is still up. A first attempt can miss for reasons that
        // say nothing about access -- a transient denial, or simply no frame yet.
        captured = attach_and_capture_on_thread(outDir, tag, 10);
        if (!captured) logf(L"agent: attempt %d did not succeed, will retry", attempts);
      }
    }
    Sleep(kAgentPollMs);
  }
  if (attempts == 0) {
    logf(L"agent: no non-Default input desktop ever appeared -- was a UAC prompt actually "
         L"shown, and is this the session it appeared in?");
  } else if (!captured) {
    logf(L"agent: saw the secure desktop %d time(s) but never captured it", attempts);
  }
  logf(L"agent: done");
  return 0;
}

// ---------------------------------------------------------------------------- service (SYSTEM, session 0)

SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
SERVICE_STATUS gStatus{};
std::wstring gOutDir;
DWORD gTargetSession = 0;
DWORD gSeconds = 60;

void set_service_state(DWORD state) {
  gStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  gStatus.dwCurrentState = state;
  gStatus.dwControlsAccepted = (state == SERVICE_RUNNING) ? SERVICE_ACCEPT_STOP : 0;
  const bool pending = state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING;
  gStatus.dwCheckPoint = pending ? ++gStatus.dwCheckPoint : 0;
  // The agent run is bounded by gSeconds, so tell the SCM to expect that long rather than
  // letting it decide the service hung.
  gStatus.dwWaitHint = pending ? (gSeconds + 60) * 1000 : 0;
  if (gStatusHandle) SetServiceStatus(gStatusHandle, &gStatus);
}

DWORD WINAPI service_control(DWORD control, DWORD, LPVOID, LPVOID) {
  if (control == SERVICE_CONTROL_STOP) set_service_state(SERVICE_STOP_PENDING);
  return NO_ERROR;
}

// Retargets this SYSTEM token at the requested session and spawns the agent there. Same shape
// as the product's start_agent, except the session comes from the caller rather than from
// WTSGetActiveConsoleSessionId -- which is the very bug the streamed session exposes.
bool spawn_agent_in_session() {
  HANDLE serviceToken = nullptr;
  HANDLE agentToken = nullptr;
  bool ok = OpenProcessToken(GetCurrentProcess(),
                             TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY |
                                 TOKEN_ADJUST_SESSIONID,
                             &serviceToken) &&
            DuplicateTokenEx(serviceToken, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation,
                             TokenPrimary, &agentToken) &&
            SetTokenInformation(agentToken, TokenSessionId, &gTargetSession,
                                sizeof(gTargetSession));
  if (serviceToken) CloseHandle(serviceToken);
  if (!ok) {
    if (agentToken) CloseHandle(agentToken);
    return false;
  }

  wchar_t command[32768]{};
  _snwprintf_s(command, _TRUNCATE, L"\"%s\" --agent \"%s\" %lu", exe_path().c_str(),
               gOutDir.c_str(), gSeconds);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.lpDesktop = const_cast<wchar_t*>(L"winsta0\\default");
  PROCESS_INFORMATION process{};
  ok = CreateProcessAsUserW(agentToken, nullptr, command, nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                            &startup, &process) != FALSE;
  CloseHandle(agentToken);
  if (ok) {
    WaitForSingleObject(process.hProcess, (gSeconds + 30) * 1000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }
  return ok;
}

void WINAPI service_main(DWORD argc, LPWSTR* argv) {
  gStatusHandle = RegisterServiceCtrlHandlerExW(kServiceName, service_control, nullptr);
  if (!gStatusHandle) {
    // Without a handle every status report is silently dropped and the controller would just
    // wait out its deadline, so leave a trace in the log the user is going to read.
    if (argc >= 2) {
      gLogPath = std::wstring(argv[1]) + L"\\probe.log";
      logf(L"service: RegisterServiceCtrlHandlerExW failed err=%s",
           last_error_text(GetLastError()).c_str());
    }
    return;
  }
  set_service_state(SERVICE_START_PENDING);
  if (argc >= 3) {
    gOutDir = argv[1];
    gTargetSession = static_cast<DWORD>(_wtoi(argv[2]));
    if (argc >= 4) gSeconds = static_cast<DWORD>(_wtoi(argv[3]));
  }
  gLogPath = gOutDir + L"\\probe.log";
  set_service_state(SERVICE_RUNNING);
  if (!spawn_agent_in_session()) {
    logf(L"service: CreateProcessAsUserW into session %lu failed err=%s", gTargetSession,
         last_error_text(GetLastError()).c_str());
  }
  set_service_state(SERVICE_STOPPED);
}

// ---------------------------------------------------------------------------- controller (elevated user)

void remove_service() {
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
  if (!manager) return;
  SC_HANDLE service = OpenServiceW(manager, kServiceName, SERVICE_ALL_ACCESS);
  if (service) {
    SERVICE_STATUS status{};
    ControlService(service, SERVICE_CONTROL_STOP, &status);
    DeleteService(service);
    CloseServiceHandle(service);
  }
  CloseServiceHandle(manager);
}

int run_controller(DWORD targetSession, DWORD seconds) {
  const std::wstring outDir = exe_dir() + L"\\probe_out";
  CreateDirectoryW(outDir.c_str(), nullptr);
  gLogPath = outDir + L"\\probe.log";
  DeleteFileW(gLogPath.c_str());

  DWORD ownSession = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &ownSession);
  logf(L"controller: this session=%lu, target session=%lu, console session=%lu", ownSession,
       targetSession, WTSGetActiveConsoleSessionId());
  logf(L"controller: output -> %s", outDir.c_str());

  remove_service();  // a leftover from an interrupted run would block registration

  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
  if (!manager) {
    logf(L"controller: OpenSCManager failed err=%s -- run this as administrator",
         last_error_text(GetLastError()).c_str());
    return 2;
  }
  const std::wstring binary = L"\"" + exe_path() + L"\" --service";
  SC_HANDLE service =
      CreateServiceW(manager, kServiceName, L"GNLink Winlogon capture probe (temporary)",
                     SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                     SERVICE_ERROR_NORMAL, binary.c_str(), nullptr, nullptr, nullptr, nullptr,
                     nullptr);
  if (!service) {
    logf(L"controller: CreateService failed err=%s", last_error_text(GetLastError()).c_str());
    CloseServiceHandle(manager);
    return 3;
  }

  const std::wstring sessionText = std::to_wstring(targetSession);
  const std::wstring secondsText = std::to_wstring(seconds);
  LPCWSTR args[] = {kServiceName, outDir.c_str(), sessionText.c_str(), secondsText.c_str()};
  if (!StartServiceW(service, ARRAYSIZE(args), args)) {
    logf(L"controller: StartService failed err=%s", last_error_text(GetLastError()).c_str());
    DeleteService(service);
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return 4;
  }

  logf(L"controller: probe is running. Open a UAC prompt now and leave it up a few seconds.");
  const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(seconds + 30) * 1000ull;
  while (GetTickCount64() < deadline) {
    SERVICE_STATUS status{};
    if (!QueryServiceStatus(service, &status)) break;
    if (status.dwCurrentState == SERVICE_STOPPED) break;
    Sleep(500);
  }

  SERVICE_STATUS stopStatus{};  // required out-parameter; passing null just fails the call
  ControlService(service, SERVICE_CONTROL_STOP, &stopStatus);
  DeleteService(service);
  CloseServiceHandle(service);
  CloseServiceHandle(manager);
  logf(L"controller: temporary service removed. Read %s and the .bmp files beside it.",
       gLogPath.c_str());
  return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc >= 2 && _wcsicmp(argv[1], L"--service") == 0) {
    SERVICE_TABLE_ENTRYW table[] = {{const_cast<wchar_t*>(kServiceName), service_main},
                                    {nullptr, nullptr}};
    StartServiceCtrlDispatcherW(table);
    return 0;
  }
  if (argc >= 3 && _wcsicmp(argv[1], L"--agent") == 0) {
    const DWORD seconds = (argc >= 4) ? static_cast<DWORD>(_wtoi(argv[3])) : 60u;
    return run_agent(argv[2], seconds);
  }

  DWORD targetSession = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &targetSession);
  DWORD seconds = 60;
  if (argc >= 2) targetSession = static_cast<DWORD>(_wtoi(argv[1]));
  if (argc >= 3) seconds = static_cast<DWORD>(_wtoi(argv[2]));
  return run_controller(targetSession, seconds);
}

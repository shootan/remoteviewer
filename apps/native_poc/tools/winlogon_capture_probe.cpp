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
// Generous enough for the longest sane run, short enough that a wedge clears itself well before
// anyone notices. The service outlives the agent it waits on, so it gets the larger budget.
constexpr DWORD kServiceHardLimitSeconds = 300;

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

// Nothing here may outlive its own run. The first attempt left a SYSTEM service process alive
// in session 0 for twenty minutes -- it survived the desktop it was working on going away when
// RDP disconnected, kept the executable locked so the fix could not even be rebuilt, and needed
// administrator rights to clear. A diagnostic that can strand a SYSTEM process is worse than
// the uncertainty it was written to remove, so both SYSTEM-side modes arm a hard deadline that
// ends the process no matter what any Win32 call is doing.
void arm_self_destruct(DWORD seconds, const wchar_t* who) {
  std::thread([seconds, who]() {
    Sleep(seconds * 1000);
    // Deliberately not a graceful shutdown: the whole point is to survive a wedged GDI or DXGI
    // call, and those are exactly the ones that will not return to let us unwind politely.
    wprintf(L"%s: self-destruct deadline reached, exiting\n", who);
    ExitProcess(3);
  }).detach();
}

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
// costume, so say how much variety the pixels actually have -- and let the caller act on it.
// Returns true when the image carries real content.
//
// This matters most right after a desktop switch: the secure desktop exists and is readable
// before Windows has painted the dimmed snapshot and the dialog onto it, so an immediate
// capture legitimately succeeds and legitimately contains nothing.
bool describe_pixels(const wchar_t* label, const uint8_t* bgra, int width, int height,
                     int strideBytes) {
  if (!bgra) return false;
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
  const bool hasContent = minValue != maxValue;
  logf(L"      %s pixels: avg=%u min=%u max=%u %s", label, average, minValue, maxValue,
       hasContent ? L"" : L"<-- UNIFORM, nothing was painted yet");
  return hasContent;
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
      const bool written = write_bmp(outPath, pixels, width, height, width * 4);
      logf(L"      BitBlt: captured %dx%d -> %s", width, height,
           written ? L"OK" : L"write failed");
      // Only a picture with something in it counts. A readable but unpainted desktop must not
      // end the search, or the run stops on the one frame that proves nothing.
      ok = written && describe_pixels(L"BitBlt", pixels, width, height, width * 4);
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
    case Outcome::Ok: return L"OK (real content)";
    case Outcome::Inconclusive: return L"INCONCLUSIVE (readable, but nothing painted yet)";
    default: return L"FAIL (refused)";
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
  int pointerOnly = 0;
  while (!acquired && GetTickCount64() < deadlineTicks) {
    hr = duplication->AcquireNextFrame(250, &frameInfo, &resource);
    if (SUCCEEDED(hr)) {
      // A frame with no present time is a cursor update: the desktop texture was not refreshed
      // and reading it yields whatever was in the buffer, which on the first acquire is black.
      // Measured on the ordinary desktop, that produced a perfectly uniform image that would
      // have been recorded as a successful capture.
      if (frameInfo.LastPresentTime.QuadPart == 0) {
        ++pointerOnly;
        duplication->ReleaseFrame();
        resource.Reset();
        continue;
      }
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
    logf(L"      DXGI: DuplicateOutput SUCCEEDED but no desktop update arrived before the "
         L"deadline (%d cursor-only frames were skipped).",
         pointerOnly);
    logf(L"      DXGI: the desktop was reachable and simply not repainting. Moving the mouse "
         L"only makes cursor frames; click or drag something on the prompt to force a repaint.");
    return Outcome::Inconclusive;
  }
  if (pointerOnly > 0) logf(L"      DXGI: skipped %d cursor-only frames first", pointerOnly);

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
        logf(L"      DXGI: captured %ux%u -> %s", desc.Width, desc.Height,
             written ? L"OK" : L"write failed");
        const bool hasContent =
            describe_pixels(L"DXGI", pixels, static_cast<int>(desc.Width),
                            static_cast<int>(desc.Height), static_cast<int>(mapped.RowPitch));
        // Readable but blank is not an answer either way, so keep it distinct from a refusal.
        outcome = !written ? Outcome::Failed : (hasContent ? Outcome::Ok : Outcome::Inconclusive);
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
         bitbltOk ? L"OK (real content)" : L"no content", outcome_text(dxgi));
    // Inconclusive is not success: retrying may still catch a painted frame.
    succeeded = bitbltOk || dxgi == Outcome::Ok;
    CloseDesktop(input);
  });
  worker.join();
  return succeeded;
}

int run_agent(const std::wstring& outDir, DWORD seconds) {
  arm_self_destruct(seconds + 45, L"agent");
  // A separate file from the controller's: _wfopen_s opens without sharing, so two processes
  // appending to one log lose lines exactly when something is going wrong and the log matters.
  gLogPath = outDir + L"\\probe_agent.log";
  DWORD sessionId = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
  logf(L"agent: running in session %lu as SYSTEM, watching for %lu seconds", sessionId, seconds);

  // Control capture on the ordinary desktop. If this fails too, the probe is broken and the
  // secure-desktop result means nothing.
  logf(L"agent: control capture on the current desktop");
  attach_and_capture_on_thread(outDir, L"control", 8);

  logf(L"agent: now open a UAC prompt (run anything as administrator) and make it REPAINT --");
  logf(L"agent:   hover the Yes/No buttons, or drag the dialog. Moving the cursor alone only");
  logf(L"agent:   produces cursor frames, which carry no desktop pixels.");
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
        if (attempts == 0) {
          // The desktop exists before Windows paints the dimmed snapshot and the dialog onto
          // it. Capturing on the same tick as the switch reliably produces a black frame that
          // says nothing about whether the desktop is readable.
          logf(L"agent: %s appeared; waiting for it to paint before the first capture",
               name.c_str());
          Sleep(600);
        }
        wchar_t tag[64]{};
        _snwprintf_s(tag, _TRUNCATE, L"secure%d", ++attempts);
        logf(L"agent: capturing %s (attempt %d)", name.c_str(), attempts);
        captured = attach_and_capture_on_thread(outDir, tag, 10);
        if (!captured) {
          logf(L"agent: attempt %d produced nothing to look at, retrying while it is still up",
               attempts);
        }
      }
    }
    Sleep(kAgentPollMs);
  }
  if (attempts == 0) {
    logf(L"agent: no non-Default input desktop ever appeared -- was a UAC prompt actually "
         L"shown, and is this the session it appeared in?");
  } else if (!captured) {
    // Distinguishable from a refusal: every attach worked, every capture call succeeded, and
    // the pixels were blank each time. That is its own finding and must not be filed as "DXGI
    // and GDI cannot read Winlogon".
    logf(L"agent: attached to the secure desktop %d time(s) and every capture returned blank.",
         attempts);
    logf(L"agent: access was never refused -- check the newest secure*.bmp by eye before "
         L"concluding anything.");
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
  logf(L"service: spawning %s", command);
  ok = CreateProcessAsUserW(agentToken, nullptr, command, nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                            &startup, &process) != FALSE;
  CloseHandle(agentToken);
  if (ok) {
    logf(L"service: agent started pid=%lu", process.dwProcessId);
    const DWORD waited =
        WaitForSingleObject(process.hProcess, (gSeconds + 30) * 1000);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    // An agent that dies instantly is the failure that leaves no other trace: it writes no log
    // of its own, so its exit code is the only thing that says what happened.
    logf(L"service: agent finished wait=%lu exitCode=0x%08lX%s", waited, exitCode,
         exitCode == 0xC0000142u ? L" (DLL init failed -- the target session's window station "
                                   L"was not reachable)"
                                 : L"");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }
  return ok;
}

// The service reads its parameters from a file next to the exe rather than from the arguments
// StartService passes. The first run produced a log with controller lines only -- no service
// line at all -- and service start arguments were the one link that could not be observed from
// outside. A file can be checked before and after, so this removes the unobservable step
// instead of guessing about it.
std::wstring config_path() { return exe_dir() + L"\\probe_config.txt"; }

bool write_config(const std::wstring& outDir, DWORD session, DWORD seconds) {
  FILE* file = nullptr;
  if (_wfopen_s(&file, config_path().c_str(), L"w, ccs=UTF-8") != 0 || !file) return false;
  fwprintf(file, L"%s\n%lu\n%lu\n", outDir.c_str(), session, seconds);
  fclose(file);
  return true;
}

bool read_config() {
  FILE* file = nullptr;
  if (_wfopen_s(&file, config_path().c_str(), L"r, ccs=UTF-8") != 0 || !file) return false;
  wchar_t dir[MAX_PATH]{};
  wchar_t session[32]{};
  wchar_t seconds[32]{};
  const bool ok = fgetws(dir, MAX_PATH, file) && fgetws(session, 32, file) &&
                  fgetws(seconds, 32, file);
  fclose(file);
  if (!ok) return false;
  std::wstring dirText = dir;
  while (!dirText.empty() && (dirText.back() == L'\n' || dirText.back() == L'\r')) {
    dirText.pop_back();
  }
  gOutDir = dirText;
  gTargetSession = static_cast<DWORD>(_wtoi(session));
  gSeconds = static_cast<DWORD>(_wtoi(seconds));
  return !gOutDir.empty();
}

void WINAPI service_main(DWORD, LPWSTR*) {
  // Log to a path that does not depend on anything read at runtime, and do it before any other
  // call, so a service that dies early still says it was alive.
  gLogPath = exe_dir() + L"\\probe_service.log";
  DeleteFileW(gLogPath.c_str());
  logf(L"service: entered service_main");
  // Armed before anything that can block, including StartServiceCtrlDispatcherW's wait for the
  // SCM to acknowledge a stop that a wedged service_main will never send.
  arm_self_destruct(kServiceHardLimitSeconds, L"service");

  gStatusHandle = RegisterServiceCtrlHandlerExW(kServiceName, service_control, nullptr);
  if (!gStatusHandle) {
    logf(L"service: RegisterServiceCtrlHandlerExW failed err=%s",
         last_error_text(GetLastError()).c_str());
    return;
  }
  set_service_state(SERVICE_START_PENDING);

  if (!read_config()) {
    logf(L"service: could not read %s", config_path().c_str());
    set_service_state(SERVICE_STOPPED);
    return;
  }
  logf(L"service: outDir=%s targetSession=%lu seconds=%lu", gOutDir.c_str(), gTargetSession,
       gSeconds);

  set_service_state(SERVICE_RUNNING);
  if (!spawn_agent_in_session()) {
    logf(L"service: CreateProcessAsUserW into session %lu failed err=%s", gTargetSession,
         last_error_text(GetLastError()).c_str());
  }
  logf(L"service: finished, stopping");
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

  if (!write_config(outDir, targetSession, seconds)) {
    logf(L"controller: could not write %s", config_path().c_str());
    DeleteService(service);
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return 5;
  }
  if (!StartServiceW(service, 0, nullptr)) {
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
  DeleteFileW(config_path().c_str());

  // Fold the SYSTEM-side logs into the one the user was told to read. Three logs in three
  // places is how a diagnostic gets misread as "nothing happened".
  auto absorb = [&](const std::wstring& path, const wchar_t* heading, const wchar_t* ifMissing) {
    FILE* source = nullptr;
    if (_wfopen_s(&source, path.c_str(), L"r, ccs=UTF-8") != 0 || !source) {
      logf(L"%s", ifMissing);
      return;
    }
    logf(L"%s", heading);
    wchar_t line[2048]{};
    while (fgetws(line, ARRAYSIZE(line), source)) {
      std::wstring text = line;
      while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r')) text.pop_back();
      if (!text.empty()) logf(L"%s", text.c_str());
    }
    fclose(source);
    DeleteFileW(path.c_str());
  };
  absorb(exe_dir() + L"\\probe_service.log", L"---- service (SYSTEM, session 0) ----",
         L"---- the service never reached service_main and left no log ----");
  absorb(outDir + L"\\probe_agent.log", L"---- agent (SYSTEM, target session) ----",
         L"---- the agent never started or never wrote a line ----");

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

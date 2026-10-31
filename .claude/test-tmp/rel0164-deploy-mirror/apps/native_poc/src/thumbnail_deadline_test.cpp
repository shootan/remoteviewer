// A window that pumps messages and never answers WM_PRINT, and what the current capture does
// when it meets one.
//
// It was built to be the negative control for the deadline work and ended up establishing
// something else. Both are below; the second matters more.
//
// The fixture is a window that pumps messages and never returns from WM_PRINT -- a nested message
// loop inside the handler. That is the incident's shape rather than an ordinary hang: WM_NULL
// still comes back and IsHungAppWindow still calls the window healthy, so the guard
// host_bgra_scale.cpp relies on today cannot see it. Both are asserted.
//
// What that fixture does NOT do is block the product. capture_window_thumbnail asks for
// PW_RENDERFULLCONTENT, and on this OS that is served from the DWM redirection surface: measured
// here, captures against the hung fixture succeed in ~24ms and leave its WM_PRINT counter at
// ZERO. The window is never asked.
//
// So the deadline's negative control cannot be built this way, and that is asserted rather than
// worked around -- if a future Windows starts dispatching WM_PRINT for these flags, the check
// below fails and says so.
//
// It also narrows the incident. If the request never reaches the application, "LDPlayer did not
// answer WM_PRINT" cannot be what blocked the control session for 1h50m. The block has to be on
// the DWM/GPU side of PrintWindow -- where that morning's TDR (amdkmdag.sys, named in the kernel
// mini-dump) also is. A block there cannot be cancelled from user mode at all, which is why the
// capture has to live somewhere a TerminateProcess can reach.
//
// One executable, three modes. The probe runs in a child process, which is what lets the parent
// give up on it: the parent cannot be the thing that blocks, so the blocking call is somewhere it
// can kill. A job object with KILL_ON_JOB_CLOSE means no child outlives this process even if it
// exits badly.

#include <windows.h>
// windows.h defines min/max as macros, and this file calls std::max. Same undef the capture
// worker does for the same reason.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include <d3d11.h>
#include <dxgi1_2.h>

#include "host_bgra_scale.hpp"
#include "time_utils.hpp"

namespace {

using remote60::native_poc::capture_window_thumbnail;
using remote60::native_poc::qpc_now_us;

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

// The deadline a fixed host has to meet. Not a product constant yet -- the host side is not
// written. It is the number this suite measures against, and the baseline below is what says
// whether it is a sane one.
constexpr uint64_t kCandidateDeadlineUs = 1000 * 1000;
// How long the negative control waits before concluding the current code missed the deadline.
// Generous on purpose: the claim is "did not answer in time", and a wide margin makes a slow
// machine unable to turn that into a false accusation.
constexpr DWORD kNegativeControlWaitMs = 5000;

enum class FixtureKind { Normal, Hang, Gpu, Layered };
bool gHangOnPrint = false;
volatile bool gStop = false;
// How many WM_PRINT / WM_PRINTCLIENT the fixture window has been sent. The parent asks for this
// over kMsgPrintCount, because "did the window get asked" is the difference between inferring
// where PrintWindow went and knowing.
volatile LONG gPrintCount = 0;
constexpr UINT kMsgPrintCount = WM_APP + 71;

LRESULT CALLBACK fixture_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == kMsgPrintCount) return static_cast<LRESULT>(gPrintCount);
  if (msg == WM_PRINT || msg == WM_PRINTCLIENT) InterlockedIncrement(&gPrintCount);
  if ((msg == WM_PRINT || msg == WM_PRINTCLIENT) && gHangOnPrint) {
    // Never return, but keep the thread dispatching. This is the shape of the real failure: the
    // sender of WM_PRINT waits forever inside SendMessage while the window goes on looking alive
    // to everybody else.
    while (!gStop) {
      MSG pending;
      while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&pending);
        DispatchMessageW(&pending);
      }
      Sleep(1);
    }
    return 0;
  }
  if (msg == WM_PRINT || msg == WM_PRINTCLIENT) {
    // The cooperative case: paint something real so the baseline is not measuring an empty DC.
    HDC dc = reinterpret_cast<HDC>(wParam);
    RECT rc{};
    GetClientRect(hwnd, &rc);
    HBRUSH brush = CreateSolidBrush(RGB(40, 90, 160));
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
    for (int i = 0; i < 40; ++i) {
      RECT band{rc.left, rc.top + i * 20, rc.right, rc.top + i * 20 + 10};
      HBRUSH b = CreateSolidBrush(RGB(20 + i * 3, 60, 200 - i * 3));
      FillRect(dc, &band, b);
      DeleteObject(b);
    }
    return 0;
  }
  if (msg == WM_CLOSE) {
    gStop = true;
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/**
 * Attach a flip-model swapchain and draw one frame.
 *
 * Flip-model on purpose. It is what a real GPU application presents with, and it is the
 * presentation mode that made the picker unreachable for GDI in this codebase before -- so if
 * anything is going to make the capture behave differently from a FillRect window, it is this.
 */
struct GpuSurface {
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  IDXGISwapChain1* swapChain = nullptr;
  ID3D11RenderTargetView* rtv = nullptr;

  bool Create(HWND hwnd, int width, int height) {
    D3D_FEATURE_LEVEL level{};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                 D3D11_SDK_VERSION, &device, &level, &context))) {
      return false;
    }
    IDXGIDevice* dxgiDevice = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice)))) {
      return false;
    }
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    bool ok = SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
              SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory)));
    if (ok) {
      DXGI_SWAP_CHAIN_DESC1 desc{};
      desc.Width = static_cast<UINT>(width);
      desc.Height = static_cast<UINT>(height);
      desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      desc.SampleDesc.Count = 1;
      desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
      desc.BufferCount = 2;
      desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
      ok = SUCCEEDED(factory->CreateSwapChainForHwnd(device, hwnd, &desc, nullptr, nullptr,
                                                    &swapChain));
    }
    if (ok) {
      ID3D11Texture2D* back = nullptr;
      ok = SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                          reinterpret_cast<void**>(&back))) &&
           SUCCEEDED(device->CreateRenderTargetView(back, nullptr, &rtv));
      if (back) back->Release();
    }
    if (factory) factory->Release();
    if (adapter) adapter->Release();
    if (dxgiDevice) dxgiDevice->Release();
    return ok;
  }

  void Present() {
    if (!swapChain || !rtv) return;
    const float colour[4] = {0.12f, 0.35f, 0.65f, 1.0f};
    context->ClearRenderTargetView(rtv, colour);
    swapChain->Present(0, 0);
  }

  ~GpuSurface() {
    if (rtv) rtv->Release();
    if (swapChain) swapChain->Release();
    if (context) context->Release();
    if (device) device->Release();
  }
};

/** Fixture mode: create the window, publish its handle, then pump until killed. */
int run_fixture(FixtureKind kind, const std::wstring& readyEventName, const std::wstring& hwndFile,
                int width, int height) {
  gHangOnPrint = kind == FixtureKind::Hang;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = fixture_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"Remote60ThumbnailFixture";
  RegisterClassExW(&wc);

  // Off screen: nothing here should appear in front of whoever is using this machine.
  // WS_EX_NOREDIRECTIONBITMAP was an attempt to force the request to the window, and it did not
  // work.
  //
  // The reasoning was that PW_RENDERFULLCONTENT is served from the DWM redirection surface, so a
  // window without one would leave DWM nothing to copy. Measured: it made no difference. The
  // capture still completed in ~24ms and the window's WM_PRINT counter was still zero, with the
  // flag and without it.
  //
  // Kept because the fixture is the more adversarial shape and the checks below are written
  // against what was actually observed. What it does NOT support is any claim about where the
  // request went -- "the window is not asked" is measured; "DWM waits internally" is not.
  // Layered windows are the other shape worth measuring: DWM composes them differently, and the
  // capture path has a separate flag for them.
  const DWORD exStyle = kind == FixtureKind::Layered ? WS_EX_LAYERED : 0;
  HWND hwnd = CreateWindowExW(exStyle, wc.lpszClassName, L"remote60 thumbnail fixture",
                              WS_OVERLAPPEDWINDOW, -6000, -6000, width, height, nullptr, nullptr,
                              wc.hInstance, nullptr);
  if (!hwnd) return 2;
  if (kind == FixtureKind::Layered) SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
  ShowWindow(hwnd, SW_SHOWNOACTIVATE);
  UpdateWindow(hwnd);

  GpuSurface gpu;
  if (kind == FixtureKind::Gpu && !gpu.Create(hwnd, width, height)) return 3;
  if (kind == FixtureKind::Gpu) gpu.Present();

  FILE* f = nullptr;
  if (_wfopen_s(&f, hwndFile.c_str(), L"w") == 0 && f) {
    std::fprintf(f, "%llu",
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(hwnd)));
    std::fclose(f);
  } else {
    return 2;
  }

  HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, readyEventName.c_str());
  if (ready) {
    SetEvent(ready);
    CloseHandle(ready);
  }

  if (kind == FixtureKind::Gpu) {
    // Keep presenting. A window that presented once and stopped is not what the deadline has to
    // survive -- a game mid-frame is.
    MSG msg;
    while (!gStop) {
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return 0;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      gpu.Present();
      Sleep(8);
    }
    return 0;
  }

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}

/** Probe mode: one capture against one window. Runs as a child so the parent can give up on it. */
int run_probe(HWND target) {
  std::vector<uint8_t> bgra;
  uint32_t w = 0;
  uint32_t h = 0;
  const uint64_t start = qpc_now_us();
  const bool ok = capture_window_thumbnail(target, 256, 160, &bgra, &w, &h);
  const uint64_t elapsed = qpc_now_us() - start;
  std::printf("probe ok=%d elapsedUs=%llu w=%u h=%u\n", ok ? 1 : 0,
              static_cast<unsigned long long>(elapsed), w, h);
  return ok ? 0 : 1;
}

std::wstring self_path() {
  std::wstring path(32768, L'\0');
  const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  path.resize(n);
  return path;
}

std::wstring temp_file(const wchar_t* tag) {
  wchar_t dir[MAX_PATH]{};
  GetTempPathW(MAX_PATH, dir);
  std::wstring path = dir;
  path += L"remote60_thumb_";
  path += tag;
  path += L"_";
  path += std::to_wstring(GetCurrentProcessId());
  path += L".txt";
  return path;
}

struct Child {
  PROCESS_INFORMATION pi{};
  HWND hwnd = nullptr;
  bool ok = false;
};

/**
 * Start a fixture child inside `job` and wait until it reports its window.
 *
 * Every call gets its own event and handle file. Sharing them by mode looked tidier and was
 * wrong: the ready event is manual-reset, so the second fixture of the same kind would sail past
 * a wait the FIRST one had already signalled, then read a stale handle out of the file.
 */
Child start_fixture(HANDLE job, FixtureKind kind, int width, int height) {
  static int nonce = 0;
  const std::wstring tag =
      std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(++nonce);
  Child child;
  const std::wstring eventName =
      L"Local\\remote60_thumb_ready_" + tag;
  const std::wstring hwndFile = temp_file(tag.c_str());
  DeleteFileW(hwndFile.c_str());

  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
  if (!ready) return child;

  const wchar_t* kindName = kind == FixtureKind::Hang      ? L"hang"
                            : kind == FixtureKind::Gpu     ? L"gpu"
                            : kind == FixtureKind::Layered ? L"layered"
                                                           : L"normal";
  std::wstring cmd = L"\"" + self_path() + L"\" --fixture " + kindName +
                     L" --ready \"" + eventName + L"\" --hwnd-file \"" + hwndFile + L"\"" +
                     L" --size " + std::to_wstring(width) + L"x" + std::to_wstring(height);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &child.pi)) {
    CloseHandle(ready);
    return child;
  }
  AssignProcessToJobObject(job, child.pi.hProcess);
  ResumeThread(child.pi.hThread);

  const DWORD waited = WaitForSingleObject(ready, 10000);
  CloseHandle(ready);
  if (waited != WAIT_OBJECT_0) return child;

  FILE* f = nullptr;
  if (_wfopen_s(&f, hwndFile.c_str(), L"r") == 0 && f) {
    unsigned long long raw = 0;
    if (std::fscanf(f, "%llu", &raw) == 1) {
      child.hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(raw));
      child.ok = child.hwnd != nullptr && IsWindow(child.hwnd);
    }
    std::fclose(f);
  }
  DeleteFileW(hwndFile.c_str());
  return child;
}

/** Spawn a probe child against `target`. The caller decides how long to care. */
PROCESS_INFORMATION start_probe(HANDLE job, HWND target) {
  PROCESS_INFORMATION pi{};
  std::wstring cmd = L"\"" + self_path() + L"\" --probe " +
                     std::to_wstring(reinterpret_cast<uintptr_t>(target));
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  if (CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
  }
  return pi;
}

void close_child(PROCESS_INFORMATION* pi) {
  if (pi->hThread) CloseHandle(pi->hThread);
  if (pi->hProcess) CloseHandle(pi->hProcess);
  *pi = PROCESS_INFORMATION{};
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  // --- child modes ---------------------------------------------------------
  std::wstring fixtureMode, readyName, hwndFile, sizeArg;
  HWND probeTarget = nullptr;
  for (int i = 1; i < argc; ++i) {
    const std::wstring key = argv[i];
    if (key == L"--fixture" && i + 1 < argc) fixtureMode = argv[++i];
    else if (key == L"--ready" && i + 1 < argc) readyName = argv[++i];
    else if (key == L"--hwnd-file" && i + 1 < argc) hwndFile = argv[++i];
    else if (key == L"--size" && i + 1 < argc) sizeArg = argv[++i];
    else if (key == L"--probe" && i + 1 < argc) {
      probeTarget = reinterpret_cast<HWND>(static_cast<uintptr_t>(std::wcstoull(argv[++i], nullptr, 10)));
    }
  }
  if (!fixtureMode.empty()) {
    int w = 1280, h = 800;
    if (!sizeArg.empty()) {
      const size_t x = sizeArg.find(L'x');
      if (x != std::wstring::npos) {
        w = std::stoi(sizeArg.substr(0, x));
        h = std::stoi(sizeArg.substr(x + 1));
      }
    }
    const FixtureKind kind = fixtureMode == L"hang"      ? FixtureKind::Hang
                             : fixtureMode == L"gpu"     ? FixtureKind::Gpu
                             : fixtureMode == L"layered" ? FixtureKind::Layered
                                                         : FixtureKind::Normal;
    return run_fixture(kind, readyName, hwndFile, w, h);
  }
  if (probeTarget != nullptr) return run_probe(probeTarget);

  // --- orchestration -------------------------------------------------------
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  // 1. Baseline: what a window that answers actually costs.
  //
  // Measured in-process on purpose. A cooperative window cannot wedge us, and routing it through
  // a child would put process creation -- tens of milliseconds of it -- inside the number the
  // deadline is supposed to be derived from.
  std::cout << "--- baseline: windows that answer WM_PRINT ---\n";
  // Not just sizes any more: the kind of surface behind the window is the variable that matters
  // once the copy is DWM's rather than the application's.
  const struct { int w; int h; FixtureKind kind; const char* label; } sizes[] = {
      {640, 480, FixtureKind::Normal, "gdi 640x480"},
      {1280, 800, FixtureKind::Normal, "gdi 1280x800"},
      {1920, 1080, FixtureKind::Normal, "gdi 1920x1080"},
      {1280, 800, FixtureKind::Layered, "layered 1280x800"},
      {1920, 1080, FixtureKind::Gpu, "d3d11 flip 1920x1080"},
  };
  uint64_t worstNormalUs = 0;
  for (const auto& size : sizes) {
    Child fixture = start_fixture(job, size.kind, size.w, size.h);
    if (!fixture.ok) {
      check(std::string("baseline fixture ") + size.label + " started", false);
      continue;
    }
    std::vector<uint64_t> samples;
    for (int i = 0; i < 12; ++i) {
      std::vector<uint8_t> bgra;
      uint32_t tw = 0, th = 0;
      const uint64_t start = qpc_now_us();
      const bool ok = capture_window_thumbnail(fixture.hwnd, 256, 160, &bgra, &tw, &th);
      const uint64_t elapsed = qpc_now_us() - start;
      if (ok) samples.push_back(elapsed);
    }
    std::sort(samples.begin(), samples.end());
    if (samples.empty()) {
      check(std::string("baseline ") + size.label + " produced samples", false);
    } else {
      const uint64_t median = samples[samples.size() / 2];
      const uint64_t worst = samples.back();
      worstNormalUs = std::max(worstNormalUs, worst);
      std::printf("  %-22s n=%zu  min=%lluus  median=%lluus  max=%lluus\n", size.label,
                  samples.size(), static_cast<unsigned long long>(samples.front()),
                  static_cast<unsigned long long>(median),
                  static_cast<unsigned long long>(worst));
    }
    TerminateProcess(fixture.pi.hProcess, 0);
    WaitForSingleObject(fixture.pi.hProcess, 2000);
    close_child(&fixture.pi);
  }
  check("every cooperative capture finished well inside the candidate deadline",
        worstNormalUs > 0 && worstNormalUs < kCandidateDeadlineUs / 2,
        "worst " + std::to_string(worstNormalUs) + "us vs deadline " +
            std::to_string(kCandidateDeadlineUs) + "us");

  // 2. The fixture has to be the right kind of broken.
  std::cout << "\n--- the hung window, as the host's existing guards see it ---\n";
  Child hung = start_fixture(job, FixtureKind::Hang, 1280, 800);
  check("the hung fixture started and published a window", hung.ok);
  if (!hung.ok) {
    std::cout << "\nRESULT: FAILED  (" << gChecks << " checks, " << gFailures << " failed)\n";
    CloseHandle(job);
    return 1;
  }

  // Provoke the stall, then look at the window from outside while it is stuck.
  PROCESS_INFORMATION stuckProbe = start_probe(job, hung.hwnd);
  check("a probe child was started against the hung window", stuckProbe.hProcess != nullptr);
  Sleep(500);  // let it get inside PrintWindow

  DWORD_PTR dummy = 0;
  const LRESULT pumped = SendMessageTimeoutW(hung.hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 2000, &dummy);
  check("the window still answers WM_NULL while WM_PRINT is outstanding", pumped != 0,
        "this is what makes it the incident's shape rather than an ordinary hang");
  check("IsHungAppWindow does NOT flag it", IsHungAppWindow(hung.hwnd) == FALSE,
        "the guard host_bgra_scale.cpp relies on today cannot see this window");

  // 3. Where the request actually goes.
  //
  // This started life as the negative control -- "the current code does not answer in time". It
  // does answer, every time, and why is worth pinning down rather than deleting.
  std::cout << "\n--- where PrintWindow routes the request ---\n";
  const DWORD waited = WaitForSingleObject(stuckProbe.hProcess, kNegativeControlWaitMs);
  check("the capture COMPLETES against a window that never returns from WM_PRINT",
        waited == WAIT_OBJECT_0,
        "so the deadline cannot be exercised through a WM_PRINT fixture on this OS");

  DWORD_PTR printCount = 0;
  SendMessageTimeoutW(hung.hwnd, kMsgPrintCount, 0, 0, SMTO_ABORTIFHUNG, 2000, &printCount);
  check("...because the window was never sent WM_PRINT at all", printCount == 0,
        std::to_string(printCount) + " received -- if this ever becomes non-zero the OS changed "
        "and a WM_PRINT fixture becomes usable again");

  std::cout << "  NOTE: the deadline negative control is deferred to the helper\n"
               "        boundary, where a stall can be injected in a process we own.\n";

  // 4. The harness can always get out. The probe finished on its own here, so this is not yet
  // exercising a kill -- but the property has to hold either way, and it is the whole reason the
  // probe runs in a child process.
  TerminateProcess(stuckProbe.hProcess, 1);
  const DWORD died = WaitForSingleObject(stuckProbe.hProcess, 5000);
  check("the probe child is always reclaimable by the process that started it",
        died == WAIT_OBJECT_0);
  close_child(&stuckProbe);

  // 5. A second window is unaffected: one bad window must not cost the others.
  Child healthy = start_fixture(job, FixtureKind::Normal, 1280, 800);
  check("a healthy window still captures while the hung one is stuck", [&] {
    if (!healthy.ok) return false;
    std::vector<uint8_t> bgra;
    uint32_t tw = 0, th = 0;
    return capture_window_thumbnail(healthy.hwnd, 256, 160, &bgra, &tw, &th);
  }());
  if (healthy.pi.hProcess) {
    TerminateProcess(healthy.pi.hProcess, 0);
    WaitForSingleObject(healthy.pi.hProcess, 2000);
    close_child(&healthy.pi);
  }

  // 6. Nothing survives us. KILL_ON_JOB_CLOSE is the whole cleanup story.
  HANDLE hungProcess = hung.pi.hProcess;
  gStop = true;
  CloseHandle(job);  // takes the hung fixture with it
  const DWORD fixtureGone = WaitForSingleObject(hungProcess, 5000);
  check("closing the job takes every child with it", fixtureGone == WAIT_OBJECT_0,
        "including the one still inside WM_PRINT");
  close_child(&hung.pi);

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  ("
            << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}

#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "gdi_capture_protocol.hpp"
#include "time_utils.hpp"

namespace {

using remote60::native_poc::qpc_now_us;
namespace protocol = remote60::native_poc::gdi_capture;

struct Args {
  std::wstring mapping;
  std::wstring frameEvent;
  std::wstring stopEvent;
  uint32_t fps = 60;
  bool captureLayeredWindows = false;
};

bool parse_u32(const wchar_t* raw, uint32_t* out) {
  if (!raw || !out) return false;
  wchar_t* end = nullptr;
  const unsigned long value = std::wcstoul(raw, &end, 10);
  if (!end || *end != L'\0') return false;
  *out = static_cast<uint32_t>(value);
  return true;
}

Args parse_args(int argc, wchar_t** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::wstring key = argv[i];
    if (key == L"--mapping" && i + 1 < argc) args.mapping = argv[++i];
    else if (key == L"--frame-event" && i + 1 < argc) args.frameEvent = argv[++i];
    else if (key == L"--stop-event" && i + 1 < argc) args.stopEvent = argv[++i];
    else if (key == L"--fps" && i + 1 < argc) (void)parse_u32(argv[++i], &args.fps);
    else if (key == L"--capture-layered") args.captureLayeredWindows = true;
  }
  args.fps = std::clamp<uint32_t>(args.fps, 1, 120);
  return args;
}

struct SlotSurface {
  HDC dc = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ previous = nullptr;
};

void draw_cursor(HDC target, const RECT& monitorRect) {
  CURSORINFO cursor{};
  cursor.cbSize = sizeof(cursor);
  if (!GetCursorInfo(&cursor) || cursor.flags != CURSOR_SHOWING || !cursor.hCursor) return;
  ICONINFO icon{};
  if (!GetIconInfo(cursor.hCursor, &icon)) return;
  const int x = cursor.ptScreenPos.x - monitorRect.left - static_cast<int>(icon.xHotspot);
  const int y = cursor.ptScreenPos.y - monitorRect.top - static_cast<int>(icon.yHotspot);
  (void)DrawIconEx(target, x, y, cursor.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
  if (icon.hbmMask) DeleteObject(icon.hbmMask);
  if (icon.hbmColor) DeleteObject(icon.hbmColor);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  const Args args = parse_args(argc, argv);
  if (args.mapping.empty() || args.frameEvent.empty() || args.stopEvent.empty()) return 2;

  SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
  (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, args.mapping.c_str());
  HANDLE frameEvent = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, args.frameEvent.c_str());
  HANDLE stopEvent = OpenEventW(SYNCHRONIZE, FALSE, args.stopEvent.c_str());
  if (!mapping || !frameEvent || !stopEvent) return 3;
  void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
  if (!view) return 4;
  auto* header = static_cast<protocol::SharedHeader*>(view);
  if (header->magic != protocol::kMagic || header->version != protocol::kVersion ||
      header->slotCount != protocol::kSlotCount || header->width < 2 || header->height < 2 ||
      header->stride != header->width * 4 ||
      header->frameBytes != header->stride * header->height) {
    return 5;
  }

  const HMONITOR monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO monitorInfo{};
  monitorInfo.cbSize = sizeof(monitorInfo);
  if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) return 6;
  const LONG monitorWidth = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
  const LONG monitorHeight = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
  if (monitorWidth != static_cast<LONG>(header->width) ||
      monitorHeight != static_cast<LONG>(header->height)) {
    return 7;
  }

  HDC screen = GetDC(nullptr);
  if (!screen) return 8;
  BITMAPINFO bitmapInfo{};
  bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(header->width);
  bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(header->height);
  bitmapInfo.bmiHeader.biPlanes = 1;
  bitmapInfo.bmiHeader.biBitCount = 32;
  bitmapInfo.bmiHeader.biCompression = BI_RGB;

  SlotSurface surfaces[protocol::kSlotCount]{};
  bool surfacesReady = true;
  for (uint32_t i = 0; i < protocol::kSlotCount; ++i) {
    surfaces[i].dc = CreateCompatibleDC(screen);
    void* bits = nullptr;
    const size_t offset = protocol::frame_data_offset(i, header->frameBytes);
    surfaces[i].bitmap = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &bits,
                                          mapping, static_cast<DWORD>(offset));
    if (!surfaces[i].dc || !surfaces[i].bitmap || !bits) {
      surfacesReady = false;
      break;
    }
    surfaces[i].previous = SelectObject(surfaces[i].dc, surfaces[i].bitmap);
  }
  if (!surfacesReady) return 9;

  HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* high resolution */,
                                        TIMER_MODIFY_STATE | SYNCHRONIZE);
  const uint64_t intervalUs = std::max<uint64_t>(1, 1000000ULL / args.fps);
  uint64_t nextFrameUs = qpc_now_us();
  uint64_t sequence = 0;
  uint32_t cursor = 0;
  while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0) {
    const uint64_t nowUs = qpc_now_us();
    if (nowUs < nextFrameUs) {
      const uint64_t remainUs = nextFrameUs - nowUs;
      if (timer && remainUs > 100) {
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(std::max<uint64_t>(1, remainUs - 50) * 10);
        if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
          HANDLE waits[] = {stopEvent, timer};
          if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0) break;
        }
      } else {
        SwitchToThread();
      }
      continue;
    }
    nextFrameUs = std::max(nextFrameUs + intervalUs, nowUs);

    uint32_t selected = protocol::kSlotCount;
    for (uint32_t attempt = 0; attempt < protocol::kSlotCount; ++attempt) {
      const uint32_t candidate = (cursor + attempt) % protocol::kSlotCount;
      if (InterlockedCompareExchange(&header->slots[candidate].state, protocol::SlotWriting,
                                     protocol::SlotFree) == protocol::SlotFree) {
        selected = candidate;
        cursor = (candidate + 1) % protocol::kSlotCount;
        break;
      }
    }
    if (selected == protocol::kSlotCount) continue;

    const uint64_t captureStartUs = qpc_now_us();
    const DWORD rasterOperation = SRCCOPY | (args.captureLayeredWindows ? CAPTUREBLT : 0);
    const BOOL copied = BitBlt(surfaces[selected].dc, 0, 0, header->width, header->height,
                               screen, monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
                               rasterOperation);
    if (copied) draw_cursor(surfaces[selected].dc, monitorInfo.rcMonitor);
    const uint64_t captureDoneUs = qpc_now_us();
    if (!copied) {
      InterlockedExchange(&header->slots[selected].state, protocol::SlotFree);
      continue;
    }
    header->slots[selected].sequence = ++sequence;
    header->slots[selected].captureQpcUs = captureStartUs;
    header->slots[selected].captureCopyUs = captureDoneUs - captureStartUs;
    MemoryBarrier();
    InterlockedExchange(&header->slots[selected].state, protocol::SlotReady);
    SetEvent(frameEvent);
  }

  if (timer) CloseHandle(timer);
  for (auto& surface : surfaces) {
    if (surface.dc && surface.previous) SelectObject(surface.dc, surface.previous);
    if (surface.bitmap) DeleteObject(surface.bitmap);
    if (surface.dc) DeleteDC(surface.dc);
  }
  ReleaseDC(nullptr, screen);
  UnmapViewOfFile(view);
  CloseHandle(stopEvent);
  CloseHandle(frameEvent);
  CloseHandle(mapping);
  return 0;
}

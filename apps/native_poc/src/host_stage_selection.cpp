// Stage 5: monitor / capture-mode / window selection and window rebind.
//
// Host split refactor Phase 3.5: moved verbatim out of host_main_loop.cpp so each stage reads on its
// own; see host_main_loop.hpp for the loop, HostContext / TickContext and Flow.


#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "capture_backend_dxgi.hpp"
#include "d3d_capture_readback.hpp"
#include "encode_resolution_ladder.hpp"
#include "gdi_capture_process.hpp"
#include "host_abr.hpp"
#include "host_args.hpp"
#include "host_backend_policy.hpp"
#include "host_bgra_scale.hpp"
#include "host_bottleneck.hpp"
#include "host_capture_device.hpp"
#include "host_capture_session.hpp"
#include "host_client_metrics.hpp"
#include "host_control_session.hpp"
#include "host_encoded_sender.hpp"
#include "host_encoder_manager.hpp"
#include "host_frame_gate.hpp"
#include "host_frame_state.hpp"
#include "host_gpu_scaler.hpp"
#include "host_input_inject.hpp"
#include "host_input_router.hpp"
#include "host_kick.hpp"
#include "host_log.hpp"
#include "host_main_loop.hpp"
#include "host_net_io.hpp"
#include "host_session.hpp"
#include "host_stats.hpp"
#include "host_string_util.hpp"
#include "host_watchdog.hpp"
#include "host_window_enum.hpp"
#include "mf_h264_codec.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using remote60::host::DesktopCaptureBackend;
using remote60::host::DxgiDesktopCaptureConfig;
using remote60::host::DxgiDesktopCaptureSession;

namespace remote60::native_poc {

Flow stage_selection(HostContext& hx, TickContext& tc) {
  auto& useH264 = hx.useH264;
  auto& captureWindowRebindIntervalUs = hx.captureWindowRebindIntervalUs;
  auto& nextCaptureWindowCheckUs = hx.nextCaptureWindowCheckUs;
  auto& item = hx.item;
  auto& frameGating = hx.frameGating;
  auto& rate = hx.rate;
  auto& watchdog = hx.watchdog;
  auto& inputRouter = hx.inputRouter;
  auto& sender = hx.sender;
  auto& encoder = hx.encoder;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& nowUs = tc.nowUs;
  auto& seq = tc.seq;
  // Switching screens is the same operation as switching to desktop mode, aimed at a particular
  // monitor. Done here rather than on the control thread because the capture item belongs to
  // this loop, exactly like the window and capture-mode selections above.
  if (const auto monitorReq = hx.mailbox.TakeSelectMonitor(hx.clientSession.epoch.load(std::memory_order_acquire))) {
    const uint32_t requestedId = monitorReq->monitorId;
    const auto monitors = enumerate_monitors();
    if (requestedId >= monitors.size()) {
      std::cerr << "[native-video-host][control] monitor-select out of range id=" << requestedId
                << " count=" << monitors.size() << "\n";
    } else {
      const auto& target = monitors[requestedId];
      auto nextItem = CreateItemForPrimaryMonitor(nullptr, nullptr, target.handle);
      if (!nextItem) {
        std::cerr << "[native-video-host][control] monitor-select capture failed id="
                  << requestedId << "\n";
      } else {
        item = nextItem;
        capture.selectedMonitorId.store(requestedId, std::memory_order_release);
        // A monitor is a desktop target, so any window selection it replaces has to go.
        capture.windowModeActive = false;
        capture.windowCriteria.processNamesLower.clear();
        capture.windowCriteria.titleNeedleLower.clear();
        capture.selectedWindowId.store(0u, std::memory_order_release);
        capture.targetFlags.store(0u, std::memory_order_release);
        capture.targetPid.store(0u, std::memory_order_release);
        capture.targetHwnd.store(0u, std::memory_order_release);
        {
          std::lock_guard<std::mutex> lk(capture.metaMu);
          capture.targetProcess = "monitor";
          capture.targetTitle = target.name;
        }
        watchdog.lastCaptureRestartUs = nowUs;
        if (restart_capture_session(hx)) {
          ++capture.restartCount;
          capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
          capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
          capture.lastCallbackUs.store(0, std::memory_order_release);
          encoder.ResetTimelineAnchors(capture);
          encoder.forceKeyNext = true;
          std::cout << "[native-video-host][control] monitor-select applied id=" << requestedId
                    << " " << target.width << "x" << target.height
                    << " at " << target.x << "," << target.y << "\n";
        } else {
          std::cerr << "[native-video-host][control] monitor-select restart failed id="
                    << requestedId << "\n";
        }
      }
    }
  }

  if (const auto modeReq = hx.mailbox.TakeCaptureMode(hx.clientSession.epoch.load(std::memory_order_acquire))) {
    const uint16_t reqMode = modeReq->mode;
    const uint32_t reqSeq = modeReq->seq;
    const uint32_t reqXPermille = std::min<uint32_t>(10000u, modeReq->xPermille);
    const uint32_t reqYPermille = std::min<uint32_t>(10000u, modeReq->yPermille);
    if (reqMode == 1) {
      auto nextItem = CreateItemForPrimaryMonitor(nullptr, "CreateForMonitor(control-overview)");
      if (!nextItem) {
        std::cerr << "[native-video-host][control] capture-mode overview failed seq=" << reqSeq << "\n";
      } else {
        item = nextItem;
        capture.windowModeActive = false;
        capture.windowCriteria.processNamesLower.clear();
        capture.windowCriteria.titleNeedleLower.clear();
        capture.selectedWindowId.store(0u, std::memory_order_release);
        capture.targetFlags.store(0u, std::memory_order_release);
        capture.targetPid.store(0u, std::memory_order_release);
        capture.targetHwnd.store(0u, std::memory_order_release);
        {
          std::lock_guard<std::mutex> lk(capture.metaMu);
          capture.targetProcess = "monitor";
          capture.targetTitle.clear();
        }
        watchdog.lastCaptureRestartUs = nowUs;
        if (restart_capture_session(hx)) {
          ++capture.restartCount;
          capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
          capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
          capture.lastCallbackUs.store(0, std::memory_order_release);
          encoder.ResetTimelineAnchors(capture);
          if (!encoder.ApplyCaptureUiQualityMode(capture, res, frameGating, inputRouter, sender, rate, useH264, true, nowUs)) {
            std::cerr << "[native-video-host][control] capture-mode overview quality apply failed seq=" << reqSeq
                      << "\n";
            return Flow::Break;
          }
          std::cout << "[native-video-host][control] capture-mode applied seq=" << reqSeq
                    << " mode=overview"
                    << " bitrate=" << encoder.activeBitrate
                    << " fps=" << encoder.activeFps
                    << " encode=" << encoder.activeEncodeW << "x" << encoder.activeEncodeH
                    << "\n";
        } else {
          std::cerr << "[native-video-host][control] capture-mode overview restart failed seq=" << reqSeq << "\n";
        }
      }
    } else if (reqMode == 2) {
      HMONITOR primaryMon = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
      MONITORINFO monInfo{};
      monInfo.cbSize = sizeof(monInfo);
      if (!GetMonitorInfo(primaryMon, &monInfo)) {
        monInfo.rcMonitor.left = 0;
        monInfo.rcMonitor.top = 0;
        monInfo.rcMonitor.right = GetSystemMetrics(SM_CXSCREEN);
        monInfo.rcMonitor.bottom = GetSystemMetrics(SM_CYSCREEN);
      }
      const int monW = std::max<int>(1, monInfo.rcMonitor.right - monInfo.rcMonitor.left);
      const int monH = std::max<int>(1, monInfo.rcMonitor.bottom - monInfo.rcMonitor.top);
      POINT focusPt{};
      focusPt.x = monInfo.rcMonitor.left +
                  static_cast<int>((static_cast<uint64_t>(reqXPermille) * static_cast<uint64_t>(monW - 1) + 5000ULL) /
                                   10000ULL);
      focusPt.y = monInfo.rcMonitor.top +
                  static_cast<int>((static_cast<uint64_t>(reqYPermille) * static_cast<uint64_t>(monH - 1) + 5000ULL) /
                                   10000ULL);
      CaptureWindowInfo selected{};
      if (!find_top_level_window_at_point(focusPt, &selected)) {
        std::cerr << "[native-video-host][control] capture-mode focus no-window seq=" << reqSeq
                  << " xPermille=" << reqXPermille
                  << " yPermille=" << reqYPermille
                  << "\n";
      } else {
        auto nextItem = CreateItemForPrimaryMonitor(selected.hwnd, "CreateForWindow(control-focus-point)");
        if (!nextItem) {
          std::cerr << "[native-video-host][control] capture-mode focus create-item failed seq=" << reqSeq << "\n";
        } else {
          item = nextItem;
          capture.windowModeActive = true;
          capture.windowClientOnlyActive = true;
          capture.windowCriteria.processNamesLower.clear();
          if (!selected.processName.empty()) {
            capture.windowCriteria.processNamesLower.insert(selected.processName);
          }
          capture.windowCriteria.titleNeedleLower.clear();
          capture.selectedWindowId.store(hwnd_to_id(selected.hwnd), std::memory_order_release);
          capture.targetFlags.store(0x1u | 0x2u, std::memory_order_release);
          capture.targetPid.store(selected.pid, std::memory_order_release);
          capture.targetHwnd.store(
              static_cast<uint64_t>(reinterpret_cast<uintptr_t>(selected.hwnd)), std::memory_order_release);
          {
            std::lock_guard<std::mutex> lk(capture.metaMu);
            capture.targetProcess = selected.processName.empty() ? "unknown" : selected.processName;
            capture.targetTitle = selected.title.empty() ? std::string{} : wide_to_utf8(selected.title);
          }
          nextCaptureWindowCheckUs = nowUs + captureWindowRebindIntervalUs;
          watchdog.lastCaptureRestartUs = nowUs;
          if (restart_capture_session(hx)) {
            ++capture.restartCount;
            capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
            capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
            capture.lastCallbackUs.store(0, std::memory_order_release);
            encoder.ResetTimelineAnchors(capture);
            if (!encoder.ApplyCaptureUiQualityMode(capture, res, frameGating, inputRouter, sender, rate, useH264, false, nowUs)) {
              std::cerr << "[native-video-host][control] capture-mode focus quality apply failed seq=" << reqSeq
                        << "\n";
              return Flow::Break;
            }
            std::cout << "[native-video-host][control] capture-mode applied seq=" << reqSeq
                      << " mode=focus-window"
                      << " pid=" << selected.pid
                      << " process=" << (selected.processName.empty() ? "unknown" : selected.processName)
                      << " title=" << (selected.title.empty() ? "<empty>" : wide_to_utf8(selected.title))
                      << " bitrate=" << encoder.activeBitrate
                      << " fps=" << encoder.activeFps
                      << " encode=" << encoder.activeEncodeW << "x" << encoder.activeEncodeH
                      << "\n";
          } else {
            std::cerr << "[native-video-host][control] capture-mode focus restart failed seq=" << reqSeq << "\n";
          }
        }
      }
    }
  }
  if (capture.windowModeActive && capture.windowCriteria.enabled() && nowUs >= nextCaptureWindowCheckUs) {
    nextCaptureWindowCheckUs = nowUs + captureWindowRebindIntervalUs;
    CaptureWindowInfo latestWindowInfo{};
    if (find_capture_window(capture.windowCriteria, &latestWindowInfo)) {
      const uintptr_t currentRaw = static_cast<uintptr_t>(capture.targetHwnd.load(std::memory_order_acquire));
      const uintptr_t nextRaw = reinterpret_cast<uintptr_t>(latestWindowInfo.hwnd);
      if (nextRaw != currentRaw) {
        const auto nextItem =
            CreateItemForPrimaryMonitor(latestWindowInfo.hwnd, "CreateForWindow(target-window-rebind)");
        if (nextItem) {
          item = nextItem;
          capture.selectedWindowId.store(hwnd_to_id(latestWindowInfo.hwnd), std::memory_order_release);
          capture.targetHwnd.store(static_cast<uint64_t>(nextRaw), std::memory_order_release);
          capture.targetPid.store(latestWindowInfo.pid, std::memory_order_release);
          {
            std::lock_guard<std::mutex> lk(capture.metaMu);
            capture.targetProcess =
                latestWindowInfo.processName.empty() ? std::string("unknown") : latestWindowInfo.processName;
            capture.targetTitle =
                latestWindowInfo.title.empty() ? std::string{} : wide_to_utf8(latestWindowInfo.title);
          }
          const uint32_t rebindCount = capture.rebindCount.fetch_add(1, std::memory_order_acq_rel) + 1;
          capture.targetFlags.store((capture.windowModeActive ? 0x1u : 0x0u) |
                                           ((capture.windowModeActive && capture.windowClientOnlyActive) ? 0x2u : 0x0u),
                                       std::memory_order_release);
          std::string targetProc = "unknown";
          std::string targetTitle;
          {
            std::lock_guard<std::mutex> lk(capture.metaMu);
            targetProc = capture.targetProcess;
            targetTitle = capture.targetTitle;
          }
          watchdog.lastCaptureRestartUs = nowUs;
          if (restart_capture_session(hx)) {
            ++capture.restartCount;
            capture.clockOffsetUs.store(std::numeric_limits<int64_t>::max(), std::memory_order_release);
            capture.lastCaptureUsForInterval.store(0, std::memory_order_release);
            capture.lastCallbackUs.store(0, std::memory_order_release);
            encoder.ResetTimelineAnchors(capture);
            encoder.forceKeyNext = true;
            std::cout << "[native-video-host] capture-window rebound hwnd=0x" << std::hex << nextRaw << std::dec
                      << " pid=" << capture.targetPid.load(std::memory_order_relaxed)
                      << " process=" << targetProc
                      << " title=" << (targetTitle.empty() ? "<empty>" : targetTitle)
                      << " rebindCount=" << rebindCount
                      << " restartCount=" << capture.restartCount
                      << "\n";
          } else {
            std::cerr << "[native-video-host] capture-window rebind restart failed\n";
          }
        }
      }
    }
  }
  return Flow::Next;
}

}  // namespace remote60::native_poc

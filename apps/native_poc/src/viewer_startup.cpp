// The viewer's startup sequence, one function per step, in the order main() calls them; each step
// is the corresponding block of the former main() verbatim, its locals now ViewerContext members.
// A step that can fail returns the exit code main() used to return there (0 = go on).
// (viewer split refactor Phase 2-10 / 3)

#include <windowsx.h>
#include "viewer_gdi_util.hpp"
#include "viewer_startup.hpp"
#include "bounded_process_exit.hpp"
#include "viewer_udp_session.hpp"

#include <iostream>
#include <vector>

#include "viewer_env_util.hpp"
#include "viewer_state.hpp"
#include "viewer_input_forward.hpp"
#include "viewer_key_chord.hpp"
#include "viewer_picker.hpp"
#include "viewer_log.hpp"
#include "viewer_window_proc.hpp"

namespace remote60::native_poc::viewer {

void apply_latency_priority() {
  // Decoder/present deadlines should not lose their timeslice to ordinary background work.
  // Keep this reversible for diagnostics and battery-sensitive deployments.
  if (!env_truthy("REMOTE60_NATIVE_NORMAL_PRIORITY")) {
    const BOOL processPriorityOk =
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    const BOOL threadPriorityOk =
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    std::cout << "[native-video-client] latency-priority processAboveNormal="
              << (processPriorityOk ? 1 : 0)
              << " mainThreadAboveNormal=" << (threadPriorityOk ? 1 : 0) << "\n";
  }
}

void apply_dpi_awareness() {
  // Without this the OS bitmap-stretches the whole window on a scaled display, which blurs
  // both the panel text and the decoded video. Must run before any window is created.
  if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
  }
}

void load_config(ViewerContext& ctx, int argc, char** argv) {
  ctx.args = parse_args(argc, argv);
  ctx.present.traceEvery = ctx.args.traceEvery;
  ctx.present.traceMax = ctx.args.traceMax;
  ctx.present.presentFrameIntervalUs = static_cast<uint32_t>(std::max<uint64_t>(
      1ULL, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, ctx.args.fpsHint))));
  // Paced playout (F-11 / P3), opt-in. The clock is seeded from the fps hint and re-measures the
  // sender's real cadence from capture timestamps as frames arrive.
  ctx.frameBuf.pacedPlayout = env_truthy("REMOTE60_NATIVE_PACED_PLAYOUT");
  ctx.frameBuf.playout.SetTargetFrameIntervalUs(ctx.present.presentFrameIntervalUs);
  if (ctx.frameBuf.pacedPlayout) {
    std::cout << "[native-video-client] paced playout enabled targetUs=" << ctx.present.presentFrameIntervalUs
              << " leadUs=" << VideoPlayoutClock::LeadForStepUs(ctx.present.presentFrameIntervalUs) << "\n";
  }
  const uint64_t keyframeReqMinIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYFRAME_REQ_MIN_INTERVAL_US",
      static_cast<uint32_t>(kKeyframeRequestMinIntervalUsDefault), 10000, 1000000);
  const uint64_t keyframeReqTokenRefillUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYFRAME_REQ_TOKEN_REFILL_US",
      static_cast<uint32_t>(kKeyframeRequestTokenRefillUsDefault), 10000, 2000000);
  const uint32_t keyframeReqTokenCapacity = env_u32_clamped(
      "REMOTE60_NATIVE_KEYFRAME_REQ_TOKEN_CAPACITY",
      kKeyframeRequestTokenCapacityDefault, 1, 16);
  ctx.control.keyframeRequests.Configure(keyframeReqMinIntervalUs, keyframeReqTokenRefillUs, keyframeReqTokenCapacity);
  ctx.gate.catchupReenterMinIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_CATCHUP_REENTER_MIN_INTERVAL_US",
      static_cast<uint32_t>(kCatchupReenterMinIntervalUsDefault), 100000, 3000000);
  ctx.gate.staleCaptureDropUs = env_u32_clamped(
      "REMOTE60_NATIVE_STALE_CAPTURE_DROP_US",
      static_cast<uint32_t>(kStaleCaptureDropUs), 1000, 2000000);
  ctx.gate.staleReferenceRecoveryMinIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_STALE_RECOVERY_MIN_INTERVAL_US",
      static_cast<uint32_t>(kStaleRecoveryMinIntervalUsDefault), 100000, 5000000);
  ctx.gate.congestionRecoverMinUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_RECOVER_MIN_US",
      static_cast<uint32_t>(kCongestionRecoverMinUsDefault), 50000, 5000000);
  ctx.gate.congestionRecoveryTimeoutUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_RECOVERY_TIMEOUT_US",
      static_cast<uint32_t>(kCongestionRecoveryTimeoutUsDefault), 100000, 10000000);
  // Congestion-entry thresholds (F-18). Tunable so a machine where local CPU contention alone
  // trips the trigger can be measured and adjusted without a rebuild.
  ctx.gate.decodeQueueLagDropUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_DECODE_QUEUE_LAG_US",
      static_cast<uint32_t>(kDecodeQueueLagDropUs), 50000, 5000000);
  ctx.gate.catchupLagDropUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_STREAM_LAG_US",
      static_cast<uint32_t>(kCatchupLagDropUs), 50000, 5000000);
  ctx.gate.denseArrivalMaxGapUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_DENSE_ARRIVAL_US",
      static_cast<uint32_t>(kDenseArrivalMaxGapUsDefault), 10000, 2000000);
  ctx.gate.lagTriggerStreakMin = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_TRIGGER_STREAK", kLagTriggerStreakMinDefault, 1, 60);
  // Time-based keyframe recovery (FrameGate::tick): 0 turns the timer off.
  ctx.gate.recoveryRetryIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEY_RECOVERY_RETRY_US",
      static_cast<uint32_t>(kKeyRecoveryRetryUsDefault), 0, 10000000);
  ctx.gate.recoveryRetryMaxIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEY_RECOVERY_RETRY_MAX_US",
      static_cast<uint32_t>(kKeyRecoveryRetryMaxUsDefault), 100000, 30000000);
  ctx.gate.recoveryRetryDeferMaxUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEY_RECOVERY_DEFER_MAX_US",
      static_cast<uint32_t>(kKeyRecoveryDeferMaxUsDefault), 0, 5000000);
  ctx.udpSimDropPm = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_SIM_DROP_PM", 0, 0, 1000);
  ctx.udpSimDropSeed = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_SIM_DROP_SEED", 0, 0, 0x7fffffffu);
  ctx.videoNackEnabled =
      env_u32_clamped("REMOTE60_NATIVE_VIDEO_NACK", kVideoNackEnabledDefault ? 1u : 0u, 0, 1) != 0;
  ctx.session.deadSessionUs =
      env_u32_clamped("REMOTE60_NATIVE_DEAD_SESSION_MS", 5000, 1000, 600000) * 1000u;
  ctx.session.deadSessionExit = env_u32_clamped("REMOTE60_NATIVE_DEAD_SESSION_EXIT", 1, 0, 1) != 0;
  ctx.videoNackHoldUs = env_u32_clamped("REMOTE60_NATIVE_VIDEO_NACK_HOLD_US", 120000, 0, 2000000);
  ctx.udpRecvTimeoutMs = env_u32_clamped("REMOTE60_NATIVE_UDP_RECV_TIMEOUT_MS", 25, 1, 1000);
  ctx.control.keyframeRequests.Reset();
}

int validate_codec_transport(ViewerContext& ctx) {
  ctx.dec.useRaw = (ctx.args.codec == "raw");
  ctx.dec.useH264 = (ctx.args.codec == "h264");
  const bool encodedExperimentEnabled =
      (REMOTE60_NATIVE_ENCODED_EXPERIMENT != 0) || env_truthy("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE");
  if (!ctx.dec.useRaw && !ctx.dec.useH264) {
    std::cerr << "[native-video-client] unsupported codec: " << ctx.args.codec << " (supported: raw,h264)\n";
    return 10;
  }
  if (ctx.dec.useH264 && !encodedExperimentEnabled) {
    std::cerr << "[native-video-client] unsupported codec: " << ctx.args.codec
              << " (enable REMOTE60_NATIVE_ENCODED_EXPERIMENT or set env REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1)\n";
    return 10;
  }
  std::string effectiveTransport = ctx.args.transport;
  if (effectiveTransport.empty()) {
    effectiveTransport = ctx.dec.useH264 ? "udp" : "tcp";
  }
  if (!parse_video_transport(effectiveTransport, &ctx.dec.transport)) {
    std::cerr << "[native-video-client] unsupported transport: " << effectiveTransport << " (supported: tcp,udp)\n";
    return 12;
  }
  if (ctx.dec.transport == VideoTransport::Udp && ctx.dec.useRaw) {
    std::cerr << "[native-video-client] raw codec over udp is not supported in current phase (use codec=h264)\n";
    return 13;
  }
  return 0;
}

void apply_initial_state(ViewerContext& ctx) {
  ctx.control.runtimeTune.Reset(ctx.args.runtimeBitrate, ctx.args.runtimeKeyint, ctx.args.runtimeFps);
  ctx.session.requestedMonitorId = ctx.args.monitorId;
  ctx.control.connected.store(false, std::memory_order_relaxed);
  // How the session opens. The explicit flag wins; with no flag we fall back to the legacy env
  // var so the automation probes (which all set REMOTE60_NATIVE_START_STREAM_VIEW=1) are
  // unaffected. "targets" is the product flow: open on the picker and stream only after a pick.
  if (ctx.args.initialView == "targets" || ctx.args.initialView == "picker") {
    ctx.startInStreamView = false;
  } else if (ctx.args.initialView == "stream") {
    ctx.startInStreamView = true;
  } else {
    ctx.startInStreamView = env_truthy("REMOTE60_NATIVE_START_STREAM_VIEW");
  }
  ctx.startInPicker = !ctx.startInStreamView;
  ctx.picker.visible.store(ctx.startInPicker, std::memory_order_relaxed);
  clear_pc_target_selection(ctx);
  // No target has taken effect yet. 0 disables the persistent generation filter, so the legacy
  // stream-view start and the pre-first-pick window accept whatever the host sends, as before.
  ctx.sel.activeStreamGeneration.store(0, std::memory_order_release);
  ctx.sel.revealPosted.store(false, std::memory_order_release);
  // Picker-first sessions must not keep the host's default stream running under the picker: the
  // request rides the scheduler (StreamState before WindowList/Select) and is queued before the
  // control link exists, so it goes out first thing once connected. An initial default-desktop
  // frame that slips through before the stream stops is dropped by the receive-path gate rather
  // than painted, and no flip swap chain is created until the user's pick produces a real frame.
  if (ctx.startInPicker) {
    ctx.control.streamState.Request(false);
  }
  ctx.control.captureModeRequests.Reset();
  ctx.picker.windowPanel.Reset();
  ctx.input.suppressMouseUntilUs.store(0, std::memory_order_relaxed);
  ctx.input.activeTouchPointerId.store(0, std::memory_order_relaxed);
  ctx.input.activeTouchDown.store(false, std::memory_order_relaxed);
}

/**
 * Sends a modifier+key chord to the host as ordinary input events.
 *
 * The host turns these into real keystrokes when its target has focus, which is what makes Win+D
 * and Alt+Tab act on the remote desktop. Queued through the same path as typing, so a chord cannot
 * overtake the keys around it.
 *
 * Worth knowing where this does nothing: when the host is capturing a single WINDOW that does not
 * hold focus, it posts the keys to that window instead of injecting them, and neither the shell nor
 * the task switcher reads posted messages. These buttons are for desktop capture.
 */
void send_host_key_chord(ViewerState& ctx, uint32_t modifierVk, uint32_t keyVk, const char* what) {
  if (!ctx.session.inputEnabled.load(std::memory_order_relaxed)) {
    log_client_line(ctx, std::string("[toolbar] ") + what + " ignored: the input channel is off");
    return;
  }
  for (const HostKeyStep& step : host_key_chord(modifierVk, keyVk)) {
    enqueue_input_event(ctx, step.kind, 0, 0, 0, step.vk);
  }
  log_client_line(ctx, std::string("[toolbar] ") + what + " sent to the host");
}

int create_window_and_toolbar(ViewerContext& ctx) {
  if (!create_window(ctx)) {
    std::cerr << "[native-video-client] window create failed\n";
    return 2;
  }

  {
    remote60::native_poc::SessionToolbarCallbacks toolbarCallbacks;
    // Re-enabled: the reason this was unset -- entering the picker mid-session looked like a
    // freeze -- is fixed. Two separate things had to be true. The picker no longer stops the
    // stream, which was done long ago. And its repaint actually reaches the screen, which this
    // comment claimed before it was true: releasing the swapchain does not give a flip-model
    // window back to GDI, so the picker was drawn and never seen until it was presented through
    // the swapchain instead (viewer_present.cpp present_picker_frame, history #532).
    // With the invisible legacy top-left buttons removed, this is the ONLY road back to target
    // selection during a session, so it must exist.
    // The callbacks outlive nothing: the toolbar is destroyed in WM_DESTROY, long before ctx.
    toolbarCallbacks.onLog = [&ctx](const std::string& line) { log_client_line(ctx, line); };
    toolbarCallbacks.onTargets = [&ctx] {
      set_picker_visible_and_sync_stream(ctx, true);
      push_session_toolbar_state(ctx);
      if (ctx.session.hwnd) InvalidateRect(ctx.session.hwnd, nullptr, FALSE);
    };
    toolbarCallbacks.onMacro = [&ctx] {
      toggle_macro_window(ctx, ctx.session.hwnd);
      push_session_toolbar_state(ctx);
    };
    // Win+D and Alt+Tab never survive being typed: the viewer's own Windows acts on them here, so
    // the remote machine never hears about it. Sent as explicit key events instead, which is the
    // only way to aim them at the other end.
    toolbarCallbacks.onShowDesktop = [&ctx] {
      send_host_key_chord(ctx, VK_LWIN, 'D', "show-desktop");
    };
    toolbarCallbacks.onSwitchWindow = [&ctx] {
      send_host_key_chord(ctx, VK_LMENU, VK_TAB, "switch-window");
    };
    toolbarCallbacks.onMonitor = [&ctx](uint32_t monitorId) {
      ctx.picker.windowPanel.RequestMonitorSelect(monitorId);
    };
    remote60::native_poc::session_toolbar_create(ctx.session.hwnd, std::move(toolbarCallbacks));
    remote60::native_poc::session_toolbar_set_visible(ctx.startInStreamView);
    push_session_toolbar_state(ctx);
  }
  return 0;
}

int init_decoder(ViewerContext& ctx) {
  ctx.gate.waitForKeyFrame = ctx.dec.useH264;
  if (ctx.dec.useH264) {
    const HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
      std::cerr << "[native-video-client] MFStartup failed hr=0x" << std::hex << static_cast<unsigned long>(hr)
                << std::dec << "\n";
      return 11;
    }
    ctx.dec.mfStarted = true;
    // Supplying AMD's decoder with an external DXGI device manager can enter atidxx64's
    // direct-surface path even when the caller later reads a CPU buffer. Keep the proven
    // system-memory decoder path as the safe default; the zero-copy experiment is an
    // explicit opt-in because affected drivers can TDR or access-violate in that path.
    const bool enableDxgiDecodeSurface =
        env_truthy("REMOTE60_NATIVE_DXGI_DECODE_SURFACE") &&
        !env_truthy("REMOTE60_NATIVE_DISABLE_DXGI_DECODE_SURFACE");
    if (enableDxgiDecodeSurface) {
      // Decode and paint share one D3D11 device so an opt-in hardware-decoder NV12 surface
      // can be sampled directly without a GPU->CPU copy and CPU->GPU upload.
      if (!ctx.ui.nv12Renderer.ready) (void)ctx.ui.nv12Renderer.init(ctx.session.hwnd);
      if (ctx.ui.nv12Renderer.ready) {
        ctx.dec.d3dDevice = ctx.ui.nv12Renderer.device;
        ctx.dec.d3dContext = ctx.ui.nv12Renderer.context;
        (void)ctx.dec.decoder.set_d3d11_device(ctx.dec.d3dDevice.Get());
      } else {
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        const HRESULT d3dHr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                                D3D11_SDK_VERSION, &ctx.dec.d3dDevice, &fl,
                                                &ctx.dec.d3dContext);
        if (SUCCEEDED(d3dHr) && ctx.dec.d3dDevice) {
          (void)ctx.dec.decoder.set_d3d11_device(ctx.dec.d3dDevice.Get());
        }
      }
    }
  }
  return 0;
}

int open_media_socket(ViewerContext& ctx) {
  // Reaching the host through the directory replaces the address entirely: the socket comes back
  // already prepared (it is the one the directory observed, so the host is punching towards it)
  // and the capability that follows is what the host authorises the session against.
  ctx.resolvedArgs = ctx.args;
  if (!ctx.args.directoryUrl.empty()) {
    if (ctx.dec.transport != VideoTransport::Udp) {
      std::cerr << "[native-video-client] the directory path is udp only\n";
      if (ctx.dec.mfStarted) MFShutdown();
      return 3;
    }
    std::string directoryError;
    std::string sessionToken = ctx.args.directorySession;
    // Where the directory says observations go. It rides on the login response, so the branch
    // that reuses a cached session never sees it -- and on an https directory that would mean
    // refusing to observe, so a reconnect would fail where a fresh connect succeeds. The health
    // probe carries the same value and needs no session, so that path asks for it there.
    remote60::native_poc::directory::ObserveEndpoint advertised;
    if (sessionToken.empty()) {
      if (!remote60::native_poc::directory_login(ctx.args.directoryUrl, ctx.args.directoryAccount,
                                                 ctx.args.directoryPassword, &sessionToken,
                                                 &directoryError, &advertised)) {
        std::cerr << "[native-video-client] directory login failed: " << directoryError << "\n";
        if (ctx.dec.mfStarted) MFShutdown();
        return 3;
      }
    } else {
      // Absence is not an error: an older directory says nothing, and observe_port_for has a
      // rule for exactly that.
      std::string ignored;
      remote60::native_poc::directory_observe_from_health(ctx.args.directoryUrl, &advertised,
                                                          &ignored);
    }

    std::string hostId = ctx.args.directoryHostId;
    if (hostId.empty()) {
      std::vector<remote60::native_poc::DirectoryHostEntry> hosts;
      if (!remote60::native_poc::directory_list_hosts(ctx.args.directoryUrl, sessionToken, &hosts,
                                                      &directoryError)) {
        std::cerr << "[native-video-client] directory hosts failed: " << directoryError << "\n";
        if (ctx.dec.mfStarted) MFShutdown();
        return 3;
      }
      for (const auto& entry : hosts) {
        if (!ctx.args.directoryHostName.empty() && entry.hostName != ctx.args.directoryHostName) continue;
        // An offline host has no mapping to punch towards, so preferring an online one avoids a
        // four-second wait that was never going to succeed.
        if (hostId.empty() || entry.online) hostId = entry.hostId;
        if (entry.online) break;
      }
      if (hostId.empty()) {
        std::cerr << "[native-video-client] no host on this account"
                  << (ctx.args.directoryHostName.empty() ? "" : " named " + ctx.args.directoryHostName)
                  << "\n";
        if (ctx.dec.mfStarted) MFShutdown();
        return 3;
      }
    }

    remote60::native_poc::DirectorySessionRequest request{};
    request.url = ctx.args.directoryUrl;
    request.sessionToken = sessionToken;
    request.hostId = hostId;
    request.advertised = advertised;
    remote60::native_poc::DirectorySessionResult session{};
    if (!remote60::native_poc::directory_session_open(request, &session, &directoryError)) {
      std::cerr << "[native-video-client] directory connect failed: " << directoryError << "\n";
      if (ctx.dec.mfStarted) MFShutdown();
      return 3;
    }
    ctx.session.sock = session.socket;
    ctx.resolvedArgs.host = session.chosen.ip;
    ctx.resolvedArgs.port = session.chosen.port;
    // Control travels over the media socket on this path; a separate TCP port cannot survive
    // hole punching. Everything downstream reads resolvedArgs.controlPort, so this is what
    // actually routes control -- it used to be a write nobody read while the tunnel / TCP
    // branches consulted args.controlPort instead (harmless only because the shell never passed
    // --control-port on the directory path). (F-19.)
    ctx.resolvedArgs.controlPort = 0;
    ctx.directoryPunchToken = session.punchToken;
    ctx.session.relayPath.store(session.relay, std::memory_order_relaxed);
    // The directory answered, so this is a measurement now rather than a default.
    ctx.session.relayPathKnown.store(true, std::memory_order_relaxed);
    push_session_toolbar_state(ctx);
    std::cout << "[native-video-client] directory chose " << session.chosen.ip << ":"
              << session.chosen.port << " ("
              << remote60::native_poc::candidate_kind_name(session.chosen.kind) << ")"
              << (session.answered ? "" : " [no answer, trying anyway]") << "\n";
  } else {
    ctx.session.sock = socket(AF_INET,
                   (ctx.dec.transport == VideoTransport::Udp) ? SOCK_DGRAM : SOCK_STREAM,
                   (ctx.dec.transport == VideoTransport::Udp) ? IPPROTO_UDP : IPPROTO_TCP);
    // An address given on the command line is dialled directly by construction -- there is no
    // relay in this path -- so here "direct" is known rather than assumed.
    ctx.session.relayPath.store(false, std::memory_order_relaxed);
    ctx.session.relayPathKnown.store(true, std::memory_order_relaxed);
  }
  if (ctx.session.sock == INVALID_SOCKET) {
    std::cerr << "[native-video-client] socket create failed\n";
    if (ctx.dec.mfStarted) MFShutdown();
    return 3;
  }
  return 0;
}

namespace {

// Laid out from the client rect each paint, so the buttons are where they are drawn at any size
// and DPI. Two rects and a message; nothing here is a control in the Win32 sense.
struct FailureLayout {
  RECT text{};
  RECT retry{};
  RECT close{};
};

FailureLayout failure_layout(HWND hwnd) {
  RECT client{};
  GetClientRect(hwnd, &client);
  const int w = client.right - client.left;
  const int h = client.bottom - client.top;
  const int bw = 150;
  const int bh = 40;
  const int gap = 14;
  FailureLayout out;
  out.text = RECT{client.left, client.top + h / 2 - 70, client.right, client.top + h / 2 - 10};
  const int y = client.top + h / 2 + 20;
  out.retry = RECT{client.left + w / 2 - bw - gap / 2, y, client.left + w / 2 - gap / 2, y + bh};
  out.close = RECT{client.left + w / 2 + gap / 2, y, client.left + w / 2 + bw + gap / 2, y + bh};
  return out;
}

bool point_in(const RECT& r, int x, int y) {
  return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

}  // namespace

bool show_startup_failure(ViewerContext& ctx, const std::string& reason) {
  HWND hwnd = ctx.session.hwnd;
  if (!hwnd) return false;   // nothing to show it in; the caller falls through to its exit code

  bool retry = false;
  bool done = false;

  // Painted directly rather than through the viewer's present path: there is no stream, no
  // swapchain and no decoder at this point, and routing through them to draw two rectangles
  // would tie a failure screen to the machinery that failed.
  auto paint = [&](HDC hdc) {
    const FailureLayout layout = failure_layout(hwnd);
    RECT client{};
    GetClientRect(hwnd, &client);
    HBRUSH back = CreateSolidBrush(RGB(15, 19, 25));
    FillRect(hdc, &client, back);
    DeleteObject(back);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(232, 234, 237));
    RECT text = layout.text;
    // The product's own UTF-8 drawing, so this screen renders Korean the same way every other
    // screen does rather than growing its own conversion.
    draw_text_utf8(ctx, hdc, reason, &text, DT_CENTER | DT_WORDBREAK | DT_EDITCONTROL);

    auto button = [&](const RECT& r, const wchar_t* label, bool primary) {
      HBRUSH fill = CreateSolidBrush(primary ? RGB(59, 130, 246) : RGB(38, 46, 59));
      FillRect(hdc, &r, fill);
      DeleteObject(fill);
      SetTextColor(hdc, primary ? RGB(255, 255, 255) : RGB(200, 206, 216));
      RECT t = r;
      DrawTextW(hdc, label, -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };
    button(layout.retry, L"다시 시도", true);
    button(layout.close, L"닫기", false);
  };

  // A loop of its own rather than a flag checked by the main pump: the main pump has not started
  // and the receiver threads do not exist yet.
  InvalidateRect(hwnd, nullptr, TRUE);
  MSG msg;
  while (!done) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        done = true;
        break;
      }
      if (msg.hwnd == hwnd && msg.message == WM_LBUTTONUP) {
        const FailureLayout layout = failure_layout(hwnd);
        const int x = GET_X_LPARAM(msg.lParam);
        const int y = GET_Y_LPARAM(msg.lParam);
        // Only when the user presses it. An automatic retry would overwrite the log line that
        // says what went wrong, which is the one thing worth keeping here.
        if (point_in(layout.retry, x, y)) {
          retry = true;
          done = true;
          break;
        }
        if (point_in(layout.close, x, y)) {
          done = true;
          break;
        }
      }
      if (msg.hwnd == hwnd && msg.message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint(hdc);
        EndPaint(hwnd, &ps);
        continue;
      }
      if (msg.hwnd == hwnd && (msg.message == WM_CLOSE || msg.message == WM_DESTROY)) {
        done = true;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (done) break;
    // The window proc may have swallowed WM_PAINT; draw straight to the DC so the screen is not
    // left blank while we wait.
    HDC hdc = GetDC(hwnd);
    if (hdc) {
      paint(hdc);
      ReleaseDC(hwnd, hdc);
    }
    Sleep(30);
  }
  return retry;
}

int connect_media_socket(ViewerContext& ctx) {
  if (ctx.dec.transport == VideoTransport::Tcp) {
    int noDelay = 1;
    setsockopt(ctx.session.sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
  }
  if (ctx.dec.transport == VideoTransport::Udp) {
    if (ctx.args.tcpRecvBufKb == 0) {
      const int recvBuf = 1024 * 1024;
      (void)setsockopt(ctx.session.sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
    }
    if (ctx.args.tcpSendBufKb == 0) {
      const int sendBuf = 256 * 1024;
      (void)setsockopt(ctx.session.sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
    }
  }
  if (ctx.args.tcpRecvBufKb > 0) {
    const int recvBuf = static_cast<int>(ctx.args.tcpRecvBufKb * 1024u);
    setsockopt(ctx.session.sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
  }
  if (ctx.args.tcpSendBufKb > 0) {
    const int sendBuf = static_cast<int>(ctx.args.tcpSendBufKb * 1024u);
    setsockopt(ctx.session.sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(ctx.resolvedArgs.port);
  if (inet_pton(AF_INET, ctx.resolvedArgs.host.c_str(), &addr.sin_addr) != 1) {
    std::cerr << "[native-video-client] invalid host " << ctx.resolvedArgs.host << "\n";
    closesocket(ctx.session.sock);
    ctx.session.sock = INVALID_SOCKET;
    if (ctx.dec.mfStarted) MFShutdown();
    return 4;
  }
  if (connect(ctx.session.sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "[native-video-client] connect failed " << ctx.resolvedArgs.host << ":" << ctx.resolvedArgs.port << "\n";
    closesocket(ctx.session.sock);
    ctx.session.sock = INVALID_SOCKET;
    if (ctx.dec.mfStarted) MFShutdown();
    return 5;
  }
  if (ctx.dec.transport == VideoTransport::Udp) {
    // The same exchange the Android session performs (F-09), at this viewer's old cadence: up to
    // ten seconds of Hello, a 200 ms wait each, a 50 ms pause between tries. The capability from
    // /api/connect rides in the packet; without it the host treats this as a plain LAN client and
    // refuses anything that needs authorisation -- secure-desktop input in particular. A token that
    // was sent now also has to come back acknowledged (kUdpFeatureDirectoryAuth), which the mobile
    // client always required and this path used to skip.
    // The Hello (with the NACK request), the ack bits kept, the receive timeout armed: the three
    // pieces the receive-path integration test runs through the same functions
    // (viewer_udp_session.hpp), so a regression here fails that test.
    const remote60::native_poc::UdpHelloOptions hello =
        viewer_udp_hello_options(ctx.directoryPunchToken, ctx.videoNackEnabled);
    uint32_t ackFeatures = 0;
    std::string helloError;
    const bool handshakeOk = remote60::native_poc::udp_hello_handshake(
        ctx.session.sock, hello, nullptr, &helloError, &ackFeatures);
    // A short receive timeout from here on, on BOTH the direct and the tunnelled path. It is the
    // clock of everything the recv thread does on a quiet link -- NACK rounds, the in-order hold,
    // the keyframe recovery deadline, the control tunnel's retransmits -- and the direct path used
    // to block forever, which left a lost chunk on a static screen unrepaired until the next frame.
    (void)viewer_arm_udp_recv_timeout(ctx.session.sock, ctx.udpRecvTimeoutMs);
    if (!handshakeOk) {
      std::cerr << "[native-video-client] udp handshake failed " << ctx.resolvedArgs.host << ":"
                << ctx.resolvedArgs.port << " (" << helloError << ")\n";
      closesocket(ctx.session.sock);
      ctx.session.sock = INVALID_SOCKET;
      if (ctx.dec.mfStarted) MFShutdown();
      return 6;
    }
    viewer_apply_udp_hello_ack(ackFeatures, ctx.videoNackEnabled, ctx.session);
    std::cout << "[native-video-client] udp hello ack features=0x" << std::hex << ackFeatures
              << std::dec << " nackRequested=" << (ctx.videoNackEnabled ? 1 : 0)
              << " nackNegotiated=" << (ctx.session.hostSupportsNack ? 1 : 0)
              << " nackHoldUs=" << ctx.videoNackHoldUs
              << " recvTimeoutMs=" << ctx.udpRecvTimeoutMs << "\n";
  }
  return 0;
}

void attach_control_tunnel_and_log(ViewerContext& ctx) {
  // No second port to dial means the directory path: control tunnels through the socket the
  // punch just opened. The send is bare because the socket is connected -- the same socket the
  // receive loop below reads, which is what makes the two directions one NAT mapping.
  if (ctx.resolvedArgs.controlPort == 0 && ctx.dec.transport == VideoTransport::Udp && ctx.session.sock != INVALID_SOCKET) {
    ctx.control.udpControl.Configure(
        [&ctx](const void* data, size_t len) -> bool {  // the channel lives in ctx itself
          return send(ctx.session.sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        remote60::native_poc::kUdpControlStreamClientToHost,
        remote60::native_poc::kUdpControlStreamHostToClient, ctx.args.udpMtu);
    ctx.control.overUdp.store(true, std::memory_order_release);
    // The receive timeout that lets the tick above run on a quiet link is set once for every UDP
    // session in connect_media_socket (it used to be set here, for the tunnel only).
    std::cout << "[native-video-client] control tunnelled over the media socket\n";
  }

  std::cout << "[native-video-client] connected host=" << ctx.resolvedArgs.host
            << " port=" << ctx.resolvedArgs.port
            << " transport=" << video_transport_name(ctx.dec.transport)
            << " codec=" << ctx.args.codec
            << " seconds=" << ctx.args.seconds << "\n";
  std::cout << "[native-video-client] keyframe-request-limiter minIntervalUs="
            << ctx.control.keyframeRequests.min_interval_us()
            << " tokenRefillUs=" << ctx.control.keyframeRequests.token_refill_us()
            << " tokenCapacity=" << ctx.control.keyframeRequests.token_capacity()
            << " catchupReenterMinUs=" << ctx.gate.catchupReenterMinIntervalUs
            << " staleCaptureDropUs=" << ctx.gate.staleCaptureDropUs
            << " congestionRecoverMinUs=" << ctx.gate.congestionRecoverMinUs
            << " congestionRecoveryTimeoutUs=" << ctx.gate.congestionRecoveryTimeoutUs
            << " keyRecoveryRetryUs=" << ctx.gate.recoveryRetryIntervalUs
            << " keyRecoveryRetryMaxUs=" << ctx.gate.recoveryRetryMaxIntervalUs
            << "\n";
  if (kInputPolicyForceBlock) {
    std::cout << "[native-video-client] input channel blocked by compile-time policy\n";
  }
  int effectiveRecvBuf = 0;
  int effectiveRecvBufLen = sizeof(effectiveRecvBuf);
  (void)getsockopt(ctx.session.sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&effectiveRecvBuf), &effectiveRecvBufLen);
  int effectiveSendBuf = 0;
  int effectiveSendBufLen = sizeof(effectiveSendBuf);
  (void)getsockopt(ctx.session.sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&effectiveSendBuf), &effectiveSendBufLen);
  std::cout << "[native-video-client] socket rcvbuf=" << effectiveRecvBuf
            << " sndbuf=" << effectiveSendBuf << " bytes\n";
}

void connect_control(ViewerContext& ctx) {
  ctx.session.controlRequired = ctx.control.overUdp.load() || ctx.resolvedArgs.controlPort > 0;
  ctx.controlClient.emplace(ctx, ctx.args, ctx.startInPicker);
  // Two ways to reach the host's control protocol, and the session only ever has one of them.
  // A direct host answers on its own TCP port; a host behind NAT is reachable solely through
  // the punched media socket, and dialling a second port there connects to nothing.
  ctx.controlReady = ctx.control.overUdp.load(std::memory_order_acquire);
  if (ctx.resolvedArgs.controlPort > 0) {
    ctx.controlSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ctx.controlSock != INVALID_SOCKET) {
      int ctlNoDelay = 1;
      setsockopt(ctx.controlSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&ctlNoDelay), sizeof(ctlNoDelay));
      sockaddr_in ctlAddr{};
      ctlAddr.sin_family = AF_INET;
      ctlAddr.sin_port = htons(ctx.resolvedArgs.controlPort);
      if (inet_pton(AF_INET, ctx.args.host.c_str(), &ctlAddr.sin_addr) == 1 &&
          connect(ctx.controlSock, reinterpret_cast<const sockaddr*>(&ctlAddr), sizeof(ctlAddr)) == 0) {
        ctx.controlReady = true;
      }
    }
  }
  {
    const bool inputChannelEnabled =
        ctx.controlReady && ctx.args.enableInputChannel && !kInputPolicyForceBlock;
    ctx.session.inputEnabled = inputChannelEnabled;
    if (inputChannelEnabled) {
      // Clear any modifier the host is still holding from a previous session. A client that
      // lost focus while a modifier was down could not send its up, and that up-less state is
      // the host's real key state -- it survives the client closing and reopening, so
      // reconnecting is the only way to shake it loose, and only if the fresh client says so.
      for (const uint32_t vk : {static_cast<uint32_t>(VK_CONTROL), static_cast<uint32_t>(VK_LCONTROL),
                                static_cast<uint32_t>(VK_RCONTROL), static_cast<uint32_t>(VK_MENU),
                                static_cast<uint32_t>(VK_LMENU), static_cast<uint32_t>(VK_RMENU),
                                static_cast<uint32_t>(VK_SHIFT), static_cast<uint32_t>(VK_LSHIFT),
                                static_cast<uint32_t>(VK_RSHIFT), static_cast<uint32_t>(VK_LWIN),
                                static_cast<uint32_t>(VK_RWIN)}) {
        enqueue_input_event(ctx, 6, 0, 0, 0, vk);
      }
    }
    ctx.control.scheduler.Reset(ctx.args.controlIntervalMs, qpc_now_us());
    if (ctx.controlReady) {
      ctx.controlClient->controlSock = ctx.controlSock;
      // Published BEFORE the thread starts. Run() stores false on its first failed exchange, so
      // storing true after the spawn could land on top of that and leave "connected" stale for
      // the rest of the session. The window was milliseconds wide; it is now zero. (F-06.)
      ctx.control.connected.store(true, std::memory_order_relaxed);
      ctx.controlThread = std::thread([&ctx]() {
        try { ctx.controlClient->Run(); } catch (...) {
          ctx.control.connected.store(false);
          ctx.session.recoveryExitCode.store(43); ctx.session.running.store(false);
          PostMessageW(ctx.session.hwnd, WM_CLOSE, 0, 0);
        }
      });
    }
    if (ctx.controlReady) {
      ctx.control.runtimeTune.SetEnabled(ctx.dec.useH264);
      queue_window_list_request(ctx, "window_list_request pending");
      if (ctx.dec.useH264 && (ctx.args.runtimeBitrate > 0 || ctx.args.runtimeKeyint > 0)) {
        ctx.control.runtimeTune.MarkDirty();
      }
      std::cout << "[native-video-client] control connected transport="
                << (ctx.control.overUdp.load(std::memory_order_acquire) ? "udp-tunnel" : "tcp")
                << " port=" << ctx.resolvedArgs.controlPort
                << " inputChannel=" << (inputChannelEnabled ? 1 : 0) << "\n";
    } else {
      if (ctx.controlSock != INVALID_SOCKET) {
        closesocket(ctx.controlSock);
        ctx.controlSock = INVALID_SOCKET;
      }
      ctx.control.connected.store(false, std::memory_order_relaxed);
      ctx.control.runtimeTune.SetEnabled(false);
      set_window_panel_status(ctx, "control_connect_failed");
      std::cout << "[native-video-client] control unavailable port=" << ctx.resolvedArgs.controlPort << "\n";
    }
  }
}

void start_receiver(ViewerContext& ctx) {
  ctx.startUs = qpc_now_us();
  VideoReceiver::NackOptions nack;
  nack.enabled = ctx.session.hostSupportsNack;
  nack.holdUs = ctx.videoNackHoldUs;
  nack.giveUpHardCapUs = static_cast<uint64_t>(env_u32_clamped("REMOTE60_NATIVE_VIDEO_GIVEUP_HARDCAP_MS", 5000, 300, 60000)) * 1000ULL;
  nack.replyAllowanceMaxUs = static_cast<uint64_t>(env_u32_clamped("REMOTE60_NATIVE_VIDEO_REPLY_ALLOWANCE_MAX_MS", 1000, 50, 5000)) * 1000ULL;
  ctx.receiver.emplace(ctx, ctx.args, ctx.dec, ctx.gate, ctx.startUs, ctx.udpSimDropPm, ctx.udpSimDropSeed,
                       nack);
  ctx.recvThread = std::thread([&ctx]() {
    try { ctx.receiver->Run(); } catch (...) {
      ctx.session.recoveryExitCode.store(43); ctx.session.running.store(false);
      PostMessageW(ctx.session.hwnd, WM_CLOSE, 0, 0);
    }
  });
}

void run_message_pump(ViewerContext& ctx) {
  ctx.session.uiHeartbeatUs.store(qpc_now_us());
  ctx.uiWatchdog = std::thread([&ctx] {
    while (!ctx.uiWatchdogStop.load()) {
      Sleep(250);
      const uint64_t age = qpc_now_us() - ctx.session.uiHeartbeatUs.load();
      if (!ctx.uiWatchdogStop.load() && age >= 15000000ULL) {
        const char text[] = "[viewer] UI stopped progressing; rebuilding session process\n";
        remote60::native_poc::terminate_with_diagnostic(43, text, sizeof(text) - 1);
      }
    }
  });
  MSG msg{};
  while (ctx.session.running.load()) {
    ctx.session.uiHeartbeatUs.store(qpc_now_us());
    bool hadMessage = false;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      hadMessage = true;
      if (msg.message == WM_QUIT) {
        ctx.session.running = false;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      ctx.session.uiHeartbeatUs.store(qpc_now_us());
    }
    if (!ctx.session.running.load()) break;

    // The toolbar shows connection, input, path, frame rate and the monitor list, all of which
    // change on other threads. Refreshing it on a slow tick here beats a push at each of the
    // dozen places that move them, and it is a posted message either way.
    {
      const uint64_t nowUs = qpc_now_us();
      if (nowUs >= ctx.session.nextToolbarPushUs) {
        ctx.session.nextToolbarPushUs = nowUs + 500000ULL;
        push_session_toolbar_state(ctx);
      }
    }

    if (ctx.args.seconds > 0) {
      const uint64_t nowUs = qpc_now_us();
      if (nowUs >= ctx.startUs + static_cast<uint64_t>(ctx.args.seconds) * 1000000ULL) {
        ctx.session.running = false;
        break;
      }
    }

    if (!hadMessage) {
      // A paint or keyboard message must wake a still desktop immediately, not wait for the
      // next coarse Windows sleep tick. Keep a timeout for shutdown and periodic UI work.
      MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
  }
}

}  // namespace remote60::native_poc::viewer

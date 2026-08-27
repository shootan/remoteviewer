#include "viewer_common.hpp"
#include "viewer_globals.hpp"
#include "viewer_env_util.hpp"
#include "viewer_log.hpp"
#include "viewer_args.hpp"
#include "viewer_decoder_backend.hpp"

namespace remote60::native_poc::viewer {


















int dpi_scale(int value) { return MulDiv(value, gUiDpi, 96); }

void ensure_ui_font(HWND hwnd) {
  int dpi = 96;
  if (hwnd) {
    const UINT windowDpi = GetDpiForWindow(hwnd);
    if (windowDpi > 0) dpi = static_cast<int>(windowDpi);
  }
  if (gUiFont && dpi == gUiDpi) return;
  if (gUiFont) {
    DeleteObject(gUiFont);
    gUiFont = nullptr;
  }
  gUiDpi = dpi;
  LOGFONTW lf{};
  lf.lfHeight = -MulDiv(9, dpi, 72);
  lf.lfWeight = FW_NORMAL;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfQuality = CLEARTYPE_QUALITY;
  lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
  std::wcscpy(lf.lfFaceName, L"Segoe UI");
  gUiFont = CreateFontIndirectW(&lf);
  if (gUiTitleFont) {
    DeleteObject(gUiTitleFont);
    gUiTitleFont = nullptr;
  }
  lf.lfHeight = -MulDiv(15, dpi, 72);
  lf.lfWeight = FW_SEMIBOLD;
  gUiTitleFont = CreateFontIndirectW(&lf);
}

// Paint-time solid brushes, cached by color. Cards used to create and destroy several
// brushes per paint, and the picker repaints on every thumbnail arrival. UI thread only.
std::unordered_map<COLORREF, HBRUSH>& brush_cache() {
  static std::unordered_map<COLORREF, HBRUSH> cache;
  return cache;
}

HBRUSH cached_brush(COLORREF color) {
  auto& cache = brush_cache();
  const auto it = cache.find(color);
  if (it != cache.end()) return it->second;
  HBRUSH brush = CreateSolidBrush(color);
  cache.emplace(color, brush);
  return brush;
}

void destroy_cached_gdi_objects() {
  for (auto& entry : brush_cache()) {
    DeleteObject(entry.second);
  }
  brush_cache().clear();
  if (gUiTitleFont) {
    DeleteObject(gUiTitleFont);
    gUiTitleFont = nullptr;
  }
}

void draw_text_utf8(HDC hdc, const std::string& text, RECT* rect, UINT format) {
  if (!rect) return;
  const std::wstring wide = utf8_to_wide(text);
  HGDIOBJ oldFont = gUiFont ? SelectObject(hdc, gUiFont) : nullptr;
  DrawTextW(hdc, wide.c_str(), static_cast<int>(wide.size()), rect, format);
  if (oldFont) SelectObject(hdc, oldFont);
}













// Browsing targets must not keep the host encoding (F1). The request rides the control
// scheduler, which orders stream state ahead of window selection. Sent only on explicit
// picker transitions: startup leaves the host's default-active stream alone, so headless
// harness clients that never open the picker keep receiving video unchanged.
void set_picker_visible_and_sync_stream(bool visible);
void ensure_cursor_overlay(HWND owner);
void update_cursor_overlay(HWND hwnd);

ClientControlMetricsSnapshot capture_client_control_metrics_snapshot() {
  ClientControlMetricsSnapshot snapshot{};
  snapshot.updatedQpcUs = gClientMetrics.updatedQpcUs.load(std::memory_order_relaxed);
  snapshot.message.width = gClientMetrics.width.load(std::memory_order_relaxed);
  snapshot.message.height = gClientMetrics.height.load(std::memory_order_relaxed);
  snapshot.message.recvFpsX100 = gClientMetrics.recvFpsX100.load(std::memory_order_relaxed);
  snapshot.message.decodedFpsX100 = gClientMetrics.decodedFpsX100.load(std::memory_order_relaxed);
  snapshot.message.recvMbpsX1000 = gClientMetrics.recvMbpsX1000.load(std::memory_order_relaxed);
  snapshot.message.skippedFrames = gClientMetrics.skippedFrames.load(std::memory_order_relaxed);
  snapshot.message.avgLatencyUs = gClientMetrics.avgLatencyUs.load(std::memory_order_relaxed);
  snapshot.message.maxLatencyUs = gClientMetrics.maxLatencyUs.load(std::memory_order_relaxed);
  snapshot.message.avgDecodeTailUs = gClientMetrics.avgDecodeTailUs.load(std::memory_order_relaxed);
  snapshot.message.maxDecodeTailUs = gClientMetrics.maxDecodeTailUs.load(std::memory_order_relaxed);
  snapshot.message.congestionState = gClientMetrics.congestionState.load(std::memory_order_relaxed);
  snapshot.message.congestionTransitions = gClientMetrics.congestionTransitions.load(std::memory_order_relaxed);
  snapshot.message.congestionRecoveryCount =
      gClientMetrics.congestionRecoveryCount.load(std::memory_order_relaxed);
  snapshot.message.congestionRecoveryReq =
      gClientMetrics.congestionRecoveryReq.load(std::memory_order_relaxed);
  snapshot.message.congestionRecoveryMaxUs =
      gClientMetrics.congestionRecoveryMaxUs.load(std::memory_order_relaxed);
  snapshot.message.queueDepthMax = gClientMetrics.queueDepthMax.load(std::memory_order_relaxed);
  snapshot.message.queueDepthH4p = gClientMetrics.queueDepthH4p.load(std::memory_order_relaxed);
  snapshot.message.udpAssemblyDropPm = gClientMetrics.udpAssemblyDropPm.load(std::memory_order_relaxed);
  return snapshot;
}




void queue_thumbnail_fetches_from_panel() {
  if (!gHostSupportsThumbnails.load(std::memory_order_relaxed)) return;
  const WindowPanelSnapshot snap = gWindowPanelState.Snapshot();
  const uint64_t nowUs = qpc_now_us();
  std::lock_guard<std::mutex> lk(gThumbMu);
  auto want = [&](uint64_t id) {
    const auto it = gThumbs.find(id);
    if (it != gThumbs.end() && it->second && nowUs - it->second->fetchedUs < kThumbRefreshUs) return;
    if (std::find(gThumbFetchQueue.begin(), gThumbFetchQueue.end(), id) != gThumbFetchQueue.end()) {
      return;
    }
    gThumbFetchQueue.push_back(id);
  };
  want(0);
  for (const auto& item : snap.items) want(item.id);
}

// Panel metrics are authored at 96 DPI and scaled per monitor; the process is
// per-monitor DPI aware, so raw pixel constants would render tiny on a scaled display.
inline int kPickerPanelPreferredWidth() { return dpi_scale(560); }
inline int kPickerPanelMinWidth() { return dpi_scale(420); }
inline int kPanelMargin() { return dpi_scale(12); }
inline int kPanelButtonHeight() { return dpi_scale(30); }
inline int kPanelButtonGap() { return dpi_scale(8); }
inline int kPanelSectionGap() { return dpi_scale(12); }
inline int kPanelInfoHeight() { return dpi_scale(64); }
inline int kPanelStatsHeight() { return dpi_scale(128); }
inline int kPanelItemHeight() { return dpi_scale(28); }
inline int kPanelItemGap() { return dpi_scale(4); }

struct ClientLayout {
  RECT clientRect{};
  RECT toggleButtonRect{};
  RECT macroButtonRect{};
  RECT panelRect{};
  RECT videoRect{};
  RECT refreshButtonRect{};
  RECT desktopButtonRect{};
  RECT selectedInfoRect{};
  RECT listRect{};
  RECT statsRect{};
};

RECT make_rect(int x, int y, int w, int h) {
  RECT r{};
  r.left = x;
  r.top = y;
  r.right = x + w;
  r.bottom = y + h;
  return r;
}

bool point_in_rect(const RECT& r, int x, int y) {
  return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

// Geometry of the card grid inside ClientLayout::listRect. Cards hold a 16:10 preview and a
// one-line caption, laid out left-to-right then top-to-bottom.
struct CardGridMetrics {
  int cols = 1;
  int cardW = 0;
  int cardH = 0;
  int thumbH = 0;
  int gap = 0;
  int visibleRows = 1;
  int visibleCards = 1;
};

CardGridMetrics compute_card_grid(const RECT& gridRect) {
  CardGridMetrics m;
  m.gap = dpi_scale(14);
  const int gridW = std::max<int>(1, gridRect.right - gridRect.left);
  const int gridH = std::max<int>(1, gridRect.bottom - gridRect.top);
  const int preferredCardW = dpi_scale(232);
  m.cols = std::max<int>(1, (gridW + m.gap) / (preferredCardW + m.gap));
  m.cardW = std::max<int>(dpi_scale(140), (gridW - (m.cols - 1) * m.gap) / m.cols);
  m.thumbH = (m.cardW * 10) / 16;
  m.cardH = m.thumbH + dpi_scale(30);
  m.visibleRows = std::max<int>(1, (gridH + m.gap) / (m.cardH + m.gap));
  m.visibleCards = m.visibleRows * m.cols;
  return m;
}

RECT card_rect_for_slot(const RECT& gridRect, const CardGridMetrics& m, int slot) {
  const int row = slot / m.cols;
  const int col = slot % m.cols;
  return make_rect(gridRect.left + col * (m.cardW + m.gap),
                   gridRect.top + row * (m.cardH + m.gap), m.cardW, m.cardH);
}


RECT aspect_fit_rect(const RECT& containerRect, uint32_t contentWidth, uint32_t contentHeight) {
  const int containerWidth =
      std::max<int>(1, static_cast<int>(containerRect.right - containerRect.left));
  const int containerHeight =
      std::max<int>(1, static_cast<int>(containerRect.bottom - containerRect.top));
  if (contentWidth == 0 || contentHeight == 0) {
    return containerRect;
  }

  const double containerAspect =
      static_cast<double>(containerWidth) / static_cast<double>(containerHeight);
  const double contentAspect =
      static_cast<double>(contentWidth) / static_cast<double>(contentHeight);
  int drawWidth = containerWidth;
  int drawHeight = containerHeight;
  if (contentAspect > containerAspect) {
    drawHeight = std::max<int>(1, static_cast<int>(std::lround(
        static_cast<double>(containerWidth) / contentAspect)));
  } else {
    drawWidth = std::max<int>(1, static_cast<int>(std::lround(
        static_cast<double>(containerHeight) * contentAspect)));
  }

  const int offsetX = (containerWidth - drawWidth) / 2;
  const int offsetY = (containerHeight - drawHeight) / 2;
  return make_rect(containerRect.left + offsetX, containerRect.top + offsetY, drawWidth, drawHeight);
}

bool resolve_active_video_content_size(uint32_t* outWidth, uint32_t* outHeight) {
  if (!outWidth || !outHeight) return false;
  *outWidth = 0;
  *outHeight = 0;

  const WindowPanelSnapshot panelSnapshot = gWindowPanelState.Snapshot();
  const uint32_t selectedWidth = panelSnapshot.selectedWidth;
  const uint32_t selectedHeight = panelSnapshot.selectedHeight;
  const uint64_t selectedStreamGeneration = panelSnapshot.lastSelectStreamGeneration;

  uint32_t frameWidth = 0;
  uint32_t frameHeight = 0;
  uint64_t frameStreamGeneration = 0;
  {
    std::lock_guard<std::mutex> lk(gFrame.mu);
    frameWidth = gFrame.width;
    frameHeight = gFrame.height;
    frameStreamGeneration = gFrame.streamGeneration;
  }

  if (selectedWidth > 0 && selectedHeight > 0) {
    if (frameWidth > 0 && frameHeight > 0 &&
        (selectedStreamGeneration == 0 || frameStreamGeneration == selectedStreamGeneration)) {
      *outWidth = frameWidth;
      *outHeight = frameHeight;
    } else {
      *outWidth = selectedWidth;
      *outHeight = selectedHeight;
    }
    return true;
  }

  const uint32_t metricWidth = gClientMetrics.width.load(std::memory_order_relaxed);
  const uint32_t metricHeight = gClientMetrics.height.load(std::memory_order_relaxed);
  if (metricWidth > 0 && metricHeight > 0) {
    *outWidth = metricWidth;
    *outHeight = metricHeight;
    return true;
  }

  if (frameWidth > 0 && frameHeight > 0) {
    *outWidth = frameWidth;
    *outHeight = frameHeight;
    return true;
  }
  return false;
}

RECT resolve_video_content_rect(HWND hwnd, const RECT& containerRect) {
  (void)hwnd;
  uint32_t contentWidth = 0;
  uint32_t contentHeight = 0;
  if (!resolve_active_video_content_size(&contentWidth, &contentHeight)) {
    return containerRect;
  }
  return aspect_fit_rect(containerRect, contentWidth, contentHeight);
}

ClientLayout compute_client_layout(HWND hwnd) {
  ClientLayout layout{};
  if (hwnd && IsWindow(hwnd)) {
    GetClientRect(hwnd, &layout.clientRect);
  } else {
    layout.clientRect = make_rect(0, 0, static_cast<int>(gWindowW), static_cast<int>(gWindowH));
  }
  const int clientW =
      std::max<int>(1, static_cast<int>(layout.clientRect.right - layout.clientRect.left));
  const int clientH =
      std::max<int>(1, static_cast<int>(layout.clientRect.bottom - layout.clientRect.top));
  layout.videoRect = make_rect(0, 0, clientW, clientH);

  if (!gWindowPickerVisible.load(std::memory_order_relaxed)) {
    // No legacy top-left Targets/Macro buttons in the stream view. The flip-model swapchain
    // erases anything GDI paints before the user can see it, so these existed only as INVISIBLE
    // hit zones -- sitting exactly where games put their top-left UI. In the field, clicking a
    // map's region breadcrumb silently toggled the picker ("the screen froze") and the spot next
    // to it opened the macro window. The session toolbar (its own composited window, summoned by
    // the top-center dwell) carries Targets/Macro/Monitor now; empty rects keep every handler
    // branch dead without touching the input-forwarding paths.
    layout.toggleButtonRect = make_rect(0, 0, 0, 0);
    layout.macroButtonRect = make_rect(0, 0, 0, 0);
    layout.panelRect = make_rect(0, 0, 0, 0);
    layout.refreshButtonRect = make_rect(0, 0, 0, 0);
    layout.desktopButtonRect = make_rect(0, 0, 0, 0);
    layout.selectedInfoRect = make_rect(0, 0, 0, 0);
    layout.listRect = make_rect(0, 0, 0, 0);
    layout.statsRect = make_rect(0, 0, 0, 0);
    return layout;
  }

  // The home screen owns the whole window: a header band with the actions, a card grid of
  // capture targets, and a one-line status footer.
  layout.panelRect = layout.clientRect;
  layout.toggleButtonRect = make_rect(0, 0, 0, 0);
  layout.macroButtonRect = make_rect(0, 0, 0, 0);

  const int margin = dpi_scale(24);
  const int headerH = dpi_scale(56);
  const int footerH = dpi_scale(36);
  const int buttonW = dpi_scale(130);

  layout.desktopButtonRect =
      make_rect(clientW - margin - buttonW, margin / 2 + (headerH - kPanelButtonHeight()) / 2,
                buttonW, kPanelButtonHeight());
  layout.refreshButtonRect =
      make_rect(layout.desktopButtonRect.left - kPanelButtonGap() - dpi_scale(96),
                layout.desktopButtonRect.top, dpi_scale(96), kPanelButtonHeight());
  layout.selectedInfoRect = make_rect(margin, margin / 2,
                                      std::max<int>(1, layout.refreshButtonRect.left - margin * 2),
                                      headerH);

  const int gridY = margin / 2 + headerH + dpi_scale(10);
  layout.listRect = make_rect(margin, gridY, std::max<int>(1, clientW - margin * 2),
                              std::max<int>(dpi_scale(120), clientH - gridY - footerH - dpi_scale(10)));
  layout.statsRect = make_rect(margin, layout.listRect.bottom + dpi_scale(6),
                               std::max<int>(1, clientW - margin * 2), footerH);
  return layout;
}

bool point_in_toggle_button(HWND hwnd, int x, int y) {
  const ClientLayout layout = compute_client_layout(hwnd);
  return point_in_rect(layout.toggleButtonRect, x, y);
}

bool point_in_macro_button(HWND hwnd, int x, int y) {
  const ClientLayout layout = compute_client_layout(hwnd);
  return point_in_rect(layout.macroButtonRect, x, y);
}

bool point_in_panel_ui(HWND hwnd, int x, int y) {
  const ClientLayout layout = compute_client_layout(hwnd);
  return point_in_rect(layout.panelRect, x, y);
}

bool point_in_video_rect(HWND hwnd, int x, int y) {
  const ClientLayout layout = compute_client_layout(hwnd);
  return point_in_rect(layout.videoRect, x, y);
}

bool map_client_point_to_video_coords(HWND hwnd, int x, int y, int32_t* outVideoX, int32_t* outVideoY) {
  if (!outVideoX || !outVideoY) return false;
  const ClientLayout layout = compute_client_layout(hwnd);
  const RECT contentRect = resolve_video_content_rect(hwnd, layout.videoRect);
  if (!point_in_rect(contentRect, x, y)) return false;
  uint32_t frameW = 0;
  uint32_t frameH = 0;
  if (!resolve_active_video_content_size(&frameW, &frameH)) return false;
  const int relX =
      std::clamp<int>(x - contentRect.left, 0,
                      std::max<int>(0, static_cast<int>(contentRect.right - contentRect.left - 1)));
  const int relY =
      std::clamp<int>(y - contentRect.top, 0,
                      std::max<int>(0, static_cast<int>(contentRect.bottom - contentRect.top - 1)));
  const int videoW = std::max<int>(1, static_cast<int>(contentRect.right - contentRect.left));
  const int videoH = std::max<int>(1, static_cast<int>(contentRect.bottom - contentRect.top));
  *outVideoX = static_cast<int32_t>((static_cast<uint64_t>(relX) * static_cast<uint64_t>(frameW - 1) +
                                     static_cast<uint64_t>(videoW / 2)) /
                                    static_cast<uint64_t>(videoW));
  *outVideoY = static_cast<int32_t>((static_cast<uint64_t>(relY) * static_cast<uint64_t>(frameH - 1) +
                                     static_cast<uint64_t>(videoH / 2)) /
                                    static_cast<uint64_t>(videoH));
  gLastInputVideoX.store(*outVideoX, std::memory_order_relaxed);
  gLastInputVideoY.store(*outVideoY, std::memory_order_relaxed);
  return true;
}

void enqueue_input_event(uint16_t kind, int32_t x, int32_t y, int32_t wheelDelta, uint32_t keyCode);
void enqueue_input_text_units(const uint16_t* text, size_t count);

void enqueue_control_input_message(const QueuedControlInputMessage& msg) {
  gInputQueueState.Enqueue(msg);
}

void enqueue_input_text_units(const uint16_t* text, size_t count) {
  if (kInputPolicyForceBlock) return;
  if (!gInputEnabled.load()) return;
  if (!text || count == 0) return;
  size_t offset = 0;
  while (offset < count) {
    const size_t remaining = count - offset;
    const size_t chunk = std::min<size_t>(remaining, remote60::native_poc::kControlInputTextMaxUtf16);
    QueuedControlInputMessage msg{};
    msg.type = MessageType::ControlInputText;
    msg.inputText.header.magic = remote60::native_poc::kMagic;
    msg.inputText.header.type = static_cast<uint16_t>(MessageType::ControlInputText);
    msg.inputText.header.size = static_cast<uint16_t>(sizeof(msg.inputText));
    msg.inputText.seq = gInputQueueState.NextSequence();
    msg.inputText.utf16Count = static_cast<uint16_t>(chunk);
    std::memcpy(msg.inputText.utf16, text + offset, chunk * sizeof(uint16_t));
    msg.inputText.clientSendQpcUs = qpc_now_us();
    enqueue_control_input_message(msg);
    offset += chunk;
  }
}

bool local_hotkey_modifiers_active() {
  return (GetKeyState(VK_CONTROL) < 0) && (GetKeyState(VK_MENU) < 0);
}


/**
 * Whether this virtual key should be forwarded as a key event.
 *
 * Keys that produce a character are excluded, because their character arrives through the
 * text path -- forwarding both injected every printable twice: Korean once as composed text
 * and once as the raw letter the host's English layout makes of the same key. That was the
 * "type 11, get 22" session, with an English echo trailing every Hangul syllable.
 */
bool key_event_should_forward(WPARAM vk) {
  // Only a key the IME is composing with stays off the key path -- its result arrives instead
  // through WM_IME_COMPOSITION as committed text. Everything else -- letters, digits, space,
  // symbols, function keys, Enter, Tab, Backspace -- goes as a key event.
  //
  // The earlier attempt gated this on "does the key produce a character" via MapVirtualKey,
  // which pushed digits, space, and symbols onto the text path. That path shares the IME
  // result-suppression counter, and once a Korean syllable committed, the counter was off by
  // enough to swallow the next few printables -- the "type it several times before it lands"
  // report. Routing every non-composed key as a key event keeps them clear of that counter
  // entirely; only Hangul, which genuinely needs composition, takes the text path.
  return vk != VK_PROCESSKEY;
}

/** Decides for a down event and records the answer for the matching up. */
bool forward_key_down(WPARAM vk) {
  const bool forward = key_event_should_forward(vk);
  if (vk < 256) gForwardedKeyDown[vk].store(forward, std::memory_order_relaxed);
  return forward;
}

bool forward_key_up(WPARAM vk) {
  if (vk < 256) return gForwardedKeyDown[vk].exchange(false, std::memory_order_relaxed);
  return key_event_should_forward(vk);
}

bool send_ime_result_text(HWND hwnd, LPARAM imeFlags) {
  if ((imeFlags & GCS_RESULTSTR) == 0) return false;
  HIMC imc = ImmGetContext(hwnd);
  if (!imc) return false;
  const LONG bytes = ImmGetCompositionStringW(imc, GCS_RESULTSTR, nullptr, 0);
  if (bytes <= 0) {
    ImmReleaseContext(hwnd, imc);
    return false;
  }
  std::vector<uint16_t> text(static_cast<size_t>(bytes) / sizeof(uint16_t));
  const LONG copied = ImmGetCompositionStringW(imc, GCS_RESULTSTR, text.data(), bytes);
  ImmReleaseContext(hwnd, imc);
  if (copied <= 0 || text.empty()) return false;
  enqueue_input_text_units(text.data(), text.size());
  return true;
}

void release_mouse_capture_if_idle(HWND hwnd) {
  if ((gMouseButtons.load(std::memory_order_relaxed) & 0x7u) == 0 && GetCapture() == hwnd) {
    ReleaseCapture();
  }
}

void enqueue_release_for_pressed_mouse_buttons() {
  const uint16_t buttons = gMouseButtons.exchange(0, std::memory_order_acq_rel);
  if ((buttons & 0x7u) == 0) return;
  const int32_t vx = gLastInputVideoX.load(std::memory_order_relaxed);
  const int32_t vy = gLastInputVideoY.load(std::memory_order_relaxed);
  if ((buttons & 0x4u) != 0) enqueue_input_event(3, vx, vy, 0, VK_MBUTTON);
  if ((buttons & 0x2u) != 0) enqueue_input_event(3, vx, vy, 0, VK_RBUTTON);
  if ((buttons & 0x1u) != 0) enqueue_input_event(3, vx, vy, 0, VK_LBUTTON);
}

// Release every key this client has an outstanding down for.
//
// A key-up only arrives if this window still has focus when the key is released. Alt, the Win
// key, and Alt+Tab are all intercepted by the local Windows and steal focus as they do it, so
// their down reaches the host and their up never does -- the host is left holding a modifier
// nobody is pressing, and because it is a real SendInput state it survives the client being
// closed and reopened. Sending the up for everything held, the moment focus is lost, is what
// keeps a modifier from latching on the host.
void enqueue_release_for_pressed_keys() {
  for (int vk = 0; vk < 256; ++vk) {
    if (gForwardedKeyDown[vk].exchange(false, std::memory_order_relaxed)) {
      enqueue_input_event(6, 0, 0, 0, static_cast<uint32_t>(vk));
    }
  }
}

uint32_t coord_to_permille(int coord, int extent) {
  if (extent <= 1) return 5000;
  const int clamped = std::clamp(coord, 0, extent - 1);
  const uint64_t numerator = static_cast<uint64_t>(clamped) * 10000ULL +
                             static_cast<uint64_t>((extent - 1) / 2);
  return static_cast<uint32_t>(numerator / static_cast<uint64_t>(extent - 1));
}

void queue_window_list_request(const char* statusText = nullptr) {
  gWindowPanelState.RequestList(statusText);
}

void queue_window_select_request(uint64_t windowId, const char* statusText = nullptr) {
  gWindowPanelState.RequestSelect(windowId, statusText);
}

void set_window_panel_status(const std::string& status) {
  gWindowPanelState.SetStatus(status);
}

void apply_window_list_snapshot(const ControlWindowListMessage& msg) {
  const ClientLayout layout = compute_client_layout(gHwnd);
  const CardGridMetrics grid = compute_card_grid(layout.listRect);
  const auto result = gWindowPanelState.ApplyWindowList(msg, grid.visibleCards);
  gHostSupportsThumbnails.store(
      (msg.flags & remote60::native_poc::kControlWindowListFlagThumbnails) != 0,
      std::memory_order_relaxed);
  queue_thumbnail_fetches_from_panel();
  log_client_line(result.logLine);
}

/** Mirrors the session state into the toolbar window. Cheap enough to call on every change. */
void push_session_toolbar_state() {
  const WindowPanelSnapshot panel = gWindowPanelState.Snapshot();
  remote60::native_poc::SessionToolbarState state;
  state.connected = gControlConnected.load(std::memory_order_relaxed);
  state.inputOn = gInputEnabled.load(std::memory_order_relaxed);
  state.macroOpen = remote60::native_poc::macro_window_visible();
  state.relay = gRelayPath.load(std::memory_order_relaxed);
  state.fps = gClientMetrics.decodedFpsX100.load(std::memory_order_relaxed) / 100;
  state.selectedMonitorId = panel.selectedMonitorId;
  for (const auto& monitor : panel.monitors) {
    state.monitors.push_back({monitor.id, monitor.width, monitor.height, monitor.primary});
  }
  remote60::native_poc::session_toolbar_update(state);
}

void set_picker_visible_and_sync_stream(bool visible) {
  gWindowPickerVisible.store(visible, std::memory_order_relaxed);
  if (visible) {
    gPickerShownAtUs.store(qpc_now_us(), std::memory_order_relaxed);
    gPickerPressTargetId.store(kPickerPressNone, std::memory_order_relaxed);
    // Mid-session the stream KEEPS RUNNING behind the picker overlay. Stopping it here made every
    // "is it frozen?" peek tear the capture down (host detaches after 5 idle seconds), a real
    // multi-second blackout, and a reselect/keyframe churn on close -- the recovery gesture was
    // manufacturing the very freeze it was checking for. Only the initial picker, before any
    // selection has been revealed (gActiveStreamGeneration==0), still holds the stream off.
    if (gActiveStreamGeneration.load(std::memory_order_acquire) == 0) {
      gStreamStateControl.Request(false);
    }
  } else {
    gStreamStateControl.Request(true);
    // The present anchor froze while the picker covered the stream; drop it and hold catchup off
    // until the first post-close present re-anchors it, so the pause cannot read as backlog.
    gLastPresentedCaptureUs.store(0, std::memory_order_relaxed);
    gCatchupSuppressUntilUs.store(qpc_now_us() + 500000, std::memory_order_relaxed);
  }
  // The picker draws its own header, so the toolbar belongs to the session view alone.
  remote60::native_poc::session_toolbar_set_visible(!visible);
  push_session_toolbar_state();
}

// Clears the in-flight selection state. Safe to call from any thread; touches only atomics and
// the stream request (itself atomic-backed).
void clear_pc_target_selection() {
  gSelectionPending.store(false, std::memory_order_release);
  gSelectionAwaitingAck.store(false, std::memory_order_release);
  gSelectionExpectedGeneration.store(0, std::memory_order_release);
}

// UI-thread entry for picking a target from the picker. Orders the control traffic so the host's
// "!streamActive" continue-gate is already passed when the select arrives: the scheduler always
// sends StreamState ahead of WindowSelect, so requesting stream-on here (before or after the
// select is queued) still reaches the host first. Desktop is sent as an explicit WindowSelect(0)
// -- one clean restart -- rather than the "already selected, just hide" shortcut, for mobile
// parity. Ignores clicks while a selection is already in flight, which is the double-click guard.
// Returns true only when the select request was actually accepted for sending, so callers can
// log/refresh on real selections instead of refused attempts (disconnected, locked target).
bool begin_pc_target_selection(uint64_t windowId, const char* statusText) {
  if (gSelectionPending.load(std::memory_order_acquire)) return false;
  if (!gControlConnected.load(std::memory_order_relaxed)) return false;
  // RequestSelect refuses when the target is locked by host config; do not touch the stream then.
  if (!gWindowPanelState.RequestSelect(windowId, statusText)) return false;
  gSelectionExpectedGeneration.store(0, std::memory_order_release);
  gSelectionAwaitingAck.store(true, std::memory_order_release);
  gSelectionPending.store(true, std::memory_order_release);
  // Bumped so the receive loop resets the decoder and holds for the new generation's keyframe.
  gSelectionEpoch.fetch_add(1, std::memory_order_acq_rel);
  gStreamStateControl.Request(true);
  // A selection is a generation change; drop the remote-cursor sample so the previous target's
  // pointer cannot linger over the new one while the first fenced sample is in flight.
  gRemoteCursorUpdateUs.store(0, std::memory_order_release);
  if (gHwnd) InvalidateRect(gHwnd, nullptr, FALSE);
  return true;
}

// Video-thread half of the reveal. It only *records* the candidate (the generation and epoch of
// the first decoded frame) and posts the reveal once; it deliberately touches none of the live
// selection state. The UI-thread handler revalidates against that state before committing, so a
// cancel / new selection / disconnect that races the post cannot wrongly close the picker.
void post_pc_selection_reveal(uint64_t readyGeneration, uint64_t readyEpoch) {
  gSelectionReadyGeneration.store(readyGeneration, std::memory_order_release);
  gSelectionReadyEpoch.store(readyEpoch, std::memory_order_release);
  bool expected = false;
  if (gSelectionRevealPosted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    if (gHwnd) PostMessageW(gHwnd, kMsgRevealStreamView, 0, 0);
  }
}

void apply_window_selected_result(const ControlWindowSelectedMessage& msg) {
  const auto result = gWindowPanelState.ApplyWindowSelected(msg);
  log_client_line(result.logLine);
  if (gSelectionPending.load(std::memory_order_acquire)) {
    if (result.ok) {
      // Ack received: hold the picker up until the first frame of this generation is presented.
      // Do NOT hide the picker here -- that is what the first-frame gate is for.
      gSelectionExpectedGeneration.store(msg.streamGeneration, std::memory_order_release);
      gSelectionAwaitingAck.store(false, std::memory_order_release);
      gWindowPanelState.SetStatus("waiting_first_frame");
    } else {
      // Select failed: stop the stream we speculatively started, keep the picker, allow a retry.
      gStreamStateControl.Request(false);
      clear_pc_target_selection();
    }
    if (gHwnd) InvalidateRect(gHwnd, nullptr, FALSE);
    return;
  }
  // No PC-side selection tracked (e.g. a legacy stream-view session): behave as before.
  if (result.ok) {
    set_picker_visible_and_sync_stream(false);
  }
}

void scroll_window_list(HWND hwnd, int deltaSteps) {
  const ClientLayout layout = compute_client_layout(hwnd);
  const CardGridMetrics grid = compute_card_grid(layout.listRect);
  const int totalCards = 1 + static_cast<int>(gWindowPanelState.Snapshot().items.size());
  const int totalRows = (totalCards + grid.cols - 1) / grid.cols;
  const int maxScrollRow = std::max(0, totalRows - grid.visibleRows);
  const int cur = gGridScrollRow.load(std::memory_order_relaxed);
  gGridScrollRow.store(std::clamp(cur + deltaSteps, 0, maxScrollRow), std::memory_order_relaxed);
}

// Hit-test a grid card. Card index 0 is the pinned Desktop card; window items follow.
bool try_hit_window_list_item(HWND hwnd, int x, int y, uint64_t* outWindowId) {
  if (!outWindowId) return false;
  const ClientLayout layout = compute_client_layout(hwnd);
  if (!point_in_rect(layout.listRect, x, y)) return false;
  const CardGridMetrics grid = compute_card_grid(layout.listRect);
  const int relX = x - layout.listRect.left;
  const int relY = y - layout.listRect.top;
  const int col = relX / (grid.cardW + grid.gap);
  const int row = relY / (grid.cardH + grid.gap);
  if (col < 0 || col >= grid.cols || row < 0 || row >= grid.visibleRows) return false;
  // Reject clicks that land in the gaps between cards.
  if (relX - col * (grid.cardW + grid.gap) >= grid.cardW) return false;
  if (relY - row * (grid.cardH + grid.gap) >= grid.cardH) return false;
  const WindowPanelSnapshot snap = gWindowPanelState.Snapshot();
  const int cardIndex =
      gGridScrollRow.load(std::memory_order_relaxed) * grid.cols + row * grid.cols + col;
  if (cardIndex == 0) {
    *outWindowId = 0;
    return true;
  }
  const int itemIndex = cardIndex - 1;
  if (itemIndex < 0 || itemIndex >= static_cast<int>(snap.items.size())) return false;
  *outWindowId = snap.items[static_cast<size_t>(itemIndex)].id;
  return true;
}

void enqueue_capture_mode_request(uint16_t mode, uint32_t xPermille, uint32_t yPermille) {
  gCaptureModeRequests.Request(mode, xPermille, yPermille);
}

void request_capture_overview_mode() {
  enqueue_capture_mode_request(1, 5000, 5000);
}

void request_capture_focus_from_client_point(HWND hwnd, int x, int y) {
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const int clientW = std::max<int>(1, static_cast<int>(rc.right - rc.left));
  const int clientH = std::max<int>(1, static_cast<int>(rc.bottom - rc.top));
  enqueue_capture_mode_request(2, coord_to_permille(x, clientW), coord_to_permille(y, clientH));
}

void draw_alpha_rect(HDC hdc, const RECT& rect, COLORREF color, BYTE alpha) {
  const int w = rect.right - rect.left;
  const int h = rect.bottom - rect.top;
  if (w <= 0 || h <= 0) return;
  HDC memDc = CreateCompatibleDC(hdc);
  if (!memDc) return;
  HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
  if (!bmp) {
    DeleteDC(memDc);
    return;
  }
  HGDIOBJ oldBmp = SelectObject(memDc, bmp);
  RECT fillRc{0, 0, w, h};
  FillRect(memDc, &fillRc, cached_brush(color));
  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = alpha;
  blend.AlphaFormat = 0;
  AlphaBlend(hdc, rect.left, rect.top, w, h, memDc, 0, 0, w, h, blend);
  SelectObject(memDc, oldBmp);
  DeleteObject(bmp);
  DeleteDC(memDc);
}

void draw_panel_button(HDC hdc, const RECT& rect, const char* label, bool active = false,
                       bool disabled = false) {
  COLORREF fill = RGB(60, 68, 80);
  if (disabled) {
    fill = RGB(42, 46, 54);
  } else if (active) {
    fill = RGB(48, 96, 62);
  }
  FillRect(hdc, &rect, cached_brush(fill));
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, disabled ? RGB(160, 165, 170) : RGB(240, 240, 240));
  RECT textRect = rect;
  draw_text_utf8(hdc, label ? std::string(label) : std::string{}, &textRect,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void push_overlay_metric_sample(uint32_t recvFpsX100, uint32_t decodedFpsX100, uint32_t recvMbpsX1000,
                                uint64_t avgLatencyUs, uint64_t nowUs) {
  std::lock_guard<std::mutex> lk(gOverlayMetricsMu);
  gOverlayMetrics.push_back({nowUs, recvFpsX100, decodedFpsX100, recvMbpsX1000, avgLatencyUs});
  const uint64_t keepWindowUs = 12000000ULL;
  while (!gOverlayMetrics.empty() && nowUs > gOverlayMetrics.front().tsUs &&
         (nowUs - gOverlayMetrics.front().tsUs) > keepWindowUs) {
    gOverlayMetrics.pop_front();
  }
}

OverlayMetricAverages collect_overlay_averages(uint64_t nowUs, uint64_t windowUs) {
  OverlayMetricAverages out{};
  std::lock_guard<std::mutex> lk(gOverlayMetricsMu);
  uint64_t sumRecvFpsX100 = 0;
  uint64_t sumDecodedFpsX100 = 0;
  uint64_t sumRecvMbpsX1000 = 0;
  uint64_t sumLatencyUs = 0;
  for (const auto& s : gOverlayMetrics) {
    if (nowUs >= s.tsUs && (nowUs - s.tsUs) <= windowUs) {
      ++out.sampleCount;
      sumRecvFpsX100 += s.recvFpsX100;
      sumDecodedFpsX100 += s.decodedFpsX100;
      sumRecvMbpsX1000 += s.recvMbpsX1000;
      sumLatencyUs += s.avgLatencyUs;
    }
  }
  if (out.sampleCount > 0) {
    out.recvFpsX100 = static_cast<uint32_t>(sumRecvFpsX100 / out.sampleCount);
    out.decodedFpsX100 = static_cast<uint32_t>(sumDecodedFpsX100 / out.sampleCount);
    out.recvMbpsX1000 = static_cast<uint32_t>(sumRecvMbpsX1000 / out.sampleCount);
    out.avgLatencyUs = sumLatencyUs / out.sampleCount;
  }
  return out;
}

void apply_runtime_tune_delta(int bitrateStep, int keyintStep) {
  gRuntimeTuneState.ApplyDelta(
      bitrateStep, keyintStep, gClientMetrics.recvMbpsX1000.load(std::memory_order_relaxed));
}

void draw_thumbnail_into(HDC hdc, const RECT& dst, const WindowThumb& thumb) {
  if (thumb.bgra.empty() || thumb.width == 0 || thumb.height == 0) return;
  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = static_cast<LONG>(thumb.width);
  bmi.bmiHeader.biHeight = -static_cast<LONG>(thumb.height);
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  const RECT fit = aspect_fit_rect(dst, thumb.width, thumb.height);
  // Thumbnails repaint rarely, so the quality mode is affordable here.
  SetStretchBltMode(hdc, HALFTONE);
  SetBrushOrgEx(hdc, 0, 0, nullptr);
  StretchDIBits(hdc, fit.left, fit.top, fit.right - fit.left, fit.bottom - fit.top, 0, 0,
                static_cast<int>(thumb.width), static_cast<int>(thumb.height), thumb.bgra.data(),
                &bmi, DIB_RGB_COLORS, SRCCOPY);
}

void draw_target_card(HDC hdc, const RECT& card, const CardGridMetrics& grid,
                      uint64_t windowId, const std::string& title, bool active, bool disabled) {
  const RECT thumbRect = make_rect(card.left, card.top, card.right - card.left, grid.thumbH);
  const RECT captionRect = make_rect(card.left, card.top + grid.thumbH, card.right - card.left,
                                     card.bottom - card.top - grid.thumbH);

  FillRect(hdc, &thumbRect, cached_brush(RGB(24, 28, 36)));
  FillRect(hdc, &captionRect, cached_brush(active ? RGB(38, 70, 52) : RGB(32, 37, 46)));

  // Snapshot under the lock, draw outside it: StretchDIBits under gThumbMu made the fetch
  // thread and the paint stall each other.
  std::shared_ptr<const WindowThumb> thumb;
  {
    std::lock_guard<std::mutex> lk(gThumbMu);
    const auto it = gThumbs.find(windowId);
    if (it != gThumbs.end()) thumb = it->second;
  }
  if (thumb) {
    draw_thumbnail_into(hdc, thumbRect, *thumb);
  } else {
    RECT ph = thumbRect;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(110, 118, 130));
    draw_text_utf8(hdc, windowId == 0 ? std::string("Desktop") : std::string("Loading preview..."),
                   &ph, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, disabled ? RGB(150, 155, 162) : RGB(236, 239, 243));
  RECT text = captionRect;
  text.left += dpi_scale(10);
  text.right -= dpi_scale(10);
  draw_text_utf8(hdc, title, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  RECT frame = card;
  FrameRect(hdc, &frame, cached_brush(active ? RGB(88, 178, 122) : RGB(52, 58, 70)));
  if (active) {
    RECT inner{card.left + 1, card.top + 1, card.right - 1, card.bottom - 1};
    FrameRect(hdc, &inner, cached_brush(RGB(88, 178, 122)));
  }
}

void draw_overlay(HDC hdc) {
  const ClientLayout layout = compute_client_layout(gHwnd);
  const bool pickerVisible = gWindowPickerVisible.load(std::memory_order_relaxed);
  if (!pickerVisible) {
    // Nothing to draw over the stream: the legacy Targets/Macro buttons were invisible ghost
    // hit-zones under the flip-model video (see compute_client_layout); the toolbar owns that UI.
    return;
  }

  draw_alpha_rect(hdc, layout.clientRect, RGB(13, 15, 20), 255);

  const WindowPanelSnapshot windowPanel = gWindowPanelState.Snapshot();
  const std::vector<WindowTargetUiEntry>& windowItems = windowPanel.items;
  const uint64_t selectedId = windowPanel.selectedId;
  const std::string& panelStatus = windowPanel.status;
  const bool selectionLocked = windowPanel.selectionLocked;

  // Header: product title and status on the left, actions on the right.
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(240, 243, 247));
  RECT titleRect = layout.selectedInfoRect;
  {
    HGDIOBJ old = gUiTitleFont ? SelectObject(hdc, gUiTitleFont) : nullptr;
    RECT t = titleRect;
    DrawTextW(hdc, L"Remote60", -1, &t, DT_LEFT | DT_SINGLELINE);
    if (old) SelectObject(hdc, old);
  }
  // Once a target is picked the picker locks: the buttons and cards read as disabled while the
  // stream spins up, and the sub-header says whether we are still waiting on the host's ack or
  // on its first frame.
  const bool selectionPending = gSelectionPending.load(std::memory_order_acquire);
  const bool awaitingAck = gSelectionAwaitingAck.load(std::memory_order_acquire);

  RECT subRect = titleRect;
  subRect.top += dpi_scale(28);
  SetTextColor(hdc, RGB(150, 158, 170));
  std::string statusLine =
      selectionLocked ? std::string("Target locked by host config") : panelStatus;
  if (selectionPending) {
    statusLine = awaitingAck ? std::string("Selecting target...")
                             : std::string("Waiting for first frame...");
  }
  if (!gControlConnected.load(std::memory_order_relaxed)) statusLine = "Connecting to host...";
  draw_text_utf8(hdc, statusLine, &subRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

  const bool actionsDisabled =
      !gControlConnected.load(std::memory_order_relaxed) || selectionLocked || selectionPending;
  draw_panel_button(hdc, layout.refreshButtonRect, "Refresh", false,
                    !gControlConnected.load(std::memory_order_relaxed) || selectionPending);
  draw_panel_button(hdc, layout.desktopButtonRect, "Desktop", selectedId == 0, actionsDisabled);

  // Card grid: desktop preview first, then one card per shareable window.
  const CardGridMetrics grid = compute_card_grid(layout.listRect);
  const int totalCards = 1 + static_cast<int>(windowItems.size());
  const int totalRows = (totalCards + grid.cols - 1) / grid.cols;
  const int maxScrollRow = std::max(0, totalRows - grid.visibleRows);
  int scrollRow = std::clamp(gGridScrollRow.load(std::memory_order_relaxed), 0, maxScrollRow);
  gGridScrollRow.store(scrollRow, std::memory_order_relaxed);
  const int firstCard = scrollRow * grid.cols;

  for (int slot = 0; slot < grid.visibleCards; ++slot) {
    const int cardIndex = firstCard + slot;
    if (cardIndex >= totalCards) break;
    const RECT card = card_rect_for_slot(layout.listRect, grid, slot);
    if (cardIndex == 0) {
      draw_target_card(hdc, card, grid, 0, "Desktop (full screen)", selectedId == 0,
                       selectionLocked || selectionPending);
    } else {
      const auto& entry = windowItems[static_cast<size_t>(cardIndex - 1)];
      draw_target_card(hdc, card, grid, entry.id, entry.title, entry.id == selectedId,
                       selectionLocked || selectionPending);
    }
  }

  if (windowItems.empty()) {
    RECT emptyRect = layout.listRect;
    emptyRect.top += grid.cardH + dpi_scale(18);
    SetTextColor(hdc, RGB(150, 158, 170));
    draw_text_utf8(hdc,
                   selectionLocked ? std::string("Window list hidden by host config")
                                   : std::string("No shareable windows yet. Click Refresh."),
                   &emptyRect, DT_CENTER | DT_SINGLELINE);
  }

  // Footer: connection and input state in one quiet line.
  std::ostringstream foot;
  foot << (gControlConnected.load(std::memory_order_relaxed) ? "Connected" : "Disconnected")
       << "   Input " << (gInputEnabled.load(std::memory_order_relaxed) ? "on" : "off");
  const uint32_t decFpsX100 = gClientMetrics.decodedFpsX100.load(std::memory_order_relaxed);
  if (decFpsX100 > 0) foot << "   " << (decFpsX100 / 100) << " fps";
  if (totalRows > grid.visibleRows) {
    foot << "   Rows " << (scrollRow + 1) << "-"
         << std::min(totalRows, scrollRow + grid.visibleRows) << " / " << totalRows
         << " (wheel to scroll)";
  }
  RECT footRect = layout.statsRect;
  SetTextColor(hdc, RGB(140, 148, 160));
  draw_text_utf8(hdc, foot.str(), &footRect,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}



struct Nv12RenderTelemetry {
  uint64_t uploadYUs = 0;
  uint64_t uploadUVUs = 0;
  uint64_t drawUs = 0;
  uint64_t presentBlockUs = 0;
  const char* failStage = "none";
};

struct Nv12D3dRenderer {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
  Microsoft::WRL::ComPtr<ID3D11Buffer> uvConstants;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texY;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texUV;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srvY;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srvUV;
  uint32_t texW = 0;
  uint32_t texH = 0;
  UINT rtvW = 0;
  UINT rtvH = 0;
  uint64_t rtvCreateCount = 0;
  uint64_t rtvResizeCount = 0;
  bool ready = false;

  bool init(HWND hwnd) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL outLevel = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                   &device, &outLevel, &context);
    if (FAILED(hr)) {
      hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                             levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                             &device, &outLevel, &context);
      if (FAILED(hr)) return false;
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device.As(&dxgiDevice))) return false;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;
    Microsoft::WRL::ComPtr<IDXGIFactory> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    // Flip-discard presents by reference through DWM instead of blitting the whole frame;
    // the legacy discard model costs a full-frame copy per present. Falls back for the
    // rare pre-Win10 driver that rejects the flip model.
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(factory->CreateSwapChain(device.Get(), &sd, &swapChain))) {
      sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
      if (FAILED(factory->CreateSwapChain(device.Get(), &sd, &swapChain))) return false;
    }
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    static const char* kVsSrc =
        "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
        "VSOut main(uint id : SV_VertexID) {"
        "  float2 p = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? 3.0 : -1.0);"
        "  VSOut o;"
        "  o.pos = float4(p, 0, 1);"
        "  o.uv = float2((p.x + 1.0) * 0.5, 1.0 - ((p.y + 1.0) * 0.5));"
        "  return o;"
        "}";
    static const char* kPsSrc =
        "cbuffer FrameConstants : register(b0) { float4 uvRect; };"
        "Texture2D texY : register(t0);"
        "Texture2D texUV : register(t1);"
        "SamplerState smp : register(s0);"
        "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {"
        "  float2 sampleUv = uvRect.xy + uv * uvRect.zw;"
        "  float y = texY.Sample(smp, sampleUv).r;"
        "  float2 c = texUV.Sample(smp, sampleUv).rg;"
        "  float Y = max(0.0, y - 16.0 / 255.0);"
        "  float U = c.x - 128.0 / 255.0;"
        "  float V = c.y - 128.0 / 255.0;"
        // BT.709 limited range; must match bgra_to_nv12/nv12_to_bgra in mf_h264_codec.cpp.
        "  float r = 1.16438356 * Y + 1.79274107 * V;"
        "  float g = 1.16438356 * Y - 0.21324861 * U - 0.53290933 * V;"
        "  float b = 1.16438356 * Y + 2.11240178 * U;"
        "  return float4(saturate(r), saturate(g), saturate(b), 1.0);"
        "}";

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    if (FAILED(D3DCompile(kVsSrc, std::strlen(kVsSrc), nullptr, nullptr, nullptr,
                          "main", "vs_4_0", 0, 0, &vsBlob, &errBlob))) {
      return false;
    }
    if (FAILED(D3DCompile(kPsSrc, std::strlen(kPsSrc), nullptr, nullptr, nullptr,
                          "main", "ps_4_0", 0, 0, &psBlob, &errBlob))) {
      return false;
    }
    if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs))) {
      return false;
    }
    if (FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps))) {
      return false;
    }

    D3D11_SAMPLER_DESC sdSamp{};
    sdSamp.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sdSamp.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdSamp.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdSamp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdSamp.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sdSamp, &sampler))) return false;

    D3D11_BUFFER_DESC constantsDesc{};
    constantsDesc.ByteWidth = 16;
    constantsDesc.Usage = D3D11_USAGE_DEFAULT;
    constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&constantsDesc, nullptr, &uvConstants))) return false;

    ready = ensure_rtv(hwnd);
    return ready;
  }

  bool ensure_rtv(HWND hwnd) {
    if (!swapChain || !device) return false;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const UINT w = std::max<LONG>(1, rc.right - rc.left);
    const UINT h = std::max<LONG>(1, rc.bottom - rc.top);

    // The steady state is a cache hit: recreating the view every frame also re-queried the
    // swapchain descriptor every frame, all of it for a window that had not moved.
    if (rtv && rtvW == w && rtvH == h) return true;

    DXGI_SWAP_CHAIN_DESC sd{};
    if (FAILED(swapChain->GetDesc(&sd))) return false;
    if (sd.BufferDesc.Width != w || sd.BufferDesc.Height != h) {
      rtv.Reset();
      ++rtvResizeCount;
      if (FAILED(swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return false;
    if (FAILED(device->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv))) return false;
    ++rtvCreateCount;
    rtvW = w;
    rtvH = h;
    return true;
  }

  bool ensure_nv12_textures(uint32_t w, uint32_t h) {
    if (!device) return false;
    if (texY && texUV && texW == w && texH == h) return true;

    texY.Reset();
    texUV.Reset();
    srvY.Reset();
    srvUV.Reset();
    texW = 0;
    texH = 0;

    D3D11_TEXTURE2D_DESC yDesc{};
    yDesc.Width = w;
    yDesc.Height = h;
    yDesc.MipLevels = 1;
    yDesc.ArraySize = 1;
    yDesc.Format = DXGI_FORMAT_R8_UNORM;
    yDesc.SampleDesc.Count = 1;
    yDesc.Usage = D3D11_USAGE_DYNAMIC;
    yDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    yDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateTexture2D(&yDesc, nullptr, &texY))) return false;

    D3D11_TEXTURE2D_DESC uvDesc{};
    uvDesc.Width = w / 2;
    uvDesc.Height = h / 2;
    uvDesc.MipLevels = 1;
    uvDesc.ArraySize = 1;
    uvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    uvDesc.SampleDesc.Count = 1;
    uvDesc.Usage = D3D11_USAGE_DYNAMIC;
    uvDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    uvDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateTexture2D(&uvDesc, nullptr, &texUV))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC ySrvDesc{};
    ySrvDesc.Format = DXGI_FORMAT_R8_UNORM;
    ySrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    ySrvDesc.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(texY.Get(), &ySrvDesc, &srvY))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC uvSrvDesc{};
    uvSrvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    uvSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    uvSrvDesc.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(texUV.Get(), &uvSrvDesc, &srvUV))) return false;

    texW = w;
    texH = h;
    return true;
  }

  bool draw(HWND hwnd, const RECT& destRect, ID3D11ShaderResourceView* ySrv,
            ID3D11ShaderResourceView* uvSrv, const float uvRect[4],
            Nv12RenderTelemetry* telemetry) {
    if (!ensure_rtv(hwnd) || !ySrv || !uvSrv || !uvConstants) {
      if (telemetry) telemetry->failStage = "draw_args";
      return false;
    }
    context->UpdateSubresource(uvConstants.Get(), 0, nullptr, uvRect, 0, 0);
    RECT rc{};
    GetClientRect(hwnd, &rc);
    RECT drawRect = destRect;
    if (drawRect.right <= drawRect.left || drawRect.bottom <= drawRect.top) drawRect = rc;
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = static_cast<float>(drawRect.left);
    vp.TopLeftY = static_cast<float>(drawRect.top);
    vp.Width = static_cast<float>(std::max<LONG>(1, drawRect.right - drawRect.left));
    vp.Height = static_cast<float>(std::max<LONG>(1, drawRect.bottom - drawRect.top));
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    ID3D11RenderTargetView* rtvs[] = {rtv.Get()};
    context->OMSetRenderTargets(1, rtvs, nullptr);
    context->RSSetViewports(1, &vp);
    const float clearColor[4] = {0, 0, 0, 1};
    context->ClearRenderTargetView(rtv.Get(), clearColor);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->PSSetShader(ps.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[] = {ySrv, uvSrv};
    context->PSSetShaderResources(0, 2, srvs);
    ID3D11Buffer* constants[] = {uvConstants.Get()};
    context->PSSetConstantBuffers(0, 1, constants);
    ID3D11SamplerState* samplers[] = {sampler.Get()};
    context->PSSetSamplers(0, 1, samplers);
    const uint64_t drawStartUs = qpc_now_us();
    context->Draw(3, 0);
    ID3D11ShaderResourceView* nullSrvs[] = {nullptr, nullptr};
    context->PSSetShaderResources(0, 2, nullSrvs);
    const uint64_t drawEndUs = qpc_now_us();
    if (telemetry) telemetry->drawUs = drawEndUs - drawStartUs;

    const uint64_t presentStartUs = qpc_now_us();
    const HRESULT hr = swapChain->Present(0, 0);
    const uint64_t presentDoneUs = qpc_now_us();
    if (telemetry) telemetry->presentBlockUs = presentDoneUs - presentStartUs;
    if (!(SUCCEEDED(hr) || hr == DXGI_STATUS_OCCLUDED) && telemetry) telemetry->failStage = "present";
    return SUCCEEDED(hr) || hr == DXGI_STATUS_OCCLUDED;
  }

  bool render_surface(HWND hwnd, const RECT& destRect, ID3D11Texture2D* texture,
                      uint32_t subresource, uint32_t codedW, uint32_t codedH,
                      uint32_t visLeft, uint32_t visTop, uint32_t w, uint32_t h,
                      Nv12RenderTelemetry* telemetry) {
    if (telemetry) *telemetry = Nv12RenderTelemetry{};
    if (!ready || !texture || !codedW || !codedH || !w || !h) {
      if (telemetry) telemetry->failStage = "surface_args";
      return false;
    }
    D3D11_TEXTURE2D_DESC td{};
    texture->GetDesc(&td);
    if (td.Format != DXGI_FORMAT_NV12 || subresource >= td.MipLevels * td.ArraySize ||
        visLeft + w > codedW || visTop + h > codedH) {
      if (telemetry) telemetry->failStage = "surface_desc";
      return false;
    }
    Microsoft::WRL::ComPtr<ID3D11Device> textureDevice;
    texture->GetDevice(&textureDevice);
    if (textureDevice.Get() != device.Get()) {
      if (telemetry) telemetry->failStage = "surface_device";
      return false;
    }
    const UINT mipSlice = subresource % td.MipLevels;
    const UINT arraySlice = subresource / td.MipLevels;
    auto make_view = [&](DXGI_FORMAT format,
                         Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>* out) -> bool {
      D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
      desc.Format = format;
      if (td.ArraySize > 1) {
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MostDetailedMip = mipSlice;
        desc.Texture2DArray.MipLevels = 1;
        desc.Texture2DArray.FirstArraySlice = arraySlice;
        desc.Texture2DArray.ArraySize = 1;
      } else {
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MostDetailedMip = mipSlice;
        desc.Texture2D.MipLevels = 1;
      }
      return SUCCEEDED(device->CreateShaderResourceView(texture, &desc, out->ReleaseAndGetAddressOf()));
    };
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ySrv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> uvSrv;
    if (!make_view(DXGI_FORMAT_R8_UNORM, &ySrv) ||
        !make_view(DXGI_FORMAT_R8G8_UNORM, &uvSrv)) {
      if (telemetry) telemetry->failStage = "surface_srv";
      return false;
    }
    const float uvRect[4] = {static_cast<float>(visLeft) / codedW,
                             static_cast<float>(visTop) / codedH,
                             static_cast<float>(w) / codedW,
                             static_cast<float>(h) / codedH};
    return draw(hwnd, destRect, ySrv.Get(), uvSrv.Get(), uvRect, telemetry);
  }

  /**
   * Draws the visible rect (w x h at visLeft/visTop) out of a coded NV12 plane. The textures
   * are sized to the visible picture, so the shader never samples the coded padding rows --
   * uploading the full 1088-row plane stretched 8 garbage rows into a 1080p picture and
   * distorted the aspect by 0.74%.
   */
  bool render(HWND hwnd, const RECT& destRect, const uint8_t* nv12, uint32_t codedW,
              uint32_t codedH, uint32_t visLeft, uint32_t visTop, uint32_t w, uint32_t h,
              Nv12RenderTelemetry* telemetry) {
    if (telemetry) {
      *telemetry = Nv12RenderTelemetry{};
    }
    if (!ready || !nv12 || codedW == 0 || codedH == 0 || w == 0 || h == 0 || (codedW & 1u) ||
        (codedH & 1u) || (w & 1u) || (h & 1u) || (visLeft & 1u) || (visTop & 1u) ||
        visLeft + w > codedW || visTop + h > codedH) {
      if (telemetry) telemetry->failStage = "invalid_args";
      return false;
    }
    if (!ensure_rtv(hwnd)) {
      if (telemetry) telemetry->failStage = "ensure_rtv";
      return false;
    }
    if (!ensure_nv12_textures(w, h)) {
      if (telemetry) telemetry->failStage = "ensure_nv12_textures";
      return false;
    }

    const uint8_t* yPlane = nv12 + static_cast<size_t>(visTop) * codedW + visLeft;
    const uint8_t* uvPlane = nv12 + static_cast<size_t>(codedW) * codedH +
                             static_cast<size_t>(visTop / 2) * codedW + visLeft;

    const uint64_t uploadYStartUs = qpc_now_us();
    D3D11_MAPPED_SUBRESOURCE yMap{};
    if (FAILED(context->Map(texY.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &yMap))) {
      if (telemetry) telemetry->failStage = "map_y";
      return false;
    }
    if (codedW == w && static_cast<UINT>(w) == yMap.RowPitch) {
      std::memcpy(reinterpret_cast<uint8_t*>(yMap.pData), yPlane, static_cast<size_t>(h) * w);
    } else {
      for (uint32_t row = 0; row < h; ++row) {
        std::memcpy(reinterpret_cast<uint8_t*>(yMap.pData) + static_cast<size_t>(row) * yMap.RowPitch,
                    yPlane + static_cast<size_t>(row) * codedW, w);
      }
    }
    context->Unmap(texY.Get(), 0);
    if (telemetry) telemetry->uploadYUs = qpc_now_us() - uploadYStartUs;

    const uint64_t uploadUVStartUs = qpc_now_us();
    D3D11_MAPPED_SUBRESOURCE uvMap{};
    if (FAILED(context->Map(texUV.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &uvMap))) {
      if (telemetry) telemetry->failStage = "map_uv";
      return false;
    }
    const uint32_t uvHeight = h / 2;
    if (codedW == w && static_cast<UINT>(w) == uvMap.RowPitch) {
      std::memcpy(reinterpret_cast<uint8_t*>(uvMap.pData), uvPlane,
                  static_cast<size_t>(uvHeight) * w);
    } else {
      for (uint32_t row = 0; row < uvHeight; ++row) {
        std::memcpy(reinterpret_cast<uint8_t*>(uvMap.pData) + static_cast<size_t>(row) * uvMap.RowPitch,
                    uvPlane + static_cast<size_t>(row) * codedW, w);
      }
    }
    context->Unmap(texUV.Get(), 0);
    if (telemetry) telemetry->uploadUVUs = qpc_now_us() - uploadUVStartUs;

    const float uvRect[4] = {0, 0, 1, 1};
    return draw(hwnd, destRect, srvY.Get(), srvUV.Get(), uvRect, telemetry);
  }
};

Nv12D3dRenderer gNv12Renderer;


void enqueue_input_event(uint16_t kind, int32_t x, int32_t y, int32_t wheelDelta, uint32_t keyCode) {
  if (kInputPolicyForceBlock) return;
  if (!gInputEnabled.load()) return;
  QueuedControlInputMessage msg{};
  msg.type = MessageType::ControlInputEvent;
  msg.inputEvent.header.magic = remote60::native_poc::kMagic;
  msg.inputEvent.header.type = static_cast<uint16_t>(MessageType::ControlInputEvent);
  msg.inputEvent.header.size = static_cast<uint16_t>(sizeof(msg.inputEvent));
  msg.inputEvent.seq = gInputQueueState.NextSequence();
  msg.inputEvent.kind = kind;
  msg.inputEvent.buttons = gMouseButtons.load();
  msg.inputEvent.x = x;
  msg.inputEvent.y = y;
  msg.inputEvent.wheelDelta = wheelDelta;
  msg.inputEvent.keyCode = keyCode;
  msg.inputEvent.clientSendQpcUs = qpc_now_us();
  // Recording taps the send path, so the macro sees exactly what the host will see -- the
  // engine keeps pointer actions and drops keys on its own.
  if (gInputMacro.IsRecording()) {
    gInputMacro.RecordEvent(msg.inputEvent, GetTickCount64());
  }
  enqueue_control_input_message(msg);
}

/** A replayed step carries its own recorded button state instead of today's live one. */
void enqueue_macro_step(const remote60::native_poc::MacroStep& step) {
  if (kInputPolicyForceBlock) return;
  if (!gInputEnabled.load()) return;
  QueuedControlInputMessage msg{};
  msg.type = MessageType::ControlInputEvent;
  msg.inputEvent.header.magic = remote60::native_poc::kMagic;
  msg.inputEvent.header.type = static_cast<uint16_t>(MessageType::ControlInputEvent);
  msg.inputEvent.header.size = static_cast<uint16_t>(sizeof(msg.inputEvent));
  msg.inputEvent.seq = gInputQueueState.NextSequence();
  msg.inputEvent.kind = step.kind;
  msg.inputEvent.buttons = step.buttons;
  msg.inputEvent.x = step.x;
  msg.inputEvent.y = step.y;
  msg.inputEvent.wheelDelta = step.wheelDelta;
  msg.inputEvent.keyCode = step.keyCode;
  msg.inputEvent.clientSendQpcUs = qpc_now_us();
  enqueue_control_input_message(msg);
}

void toggle_macro_window(HWND owner) {
  remote60::native_poc::MacroWindowHooks hooks;
  hooks.macro = &gInputMacro;
  hooks.sendStep = [](const remote60::native_poc::MacroStep& step) { enqueue_macro_step(step); };
  remote60::native_poc::macro_window_toggle(GetModuleHandleW(nullptr), owner, hooks);
  if (owner) InvalidateRect(owner, nullptr, FALSE);
}


LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CLOSE:
      gRunning = false;
      if (gSock != INVALID_SOCKET) shutdown(gSock, SD_BOTH);
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      remote60::native_poc::session_toolbar_destroy();
      destroy_cached_gdi_objects();
      PostQuitMessage(0);
      return 0;
    case kMsgRevealStreamView: {
      // The video thread saw the first frame of a selection and posted this once. Revalidate
      // against the live selection state before committing: a cancel / new selection / disconnect
      // may have raced the post, and closing the picker then would be wrong. Require that the same
      // transaction is still pending, its ack is in, and the recorded epoch and generation still
      // match. Always release the latch at the end so a later legitimate first frame can re-post.
      const bool commit =
          gSelectionPending.load(std::memory_order_acquire) &&
          !gSelectionAwaitingAck.load(std::memory_order_acquire) &&
          gSelectionEpoch.load(std::memory_order_acquire) ==
              gSelectionReadyEpoch.load(std::memory_order_acquire) &&
          gSelectionExpectedGeneration.load(std::memory_order_acquire) ==
              gSelectionReadyGeneration.load(std::memory_order_acquire);
      if (commit) {
        // Persistent filter for late stragglers from the previous target (see the recv gate).
        gActiveStreamGeneration.store(gSelectionReadyGeneration.load(std::memory_order_acquire),
                                      std::memory_order_release);
        // Dropping the picker guard opens both the paint path and the input guard (input handlers
        // early-return while the picker is up); clearing pending re-enables the picker's buttons.
        gWindowPickerVisible.store(false, std::memory_order_relaxed);
        clear_pc_target_selection();
        remote60::native_poc::session_toolbar_set_visible(true);
        push_session_toolbar_state();
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      gSelectionRevealPosted.store(false, std::memory_order_release);
      return 0;
    }
    // The toolbar is a window of its own, so it does not move with this one for free.
    case WM_WINDOWPOSCHANGED:
      remote60::native_poc::session_toolbar_follow_owner();
      return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_DPICHANGED: {
      ensure_ui_font(hwnd);
      const RECT* suggested = reinterpret_cast<const RECT*>(lp);
      if (suggested) {
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      InvalidateRect(hwnd, nullptr, TRUE);
      return 0;
    }
    case WM_MOUSEMOVE:
      // The toolbar hides itself so it stops blocking clicks, which leaves it deaf: a hidden
      // window gets no mouse events, so this window watches for the summoning dwell for it.
      if (!gWindowPickerVisible.load(std::memory_order_relaxed)) {
        RECT toolbarZone{};
        GetClientRect(hwnd, &toolbarZone);
        remote60::native_poc::session_toolbar_notify_mouse(GET_X_LPARAM(lp), GET_Y_LPARAM(lp),
                                                           toolbarZone.right);
      }
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      if ((gMouseButtons.load(std::memory_order_relaxed) & 0x7u) == 0) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        enqueue_input_event(1, vx, vy, 0, 0);
      }
      return 0;
    case WM_LBUTTONDOWN:
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
        gWindowPickerToggleDown.store(true, std::memory_order_relaxed);
        return 0;
      }
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
        gMacroButtonDown.store(true, std::memory_order_relaxed);
        return 0;
      }
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) {
        if (gSelectionPending.load(std::memory_order_acquire)) {
          gPickerPressTargetId.store(kPickerPressNone, std::memory_order_relaxed);
          return 0;
        }
        // Remember which target (if any) this press started on; the UP handler only selects when
        // it ends on the same one. A press on empty picker space latches "none", and so does a
        // press within the first 300ms after the picker appeared -- the gesture must START after
        // the picker is stable, or a long-press begun against the old screen could still select.
        const int dx = GET_X_LPARAM(lp);
        const int dy = GET_Y_LPARAM(lp);
        const ClientLayout downLayout = compute_client_layout(hwnd);
        uint64_t pressedId = kPickerPressNone;
        uint64_t hitId = 0;
        if (point_in_rect(downLayout.desktopButtonRect, dx, dy)) {
          pressedId = 0;
        } else if (try_hit_window_list_item(hwnd, dx, dy, &hitId)) {
          pressedId = hitId;
        }
        if (qpc_now_us() <
            gPickerShownAtUs.load(std::memory_order_relaxed) + kPickerSelectMinShownUs) {
          pressedId = kPickerPressNone;
        }
        gPickerPressTargetId.store(pressedId, std::memory_order_relaxed);
        return 0;
      }
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      SetFocus(hwnd);
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        SetCapture(hwnd);
        gMouseButtons.fetch_or(1);
        enqueue_input_event(2, vx, vy, 0, VK_LBUTTON);
      }
      return 0;
    case WM_LBUTTONUP: {
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      const int x = GET_X_LPARAM(lp);
      const int y = GET_Y_LPARAM(lp);
      const ClientLayout layout = compute_client_layout(hwnd);
      if (gWindowPickerToggleDown.exchange(false, std::memory_order_relaxed)) {
        if (point_in_rect(layout.toggleButtonRect, x, y)) {
          set_picker_visible_and_sync_stream(
              !gWindowPickerVisible.load(std::memory_order_relaxed));
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (gMacroButtonDown.exchange(false, std::memory_order_relaxed)) {
        if (point_in_rect(layout.macroButtonRect, x, y)) {
          toggle_macro_window(hwnd);
        }
        return 0;
      }
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) {
        // Consume the press latch FIRST, unconditionally: any UP ends the gesture, and an early
        // return below must not leave a stale latch to approve a later unrelated UP.
        const uint64_t pressedId =
            gPickerPressTargetId.exchange(kPickerPressNone, std::memory_order_relaxed);
        // A selection already in flight owns the picker until its first frame arrives; ignore
        // further target clicks so a double-click cannot queue a second, racing select.
        if (gSelectionPending.load(std::memory_order_acquire)) return 0;
        if (point_in_rect(layout.refreshButtonRect, x, y)) {
          queue_window_list_request("window_list_request pending");
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        // Mis-click guard: selecting needs a picker that has been up for a moment (a click begun
        // before it appeared must not land on a card) AND a DOWN that started on the same target.
        const uint64_t shownAtUs = gPickerShownAtUs.load(std::memory_order_relaxed);
        const uint64_t nowUs = qpc_now_us();
        if (nowUs < shownAtUs + kPickerSelectMinShownUs) return 0;
        const uint64_t shownAgeMs = (nowUs - shownAtUs) / 1000;
        if (point_in_rect(layout.desktopButtonRect, x, y)) {
          if (pressedId != 0) return 0;
          // Explicit WindowSelect(0) even when desktop is already the selected target: one clean
          // restart with a fresh generation, so the first-frame gate has something to wait on.
          if (begin_pc_target_selection(0, "desktop_select_requested")) {
            std::cout << "[native-video-client][picker] select source=mouse x=" << x << " y=" << y
                      << " id=0 shownAgeMs=" << shownAgeMs << "\n";
            InvalidateRect(hwnd, nullptr, FALSE);
          }
          return 0;
        }
        uint64_t hitWindowId = 0;
        if (try_hit_window_list_item(hwnd, x, y, &hitWindowId)) {
          if (pressedId != hitWindowId) return 0;
          if (begin_pc_target_selection(hitWindowId, "window_select_requested")) {
            std::cout << "[native-video-client][picker] select source=mouse x=" << x << " y=" << y
                      << " id=" << hitWindowId << " shownAgeMs=" << shownAgeMs << "\n";
            InvalidateRect(hwnd, nullptr, FALSE);
          }
          return 0;
        }
        return 0;
      }
      if (point_in_rect(layout.refreshButtonRect, x, y)) {
        queue_window_list_request("window_list_request pending");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (point_in_rect(layout.desktopButtonRect, x, y)) {
        queue_window_select_request(0, "desktop_select_requested");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      uint64_t hitWindowId = 0;
      if (try_hit_window_list_item(hwnd, x, y, &hitWindowId)) {
        queue_window_select_request(hitWindowId, "window_select_requested");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (point_in_panel_ui(hwnd, x, y)) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, x, y, &vx, &vy)) return 0;
        gMouseButtons.fetch_and(static_cast<uint16_t>(~1u));
        enqueue_input_event(3, vx, vy, 0, VK_LBUTTON);
        release_mouse_capture_if_idle(hwnd);
      }
      return 0;
    }
    case WM_RBUTTONDOWN:
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        SetCapture(hwnd);
        gMouseButtons.fetch_or(2);
        enqueue_input_event(2, vx, vy, 0, VK_RBUTTON);
      }
      return 0;
    case WM_RBUTTONUP:
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        gMouseButtons.fetch_and(static_cast<uint16_t>(~2u));
        enqueue_input_event(3, vx, vy, 0, VK_RBUTTON);
        release_mouse_capture_if_idle(hwnd);
      }
      return 0;
    case WM_MBUTTONDOWN:
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        SetCapture(hwnd);
        gMouseButtons.fetch_or(4);
        enqueue_input_event(2, vx, vy, 0, VK_MBUTTON);
      }
      return 0;
    case WM_MBUTTONUP:
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      if (point_in_toggle_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (point_in_macro_button(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) return 0;
      if (point_in_panel_ui(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
      if (kInputPolicyForceBlock) return 0;
      {
        int32_t vx = 0;
        int32_t vy = 0;
        if (!map_client_point_to_video_coords(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &vx, &vy)) return 0;
        gMouseButtons.fetch_and(static_cast<uint16_t>(~4u));
        enqueue_input_event(3, vx, vy, 0, VK_MBUTTON);
        release_mouse_capture_if_idle(hwnd);
      }
      return 0;
    case WM_MOUSEWHEEL: {
      if (qpc_now_us() < gSuppressMouseUntilUs.load(std::memory_order_relaxed)) return 0;
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &p);
      const ClientLayout layout = compute_client_layout(hwnd);
      if (point_in_rect(layout.toggleButtonRect, p.x, p.y)) return 0;
      if (point_in_rect(layout.macroButtonRect, p.x, p.y)) return 0;
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) {
        if (point_in_rect(layout.listRect, p.x, p.y)) {
          const int wheel = GET_WHEEL_DELTA_WPARAM(wp);
          scroll_window_list(hwnd, (wheel < 0) ? 1 : -1);
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (point_in_rect(layout.listRect, p.x, p.y)) {
        const int wheel = GET_WHEEL_DELTA_WPARAM(wp);
        scroll_window_list(hwnd, (wheel < 0) ? 1 : -1);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (point_in_panel_ui(hwnd, p.x, p.y)) return 0;
      if (kInputPolicyForceBlock) return 0;
      int32_t vx = 0;
      int32_t vy = 0;
      if (!map_client_point_to_video_coords(hwnd, p.x, p.y, &vx, &vy)) return 0;
      enqueue_input_event(4, vx, vy, GET_WHEEL_DELTA_WPARAM(wp), 0);
      return 0;
    }
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP: {
      UINT32 pointerId = GET_POINTERID_WPARAM(wp);
      POINTER_INPUT_TYPE pointerType = PT_POINTER;
      if (!GetPointerType(pointerId, &pointerType) || pointerType != PT_TOUCH) {
        return DefWindowProcW(hwnd, msg, wp, lp);
      }
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &p);
      gSuppressMouseUntilUs.store(qpc_now_us() + 300000ULL, std::memory_order_relaxed);
      const ClientLayout layout = compute_client_layout(hwnd);
      if (point_in_rect(layout.toggleButtonRect, p.x, p.y)) {
        if (msg == WM_POINTERDOWN) {
          gWindowPickerToggleDown.store(true, std::memory_order_relaxed);
        } else if (msg == WM_POINTERUP && gWindowPickerToggleDown.exchange(false, std::memory_order_relaxed)) {
          set_picker_visible_and_sync_stream(
              !gWindowPickerVisible.load(std::memory_order_relaxed));
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
      if (point_in_rect(layout.macroButtonRect, p.x, p.y)) {
        if (msg == WM_POINTERDOWN) {
          gMacroButtonDown.store(true, std::memory_order_relaxed);
        } else if (msg == WM_POINTERUP &&
                   gMacroButtonDown.exchange(false, std::memory_order_relaxed)) {
          toggle_macro_window(hwnd);
        }
        return 0;
      }
      if (gWindowPickerVisible.load(std::memory_order_relaxed)) {
        // A selection in flight owns the picker; also clear the latch so a gesture spanning the
        // pending window cannot leave a stale press behind.
        if (gSelectionPending.load(std::memory_order_acquire)) {
          gPickerPressTargetId.store(kPickerPressNone, std::memory_order_relaxed);
          return 0;
        }
        if (msg == WM_POINTERDOWN) {
          // Same DOWN/UP-on-the-same-target latch as the mouse path, including the "gesture must
          // start after the picker is 300ms stable" rule.
          uint64_t pressedId = kPickerPressNone;
          uint64_t hitId = 0;
          if (point_in_rect(layout.desktopButtonRect, p.x, p.y)) {
            pressedId = 0;
          } else if (try_hit_window_list_item(hwnd, p.x, p.y, &hitId)) {
            pressedId = hitId;
          }
          if (qpc_now_us() <
              gPickerShownAtUs.load(std::memory_order_relaxed) + kPickerSelectMinShownUs) {
            pressedId = kPickerPressNone;
          }
          gPickerPressTargetId.store(pressedId, std::memory_order_relaxed);
          return 0;
        }
        if (msg == WM_POINTERUP) {
          const uint64_t pressedId =
              gPickerPressTargetId.exchange(kPickerPressNone, std::memory_order_relaxed);
          const uint64_t shownAtUs = gPickerShownAtUs.load(std::memory_order_relaxed);
          const uint64_t nowUs = qpc_now_us();
          const bool shownLongEnough = nowUs >= shownAtUs + kPickerSelectMinShownUs;
          const uint64_t shownAgeMs = (nowUs - shownAtUs) / 1000;
          if (point_in_rect(layout.refreshButtonRect, p.x, p.y)) {
            queue_window_list_request("window_list_request pending");
            InvalidateRect(hwnd, nullptr, FALSE);
          } else if (point_in_rect(layout.desktopButtonRect, p.x, p.y)) {
            if (shownLongEnough && pressedId == 0 &&
                begin_pc_target_selection(0, "desktop_select_requested")) {
              std::cout << "[native-video-client][picker] select source=touch x=" << p.x
                        << " y=" << p.y << " id=0 shownAgeMs=" << shownAgeMs << "\n";
              InvalidateRect(hwnd, nullptr, FALSE);
            }
          } else {
            uint64_t hitWindowId = 0;
            if (try_hit_window_list_item(hwnd, p.x, p.y, &hitWindowId)) {
              if (shownLongEnough && pressedId == hitWindowId &&
                  begin_pc_target_selection(hitWindowId, "window_select_requested")) {
                std::cout << "[native-video-client][picker] select source=touch x=" << p.x
                          << " y=" << p.y << " id=" << hitWindowId
                          << " shownAgeMs=" << shownAgeMs << "\n";
                InvalidateRect(hwnd, nullptr, FALSE);
              }
            }
          }
        }
        return 0;
      }
      if (point_in_panel_ui(hwnd, p.x, p.y)) return 0;
      int32_t vx = 0;
      int32_t vy = 0;
      if (!map_client_point_to_video_coords(hwnd, p.x, p.y, &vx, &vy)) return 0;
      if (msg == WM_POINTERDOWN) {
        if (gActiveTouchDown.load(std::memory_order_relaxed)) return 0;
        SetFocus(hwnd);
        SetCapture(hwnd);
        gActiveTouchPointerId.store(pointerId, std::memory_order_relaxed);
        gActiveTouchDown.store(true, std::memory_order_relaxed);
        gMouseButtons.fetch_or(1);
        enqueue_input_event(2, vx, vy, 0, VK_LBUTTON);
      } else if (msg == WM_POINTERUPDATE) {
        if (!gActiveTouchDown.load(std::memory_order_relaxed) ||
            gActiveTouchPointerId.load(std::memory_order_relaxed) != pointerId) {
          return 0;
        }
        enqueue_input_event(1, vx, vy, 0, 0);
      } else {
        if (!gActiveTouchDown.load(std::memory_order_relaxed) ||
            gActiveTouchPointerId.load(std::memory_order_relaxed) != pointerId) {
          return 0;
        }
        gMouseButtons.fetch_and(static_cast<uint16_t>(~1u));
        gActiveTouchDown.store(false, std::memory_order_relaxed);
        gActiveTouchPointerId.store(0, std::memory_order_relaxed);
        enqueue_input_event(3, vx, vy, 0, VK_LBUTTON);
        release_mouse_capture_if_idle(hwnd);
      }
      return 0;
    }
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
    case WM_POINTERCAPTURECHANGED:
      enqueue_release_for_pressed_mouse_buttons();
      gActiveTouchDown.store(false, std::memory_order_relaxed);
      gActiveTouchPointerId.store(0, std::memory_order_relaxed);
      // A gesture that lost capture mid-flight must not leave a stale picker press behind: the
      // whole point of the latch is that an UP without its own valid DOWN selects nothing.
      gPickerPressTargetId.store(kPickerPressNone, std::memory_order_relaxed);
      return 0;
    case WM_IME_SETCONTEXT: {
      const LPARAM masked =
          lp & ~(static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 0) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 1) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 2) |
                 static_cast<LPARAM>(ISC_SHOWUICANDIDATEWINDOW << 3) |
                 static_cast<LPARAM>(ISC_SHOWUIGUIDELINE));
      return DefWindowProcW(hwnd, msg, wp, masked);
    }
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_CHAR:
      return 0;
    case WM_IME_COMPOSITION:
      if (kInputPolicyForceBlock) return 0;
      (void)send_ime_result_text(hwnd, lp);
      return 0;
    case WM_KEYDOWN:
      if (local_hotkey_modifiers_active() && wp == VK_F5) {
        queue_window_list_request("window_list_request pending");
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (local_hotkey_modifiers_active() && wp == VK_F9) {
        request_capture_overview_mode();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (local_hotkey_modifiers_active() && wp == VK_OEM_4) {  // [
        apply_runtime_tune_delta(-1, 0);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (local_hotkey_modifiers_active() && wp == VK_OEM_6) {  // ]
        apply_runtime_tune_delta(1, 0);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (local_hotkey_modifiers_active() && wp == VK_OEM_1) {  // ;
        apply_runtime_tune_delta(0, -1);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (local_hotkey_modifiers_active() && wp == VK_OEM_7) {  // '
        apply_runtime_tune_delta(0, 1);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (kInputPolicyForceBlock) return 0;
      if (forward_key_down(wp)) enqueue_input_event(5, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_KEYUP:
      if (kInputPolicyForceBlock) return 0;
      if (forward_key_up(wp)) enqueue_input_event(6, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_SYSKEYDOWN:
      if (kInputPolicyForceBlock) return 0;
      if (forward_key_down(wp)) enqueue_input_event(5, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_SYSKEYUP:
      if (kInputPolicyForceBlock) return 0;
      if (forward_key_up(wp)) enqueue_input_event(6, 0, 0, 0, static_cast<uint32_t>(wp));
      return 0;
    case WM_KILLFOCUS:
      // Focus is about to leave, so no more key-ups will reach this window. Release whatever
      // is held now, before Alt/Win/Alt+Tab strands it on the host.
      if (!kInputPolicyForceBlock) enqueue_release_for_pressed_keys();
      gPickerPressTargetId.store(kPickerPressNone, std::memory_order_relaxed);
      return 0;
    case WM_CHAR:
      // Ignored on purpose. Every physical key now travels the key-event path, and IME
      // composition results travel the text path from WM_IME_COMPOSITION. Emitting text here
      // too would double every printable -- and it was this handler's IME-suppression
      // bookkeeping, drifting after a Hangul commit, that swallowed digits and space. With the
      // two paths cleanly split, there is nothing left for WM_CHAR to do.
      return 0;
    case WM_SYSCHAR:
      return 0;
    case WM_ERASEBKGND:
      // Avoid background erase flicker between frames.
      return 1;
    case WM_TIMER:
      if (wp == kCursorOverlayTimerId) {
        update_cursor_overlay(hwnd);
        return 0;
      }
      break;
    case WM_PAINT: {
      gPaintQueued = false;
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      const uint64_t paintStartUs = qpc_now_us();
      const ClientLayout layout = compute_client_layout(hwnd);
      const RECT& videoRect = layout.videoRect;
      const RECT contentRect = resolve_video_content_rect(hwnd, videoRect);
      const bool pickerVisible = gWindowPickerVisible.load(std::memory_order_relaxed);
      static bool hasPresentedAtLeastOneFrame = false;

      std::shared_ptr<std::vector<uint8_t>> local;
      Microsoft::WRL::ComPtr<IMFSample> localSurfaceSample;
      Microsoft::WRL::ComPtr<ID3D11Texture2D> localSurfaceTexture;
      uint32_t localSurfaceSubresource = 0;
      SharedFrame::PixelFormat localFormat = SharedFrame::PixelFormat::Unknown;
      uint32_t w = 0, h = 0;
      uint32_t codedW = 0, codedH = 0;
      uint32_t visL = 0, visT = 0;
      uint32_t seq = 0;
      bool frameKey = false;
      uint64_t frameStreamGeneration = 0;
      uint64_t captureUs = 0;
      uint64_t encodeStartUs = 0;
      uint64_t encodeEndUs = 0;
      uint64_t sendUs = 0;
      uint64_t recvUs = 0;
      uint64_t decodeStartUs = 0;
      uint64_t decodeEndUs = 0;
      uint64_t queueSetUs = 0;
      uint64_t decodeToQueueUs = 0;
      uint64_t frameVersion = 0;
      {
        std::lock_guard<std::mutex> lk(gFrame.mu);
        if ((gFrame.bytes && !gFrame.bytes->empty()) || gFrame.surfaceTexture) {
          local = gFrame.bytes;
          localSurfaceSample = gFrame.surfaceSample;
          localSurfaceTexture = gFrame.surfaceTexture;
          localSurfaceSubresource = gFrame.surfaceSubresource;
          localFormat = gFrame.format;
          w = gFrame.width;
          h = gFrame.height;
          codedW = (gFrame.codedWidth > 0) ? gFrame.codedWidth : gFrame.width;
          codedH = (gFrame.codedHeight > 0) ? gFrame.codedHeight : gFrame.height;
          visL = gFrame.visibleLeft;
          visT = gFrame.visibleTop;
          seq = gFrame.seq;
          frameKey = gFrame.key;
          frameStreamGeneration = gFrame.streamGeneration;
          captureUs = gFrame.captureUs;
          encodeStartUs = gFrame.encodeStartUs;
          encodeEndUs = gFrame.encodeEndUs;
          sendUs = gFrame.sendUs;
          recvUs = gFrame.recvUs;
          decodeStartUs = gFrame.decodeStartUs;
          decodeEndUs = gFrame.decodeEndUs;
          queueSetUs = gFrame.queueSetUs;
          decodeToQueueUs = gFrame.decodeToQueueUs;
          frameVersion = gFrame.version;
        }
      }
      bool presented = false;
      Nv12RenderTelemetry renderTelemetry{};
      const char* renderPath = "none";
      const char* fallbackReason = "none";
      if (!pickerVisible && (local || localSurfaceTexture) && w > 0 && h > 0) {
        if (localFormat == SharedFrame::PixelFormat::Nv12) {
          if (!gNv12Renderer.ready) {
            if (!gNv12Renderer.init(hwnd)) {
              ++gD3dPresentFailCount;
              ++gFallbackInitFailCount;
              fallbackReason = "d3d_init_fail";
            }
          }
          if (gNv12Renderer.ready) {
            if (localSurfaceTexture) {
              presented = gNv12Renderer.render_surface(
                  hwnd, contentRect, localSurfaceTexture.Get(), localSurfaceSubresource,
                  codedW, codedH, visL, visT, w, h, &renderTelemetry);
            } else {
              presented = gNv12Renderer.render(hwnd, contentRect, local->data(), codedW, codedH,
                                               visL, visT, w, h, &renderTelemetry);
            }
            if (presented) {
              ++gD3dPresentSuccessCount;
              renderPath = localSurfaceTexture ? "d3d_nv12_surface" : "d3d_nv12";
            } else {
              ++gD3dPresentFailCount;
              ++gFallbackRenderFailCount;
              fallbackReason = renderTelemetry.failStage;
            }
          }
          if (!presented && local) {
            std::vector<uint8_t> bgra;
            if (nv12_to_bgra(local->data(), codedW, codedH, &bgra) && !bgra.empty()) {
              // The DIB carries the coded plane; the source rect and a row-offset base
              // pointer select only the visible picture out of it.
              BITMAPINFO bmi{};
              bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
              bmi.bmiHeader.biWidth = static_cast<LONG>(codedW);
              bmi.bmiHeader.biHeight = -static_cast<LONG>(h);
              bmi.bmiHeader.biPlanes = 1;
              bmi.bmiHeader.biBitCount = 32;
              bmi.bmiHeader.biCompression = BI_RGB;
              SetStretchBltMode(hdc, COLORONCOLOR);
              FillRect(hdc, &videoRect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
              StretchDIBits(hdc, contentRect.left, contentRect.top,
                            contentRect.right - contentRect.left, contentRect.bottom - contentRect.top,
                            static_cast<int>(visL), 0, static_cast<int>(w), static_cast<int>(h),
                            bgra.data() + static_cast<size_t>(visT) * codedW * 4, &bmi,
                            DIB_RGB_COLORS, SRCCOPY);
              presented = true;
              ++gGdiFallbackPresentedCount;
              renderPath = "gdi_nv12_fallback";
            } else {
              ++gFallbackNv12ConvertFailCount;
              fallbackReason = "nv12_to_bgra_fail";
            }
          }
        } else if (localFormat == SharedFrame::PixelFormat::Bgra32) {
          BITMAPINFO bmi{};
          bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
          bmi.bmiHeader.biWidth = static_cast<LONG>(w);
          bmi.bmiHeader.biHeight = -static_cast<LONG>(h);  // top-down
          bmi.bmiHeader.biPlanes = 1;
          bmi.bmiHeader.biBitCount = 32;
          bmi.bmiHeader.biCompression = BI_RGB;
          SetStretchBltMode(hdc, COLORONCOLOR);
          FillRect(hdc, &videoRect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
          StretchDIBits(hdc, contentRect.left, contentRect.top,
                        contentRect.right - contentRect.left, contentRect.bottom - contentRect.top,
                        0, 0, static_cast<int>(w), static_cast<int>(h),
                        local->data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
          presented = true;
          renderPath = "gdi_bgra";
        }
      }
      if (presented) {
        hasPresentedAtLeastOneFrame = true;
        static uint64_t lastPresentUs = 0;
        static uint64_t lastUserFeedbackUs = 0;
        static uint64_t lastUserFeedbackOverwrite = 0;
        gLastPresentedVersion.store(frameVersion, std::memory_order_relaxed);
        gLastPresentedCaptureUs.store(captureUs, std::memory_order_relaxed);
        const uint64_t presentUs = qpc_now_us();
        const uint64_t presentGapUs = (lastPresentUs > 0) ? (presentUs - lastPresentUs) : 0;
        const uint64_t queueToPaintUs = (paintStartUs >= queueSetUs) ? (paintStartUs - queueSetUs) : 0;
        const uint64_t queueToPresentUs = (presentUs >= paintStartUs) ? (presentUs - paintStartUs) : 0;
        const uint32_t traceEvery = gTraceEvery.load();
        const uint32_t traceMax = gTraceMax.load();
        if (traceEvery > 0 && (seq % traceEvery) == 0 &&
            (traceMax == 0 || gTracePresentPrinted.load() < traceMax)) {
          const auto nowPrinted = gTracePresentPrinted.fetch_add(1) + 1;
          if (traceMax == 0 || nowPrinted <= traceMax) {
            const uint64_t netUs = (recvUs >= sendUs) ? (recvUs - sendUs) : 0;
            const uint64_t c2eUs = (encodeStartUs >= captureUs) ? (encodeStartUs - captureUs) : 0;
            const uint64_t encUs = (encodeEndUs >= encodeStartUs) ? (encodeEndUs - encodeStartUs) : 0;
            const uint64_t e2sUs = (sendUs >= encodeEndUs) ? (sendUs - encodeEndUs) : 0;
            const uint64_t r2dUs = (decodeStartUs >= recvUs) ? (decodeStartUs - recvUs) : 0;
            const uint64_t decUs = (decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0;
            const uint64_t d2pUs = (presentUs >= decodeEndUs) ? (presentUs - decodeEndUs) : 0;
            const uint64_t renderUs = (presentUs >= recvUs) ? (presentUs - recvUs) : 0;
            const uint64_t queueWaitUs = (paintStartUs >= queueSetUs) ? (paintStartUs - queueSetUs) : 0;
            const uint64_t paintUs = (presentUs >= paintStartUs) ? (presentUs - paintStartUs) : 0;
            const uint64_t totalUs = (presentUs >= captureUs) ? (presentUs - captureUs) : 0;
            std::ostringstream oss;
            oss << "[native-video-client][trace_present] seq=" << seq
                << " captureUs=" << captureUs
                << " encodeStartUs=" << encodeStartUs
                << " encodeEndUs=" << encodeEndUs
                << " sendUs=" << sendUs
                << " recvUs=" << recvUs
                << " decodeStartUs=" << decodeStartUs
                << " decodeEndUs=" << decodeEndUs
                << " presentUs=" << presentUs
                << " c2eUs=" << c2eUs
                << " encUs=" << encUs
                << " e2sUs=" << e2sUs
                << " netUs=" << netUs
                << " r2dUs=" << r2dUs
                << " decUs=" << decUs
                << " d2pUs=" << d2pUs
                << " decodeToQueueUs=" << decodeToQueueUs
                << " queueWaitUs=" << queueWaitUs
                << " paintUs=" << paintUs
                << " uploadYUs=" << renderTelemetry.uploadYUs
                << " uploadUVUs=" << renderTelemetry.uploadUVUs
                << " drawUs=" << renderTelemetry.drawUs
                << " presentBlockUs=" << renderTelemetry.presentBlockUs
                << " renderUs=" << renderUs
                << " totalUs=" << totalUs
                << " renderPath=" << renderPath
                << " fallbackReason=" << fallbackReason;
            log_client_line(oss.str());
          }
        }
        // Emitted for every present, not only the ones that crossed a warning threshold.
        // Smoothness is a property of the whole interval distribution: a stream can average a
        // clean 30fps while alternating 16ms and 50ms gaps, which is exactly what a viewer
        // reports as stutter. Gating this behind the warning thresholds left the aggregate
        // reading zero through visibly uneven playback, so there was nothing to optimise
        // against.
        if (lastPresentUs > 0) {
          std::ostringstream gapLine;
          gapLine << "[native-video-client][present] seq=" << seq
                  << " frameGapUs=" << presentGapUs;
          log_client_line(gapLine.str());
        }
        const uint64_t totalUs = (presentUs >= captureUs) ? (presentUs - captureUs) : 0;
        // GNLink stream telemetry (diagnostics only): one line per presented keyframe, plus any
        // non-key frame whose present interval jumped past 1.5x the expected cadence -- the client
        // side of a periodic stutter. Joins the host 'wire seq=' log by seq+gen; steady play stays
        // quiet. This only observes the timestamps the present path already produced.
        {
          const uint32_t expIntervalUs = gPresentFrameIntervalUs.load(std::memory_order_relaxed);
          const uint64_t anomalyGapUs =
              (expIntervalUs > 0) ? (static_cast<uint64_t>(expIntervalUs) * 3ULL) / 2ULL : 25000ULL;
          const bool presentAnomaly = (lastPresentUs > 0 && presentGapUs > anomalyGapUs);
          if (frameKey || presentAnomaly) {
            uint64_t presentBacklog = 0;
            {
              std::lock_guard<std::mutex> lk(gFrame.mu);
              presentBacklog = (gFrame.version >= frameVersion) ? (gFrame.version - frameVersion) : 0;
            }
            std::ostringstream telem;
            telem << "[native-video-client][telemetry] stage=present"
                  << " seq=" << seq
                  << " gen=" << frameStreamGeneration
                  << " key=" << (frameKey ? 1 : 0)
                  << " presentUs=" << presentUs
                  << " presentedIntervalUs=" << presentGapUs
                  << " presentBacklog=" << presentBacklog
                  << " paintUs=" << queueToPresentUs
                  << " totalUs=" << totalUs;
            log_client_line(telem.str());
          }
        }
        if ((totalUs >= kUserFeedbackLagWarnUs || (presentGapUs >= kUserFeedbackGapWarnUs && lastPresentUs > 0)) &&
            (presentUs >= lastUserFeedbackUs + kUserFeedbackMinIntervalUs || lastUserFeedbackUs == 0)) {
          const uint64_t overwriteCountNow = gOverwriteBeforePresentCount.load(std::memory_order_relaxed);
          const uint64_t overwriteDelta = (overwriteCountNow >= lastUserFeedbackOverwrite)
                                             ? (overwriteCountNow - lastUserFeedbackOverwrite)
                                             : 0;
          const uint64_t d3dSuccess = gD3dPresentSuccessCount.load(std::memory_order_relaxed);
          const uint64_t d3dFail = gD3dPresentFailCount.load(std::memory_order_relaxed);
          const uint64_t gdiFallback = gGdiFallbackPresentedCount.load(std::memory_order_relaxed);
          const uint64_t paintCoalesced = gPaintCoalescedCount.load(std::memory_order_relaxed);
          const uint64_t queueWaitUs = (paintStartUs >= queueSetUs) ? (paintStartUs - queueSetUs) : 0;
          const uint64_t paintUs = (presentUs >= paintStartUs) ? (presentUs - paintStartUs) : 0;
          const uint64_t netUs = (recvUs >= sendUs) ? (recvUs - sendUs) : 0;
          const uint64_t c2eUs = (encodeStartUs >= captureUs) ? (encodeStartUs - captureUs) : 0;
          const uint64_t encUs = (encodeEndUs >= encodeStartUs) ? (encodeEndUs - encodeStartUs) : 0;
          const uint64_t e2sUs = (sendUs >= encodeEndUs) ? (sendUs - encodeEndUs) : 0;
          const uint64_t r2dUs = (decodeStartUs >= recvUs) ? (decodeStartUs - recvUs) : 0;
          const uint64_t decUs = (decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0;
          const uint64_t d2pUs = (presentUs >= decodeEndUs) ? (presentUs - decodeEndUs) : 0;
          std::ostringstream oss;
          oss << "[native-video-client][user-feedback] seq=" << seq
              << " totalUs=" << totalUs
              << " capGapUs=" << presentGapUs
              << " queueToPaintUs=" << queueToPaintUs
              << " queueToPresentUs=" << queueToPresentUs
              << " d3dPresentSuccess=" << d3dSuccess
              << " d3dPresentFail=" << d3dFail
              << " gdiFallback=" << gdiFallback
              << " paintCoalesced=" << paintCoalesced
              << " overwriteDelta=" << overwriteDelta
              << " c2eUs=" << c2eUs
              << " encUs=" << encUs
              << " e2sUs=" << e2sUs
              << " netUs=" << netUs
              << " r2dUs=" << r2dUs
              << " decUs=" << decUs
              << " d2pUs=" << d2pUs
              << " decodeToQueueUs=" << decodeToQueueUs
              << " queueWaitUs=" << queueWaitUs
              << " paintUs=" << paintUs
              << " presentBlockUs=" << renderTelemetry.presentBlockUs
              << " renderPath=" << renderPath
              << " fallbackReason=" << fallbackReason;
          log_client_line(oss.str());
          lastUserFeedbackUs = presentUs;
          lastUserFeedbackOverwrite = overwriteCountNow;
        }
        lastPresentUs = presentUs;
      } else if (pickerVisible || !hasPresentedAtLeastOneFrame) {
        // Before first successful frame, keep a deterministic background.
        FillRect(hdc, &videoRect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
      }
      draw_overlay(hdc);
      EndPaint(hwnd, &ps);
      uint64_t latestVersion = 0;
      {
        std::lock_guard<std::mutex> lk(gFrame.mu);
        latestVersion = gFrame.version;
      }
      if (!pickerVisible && latestVersion != frameVersion) {
        if (!gPaintQueued.exchange(true)) {
          InvalidateRect(hwnd, nullptr, FALSE);
        } else {
          ++gPaintCoalescedCount;
        }
      }
      return 0;
    }
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

// Remote-cursor overlay: a small layered, click-through popup owned by the video window. GDI
// drawn over a flip-model swapchain does not compose reliably, so the cursor lives in its own
// window that just moves. Content is a blue ring with a center dot (a deliberately distinct
// marker -- a second arrow would ghost behind the local one by an RTT), rasterized once.
void ensure_cursor_overlay(HWND owner) {
  if (gCursorOverlayHwnd) return;
  HINSTANCE inst = GetModuleHandle(nullptr);
  static bool registered = false;
  const wchar_t* cls = L"Remote60CursorOverlay";
  if (!registered) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = inst;
    wc.lpszClassName = cls;
    if (!RegisterClassExW(&wc)) return;
    registered = true;
  }
  constexpr int kSize = kCursorOverlaySize;
  gCursorOverlayHwnd = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, cls, L"",
      WS_POPUP, 0, 0, kSize, kSize, owner, nullptr, inst, nullptr);
  if (!gCursorOverlayHwnd) return;
  // Rasterize the arrow into a premultiplied 32bpp DIB: GDI writes alpha 0, so pixels that got
  // color are promoted to opaque afterwards; untouched pixels stay fully transparent.
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = kSize;
  bi.bmiHeader.biHeight = -kSize;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HDC screenDc = GetDC(nullptr);
  HDC memDc = CreateCompatibleDC(screenDc);
  HBITMAP dib = CreateDIBSection(memDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (dib && bits) {
    HGDIOBJ oldBmp = SelectObject(memDc, dib);
    std::memset(bits, 0, static_cast<size_t>(kSize) * kSize * 4);
    // A distinct remote marker, not a second arrow: the local cursor is already an arrow, and a
    // ghost twin trailing it by one RTT reads as a rendering bug. A colored ring with a center
    // dot is unmistakably "the remote pointer" -- and every drawn pixel is non-black, which keeps
    // the alpha promotion below honest (a pure-black outline would be indistinguishable from the
    // untouched transparent background and vanish).
    HPEN ring = CreatePen(PS_SOLID, 3, RGB(64, 160, 255));
    HGDIOBJ oldPen = SelectObject(memDc, ring);
    HGDIOBJ oldBrush = SelectObject(memDc, GetStockObject(HOLLOW_BRUSH));
    Ellipse(memDc, 3, 3, kSize - 3, kSize - 3);
    SelectObject(memDc, oldBrush);
    HBRUSH dot = CreateSolidBrush(RGB(64, 160, 255));
    HGDIOBJ oldBrush2 = SelectObject(memDc, dot);
    HGDIOBJ oldPen2 = SelectObject(memDc, GetStockObject(NULL_PEN));
    const int c = kSize / 2;
    Ellipse(memDc, c - 3, c - 3, c + 3, c + 3);
    SelectObject(memDc, oldBrush2);
    SelectObject(memDc, oldPen2);
    SelectObject(memDc, oldPen);
    DeleteObject(ring);
    DeleteObject(dot);
    auto* px = static_cast<uint32_t*>(bits);
    for (int i = 0; i < kSize * kSize; ++i) {
      if (px[i] != 0) px[i] |= 0xFF000000u;  // colored pixel -> opaque (already premultiplied)
    }
    POINT zero{0, 0};
    SIZE size{kSize, kSize};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    POINT origin{0, 0};
    UpdateLayeredWindow(gCursorOverlayHwnd, screenDc, nullptr, &size, memDc, &zero, 0, &blend,
                        ULW_ALPHA);
    SelectObject(memDc, oldBmp);
  }
  if (dib) DeleteObject(dib);
  DeleteDC(memDc);
  ReleaseDC(nullptr, screenDc);
}

// Timer body: maps the latest remote-cursor sample (capture pixels) into the letterboxed video
// rect and moves the overlay; hides it when stale (>500ms), invisible, occluded by the picker,
// or when the window is minimized.
void update_cursor_overlay(HWND hwnd) {
  // Field verdict: the ring reads as clutter -- disabled by default per the user, kept behind an
  // env for future reconsideration. Static refresh (the thing that keeps still screens alive) is
  // an independent host feature and is unaffected by this.
  // Same parser as the host side (1/true/on), so a future re-enable cannot end up half-on.
  static const bool remoteCursorEnabled = env_truthy("REMOTE60_NATIVE_REMOTE_CURSOR");
  if (!remoteCursorEnabled) {
    if (gCursorOverlayHwnd) ShowWindow(gCursorOverlayHwnd, SW_HIDE);
    return;
  }
  ensure_cursor_overlay(hwnd);
  if (!gCursorOverlayHwnd) return;
  const uint64_t updUs = gRemoteCursorUpdateUs.load(std::memory_order_acquire);
  const uint32_t capW = gRemoteCursorCapW.load(std::memory_order_relaxed);
  const uint32_t capH = gRemoteCursorCapH.load(std::memory_order_relaxed);
  // Generation fence: a sample from the previous target must not paint over a freshly selected
  // one. activeGen==0 = legacy stream view before any PC-side selection; accept anything there.
  const uint64_t cursorGen = gRemoteCursorGeneration.load(std::memory_order_relaxed);
  const uint64_t activeGen = gActiveStreamGeneration.load(std::memory_order_acquire);
  const bool fresh = updUs != 0 && (qpc_now_us() - updUs) < kRemoteCursorStaleUs;
  const bool show = fresh && gRemoteCursorVisible.load(std::memory_order_relaxed) &&
                    capW > 0 && capH > 0 &&
                    (activeGen == 0 || cursorGen == activeGen) &&
                    !gWindowPickerVisible.load(std::memory_order_relaxed) && !IsIconic(hwnd);
  if (!show) {
    ShowWindow(gCursorOverlayHwnd, SW_HIDE);
    return;
  }
  const ClientLayout layout = compute_client_layout(hwnd);
  const RECT content = resolve_video_content_rect(hwnd, layout.videoRect);
  const int videoW = std::max<int>(1, static_cast<int>(content.right - content.left));
  const int videoH = std::max<int>(1, static_cast<int>(content.bottom - content.top));
  const int32_t cx = gRemoteCursorX.load(std::memory_order_relaxed);
  const int32_t cy = gRemoteCursorY.load(std::memory_order_relaxed);
  POINT pt{};
  pt.x = content.left + static_cast<int>(static_cast<int64_t>(std::clamp<int32_t>(cx, 0, capW - 1)) *
                                         videoW / static_cast<int>(capW));
  pt.y = content.top + static_cast<int>(static_cast<int64_t>(std::clamp<int32_t>(cy, 0, capH - 1)) *
                                        videoH / static_cast<int>(capH));
  ClientToScreen(hwnd, &pt);
  // The marker is a ring; center it on the reported point rather than hanging it off a corner.
  SetWindowPos(gCursorOverlayHwnd, nullptr, pt.x - kCursorOverlaySize / 2,
               pt.y - kCursorOverlaySize / 2, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// UNICODE is not defined for this target, so the generic Win32 names resolve to the ANSI
// entry points. This window is registered and created wide, so every message API it touches
// must be the explicit *W form -- DefWindowProcA on a Unicode window read the wide title as
// ANSI and truncated it to "r", and delivered WM_CHAR as ANSI.
bool create_window() {
  HINSTANCE inst = GetModuleHandle(nullptr);
  const wchar_t* cls = L"Remote60NativeVideoClient";
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  // Keep background unmanaged so WM_ERASEBKGND can suppress flicker.
  wc.hbrBackground = nullptr;
  wc.lpszClassName = cls;
  if (!RegisterClassExW(&wc)) return false;

  gHwnd = CreateWindowExW(0, cls, L"remote60 native video client",
                          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          static_cast<int>(gWindowW), static_cast<int>(gWindowH),
                          nullptr, nullptr, inst, nullptr);
  if (!gHwnd) return false;
  ensure_ui_font(gHwnd);
  // The process is per-monitor DPI aware, so the requested size is physical pixels; rescale
  // to keep the intended logical size on scaled displays.
  if (gUiDpi != 96) {
    SetWindowPos(gHwnd, nullptr, 0, 0, dpi_scale(static_cast<int>(gWindowW)),
                 dpi_scale(static_cast<int>(gWindowH)), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  ShowWindow(gHwnd, SW_SHOW);
  UpdateWindow(gHwnd);
  // Remote-cursor overlay cadence: 50ms is enough for a 30Hz feed and costs nothing when hidden.
  SetTimer(gHwnd, kCursorOverlayTimerId, 50, nullptr);
  // The session starts on the picker; stamp its shown-time so the select debounce has one uniform
  // contract from the very first gesture instead of a special startup exemption.
  gPickerShownAtUs.store(qpc_now_us(), std::memory_order_relaxed);
  return true;
}

}  // namespace remote60::native_poc::viewer

using namespace remote60::native_poc::viewer;

int main(int argc, char** argv) {
  std::cout.setf(std::ios::unitbuf);
  std::cerr.setf(std::ios::unitbuf);

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

  // Without this the OS bitmap-stretches the whole window on a scaled display, which blurs
  // both the panel text and the decoded video. Must run before any window is created.
  if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
  }

  const Args args = parse_args(argc, argv);
  gTraceEvery = args.traceEvery;
  gTraceMax = args.traceMax;
  gPresentFrameIntervalUs = static_cast<uint32_t>(std::max<uint64_t>(
      1ULL, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, args.fpsHint))));
  const uint64_t keyframeReqMinIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYFRAME_REQ_MIN_INTERVAL_US",
      static_cast<uint32_t>(kKeyframeRequestMinIntervalUsDefault), 10000, 1000000);
  const uint64_t keyframeReqTokenRefillUs = env_u32_clamped(
      "REMOTE60_NATIVE_KEYFRAME_REQ_TOKEN_REFILL_US",
      static_cast<uint32_t>(kKeyframeRequestTokenRefillUsDefault), 10000, 2000000);
  const uint32_t keyframeReqTokenCapacity = env_u32_clamped(
      "REMOTE60_NATIVE_KEYFRAME_REQ_TOKEN_CAPACITY",
      kKeyframeRequestTokenCapacityDefault, 1, 16);
  gKeyframeRequests.Configure(keyframeReqMinIntervalUs, keyframeReqTokenRefillUs, keyframeReqTokenCapacity);
  const uint64_t catchupReenterMinIntervalUs = env_u32_clamped(
      "REMOTE60_NATIVE_CATCHUP_REENTER_MIN_INTERVAL_US",
      static_cast<uint32_t>(kCatchupReenterMinIntervalUsDefault), 100000, 3000000);
  const uint64_t staleCaptureDropUs = env_u32_clamped(
      "REMOTE60_NATIVE_STALE_CAPTURE_DROP_US",
      static_cast<uint32_t>(kStaleCaptureDropUs), 1000, 2000000);
  const uint64_t congestionRecoverMinUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_RECOVER_MIN_US",
      static_cast<uint32_t>(kCongestionRecoverMinUsDefault), 50000, 5000000);
  const uint64_t congestionRecoveryTimeoutUs = env_u32_clamped(
      "REMOTE60_NATIVE_CONGEST_RECOVERY_TIMEOUT_US",
      static_cast<uint32_t>(kCongestionRecoveryTimeoutUsDefault), 100000, 10000000);
  const uint32_t udpSimDropPm = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_SIM_DROP_PM", 0, 0, 1000);
  const uint32_t udpSimDropSeed = env_u32_clamped(
      "REMOTE60_NATIVE_UDP_SIM_DROP_SEED", 0, 0, 0x7fffffffu);
  gKeyframeRequests.Reset();

  const bool useRaw = (args.codec == "raw");
  const bool useH264 = (args.codec == "h264");
  const bool encodedExperimentEnabled =
      (REMOTE60_NATIVE_ENCODED_EXPERIMENT != 0) || env_truthy("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE");
  if (!useRaw && !useH264) {
    std::cerr << "[native-video-client] unsupported codec: " << args.codec << " (supported: raw,h264)\n";
    return 10;
  }
  if (useH264 && !encodedExperimentEnabled) {
    std::cerr << "[native-video-client] unsupported codec: " << args.codec
              << " (enable REMOTE60_NATIVE_ENCODED_EXPERIMENT or set env REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1)\n";
    return 10;
  }
  std::string effectiveTransport = args.transport;
  if (effectiveTransport.empty()) {
    effectiveTransport = useH264 ? "udp" : "tcp";
  }
  VideoTransport transport = VideoTransport::Tcp;
  if (!parse_video_transport(effectiveTransport, &transport)) {
    std::cerr << "[native-video-client] unsupported transport: " << effectiveTransport << " (supported: tcp,udp)\n";
    return 12;
  }
  if (transport == VideoTransport::Udp && useRaw) {
    std::cerr << "[native-video-client] raw codec over udp is not supported in current phase (use codec=h264)\n";
    return 13;
  }

  gOverlayConfig.host = args.host;
  gOverlayConfig.port = args.port;
  gOverlayConfig.controlPort = args.controlPort;
  gOverlayConfig.transport = video_transport_name(transport);
  gOverlayConfig.codec = args.codec;
  gOverlayConfig.fpsHint = args.fpsHint;
  gOverlayConfig.controlIntervalMs = args.controlIntervalMs;
  gOverlayConfig.tcpRecvBufKb = args.tcpRecvBufKb;
  gOverlayConfig.tcpSendBufKb = args.tcpSendBufKb;
  gOverlayConfig.udpMtu = args.udpMtu;
  gOverlayConfig.keyReqMinIntervalUs = gKeyframeRequests.min_interval_us();
  gOverlayConfig.keyReqTokenRefillUs = gKeyframeRequests.token_refill_us();
  gOverlayConfig.keyReqTokenCapacity = gKeyframeRequests.token_capacity();
  gOverlayConfig.udpSimDropPm = udpSimDropPm;
  gRuntimeTuneState.Reset(args.runtimeBitrate, args.runtimeKeyint, args.runtimeFps);
  gRequestedMonitorId = args.monitorId;
  gControlConnected.store(false, std::memory_order_relaxed);
  // How the session opens. The explicit flag wins; with no flag we fall back to the legacy env
  // var so the automation probes (which all set REMOTE60_NATIVE_START_STREAM_VIEW=1) are
  // unaffected. "targets" is the product flow: open on the picker and stream only after a pick.
  bool startInStreamView;
  if (args.initialView == "targets" || args.initialView == "picker") {
    startInStreamView = false;
  } else if (args.initialView == "stream") {
    startInStreamView = true;
  } else {
    startInStreamView = env_truthy("REMOTE60_NATIVE_START_STREAM_VIEW");
  }
  const bool startInPicker = !startInStreamView;
  gCaptureOverviewMode.store(startInPicker, std::memory_order_relaxed);
  gWindowPickerVisible.store(startInPicker, std::memory_order_relaxed);
  clear_pc_target_selection();
  // No target has taken effect yet. 0 disables the persistent generation filter, so the legacy
  // stream-view start and the pre-first-pick window accept whatever the host sends, as before.
  gActiveStreamGeneration.store(0, std::memory_order_release);
  gSelectionRevealPosted.store(false, std::memory_order_release);
  // Picker-first sessions must not keep the host's default stream running under the picker: the
  // request rides the scheduler (StreamState before WindowList/Select) and is queued before the
  // control link exists, so it goes out first thing once connected. An initial default-desktop
  // frame that slips through before the stream stops is dropped by the receive-path gate rather
  // than painted, and no flip swap chain is created until the user's pick produces a real frame.
  if (startInPicker) {
    gStreamStateControl.Request(false);
  }
  gCaptureModeRequests.Reset();
  gWindowPanelState.Reset();
  gSuppressMouseUntilUs.store(0, std::memory_order_relaxed);
  gActiveTouchPointerId.store(0, std::memory_order_relaxed);
  gActiveTouchDown.store(false, std::memory_order_relaxed);

  remote60::native_poc::WinsockScope ws;
  if (!ws.ok) {
    std::cerr << "[native-video-client] WSAStartup failed\n";
    return 1;
  }

  if (!create_window()) {
    std::cerr << "[native-video-client] window create failed\n";
    return 2;
  }

  {
    remote60::native_poc::SessionToolbarCallbacks toolbarCallbacks;
    // Re-enabled: the reason this was unset -- entering the picker mid-session looked like a
    // freeze -- is fixed (the picker no longer stops the stream, and its repaint is composited).
    // With the invisible legacy top-left buttons removed, this is the ONLY road back to target
    // selection during a session, so it must exist.
    toolbarCallbacks.onTargets = [] {
      set_picker_visible_and_sync_stream(true);
      push_session_toolbar_state();
      if (gHwnd) InvalidateRect(gHwnd, nullptr, FALSE);
    };
    toolbarCallbacks.onMacro = [] {
      toggle_macro_window(gHwnd);
      push_session_toolbar_state();
    };
    toolbarCallbacks.onMonitor = [](uint32_t monitorId) {
      gWindowPanelState.RequestMonitorSelect(monitorId);
    };
    remote60::native_poc::session_toolbar_create(gHwnd, std::move(toolbarCallbacks));
    remote60::native_poc::session_toolbar_set_visible(startInStreamView);
    push_session_toolbar_state();
  }

  bool mfStarted = false;
  H264Decoder decoder;
  bool decoderReady = false;
  bool waitForKeyFrame = useH264;
  uint32_t decoderW = 0;
  uint32_t decoderH = 0;
  Microsoft::WRL::ComPtr<ID3D11Device> decD3dDevice;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> decD3dContext;
  if (useH264) {
    const HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
      std::cerr << "[native-video-client] MFStartup failed hr=0x" << std::hex << static_cast<unsigned long>(hr)
                << std::dec << "\n";
      return 11;
    }
    mfStarted = true;
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
      if (!gNv12Renderer.ready) (void)gNv12Renderer.init(gHwnd);
      if (gNv12Renderer.ready) {
        decD3dDevice = gNv12Renderer.device;
        decD3dContext = gNv12Renderer.context;
        (void)decoder.set_d3d11_device(decD3dDevice.Get());
      } else {
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        const HRESULT d3dHr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                                D3D11_SDK_VERSION, &decD3dDevice, &fl,
                                                &decD3dContext);
        if (SUCCEEDED(d3dHr) && decD3dDevice) {
          (void)decoder.set_d3d11_device(decD3dDevice.Get());
        }
      }
    }
  }

  // Reaching the host through the directory replaces the address entirely: the socket comes back
  // already prepared (it is the one the directory observed, so the host is punching towards it)
  // and the capability that follows is what the host authorises the session against.
  std::string directoryPunchToken;
  Args resolvedArgs = args;
  if (!args.directoryUrl.empty()) {
    if (transport != VideoTransport::Udp) {
      std::cerr << "[native-video-client] the directory path is udp only\n";
      if (mfStarted) MFShutdown();
      return 3;
    }
    std::string directoryError;
    std::string sessionToken = args.directorySession;
    if (sessionToken.empty() &&
        !remote60::native_poc::directory_login(args.directoryUrl, args.directoryAccount,
                                               args.directoryPassword, &sessionToken,
                                               &directoryError)) {
      std::cerr << "[native-video-client] directory login failed: " << directoryError << "\n";
      if (mfStarted) MFShutdown();
      return 3;
    }

    std::string hostId = args.directoryHostId;
    if (hostId.empty()) {
      std::vector<remote60::native_poc::DirectoryHostEntry> hosts;
      if (!remote60::native_poc::directory_list_hosts(args.directoryUrl, sessionToken, &hosts,
                                                      &directoryError)) {
        std::cerr << "[native-video-client] directory hosts failed: " << directoryError << "\n";
        if (mfStarted) MFShutdown();
        return 3;
      }
      for (const auto& entry : hosts) {
        if (!args.directoryHostName.empty() && entry.hostName != args.directoryHostName) continue;
        // An offline host has no mapping to punch towards, so preferring an online one avoids a
        // four-second wait that was never going to succeed.
        if (hostId.empty() || entry.online) hostId = entry.hostId;
        if (entry.online) break;
      }
      if (hostId.empty()) {
        std::cerr << "[native-video-client] no host on this account"
                  << (args.directoryHostName.empty() ? "" : " named " + args.directoryHostName)
                  << "\n";
        if (mfStarted) MFShutdown();
        return 3;
      }
    }

    remote60::native_poc::DirectorySessionRequest request{};
    request.url = args.directoryUrl;
    request.sessionToken = sessionToken;
    request.hostId = hostId;
    remote60::native_poc::DirectorySessionResult session{};
    if (!remote60::native_poc::directory_session_open(request, &session, &directoryError)) {
      std::cerr << "[native-video-client] directory connect failed: " << directoryError << "\n";
      if (mfStarted) MFShutdown();
      return 3;
    }
    gSock = session.socket;
    resolvedArgs.host = session.chosen.ip;
    resolvedArgs.port = session.chosen.port;
    // Control travels over the media socket on this path; a separate TCP port cannot survive
    // hole punching.
    resolvedArgs.controlPort = 0;
    directoryPunchToken = session.punchToken;
    gRelayPath.store(session.relay, std::memory_order_relaxed);
    push_session_toolbar_state();
    std::cout << "[native-video-client] directory chose " << session.chosen.ip << ":"
              << session.chosen.port << " ("
              << remote60::native_poc::candidate_kind_name(session.chosen.kind) << ")"
              << (session.answered ? "" : " [no answer, trying anyway]") << "\n";
  } else {
    gSock = socket(AF_INET,
                   (transport == VideoTransport::Udp) ? SOCK_DGRAM : SOCK_STREAM,
                   (transport == VideoTransport::Udp) ? IPPROTO_UDP : IPPROTO_TCP);
  }
  if (gSock == INVALID_SOCKET) {
    std::cerr << "[native-video-client] socket create failed\n";
    if (mfStarted) MFShutdown();
    return 3;
  }

  if (transport == VideoTransport::Tcp) {
    int noDelay = 1;
    setsockopt(gSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
  }
  if (transport == VideoTransport::Udp) {
    if (args.tcpRecvBufKb == 0) {
      const int recvBuf = 1024 * 1024;
      (void)setsockopt(gSock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
    }
    if (args.tcpSendBufKb == 0) {
      const int sendBuf = 256 * 1024;
      (void)setsockopt(gSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
    }
  }
  if (args.tcpRecvBufKb > 0) {
    const int recvBuf = static_cast<int>(args.tcpRecvBufKb * 1024u);
    setsockopt(gSock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
  }
  if (args.tcpSendBufKb > 0) {
    const int sendBuf = static_cast<int>(args.tcpSendBufKb * 1024u);
    setsockopt(gSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBuf), sizeof(sendBuf));
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(resolvedArgs.port);
  if (inet_pton(AF_INET, resolvedArgs.host.c_str(), &addr.sin_addr) != 1) {
    std::cerr << "[native-video-client] invalid host " << resolvedArgs.host << "\n";
    closesocket(gSock);
    gSock = INVALID_SOCKET;
    if (mfStarted) MFShutdown();
    return 4;
  }
  if (connect(gSock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "[native-video-client] connect failed " << resolvedArgs.host << ":" << resolvedArgs.port << "\n";
    closesocket(gSock);
    gSock = INVALID_SOCKET;
    if (mfStarted) MFShutdown();
    return 5;
  }
  if (transport == VideoTransport::Udp) {
    int timeoutMs = 200;
    (void)setsockopt(gSock, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    bool handshakeOk = false;
    for (int attempt = 0; attempt < 40 && !handshakeOk; ++attempt) {
      UdpHelloPacket hello{};
      hello.kind = static_cast<uint16_t>(UdpPacketKind::Hello);
      // The capability from /api/connect. Without it the host treats this as a plain LAN client
      // and refuses anything that needs authorisation -- secure-desktop input in particular.
      if (!directoryPunchToken.empty()) {
        std::snprintf(hello.authToken, sizeof(hello.authToken), "%s", directoryPunchToken.c_str());
      }
      const int sent = send(gSock, reinterpret_cast<const char*>(&hello), sizeof(hello), 0);
      if (sent <= 0) {
        Sleep(50);
        continue;
      }
      UdpHelloPacket ack{};
      const int n = recv(gSock, reinterpret_cast<char*>(&ack), sizeof(ack), 0);
      if (n >= static_cast<int>(sizeof(UdpHelloPacket)) &&
          ack.magic == remote60::native_poc::kMagic &&
          ack.kind == static_cast<uint16_t>(UdpPacketKind::HelloAck) &&
          ack.version == remote60::native_poc::kUdpProtocolVersion &&
          (ack.features & remote60::native_poc::kUdpFeatureVideoFec) != 0) {
        handshakeOk = true;
        break;
      }
      Sleep(50);
    }
    timeoutMs = 0;
    (void)setsockopt(gSock, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    if (!handshakeOk) {
      std::cerr << "[native-video-client] udp handshake failed " << args.host << ":" << args.port << "\n";
      closesocket(gSock);
      gSock = INVALID_SOCKET;
      if (mfStarted) MFShutdown();
      return 6;
    }
  }

  // No second port to dial means the directory path: control tunnels through the socket the
  // punch just opened. The send is bare because the socket is connected -- the same socket the
  // receive loop below reads, which is what makes the two directions one NAT mapping.
  if (args.controlPort == 0 && transport == VideoTransport::Udp && gSock != INVALID_SOCKET) {
    gUdpControl.Configure(
        [](const void* data, size_t len) -> bool {
          return send(gSock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        remote60::native_poc::kUdpControlStreamClientToHost,
        remote60::native_poc::kUdpControlStreamHostToClient, args.udpMtu);
    gControlOverUdp.store(true, std::memory_order_release);
    // Without this the receive blocks forever on a link that has gone quiet, and the tick above
    // never runs -- which is the one moment recovery is needed.
    DWORD recvTimeoutMs = 200;
    setsockopt(gSock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeoutMs),
               sizeof(recvTimeoutMs));
    std::cout << "[native-video-client] control tunnelled over the media socket\n";
  }

  std::cout << "[native-video-client] connected host=" << args.host
            << " port=" << args.port
            << " transport=" << video_transport_name(transport)
            << " codec=" << args.codec
            << " seconds=" << args.seconds << "\n";
  std::cout << "[native-video-client] keyframe-request-limiter minIntervalUs="
            << gKeyframeRequests.min_interval_us()
            << " tokenRefillUs=" << gKeyframeRequests.token_refill_us()
            << " tokenCapacity=" << gKeyframeRequests.token_capacity()
            << " catchupReenterMinUs=" << catchupReenterMinIntervalUs
            << " staleCaptureDropUs=" << staleCaptureDropUs
            << " congestionRecoverMinUs=" << congestionRecoverMinUs
            << " congestionRecoveryTimeoutUs=" << congestionRecoveryTimeoutUs
            << "\n";
  if (kInputPolicyForceBlock) {
    std::cout << "[native-video-client] input channel blocked by compile-time policy\n";
  }
  int effectiveRecvBuf = 0;
  int effectiveRecvBufLen = sizeof(effectiveRecvBuf);
  (void)getsockopt(gSock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&effectiveRecvBuf), &effectiveRecvBufLen);
  int effectiveSendBuf = 0;
  int effectiveSendBufLen = sizeof(effectiveSendBuf);
  (void)getsockopt(gSock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&effectiveSendBuf), &effectiveSendBufLen);
  std::cout << "[native-video-client] socket rcvbuf=" << effectiveRecvBuf
            << " sndbuf=" << effectiveSendBuf << " bytes\n";

  SOCKET controlSock = INVALID_SOCKET;
  std::thread controlThread;
  // Two ways to reach the host's control protocol, and the session only ever has one of them.
  // A direct host answers on its own TCP port; a host behind NAT is reachable solely through
  // the punched media socket, and dialling a second port there connects to nothing.
  bool controlReady = gControlOverUdp.load(std::memory_order_acquire);
  if (args.controlPort > 0) {
    controlSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (controlSock != INVALID_SOCKET) {
      int ctlNoDelay = 1;
      setsockopt(controlSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&ctlNoDelay), sizeof(ctlNoDelay));
      sockaddr_in ctlAddr{};
      ctlAddr.sin_family = AF_INET;
      ctlAddr.sin_port = htons(args.controlPort);
      if (inet_pton(AF_INET, args.host.c_str(), &ctlAddr.sin_addr) == 1 &&
          connect(controlSock, reinterpret_cast<const sockaddr*>(&ctlAddr), sizeof(ctlAddr)) == 0) {
        controlReady = true;
      }
    }
  }
  {
    const bool inputChannelEnabled =
        controlReady && args.enableInputChannel && !kInputPolicyForceBlock;
    gInputEnabled = inputChannelEnabled;
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
        enqueue_input_event(6, 0, 0, 0, vk);
      }
    }
    gControlScheduler.Reset(args.controlIntervalMs, qpc_now_us());
    if (controlReady) {
      controlThread = std::thread([&]() {
        // Fetch one queued preview over the control socket. Runs between scheduler
        // actions on the same strict request/response pipeline, one card per call so a
        // large backlog cannot starve input events. Only invoked when the host advertised
        // the capability, because an older host would drain the request and never reply.
        // Returns: 1 fetched, 0 nothing to do, -1 socket failure (stream desynced).
        auto fetch_one_thumbnail = [&](remote60::native_poc::ControlLink& link) -> int {
          // Routed through the ControlLink, not the raw socket, so a directory session (control
          // tunnelled over the punched UDP socket) fetches previews too -- modelled on the
          // Android ClientSessionController::FetchOneThumbnailLocked. One card per idle action
          // keeps the strict request/response loop from being starved. Only invoked when the
          // host advertised the capability, because an older host would drain the request and
          // never reply. Returns: 1 fetched, 0 nothing to do, -1 link failure (stream desynced).
          if (!gHostSupportsThumbnails.load(std::memory_order_relaxed)) return 0;
          uint64_t id = 0;
          {
            std::lock_guard<std::mutex> lk(gThumbMu);
            if (gThumbFetchQueue.empty()) return 0;
            id = gThumbFetchQueue.front();
            gThumbFetchQueue.pop_front();
          }
          remote60::native_poc::ControlWindowThumbnailRequestMessage req{};
          req.header.magic = remote60::native_poc::kMagic;
          req.header.type =
              static_cast<uint16_t>(MessageType::ControlWindowThumbnailRequest);
          req.header.size = static_cast<uint16_t>(sizeof(req));
          req.seq = 0;
          req.windowId = id;
          req.maxWidth = 256;
          req.maxHeight = 160;
          req.clientSendQpcUs = qpc_now_us();
          // One request is one message; EndMessage() draws the boundary UDP needs and TCP ignores.
          if (!link.Write(&req, sizeof(req)) || !link.EndMessage()) return -1;
          remote60::native_poc::ControlWindowThumbnailHeader rsp{};
          if (!link.Read(&rsp, sizeof(rsp))) return -1;
          if (rsp.header.magic != remote60::native_poc::kMagic ||
              rsp.header.type != static_cast<uint16_t>(MessageType::ControlWindowThumbnail) ||
              rsp.payloadSize > remote60::native_poc::kWindowThumbnailMaxPayloadBytes) {
            return -1;
          }
          std::vector<uint8_t> payload(rsp.payloadSize);
          if (rsp.payloadSize > 0 && !link.Read(payload.data(), payload.size())) {
            return -1;
          }
          if ((rsp.flags & 0x1u) != 0 && rsp.width > 0 && rsp.height > 0 &&
              payload.size() == static_cast<size_t>(rsp.width) * rsp.height * 4u) {
            auto thumb = std::make_shared<WindowThumb>();
            thumb->width = rsp.width;
            thumb->height = rsp.height;
            thumb->bgra = std::move(payload);
            thumb->fetchedUs = qpc_now_us();
            {
              std::lock_guard<std::mutex> lk(gThumbMu);
              gThumbs[id] = std::move(thumb);
            }
            // Outside the lock: the paint handler takes gThumbMu, and invalidating while
            // holding it invited a stall on every received preview.
            InvalidateRect(gHwnd, nullptr, FALSE);
          }
          return 1;
        };
          // Built once, not per action: the tunnelled link carries the partially-read inbound
          // message between calls, and a fresh one each time would drop whatever it held.
          std::unique_ptr<remote60::native_poc::ControlLink> controlLink;
          if (gControlOverUdp.load(std::memory_order_acquire)) {
            controlLink = std::make_unique<remote60::native_poc::UdpControlLink>(
                &gUdpControl, kUdpControlReadTimeoutMs);
          } else {
            controlLink = std::make_unique<remote60::native_poc::TcpControlLink>(controlSock);
          }

          while (gRunning.load()) {
            // Drives retransmission and gap recovery; cheap when there is nothing outstanding.
            if (gControlOverUdp.load(std::memory_order_acquire)) gUdpControl.Tick();
            bool didWork = false;
            const uint64_t nowUs = qpc_now_us();
            ControlOutboundAction action{};
            if (gControlScheduler.NextAction(
                    nowUs, capture_client_control_metrics_snapshot(), &gWindowPanelState,
                    &gStreamStateControl, &gCaptureModeRequests, &gKeyframeRequests, &gRuntimeTuneState,
                    &gInputQueueState, &action)) {
              TcpControlResponse response{};
              const uint64_t actionStartUs = qpc_now_us();
              const bool actionOk = execute_control_action(*controlLink, action, &response);
              // One exchange that never gets its reply stalls every later one behind it,
              // including input. Naming the slow action is the only way to see which.
              const uint64_t actionUs = qpc_now_us() - actionStartUs;
              if (actionUs > 1000000ULL) {
                std::cout << "[native-video-client][control] slow action kind="
                          << static_cast<int>(action.kind) << " tookUs=" << actionUs
                          << " ok=" << (actionOk ? 1 : 0) << "\n";
              }
              if (!actionOk) {
                // A failed exchange ends the session's control, so it has to say which one and
                // on what transport. This used to break out silently, which made a control
                // channel that died on one bad message look identical to one that never
                // connected.
                std::cout << "[native-video-client][control] action failed kind="
                          << static_cast<int>(action.kind) << " transport="
                          << (gControlOverUdp.load(std::memory_order_acquire) ? "udp-tunnel" : "tcp");
                if (gControlOverUdp.load(std::memory_order_acquire)) {
                  // Closed means the channel gave up on the peer; open means the exchange came
                  // back as something other than the reply this action was waiting for.
                  const auto stats = gUdpControl.GetStats();
                  std::cout << " closed=" << (gUdpControl.IsClosed() ? 1 : 0)
                            << " reason=" << to_string(gUdpControl.CloseReason())
                            << " sent=" << stats.messagesSent
                            << " received=" << stats.messagesReceived
                            << " retx=" << stats.fragmentRetransmits
                            << " nacks=" << stats.nacksSent;
                }
                std::cout << "\n";
                break;
              }
              if (action.kind == ControlOutboundActionKind::InputEvent) {
                const uint64_t sent = ++gInputEventsSent;
                if (args.inputLogEvery > 0 && (sent % args.inputLogEvery) == 0) {
                  std::cout << "[native-video-client][input] sent=" << sent
                            << " kind=" << action.inputEvent.kind
                            << " seq=" << action.inputEvent.seq << "\n";
                }
              }
              didWork = true;

              if (action.kind == ControlOutboundActionKind::CaptureMode) {
                gCaptureOverviewMode.store(action.captureMode.mode == 1, std::memory_order_relaxed);
                std::cout << "[native-video-client][control] capture-mode-request seq=" << action.captureMode.seq
                          << " mode=" << action.captureMode.mode
                          << " xPermille=" << action.captureMode.xPermille
                          << " yPermille=" << action.captureMode.yPermille
                          << "\n";
              } else if (action.kind == ControlOutboundActionKind::KeyframeRequest) {
                std::cout << "[native-video-client][control] keyframe-request seq=" << action.keyframe.seq
                          << " reason=" << action.keyframe.reason << "\n";
              } else if (action.kind == ControlOutboundActionKind::StreamState) {
                std::cout << "[native-video-client][control] stream-state seq="
                          << action.streamState.seq
                          << " active=" << ((action.streamState.flags & 0x1u) ? 1 : 0) << "\n";
              } else if (action.kind == ControlOutboundActionKind::RuntimeTune) {
                std::cout << "[native-video-client][control] runtime-config seq=" << action.runtimeTune.seq
                          << " bitrate=" << action.runtimeTune.bitrate
                          << " keyint=" << action.runtimeTune.keyint
                          << " flags=" << action.runtimeTune.flags
                          << "\n";
              }

              switch (response.kind) {
                case TcpControlResponseKind::Pong: {
                  const auto& pong = response.pong;
                  const uint64_t doneUs = qpc_now_us();
                  gControlScheduler.OnPingCompleted(doneUs);
                  gHostCaptureTargetPid.store(pong.captureTargetPid, std::memory_order_relaxed);
                  gHostCaptureTargetFlags.store(pong.captureTargetFlags, std::memory_order_relaxed);
                  gHostCaptureRebindCount.store(pong.captureRebindCount, std::memory_order_relaxed);
                  gHostCaptureTargetHwnd.store(pong.captureTargetHwnd, std::memory_order_relaxed);
                  gHostCaptureMetaUpdatedUs.store(doneUs, std::memory_order_relaxed);
                  gCaptureOverviewMode.store(
                      (pong.captureTargetFlags &
                       remote60::native_poc::kCaptureFlagWindowTargetEnabled) == 0,
                      std::memory_order_relaxed);
                  {
                    // Say it once per transition rather than every ping. A frozen picture with no
                    // explanation is the worst version of this; a line saying a Windows security
                    // prompt is on screen turns it into something the operator can act on.
                    const bool secure =
                        (pong.captureTargetFlags &
                         remote60::native_poc::kCaptureFlagSecureDesktopActive) != 0;
                    static bool reportedSecure = false;
                    if (secure != reportedSecure) {
                      reportedSecure = secure;
                      std::cout << "[native-video-client] secure-desktop-active="
                                << (secure ? 1 : 0)
                                << (secure ? "  (a Windows security prompt is on screen; it "
                                             "cannot be captured, so the picture is paused)"
                                           : "  (picture resumes)")
                                << std::endl;
                    }
                  }
                  {
                    std::lock_guard<std::mutex> lk(gHostCaptureMetaMu);
                    gHostCaptureTargetProcess =
                        fixed_cstr_to_string(pong.captureTargetProcess, sizeof(pong.captureTargetProcess));
                    gHostCaptureTargetTitle =
                        fixed_cstr_to_string(pong.captureTargetTitle, sizeof(pong.captureTargetTitle));
                  }
                  const uint64_t rttUs =
                      (doneUs >= action.ping.clientSendQpcUs) ? (doneUs - action.ping.clientSendQpcUs) : 0;
                  std::cout << "[native-video-client][control] seq=" << pong.seq
                            << " rttUs=" << rttUs
                            << " hostQueueUs=" << ((pong.hostSendQpcUs >= pong.hostRecvQpcUs)
                                                        ? (pong.hostSendQpcUs - pong.hostRecvQpcUs)
                                                        : 0)
                            << " hostCapPid=" << pong.captureTargetPid
                            << " hostCapProc=" << fixed_cstr_to_string(
                                   pong.captureTargetProcess, sizeof(pong.captureTargetProcess))
                            << " hostCapRebind=" << pong.captureRebindCount
                            << "\n";
                  // GNLink stream telemetry (diagnostics only): a periodic NTP-style clock offset
                  // (host QPC minus client QPC) plus RTT, so the seq-joined host/client logs can
                  // also be roughly aligned on an absolute timeline. Runs once per pong (~control
                  // interval); no new control traffic is introduced.
                  {
                    const int64_t t1 = static_cast<int64_t>(action.ping.clientSendQpcUs);
                    const int64_t t2 = static_cast<int64_t>(pong.hostRecvQpcUs);
                    const int64_t t3 = static_cast<int64_t>(pong.hostSendQpcUs);
                    const int64_t t4 = static_cast<int64_t>(doneUs);
                    const int64_t clockOffsetUs = ((t2 - t1) + (t3 - t4)) / 2;
                    std::ostringstream telem;
                    telem << "[native-video-client][telemetry] stage=clock"
                          << " pingSeq=" << pong.seq
                          << " rttUs=" << rttUs
                          << " clockOffsetUs=" << clockOffsetUs
                          << " clientSendUs=" << action.ping.clientSendQpcUs
                          << " hostRecvUs=" << pong.hostRecvQpcUs
                          << " hostSendUs=" << pong.hostSendQpcUs
                          << " clientRecvUs=" << doneUs;
                    log_client_line(telem.str());
                  }
                  break;
                }
                case TcpControlResponseKind::WindowList: {
                  apply_window_list_snapshot(response.windowList);
                  // The window list is where the host says whether it knows the monitor
                  // messages; asking one that does not would stall this loop waiting for a
                  // reply that never comes.
                  const bool supportsMonitors =
                      (response.windowList.flags &
                       remote60::native_poc::kControlWindowListFlagMonitors) != 0;
                  const bool monitorsNewlySupported =
                      gWindowPanelState.SetHostSupportsMonitors(supportsMonitors);
                  // The stored --monitor is auto-applied only when the session opens straight
                  // into the stream. In picker mode the user has not chosen a target yet, so
                  // selecting a monitor here would restart the host capture before any pick and
                  // fight the first-frame gate; a monitor pick is a follow-up (toolbar) action.
                  if (!startInPicker && monitorsNewlySupported && gRequestedMonitorId > 0) {
                    // Only when a screen other than the primary was asked for: selecting monitor
                    // zero would restart the capture for no change.
                    gWindowPanelState.RequestMonitorSelect(gRequestedMonitorId);
                  }
                  InvalidateRect(gHwnd, nullptr, FALSE);
                  break;
                }
                case TcpControlResponseKind::MonitorList:
                  gWindowPanelState.ApplyMonitorList(response.monitorList);
                  break;
                case TcpControlResponseKind::WindowSelected:
                  apply_window_selected_result(response.windowSelected);
                  queue_window_list_request("window_list_request pending");
                  InvalidateRect(gHwnd, nullptr, FALSE);
                  break;
                case TcpControlResponseKind::InputAck: {
                  const uint64_t ackCount = gControlScheduler.RecordInputAck(args.inputLogEvery);
                  if (ackCount > 0) {
                    std::cout << "[native-video-client][input] ackSeq=" << response.inputAck.seq
                              << " sent=" << ackCount
                              << " dropped=" << gInputQueueState.dropped_count()
                              << "\n";
                  }
                  break;
                }
                case TcpControlResponseKind::None:
                default:
                  break;
              }
            }

            if (!didWork && gWindowPickerVisible.load(std::memory_order_relaxed)) {
              const int fetched = fetch_one_thumbnail(*controlLink);
              if (fetched < 0) break;
              didWork = (fetched > 0);
            }
            if (!didWork) Sleep(2);
          }
          gControlConnected.store(false, std::memory_order_relaxed);
          gRuntimeTuneState.SetEnabled(false);
          // A selection cannot complete once control is gone: drop the pending state so the picker
          // re-enables instead of staying locked on "waiting for first frame". The viewer exits
          // shortly after (the video socket dies too), which returns the shell to the host list.
          clear_pc_target_selection();
          // Drop the persistent generation filter too: a reconnect renegotiates generations from
          // scratch, so an old value must not silently filter the new stream to nothing.
          gActiveStreamGeneration.store(0, std::memory_order_release);
          set_window_panel_status("control_disconnected");
          InvalidateRect(gHwnd, nullptr, FALSE);
      });
    }
    if (controlReady) {
      gControlConnected.store(true, std::memory_order_relaxed);
      gRuntimeTuneState.SetEnabled(useH264);
      queue_window_list_request("window_list_request pending");
      if (useH264 && (args.runtimeBitrate > 0 || args.runtimeKeyint > 0)) {
        gRuntimeTuneState.MarkDirty();
      }
      std::cout << "[native-video-client] control connected transport="
                << (gControlOverUdp.load(std::memory_order_acquire) ? "udp-tunnel" : "tcp")
                << " port=" << args.controlPort
                << " inputChannel=" << (inputChannelEnabled ? 1 : 0) << "\n";
    } else {
      if (controlSock != INVALID_SOCKET) {
        closesocket(controlSock);
        controlSock = INVALID_SOCKET;
      }
      gControlConnected.store(false, std::memory_order_relaxed);
      gRuntimeTuneState.SetEnabled(false);
      set_window_panel_status("control_connect_failed");
      std::cout << "[native-video-client] control unavailable port=" << args.controlPort << "\n";
    }
  }

  const uint64_t startUs = qpc_now_us();
  std::thread recvThread([&]() {
    // Which selection generation this loop has already reset the decoder for. A bump by
    // begin_pc_target_selection() on the UI thread makes the next frame flush stale references.
    uint64_t recvSelectionEpoch = gSelectionEpoch.load(std::memory_order_acquire);
    uint64_t statAtUs = qpc_now_us() + 1000000ULL;
    uint64_t recvFrames = 0;
    uint64_t decodedFrames = 0;
    uint64_t skippedQueued = 0;
    uint64_t recvBytes = 0;
    uint64_t decodedBytes = 0;
    uint64_t sumLatencyUs = 0;
    uint64_t maxLatencyUs = 0;
    uint64_t sumDecodeTailUs = 0;
    uint64_t maxDecodeTailUs = 0;
    uint64_t decodeFailCount = 0;
    // Consecutive hard decode failures. A flush (decoder.reset) recovers a corrupt frame, but
    // not a wedged hardware MFT or a lost D3D device -- and the viewer's only recovery for a
    // same-resolution decode error was that flush, so once the decoder wedged (a YouTube scene
    // change on a busy GPU could do it) every following frame failed identically and the
    // picture froze until the app was restarted. Past a threshold, rebuild the decoder instead.
    uint32_t decodeConsecutiveFailCount = 0;
    constexpr uint32_t kDecodeRebuildThreshold = 8;
    uint64_t decodeTimestampOverflowCount = 0;
    uint64_t decodeEmptyCount = 0;
    uint64_t decodeEmptyStreak = 0;
    uint64_t decodeEmptyStreakStartUs = 0;
    uint64_t decodeEmptyRecoveryCount = 0;
    uint64_t waitingKeyDropCount = 0;
    uint64_t lagDropCount = 0;
    uint64_t udpChunkRecvCount = 0;
    uint64_t udpAssemblyCompletedCount = 0;
    uint64_t udpAssemblyDroppedCount = 0;
    uint64_t udpAssemblyMalformedCount = 0;
    uint64_t udpAssemblyReorderCount = 0;
    uint64_t udpAssemblyKeyReqCount = 0;
    uint64_t udpAssemblyFecRecoveredCount = 0;
    uint32_t udpAssemblyDropPmLast = 0;
    uint64_t lastPacketRecvUs = 0;
    uint32_t lagTriggerStreak = 0;
    uint64_t lastCatchupEnterUs = 0;
    uint64_t catchupEnterThrottledCount = 0;
    bool catchupMode = false;
    // lastPresentedCaptureUs is now gLastPresentedCaptureUs (atomic, updated after actual present)
    bool captureTimelineReady = false;
    uint64_t captureRemoteBaseUs = 0;
    uint64_t captureLocalBaseUs = 0;
    bool sendTimelineReady = false;
    uint64_t sendRemoteBaseUs = 0;
    uint64_t sendLocalBaseUs = 0;
    const uint64_t frameIntervalUs = std::max<uint64_t>(
        1ULL, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, args.fpsHint)));
    ClientCongestionState congestionState = ClientCongestionState::Normal;
    uint64_t congestionStateEnterUs = 0;
    uint64_t congestionTransitionCount = 0;
    uint64_t congestionRecoveryCount = 0;
    uint64_t congestionRecoveryTotalUs = 0;
    uint64_t congestionRecoveryMaxUs = 0;
    uint64_t congestionRecoveryRequestCount = 0;
    uint64_t staleDropCount = 0;
    uint64_t holdLatestDropCount = 0;
    uint64_t burstDropCount = 0;
    uint64_t staleReferenceRecoveryCount = 0;
    // Capture timestamp of the newest keyframe the decoder has successfully consumed. A stale
    // frame OLDER than this anchor was already resynced past (safe to quiet-drop); one AT OR
    // AFTER it still sits in the live reference chain, so dropping it needs an IDR resync.
    uint64_t lastDecodedKeyCaptureUs = 0;
    uint64_t latestCaptureSeenUs = 0;
    uint64_t queueDepthSampleCount = 0;
    uint64_t queueDepthHist[5] = {0, 0, 0, 0, 0};
    uint32_t queueDepthFramesMax = 0;
    uint64_t recoveringSinceUs = 0;
    uint32_t recoveringHealthyStreak = 0;
    uint64_t lastRecoveryRequestUs = 0;
    auto queue_depth_frames = [&](uint64_t lagUs) -> uint32_t {
      if (lagUs == 0) return 0;
      const uint64_t depth64 = (lagUs + frameIntervalUs - 1) / frameIntervalUs;
      return static_cast<uint32_t>(std::min<uint64_t>(depth64, 1000ULL));
    };
    auto sample_queue_depth = [&](uint64_t lagUs) {
      const uint32_t depthFrames = queue_depth_frames(lagUs);
      ++queueDepthSampleCount;
      if (depthFrames > queueDepthFramesMax) queueDepthFramesMax = depthFrames;
      if (depthFrames == 0) {
        ++queueDepthHist[0];
      } else if (depthFrames == 1) {
        ++queueDepthHist[1];
      } else if (depthFrames == 2) {
        ++queueDepthHist[2];
      } else if (depthFrames == 3) {
        ++queueDepthHist[3];
      } else {
        ++queueDepthHist[4];
      }
    };
    auto transition_congestion_state = [&](ClientCongestionState nextState, uint64_t nowUs, const char* reason,
                                           uint64_t streamLagUs, uint64_t decodeQueueLagEstimateUs, uint32_t seq) {
      if (nextState == congestionState) return;
      const ClientCongestionState prev = congestionState;
      if (prev != ClientCongestionState::Normal &&
          nextState == ClientCongestionState::Normal &&
          congestionStateEnterUs > 0 &&
          nowUs >= congestionStateEnterUs) {
        const uint64_t recoverUs = nowUs - congestionStateEnterUs;
        ++congestionRecoveryCount;
        congestionRecoveryTotalUs += recoverUs;
        if (recoverUs > congestionRecoveryMaxUs) congestionRecoveryMaxUs = recoverUs;
      }
      congestionState = nextState;
      congestionStateEnterUs = (nextState == ClientCongestionState::Normal) ? 0 : nowUs;
      if (nextState == ClientCongestionState::Recovering) {
        recoveringSinceUs = nowUs;
        recoveringHealthyStreak = 0;
      } else if (nextState != ClientCongestionState::Recovering) {
        recoveringSinceUs = 0;
        recoveringHealthyStreak = 0;
      }
      ++congestionTransitionCount;
      std::cout << "[native-video-client][congestion] state=" << congestion_state_name(nextState)
                << " prev=" << congestion_state_name(prev)
                << " reason=" << reason
                << " streamLagUs=" << streamLagUs
                << " decodeQueueLagUs=" << decodeQueueLagEstimateUs
                << " seq=" << seq
                << "\n";
    };
    struct PresentCounterSnapshot {
      uint64_t d3dPresentSuccess = 0;
      uint64_t d3dPresentFail = 0;
      uint64_t gdiFallbackPresented = 0;
      uint64_t fallbackInitFail = 0;
      uint64_t fallbackRenderFail = 0;
      uint64_t fallbackNv12ConvertFail = 0;
      uint64_t paintCoalesced = 0;
      uint64_t overwriteBeforePresent = 0;
    };
    auto load_present_counters = [&]() -> PresentCounterSnapshot {
      PresentCounterSnapshot s{};
      s.d3dPresentSuccess = gD3dPresentSuccessCount.load(std::memory_order_relaxed);
      s.d3dPresentFail = gD3dPresentFailCount.load(std::memory_order_relaxed);
      s.gdiFallbackPresented = gGdiFallbackPresentedCount.load(std::memory_order_relaxed);
      s.fallbackInitFail = gFallbackInitFailCount.load(std::memory_order_relaxed);
      s.fallbackRenderFail = gFallbackRenderFailCount.load(std::memory_order_relaxed);
      s.fallbackNv12ConvertFail = gFallbackNv12ConvertFailCount.load(std::memory_order_relaxed);
      s.paintCoalesced = gPaintCoalescedCount.load(std::memory_order_relaxed);
      s.overwriteBeforePresent = gOverwriteBeforePresentCount.load(std::memory_order_relaxed);
      return s;
    };
    PresentCounterSnapshot lastPresentCounters = load_present_counters();
    auto append_present_counter_fields = [&](std::ostream& os) {
      const PresentCounterSnapshot nowCounters = load_present_counters();
      const uint64_t d3dPresentSuccess = nowCounters.d3dPresentSuccess - lastPresentCounters.d3dPresentSuccess;
      const uint64_t d3dPresentFail = nowCounters.d3dPresentFail - lastPresentCounters.d3dPresentFail;
      const uint64_t gdiFallbackPresented =
          nowCounters.gdiFallbackPresented - lastPresentCounters.gdiFallbackPresented;
      const uint64_t fallbackInitFail = nowCounters.fallbackInitFail - lastPresentCounters.fallbackInitFail;
      const uint64_t fallbackRenderFail = nowCounters.fallbackRenderFail - lastPresentCounters.fallbackRenderFail;
      const uint64_t fallbackNv12ConvertFail =
          nowCounters.fallbackNv12ConvertFail - lastPresentCounters.fallbackNv12ConvertFail;
      const uint64_t paintCoalesced = nowCounters.paintCoalesced - lastPresentCounters.paintCoalesced;
      const uint64_t overwriteBeforePresent =
          nowCounters.overwriteBeforePresent - lastPresentCounters.overwriteBeforePresent;
      const uint64_t d3dAttempts = d3dPresentSuccess + d3dPresentFail;
      const uint64_t gdiFallbackRateX1000 = (d3dAttempts > 0)
          ? ((gdiFallbackPresented * 1000ULL) / d3dAttempts)
          : 0;
      os << " d3dPresentSuccess=" << d3dPresentSuccess
         << " d3dPresentFail=" << d3dPresentFail
         << " gdiFallbackPresented=" << gdiFallbackPresented
         << " gdiFallbackRateX1000=" << gdiFallbackRateX1000
         << " fallbackInitFail=" << fallbackInitFail
         << " fallbackRenderFail=" << fallbackRenderFail
         << " fallbackNv12ConvertFail=" << fallbackNv12ConvertFail
         << " paintCoalesced=" << paintCoalesced
         << " overwriteBeforePresent=" << overwriteBeforePresent;
      lastPresentCounters = nowCounters;
    };
    auto append_congestion_fields = [&](std::ostream& os) {
      const uint64_t recoveryAvgUs =
          (congestionRecoveryCount > 0) ? (congestionRecoveryTotalUs / congestionRecoveryCount) : 0;
      os << " congestionState=" << congestion_state_name(congestionState)
         << " congestionTransitions=" << congestionTransitionCount
         << " congestionRecoveryCount=" << congestionRecoveryCount
         << " congestionRecoveryAvgUs=" << recoveryAvgUs
         << " congestionRecoveryMaxUs=" << congestionRecoveryMaxUs
         << " congestionRecoveryReq=" << congestionRecoveryRequestCount
         << " staleDrops=" << staleDropCount
         << " holdLatestDrops=" << holdLatestDropCount
         << " burstDrops=" << burstDropCount
         << " staleRefRecoveries=" << staleReferenceRecoveryCount
         << " queueDepthSamples=" << queueDepthSampleCount
         << " queueDepthMax=" << queueDepthFramesMax
         << " queueDepthH0=" << queueDepthHist[0]
         << " queueDepthH1=" << queueDepthHist[1]
         << " queueDepthH2=" << queueDepthHist[2]
         << " queueDepthH3=" << queueDepthHist[3]
         << " queueDepthH4p=" << queueDepthHist[4];
    };
    auto aligned_lag_us = [&](uint64_t remoteTsUs, uint64_t localNowUs,
                              bool& timelineReady, uint64_t& remoteBaseUs, uint64_t& localBaseUs) -> uint64_t {
      if (!timelineReady || remoteTsUs < remoteBaseUs) {
        timelineReady = true;
        remoteBaseUs = remoteTsUs;
        localBaseUs = localNowUs;
        return 0;
      }
      const uint64_t remoteDeltaUs = remoteTsUs - remoteBaseUs;
      uint64_t expectedLocalUs = localBaseUs;
      if (std::numeric_limits<uint64_t>::max() - expectedLocalUs < remoteDeltaUs) {
        expectedLocalUs = std::numeric_limits<uint64_t>::max();
      } else {
        expectedLocalUs += remoteDeltaUs;
      }
      return (localNowUs >= expectedLocalUs) ? (localNowUs - expectedLocalUs) : 0;
    };
    auto publish_metrics = [&](uint32_t metricW, uint32_t metricH, uint64_t nowUs,
                               uint64_t avgLatencyUs, uint64_t maxLatencyUsLocal,
                               uint64_t avgDecodeTailUs, uint64_t maxDecodeTailUsLocal,
                               double mbpsLocal) {
      const uint64_t cappedRecvFpsX100 = std::min<uint64_t>(recvFrames * 100ULL, 0xFFFFFFFFULL);
      const uint64_t cappedDecodedFpsX100 = std::min<uint64_t>(decodedFrames * 100ULL, 0xFFFFFFFFULL);
      const double mbpsX1000 = mbpsLocal * 1000.0;
      uint32_t recvMbpsX1000 = 0;
      if (mbpsX1000 > 0.0) {
        recvMbpsX1000 = static_cast<uint32_t>(
            std::min<double>(mbpsX1000, static_cast<double>(0xFFFFFFFFu)));
      }
      gClientMetrics.width = metricW;
      gClientMetrics.height = metricH;
      gClientMetrics.recvFpsX100 = static_cast<uint32_t>(cappedRecvFpsX100);
      gClientMetrics.decodedFpsX100 = static_cast<uint32_t>(cappedDecodedFpsX100);
      gClientMetrics.recvMbpsX1000 = recvMbpsX1000;
      gClientMetrics.skippedFrames = static_cast<uint32_t>(std::min<uint64_t>(skippedQueued, 0xFFFFFFFFULL));
      gClientMetrics.avgLatencyUs = avgLatencyUs;
      gClientMetrics.maxLatencyUs = maxLatencyUsLocal;
      gClientMetrics.avgDecodeTailUs = avgDecodeTailUs;
      gClientMetrics.maxDecodeTailUs = maxDecodeTailUsLocal;
      gClientMetrics.congestionState = static_cast<uint32_t>(congestionState);
      gClientMetrics.congestionTransitions =
          static_cast<uint32_t>(std::min<uint64_t>(congestionTransitionCount, 0xFFFFFFFFULL));
      gClientMetrics.congestionRecoveryCount =
          static_cast<uint32_t>(std::min<uint64_t>(congestionRecoveryCount, 0xFFFFFFFFULL));
      gClientMetrics.congestionRecoveryReq =
          static_cast<uint32_t>(std::min<uint64_t>(congestionRecoveryRequestCount, 0xFFFFFFFFULL));
      gClientMetrics.congestionRecoveryMaxUs =
          static_cast<uint32_t>(std::min<uint64_t>(congestionRecoveryMaxUs, 0xFFFFFFFFULL));
      gClientMetrics.queueDepthMax = queueDepthFramesMax;
      gClientMetrics.queueDepthH4p =
          static_cast<uint32_t>(std::min<uint64_t>(queueDepthHist[4], 0xFFFFFFFFULL));
      gClientMetrics.udpAssemblyDropPm = udpAssemblyDropPmLast;
      gClientMetrics.seq.fetch_add(1);
      gClientMetrics.updatedQpcUs = nowUs;
      push_overlay_metric_sample(gClientMetrics.recvFpsX100.load(std::memory_order_relaxed),
                                 gClientMetrics.decodedFpsX100.load(std::memory_order_relaxed),
                                 gClientMetrics.recvMbpsX1000.load(std::memory_order_relaxed),
                                 gClientMetrics.avgLatencyUs.load(std::memory_order_relaxed),
                                 nowUs);
      if (gHwnd && !gWindowPickerVisible.load(std::memory_order_relaxed)) {
        if (!gPaintQueued.exchange(true)) {
          InvalidateRect(gHwnd, nullptr, FALSE);
        } else {
          ++gPaintCoalescedCount;
        }
      }
    };
    auto process_h264_frame = [&](const EncodedFrameHeader& h, std::vector<uint8_t>* payloadPtr,
                                  uint64_t packetNowUs) -> bool {
      if (!payloadPtr) return true;
      ++recvFrames;
      recvBytes += h.payloadSize;
      const uint64_t recvGapUs =
          (lastPacketRecvUs > 0 && packetNowUs >= lastPacketRecvUs) ? (packetNowUs - lastPacketRecvUs) : 0;
      lastPacketRecvUs = packetNowUs;
      if (recvGapUs > 250000) {
        // Sparse arrival usually means source/capture stall, not decoder backlog.
        lagTriggerStreak = 0;
      }

      if (!useH264) {
        ++skippedQueued;
        return true;
      }

      // Target-selection gate (mobile parity, Android commit 4892dea). While the user's pick is
      // resolving, keep the picker up and present nothing until the acknowledged generation's
      // first frame decodes.
      if (gSelectionEpoch.load(std::memory_order_acquire) != recvSelectionEpoch) {
        // A fresh pick: drop stale reference frames and hold for the new generation's keyframe.
        recvSelectionEpoch = gSelectionEpoch.load(std::memory_order_acquire);
        decoder.reset();
        waitForKeyFrame = true;
      }
      if (gSelectionPending.load(std::memory_order_acquire)) {
        if (gSelectionAwaitingAck.load(std::memory_order_acquire)) {
          // No ack yet: every frame here is either the old target or an unconfirmed guess.
          ++skippedQueued;
          return true;
        }
        const uint64_t expectedGen = gSelectionExpectedGeneration.load(std::memory_order_acquire);
        if (expectedGen != 0 && h.streamGeneration != expectedGen) {
          // The previous target's stream still draining after the ack; not what we selected.
          ++skippedQueued;
          return true;
        }
      } else {
        // No selection in flight. After a reveal, only the active target's generation is welcome:
        // a late straggler from the previously selected target, still in flight on the wire, would
        // otherwise flash on screen. gActiveStreamGeneration==0 means no PC-side selection has
        // taken effect (legacy stream-view start, or before the first pick), so accept anything as
        // before. Host auto-resolution changes keep the same generation, so this does not fight
        // them -- only a host-side target selection bumps the generation.
        const uint64_t activeGen = gActiveStreamGeneration.load(std::memory_order_acquire);
        if (activeGen != 0 && h.streamGeneration != activeGen) {
          ++skippedQueued;
          return true;
        }
      }

      if (!decoderReady || decoderW != h.width || decoderH != h.height) {
        if (!decoder.initialize(h.width, h.height, args.fpsHint)) {
          std::cerr << "[native-video-client] H264 decoder initialize failed size=" << h.width << "x" << h.height
                    << "\n";
          return false;
        }
    const std::string requestedDecoderBackend = env_string_or_empty("REMOTE60_NATIVE_DECODER_BACKEND");
    const std::string requestedDecoderBackendPrint =
        requestedDecoderBackend.empty() ? "default(mft_auto)" : requestedDecoderBackend;
        const std::string backendFallbackReason =
            backend_fallback_reason(requestedDecoderBackend, decoder.backend_name());
        std::cout << "[native-video-client] H264 decoder backend=" << decoder.backend_name()
                  << " backendRequested=" << requestedDecoderBackendPrint
                  << " backendResolved=" << decoder.backend_name()
                  << " backendFallbackReason=" << backendFallbackReason
                  << " hw=" << (decoder.using_hardware() ? 1 : 0)
                  << " size=" << h.width << "x" << h.height << "\n";
        decoderReady = true;
        decoderW = h.width;
        decoderH = h.height;
        waitForKeyFrame = true;
      }

      const bool keyFrame = ((h.flags & 1u) != 0);
      if (h.captureQpcUs > latestCaptureSeenUs) {
        latestCaptureSeenUs = h.captureQpcUs;
      }
      const uint64_t streamLagUs = aligned_lag_us(
          h.captureQpcUs, packetNowUs, captureTimelineReady, captureRemoteBaseUs, captureLocalBaseUs);
      const uint64_t presentedCapUs = gLastPresentedCaptureUs.load(std::memory_order_relaxed);
      const uint64_t decodeQueueLagEstimateUs =
          (presentedCapUs > 0 && h.captureQpcUs >= presentedCapUs)
              ? (h.captureQpcUs - presentedCapUs)
              : 0;
      sample_queue_depth(decodeQueueLagEstimateUs);
      const uint64_t staleBehindPresentedUs =
          (presentedCapUs > 0 && presentedCapUs > h.captureQpcUs)
              ? (presentedCapUs - h.captureQpcUs)
              : 0;
      const uint64_t staleBehindLatestUs =
          (latestCaptureSeenUs > h.captureQpcUs)
              ? (latestCaptureSeenUs - h.captureQpcUs)
              : 0;
      if (staleBehindPresentedUs > staleCaptureDropUs || staleBehindLatestUs > staleCaptureDropUs) {
        ++skippedQueued;
        ++lagDropCount;
        ++staleDropCount;
        if (staleBehindLatestUs > staleCaptureDropUs) {
          ++holdLatestDropCount;
        }
        // Dropping a frame that is NOT older than the last decoded keyframe breaks the still-live
        // reference chain. This is a B=0 low-latency IPPP stream and the wire header carries no
        // ref flag, so every such P must be treated as a reference: decoding later P-frames that
        // referenced the dropped one produces garbage (the corrupted text/scroll seen in the
        // field). Resync on the next IDR instead -- freeze on the last good frame until it lands.
        // A frame older than the anchor is a late/reordered straggler the decoder already resynced
        // past, so quiet-drop stays safe there. Recover once per gap; the wait gate below then
        // drops non-key frames until the IDR and request_keyframe's limiter throttles the ask.
        const bool inLiveReferenceChain = (h.captureQpcUs >= lastDecodedKeyCaptureUs);
        if (inLiveReferenceChain && !waitForKeyFrame) {
          waitForKeyFrame = true;
          decoder.reset();
          request_keyframe(6);  // stale_reference_gap
          ++congestionRecoveryRequestCount;
          ++staleReferenceRecoveryCount;
          std::cout << "[native-video-client] stale-reference recovery seq=" << h.seq
                    << " count=" << staleReferenceRecoveryCount
                    << " staleBehindLatestUs=" << staleBehindLatestUs << "\n";
        }
        if ((lagDropCount % 120) == 1) {
          std::cout << "[native-video-client] stale frame drop count=" << lagDropCount
                    << " staleBehindPresentedUs=" << staleBehindPresentedUs
                    << " staleBehindLatestUs=" << staleBehindLatestUs
                    << " inRefChain=" << (inLiveReferenceChain ? 1 : 0)
                    << " seq=" << h.seq << "\n";
        }
        return true;
      }

      const bool lagTrigger =
          (decodeQueueLagEstimateUs > kDecodeQueueLagDropUs) ||
          (presentedCapUs > 0 && streamLagUs > kCatchupLagDropUs);
      const bool denseArrival = (recvGapUs == 0 || recvGapUs <= 150000);
      // The picker overlay pauses presents on purpose; lag measured against a frozen present
      // anchor is not congestion. Same for the short post-close grace until the anchor is fresh.
      const bool catchupSuppressed =
          gWindowPickerVisible.load(std::memory_order_relaxed) ||
          packetNowUs < gCatchupSuppressUntilUs.load(std::memory_order_relaxed);
      if (lagTrigger && denseArrival && !catchupSuppressed) {
        if (lagTriggerStreak < std::numeric_limits<uint32_t>::max()) {
          ++lagTriggerStreak;
        }
      } else {
        lagTriggerStreak = 0;
      }
      if (congestionState != ClientCongestionState::Congested && lagTriggerStreak >= 3) {
        lagTriggerStreak = 0;
        const bool catchupEnterAllowed =
            (lastCatchupEnterUs == 0) || (packetNowUs >= (lastCatchupEnterUs + catchupReenterMinIntervalUs));
        if (!catchupEnterAllowed) {
          ++catchupEnterThrottledCount;
          if ((catchupEnterThrottledCount % 120) == 1) {
            std::cout << "[native-video-client] catchup-enter-throttled count="
                      << catchupEnterThrottledCount
                      << " streamLagUs=" << streamLagUs
                      << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                      << " minIntervalUs=" << catchupReenterMinIntervalUs
                      << "\n";
          }
        } else {
          transition_congestion_state(ClientCongestionState::Congested, packetNowUs,
                                      (decodeQueueLagEstimateUs > kDecodeQueueLagDropUs)
                                          ? "decode_queue"
                                          : "stream_lag_emergency",
                                      streamLagUs, decodeQueueLagEstimateUs, h.seq);
          catchupMode = true;
          lastCatchupEnterUs = packetNowUs;
          waitForKeyFrame = true;
          decoder.reset();
          request_keyframe(1);
          ++congestionRecoveryRequestCount;
          std::cout << "[native-video-client] catchup enter streamLagUs=" << streamLagUs
                    << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                    << " recvGapUs=" << recvGapUs
                    << " reason="
                    << ((decodeQueueLagEstimateUs > kDecodeQueueLagDropUs) ? "decode_queue" : "stream_lag_emergency")
                    << " seq=" << h.seq << "\n";
        }
      }
      if (congestionState == ClientCongestionState::Congested && !keyFrame) {
        decodeEmptyStreak = 0;
        decodeEmptyStreakStartUs = 0;
        ++skippedQueued;
        ++lagDropCount;
        ++burstDropCount;
        if ((lagDropCount % 120) == 1) {
          std::cout << "[native-video-client] catchup drops=" << lagDropCount
                    << " streamLagUs=" << streamLagUs
                    << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                    << "\n";
        }
        return true;
      }
      if (congestionState == ClientCongestionState::Congested && keyFrame) {
        catchupMode = false;
        transition_congestion_state(ClientCongestionState::Recovering, packetNowUs, "keyframe",
                                    streamLagUs, decodeQueueLagEstimateUs, h.seq);
        std::cout << "[native-video-client] catchup exit streamLagUs=" << streamLagUs
                  << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                  << " seq=" << h.seq << "\n";
      }
      if (congestionState == ClientCongestionState::Recovering) {
        const bool lagHealthy =
            decodeQueueLagEstimateUs <= kDecodeQueueLagResumeUs &&
            streamLagUs <= kCatchupResumeKeyLagUs;
        if (lagHealthy) {
          if (recoveringHealthyStreak < std::numeric_limits<uint32_t>::max()) {
            ++recoveringHealthyStreak;
          }
        } else {
          recoveringHealthyStreak = 0;
        }
        const bool recoverMinElapsed =
            recoveringSinceUs > 0 && packetNowUs >= (recoveringSinceUs + congestionRecoverMinUs);
        if (lagHealthy && recoverMinElapsed && recoveringHealthyStreak >= 3) {
          transition_congestion_state(ClientCongestionState::Normal, packetNowUs, "recover_stable",
                                      streamLagUs, decodeQueueLagEstimateUs, h.seq);
        } else if (!lagHealthy && !catchupSuppressed &&
                   recoveringSinceUs > 0 &&
                   packetNowUs >= (recoveringSinceUs + congestionRecoveryTimeoutUs)) {
          const bool requestAllowed =
              (lastRecoveryRequestUs == 0) || (packetNowUs >= (lastRecoveryRequestUs + 300000));
          if (requestAllowed) {
            request_keyframe(1);
            ++congestionRecoveryRequestCount;
            lastRecoveryRequestUs = packetNowUs;
          }
          catchupMode = true;
          waitForKeyFrame = true;
          decoder.reset();
          lastCatchupEnterUs = packetNowUs;
          transition_congestion_state(ClientCongestionState::Congested, packetNowUs, "recover_timeout",
                                      streamLagUs, decodeQueueLagEstimateUs, h.seq);
        }
      }

      if (waitForKeyFrame && !keyFrame) {
        decodeEmptyStreak = 0;
        decodeEmptyStreakStartUs = 0;
        ++skippedQueued;
        ++waitingKeyDropCount;
        ++burstDropCount;
        if ((waitingKeyDropCount % 30) == 1) {
          request_keyframe(3);
        }
        if ((waitingKeyDropCount % 120) == 1) {
          std::cout << "[native-video-client] waiting keyframe drops=" << waitingKeyDropCount << "\n";
        }
        if (packetNowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
          const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          const double decodedRawMbps = (decodedBytes * 8.0) / (1000.0 * 1000.0);
          const uint64_t decodeRatioX100 =
              (recvBytes > 0) ? ((decodedBytes * 100ULL) / recvBytes) : 0;
          publish_metrics(h.width, h.height, packetNowUs,
                          avgLatencyUs, maxLatencyUs, avgDecodeTailUs, maxDecodeTailUs, mbps);
          std::ostringstream oss;
          oss << "[native-video-client] recvFrames=" << recvFrames
              << " decodedFrames=" << decodedFrames
              << " skippedQueued=" << skippedQueued
              << " avgLatencyUs=" << avgLatencyUs
              << " maxLatencyUs=" << maxLatencyUs
              << " avgDecodeTailUs=" << avgDecodeTailUs
              << " maxDecodeTailUs=" << maxDecodeTailUs
              << " mbps=" << mbps
              << " decodedRawMbps=" << decodedRawMbps
              << " decodeRatioX100=" << decodeRatioX100
              << " size=" << h.width << "x" << h.height;
          append_congestion_fields(oss);
          append_present_counter_fields(oss);
          log_client_line(oss.str());
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          decodedBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
        return true;
      }

      const uint64_t decodeStartUs = qpc_now_us();
      std::vector<DecodedFrameNv12> outFrames;
      const int64_t inputSampleTimeHns = static_cast<int64_t>(h.captureQpcUs) * 10;
      bool pendingTimestampOverflow = false;
      if (!decoder.decode_access_unit(*payloadPtr, keyFrame, inputSampleTimeHns, &outFrames,
                                      &pendingTimestampOverflow)) {
        decodeEmptyStreak = 0;
        decodeEmptyStreakStartUs = 0;
        ++skippedQueued;
        ++decodeFailCount;
        request_keyframe(4);
        ++congestionRecoveryRequestCount;
        if ((decodeFailCount % 60) == 1) {
          std::cout << "[native-video-client] decode failed count=" << decodeFailCount << "\n";
        }
        catchupMode = true;
        lastCatchupEnterUs = packetNowUs;
        waitForKeyFrame = true;
        if (++decodeConsecutiveFailCount >= kDecodeRebuildThreshold) {
          // Flush did not clear it: the transform or device is wedged. A full rebuild is the
          // only recovery, and it is what the resolution-change path already does -- reached
          // here without a resolution change so the wedge is not caught otherwise.
          std::cout << "[native-video-client] decoder wedged (consecutive fails="
                    << decodeConsecutiveFailCount << "); rebuilding\n";
          if (decoder.initialize(decoderW, decoderH, args.fpsHint)) {
            decodeConsecutiveFailCount = 0;
          }
          // On rebuild failure, keep the streak so the next frame retries the rebuild.
        } else {
          decoder.reset();
        }
        transition_congestion_state(ClientCongestionState::Congested, packetNowUs, "decode_fail",
                                    streamLagUs, decodeQueueLagEstimateUs, h.seq);
        if (packetNowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
          const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          const double decodedRawMbps = (decodedBytes * 8.0) / (1000.0 * 1000.0);
          const uint64_t decodeRatioX100 =
              (recvBytes > 0) ? ((decodedBytes * 100ULL) / recvBytes) : 0;
          publish_metrics(h.width, h.height, packetNowUs,
                          avgLatencyUs, maxLatencyUs, avgDecodeTailUs, maxDecodeTailUs, mbps);
          std::ostringstream oss;
          oss << "[native-video-client] recvFrames=" << recvFrames
              << " decodedFrames=" << decodedFrames
              << " skippedQueued=" << skippedQueued
              << " avgLatencyUs=" << avgLatencyUs
              << " maxLatencyUs=" << maxLatencyUs
              << " avgDecodeTailUs=" << avgDecodeTailUs
              << " maxDecodeTailUs=" << maxDecodeTailUs
              << " mbps=" << mbps
              << " decodedRawMbps=" << decodedRawMbps
              << " decodeRatioX100=" << decodeRatioX100
              << " size=" << h.width << "x" << h.height;
          append_congestion_fields(oss);
          append_present_counter_fields(oss);
          log_client_line(oss.str());
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          decodedBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
        return true;
      }
      // decode_access_unit succeeded: the transform is healthy, so the wedge streak is clear.
      decodeConsecutiveFailCount = 0;
      if (pendingTimestampOverflow) {
        decodeEmptyStreak = 0;
        decodeEmptyStreakStartUs = 0;
        ++skippedQueued;
        ++decodeTimestampOverflowCount;
        request_keyframe(4);
        ++congestionRecoveryRequestCount;
        if ((decodeTimestampOverflowCount % 10ULL) == 1ULL) {
          std::cout << "[native-video-client] decoder timestamp queue overflow count="
                    << decodeTimestampOverflowCount << "\n";
        }
        catchupMode = true;
        lastCatchupEnterUs = packetNowUs;
        waitForKeyFrame = true;
        decoder.reset();
        transition_congestion_state(ClientCongestionState::Congested, packetNowUs, "decode_ts_overflow",
                                    streamLagUs, decodeQueueLagEstimateUs, h.seq);
        return true;
      }
      waitForKeyFrame = false;
      if (keyFrame) {
        // Advance the reference-chain anchor: a successfully decoded IDR resyncs the decoder, so
        // any later stale frame older than this is safe to quiet-drop.
        lastDecodedKeyCaptureUs = h.captureQpcUs;
      }
      if (outFrames.empty()) {
        ++decodeEmptyCount;
        ++decodeEmptyStreak;
        if (decodeEmptyStreak == 1) {
          decodeEmptyStreakStartUs = packetNowUs;
        }
        const uint64_t emptyStreakUs =
            (decodeEmptyStreakStartUs > 0 && packetNowUs >= decodeEmptyStreakStartUs)
                ? (packetNowUs - decodeEmptyStreakStartUs)
                : 0;
        if (decodeEmptyStreak >= 12 || emptyStreakUs >= 300000) {
          const bool catchupEnterAllowed =
              (lastCatchupEnterUs == 0) || (packetNowUs >= (lastCatchupEnterUs + catchupReenterMinIntervalUs));
          if (catchupEnterAllowed) {
            ++decodeEmptyRecoveryCount;
            waitForKeyFrame = true;
            catchupMode = true;
            lastCatchupEnterUs = packetNowUs;
            request_keyframe(5);
            ++congestionRecoveryRequestCount;
            decoder.reset();
            transition_congestion_state(ClientCongestionState::Congested, packetNowUs, "decode_empty",
                                        streamLagUs, decodeQueueLagEstimateUs, h.seq);
            if ((decodeEmptyRecoveryCount % 10) == 1) {
              std::cout << "[native-video-client] decode empty recovery count=" << decodeEmptyRecoveryCount
                        << " streak=" << decodeEmptyStreak
                        << " emptyUs=" << emptyStreakUs
                        << "\n";
            }
          } else {
            ++catchupEnterThrottledCount;
            if ((catchupEnterThrottledCount % 120) == 1) {
              std::cout << "[native-video-client] decode-empty-recovery-throttled count="
                        << catchupEnterThrottledCount
                        << " streak=" << decodeEmptyStreak
                        << " emptyUs=" << emptyStreakUs
                        << " minIntervalUs=" << catchupReenterMinIntervalUs
                        << "\n";
            }
          }
          decodeEmptyStreak = 0;
          decodeEmptyStreakStartUs = 0;
        }
        if ((decodeEmptyCount % 120) == 1) {
          std::cout << "[native-video-client] decode output empty count=" << decodeEmptyCount
                    << " streak=" << decodeEmptyStreak
                    << " emptyUs=" << emptyStreakUs
                    << "\n";
        }
        if (packetNowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
          const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          const double decodedRawMbps = (decodedBytes * 8.0) / (1000.0 * 1000.0);
          const uint64_t decodeRatioX100 =
              (recvBytes > 0) ? ((decodedBytes * 100ULL) / recvBytes) : 0;
          publish_metrics(h.width, h.height, packetNowUs,
                          avgLatencyUs, maxLatencyUs, avgDecodeTailUs, maxDecodeTailUs, mbps);
          std::ostringstream oss;
          oss << "[native-video-client] recvFrames=" << recvFrames
              << " decodedFrames=" << decodedFrames
              << " skippedQueued=" << skippedQueued
              << " avgLatencyUs=" << avgLatencyUs
              << " maxLatencyUs=" << maxLatencyUs
              << " avgDecodeTailUs=" << avgDecodeTailUs
              << " maxDecodeTailUs=" << maxDecodeTailUs
              << " mbps=" << mbps
              << " decodedRawMbps=" << decodedRawMbps
              << " decodeRatioX100=" << decodeRatioX100
              << " size=" << h.width << "x" << h.height;
          append_congestion_fields(oss);
          append_present_counter_fields(oss);
          log_client_line(oss.str());
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          decodedBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
        return true;
      }
      decodeEmptyStreak = 0;
      decodeEmptyStreakStartUs = 0;

      auto& decoded = outFrames.back();
      const bool tsFromMft = decoded.sampleTimeFromOutput && (decoded.sampleTimeHns > 0);
      const bool tsFromInputFallback = (!decoded.sampleTimeFromOutput) && (decoded.sampleTimeHns > 0);
      const bool tsFromHeaderFallback = (decoded.sampleTimeHns <= 0);
      const uint64_t decodedCaptureUs =
          tsFromHeaderFallback ? h.captureQpcUs : static_cast<uint64_t>(decoded.sampleTimeHns / 10);
      const char* tsSource = tsFromMft ? "mft" : (tsFromInputFallback ? "input_fallback" : "header_fallback");
      if (decoded.bytes.empty() && !decoded.surfaceTexture) {
        ++skippedQueued;
        waitForKeyFrame = true;
        return true;
      }
      const uint64_t decodedPayloadBytes = decoded.bytes.empty()
          ? (static_cast<uint64_t>(decoded.width) * decoded.height * 3 / 2)
          : static_cast<uint64_t>(decoded.bytes.size());
      const uint64_t decodeEndUs = qpc_now_us();
      std::shared_ptr<std::vector<uint8_t>> frameNv12;
      if (!decoded.bytes.empty()) {
        frameNv12 = std::make_shared<std::vector<uint8_t>>(std::move(decoded.bytes));
        if (!frameNv12 || frameNv12->empty()) {
          ++skippedQueued;
          waitForKeyFrame = true;
          return true;
        }
      }

      const uint64_t nowUs = qpc_now_us();
      const uint64_t queueSetUs = nowUs;
      const uint64_t decodeToQueueUs = (queueSetUs >= decodeEndUs) ? (queueSetUs - decodeEndUs) : 0;
      {
        std::lock_guard<std::mutex> lk(gFrame.mu);
        const uint64_t prevVersion = gFrame.version;
        const uint64_t lastPresentedVersion = gLastPresentedVersion.load(std::memory_order_relaxed);
        // Overwrites while the picker covers the stream are the intended latest-wins behavior of a
        // deliberately paused present, not a symptom -- keep them out of the telemetry.
        if (prevVersion > lastPresentedVersion &&
            !gWindowPickerVisible.load(std::memory_order_relaxed)) {
          ++gOverwriteBeforePresentCount;
        }
        gFrame.format = SharedFrame::PixelFormat::Nv12;
        gFrame.width = (decoded.visibleWidth > 0) ? decoded.visibleWidth : decoded.width;
        gFrame.height = (decoded.visibleHeight > 0) ? decoded.visibleHeight : decoded.height;
        gFrame.codedWidth = decoded.width;
        gFrame.codedHeight = decoded.height;
        gFrame.visibleLeft = decoded.visibleLeft;
        gFrame.visibleTop = decoded.visibleTop;
        gFrame.stride = decoded.width;
        gFrame.seq = h.seq;
        gFrame.captureUs = decodedCaptureUs;
        gFrame.encodeStartUs = h.encodeStartQpcUs;
        gFrame.encodeEndUs = h.encodeEndQpcUs;
        gFrame.sendUs = h.sendQpcUs;
        gFrame.recvUs = packetNowUs;
        gFrame.decodeStartUs = decodeStartUs;
        gFrame.decodeEndUs = decodeEndUs;
        gFrame.queueSetUs = queueSetUs;
        gFrame.decodeToQueueUs = decodeToQueueUs;
        gFrame.streamGeneration = h.streamGeneration;
        gFrame.key = keyFrame;
        gFrame.version = prevVersion + 1;
        gFrame.bytes = std::move(frameNv12);
        gFrame.surfaceSample = std::move(decoded.surfaceSample);
        gFrame.surfaceTexture = std::move(decoded.surfaceTexture);
        gFrame.surfaceSubresource = decoded.surfaceSubresource;
      }
      // First real frame of the acknowledged selection just landed. The gate above guarantees it
      // belongs to the selected generation; record the candidate and post the reveal once. The
      // picker flip, input guard and toolbar are committed on the UI thread (after revalidation),
      // not here, so a racing cancel/new-selection/disconnect cannot wrongly close the picker.
      if (gSelectionPending.load(std::memory_order_acquire) &&
          !gSelectionAwaitingAck.load(std::memory_order_acquire)) {
        post_pc_selection_reveal(h.streamGeneration,
                                 gSelectionEpoch.load(std::memory_order_acquire));
      }
      // While the picker overlays a live stream, WM_PAINT redraws the picker (not the video), so
      // a per-frame invalidate would repaint the whole card grid at video cadence for nothing.
      // The reveal above and the picker-close handler invalidate on their own, so the newest
      // decoded frame still shows the moment the picker leaves.
      if (gHwnd && !gWindowPickerVisible.load(std::memory_order_relaxed)) {
        if (!gPaintQueued.exchange(true)) {
          InvalidateRect(gHwnd, nullptr, FALSE);
        } else {
          ++gPaintCoalescedCount;
        }
      }

      if (args.traceEvery > 0 && (h.seq % args.traceEvery) == 0 &&
          (args.traceMax == 0 || gTraceRecvPrinted.load() < args.traceMax)) {
        const auto nowPrinted = gTraceRecvPrinted.fetch_add(1) + 1;
        if (args.traceMax == 0 || nowPrinted <= args.traceMax) {
          std::ostringstream oss;
          oss << "[native-video-client][trace_recv] seq=" << h.seq
              << " captureUs=" << decodedCaptureUs
              << " hdrCaptureUs=" << h.captureQpcUs
              << " encodeStartUs=" << h.encodeStartQpcUs
              << " encodeEndUs=" << h.encodeEndQpcUs
              << " sendUs=" << h.sendQpcUs
              << " recvUs=" << packetNowUs
              << " decodeStartUs=" << decodeStartUs
              << " decodeEndUs=" << decodeEndUs
              << " c2eUs=" << ((h.encodeStartQpcUs >= h.captureQpcUs) ? (h.encodeStartQpcUs - h.captureQpcUs) : 0)
              << " encUs=" << ((h.encodeEndQpcUs >= h.encodeStartQpcUs) ? (h.encodeEndQpcUs - h.encodeStartQpcUs) : 0)
              << " e2sUs=" << ((h.sendQpcUs >= h.encodeEndQpcUs) ? (h.sendQpcUs - h.encodeEndQpcUs) : 0)
              << " netUs=" << ((packetNowUs >= h.sendQpcUs) ? (packetNowUs - h.sendQpcUs) : 0)
              << " r2dUs=" << ((decodeStartUs >= packetNowUs) ? (decodeStartUs - packetNowUs) : 0)
              << " decUs=" << ((decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0)
              << " decodeQueueLagUs=" << ((h.captureQpcUs >= decodedCaptureUs) ? (h.captureQpcUs - decodedCaptureUs) : 0)
              << " tsSource=" << tsSource
              << " bytes=" << h.payloadSize
              << " key=" << (keyFrame ? 1 : 0);
          log_client_line(oss.str());
        }
      }

      // GNLink stream telemetry (diagnostics only): one line per decoded keyframe, plus any
      // non-key frame whose decode cost ran past 1.5x the expected frame interval. Joins the host
      // 'wire seq=' log by seq+gen. decodeQueueLagUs is the capture-lag estimate already computed
      // for scheduling (not a literal decoder input-queue count -- the MFT does not expose one).
      {
        const uint64_t decodeUs = (decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0;
        const uint64_t decodeAnomalyUs = (frameIntervalUs * 3ULL) / 2ULL;
        if (keyFrame || decodeUs > decodeAnomalyUs) {
          const uint64_t r2dUs = (decodeStartUs >= packetNowUs) ? (decodeStartUs - packetNowUs) : 0;
          std::ostringstream telem;
          telem << "[native-video-client][telemetry] stage=decode"
                << " seq=" << h.seq
                << " gen=" << h.streamGeneration
                << " key=" << (keyFrame ? 1 : 0)
                << " recvUs=" << packetNowUs
                << " decodeStartUs=" << decodeStartUs
                << " decodeEndUs=" << decodeEndUs
                << " decodeUs=" << decodeUs
                << " r2dUs=" << r2dUs
                << " decodeQueueLagUs=" << decodeQueueLagEstimateUs
                << " pts=" << decodedCaptureUs;
          log_client_line(telem.str());
        }
      }

      ++decodedFrames;
      decodedBytes += decodedPayloadBytes;
      // lastPresentedCaptureUs is now updated by render thread via gLastPresentedCaptureUs
      const uint64_t latencyUs = aligned_lag_us(
          decodedCaptureUs, nowUs, captureTimelineReady, captureRemoteBaseUs, captureLocalBaseUs);
      const uint64_t decodeTailUs = aligned_lag_us(
          h.sendQpcUs, nowUs, sendTimelineReady, sendRemoteBaseUs, sendLocalBaseUs);
      sumLatencyUs += latencyUs;
      sumDecodeTailUs += decodeTailUs;
      maxLatencyUs = std::max(maxLatencyUs, latencyUs);
      maxDecodeTailUs = std::max(maxDecodeTailUs, decodeTailUs);

      if (nowUs >= statAtUs) {
        const uint64_t avgLatencyUs = (decodedFrames > 0) ? (sumLatencyUs / decodedFrames) : 0;
        const uint64_t avgDecodeTailUs = (decodedFrames > 0) ? (sumDecodeTailUs / decodedFrames) : 0;
        const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
        const double decodedRawMbps = (decodedBytes * 8.0) / (1000.0 * 1000.0);
        const uint64_t decodeRatioX100 =
            (recvBytes > 0) ? ((decodedBytes * 100ULL) / recvBytes) : 0;
        const uint32_t visibleW = (decoded.visibleWidth > 0) ? decoded.visibleWidth : decoded.width;
        const uint32_t visibleH = (decoded.visibleHeight > 0) ? decoded.visibleHeight : decoded.height;
        publish_metrics(visibleW, visibleH, nowUs,
                        avgLatencyUs, maxLatencyUs, avgDecodeTailUs, maxDecodeTailUs, mbps);
        std::ostringstream oss;
        oss << "[native-video-client] recvFrames=" << recvFrames
            << " decodedFrames=" << decodedFrames
            << " skippedQueued=" << skippedQueued
            << " avgLatencyUs=" << avgLatencyUs
            << " maxLatencyUs=" << maxLatencyUs
            << " avgDecodeTailUs=" << avgDecodeTailUs
            << " maxDecodeTailUs=" << maxDecodeTailUs
            << " mbps=" << mbps
            << " decodedRawMbps=" << decodedRawMbps
            << " decodeRatioX100=" << decodeRatioX100
            << " size=" << visibleW << "x" << visibleH
            << " codedSize=" << decoded.width << "x" << decoded.height;
        append_congestion_fields(oss);
        append_present_counter_fields(oss);
        log_client_line(oss.str());
        recvFrames = 0;
        decodedFrames = 0;
        skippedQueued = 0;
        recvBytes = 0;
        decodedBytes = 0;
        sumLatencyUs = 0;
        maxLatencyUs = 0;
        sumDecodeTailUs = 0;
        maxDecodeTailUs = 0;
        statAtUs += 1000000ULL;
      }
      return true;
    };

    if (transport == VideoTransport::Udp) {
      std::array<uint8_t, 1600> datagram{};
      const uint32_t effectiveUdpSimDropSeed = (udpSimDropSeed > 0)
                                                   ? udpSimDropSeed
                                                   : static_cast<uint32_t>(qpc_now_us() & 0x7fffffffu);
      std::minstd_rand udpSimRng(effectiveUdpSimDropSeed);
      std::uniform_int_distribution<uint32_t> udpSimDropDist(0, 999);
      UdpH264FrameAssembler assembler;
      uint64_t assemblyDropped = 0;
      uint64_t oversizePayloadDropCount = 0;
      uint64_t udpSimDroppedCount = 0;
      uint64_t udpSimAcceptedCount = 0;
      uint64_t udpAssemblyStatAtUs = qpc_now_us() + 1000000ULL;
      uint64_t lastUdpChunkRecvCount = 0;
      uint64_t lastUdpAssemblyCompletedCount = 0;
      uint64_t lastUdpAssemblyDroppedCount = 0;
      uint64_t lastUdpAssemblyMalformedCount = 0;
      uint64_t lastUdpAssemblyReorderCount = 0;
      uint64_t lastUdpAssemblyKeyReqCount = 0;
      uint64_t lastUdpAssemblyFecRecoveredCount = 0;
      uint64_t lastUdpSimDroppedCount = 0;
      uint64_t lastUdpSimAcceptedCount = 0;

      while (gRunning.load()) {
        const int n = recv(gSock, reinterpret_cast<char*>(datagram.data()), static_cast<int>(datagram.size()), 0);
        if (n <= 0) {
          // A read timeout is not a dead socket. It is also the tunnel's heartbeat: the control
          // thread spends most of its time blocked waiting for a reply, so if retransmission
          // were driven from there it would stop exactly when a reply goes missing -- and the
          // host, hearing nothing, declares the client lost. This thread always runs.
          if (remote60::native_poc::last_socket_error_is_retryable()) {
            if (gControlOverUdp.load(std::memory_order_acquire)) gUdpControl.Tick();
            continue;
          }
          break;
        }
        if (gControlOverUdp.load(std::memory_order_acquire)) gUdpControl.Tick();
        // Control is offered the datagram BEFORE the video length guard, and the order is the
        // whole point. A control message is not bounded below by the video header: a
        // single-fragment input ack is 32 + 28 = 60 bytes against an 88-byte video header, so
        // checking the video size first silently ate every small reply -- input acks and window
        // selections -- while the larger ones (pong, window lists) came through and made the
        // channel look healthy. OnPacket claims only its own kinds, so video cannot be stolen.
        if (gControlOverUdp.load(std::memory_order_acquire) &&
            gUdpControl.OnPacket(datagram.data(), static_cast<size_t>(n))) {
          continue;
        }
        // Remote hardware-cursor sample: smaller than the video header, so it must be claimed
        // before the size guard below silently eats it. Latest-wins into atomics; the UI timer
        // does the mapping and drawing.
        if (n == static_cast<int>(sizeof(remote60::native_poc::UdpCursorPosPacket))) {
          remote60::native_poc::UdpCursorPosPacket cp{};
          std::memcpy(&cp, datagram.data(), sizeof(cp));
          if (cp.magic == remote60::native_poc::kMagic &&
              cp.kind == static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::CursorPos) &&
              cp.size == sizeof(cp)) {
            // Bounds sanity before the values reach mapping math: a malformed peer packet must
            // not be able to feed the clamp arithmetic absurd dimensions. Claimed either way.
            if (cp.captureW >= 2 && cp.captureW <= 16384 && cp.captureH >= 2 &&
                cp.captureH <= 16384) {
              gRemoteCursorX.store(cp.x, std::memory_order_relaxed);
              gRemoteCursorY.store(cp.y, std::memory_order_relaxed);
              gRemoteCursorCapW.store(cp.captureW, std::memory_order_relaxed);
              gRemoteCursorCapH.store(cp.captureH, std::memory_order_relaxed);
              gRemoteCursorGeneration.store(cp.streamGeneration, std::memory_order_relaxed);
              gRemoteCursorVisible.store((cp.flags & 0x1u) != 0, std::memory_order_relaxed);
              gRemoteCursorUpdateUs.store(qpc_now_us(), std::memory_order_release);
            }
            continue;
          }
        }
        if (n < static_cast<int>(sizeof(UdpVideoChunkHeader))) continue;

        UdpVideoChunkHeader u{};
        std::memcpy(&u, datagram.data(), sizeof(u));
        if (u.magic != remote60::native_poc::kMagic ||
            u.kind != static_cast<uint16_t>(UdpPacketKind::VideoChunk) ||
            u.size != sizeof(UdpVideoChunkHeader)) {
          continue;
        }
        if (u.codec != static_cast<uint16_t>(UdpCodec::H264)) {
          ++skippedQueued;
          continue;
        }
        if (udpSimDropPm > 0) {
          const uint32_t samplePm = udpSimDropDist(udpSimRng);
          if (samplePm < udpSimDropPm) {
            ++udpSimDroppedCount;
            ++skippedQueued;
            continue;
          }
        }
        ++udpSimAcceptedCount;
        ++udpChunkRecvCount;

        const auto assembleResult = assembler.PushDatagram(datagram.data(), static_cast<size_t>(n));
        if (assembleResult.fecRecovered) {
          udpAssemblyFecRecoveredCount += assembleResult.fecRecoveredChunks;
        }
        bool discontinuityHandled = false;
        auto handle_udp_discontinuity = [&]() {
          if (discontinuityHandled) return;
          discontinuityHandled = true;
          waitForKeyFrame = true;
          decoder.reset();
          request_keyframe(2);
          ++udpAssemblyKeyReqCount;
        };
        if (assembleResult.droppedPreviousIncomplete) {
          ++assemblyDropped;
          ++udpAssemblyDroppedCount;
          handle_udp_discontinuity();
        }

        if (assembleResult.disposition == UdpH264AssemblyDisposition::Malformed) {
          ++skippedQueued;
          ++udpAssemblyMalformedCount;
          handle_udp_discontinuity();
          if (assembleResult.oversizePayload && ((++oversizePayloadDropCount % 30ULL) == 1ULL)) {
            std::cout << "[native-video-client] dropped oversized udp payload bytes="
                      << assembleResult.rejectedPayloadSize
                      << " count=" << oversizePayloadDropCount << "\n";
          }
          continue;
        }

        if (assembleResult.disposition == UdpH264AssemblyDisposition::Dropped) {
          ++skippedQueued;
          ++assemblyDropped;
          ++udpAssemblyDroppedCount;
          if (assembleResult.reorderDetected) ++udpAssemblyReorderCount;
          handle_udp_discontinuity();
          if ((assemblyDropped % 120) == 1) {
            std::cout << "[native-video-client] udp assembly drop count=" << assemblyDropped
                      << " seq=" << u.seq
                      << " expectedSeq=" << assembleResult.expectedSeq
                      << " chunkOffset=" << u.chunkOffset
                      << " nextOffset=" << assembleResult.expectedNextOffset
                      << "\n";
          }
          continue;
        }

        if (assembleResult.disposition == UdpH264AssemblyDisposition::Completed) {
          ++udpAssemblyCompletedCount;
          const uint64_t packetNowUs = qpc_now_us();
          // GNLink stream telemetry (diagnostics only): one line per assembled keyframe, plus any
          // non-key frame that needed FEC repair or showed loss/reorder, so a periodic-stutter
          // session joins the host 'wire seq=' log by seq+gen while steady play stays quiet.
          {
            const auto& fh = assembleResult.frame.header;
            const bool key = ((fh.flags & 1u) != 0);
            if (key || assembleResult.fecRecovered || assembleResult.reorderDetected ||
                assembleResult.droppedPreviousIncomplete) {
              std::ostringstream telem;
              telem << "[native-video-client][telemetry] stage=assembly"
                    << " seq=" << fh.seq
                    << " gen=" << fh.streamGeneration
                    << " key=" << (key ? 1 : 0)
                    << " lastChunkRecvUs=" << packetNowUs
                    << " bytes=" << fh.payloadSize
                    << " fecRecovered=" << (assembleResult.fecRecovered ? 1 : 0)
                    << " fecRecoveredChunks=" << assembleResult.fecRecoveredChunks
                    << " reorder=" << (assembleResult.reorderDetected ? 1 : 0)
                    << " droppedPrev=" << (assembleResult.droppedPreviousIncomplete ? 1 : 0);
              log_client_line(telem.str());
            }
          }
          auto payload = std::move(assembleResult.frame.payload);
          if (!process_h264_frame(assembleResult.frame.header, &payload, packetNowUs)) break;
        }

        const uint64_t nowUs = qpc_now_us();
        if (nowUs >= udpAssemblyStatAtUs) {
          const uint64_t chunksDelta = udpChunkRecvCount - lastUdpChunkRecvCount;
          const uint64_t completedDelta = udpAssemblyCompletedCount - lastUdpAssemblyCompletedCount;
          const uint64_t droppedDelta = udpAssemblyDroppedCount - lastUdpAssemblyDroppedCount;
          const uint64_t malformedDelta = udpAssemblyMalformedCount - lastUdpAssemblyMalformedCount;
          const uint64_t reorderDelta = udpAssemblyReorderCount - lastUdpAssemblyReorderCount;
          const uint64_t keyReqDelta = udpAssemblyKeyReqCount - lastUdpAssemblyKeyReqCount;
          const uint64_t fecRecoveredDelta =
              udpAssemblyFecRecoveredCount - lastUdpAssemblyFecRecoveredCount;
          const uint64_t simDroppedDelta = udpSimDroppedCount - lastUdpSimDroppedCount;
          const uint64_t simAcceptedDelta = udpSimAcceptedCount - lastUdpSimAcceptedCount;
          const uint64_t simTotalDelta = simDroppedDelta + simAcceptedDelta;
          const uint64_t simDropPermille = (simTotalDelta > 0)
              ? ((simDroppedDelta * 1000ULL) / simTotalDelta)
              : 0;
          const uint64_t totalFramesDelta = completedDelta + droppedDelta;
          const uint64_t dropPermille = (totalFramesDelta > 0)
              ? ((droppedDelta * 1000ULL) / totalFramesDelta)
              : 0;
          udpAssemblyDropPmLast = static_cast<uint32_t>(std::min<uint64_t>(dropPermille, 1000ULL));
          std::cout << "[native-video-client] udp-assembly chunks=" << chunksDelta
                    << " completed=" << completedDelta
                    << " dropped=" << droppedDelta
                    << " dropPm=" << dropPermille
                    << " malformed=" << malformedDelta
                    << " reorder=" << reorderDelta
                    << " keyReq=" << keyReqDelta
                    << " fecRecovered=" << fecRecoveredDelta
                    << " simDropPm=" << simDropPermille
                    << " simDropTotal=" << simDroppedDelta
                    << " waitForKey=" << (waitForKeyFrame ? 1 : 0)
                    << " catchup=" << (catchupMode ? 1 : 0)
                    << "\n";
          lastUdpChunkRecvCount = udpChunkRecvCount;
          lastUdpAssemblyCompletedCount = udpAssemblyCompletedCount;
          lastUdpAssemblyDroppedCount = udpAssemblyDroppedCount;
          lastUdpAssemblyMalformedCount = udpAssemblyMalformedCount;
          lastUdpAssemblyReorderCount = udpAssemblyReorderCount;
          lastUdpAssemblyKeyReqCount = udpAssemblyKeyReqCount;
          lastUdpAssemblyFecRecoveredCount = udpAssemblyFecRecoveredCount;
          lastUdpSimDroppedCount = udpSimDroppedCount;
          lastUdpSimAcceptedCount = udpSimAcceptedCount;
          udpAssemblyStatAtUs += 1000000ULL;
        }
        if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
          break;
        }
      }

      gRunning = false;
      if (gHwnd) PostMessageW(gHwnd, WM_CLOSE, 0, 0);
      return;
    }

    while (gRunning.load()) {
      MessageHeader header{};
      if (!remote60::native_poc::recv_all(gSock, &header, sizeof(header))) break;
      if (header.magic != remote60::native_poc::kMagic || header.size < sizeof(header)) break;
      const auto msgType = static_cast<MessageType>(header.type);

      if (msgType == MessageType::RawFrameBgra && header.size == sizeof(RawFrameHeader)) {
        RawFrameHeader h{};
        h.header = header;
        if (!remote60::native_poc::recv_all(gSock, &h.seq, sizeof(h) - sizeof(MessageHeader))) break;
        std::vector<uint8_t> payload(h.payloadSize);
        if (!remote60::native_poc::recv_all(gSock, payload.data(), payload.size())) break;

        if (!useRaw) {
          ++skippedQueued;
          continue;
        }

        const uint64_t nowUs = qpc_now_us();
        const uint64_t queueSetUs = nowUs;
        auto frameBgra = std::make_shared<std::vector<uint8_t>>(std::move(payload));
        if (!frameBgra || frameBgra->empty()) {
          ++skippedQueued;
          continue;
        }
        {
          std::lock_guard<std::mutex> lk(gFrame.mu);
          const uint64_t prevVersion = gFrame.version;
          const uint64_t lastPresentedVersion = gLastPresentedVersion.load(std::memory_order_relaxed);
          if (prevVersion > lastPresentedVersion) {
            ++gOverwriteBeforePresentCount;
          }
          gFrame.format = SharedFrame::PixelFormat::Bgra32;
          gFrame.width = h.width;
          gFrame.height = h.height;
          gFrame.codedWidth = h.width;
          gFrame.codedHeight = h.height;
          gFrame.visibleLeft = 0;
          gFrame.visibleTop = 0;
          gFrame.stride = h.stride;
          gFrame.seq = h.seq;
          gFrame.captureUs = h.captureQpcUs;
          gFrame.encodeStartUs = h.encodeStartQpcUs;
          gFrame.encodeEndUs = h.encodeEndQpcUs;
          gFrame.sendUs = h.sendQpcUs;
          gFrame.recvUs = nowUs;
          gFrame.decodeStartUs = nowUs;
          gFrame.decodeEndUs = nowUs;
          gFrame.queueSetUs = queueSetUs;
          gFrame.decodeToQueueUs = 0;
          gFrame.streamGeneration = h.streamGeneration;
          gFrame.key = false;  // raw BGRA has no keyframe concept; keeps present telemetry quiet.
          gFrame.version = prevVersion + 1;
          gFrame.bytes = std::move(frameBgra);
          gFrame.surfaceSample.Reset();
          gFrame.surfaceTexture.Reset();
          gFrame.surfaceSubresource = 0;
        }
        if (gHwnd) {
          if (!gPaintQueued.exchange(true)) {
            InvalidateRect(gHwnd, nullptr, FALSE);
          } else {
            ++gPaintCoalescedCount;
          }
        }

        if (args.traceEvery > 0 && (h.seq % args.traceEvery) == 0 &&
            (args.traceMax == 0 || gTraceRecvPrinted.load() < args.traceMax)) {
          const auto nowPrinted = gTraceRecvPrinted.fetch_add(1) + 1;
          if (args.traceMax == 0 || nowPrinted <= args.traceMax) {
            std::ostringstream oss;
            oss << "[native-video-client][trace_recv] seq=" << h.seq
                << " captureUs=" << h.captureQpcUs
                << " encodeStartUs=" << h.encodeStartQpcUs
                << " encodeEndUs=" << h.encodeEndQpcUs
                << " sendUs=" << h.sendQpcUs
                << " recvUs=" << nowUs
                << " decodeStartUs=" << nowUs
                << " decodeEndUs=" << nowUs
                << " c2eUs=" << ((h.encodeStartQpcUs >= h.captureQpcUs) ? (h.encodeStartQpcUs - h.captureQpcUs) : 0)
                << " encUs=" << ((h.encodeEndQpcUs >= h.encodeStartQpcUs) ? (h.encodeEndQpcUs - h.encodeStartQpcUs) : 0)
                << " e2sUs=" << ((h.sendQpcUs >= h.encodeEndQpcUs) ? (h.sendQpcUs - h.encodeEndQpcUs) : 0)
                << " netUs=" << ((nowUs >= h.sendQpcUs) ? (nowUs - h.sendQpcUs) : 0)
                << " r2dUs=0"
                << " decUs=0"
                << " bytes=" << h.payloadSize;
            log_client_line(oss.str());
          }
        }

        ++recvFrames;
        ++decodedFrames;
        recvBytes += h.payloadSize;
        decodedBytes += static_cast<uint64_t>(h.payloadSize);
        const uint64_t latencyUs = (nowUs >= h.captureQpcUs) ? (nowUs - h.captureQpcUs) : 0;
        const uint64_t decodeTailUs = (nowUs >= h.sendQpcUs) ? (nowUs - h.sendQpcUs) : 0;
        sumLatencyUs += latencyUs;
        sumDecodeTailUs += decodeTailUs;
        maxLatencyUs = std::max(maxLatencyUs, latencyUs);
        maxDecodeTailUs = std::max(maxDecodeTailUs, decodeTailUs);

        if (nowUs >= statAtUs) {
          const uint64_t avgLatencyUs = (recvFrames > 0) ? (sumLatencyUs / recvFrames) : 0;
          const uint64_t avgDecodeTailUs = (recvFrames > 0) ? (sumDecodeTailUs / recvFrames) : 0;
          const double mbps = (recvBytes * 8.0) / (1000.0 * 1000.0);
          const double decodedRawMbps = (decodedBytes * 8.0) / (1000.0 * 1000.0);
          const uint64_t decodeRatioX100 =
              (recvBytes > 0) ? ((decodedBytes * 100ULL) / recvBytes) : 0;
          publish_metrics(h.width, h.height, nowUs,
                          avgLatencyUs, maxLatencyUs, avgDecodeTailUs, maxDecodeTailUs, mbps);
          std::ostringstream oss;
          oss << "[native-video-client] recvFrames=" << recvFrames
              << " decodedFrames=" << decodedFrames
              << " skippedQueued=" << skippedQueued
              << " avgLatencyUs=" << avgLatencyUs
              << " maxLatencyUs=" << maxLatencyUs
              << " avgDecodeTailUs=" << avgDecodeTailUs
              << " maxDecodeTailUs=" << maxDecodeTailUs
              << " mbps=" << mbps
              << " decodedRawMbps=" << decodedRawMbps
              << " decodeRatioX100=" << decodeRatioX100
              << " size=" << h.width << "x" << h.height;
          append_congestion_fields(oss);
          append_present_counter_fields(oss);
          log_client_line(oss.str());
          recvFrames = 0;
          decodedFrames = 0;
          skippedQueued = 0;
          recvBytes = 0;
          decodedBytes = 0;
          sumLatencyUs = 0;
          maxLatencyUs = 0;
          sumDecodeTailUs = 0;
          maxDecodeTailUs = 0;
          statAtUs += 1000000ULL;
        }
      } else if (msgType == MessageType::EncodedFrameH264 && header.size == sizeof(EncodedFrameHeader)) {
        EncodedFrameHeader h{};
        h.header = header;
        if (!remote60::native_poc::recv_all(gSock, &h.seq, sizeof(h) - sizeof(MessageHeader))) break;
        std::vector<uint8_t> payload(h.payloadSize);
        if (!remote60::native_poc::recv_all(gSock, payload.data(), payload.size())) break;
        const uint64_t packetNowUs = qpc_now_us();
        if (!process_h264_frame(h, &payload, packetNowUs)) break;
      } else {
        const size_t bodySize = static_cast<size_t>(header.size - sizeof(header));
        if (bodySize > 0 && !remote60::native_poc::recv_discard(gSock, bodySize)) break;
        ++skippedQueued;
      }

      const uint64_t nowUs = qpc_now_us();
      if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
        break;
      }
    }
    gRunning = false;
    if (gHwnd) PostMessageW(gHwnd, WM_CLOSE, 0, 0);
  });

  MSG msg{};
  while (gRunning.load()) {
    bool hadMessage = false;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      hadMessage = true;
      if (msg.message == WM_QUIT) {
        gRunning = false;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (!gRunning.load()) break;

    // The toolbar shows connection, input, path, frame rate and the monitor list, all of which
    // change on other threads. Refreshing it on a slow tick here beats a push at each of the
    // dozen places that move them, and it is a posted message either way.
    {
      static uint64_t nextToolbarPushUs = 0;
      const uint64_t nowUs = qpc_now_us();
      if (nowUs >= nextToolbarPushUs) {
        nextToolbarPushUs = nowUs + 500000ULL;
        push_session_toolbar_state();
      }
    }

    if (args.seconds > 0) {
      const uint64_t nowUs = qpc_now_us();
      if (nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
        gRunning = false;
        break;
      }
    }

    if (!hadMessage) {
      Sleep(5);
    }
  }

  gRunning = false;
  gInputEnabled = false;
  // Before anything is joined: the control thread can be parked in a blocking receive for the
  // read timeout, and closing the channel is what wakes it. Otherwise shutdown waits it out.
  gUdpControl.Close(remote60::native_poc::ControlCloseReason::Shutdown);
  gInputMacro.StopPlayback();
  gInputMacro.StopRecording();
  remote60::native_poc::macro_window_destroy();
  if (gSock != INVALID_SOCKET) {
    shutdown(gSock, SD_BOTH);
    closesocket(gSock);
    gSock = INVALID_SOCKET;
  }
  if (controlSock != INVALID_SOCKET) {
    shutdown(controlSock, SD_BOTH);
    closesocket(controlSock);
    controlSock = INVALID_SOCKET;
  }
  if (controlThread.joinable()) controlThread.join();
  if (recvThread.joinable()) recvThread.join();

  if (useH264) {
    {
      std::lock_guard<std::mutex> lk(gFrame.mu);
      gFrame.surfaceSample.Reset();
      gFrame.surfaceTexture.Reset();
      gFrame.bytes.reset();
    }
    decoder.shutdown();
    if (mfStarted) MFShutdown();
  }

  std::cout << "[native-video-client] done\n";
  return 0;
}

// See viewer_overlay_draw.hpp. Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0).

#include "viewer_log.hpp"
#include "viewer_overlay_draw.hpp"
#include "viewer_picker.hpp"

#include "viewer_common.hpp"
#include "viewer_gdi_util.hpp"
#include "viewer_state.hpp"
#include "viewer_layout.hpp"

namespace remote60::native_poc::viewer {

void apply_runtime_tune_delta(ViewerState& ctx, int bitrateStep, int keyintStep) {
  ctx.control.runtimeTune.ApplyDelta(
      bitrateStep, keyintStep, ctx.metrics.Snapshot().recvMbpsX1000);
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

void draw_target_card(ViewerState& ctx, HDC hdc, const RECT& card, const CardGridMetrics& grid,
                      uint64_t windowId, const std::string& title, bool active, bool disabled) {
  const RECT thumbRect = make_rect(card.left, card.top, card.right - card.left, grid.thumbH);
  const RECT captionRect = make_rect(card.left, card.top + grid.thumbH, card.right - card.left,
                                     card.bottom - card.top - grid.thumbH);

  FillRect(hdc, &thumbRect, cached_brush(ctx, RGB(24, 28, 36)));
  // Selected cards are tinted, not filled: a filled card and a filled button would read as the
  // same kind of thing, and the button is an action while the card is a state.
  FillRect(hdc, &captionRect, cached_brush(ctx, active ? RGB(26, 48, 84) : RGB(32, 37, 46)));

  // Snapshot under the lock, draw outside it: StretchDIBits under ctx.picker.thumbMu made the fetch
  // thread and the paint stall each other.
  std::shared_ptr<const WindowThumb> thumb;
  {
    std::lock_guard<std::mutex> lk(ctx.picker.thumbMu);
    const auto it = ctx.picker.thumbs.find(windowId);
    if (it != ctx.picker.thumbs.end()) thumb = it->second;
  }
  if (thumb) {
    draw_thumbnail_into(hdc, thumbRect, *thumb);
  } else {
    RECT ph = thumbRect;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(110, 118, 130));
    // The desktop card's caption already says what it is; repeating it inside the empty preview
    // put the same two words twice in one card.
    if (windowId != 0) {
      draw_text_utf8(ctx, hdc, std::string("미리보기 준비 중…"), &ph,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  }

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, disabled ? RGB(150, 155, 162) : RGB(236, 239, 243));
  RECT text = captionRect;
  text.left += dpi_scale(ctx, 10);
  text.right -= dpi_scale(ctx, 10);
  draw_text_utf8(ctx, hdc, title, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  RECT frame = card;
  FrameRect(hdc, &frame, cached_brush(ctx, active ? RGB(59, 130, 246) : RGB(52, 58, 70)));
  if (active) {
    RECT inner{card.left + 1, card.top + 1, card.right - 1, card.bottom - 1};
    FrameRect(hdc, &inner, cached_brush(ctx, RGB(59, 130, 246)));

    // A mark, not just a colour. Which card is selected has to survive a screenshot in grey, a
    // monitor with the colours wrong, and eyes that do not separate blue from grey-blue.
    const int side = dpi_scale(ctx, 18);
    const int pad = dpi_scale(ctx, 6);
    const RECT badge{card.right - side - pad, card.top + pad, card.right - pad,
                     card.top + side + pad};
    FillRect(hdc, &badge, cached_brush(ctx, RGB(59, 130, 246)));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    RECT tick = badge;
    draw_text_utf8(ctx, hdc, std::string("\u2713"), &tick,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

void draw_overlay(ViewerState& ctx, HDC hdc) {
  const ClientLayout layout = compute_client_layout(ctx, ctx.session.hwnd);
  const bool pickerVisible = ctx.picker.visible.load(std::memory_order_relaxed);

  // Said once per showing, not once per paint. "The flag flipped" and "something was actually
  // drawn" are different claims, and only the second one is what the user means by the picker
  // appearing -- the swapchain composites on top, so a paint that never arrives looks exactly
  // like a frozen video frame.
  {
    static thread_local uint64_t reportedShownAtUs = 0;
    const uint64_t shownAt = ctx.picker.shownAtUs.load(std::memory_order_relaxed);
    if (pickerVisible && shownAt != 0 && shownAt != reportedShownAtUs) {
      reportedShownAtUs = shownAt;
      log_client_line(ctx, "[picker] first paint after show, client=" +
                               std::to_string(layout.clientRect.right - layout.clientRect.left) +
                               "x" +
                               std::to_string(layout.clientRect.bottom - layout.clientRect.top));
    }
  }

  if (!pickerVisible) {
    // Before any frame has been presented there is nothing underneath, and returning here is what
    // produced the window a user actually gets when a host does not answer: black, titled GNLink,
    // saying nothing, and then gone. This says what is happening.
    //
    // `lastPresentedVersion` and not `lastPresentedCaptureUs`: the picker resets the latter on
    // every close, so it would be zero again mid-session and this would paint over live video.
    // The version only ever counts up.
    if (ctx.frameBuf.lastPresentedVersion.load(std::memory_order_relaxed) == 0) {
      static uint64_t waitingSinceUs = 0;
      const uint64_t now = qpc_now_us();
      if (waitingSinceUs == 0) waitingSinceUs = now;

      draw_alpha_rect(ctx, hdc, layout.clientRect, RGB(13, 15, 20), 255);
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, RGB(200, 206, 216));
      RECT line = layout.clientRect;
      // Changes once the wait stops looking instantaneous, so a slow answer does not read as a
      // frozen program. Nothing here is a diagnosis -- it says what is being waited for.
      const bool slow = now - waitingSinceUs > 4000000ULL;
      draw_text_utf8(ctx, hdc,
                     slow ? std::string("응답을 기다리는 중입니다…")
                          : std::string("연결하는 중…"),
                     &line, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    // Nothing to draw over the stream: the legacy Targets/Macro buttons were invisible ghost
    // hit-zones under the flip-model video (see compute_client_layout); the toolbar owns that UI.
    return;
  }

  draw_alpha_rect(ctx, hdc, layout.clientRect, RGB(13, 15, 20), 255);

  const WindowPanelSnapshot windowPanel = ctx.picker.windowPanel.Snapshot();
  const std::vector<WindowTargetUiEntry>& windowItems = windowPanel.items;
  const uint64_t selectedId = windowPanel.selectedId;
  const std::string& panelStatus = windowPanel.status;
  const std::string& panelDisplayStatus = windowPanel.displayStatus;
  const bool selectionLocked = windowPanel.selectionLocked;

  // Header: product title and status on the left, actions on the right.
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(240, 243, 247));
  RECT titleRect = layout.selectedInfoRect;
  {
    HGDIOBJ old = ctx.ui.titleFont ? SelectObject(hdc, ctx.ui.titleFont) : nullptr;
    RECT t = titleRect;
    // The product name was already on the window title; what this screen needs to say is what
    // it is for.
    DrawTextW(hdc, L"공유 화면 선택", -1, &t, DT_LEFT | DT_SINGLELINE);
    if (old) SelectObject(hdc, old);
  }
  // Once a target is picked the picker locks: the buttons and cards read as disabled while the
  // stream spins up, and the sub-header says whether we are still waiting on the host's ack or
  // on its first frame.
  const bool selectionPending = ctx.sel.pending.load(std::memory_order_acquire);
  const bool awaitingAck = ctx.sel.awaitingAck.load(std::memory_order_acquire);

  RECT subRect = titleRect;
  subRect.top += dpi_scale(ctx, 28);
  SetTextColor(hdc, RGB(150, 158, 170));
  // panelDisplayStatus, not panelStatus: the latter is the token the code matches on
  // (`window_list_received count=3`), and it used to be drawn here verbatim.
  // The list line is written here rather than in the shared core: that file is compiled into two
  // dozen targets without /utf-8, where a Korean literal does not survive the lexer.
  std::string listedLine;
  if (ctx.control.connected.load(std::memory_order_relaxed)) {
    listedLine = windowItems.empty()
                     ? std::string("공유할 수 있는 창이 없습니다. 전체 화면을 선택하세요.")
                     : "공유할 수 있는 창 " + std::to_string(windowItems.size()) + "개";
  }

  // The tokens that mean something went wrong, said in Korean.
  //
  // Without this the three failure states -- the control channel dropping, the session being
  // lost, the connect failing -- showed the ordinary list line instead, which told the user the
  // picker was fine when it was not. Unknown tokens fall through to the list line rather than
  // being printed, because a developer string on screen is what this whole split was about.
  std::string tokenLine;
  if (panelStatus == "control_disconnected") {
    tokenLine = "호스트와의 연결이 끊겼습니다.";
  } else if (panelStatus == "session_lost") {
    tokenLine = "세션이 끊겼습니다. 다시 연결해 주세요.";
  } else if (panelStatus == "control_connect_failed") {
    tokenLine = "호스트에 연결하지 못했습니다.";
  } else if (panelStatus == "waiting_control" || panelStatus == "window_list_request pending") {
    tokenLine = "화면 목록을 불러오는 중…";
  } else if (panelStatus == "waiting_first_frame") {
    tokenLine = "첫 화면을 기다리는 중…";
  }

  std::string statusLine = selectionLocked
                               ? std::string("호스트 설정으로 대상이 고정돼 있습니다")
                               : (!panelDisplayStatus.empty()
                                      ? panelDisplayStatus
                                      : (!tokenLine.empty() ? tokenLine : listedLine));
  if (selectionPending) {
    statusLine = awaitingAck ? std::string("선택하는 중…")
                             : std::string("첫 화면을 기다리는 중…");
  }
  if (!ctx.control.connected.load(std::memory_order_relaxed)) {
    statusLine = "호스트에 연결하는 중…";
  }
  draw_text_utf8(ctx, hdc, statusLine, &subRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

  const bool actionsDisabled =
      !ctx.control.connected.load(std::memory_order_relaxed) || selectionLocked || selectionPending;
  draw_panel_button(ctx, hdc, layout.refreshButtonRect, "새로 고침", false,
                    !ctx.control.connected.load(std::memory_order_relaxed) || selectionPending);
  // No 전체 화면 button: the first card is the desktop and selecting it does the same thing. Two
  // controls for one outcome, both accent-coloured, made "which one is selected" ambiguous.
  (void)actionsDisabled;

  // Card grid: desktop preview first, then one card per shareable window.
  const CardGridMetrics grid = compute_card_grid(ctx, layout.listRect);
  const int totalCards = kPickerListsWindows ? 1 + static_cast<int>(windowItems.size()) : 1;
  const int totalRows = (totalCards + grid.cols - 1) / grid.cols;
  const int maxScrollRow = std::max(0, totalRows - grid.visibleRows);
  int scrollRow = std::clamp(ctx.picker.gridScrollRow.load(std::memory_order_relaxed), 0, maxScrollRow);
  ctx.picker.gridScrollRow.store(scrollRow, std::memory_order_relaxed);
  const int firstCard = scrollRow * grid.cols;

  for (int slot = 0; slot < grid.visibleCards; ++slot) {
    const int cardIndex = firstCard + slot;
    if (cardIndex >= totalCards) break;
    const RECT card = card_rect_for_slot(layout.listRect, grid, slot);
    if (cardIndex == 0) {
      draw_target_card(ctx, hdc, card, grid, 0, "전체 화면", selectedId == 0,
                       selectionLocked || selectionPending);
    } else {
      const auto& entry = windowItems[static_cast<size_t>(cardIndex - 1)];
      draw_target_card(ctx, hdc, card, grid, entry.id, entry.title, entry.id == selectedId,
                       selectionLocked || selectionPending);
    }
  }

  if (kPickerListsWindows && windowItems.empty()) {
    RECT emptyRect = layout.listRect;
    emptyRect.top += grid.cardH + dpi_scale(ctx, 18);
    SetTextColor(hdc, RGB(150, 158, 170));
    draw_text_utf8(ctx, hdc,
                   selectionLocked
                       ? std::string("호스트 설정으로 창 목록이 숨겨져 있습니다")
                       : std::string("공유할 수 있는 창이 없습니다. 새로 고침을 눌러 보세요."),
                   &emptyRect, DT_CENTER | DT_SINGLELINE);
  }

  // Footer: connection and input state in one quiet line.
  std::ostringstream foot;
  foot << (ctx.control.connected.load(std::memory_order_relaxed) ? "연결됨" : "연결 끊김")
       << "   입력 "
       << (ctx.session.inputEnabled.load(std::memory_order_relaxed) ? "켜짐" : "꺼짐");
  const uint32_t decFpsX100 = ctx.metrics.Snapshot().decodedFpsX100;
  if (decFpsX100 > 0) foot << "   " << (decFpsX100 / 100) << " fps";
  if (totalRows > grid.visibleRows) {
    foot << "   " << (scrollRow + 1) << "-"
         << std::min(totalRows, scrollRow + grid.visibleRows) << " / " << totalRows << " 줄"
         << " (휠로 스크롤)";
  }
  RECT footRect = layout.statsRect;
  SetTextColor(hdc, RGB(140, 148, 160));
  draw_text_utf8(ctx, hdc, foot.str(), &footRect,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

}  // namespace remote60::native_poc::viewer

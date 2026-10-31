// See viewer_clip_transfer_bar.hpp.

#include "viewer_clip_transfer_bar.hpp"

#include <windowsx.h>

#include <algorithm>
#include <cstdio>

#include "bulk_pacer.hpp"
#include "poc_protocol.hpp"

namespace remote60::native_poc {

namespace {

constexpr wchar_t kClassName[] = L"Remote60ClipTransferBar";
constexpr UINT_PTR kTimerPoll = 1;
constexpr UINT kPollMs = 250;
constexpr uint64_t kSlowAfterMs = 10000;  // past this, the line says it is taking time

std::wstring megabytes(uint64_t bytes) {
  wchar_t buf[32];
  std::swprintf(buf, 32, L"%.1f", static_cast<double>(bytes) / (1024.0 * 1024.0));
  return buf;
}

std::wstring seconds(uint64_t ms) { return std::to_wstring((ms + 500) / 1000) + L"초"; }

std::wstring failure_reason(ClipImageReason r) {
  switch (r) {
    case ClipImageReason::Stalled: return L"원격 PC가 응답하지 않음";
    case ClipImageReason::Timeout: return L"시간 초과";
    case ClipImageReason::VerifyFailed: return L"받은 데이터 검증 실패";
    case ClipImageReason::DecodeFailed: return L"이미지를 읽지 못함";
    case ClipImageReason::SizeMismatch: return L"이미지 크기 불일치";
    case ClipImageReason::PublishFailed: return L"원격 클립보드에 쓰지 못함";
    case ClipImageReason::Session: return L"연결이 끊김";
    case ClipImageReason::Limit: return L"크기 제한 초과";
    case ClipImageReason::ColorProfile: return L"색 프로필이 들어 있는 이미지";
    default: return L"알 수 없는 이유";
  }
}

}  // namespace

static std::wstring not_sent_reason(ClipPackageResult r) {
  switch (r) {
    case ClipPackageResult::TooLarge: return L"이미지가 너무 커서 보내지 않았습니다";
    case ClipPackageResult::ColorProfile: return L"색 프로필이 들어 있는 이미지라 보내지 않았습니다";
    default: return L"복사한 이미지를 읽지 못해 보내지 않았습니다";
  }
}

static std::wstring refused_reason(ClipImageVerdict v) {
  switch (v) {
    case ClipImageVerdict::TooLarge: return L"원격 PC가 크기 제한으로 이미지를 받지 않았습니다";
    case ClipImageVerdict::Disabled: return L"원격 PC에서 클립보드 공유가 꺼져 있어 이미지를 받지 않았습니다";
    case ClipImageVerdict::Busy: return L"원격 PC가 다른 이미지를 받는 중이라 이 이미지를 받지 않았습니다";
    default: return L"원격 PC가 이미지를 받지 않았습니다";
  }
}

namespace {

std::wstring file_refused_text(uint16_t status) {
  switch (static_cast<file_copy::Status>(status)) {
    case file_copy::Status::Replaced: return L"원본 파일이 복사한 뒤 다른 파일로 바뀌어 붙여넣지 못했습니다";
    case file_copy::Status::Changed: return L"원본 파일이 복사한 뒤 바뀌어 붙여넣지 못했습니다";
    case file_copy::Status::SharingViolation: return L"원본 파일을 다른 프로그램이 쓰고 있어 붙여넣지 못했습니다";
    case file_copy::Status::NotFound: return L"원본 파일이 없어져 붙여넣지 못했습니다";
    case file_copy::Status::AccessDenied: return L"원본 파일을 읽을 권한이 없어 붙여넣지 못했습니다";
    case file_copy::Status::Refused: return L"다른 전송이 진행 중이라 붙여넣지 못했습니다";
    default: return L"파일을 붙여넣지 못했습니다";
  }
}

std::wstring file_ended_text(const FileCopyClient::Progress& p) {
  const auto reason = static_cast<file_copy::net::PasteEndReason>(p.lastReason);
  const std::wstring n = std::to_wstring(p.lastFiles);
  if (p.lastRefused != 0) return file_refused_text(p.lastRefused);
  if (reason == file_copy::net::PasteEndReason::Completed) {
    // Completed = the consumer (Explorer, ...) ended the paste successfully -- not merely "bytes arrived".
    return (p.lastToRemote ? L"원격 PC에서 파일 " : L"파일 ") + n + L"개 붙여넣기 완료 (" + megabytes(p.lastBytes) + L" MB, " +
           seconds(p.lastElapsedMs) + L")";
  }
  std::wstring s;
  switch (reason) {
    case file_copy::net::PasteEndReason::Cancelled: s = L"파일 붙여넣기를 취소했습니다"; break;
    case file_copy::net::PasteEndReason::Verification: s = L"전송 데이터 검증에 실패해 붙여넣기가 중단됐습니다"; break;
    case file_copy::net::PasteEndReason::Replaced: s = L"원본 파일이 다른 파일로 바뀌어 붙여넣기가 중단됐습니다"; break;
    case file_copy::net::PasteEndReason::Changed: s = L"원본 파일이 바뀌어 붙여넣기가 중단됐습니다"; break;
    case file_copy::net::PasteEndReason::InUse: s = L"원본 파일을 다른 프로그램이 쓰고 있어 붙여넣기가 중단됐습니다"; break;
    case file_copy::net::PasteEndReason::Idle: s = L"진행이 멈춰 붙여넣기가 중단됐습니다"; break;
    case file_copy::net::PasteEndReason::Disabled: s = L"파일 복사가 꺼져 붙여넣기가 중단됐습니다"; break;
    case file_copy::net::PasteEndReason::Session: s = L"연결이 끊겨 붙여넣기가 중단됐습니다"; break;
    case file_copy::net::PasteEndReason::Superseded: s = L"새 붙여넣기가 시작돼 이전 붙여넣기가 중단됐습니다"; break;
    default: s = L"붙여넣는 프로그램이 실패를 알렸습니다"; break;
  }
  // Removing a partly written file is not ours to promise: the consumer decides.
  if (p.lastBytes > 0) s += L" — 받는 쪽에 일부만 저장된 파일이 남았을 수 있습니다";
  return s;
}

}  // namespace

ClipBarView file_transfer_bar_view(const FileCopyClient::Progress& p, uint64_t nowUs, FileBarState* st) {
  ClipBarView v;
  if (!st->primed) {
    // The first poll: whatever ended or was offered before the bar existed is not news.
    st->primed = true;
    st->seenFinished = p.finished;
    st->seenOffered = p.offered;
    st->seenAvailable = p.available;
    st->seenNoHelper = p.noHelper;
  }
  if (p.cancelling) {
    v.isFile = true;
    v.phase = ClipBarPhase::Cancelling;
    v.fileText = L"파일 붙여넣기를 취소하는 중… (상대 PC의 확인을 기다립니다)";
    return v;
  }
  if (p.sending || p.receiving) {
    v.isFile = true;
    v.phase = ClipBarPhase::Sending;
    v.fileCancel = true;
    v.bytesDone = p.bytesDone;
    v.bytesTotal = p.bytesTotal;
    v.elapsedMs = p.elapsedMs;
    const uint64_t pct = p.bytesTotal ? (std::min<uint64_t>)(99, p.bytesDone * 100 / p.bytesTotal) : 0;
    v.fileText = (p.sending ? L"원격 PC로 파일 " : L"원격 PC에서 파일 ") + std::to_wstring(p.files) +
                 (p.sending ? L"개 보내는 중 " : L"개 받는 중 ") + std::to_wstring(pct) + L"% (" + megabytes(p.bytesDone) + L" / " +
                 megabytes(p.bytesTotal) + L" MB) · " + seconds(p.elapsedMs);
    if (p.elapsedMs >= kSlowAfterMs) v.fileText += L" — 전송에 시간이 걸리고 있습니다";
    st->seenFinished = p.finished;  // what ended before this one is not news any more
    st->resultUntilUs = 0;
    return v;
  }
  if (p.finished != st->seenFinished) {
    st->seenFinished = p.finished;
    st->resultUntilUs = nowUs + kClipBarResultUs;
  }
  if (st->resultUntilUs > nowUs) {
    v.isFile = true;
    v.phase = ClipBarPhase::Result;
    v.fileText = file_ended_text(p);
    return v;
  }
  if (p.noHelper != st->seenNoHelper) {
    st->seenNoHelper = p.noHelper;
    st->noHelperUntilUs = nowUs + kClipBarResultUs;
  }
  if (st->noHelperUntilUs > nowUs) {
    // Not silent -- and not a guess (helper-shell-token r1): only a helper found MISSING on this PC
    // is an install / update problem. Anything else (a token refused, no shell, a failed start or
    // handshake), and every refusal from the remote PC (its reason is not on the wire), is said as
    // "could not start"; the exact reason is in the host / viewer log.
    v.isFile = true;
    v.phase = ClipBarPhase::Result;
    if (!p.noHelperHere) {
      v.fileText = L"원격 PC의 파일 복사 도우미를 시작하지 못했습니다. 설치 상태와 실행 권한을 확인해 주세요";
    } else if (p.noHelperHereMissing) {
      v.fileText = L"이 PC의 GNLink 에 파일 복사 도우미가 없습니다. GNLink 를 업데이트하거나 다시 설치해 주세요";
    } else {
      v.fileText = L"이 PC의 파일 복사 도우미를 시작하지 못했습니다. 설치 상태와 실행 권한을 확인해 주세요";
    }
    return v;
  }
  if (p.offered != st->seenOffered) {
    st->seenOffered = p.offered;
    st->offeredUntilUs = nowUs + kClipBarResultUs;
  }
  if (p.available != st->seenAvailable) {
    st->seenAvailable = p.available;
    st->availableUntilUs = nowUs + kClipBarResultUs;
  }
  // A published offer says what the user can do now -- not that anything was sent.
  if (st->availableUntilUs > nowUs) {
    v.isFile = true;
    v.phase = ClipBarPhase::Result;
    v.fileText = L"원격 PC에서 복사한 파일 " + std::to_wstring(p.availableFiles) + L"개를 이 PC에 붙여넣을 수 있습니다";
    return v;
  }
  if (st->offeredUntilUs > nowUs) {
    v.isFile = true;
    v.phase = ClipBarPhase::Result;
    v.fileText = L"복사한 파일 " + std::to_wstring(p.offeredFiles) + L"개를 원격 PC에서 붙여넣을 수 있습니다";
    return v;
  }
  return v;
}

std::wstring clip_transfer_bar_text(const ClipBarView& v) {
  if (v.isPaste) return v.pasteText;
  if (v.isFile) return v.fileText;
  if (v.phase == ClipBarPhase::Sending) {
    const uint64_t pct = v.bytesTotal ? (std::min<uint64_t>)(99, v.bytesDone * 100 / v.bytesTotal) : 0;
    std::wstring s = L"원격 PC로 이미지 보내는 중 " + std::to_wstring(pct) + L"% (" + megabytes(v.bytesDone) + L" / " +
                     megabytes(v.bytesTotal) + L" MB) · " + seconds(v.elapsedMs);
    // Taking time is a fact; WHY is not measured here (the path, the host verifying, this PC), so
    // the line does not guess.
    if (v.elapsedMs >= kSlowAfterMs) s += L" — 전송에 시간이 걸리고 있습니다";
    return s;
  }
  if (v.phase == ClipBarPhase::Cancelling) {
    if (v.imageForFile) return L"파일 붙여넣기를 먼저 하려고 이미지 보내기를 멈추는 중…";
    return static_cast<ClipImageReason>(v.cancellingWhy) == ClipImageReason::User
               ? L"이미지 보내기를 취소하는 중…"
               : L"새로 복사한 내용으로 바꾸는 중…";
  }
  if (v.phase != ClipBarPhase::Result) return std::wstring();
  const auto why = static_cast<ClipImageReason>(v.detail);
  switch (v.outcome) {
    case ClipOutcome::Published:
      return L"이미지를 원격 PC 클립보드에 넣었습니다 (" + megabytes(v.bytesTotal) + L" MB, " + seconds(v.elapsedMs) +
             L")";
    case ClipOutcome::Cancelled:
      if (why == ClipImageReason::User) return L"이미지 보내기를 취소했습니다";
      if (why == ClipImageReason::Superseded) return L"새로 복사한 내용으로 바뀌어 이전 이미지는 보내지 않았습니다";
      if (why == ClipImageReason::Session) return L"연결이 끊겨 이미지 보내기가 중단됐습니다";
      return L"이미지 보내기가 취소됐습니다";
    case ClipOutcome::CancelTooLate:
      return L"취소하기 전에 이미 원격 PC 클립보드에 들어갔습니다";
    case ClipOutcome::CancelUnconfirmed:
      return L"보내기를 멈췄지만 원격 PC에서 어떻게 됐는지 확인하지 못했습니다";
    case ClipOutcome::HostSuperseded:
      return L"원격 PC 클립보드가 먼저 바뀌어 이미지를 넣지 않았습니다";
    case ClipOutcome::Failed:
      return L"이미지를 보내지 못했습니다 (" + failure_reason(why) + L")";
    case ClipOutcome::NotSent:
      return not_sent_reason(static_cast<ClipPackageResult>(v.detail));
    case ClipOutcome::Refused:
      return refused_reason(static_cast<ClipImageVerdict>(v.detail));
    default:
      return std::wstring();
  }
}

ClipBarView clip_transfer_bar_view(const ClipImageClient::Progress& p, uint64_t nowUs, uint64_t* seenFinished,
                                   uint64_t* resultUntilUs) {
  ClipBarView v;
  if (*resultUntilUs == 1) {
    // The first poll after the bar is created: whatever ended before it existed is not news.
    *seenFinished = p.finished;
    *resultUntilUs = 0;
  }
  if (p.active) {
    v.phase = ClipBarPhase::Sending;
    v.bytesDone = p.bytesConfirmed;
    v.bytesTotal = p.bytesTotal;
    v.elapsedMs = p.elapsedMs;
    *seenFinished = p.finished;  // whatever ended before this one is not news any more
    *resultUntilUs = 0;
    return v;
  }
  if (p.cancelling) {  // stopped here; what the host did is not known yet
    v.phase = ClipBarPhase::Cancelling;
    v.cancellingWhy = p.cancellingWhy;
    v.imageForFile = p.heldForFile;
    *seenFinished = p.finished;
    *resultUntilUs = 0;
    return v;
  }
  if (p.finished != *seenFinished) {
    // A new outcome, including one that began and ended between two polls (a fast LAN copy).
    *seenFinished = p.finished;
    *resultUntilUs = nowUs + kClipBarResultUs;
  }
  if (*resultUntilUs > nowUs) {
    v.phase = ClipBarPhase::Result;
    v.bytesDone = p.bytesConfirmed;
    v.bytesTotal = p.bytesTotal;
    v.elapsedMs = p.elapsedMs;
    v.outcome = p.outcome;
    v.detail = p.detail;
  }
  return v;
}

namespace {

struct Bar {
  HWND hwnd = nullptr;
  HWND owner = nullptr;
  ClipTransferBarHooks hooks;
  ClipBarView view;
  FileBarState fileState;
  uint64_t seenFinished = 0;
  uint64_t resultUntilUs = 1;  // 1 = "nothing polled yet" (see clip_transfer_bar_view)
  RECT cancel{};
  RECT retry{};
  bool pressed = false;
  bool retryPressed = false;
  int dpi = 96;
  HFONT font = nullptr;
  HBRUSH background = nullptr;
  HBRUSH border = nullptr;
  HBRUSH button = nullptr;
  HBRUSH buttonDown = nullptr;
};

Bar g;

int scaled(int v) { return MulDiv(v, g.dpi, 96); }

SIZE measure(const std::wstring& text) {
  HDC hdc = GetDC(g.hwnd);
  HGDIOBJ old = g.font ? SelectObject(hdc, g.font) : nullptr;
  SIZE s{};
  GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &s);
  if (old) SelectObject(hdc, old);
  ReleaseDC(g.hwnd, hdc);
  return s;
}

void reposition() {
  if (!g.hwnd || !g.owner || !IsWindow(g.owner)) return;
  const bool showable = g.view.phase != ClipBarPhase::Hidden && IsWindowVisible(g.owner) && !IsIconic(g.owner);
  if (!showable) {
    ShowWindow(g.hwnd, SW_HIDE);
    return;
  }
  const std::wstring text = clip_transfer_bar_text(g.view);
  const SIZE t = measure(text);
  const int pad = scaled(10);
  const int height = t.cy + pad * 2;
  int width = t.cx + pad * 2;
  g.cancel = RECT{};
  g.retry = RECT{};
  if (clip_transfer_bar_has_retry(g.view)) {
    const SIZE b = measure(L"다시 시도");
    const int bw = b.cx + scaled(28);
    g.retry = RECT{width, scaled(5), width + bw, height - scaled(5)};
    width += bw + pad;
  }
  if (clip_transfer_bar_has_cancel(g.view)) {
    const SIZE b = measure(L"취소");
    const int bw = b.cx + scaled(28);
    g.cancel = RECT{width, scaled(5), width + bw, height - scaled(5)};
    width += bw + pad;
  }
  RECT client{};
  GetClientRect(g.owner, &client);
  POINT origin{client.left, client.top};
  ClientToScreen(g.owner, &origin);
  const int clientW = client.right - client.left;
  const int clientH = client.bottom - client.top;
  width = (std::min)(width, (std::max)(clientW - scaled(16), scaled(120)));
  if (g.cancel.right > width - pad) {  // narrow window: the button keeps its place at the right edge
    const int bw = g.cancel.right - g.cancel.left;
    g.cancel.right = width - pad;
    g.cancel.left = g.cancel.right - bw;
  }
  if (g.retry.right > width - pad) {
    const int bw = g.retry.right - g.retry.left;
    g.retry.right = width - pad;
    g.retry.left = g.retry.right - bw;
  }
  const int x = origin.x + (std::max)(0, (clientW - width) / 2);
  const int y = origin.y + (std::max)(0, clientH - height - scaled(16));
  SetWindowPos(g.hwnd, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | (IsWindowVisible(g.hwnd) ? 0u : SWP_SHOWWINDOW));
  InvalidateRect(g.hwnd, nullptr, FALSE);
}

void poll() {
  if (!g.hooks.progress) return;
  const ClipBarView before = g.view;
  const uint64_t nowUs = BulkPacer::NowUs();
  g.view = clip_transfer_bar_view(g.hooks.progress(), nowUs, &g.seenFinished, &g.resultUntilUs);
  // A file paste (running, cancelling, just ended, just offered) takes the bar over the image's line.
  if (g.hooks.fileProgress) {
    const ClipBarView fv = file_transfer_bar_view(g.hooks.fileProgress(), nowUs, &g.fileState);
    if (fv.isFile) g.view = fv;
  }
  // A Ctrl+V waiting for its answer, or one that did not happen, is what the user is looking at.
  if (g.hooks.pasteView) {
    const ClipPasteBarView pv = g.hooks.pasteView();
    if (pv.active) {
      ClipBarView v;
      v.phase = pv.failed || pv.retry ? ClipBarPhase::Result : ClipBarPhase::Sending;
      v.isPaste = true;
      v.pasteText = pv.text;
      v.pasteCancel = pv.cancel;
      v.pasteRetry = pv.retry;
      v.pasteFailed = pv.failed;
      g.view = v;
    }
  }
  if (g.view.phase != before.phase && g.hooks.onLog) {
    std::string line = "[clip-bar] phase=" + std::to_string(static_cast<int>(g.view.phase));
    if (g.view.phase == ClipBarPhase::Result) {
      line += " outcome=" + std::to_string(static_cast<int>(g.view.outcome)) + " detail=" + std::to_string(g.view.detail);
    }
    g.hooks.onLog(line);
  }
  const bool changed = g.view.phase != before.phase || g.view.isFile != before.isFile || g.view.fileText != before.fileText ||
                       g.view.isPaste != before.isPaste || g.view.pasteText != before.pasteText ||
                       g.view.pasteRetry != before.pasteRetry || g.view.pasteCancel != before.pasteCancel ||
                       g.view.bytesDone != before.bytesDone ||
                       g.view.elapsedMs / 1000 != before.elapsedMs / 1000 || g.view.outcome != before.outcome ||
                       g.view.detail != before.detail;
  if (changed) reposition();
}

void paint(HDC target) {
  RECT client{};
  GetClientRect(g.hwnd, &client);
  const int w = client.right - client.left;
  const int h = client.bottom - client.top;
  HDC hdc = CreateCompatibleDC(target);  // off-screen, then one blit: no flicker over moving video
  HBITMAP bmp = CreateCompatibleBitmap(target, w, h);
  HGDIOBJ oldBmp = SelectObject(hdc, bmp);
  FillRect(hdc, &client, g.background);
  FrameRect(hdc, &client, g.border);
  SetBkMode(hdc, TRANSPARENT);
  HGDIOBJ oldFont = g.font ? SelectObject(hdc, g.font) : nullptr;
  const int pad = scaled(10);
  const int firstButton = g.retry.right > 0 ? g.retry.left : (g.cancel.right > 0 ? g.cancel.left : w);
  RECT textRect{pad, 0, firstButton - pad / 2, h};
  const bool failed = g.view.isPaste ? g.view.pasteFailed
                                     : g.view.phase == ClipBarPhase::Result &&
                                           (g.view.outcome == ClipOutcome::Failed || g.view.outcome == ClipOutcome::NotSent ||
                                            g.view.outcome == ClipOutcome::Refused ||
                                            g.view.outcome == ClipOutcome::CancelUnconfirmed);
  SetTextColor(hdc, failed ? RGB(242, 150, 140) : RGB(232, 236, 242));
  const std::wstring text = clip_transfer_bar_text(g.view);
  DrawTextW(hdc, text.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  if (g.cancel.right > 0) {
    RECT b = g.cancel;
    FillRect(hdc, &b, g.pressed ? g.buttonDown : g.button);
    FrameRect(hdc, &b, g.border);
    SetTextColor(hdc, RGB(232, 236, 242));
    DrawTextW(hdc, L"취소", -1, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  if (g.retry.right > 0) {
    RECT b = g.retry;
    FillRect(hdc, &b, g.retryPressed ? g.buttonDown : g.button);
    FrameRect(hdc, &b, g.border);
    SetTextColor(hdc, RGB(232, 236, 242));
    DrawTextW(hdc, L"다시 시도", -1, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  if (oldFont) SelectObject(hdc, oldFont);
  BitBlt(target, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
  SelectObject(hdc, oldBmp);
  DeleteObject(bmp);
  DeleteDC(hdc);
}

bool in_cancel(int x, int y) {
  return g.cancel.right > 0 && x >= g.cancel.left && x < g.cancel.right && y >= g.cancel.top && y < g.cancel.bottom;
}

bool in_retry(int x, int y) {
  return g.retry.right > 0 && x >= g.retry.left && x < g.retry.right && y >= g.retry.top && y < g.retry.bottom;
}

LRESULT CALLBACK bar_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    // Clicking the bar must not take focus from the video window: keystrokes are captured there.
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      paint(hdc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_TIMER:
      if (wp == kTimerPoll) {
        poll();
        return 0;
      }
      break;
    case WM_LBUTTONDOWN:
      g.pressed = in_cancel(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      g.retryPressed = !g.pressed && in_retry(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      if (g.pressed || g.retryPressed) SetCapture(hwnd);
      if (g.hooks.onLog) {
        g.hooks.onLog("[clip-bar] down x=" + std::to_string(GET_X_LPARAM(lp)) + " y=" + std::to_string(GET_Y_LPARAM(lp)) +
                      " onCancel=" + (g.pressed ? "1" : "0"));
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_LBUTTONUP: {
      const bool was = g.pressed;
      const bool wasRetry = g.retryPressed;
      g.pressed = false;
      g.retryPressed = false;
      if (GetCapture() == hwnd) ReleaseCapture();
      InvalidateRect(hwnd, nullptr, FALSE);
      if (wasRetry) {
        if (!in_retry(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)) || !clip_transfer_bar_has_retry(g.view)) {
          if (g.hooks.onLog) g.hooks.onLog("[clip-bar] retry up ignored");
          return 0;
        }
        if (g.hooks.onLog) {
          g.hooks.onLog(std::string("[clip-bar] paste retry clicked, callback ") + (g.hooks.onPasteRetry ? "present" : "MISSING"));
        }
        if (g.hooks.onPasteRetry) g.hooks.onPasteRetry();
        poll();
        return 0;
      }
      if (!was) return 0;
      if (!in_cancel(GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
        if (g.hooks.onLog) g.hooks.onLog("[clip-bar] up ignored: released off Cancel");
        return 0;
      }
      if (!clip_transfer_bar_has_cancel(g.view)) {
        if (g.hooks.onLog) g.hooks.onLog("[clip-bar] up ignored: nothing is being sent any more");
        return 0;
      }
      if (g.hooks.onLog) {
        g.hooks.onLog(std::string("[clip-bar] cancel clicked, callback ") + (g.hooks.onCancel ? "present" : "MISSING"));
      }
      if (g.view.isPaste) {
        if (g.hooks.onLog) g.hooks.onLog("[clip-bar] paste cancel clicked");
        if (g.hooks.onPasteCancel) g.hooks.onPasteCancel();
      } else if (g.view.isFile) {
        if (g.hooks.onLog) g.hooks.onLog("[clip-bar] file paste cancel clicked");
        if (g.hooks.onFileCancel) g.hooks.onFileCancel();
      } else if (g.hooks.onCancel) {
        g.hooks.onCancel();
      }
      poll();
      return 0;
    }
    case WM_DPICHANGED:
      g.dpi = HIWORD(wp);
      reposition();
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

bool clip_transfer_bar_create(HWND owner, ClipTransferBarHooks hooks) {
  if (g.hwnd) return true;
  if (!owner || !IsWindow(owner)) return false;
  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = bar_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  wc.lpszClassName = kClassName;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
  g.owner = owner;
  g.hooks = std::move(hooks);
  g.dpi = static_cast<int>(GetDpiForWindow(owner));
  if (g.dpi <= 0) g.dpi = 96;
  g.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"", WS_POPUP, 0, 0, 10, 10, owner,
                           nullptr, instance, nullptr);
  if (!g.hwnd) {
    g.owner = nullptr;
    return false;
  }
  LOGFONTW lf{};
  lf.lfHeight = -MulDiv(12, g.dpi, 72);
  lf.lfWeight = FW_NORMAL;
  lstrcpynW(lf.lfFaceName, L"Malgun Gothic", LF_FACESIZE);
  g.font = CreateFontIndirectW(&lf);
  g.background = CreateSolidBrush(RGB(20, 24, 31));
  g.border = CreateSolidBrush(RGB(64, 74, 90));
  g.button = CreateSolidBrush(RGB(43, 51, 64));
  g.buttonDown = CreateSolidBrush(RGB(24, 29, 37));
  SetTimer(g.hwnd, kTimerPoll, kPollMs, nullptr);
  if (g.hooks.onLog) g.hooks.onLog("[clip-bar] created");
  return true;
}

void clip_transfer_bar_follow_owner() { reposition(); }

void clip_transfer_bar_destroy() {
  if (g.hwnd) {
    KillTimer(g.hwnd, kTimerPoll);
    DestroyWindow(g.hwnd);
  }
  for (HGDIOBJ* o : {reinterpret_cast<HGDIOBJ*>(&g.font), reinterpret_cast<HGDIOBJ*>(&g.background),
                     reinterpret_cast<HGDIOBJ*>(&g.border), reinterpret_cast<HGDIOBJ*>(&g.button),
                     reinterpret_cast<HGDIOBJ*>(&g.buttonDown)}) {
    if (*o) DeleteObject(*o);
    *o = nullptr;
  }
  g = Bar{};
}

HWND clip_transfer_bar_window() { return g.hwnd; }
RECT clip_transfer_bar_cancel_rect() { return g.cancel; }
RECT clip_transfer_bar_retry_rect() { return g.retry; }
void clip_transfer_bar_refresh() {
  if (g.hwnd) poll();
}
ClipBarView clip_transfer_bar_current() { return g.view; }

}  // namespace remote60::native_poc

// See clipboard_monitor.hpp for the module summary.

#include "clipboard_monitor.hpp"

#include <iostream>
#include <memory>

#include "clipboard_win32.hpp"

namespace remote60::native_poc {

namespace {
constexpr wchar_t kClassName[] = L"Remote60ClipboardMonitor";
// Marshals a SetText onto the monitor thread. lParam is a heap std::u16string the handler owns.
constexpr UINT kMsgSetClipboard = WM_APP + 1;
}  // namespace

LRESULT CALLBACK ClipboardMonitor::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create ? create->lpCreateParams : nullptr));
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
  auto* self = reinterpret_cast<ClipboardMonitor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_CLIPBOARDUPDATE: {
      if (self && self->onText_) {
        // Win32 hands back wchar_t; the core and the wire speak UTF-16 code units, which on
        // Windows is the same 16 bits (clipboard_win32.hpp).
        std::wstring wide;
        if (clipboard_read_unicode_text(hwnd, &wide)) self->onText_(wide_to_u16(wide));
      }
      return 0;
    }
    case kMsgSetClipboard: {
      std::unique_ptr<std::u16string> text(reinterpret_cast<std::u16string*>(lParam));
      if (text) (void)clipboard_set_unicode_text(hwnd, u16_to_wide(*text));
      return 0;
    }
    case WM_DESTROY:
      RemoveClipboardFormatListener(hwnd);
      PostQuitMessage(0);  // ends the monitor thread's GetMessage pump
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ClipboardMonitor::ThreadMain() {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &ClipboardMonitor::WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kClassName;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    ready_.store(true, std::memory_order_release);  // unblock Start(); running_ stays false
    return;
  }
  // HWND_MESSAGE: a message-only window. It is never shown and never appears anywhere, but it has a
  // message queue, which is all AddClipboardFormatListener needs.
  HWND hwnd = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                              wc.hInstance, this);
  if (!hwnd) {
    ready_.store(true, std::memory_order_release);
    return;
  }
  const bool listening = AddClipboardFormatListener(hwnd) != 0;
  hwnd_.store(hwnd, std::memory_order_release);
  running_.store(listening, std::memory_order_release);
  ready_.store(true, std::memory_order_release);
  if (!listening) {
    DestroyWindow(hwnd);
    hwnd_.store(nullptr, std::memory_order_release);
    return;
  }

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  running_.store(false, std::memory_order_release);
  hwnd_.store(nullptr, std::memory_order_release);
}

bool ClipboardMonitor::Start(OnTextFn onText) {
  if (thread_.joinable()) return running_.load(std::memory_order_acquire);
  onText_ = std::move(onText);
  ready_.store(false, std::memory_order_release);
  thread_ = std::thread([this]() { ThreadMain(); });
  threadId_ = GetThreadId(thread_.native_handle());
  // Wait until the thread has published whether the window came up. Bounded so a broken environment
  // cannot hang startup.
  for (int i = 0; i < 200 && !ready_.load(std::memory_order_acquire); ++i) Sleep(5);
  return running_.load(std::memory_order_acquire);
}

void ClipboardMonitor::Stop() {
  if (!thread_.joinable()) return;
  if (HWND hwnd = hwnd_.load(std::memory_order_acquire)) {
    // WM_CLOSE -> DefWindowProc DestroyWindow -> WM_DESTROY -> PostQuitMessage, which is what ends
    // the GetMessage pump. If the window never came up (listener failed) the thread has already
    // returned, and join() below simply completes.
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
  } else if (threadId_ != 0) {
    // The window is gone but the thread may still be between publishing readiness and returning.
    PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
  }
  thread_.join();
  running_.store(false, std::memory_order_release);
}

bool ClipboardMonitor::SetText(const std::u16string& text) {
  HWND hwnd = hwnd_.load(std::memory_order_acquire);
  if (!hwnd || !running_.load(std::memory_order_acquire)) return false;
  // Ownership of the copy passes to the handler on the monitor thread.
  auto* copy = new std::u16string(text);
  if (!PostMessageW(hwnd, kMsgSetClipboard, 0, reinterpret_cast<LPARAM>(copy))) {
    delete copy;
    return false;
  }
  return true;
}

// --- HostClipboardHub -------------------------------------------------------------------------

bool HostClipboardHub::Start() {
  const bool ok = monitor_.Start([this](const std::u16string& text) { OnLocalText(text); });
  started_.store(ok, std::memory_order_release);
  if (ok) {
    std::cout << "[native-video-host][clipboard] monitor started\n";
  } else {
    std::cout << "[native-video-host][clipboard] monitor unavailable; clipboard sync disabled\n";
  }
  return ok;
}

void HostClipboardHub::Stop() {
  monitor_.Stop();
  started_.store(false, std::memory_order_release);
}

void HostClipboardHub::OnLocalText(const std::u16string& text) {
  uint64_t hash = 0;
  std::lock_guard<std::mutex> lock(mu_);
  // Send == a genuine new local clipboard worth publishing to clients. SkipEcho == the change our
  // own ApplyRemote just caused, which must not raise the generation. The other skips are empty /
  // duplicate / oversize.
  if (core_.OnLocalChange(text, &hash) != ClipboardLocalDecision::Send) return;
  ++generation_;
  text_ = text;
  hash_ = hash;
}

HostClipboardHub::Snapshot HostClipboardHub::Get() {
  std::lock_guard<std::mutex> lock(mu_);
  return Snapshot{generation_, hash_, text_};
}

void HostClipboardHub::ApplyRemote(const std::u16string& text, uint64_t hash) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    // Record it as applied BEFORE writing the clipboard, so OnLocalText recognises the resulting
    // change notification as an echo. Apply == new content; anything else means it is already here
    // or was just sent, so there is nothing to write.
    if (core_.OnRemoteData(text, hash) != ClipboardRemoteDecision::Apply) return;
  }
  (void)monitor_.SetText(text);
}

}  // namespace remote60::native_poc

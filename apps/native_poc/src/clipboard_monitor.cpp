// See clipboard_monitor.hpp for the module summary.

#include "clipboard_monitor.hpp"

#include <algorithm>
#include <iostream>
#include <memory>

#include "clipboard_win32.hpp"

namespace remote60::native_poc {

namespace {
constexpr wchar_t kClassName[] = L"Remote60ClipboardMonitor";
// Marshals a SetText onto the monitor thread. lParam is a heap std::u16string the handler owns.
constexpr UINT kMsgSetClipboard = WM_APP + 1;
// Marshals an Invoke onto the monitor thread. lParam is a heap std::shared_ptr<InvokeJob>.
constexpr UINT kMsgInvoke = WM_APP + 2;

struct InvokeJob {
  std::function<void(HWND)> fn;
  HANDLE done = nullptr;
  ~InvokeJob() {
    if (done) CloseHandle(done);
  }
};
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
      if (self && self->onChange_) {
        DWORD owner = 0;
        if (HWND o = GetClipboardOwner()) GetWindowThreadProcessId(o, &owner);
        self->onChange_(owner);
      }
      if (self) self->ReportFiles(hwnd);
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
    case kMsgInvoke: {
      std::unique_ptr<std::shared_ptr<InvokeJob>> job(reinterpret_cast<std::shared_ptr<InvokeJob>*>(lParam));
      if (job && *job) {
        if ((*job)->fn) (*job)->fn(hwnd);
        SetEvent((*job)->done);
      }
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

void ClipboardMonitor::ReportFiles(HWND hwnd) {
  OnFilesFn fn;
  {
    std::lock_guard<std::mutex> lock(filesMu_);
    fn = onFiles_;
  }
  if (!fn) return;
  const uint64_t seq = GetClipboardSequenceNumber();
  std::vector<std::wstring> paths;
  // One over the offer limit (100), so the rules say "too many" rather than offering part of it.
  if (!clipboard_read_file_paths(hwnd, 101, &paths)) return;  // busy / secure desktop: the next change reports
  fn(seq, std::move(paths));
}

void ClipboardMonitor::SetOnFiles(OnFilesFn fn) {
  {
    std::lock_guard<std::mutex> lock(filesMu_);
    onFiles_ = std::move(fn);
  }
  (void)Invoke([this](HWND hwnd) { ReportFiles(hwnd); }, 2000);
}

bool ClipboardMonitor::Start(OnTextFn onText, OnChangeFn onChange) {
  if (thread_.joinable()) return running_.load(std::memory_order_acquire);
  onText_ = std::move(onText);
  onChange_ = std::move(onChange);
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

bool ClipboardMonitor::Invoke(std::function<void(HWND)> fn, DWORD timeoutMs) {
  HWND hwnd = hwnd_.load(std::memory_order_acquire);
  if (!hwnd || !running_.load(std::memory_order_acquire)) return false;
  auto job = std::make_shared<InvokeJob>();
  job->fn = std::move(fn);
  job->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!job->done) return false;
  auto* holder = new std::shared_ptr<InvokeJob>(job);
  if (!PostMessageW(hwnd, kMsgInvoke, 0, reinterpret_cast<LPARAM>(holder))) {
    delete holder;
    return false;
  }
  return WaitForSingleObject(job->done, timeoutMs) == WAIT_OBJECT_0;
}

// --- HostClipboardHub -------------------------------------------------------------------------

bool HostClipboardHub::Start() {
  // Whatever is on the clipboard right now belongs to before the host was running, so it is
  // recorded as a baseline and never published. Done BEFORE the listener is registered, not after:
  // if registering provokes an immediate change notification (it does not on this machine, but
  // that is an OS detail and not a guarantee), the monitor thread could otherwise publish the old
  // contents before this line ran.
  {
    std::wstring wide;
    if (clipboard_read_unicode_text(nullptr, &wide) && !wide.empty()) {
      std::lock_guard<std::mutex> lock(mu_);
      core_.SeedBaseline(wide_to_u16(wide));
    }
  }
  const bool ok = monitor_.Start([this](const std::u16string& text) { OnLocalText(text); },
                                 [this](DWORD ownerPid) { OnChange(ownerPid); });
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

void HostClipboardHub::NoteOwnHelper(DWORD pid) {
  if (pid == 0) return;
  std::lock_guard<std::mutex> lock(helperMu_);
  ownHelpers_.push_back(pid);
  if (ownHelpers_.size() > 8) ownHelpers_.erase(ownHelpers_.begin());
}

void HostClipboardHub::OnChange(DWORD ownerPid) {
  bool own = ownerPid != 0 && ownerPid == GetCurrentProcessId();
  if (!own && ownerPid != 0) {
    std::lock_guard<std::mutex> lock(helperMu_);
    own = std::find(ownHelpers_.begin(), ownHelpers_.end(), ownerPid) != ownHelpers_.end();
  }
  if (own) return;  // this host's own write: a paste it applied, an image it published, files its helper put
  const uint64_t gen = copyGen_.fetch_add(1, std::memory_order_acq_rel) + 1;
  std::cout << "[native-video-host][clipboard] copy on this PC copyGen=" << gen << " owner=" << ownerPid << "\n";
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

HostClipboardHub::PasteOutcome HostClipboardHub::ApplyPaste(const std::u16string& text, uint64_t hash,
                                                            DWORD timeoutMs) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    // The decision is ignored on purpose: SkipDuplicate (applied before) and SkipEcho (the host's own
    // earlier copy) both still need the write, because what is on the clipboard NOW may be neither.
    // Either way the content is recorded, so the write's own notification reads as an echo.
    (void)core_.OnRemoteData(text, hash);
  }
  struct Job {
    std::wstring text;
    std::atomic<bool> abandoned{false};
    bool ok = false;
    uint8_t stage = 0;
    uint32_t win32 = 0;
    uint32_t clipSeq = 0;
  };
  auto job = std::make_shared<Job>();
  job->text = u16_to_wide(text);
  const bool finished = monitor_.Invoke(
      [job](HWND hwnd) {
        // The caller has already answered "failed" to a write it stopped waiting for; doing it late
        // would put text on the clipboard the viewer was told is not there.
        if (job->abandoned.load()) return;
        job->ok = clipboard_set_unicode_text_staged(hwnd, job->text, &job->stage, &job->win32);
        job->clipSeq = GetClipboardSequenceNumber();
      },
      timeoutMs);
  PasteOutcome out;
  if (!finished) {
    job->abandoned.store(true);
    out.stage = 4;
  } else {
    out.ok = job->ok;
    out.stage = job->stage;
    out.win32 = job->win32;
    out.clipSeq = job->clipSeq;
  }
  out.userCopyGen = copyGen_.load(std::memory_order_acquire);
  return out;
}

ClipPublishResult HostClipboardHub::PublishImage(uint64_t expectSequence, HGLOBAL pngGlobal, HGLOBAL dibv5,
                                                 const std::u16string& text) {
  struct Job {
    HGLOBAL png = nullptr;
    HGLOBAL dib = nullptr;
    std::u16string text;
    uint64_t expect = 0;
    std::atomic<bool> abandoned{false};
    ClipPublishResult result = ClipPublishResult::OpenFailed;
    ~Job() {
      if (png) GlobalFree(png);
      if (dib) GlobalFree(dib);
    }
  };
  auto job = std::make_shared<Job>();
  job->png = pngGlobal;
  job->dib = dibv5;
  job->text = text;
  job->expect = expectSequence;
  const bool finished = monitor_.Invoke(
      [this, job](HWND hwnd) {
        // A publish that its caller has stopped waiting for must not happen late (the viewer has
        // been told it failed): the job then just frees what it holds.
        if (job->abandoned.load()) return;
        HGLOBAL png = job->png, dib = job->dib;
        job->png = job->dib = nullptr;  // clip_image_publish owns them from here
        job->result = clip_image_publish(hwnd, job->expect, png, dib, &job->text, nullptr);
        if (job->result == ClipPublishResult::Published && !job->text.empty()) {
          std::lock_guard<std::mutex> lock(mu_);
          (void)core_.OnRemoteData(job->text, clipboard_fnv1a(job->text));
        }
      },
      10000);
  if (!finished) {
    job->abandoned.store(true);
    return ClipPublishResult::OpenFailed;
  }
  return job->result;
}

}  // namespace remote60::native_poc

// See viewer_clip_image_wiring.hpp.

#include "viewer_state.hpp"
#include "viewer_log.hpp"
#include "viewer_clip_image_wiring.hpp"
#include "viewer_clip_transfer_bar.hpp"
#include "viewer_paste_ui.hpp"
#include "time_utils.hpp"

#include <cstdlib>
#include <cstring>

namespace remote60::native_poc::viewer {

void start_clip_image_client(ViewerState& ctx, uint32_t udpMtu) {
  ctx.control.clipImage.SetBulkNegotiated(ctx.session.bulkChannelNegotiated);
  ctx.control.fileCopy.SetBulkNegotiated(ctx.session.bulkChannelNegotiated);
  if (!ctx.session.bulkChannelNegotiated) return;
  ctx.control.clipImage.SetBulkArbiter(&ctx.control.bulkArbiter);
  ctx.control.clipImage.Start(
      [&ctx](const void* data, size_t len) -> bool {
        return send(ctx.session.sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
      },
      [&ctx]() -> uint64_t {
        const uint64_t at = ctx.control.lastRttAtUs.load(std::memory_order_relaxed);
        const uint64_t now = remote60::native_poc::qpc_now_us();
        // A ping older than 3 s says nothing about the path now.
        return (at != 0 && now >= at && now - at <= 3000000) ? ctx.control.lastRttUs.load(std::memory_order_relaxed)
                                                             : 0;
      },
      [&ctx]() -> bool {
        return ctx.control.overUdp.load(std::memory_order_acquire) && ctx.control.udpControl.TxPending();
      },
      udpMtu, remote60::native_poc::clip_bulk_rate_config_from_env());
  // File copy rides the same bulk rules (bulk_uplink.hpp) and the same one-bulk arbiter. Its own
  // switch: REMOTE60_CLIPBOARD_FILES=0 turns files alone off; clipboard sync off turns it off too
  // (the pump that drives it only runs while clipboard sync is on).
  {
    const char* v = std::getenv("REMOTE60_CLIPBOARD_FILES");
    ctx.control.fileCopy.SetAllowed(!(v && (v[0] == '0' || std::strcmp(v, "off") == 0 || std::strcmp(v, "false") == 0)));
  }
  // R->P: files copied on the remote PC are put on THIS clipboard by the clipboard helper beside
  // this program, started as this program's own user (the viewer is not elevated; nothing is raised).
  ctx.control.fileCopy.SetHelperLauncher([](remote60::native_poc::file_copy::HelperLink* link, std::string* why) {
    wchar_t path[MAX_PATH] = L"";
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
      *why = "own path unknown";
      return false;
    }
    std::wstring exe(path, n);
    exe = exe.substr(0, exe.find_last_of(L'\\') + 1) + L"GNLinkClipHelper.exe";
    return remote60::native_poc::file_copy::launch_file_copy_helper_as_self(exe, nullptr, L"", link, why);
  });
  // D5: a paste (an explicit act) stops a running image and waits for its confirmed end; the image
  // may go again afterwards under the conditions ClipImageClient::AfterFilePaste checks.
  ctx.control.fileCopy.SetImagePreemption([&ctx] { return ctx.control.clipImage.PreemptForFilePaste(); },
                                          [&ctx](bool mayResume) { ctx.control.clipImage.AfterFilePaste(mayResume); });
  ctx.control.fileCopy.Start(
      [&ctx](const void* data, size_t len) -> bool {
        return send(ctx.session.sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
      },
      [&ctx]() -> uint64_t {
        const uint64_t at = ctx.control.lastRttAtUs.load(std::memory_order_relaxed);
        const uint64_t now = remote60::native_poc::qpc_now_us();
        return (at != 0 && now >= at && now - at <= 3000000) ? ctx.control.lastRttUs.load(std::memory_order_relaxed)
                                                             : 0;
      },
      [&ctx]() -> bool {
        return ctx.control.overUdp.load(std::memory_order_acquire) && ctx.control.udpControl.TxPending();
      },
      udpMtu, remote60::native_poc::clip_bulk_rate_config_from_env(), &ctx.control.bulkArbiter);
}

void create_clip_transfer_bar(ViewerState& ctx) {
  remote60::native_poc::ClipTransferBarHooks hooks;
  // The hooks outlive nothing: the bar is destroyed in WM_DESTROY, long before ctx.
  hooks.progress = [&ctx] { return ctx.control.clipImage.GetProgress(); };
  hooks.onCancel = [&ctx] { ctx.control.clipImage.CancelByUser(); };
  hooks.onLog = [&ctx](const std::string& line) { log_client_line(ctx, line); };
  hooks.fileProgress = [&ctx] { return ctx.control.fileCopy.GetProgress(); };
  hooks.onFileCancel = [&ctx] { ctx.control.fileCopy.CancelPaste(); };
  // Paste on demand (t-y4wj64jw): a Ctrl+V waiting for its answer, or why it was not sent.
  hooks.pasteView = [&ctx] { return paste_bar_view(ctx); };
  hooks.onPasteCancel = [&ctx] { paste_cancel_from_bar(ctx); };
  hooks.onPasteRetry = [&ctx] { paste_retry_from_bar(ctx); };
  remote60::native_poc::clip_transfer_bar_create(ctx.session.hwnd, std::move(hooks));
}

}  // namespace remote60::native_poc::viewer

// See viewer_clip_image_wiring.hpp.

#include "viewer_state.hpp"
#include "viewer_log.hpp"
#include "viewer_clip_image_wiring.hpp"
#include "viewer_clip_transfer_bar.hpp"
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
  remote60::native_poc::clip_transfer_bar_create(ctx.session.hwnd, std::move(hooks));
}

}  // namespace remote60::native_poc::viewer

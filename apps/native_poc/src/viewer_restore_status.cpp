// See viewer_restore.hpp (dxgi-wedge-fix r1): the picker header's line while restoring / after a failed
// restore. Its own file: the picker preview tool draws the overlay without the rest of the restore.

#include "viewer_state.hpp"
#include "viewer_restore.hpp"

namespace remote60::native_poc::viewer {

std::string restore_status_line(const ViewerState& ctx, bool controlUp) {
  const auto& r = ctx.picker.restore;
  switch (r.phase) {
    case RestoreState::Phase::Waiting:
    case RestoreState::Phase::Requested:
      return controlUp ? "재연결 중 — 이전 화면으로 돌아가는 중…" : "재연결 중…";
    case RestoreState::Phase::Failed: {
      std::string why = "알 수 없는 이유";
      if (r.failReason == "monitor_gone") why = "그 화면이 더 이상 없습니다";
      else if (r.failReason == "monitor_changed") why = "그 화면의 해상도나 배치가 바뀌었습니다";
      else if (r.failReason == "monitor_ambiguous") why = "같은 화면이 여럿입니다";
      else if (r.failReason == "window_not_restored") why = "창은 자동으로 다시 고르지 않습니다";
      else if (r.failReason == "monitor_select_unsupported" || r.failReason == "monitor_list_unsupported")
        why = "호스트가 화면 선택을 지원하지 않습니다";
      else if (r.failReason == "monitor_list_timeout" || r.failReason == "window_list_timeout")
        why = "호스트가 목록을 보내지 않았습니다";
      else if (r.failReason == "target_locked_by_host") why = "호스트 설정으로 대상이 고정돼 있습니다";
      return "이전 화면으로 돌아가지 못했습니다(" + why + "). 화면을 선택해 주세요.";
    }
    case RestoreState::Phase::None:
    case RestoreState::Phase::Done:
      break;
  }
  if (r.reconnect && !controlUp) return "재연결 중…";
  return {};
}

}  // namespace remote60::native_poc::viewer

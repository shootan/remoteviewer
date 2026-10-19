#pragma once

// Remote-reboot confirm popup for the PC viewer (prelogin-service r3, d-viewer). A native modal dialog
// (same shape as viewer_unlock.cpp's prompt_unlock_password) shown when the user picks the in-menu
// "다시 시작" item. 확인 -> true (the caller then sets session.rebootRequested exactly once); 취소 or close
// -> false (no request). When showPreloginWarning is set (the host is NOT durable-enabled, i.e.
// kCaptureFlagPreloginEnabledV1 is absent -- decided by reboot_menu_policy), an extra warning line is
// shown that after the restart the PC is unreachable until someone logs in.
//
// The window class and the two button control ids are fixed and exposed so a UI test can find the real
// window, capture it, and inject the real WM_COMMAND a button click would post.

#include <windows.h>

namespace remote60::native_poc {

constexpr wchar_t kRebootConfirmClass[] = L"GNLinkRebootConfirm";
constexpr int kRebootConfirmOkId = 1;      // "다시 시작"
constexpr int kRebootConfirmCancelId = 2;  // "취소"

// Shows the modal confirm dialog and returns true iff the user confirmed. Blocks until dismissed.
bool show_reboot_confirm(HWND owner, bool showPreloginWarning);

}  // namespace remote60::native_poc

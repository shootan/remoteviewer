// Native modal reboot-confirm dialog (viewer_reboot_prompt.hpp). Mirrors viewer_unlock.cpp's prompt.

#include "viewer_reboot_prompt.hpp"

#include <string>

namespace remote60::native_poc {

namespace {

struct RebootPromptState {
  bool confirmed = false;
  bool done = false;
};

LRESULT CALLBACK reboot_prompt_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  auto* st = reinterpret_cast<RebootPromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_COMMAND: {
      const int id = LOWORD(wParam);
      if (id == kRebootConfirmOkId || id == kRebootConfirmCancelId) {
        if (st) {
          st->confirmed = (id == kRebootConfirmOkId);
          st->done = true;
        }
        DestroyWindow(hwnd);
        return 0;
      }
      break;
    }
    case WM_CLOSE:
      if (st) st->done = true;  // close == cancel
      DestroyWindow(hwnd);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

bool show_reboot_confirm(HWND owner, bool showPreloginWarning) {
  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = reboot_prompt_proc;
  wc.hInstance = inst;
  wc.lpszClassName = kRebootConfirmClass;
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassExW(&wc);  // harmless if already registered

  const int w = 440;
  const int h = showPreloginWarning ? 230 : 180;
  int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
  RECT orc{};
  if (owner && GetWindowRect(owner, &orc)) {
    x = orc.left + ((orc.right - orc.left) - w) / 2;
    y = orc.top + ((orc.bottom - orc.top) - h) / 2;
  }

  RebootPromptState st;
  HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kRebootConfirmClass, L"다시 시작",
                             WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, owner, nullptr, inst, nullptr);
  if (!dlg) return false;
  SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));

  std::wstring body = L"원격 PC를 다시 시작합니다.\n저장하지 않은 작업은 사라질 수 있습니다.";
  if (showPreloginWarning) {
    body += L"\n\n로그인 전 원격 접속이 꺼져 있어, 다시 시작한 뒤에는\n그 PC에서 로그인할 때까지 접속할 수 없습니다.";
  }
  CreateWindowExW(0, L"STATIC", body.c_str(), WS_CHILD | WS_VISIBLE, 16, 14, w - 40, h - 90, dlg,
                  nullptr, inst, nullptr);
  CreateWindowExW(0, L"BUTTON", L"다시 시작", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, w - 230, h - 70, 100,
                  28, dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRebootConfirmOkId)), inst, nullptr);
  CreateWindowExW(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON, w - 120,
                  h - 70, 100, 28, dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRebootConfirmCancelId)),
                  inst, nullptr);

  if (owner) EnableWindow(owner, FALSE);
  ShowWindow(dlg, SW_SHOW);
  SetForegroundWindow(dlg);

  MSG m;
  while (!st.done && GetMessageW(&m, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(dlg, &m)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
  }
  if (owner) {
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
  }
  return st.confirmed;
}

}  // namespace remote60::native_poc

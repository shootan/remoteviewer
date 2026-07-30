#include "client_macro_window.hpp"

#include <shlobj.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace remote60::native_poc {
namespace {

constexpr wchar_t kClassName[] = L"GNLinkMacroWindow";
constexpr UINT kPumpTimerId = 1;
constexpr UINT kPumpIntervalMs = 16;

// Control ids.
enum : int {
  kIdRecord = 100,
  kIdPause,
  kIdPlay,
  kIdClear,
  kIdList,
  kIdDeleteStep,
  kIdApplyStep,
  kIdEditX,
  kIdEditY,
  kIdEditDelay,
  kIdRepeat,
  kIdGapMin,
  kIdGapMax,
  kIdTimingJitter,
  kIdPositionJitter,
  kIdSaveName,
  kIdSave,
  kIdSavedCombo,
  kIdLoad,
  kIdDeleteSaved,
  kIdStatus,
};

HWND gWindow = nullptr;
MacroWindowHooks gHooks;
HFONT gFont = nullptr;
size_t gRenderedCount = static_cast<size_t>(-1);
int gRenderedState = -1;

std::wstring widen_ascii(const std::string& text) {
  return std::wstring(text.begin(), text.end());
}

std::wstring storage_dir() {
  PWSTR base = nullptr;
  std::wstring dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &base)) && base) {
    dir = base;
    CoTaskMemFree(base);
  } else {
    dir = L".";
  }
  dir += L"\\GNLink";
  CreateDirectoryW(dir.c_str(), nullptr);
  dir += L"\\macros";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

int read_int(HWND parent, int id, int fallback) {
  wchar_t buffer[32] = {};
  GetWindowTextW(GetDlgItem(parent, id), buffer, 31);
  if (buffer[0] == 0) return fallback;
  return _wtoi(buffer);
}

void set_text(HWND parent, int id, const std::wstring& text) {
  SetWindowTextW(GetDlgItem(parent, id), text.c_str());
}

std::wstring get_text(HWND parent, int id) {
  wchar_t buffer[128] = {};
  GetWindowTextW(GetDlgItem(parent, id), buffer, 127);
  return buffer;
}

HWND make_control(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x,
                  int y, int w, int h, int id) {
  HWND control = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                                 parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 GetModuleHandleW(nullptr), nullptr);
  if (control && gFont) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
  }
  return control;
}

void refresh_saved_combo(HWND hwnd) {
  HWND combo = GetDlgItem(hwnd, kIdSavedCombo);
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  WIN32_FIND_DATAW found{};
  const std::wstring pattern = storage_dir() + L"\\*.gnmacro";
  HANDLE it = FindFirstFileW(pattern.c_str(), &found);
  if (it != INVALID_HANDLE_VALUE) {
    do {
      std::wstring name = found.cFileName;
      const size_t dot = name.rfind(L".gnmacro");
      if (dot != std::wstring::npos) name.resize(dot);
      SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    } while (FindNextFileW(it, &found));
    FindClose(it);
  }
  SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

/** Rebuilds the step list only when the macro actually changed; the timer calls this at 60 Hz. */
void refresh_step_list(HWND hwnd, bool force) {
  InputMacro* macro = gHooks.macro;
  if (!macro) return;
  const auto steps = macro->Steps();
  const int state = static_cast<int>(macro->state());
  if (!force && steps.size() == gRenderedCount && state == gRenderedState) return;
  gRenderedCount = steps.size();
  gRenderedState = state;

  HWND list = GetDlgItem(hwnd, kIdList);
  const LRESULT selected = SendMessageW(list, LB_GETCURSEL, 0, 0);
  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  for (size_t i = 0; i < steps.size(); ++i) {
    const std::wstring line = widen_ascii(InputMacro::DescribeStep(steps[i], i));
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
  }
  if (selected >= 0 && static_cast<size_t>(selected) < steps.size()) {
    SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(selected), 0);
  }
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);
}

void refresh_status(HWND hwnd) {
  InputMacro* macro = gHooks.macro;
  if (!macro) return;
  const auto state = macro->state();
  const bool paused = macro->IsPaused();
  wchar_t text[128];
  if (state == InputMacro::State::Recording) {
    std::swprintf(text, 128, paused ? L"녹화 일시정지됨 - %zu개" : L"● 녹화 중 - %zu개",
                  macro->StepCount());
  } else if (state == InputMacro::State::Playing) {
    std::swprintf(text, 128, paused ? L"재생 일시정지됨" : L"▶ 재생 중 - %u회 반복 완료",
                  macro->CompletedRepeats());
  } else {
    std::swprintf(text, 128, L"%zu개 동작 녹화됨", macro->StepCount());
  }
  set_text(hwnd, kIdStatus, text);

  set_text(hwnd, kIdRecord, state == InputMacro::State::Recording ? L"중지" : L"녹화");
  set_text(hwnd, kIdPlay, state == InputMacro::State::Playing ? L"중지" : L"재생");
  set_text(hwnd, kIdPause, paused ? L"재개" : L"일시정지");
  EnableWindow(GetDlgItem(hwnd, kIdPause), state != InputMacro::State::Idle);
  EnableWindow(GetDlgItem(hwnd, kIdPlay),
               macro->StepCount() > 0 || state == InputMacro::State::Playing);
}

uint64_t now_ms() { return GetTickCount64(); }

void prefill_step_editors(HWND hwnd) {
  InputMacro* macro = gHooks.macro;
  if (!macro) return;
  const LRESULT selected = SendMessageW(GetDlgItem(hwnd, kIdList), LB_GETCURSEL, 0, 0);
  const auto steps = macro->Steps();
  if (selected < 0 || static_cast<size_t>(selected) >= steps.size()) return;
  const MacroStep& step = steps[static_cast<size_t>(selected)];
  set_text(hwnd, kIdEditX, std::to_wstring(step.x));
  set_text(hwnd, kIdEditY, std::to_wstring(step.y));
  set_text(hwnd, kIdEditDelay, std::to_wstring(step.delayMs));
}

void start_playback(HWND hwnd) {
  InputMacro* macro = gHooks.macro;
  if (!macro) return;
  MacroPlaybackOptions options;
  options.timingJitterMs = static_cast<uint32_t>(std::max(0, read_int(hwnd, kIdTimingJitter, 0)));
  options.positionJitterPx =
      static_cast<uint32_t>(std::max(0, read_int(hwnd, kIdPositionJitter, 0)));
  options.repeatCount = static_cast<uint32_t>(std::max(0, read_int(hwnd, kIdRepeat, 1)));
  options.repeatGapMinMs = static_cast<uint32_t>(std::max(0, read_int(hwnd, kIdGapMin, 0)));
  options.repeatGapMaxMs = static_cast<uint32_t>(std::max(0, read_int(hwnd, kIdGapMax, 0)));
  macro->StartPlayback(options, now_ms(), static_cast<uint32_t>(now_ms()));
}

void save_current(HWND hwnd) {
  InputMacro* macro = gHooks.macro;
  if (!macro || macro->StepCount() == 0) return;
  std::wstring name = get_text(hwnd, kIdSaveName);
  // The name becomes a file name; strip anything the filesystem might interpret.
  for (wchar_t& c : name) {
    if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' ||
        c == L'<' || c == L'>' || c == L'|') {
      c = L'_';
    }
  }
  while (!name.empty() && name.back() == L' ') name.pop_back();
  if (name.empty()) {
    MessageBoxW(hwnd, L"매크로 이름을 입력하세요.", L"매크로 저장", MB_OK | MB_ICONINFORMATION);
    return;
  }
  const std::wstring path = storage_dir() + L"\\" + name + L".gnmacro";
  std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
  if (!out) {
    MessageBoxW(hwnd, L"파일을 쓸 수 없습니다.", L"매크로 저장", MB_OK | MB_ICONWARNING);
    return;
  }
  const std::string text = macro->Serialize();
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  out.close();
  refresh_saved_combo(hwnd);
}

std::wstring selected_saved_name(HWND hwnd) {
  HWND combo = GetDlgItem(hwnd, kIdSavedCombo);
  const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
  if (index < 0) return L"";
  const LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, static_cast<WPARAM>(index), 0);
  if (length <= 0 || length > 512) return L"";
  std::wstring name(static_cast<size_t>(length) + 1, L'\0');
  SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(index),
               reinterpret_cast<LPARAM>(name.data()));
  name.resize(static_cast<size_t>(length));
  return name;
}

void load_selected(HWND hwnd) {
  InputMacro* macro = gHooks.macro;
  if (!macro) return;
  const std::wstring name = selected_saved_name(hwnd);
  if (name.empty()) return;
  const std::wstring path = storage_dir() + L"\\" + name + L".gnmacro";
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) {
    MessageBoxW(hwnd, L"파일을 열 수 없습니다.", L"매크로 불러오기", MB_OK | MB_ICONWARNING);
    return;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  if (!macro->LoadSerialized(buffer.str())) {
    MessageBoxW(hwnd, L"매크로 파일이 아니거나 손상되었습니다.", L"매크로 불러오기",
                MB_OK | MB_ICONWARNING);
    return;
  }
  set_text(hwnd, kIdSaveName, name);
  refresh_step_list(hwnd, true);
  refresh_status(hwnd);
}

void delete_selected_saved(HWND hwnd) {
  const std::wstring name = selected_saved_name(hwnd);
  if (name.empty()) return;
  std::wstring prompt = L"삭제할까요: " + name + L"?";
  if (MessageBoxW(hwnd, prompt.c_str(), L"저장된 매크로", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
    return;
  }
  DeleteFileW((storage_dir() + L"\\" + name + L".gnmacro").c_str());
  refresh_saved_combo(hwnd);
}

void create_controls(HWND hwnd) {
  NONCLIENTMETRICSW metrics{};
  metrics.cbSize = sizeof(metrics);
  if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
    gFont = CreateFontIndirectW(&metrics.lfMessageFont);
  }

  const int margin = 12;
  const int buttonH = 30;
  int y = margin;

  make_control(hwnd, L"STATIC", L"", 0, margin, y + 6, 440, 20, kIdStatus);
  y += 30;

  int x = margin;
  make_control(hwnd, L"BUTTON", L"녹화", BS_PUSHBUTTON, x, y, 84, buttonH, kIdRecord);
  x += 90;
  make_control(hwnd, L"BUTTON", L"일시정지", BS_PUSHBUTTON, x, y, 84, buttonH, kIdPause);
  x += 90;
  make_control(hwnd, L"BUTTON", L"재생", BS_PUSHBUTTON, x, y, 84, buttonH, kIdPlay);
  x += 90;
  make_control(hwnd, L"BUTTON", L"지우기", BS_PUSHBUTTON, x, y, 84, buttonH, kIdClear);
  y += buttonH + 10;

  make_control(hwnd, L"LISTBOX", L"",
               LBS_NOTIFY | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, margin, y, 440, 200,
               kIdList);
  y += 208;

  make_control(hwnd, L"STATIC", L"X", 0, margin, y + 6, 16, 20, 0);
  make_control(hwnd, L"EDIT", L"", WS_BORDER | ES_NUMBER, margin + 18, y, 70, 26, kIdEditX);
  make_control(hwnd, L"STATIC", L"Y", 0, margin + 96, y + 6, 16, 20, 0);
  make_control(hwnd, L"EDIT", L"", WS_BORDER | ES_NUMBER, margin + 114, y, 70, 26, kIdEditY);
  make_control(hwnd, L"STATIC", L"간격ms", 0, margin + 192, y + 6, 46, 20, 0);
  make_control(hwnd, L"EDIT", L"", WS_BORDER | ES_NUMBER, margin + 240, y, 60, 26, kIdEditDelay);
  make_control(hwnd, L"BUTTON", L"수정", BS_PUSHBUTTON, margin + 308, y - 2, 60, buttonH,
               kIdApplyStep);
  make_control(hwnd, L"BUTTON", L"선택 삭제", BS_PUSHBUTTON, margin + 374, y - 2, 78, buttonH,
               kIdDeleteStep);
  y += 38;

  make_control(hwnd, L"STATIC", L"반복(0=계속)", 0, margin, y + 6, 82, 20, 0);
  make_control(hwnd, L"EDIT", L"1", WS_BORDER | ES_NUMBER, margin + 86, y, 50, 26, kIdRepeat);
  make_control(hwnd, L"STATIC", L"반복 간격 ms", 0, margin + 148, y + 6, 82, 20, 0);
  make_control(hwnd, L"EDIT", L"500", WS_BORDER | ES_NUMBER, margin + 234, y, 56, 26, kIdGapMin);
  make_control(hwnd, L"STATIC", L"~", 0, margin + 294, y + 6, 12, 20, 0);
  make_control(hwnd, L"EDIT", L"1500", WS_BORDER | ES_NUMBER, margin + 308, y, 56, 26, kIdGapMax);
  y += 34;

  make_control(hwnd, L"STATIC", L"시간 흔들기 ms", 0, margin, y + 6, 94, 20, 0);
  make_control(hwnd, L"EDIT", L"40", WS_BORDER | ES_NUMBER, margin + 98, y, 50, 26,
               kIdTimingJitter);
  make_control(hwnd, L"STATIC", L"위치 흔들기 px", 0, margin + 160, y + 6, 94, 20, 0);
  make_control(hwnd, L"EDIT", L"2", WS_BORDER | ES_NUMBER, margin + 258, y, 50, 26,
               kIdPositionJitter);
  y += 40;

  make_control(hwnd, L"EDIT", L"", WS_BORDER, margin, y, 200, 26, kIdSaveName);
  make_control(hwnd, L"BUTTON", L"저장", BS_PUSHBUTTON, margin + 206, y - 2, 60, buttonH, kIdSave);
  y += 36;

  make_control(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, margin, y, 200, 200,
               kIdSavedCombo);
  make_control(hwnd, L"BUTTON", L"불러오기", BS_PUSHBUTTON, margin + 206, y - 2, 78, buttonH,
               kIdLoad);
  make_control(hwnd, L"BUTTON", L"삭제", BS_PUSHBUTTON, margin + 290, y - 2, 60, buttonH,
               kIdDeleteSaved);

  refresh_saved_combo(hwnd);
}

LRESULT CALLBACK MacroWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  InputMacro* macro = gHooks.macro;
  switch (msg) {
    case WM_CREATE:
      create_controls(hwnd);
      SetTimer(hwnd, kPumpTimerId, kPumpIntervalMs, nullptr);
      return 0;

    case WM_TIMER: {
      if (wp != kPumpTimerId || !macro) return 0;
      // Sends whatever is due. Capped so a huge backlog cannot monopolise the UI thread.
      MacroStep step;
      int sent = 0;
      const uint64_t now = now_ms();
      while (macro->PollDueStep(now, &step)) {
        if (gHooks.sendStep) gHooks.sendStep(step);
        if (++sent > 64) break;
      }
      refresh_status(hwnd);
      refresh_step_list(hwnd, false);
      return 0;
    }

    case WM_COMMAND: {
      if (!macro) return 0;
      const int id = LOWORD(wp);
      const int code = HIWORD(wp);
      switch (id) {
        case kIdRecord:
          if (macro->IsRecording()) {
            macro->StopRecording();
          } else {
            macro->StopPlayback();
            macro->StartRecording(now_ms());
          }
          break;
        case kIdPause:
          macro->SetPaused(!macro->IsPaused(), now_ms());
          break;
        case kIdPlay:
          if (macro->IsPlaying()) {
            macro->StopPlayback();
          } else {
            macro->StopRecording();
            start_playback(hwnd);
          }
          break;
        case kIdClear:
          macro->StopPlayback();
          macro->StopRecording();
          macro->Clear();
          refresh_step_list(hwnd, true);
          break;
        case kIdList:
          if (code == LBN_SELCHANGE) prefill_step_editors(hwnd);
          return 0;
        case kIdDeleteStep: {
          const LRESULT selected = SendMessageW(GetDlgItem(hwnd, kIdList), LB_GETCURSEL, 0, 0);
          if (selected >= 0) {
            macro->RemoveStep(static_cast<size_t>(selected));
            refresh_step_list(hwnd, true);
          }
          break;
        }
        case kIdApplyStep: {
          const LRESULT selected = SendMessageW(GetDlgItem(hwnd, kIdList), LB_GETCURSEL, 0, 0);
          if (selected >= 0) {
            macro->UpdateStep(static_cast<size_t>(selected), read_int(hwnd, kIdEditX, 0),
                              read_int(hwnd, kIdEditY, 0), read_int(hwnd, kIdEditDelay, 0));
            refresh_step_list(hwnd, true);
          }
          break;
        }
        case kIdSave:
          save_current(hwnd);
          break;
        case kIdLoad:
          load_selected(hwnd);
          break;
        case kIdDeleteSaved:
          delete_selected_saved(hwnd);
          break;
        default:
          return 0;
      }
      refresh_status(hwnd);
      return 0;
    }

    case WM_CLOSE:
      // Hidden, not destroyed: a running playback keeps pumping on the timer.
      ShowWindow(hwnd, SW_HIDE);
      return 0;

    case WM_DESTROY:
      KillTimer(hwnd, kPumpTimerId);
      if (gFont) {
        DeleteObject(gFont);
        gFont = nullptr;
      }
      gWindow = nullptr;
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void macro_window_toggle(HINSTANCE instance, HWND owner, const MacroWindowHooks& hooks) {
  gHooks = hooks;
  if (gWindow && IsWindowVisible(gWindow)) {
    ShowWindow(gWindow, SW_HIDE);
    return;
  }
  if (!gWindow) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MacroWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW, ANSI-proof
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);   // Refuses quietly if already registered.

    RECT ownerRect{};
    int x = CW_USEDEFAULT;
    int yPos = CW_USEDEFAULT;
    if (owner && GetWindowRect(owner, &ownerRect)) {
      x = ownerRect.right + 8;
      yPos = ownerRect.top;
    }
    RECT frame = {0, 0, 478, 560};
    AdjustWindowRectEx(&frame, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
    gWindow = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"매크로",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, x, yPos,
                              frame.right - frame.left, frame.bottom - frame.top, owner,
                              nullptr, instance, nullptr);
    if (!gWindow) return;
  }
  gRenderedCount = static_cast<size_t>(-1);
  gRenderedState = -1;
  refresh_step_list(gWindow, true);
  refresh_status(gWindow);
  refresh_saved_combo(gWindow);
  ShowWindow(gWindow, SW_SHOWNOACTIVATE);
}

bool macro_window_visible() { return gWindow && IsWindowVisible(gWindow); }

void macro_window_destroy() {
  if (gWindow) DestroyWindow(gWindow);
}

}  // namespace remote60::native_poc

# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/update_process_targets.cpp'
d = io.open(p, encoding='utf-8').read()

# 1. the probe belongs inside the anonymous namespace, next to the walk it reuses
old = '''bool process_has_top_level_window(uint32_t pid) {
  // The same walk request_process_stop uses to decide whether WM_CLOSE is possible, asked as a
  // question instead of as an action. Whether a process can be asked politely is a property of
  // the process, and the caller needs it BEFORE deciding whom to ask.
  WindowCloser probe{static_cast<DWORD>(pid), false};
  probe.dryRun = true;
  EnumWindows(close_top_level, reinterpret_cast<LPARAM>(&probe));
  return probe.posted;
}

std::vector<std::wstring> product_payload_names() {'''
new = '''std::vector<std::wstring> product_payload_names() {'''
assert d.count(old) == 1, 'move probe'
d = d.replace(old, new, 1)

# 2. WindowCloser learns to look without touching
old2 = '''struct WindowCloser {
  DWORD pid = 0;
  bool posted = false;
};

BOOL CALLBACK close_top_level(HWND hwnd, LPARAM param) {
  auto* closer = reinterpret_cast<WindowCloser*>(param);
  DWORD owner = 0;
  GetWindowThreadProcessId(hwnd, &owner);
  if (owner == closer->pid) {
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    closer->posted = true;
  }
  return TRUE;
}

}  // namespace'''
new2 = '''struct WindowCloser {
  DWORD pid = 0;
  bool posted = false;
  // Look without touching. The same walk answers two different questions -- "close this" and
  // "could this be closed" -- and the second one has to be asked BEFORE deciding whom to ask,
  // which is the whole point of routing a windowless process to its supervisor.
  bool dryRun = false;
};

BOOL CALLBACK close_top_level(HWND hwnd, LPARAM param) {
  auto* closer = reinterpret_cast<WindowCloser*>(param);
  DWORD owner = 0;
  GetWindowThreadProcessId(hwnd, &owner);
  if (owner == closer->pid) {
    if (!closer->dryRun) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    closer->posted = true;
  }
  return TRUE;
}

/** Whether this process owns a top-level window, i.e. whether WM_CLOSE is a possible request. */
bool process_has_top_level_window(uint32_t pid) {
  WindowCloser probe{static_cast<DWORD>(pid), false, true};
  EnumWindows(close_top_level, reinterpret_cast<LPARAM>(&probe));
  return probe.posted;
}

}  // namespace'''
assert d.count(old2) == 1, 'WindowCloser'
d = d.replace(old2, new2, 1)

# 3. the CTRL_BREAK comment now says exactly why it fails, and that it is a last resort for
#    processes nobody claims
old3 = '''  // The console-subsystem children (GNLinkStream, GNLinkCapture, GNLinkViewer) have no window.
  // A CTRL_BREAK to their group is the closest thing to a polite request; it only works when
  // they share a console with us, which the product's own supervisor does not guarantee.
  if (GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, static_cast<DWORD>(pid))) return true;'''
new3 = '''  // A windowless process, and nobody we asked owns it.
  //
  // This almost never succeeds, and it is worth being exact about why: the second argument is a
  // process GROUP id, not a pid. It works only if this pid happens to be a group leader AND
  // shares our console -- and the product's supervisor starts its children with neither. For
  // years this was the only path for GNLinkStream and GNLinkCapture, which is why an update could
  // not proceed while either was running.
  //
  // It is kept for the case it was always meant for: a windowless process that is NOT a child of
  // anything we are already asking, where there is nothing else to try. PrepareForSwap no longer
  // routes owned children here.
  if (GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, static_cast<DWORD>(pid))) return true;'''
assert d.count(old3) == 1, 'ctrl break comment'
d = d.replace(old3, new3, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')

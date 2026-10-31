# -*- coding: utf-8 -*-
import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d: %s' % (path, i, old[:70])
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)


# ---------------------------------------------------------------- ProcessTarget grows two facts
edit('apps/native_poc/src/update_effects.hpp', [
 ('''struct ProcessTarget {
  uint32_t pid = 0;
  std::wstring imagePath;    // full path as observed at enumeration time
  uint64_t creationTime = 0; // FILETIME as a single value; 0 when it could not be read

  bool operator==(const ProcessTarget& other) const {
    return pid == other.pid && creationTime == other.creationTime && imagePath == other.imagePath;
  }
};''',
  '''struct ProcessTarget {
  uint32_t pid = 0;
  std::wstring imagePath;    // full path as observed at enumeration time
  uint64_t creationTime = 0; // FILETIME as a single value; 0 when it could not be read
  // Who started it, and whether it can be asked to close the way a person would close it.
  //
  // Both exist so that a windowless process can be routed to its supervisor instead of being
  // asked directly. Asking directly is what could not work: the only mechanism for a process
  // without a window is a console control event, and that needs a shared console the product's
  // own supervisor does not give it. 0 / false mean "not known", and a target that is not known
  // to be owned is asked directly rather than assumed to belong to someone.
  uint32_t parentPid = 0;
  bool hasWindow = false;

  // Identity only. parentPid and hasWindow describe the moment, not the process: the same process
  // is the same process whether or not it had opened a window yet.
  bool operator==(const ProcessTarget& other) const {
    return pid == other.pid && creationTime == other.creationTime && imagePath == other.imagePath;
  }
};'''),
 ('''  std::string stagedReleaseId_;
  std::vector<std::wstring> stagedNames_;
  bool swapped_ = false;''',
  '''  std::string stagedReleaseId_;
  std::vector<std::wstring> stagedNames_;
  // The exact processes PrepareForSwap asked about, kept so that Quiesce waits for those and not
  // for whatever a second enumeration happens to find. Re-enumerating meant a process that
  // started in between became something to wait for, and one that had already gone silently
  // stopped being checked at all.
  std::vector<ProcessTarget> preparedTargets_;
  std::vector<uint32_t> ownedChildPids_;
  bool swapped_ = false;'''),
])

# ---------------------------------------------------------------- the enumerator fills them in
edit('apps/native_poc/src/update_process_targets.cpp', [
 ('''        ProcessTarget target;
        if (capture_process_identity(static_cast<uint32_t>(entry.th32ProcessID), &target)) {''',
  '''        ProcessTarget target;
        if (capture_process_identity(static_cast<uint32_t>(entry.th32ProcessID), &target)) {
          target.parentPid = static_cast<uint32_t>(entry.th32ParentProcessID);
          target.hasWindow = process_has_top_level_window(target.pid);'''),
 ('''  const uint32_t pid = target.pid;
  // A GUI process gets WM_CLOSE on its top-level windows.''',
  '''  const uint32_t pid = target.pid;
  // A GUI process gets WM_CLOSE on its top-level windows.'''),
])

# a probe used by the enumerator, next to the existing window walk
d = io.open('apps/native_poc/src/update_process_targets.cpp', encoding='utf-8').read()
old = '''std::vector<std::wstring> product_payload_names() {'''
new = '''bool process_has_top_level_window(uint32_t pid) {
  // The same walk request_process_stop uses to decide whether WM_CLOSE is possible, asked as a
  // question instead of as an action. Whether a process can be asked politely is a property of
  // the process, and the caller needs it BEFORE deciding whom to ask.
  WindowCloser probe{static_cast<DWORD>(pid), false};
  probe.dryRun = true;
  EnumWindows(close_top_level, reinterpret_cast<LPARAM>(&probe));
  return probe.posted;
}

std::vector<std::wstring> product_payload_names() {'''
assert d.count(old) == 1, 'probe anchor'
d = d.replace(old, new, 1)
io.open('apps/native_poc/src/update_process_targets.cpp', 'w', encoding='utf-8', newline='').write(d)
print('ok probe')

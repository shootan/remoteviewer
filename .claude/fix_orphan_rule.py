# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/update_effects.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  // Whether a pid appears among the targets at all. A windowless process whose parent is not here
  // has no supervisor left to ask -- the parent has already exited.
  const auto is_a_target = [this](uint32_t pid) {
    for (const ProcessTarget& t : preparedTargets_) {
      if (t.pid == pid) return true;
    }
    return false;
  };
'''
new = '''  // An orphan is a narrower thing than "its parent is not in this list", and getting that wrong
  // is how the first version of this broke every caller that describes its targets by hand: a
  // ProcessTarget with parentPid == 0 means "not known", and treating an unknown as a dead parent
  // routed processes into a wait that nothing had asked to stop.
  //
  // Unknown parentage is asked directly -- the conservative answer, and the one the field code
  // took before any of this. A parent that is ALIVE but not one of our targets is not an orphan
  // either: it has a supervisor, just not one we are stopping, so waiting for it would be waiting
  // for nobody. Only a parent that has actually exited leaves a process with nobody to ask.
  const auto parent_has_exited = [](uint32_t pid) {
    if (pid == 0) return false;  // not known, which is not the same as gone
    SetLastError(0);
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (h) {
      const bool signalled = WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
      CloseHandle(h);
      return signalled;
    }
    // ERROR_INVALID_PARAMETER is a pid that is no longer a process. Anything else (access denied)
    // means the question could not be asked, and an unanswered question is not a yes.
    return GetLastError() == ERROR_INVALID_PARAMETER;
  };
'''
assert d.count(old) == 1, 'helper'
d = d.replace(old, new, 1)

old2 = '''      if (!is_a_target(target.parentPid)) {'''
new2 = '''      if (parent_has_exited(target.parentPid)) {'''
assert d.count(old2) == 1, 'condition'
d = d.replace(old2, new2, 1)

# the comment below it described the old condition
old3 = '''        // An orphan: no window, and the process that started it is gone. This is what the field
        // log recorded -- the host had already closed (the relaunch line proves it), leaving a
        // console child behind, and because the parent was no longer in the list the ownership
        // check could not pass. It fell through to a direct request, which for a windowless
        // process cannot succeed, and the attempt was abandoned.'''
new3 = '''        // An orphan: no window, and the process that started it has exited. This is what the
        // field log recorded -- the host had already closed (the relaunch line proves it), leaving
        // a console child behind, and with the parent gone the ownership check could not pass. It
        // fell through to a direct request, which for a windowless process cannot succeed, and the
        // attempt was abandoned.'''
assert d.count(old3) == 1, 'comment'
d = d.replace(old3, new3, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')

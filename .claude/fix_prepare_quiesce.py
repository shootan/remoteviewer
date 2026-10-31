# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/update_effects.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''bool WindowsUpdateEffects::PrepareForSwap() {
  // Asking is separate from waiting. If a target cannot even be asked to stop, the attempt is
  // abandoned here -- before anything on disk is touched -- rather than escalated.
  const std::vector<ProcessTarget> targets = config_.enumerateTargets();
  for (const ProcessTarget& target : targets) {
    if (!config_.requestStop(target)) {
      lastError_ = "could not ask pid " + std::to_string(target.pid) + " to stop";
      return false;
    }
  }
  return true;
}

bool WindowsUpdateEffects::Quiesce() {
  // Waits for the exact processes that were asked to stop. No image-name sweep, no /T tree kill,
  // and no forced termination: a target that will not exit means the update does not happen
  // (design 3.2, and the state machine turns this into AbandonedBeforeSwap).
  const DWORD deadline = GetTickCount() + config_.quiesceTimeoutMs;
  for (const ProcessTarget& target : config_.enumerateTargets()) {
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
    if (!h) {
      // Already gone, or not ours to wait on. Either way there is nothing to wait for.
      continue;
    }'''

new = '''bool WindowsUpdateEffects::PrepareForSwap() {
  // Asking is separate from waiting. If a target cannot even be asked to stop, the attempt is
  // abandoned here -- before anything on disk is touched -- rather than escalated.
  preparedTargets_ = config_.enumerateTargets();
  ownedChildPids_.clear();

  // Who can be asked, and who belongs to somebody who can.
  //
  // A process without a window cannot be asked directly. The only mechanism for one is a console
  // control event, and that needs a console the supervisor does not share with the children it
  // starts -- so for GNLinkStream and GNLinkCapture the request could never succeed, and an
  // update could not proceed while either was running. It was not a flaky path; it was a path
  // with no success case.
  //
  // So a windowless target that was started by a supervisor we are already asking is left to that
  // supervisor, the way closing an application leaves it to close its own children.
  //
  // Ownership is verified, never assumed: the parent has to be one of these targets, have a
  // window of its own, and have started before the child. Without all three a standalone console
  // process would be quietly adopted by whichever GUI process happened to be in the list, and
  // then nobody would ask it to stop at all.
  const auto owner_of = [this](const ProcessTarget& child) -> const ProcessTarget* {
    if (child.parentPid == 0 || child.creationTime == 0) return nullptr;
    for (const ProcessTarget& parent : preparedTargets_) {
      if (parent.pid != child.parentPid) continue;
      if (!parent.hasWindow) return nullptr;
      // A parent that started after its child is not the parent: the pid was reused.
      if (parent.creationTime == 0 || parent.creationTime >= child.creationTime) return nullptr;
      return &parent;
    }
    return nullptr;
  };

  for (const ProcessTarget& target : preparedTargets_) {
    if (!target.hasWindow && owner_of(target) != nullptr) {
      ownedChildPids_.push_back(target.pid);
      continue;
    }
    if (!config_.requestStop(target)) {
      lastError_ = "could not ask pid " + std::to_string(target.pid) + " to stop";
      return false;
    }
  }
  return true;
}

bool WindowsUpdateEffects::Quiesce() {
  // Waits for the exact processes that were asked to stop. No image-name sweep, no /T tree kill,
  // and no forced termination: a target that will not exit means the update does not happen
  // (design 3.2, and the state machine turns this into AbandonedBeforeSwap).
  //
  // The list is the one PrepareForSwap worked from, not a fresh enumeration. Enumerating again
  // meant waiting for whatever had started in between -- and, worse, silently not waiting for
  // something that had already been asked and was on its way out.
  const DWORD deadline = GetTickCount() + config_.quiesceTimeoutMs;
  const std::vector<ProcessTarget> fallback =
      preparedTargets_.empty() ? config_.enumerateTargets() : std::vector<ProcessTarget>{};
  const std::vector<ProcessTarget>& targets =
      preparedTargets_.empty() ? fallback : preparedTargets_;
  for (const ProcessTarget& target : targets) {
    SetLastError(0);
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
    if (!h) {
      // Gone is not the same as "cannot look". ERROR_ACCESS_DENIED means the answer is unknown,
      // and an unknown treated as "it exited" is how an update proceeds over a process that is
      // still holding the files it is about to replace.
      if (GetLastError() == ERROR_ACCESS_DENIED) {
        lastError_ = "cannot tell whether pid " + std::to_string(target.pid) +
                     " exited (access denied)";
        return false;
      }
      // Anything else here means the pid is not a process any more, which is what we wanted.
      continue;
    }'''

assert d.count(old) == 1, 'prepare/quiesce'
d = d.replace(old, new, 1)

old2 = '''    if (waited != WAIT_OBJECT_0) {
      lastError_ = "pid " + std::to_string(target.pid) + " did not exit";
      return false;
    }'''
new2 = '''    if (waited != WAIT_OBJECT_0) {
      // Two different failures, and they were one message. "Could not be asked" is a supervisor
      // that has no window; "outlived its parent" is a child whose supervisor was asked, went
      // away, and left it behind. Only the second says the supervisor contract is broken.
      const bool owned =
          std::find(ownedChildPids_.begin(), ownedChildPids_.end(), target.pid) !=
          ownedChildPids_.end();
      lastError_ = owned ? "pid " + std::to_string(target.pid) + " (started by pid " +
                               std::to_string(target.parentPid) + ") outlived its parent"
                         : "pid " + std::to_string(target.pid) + " did not exit";
      return false;
    }'''
assert d.count(old2) == 1, 'quiesce message'
d = d.replace(old2, new2, 1)

if '#include <algorithm>' not in d:
    anchor = '#include <string>'
    assert d.count(anchor) >= 1
    d = d.replace(anchor, '#include <algorithm>\n#include <string>', 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')

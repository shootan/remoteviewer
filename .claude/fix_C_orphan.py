# -*- coding: utf-8 -*-
import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d: %s' % (path, i, old[:70])
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)


edit('apps/native_poc/src/update_effects.hpp', [
 ('''  std::vector<ProcessTarget> preparedTargets_;
  std::vector<uint32_t> ownedChildPids_;''',
  '''  std::vector<ProcessTarget> preparedTargets_;
  std::vector<uint32_t> ownedChildPids_;
  // Windowless targets with nobody to ask: their parent is not among the targets, which means it
  // has already exited. Waited for rather than asked, and told apart from the owned ones so the
  // log says which situation it was.
  std::vector<uint32_t> orphanPids_;'''),
])

edit('apps/native_poc/src/update_effects.cpp', [
 ('''  for (const ProcessTarget& target : preparedTargets_) {
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
}''',
  '''  // Whether a pid appears among the targets at all. A windowless process whose parent is not here
  // has no supervisor left to ask -- the parent has already exited.
  const auto is_a_target = [this](uint32_t pid) {
    for (const ProcessTarget& t : preparedTargets_) {
      if (t.pid == pid) return true;
    }
    return false;
  };

  for (const ProcessTarget& target : preparedTargets_) {
    if (!target.hasWindow) {
      if (owner_of(target) != nullptr) {
        ownedChildPids_.push_back(target.pid);
        continue;
      }
      if (!is_a_target(target.parentPid)) {
        // An orphan: no window, and the process that started it is gone. This is what the field
        // log recorded -- the host had already closed (the relaunch line proves it), leaving a
        // console child behind, and because the parent was no longer in the list the ownership
        // check could not pass. It fell through to a direct request, which for a windowless
        // process cannot succeed, and the attempt was abandoned.
        //
        // There is nobody to ask, and asking it is the thing that does not work. So it is waited
        // for instead: a child whose supervisor has gone is usually already on its way out. If it
        // is not gone by the deadline the update is abandoned anyway -- which is exactly what
        // happens today, so this can only turn a certain failure into a possible success.
        orphanPids_.push_back(target.pid);
        continue;
      }
    }
    if (!config_.requestStop(target)) {
      lastError_ = "could not ask pid " + std::to_string(target.pid) + " to stop";
      return false;
    }
  }
  return true;
}'''),
 ('''      const bool owned =
          std::find(ownedChildPids_.begin(), ownedChildPids_.end(), target.pid) !=
          ownedChildPids_.end();
      lastError_ = owned ? "pid " + std::to_string(target.pid) + " (started by pid " +
                               std::to_string(target.parentPid) + ") outlived its parent"
                         : "pid " + std::to_string(target.pid) + " did not exit";
      return false;''',
  '''      const bool owned =
          std::find(ownedChildPids_.begin(), ownedChildPids_.end(), target.pid) !=
          ownedChildPids_.end();
      const bool orphan =
          std::find(orphanPids_.begin(), orphanPids_.end(), target.pid) != orphanPids_.end();
      // Three different situations that used to share one sentence. Which one it was decides
      // where to look: a supervisor that ignored the request, a child its supervisor left behind,
      // or a windowless process nobody was left to ask.
      if (owned) {
        lastError_ = "pid " + std::to_string(target.pid) + " (started by pid " +
                     std::to_string(target.parentPid) + ") outlived its parent";
      } else if (orphan) {
        lastError_ = "orphaned windowless pid " + std::to_string(target.pid) +
                     " did not exit (its parent " + std::to_string(target.parentPid) +
                     " was already gone, so there was nobody to ask)";
      } else {
        lastError_ = "pid " + std::to_string(target.pid) + " did not exit";
      }
      return false;'''),
 ('''  preparedTargets_ = config_.enumerateTargets();
  ownedChildPids_.clear();''',
  '''  preparedTargets_ = config_.enumerateTargets();
  ownedChildPids_.clear();
  orphanPids_.clear();'''),
])

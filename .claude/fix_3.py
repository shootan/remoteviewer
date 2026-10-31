# -*- coding: utf-8 -*-
import io

def edit(path, pairs):
    d = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(pairs):
        assert d.count(old) == 1, '%s pair %d: %s' % (path, i, old[:70])
        d = d.replace(old, new, 1)
    io.open(path, 'w', encoding='utf-8', newline='').write(d)
    print('ok', path)


edit('apps/native_poc/src/update_relaunch.hpp', [
 ('''  /** How often it looks. */
  uint32_t healthPollMs = 500;''',
  '''  /** How often it looks. */
  uint32_t healthPollMs = 500;

  /**
   * How long a process that was ASKED to stop is given to finish closing before "still running"
   * is believed.
   *
   * The field log has the whole failure in two lines: at 18:56:20.266 the host acknowledged and
   * was standing down, and at 18:56:20.275 -- nine milliseconds later -- this read it as still
   * running and brought nothing back. The attempt was abandoned, the host closed on its own a
   * moment later, and there was no longer anything to restart.
   *
   * Exhausting the grace is NOT an exit. A process that is still there when the time is up stays
   * Running, and the relaunch is skipped for the honest reason that it really is still there.
   *
   * Zero by default so a test injecting a fixed answer does not pay a real timeout for a constant.
   * Production sets it (updater_effects.cpp); anything driving the real liveness check should.
   */
  uint32_t closingGraceMs = 0;'''),
])

edit('apps/native_poc/src/update_relaunch.cpp', [
 ('''        bool alive = false;
        bool unknown = false;
        for (const ProcessTarget& target : shared->stopped) {
          if (image_leaf_lower(target.imagePath) != image_leaf_lower(entry.imageName)) continue;
          const RelaunchConfig::Liveness answer = ask(target);
          if (answer == RelaunchConfig::Liveness::Running) alive = true;
          if (answer == RelaunchConfig::Liveness::Unknown) unknown = true;
        }
        if (alive) {''',
  '''        const auto evaluate = [&](bool* outAlive, bool* outUnknown) {
          *outAlive = false;
          *outUnknown = false;
          for (const ProcessTarget& target : shared->stopped) {
            if (image_leaf_lower(target.imagePath) != image_leaf_lower(entry.imageName)) continue;
            const RelaunchConfig::Liveness answer = ask(target);
            if (answer == RelaunchConfig::Liveness::Running) *outAlive = true;
            if (answer == RelaunchConfig::Liveness::Unknown) *outUnknown = true;
          }
        };

        bool alive = false;
        bool unknown = false;
        evaluate(&alive, &unknown);

        // Everything here was asked to stop, so "still running" is as likely to mean "closing" as
        // "refused". Asked once, nine milliseconds after the acknowledgement, the two are
        // indistinguishable -- and the field run resolved that ambiguity the wrong way, skipped
        // the relaunch, and left the machine with nothing running once the host finished closing.
        //
        // So a Running answer is re-asked until it settles or the grace runs out. Running out is
        // not an exit: it stays Running and the skip below happens for the reason it names.
        if (alive && shared->config.closingGraceMs > 0) {
          const DWORD deadline = GetTickCount() + shared->config.closingGraceMs;
          while (alive && GetTickCount() < deadline) {
            Sleep(50);
            evaluate(&alive, &unknown);
          }
        }

        if (alive) {'''),
])

edit('apps/native_poc/src/updater_effects.cpp', [
 ('''  deps.relaunchTable = product_images();
  return deps;
}''',
  '''  deps.relaunchTable = product_images();
  // The supervisor is asked to close and then read for liveness almost immediately. Without a
  // grace the two happen close enough together that a process which is closing reads as one that
  // refused -- which is what left a machine with nothing running after an abandoned attempt.
  deps.closingGraceMs = 15000;
  return deps;
}'''),
])

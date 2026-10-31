# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()

anchor = '''  {
    // A target that will not stop must NOT be forced. Quiesce fails and the caller abandons.'''

new = '''  {
    // The field failure of 2026-09-11, as a pair: asking fails because the target is already
    // leaving, and asking fails because the target is there and refusing. Only the first is
    // forgiven, and the difference has to be the process, not the message.
    //
    // Three attempts in a row ended "AbandonedBeforeSwap -- could not ask pid 7332 to stop"
    // about a host that had acknowledged the handoff 70ms earlier and was standing down. The
    // enumeration and the request are two moments; between them the window was destroyed and
    // the request had nothing to reach.
    DummyProcess leaving;
    check("leaving dummy started", leaving.start());
    ProcessTarget gone;
    check("leaving identity captured", capture_process_identity(leaving.pid(), &gone));
    gone.hasWindow = true;  // it had one when it was enumerated
    leaving.kill();
    for (int i = 0; i < 100 && leaving.alive(); ++i) Sleep(10);
    check("the target really is gone before we ask", !leaving.alive());

    UpdateEffectsConfig c = base_config(install, staging);
    c.enumerateTargets = [gone]() { return std::vector<ProcessTarget>{gone}; };
    c.requestStop = [](const ProcessTarget&) { return false; };  // nothing left to ask
    WindowsUpdateEffects e(c);
    check("asking a process that already left is not a failure", e.PrepareForSwap(),
          e.last_error());
    check("and quiesce agrees it is gone", e.Quiesce(), e.last_error());
  }

  {
    // The other half of the same branch: a LIVE target whose stop request fails is still a
    // failure. Without this the forgiveness above would excuse every refusal.
    DummyProcess present;
    check("present dummy started", present.start());
    ProcessTarget alive;
    check("present identity captured", capture_process_identity(present.pid(), &alive));
    alive.hasWindow = true;

    UpdateEffectsConfig c = base_config(install, staging);
    c.enumerateTargets = [alive]() { return std::vector<ProcessTarget>{alive}; };
    c.requestStop = [](const ProcessTarget&) { return false; };
    WindowsUpdateEffects e(c);
    check("a live target that cannot be asked still fails", !e.PrepareForSwap());
    check("and says which pid", e.last_error().find(std::to_string(alive.pid())) !=
                                    std::string::npos, e.last_error());
    present.kill();
  }

  {
    // A target that will not stop must NOT be forced. Quiesce fails and the caller abandons.'''

assert d.count(anchor) == 1, 'anchor'
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(anchor, new, 1))
print('ok')

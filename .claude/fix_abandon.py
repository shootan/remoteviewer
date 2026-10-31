import io

# ---------------------------------------------------------------- a result for it
p = 'apps/native_poc/src/update_state_machine.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  // Stopped before anything on disk was touched. The install is untouched and still works.
  AbandonedBeforeSwap,'''
new = '''  // Stopped before anything on disk was touched. The install is untouched and still works, and
  // whatever left on our account has been started again.
  AbandonedBeforeSwap,
  /**
   * Nothing on disk was touched, and something that left is not back.
   *
   * The worst-looking outcome for its cause. Nothing went wrong with the update -- no file was
   * replaced, no registration changed -- and yet the machine may be unreachable, because the
   * product was asked to make way and then nobody asked it back. Harder to diagnose than a failed
   * update precisely because there is no damage to find.
   */
  AbandonedNotRelaunched,'''
assert old in d, 'result'
d = d.replace(old, new, 1)

old = '''UpdateOutcome run_update(UpdateEffects& effects,'''
new = '''/**
 * NOTE on the order of Quiesce and readiness.
 *
 * A caller may be waiting to exit on this update's behalf, and it is released as soon as the
 * download verifies -- before Quiesce enumerates anything. That means a process which left
 * BECAUSE of that release is already gone by the time targets are enumerated, so it cannot appear
 * in the list of things to bring back. Implementations that signal readiness must therefore
 * capture the target identities BEFORE they signal, not when Quiesce asks.
 *
 * The state machine cannot enforce that -- it does not know a signal exists -- so it is stated
 * here and the updater's decorator does it.
 */
UpdateOutcome run_update(UpdateEffects& effects,'''
assert old in d, 'decl'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ---------------------------------------------------------------- the abandon paths restore
p = 'apps/native_poc/src/update_state_machine.cpp'
d = io.open(p, encoding='utf-8').read()

d = d.replace('''    case UpdateResult::AbandonedBeforeSwap: return "AbandonedBeforeSwap";''',
              '''    case UpdateResult::AbandonedBeforeSwap: return "AbandonedBeforeSwap";
    case UpdateResult::AbandonedNotRelaunched: return "AbandonedNotRelaunched";''', 1)

old = '''  // Everything past this point either rolls back or is abandoned cleanly, so the rollback path
  // is written once here and reused.'''
new = '''  /**
   * Gives up without having changed anything, and puts back whatever left on our account.
   *
   * The second half is the part that was missing. A caller may already have exited because this
   * update told it the download was verified, and abandoning after that point left a machine with
   * every file intact and nothing running -- which for a remote user is an unreachable machine,
   * arrived at without a single thing going wrong. Nothing to repair, nothing to find.
   *
   * Relaunch is safe on the paths where nothing left: no targets were captured, so the plan is
   * empty and it does nothing. Attempted once, never in a loop.
   */
  const auto abandon = [&](const char* why) {
    out.detail = why;
    if (effects.Relaunch()) {
      out.result = UpdateResult::AbandonedBeforeSwap;
    } else {
      out.result = UpdateResult::AbandonedNotRelaunched;
    }
    return out;
  };

  // Everything past this point either rolls back or is abandoned cleanly, so the rollback path
  // is written once here and reused.'''
assert old in d, 'abandon lambda site'
d = d.replace(old, new, 1)

old = '''  enter(UpdateState::Download);
  if (!effects.Download(fields)) {
    effects.DiscardDownload();  // a partial file must not survive to be picked up later
    out.result = UpdateResult::AbandonedBeforeSwap;
    out.detail = "download did not complete";
    return out;
  }

  enter(UpdateState::Verify);
  if (!effects.VerifyDownload(fields)) {
    effects.DiscardDownload();
    out.result = UpdateResult::AbandonedBeforeSwap;
    out.detail = "downloaded artifact did not match the manifest";
    return out;
  }

  enter(UpdateState::Prepare);
  if (!effects.PrepareForSwap()) {
    // Deliberately does NOT escalate to a forced shutdown. The product is in the middle of doing
    // something, and an update is worth less than whatever that is. The verified download is kept
    // so the next attempt starts here rather than at the top.
    out.result = UpdateResult::AbandonedBeforeSwap;
    out.detail = "product did not reach a safe point";
    return out;
  }

  enter(UpdateState::Quiesce);
  if (!effects.Quiesce()) {
    // Same rule, same reason. Nothing on disk has been touched yet, so backing out costs nothing.
    out.result = UpdateResult::AbandonedBeforeSwap;
    out.detail = "product did not shut down cleanly";
    return out;
  }'''
new = '''  enter(UpdateState::Download);
  if (!effects.Download(fields)) {
    effects.DiscardDownload();  // a partial file must not survive to be picked up later
    return abandon("download did not complete");
  }

  enter(UpdateState::Verify);
  if (!effects.VerifyDownload(fields)) {
    effects.DiscardDownload();
    return abandon("downloaded artifact did not match the manifest");
  }

  enter(UpdateState::Prepare);
  if (!effects.PrepareForSwap()) {
    // Deliberately does NOT escalate to a forced shutdown. The product is in the middle of doing
    // something, and an update is worth less than whatever that is. The verified download is kept
    // so the next attempt starts here rather than at the top.
    //
    // Past the point where a waiting caller has been released, so this owes a restoration.
    return abandon("product did not reach a safe point");
  }

  enter(UpdateState::Quiesce);
  if (!effects.Quiesce()) {
    // Same rule, same reason. Nothing on disk has been touched yet, so backing out costs nothing
    // -- except that some of the product may already have stopped, and putting it back is this
    // path's job.
    return abandon("product did not shut down cleanly");
  }'''
assert old in d, 'abandon sites'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('abandon paths restore what left')

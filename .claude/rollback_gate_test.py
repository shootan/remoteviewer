import io
p='apps/native_poc/src/update_effects_test.cpp'
d=io.open(p,encoding='utf-8').read()

old='''  {
    // A first install: nothing there to move aside. Must not be mistaken for a failure.'''
new='''  {
    // A rollback that is REFUSED because something this attempt started could not be stopped.
    //
    // Restoring files while a process may still be holding them does not undo the update -- it
    // produces an installation that is part old and part new, and the failure afterwards reads as
    // the rollback's fault rather than as the reason it should never have started. So the gate is
    // asked first, and a no means nothing moves at all.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    c.releaseBeforeRollback = []() { return false; };  // "I could not prove it stopped"
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("refused rollback: the swap itself succeeds", e.Swap(), e.last_error());
    check("a rollback that cannot stop what it started does not report success", !e.Rollback(),
          e.last_error());
    // The point of the whole case: not one file moved. Refusing has to leave the installation
    // exactly as the swap left it, so that a later attempt still has both halves to work from.
    check("refused rollback: the new bytes are still in place",
          read_text(install + L"\\AlphaPayload.bin") == kArtifactBytes,
          read_text(install + L"\\AlphaPayload.bin"));
    check("refused rollback: the backups are still there, so a way back still exists",
          exists(install + L"\\AlphaPayload.bin.gnlink-old"));
    check("refused rollback: the reason says so", e.last_error().find("not rolling back") == 0,
          e.last_error());
    e.DiscardDownload();
  }
  {
    // The counter-control. Same swap, same rollback, and the gate says yes -- so the restore runs.
    // Without this the case above would also pass if Rollback() simply never worked.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    bool asked = false;
    c.releaseBeforeRollback = [&asked]() { asked = true; return true; };
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("permitted rollback: swap succeeds", e.Swap(), e.last_error());
    check("a rollback whose stops all succeeded goes ahead", e.Rollback(), e.last_error());
    check("permitted rollback: the gate was consulted", asked);
    check("permitted rollback: the old bytes are back",
          read_text(install + L"\\AlphaPayload.bin") == kOldAlpha,
          read_text(install + L"\\AlphaPayload.bin"));
    e.DiscardDownload();
  }
  {
    // Commit reading what DeleteFileW told it.
    //
    // A backup held open cannot be deleted. That used to be discarded -- the result was ignored
    // and the list cleared -- so the file stayed beside the installation with nothing anywhere
    // recording that it had. It is the mechanism behind a stale .gnlink-old that no reading of the
    // code explained, and it is reproduced here rather than deferred to a machine.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("orphan case: swap succeeds", e.Swap(), e.last_error());
    const std::wstring backup = install + L"\\AlphaPayload.bin.gnlink-old";
    check("orphan case: the backup exists to be held", exists(backup));
    HANDLE hold = CreateFileW(backup.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    check("orphan case: the test could hold the backup open", hold != INVALID_HANDLE_VALUE);
    e.Commit();
    check("a backup that could not be deleted is recorded, not forgotten",
          e.orphaned_backups().size() == 1,
          std::to_string(e.orphaned_backups().size()));
    check("...and it names which one", !e.orphaned_backups().empty() &&
          e.orphaned_backups()[0] == L"AlphaPayload.bin");
    check("...and it is visible in the error text",
          e.last_error().find("backups left behind") != std::string::npos, e.last_error());
    // And the file really did survive -- the recording describes something true.
    check("...and the backup really is still on disk", exists(backup));
    if (hold != INVALID_HANDLE_VALUE) CloseHandle(hold);
    DeleteFileW(backup.c_str());
    e.DiscardDownload();
  }
  {
    // Counter-control: an ordinary commit leaves nothing behind and records nothing. Without it,
    // "orphans are recorded" would also pass if every commit reported every backup.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("clean commit case: swap succeeds", e.Swap(), e.last_error());
    e.Commit();
    check("a commit that deletes everything records no orphans", e.orphaned_backups().empty(),
          std::to_string(e.orphaned_backups().size()));
    check("...and the backups really are gone",
          !exists(install + L"\\AlphaPayload.bin.gnlink-old"));
    e.DiscardDownload();
  }

  {
    // A first install: nothing there to move aside. Must not be mistaken for a failure.'''
assert old in d,'first install anchor'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('rollback gate + orphan recording, each with a counter-control')

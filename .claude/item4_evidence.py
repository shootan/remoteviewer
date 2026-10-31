import io

BS = chr(92)

# ------------------------------------------------------------------ the cap says something when it runs out
p = 'apps/native_poc/src/update_effects.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''    if (!DeleteFileW(backup.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
      for (int n = 1; n <= 50; ++n) {
        const std::wstring aside = backup + L"." + std::to_wstring(n);
        if (GetFileAttributesW(aside.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        if (MoveFileExW(backup.c_str(), aside.c_str(), 0)) {
          orphanedBackups_.push_back(name);
          lastError_ = "an older backup of " + to_utf8(name) +
                       " could not be deleted and was moved to " + to_utf8(aside);
        }
        break;
      }
    }'''
new = '''    if (!DeleteFileW(backup.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
      // Bounded on purpose. Every one of these is a file nobody could delete, so an unbounded
      // count would quietly turn a recurring failure into an accumulating pile -- the failure
      // should become visible, not become litter.
      bool moved = false;
      for (int n = 1; n <= kMaxStaleBackups; ++n) {
        const std::wstring aside = backup + L"." + std::to_wstring(n);
        if (GetFileAttributesW(aside.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        if (MoveFileExW(backup.c_str(), aside.c_str(), 0)) {
          moved = true;
          orphanedBackups_.push_back(name);
          lastError_ = "an older backup of " + to_utf8(name) +
                       " could not be deleted and was moved to " + to_utf8(aside);
        }
        break;
      }
      if (!moved) {
        // The swap below will fail on the taken name, and it would report only that the file
        // would not move. That is the symptom; this is the cause, and without it an operator
        // reads "could not move aside" and looks at the wrong file.
        lastError_ = "an older backup of " + to_utf8(name) +
                     " cannot be deleted and there is nowhere left to put it (" +
                     std::to_string(kMaxStaleBackups) + " already set aside)";
      }
    }'''
assert old in d, 'stale backup loop'
d = d.replace(old, new, 1)

old = '''bool WindowsUpdateEffects::Swap() {'''
new = '''/**
 * How many undeletable backups may be set aside before a swap gives up.
 *
 * Each one is a file that could not be removed, so letting them accumulate without limit would
 * hide a repeating failure behind a growing directory. Reaching the limit is reported as its own
 * cause rather than as the move-aside failure it produces.
 */
constexpr int kMaxStaleBackups = 50;

bool WindowsUpdateEffects::Swap() {'''
assert old in d, 'Swap def'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ------------------------------------------------------------------ the evidence
p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''  {
    // A first install: nothing there to move aside. Must not be mistaken for a failure.'''
new = '''  {
    // The NEXT update, with a backup nobody could delete still sitting on the name.
    //
    // This is what makes the recording worth having. A stuck `.gnlink-old` used to block the
    // move-aside outright, so one undeletable file meant no further update could be installed at
    // all -- the litter was not the damage, being unable to ship a fix was.
    seed_install();
    UpdateEffectsConfig first = base_config(install, staging);
    WindowsUpdateEffects a(first);
    const ManifestFields f = artifact_fields();
    a.Download(f);
    check("next-attempt case: the first swap succeeds", a.Swap(), a.last_error());
    const std::wstring backup = install + L"\\\\AlphaPayload.bin.gnlink-old";
    HANDLE hold = CreateFileW(backup.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    check("next-attempt case: the backup is held open", hold != INVALID_HANDLE_VALUE);
    a.Commit();
    check("next-attempt case: the commit could not delete it", !a.orphaned_backups().empty(),
          a.last_error());
    a.DiscardDownload();

    // A second update arrives while that file is still stuck.
    UpdateEffectsConfig second = base_config(install, staging);
    WindowsUpdateEffects b(second);
    b.Download(f);
    check("a later update is not blocked by a backup that could not be deleted", b.Swap(),
          b.last_error());
    check("...and it says where the stuck one went",
          b.last_error().find("was moved to") != std::string::npos, b.last_error());
    check("...and the name it needed is free again, holding this attempt's backup",
          exists(backup));
    check("...and the stuck one is still there under its new name, not deleted",
          exists(backup + L".1"));
    // The rollback after all that must still produce the ORIGINAL bytes, not the ones it happened
    // to find lying around under a similar name.
    check("...and a rollback after it restores the original bytes exactly", b.Rollback(),
          b.last_error());
    check("...bytes match the seeded original",
          read_text(install + L"\\\\AlphaPayload.bin") == kOldAlpha,
          read_text(install + L"\\\\AlphaPayload.bin"));
    if (hold != INVALID_HANDLE_VALUE) CloseHandle(hold);
    DeleteFileW((backup + L".1").c_str());
    DeleteFileW(backup.c_str());
    b.DiscardDownload();
  }
  {
    // And the limit. Fifty stuck backups is a repeating failure, not a directory to keep growing,
    // so the swap stops -- and says which of the two problems it hit.
    seed_install();
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    const std::wstring backup = install + L"\\\\AlphaPayload.bin.gnlink-old";
    write_text(backup, "stuck");
    HANDLE hold = CreateFileW(backup.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    check("cap case: the backup name is taken and held", hold != INVALID_HANDLE_VALUE);
    for (int n = 1; n <= 50; ++n) {
      write_text(backup + L"." + std::to_wstring(n), "older");
    }
    check("a swap with nowhere left to put the stuck backup fails", !e.Swap(), e.last_error());
    check("...and blames the right thing -- nowhere to put it, not a file that would not move",
          e.last_error().find("nowhere left to put it") != std::string::npos, e.last_error());
    // Nothing half-done: the payload that was never touched is still the old version.
    check("...and nothing was left replaced", read_text(install + L"\\\\BetaPayload.bin") == kOldBeta,
          read_text(install + L"\\\\BetaPayload.bin"));
    if (hold != INVALID_HANDLE_VALUE) CloseHandle(hold);
    for (int n = 1; n <= 50; ++n) DeleteFileW((backup + L"." + std::to_wstring(n)).c_str());
    DeleteFileW(backup.c_str());
    e.DiscardDownload();
  }

  {
    // A first install: nothing there to move aside. Must not be mistaken for a failure.'''
assert old in d, 'first install anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('next attempt, the cap, and the restored bytes')

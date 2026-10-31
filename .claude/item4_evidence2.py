import io

p = 'apps/native_poc/src/update_effects_test.cpp'
d = io.open(p, encoding='utf-8').read()

start = d.find('''  {
    // The NEXT update, with a backup nobody could delete still sitting on the name.''')
end = d.find('''  {
    // A first install: nothing there to move aside. Must not be mistaken for a failure.''')
assert start != -1 and end > start, 'blocks to replace'

new = '''  {
    // The NEXT update, with a backup nobody can delete still sitting on the name.
    //
    // This is what makes the recording worth having. A stuck `.gnlink-old` used to block the
    // move-aside outright, so one undeletable file meant no further update could be installed --
    // the litter was never the damage; being unable to ship a fix was.
    //
    // Stuck the way it is really stuck: the backup is a RUNNING IMAGE. Windows refuses to delete
    // one and permits renaming it, and that asymmetry is the whole reason this works. A copy of
    // the command interpreter under the payload's name is a valid PE and runs regardless of the
    // extension.
    seed_install();
    const std::wstring backup = install + L"\\\\AlphaPayload.bin.gnlink-old";
    wchar_t comspec[MAX_PATH]{};
    const bool haveShell = GetEnvironmentVariableW(L"COMSPEC", comspec, MAX_PATH) > 0;
    const bool copied = haveShell && CopyFileW(comspec, backup.c_str(), FALSE) != FALSE;
    check("next-attempt case: a stale backup exists", copied);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\\"" + backup + L"\\"";
    const BOOL running = copied && CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                                                  CREATE_NO_WINDOW, nullptr, install.c_str(), &si,
                                                  &pi);
    check("next-attempt case: and something is running it, so it cannot be deleted",
          running != FALSE);
    check("next-attempt case: deleting it really does fail",
          DeleteFileW(backup.c_str()) == FALSE && GetLastError() == ERROR_ACCESS_DENIED,
          std::to_string(GetLastError()));

    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("a later update is not blocked by a backup that could not be deleted", e.Swap(),
          e.last_error());
    check("...and it says where the stuck one went",
          e.last_error().find("was moved to") != std::string::npos, e.last_error());
    check("...and it was recorded, not just moved", !e.orphaned_backups().empty());
    check("...and the name it needed is free again, holding this attempt's backup",
          exists(backup));
    check("...and the stuck one is still there under its new name, not deleted",
          exists(backup + L".1"));
    // The rollback that follows must produce the ORIGINAL bytes -- not the ones it happened to
    // find lying around under a similar name.
    check("...and a rollback after it restores the original bytes exactly", e.Rollback(),
          e.last_error());
    check("...bytes match the seeded original",
          read_text(install + L"\\\\AlphaPayload.bin") == kOldAlpha,
          read_text(install + L"\\\\AlphaPayload.bin"));

    if (running) {
      TerminateProcess(pi.hProcess, 0);
      WaitForSingleObject(pi.hProcess, 3000);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    for (int attempt = 0; attempt < 40; ++attempt) {
      if (DeleteFileW((backup + L".1").c_str())) break;
      Sleep(50);
    }
    DeleteFileW(backup.c_str());
    e.DiscardDownload();
  }
  {
    // The limit. Fifty set aside is a repeating failure, not a directory to keep growing, so the
    // swap stops -- and names which of the two problems it hit, because "could not move aside" on
    // its own sends an operator to look at the wrong file.
    seed_install();
    const std::wstring backup = install + L"\\\\AlphaPayload.bin.gnlink-old";
    wchar_t comspec[MAX_PATH]{};
    const bool haveShell = GetEnvironmentVariableW(L"COMSPEC", comspec, MAX_PATH) > 0;
    const bool copied = haveShell && CopyFileW(comspec, backup.c_str(), FALSE) != FALSE;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\\"" + backup + L"\\"";
    const BOOL running = copied && CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                                                  CREATE_NO_WINDOW, nullptr, install.c_str(), &si,
                                                  &pi);
    check("cap case: an undeletable backup is in the way", running != FALSE);
    for (int n = 1; n <= 50; ++n) write_text(backup + L"." + std::to_wstring(n), "older");

    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("a swap with nowhere left to put the stuck backup fails", !e.Swap(), e.last_error());
    check("...and nothing was left replaced",
          read_text(install + L"\\\\BetaPayload.bin") == kOldBeta,
          read_text(install + L"\\\\BetaPayload.bin"));

    if (running) {
      TerminateProcess(pi.hProcess, 0);
      WaitForSingleObject(pi.hProcess, 3000);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    for (int n = 1; n <= 50; ++n) DeleteFileW((backup + L"." + std::to_wstring(n)).c_str());
    for (int attempt = 0; attempt < 40; ++attempt) {
      if (DeleteFileW(backup.c_str())) break;
      Sleep(50);
    }
    e.DiscardDownload();
  }
  {
    // The OTHER kind of stuck, and the limit of the rename trick -- stated rather than left to be
    // discovered. A backup held open with no sharing cannot be deleted AND cannot be renamed, so
    // there is no way past it; the swap fails and nothing is half-done. Renaming rescues a
    // running image, which is the common case, and it does not rescue this one.
    seed_install();
    const std::wstring backup = install + L"\\\\AlphaPayload.bin.gnlink-old";
    write_text(backup, "stuck");
    HANDLE hold = CreateFileW(backup.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    check("exclusive-hold case: the backup is held with no sharing",
          hold != INVALID_HANDLE_VALUE);
    UpdateEffectsConfig c = base_config(install, staging);
    WindowsUpdateEffects e(c);
    const ManifestFields f = artifact_fields();
    e.Download(f);
    check("a backup that cannot even be renamed stops the swap", !e.Swap(), e.last_error());
    check("...and nothing was left replaced",
          read_text(install + L"\\\\BetaPayload.bin") == kOldBeta,
          read_text(install + L"\\\\BetaPayload.bin"));
    check("...and the payload it could not move is unchanged",
          read_text(install + L"\\\\AlphaPayload.bin") == kOldAlpha,
          read_text(install + L"\\\\AlphaPayload.bin"));
    if (hold != INVALID_HANDLE_VALUE) CloseHandle(hold);
    DeleteFileW(backup.c_str());
    e.DiscardDownload();
  }

'''
d = d[:start] + new + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('stuck the way it is really stuck, plus the cap and the limit of renaming')

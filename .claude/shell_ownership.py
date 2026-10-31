import io
p='apps/native_poc/src/update_relaunch.cpp'
d=io.open(p,encoding='utf-8').read()

old='''        const std::vector<DWORD> before = pids_running_image(exePath);
        outcome.started = launch_via_shell(exePath, L"");
        if (outcome.started) {
          // Briefly: the shell hands off asynchronously, so the process may not exist yet.
          for (int attempt = 0; attempt < 20 && outcome.startedPid == 0; ++attempt) {
            for (DWORD pid : pids_running_image(exePath)) {
              if (std::find(before.begin(), before.end(), pid) == before.end()) {
                outcome.startedPid = pid;
                break;
              }
            }
            if (outcome.startedPid == 0) Sleep(50);
          }
        }'''
new='''        const std::vector<DWORD> before = pids_running_image(exePath);
        outcome.started = launch_via_shell(exePath, L"");
        if (outcome.started) {
          // Briefly: the shell hands off asynchronously, so the process may not exist yet.
          std::vector<DWORD> appeared;
          for (int attempt = 0; attempt < 20 && appeared.empty(); ++attempt) {
            for (DWORD pid : pids_running_image(exePath)) {
              if (std::find(before.begin(), before.end(), pid) == before.end()) {
                appeared.push_back(pid);
              }
            }
            if (appeared.empty()) Sleep(50);
          }
          // ONE new process running exactly this image, appearing between the snapshot and now.
          // That is per-launch correlation, and it is the only basis on which this claims to own
          // something the shell created. Two would mean the user started one at the same moment,
          // and a rollback that terminated on a guess would be terminating a stranger's window --
          // so ambiguity is left unowned, which later reads as "could not be stopped" and stops
          // the rollback rather than proceeding on an assumption.
          if (appeared.size() == 1) {
            outcome.startedPid = appeared[0];
            HANDLE h = OpenProcess(
                SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                appeared[0]);
            if (h) {
              // Checked again through the handle. Between the snapshot and the open the pid could
              // have been reused, and from here on the handle -- not the number -- is the
              // identity, so this is the last moment the check can be made at all.
              wchar_t image[MAX_PATH]{};
              DWORD size = MAX_PATH;
              if (QueryFullProcessImageNameW(h, 0, image, &size) &&
                  _wcsicmp(image, exePath.c_str()) == 0) {
                shared->ownedHandles.push_back({entry.imageName, h});
              } else {
                CloseHandle(h);
              }
            }
          } else if (appeared.size() > 1) {
            outcome.detail = "started, but more than one appeared -- not claimed as ours";
          }
        }'''
assert old in d,'shell correlation'
d=d.replace(old,new,1)

old='''        // Not started, and deliberately not retried as a child of this process. Starting it here
        // would give it an administrator token it never had. That is a safe failure, not a
        // success, so it is recorded as a failure and the user is told.
        outcome.detail = outcome.started
                             ? "started in the user context"
                             : "could not be started in the user context, and was NOT started "
                               "elevated instead";'''
new='''        // Not started, and deliberately not retried as a child of this process. Starting it here
        // would give it an administrator token it never had. That is a safe failure, not a
        // success, so it is recorded as a failure and the user is told.
        if (outcome.detail.empty()) {
          outcome.detail = outcome.started
                               ? "started in the user context"
                               : "could not be started in the user context, and was NOT started "
                                 "elevated instead";
        }'''
assert old in d,'shell detail'
d=d.replace(old,new,1)

old='''    // Anything started through the shell. We have no handle for it, because the shell created it;
    // a pid found by comparing snapshots is a good guess and a guess is not ownership. Killing on
    // that basis can kill a stranger, so it is reported instead.
    for (const RelaunchOutcome& outcome : shared->outcomes) {
      if (outcome.kind != RelaunchKind::UserProcess || !outcome.started) continue;
      bool owned = false;
      for (const auto& pair : shared->ownedHandles) {
        if (pair.first == outcome.imageName) owned = true;
      }
      if (!owned) report.unstoppable.push_back(outcome.imageName);
    }'''
new='''    // Anything started through the shell that could NOT be tied to one process. The shell creates
    // it, so there is no handle to inherit; a handle is only claimed when exactly one process
    // running exactly that image appeared between the before-snapshot and the after-snapshot. When
    // that correlation did not hold -- nothing appeared, or several did -- there is no identity to
    // act on, and killing by name would mean killing whatever else answers to it.
    for (const RelaunchOutcome& outcome : shared->outcomes) {
      if (outcome.kind != RelaunchKind::UserProcess || !outcome.started) continue;
      bool owned = false;
      for (const auto& pair : shared->ownedHandles) {
        if (pair.first == outcome.imageName) owned = true;
      }
      if (!owned) report.unstoppable.push_back(outcome.imageName);
    }'''
assert old in d,'unstoppable comment'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('shell launches are owned when one launch maps to one process')

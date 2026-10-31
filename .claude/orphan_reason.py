import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()

old='''    /** Backups the commit could not delete. The unexplained .gnlink-old, now named. */
    std::vector<std::wstring> orphaned;'''
new='''    /** Backups the commit could not delete. The unexplained .gnlink-old, now named. */
    std::vector<std::wstring> orphaned;
    /** Image leaves of everything still running out of the install directory when the run ended. */
    std::vector<std::wstring> runningLeaves;'''
assert old in d,'result orphan field'
d=d.replace(old,new,1)

old='''    for (const auto& pair : running_under(install)) {
      if (pair.first.find(kHostName) != std::wstring::npos) ++r.hostInstances;
      if (pair.first.find(kClientName) != std::wstring::npos) ++r.clientInstances;
    }'''
new='''    for (const auto& pair : running_under(install)) {
      r.runningLeaves.push_back(pair.first);
      if (pair.first.find(kHostName) != std::wstring::npos) ++r.hostInstances;
      if (pair.first.find(kClientName) != std::wstring::npos) ++r.clientInstances;
    }'''
assert old in d,'instance count'
d=d.replace(old,new,1)

old='''      // And the other direction: nothing is reported as left behind that is not actually there.
      // Without this the two checks above would also pass if every commit named everything.
      check("nothing is reported as left behind unless it really is",
            (!named(kHostName) || r.hostBackupLeft) &&
                (!named(kClientName) || r.clientBackupLeft),
            detail);'''
new='''      // And the other direction: nothing is reported as left behind that is not actually there.
      // Without this the two checks above would also pass if every commit named everything.
      check("nothing is reported as left behind unless it really is",
            (!named(kHostName) || r.hostBackupLeft) &&
                (!named(kClientName) || r.clientBackupLeft),
            detail);

      // And WHY. A file cannot be deleted while a process is running it -- renaming one is
      // allowed, deleting one is not -- so a backup that survives means something is still
      // running out of it under its new name. This turns the recording from "it happened" into a
      // reason, and it is the answer to the client backup that had none: the client this
      // scenario started before the update never left, so the swap renamed its image out from
      // under it and it kept running from the renamed file.
      for (const std::wstring& name : r.orphaned) {
        const std::wstring image = name + L".gnlink-old";
        bool held = false;
        for (const std::wstring& leaf : r.runningLeaves) {
          if (leaf == image) held = true;
        }
        std::string running;
        for (const std::wstring& leaf : r.runningLeaves) running += narrow(leaf) + " ";
        check("a backup that survived is one something is still running from", held,
              narrow(image) + " -- running: " + running);
      }'''
assert old in d,'accounting block'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('the orphan now has a reason attached')

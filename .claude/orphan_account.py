import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''      check("production wires every injected seam", r.unwired.empty(), names);
    }
    return r;'''
new='''      check("production wires every injected seam", r.unwired.empty(), names);
    }

    // A backup that outlives a committed update is either deleted or named. It used to be
    // neither: the delete result was thrown away and the list cleared, so a surviving
    // .gnlink-old was indistinguishable from one that had been removed -- which is why a client
    // backup kept turning up here with no explanation available anywhere in the code.
    const bool committed = r.outcome.result == UpdateResult::Updated ||
                           r.outcome.result == UpdateResult::UpdatedButNotRelaunched;
    if (committed) {
      const auto named = [&r](const std::wstring& image) {
        for (const std::wstring& name : r.orphaned) {
          if (name == image) return true;
        }
        return false;
      };
      std::string detail;
      for (const std::wstring& name : r.orphaned) detail += narrow(name) + " ";
      check("a surviving host backup is named, not silent",
            !r.hostBackupLeft || named(kHostName), detail);
      check("a surviving client backup is named, not silent",
            !r.clientBackupLeft || named(kClientName), detail);
      // And the other direction: nothing is reported as left behind that is not actually there.
      // Without this the two checks above would also pass if every commit named everything.
      check("nothing is reported as left behind unless it really is",
            (!named(kHostName) || r.hostBackupLeft) &&
                (!named(kClientName) || r.clientBackupLeft),
            detail);
    }
    return r;'''
assert old in d,'runner tail'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('surviving backups must be named')

import io

p = 'apps/native_poc/src/updater_scenarios_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''      // Identified from the SNAPSHOT, with no OpenProcess.
      //
      // This used to open each process to read its full image path, and skip any it could not
      // open -- which turned out to be exactly the processes it existed to stop. They were listed
      // in the snapshot by name the whole time; the sweep simply could not see them, reported
      // nothing running, and left them holding their images. The next scenario then could not
      // write its starting bytes, and failed an assertion about bytes with no process visible
      // anywhere to explain it.
      //
      // A prefix match on the name, because a swap renames what it replaces to
      // `<name>.gnlink-old` and a process running that file keeps running under the new name.
      // These two names appear nowhere else on a machine, which is what makes a name enough.
      const std::wstring leaf = entry.szExeFile;'''
new = '''      // THE RULE: the name narrows the candidates; the PATH decides. A process whose path cannot
      // be read is reported and left running -- never stopped.
      //
      // Two true things meet at this line and it is easy to take the wrong one for the rule.
      //
      // The first: a sweep that needed a handle to see a process was blind to exactly the
      // processes it existed to stop. They sat in the snapshot by name the whole time, so it
      // reported nothing running and left them holding their images, and the next scenario could
      // not write its starting bytes.
      //
      // The second, and the one that governs here: seeing something by name is not owning it.
      // Reading "the name is enough" out of the first fact is how this came to terminate
      // processes it could not identify -- and "nothing else on this machine is called
      // ScnHost.exe" is a guess about the machine, not evidence about a process.
      //
      // So both hold. The name gets a process LOOKED at; only a path under `dir` gets it stopped.
      // If the loop below ever loses its OpenProcess again, this stops being a scope and starts
      // being a list of everything that shares a name -- which is what it was.
      //
      // The match is a prefix, because a swap renames what it replaces to `<name>.gnlink-old` and
      // a process running that file keeps running under the new name.
      const std::wstring leaf = entry.szExeFile;'''
assert old in d, 'stale comment'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('the comment states the rule that is actually in force')

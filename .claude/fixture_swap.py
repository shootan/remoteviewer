import io

BSS = 'L"' + chr(92) + chr(92) + '"'
p = 'apps/native_poc/src/updater_scenarios_test.cpp'
d = io.open(p, encoding='utf-8').read()

# ------------------------------------------------------------------ fixtures come from the dummy
old = '''  const std::string oldBody = read_file(comspec);'''
new = '''  // The product stand-in is remote60_scn_dummy: a real PE that RECORDS that it ran and exits.
  //
  // Copies of the command interpreter were used here, and they do not exit -- every scenario left
  // one running, which is why this suite needed a sweep that hunted processes down by name. And
  // nothing could tell whether a shell-routed launch had actually happened, so the suite removed
  // its directory while a request was still in flight and the user got a "Windows cannot find
  // ...\\\\ScnClient.exe" dialog on their desktop. A fixture that leaves a witness turns that from
  // a guess into something this can wait for.
  const std::wstring dummySource = GNLINK_SCN_DUMMY;
  const std::string oldBody = read_file(dummySource);
  if (oldBody.empty()) {
    std::cout << "RESULT: FAILED  (the fixture executable was not built: " << narrow(dummySource)
              << ")\\n";
    return 1;
  }
  /** Where a launched fixture records itself. Cleared per scenario by seed(). */
  const std::wstring witnessPath = install + ''' + BSS + ''' L"witness.txt";
  const auto witness_text = [&witnessPath]() { return read_file(witnessPath); };
  const auto witness_count = [&witness_text](const std::wstring& name) {
    const std::string text = witness_text();
    const std::string needle = narrow(name);
    int n = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
      ++n;
    }
    return n;
  };
  /**
   * Waits until `name` has recorded itself, or the wait runs out.
   *
   * This is the replacement for "check the file is there before asking the shell to run it". That
   * check answered a question about the past; a shell launch happens later, in another process,
   * and the only honest way to know it happened is that the thing it started said so.
   */
  const auto wait_for_witness = [&witness_count](const std::wstring& name, int timeoutMs) {
    for (int waited = 0; waited < timeoutMs; waited += 100) {
      if (witness_count(name) > 0) return true;
      Sleep(100);
    }
    return witness_count(name) > 0;
  };'''
assert old in d, 'oldBody'
d = d.replace(old, new, 1)

# COMSPEC is still needed, but only for the one fixture that must NOT exit
old = '''  wchar_t comspec[MAX_PATH]{};
  if (GetEnvironmentVariableW(L"COMSPEC", comspec, MAX_PATH) == 0) {'''
new = '''  // Still needed for exactly one thing: a process that refuses to exit, for the quiesce timeout.
  // It is started directly by this test with CreateProcessW and stopped through the handle it was
  // given -- it never goes near the shell, and nothing else here uses it.
  wchar_t comspec[MAX_PATH]{};
  if (GetEnvironmentVariableW(L"COMSPEC", comspec, MAX_PATH) == 0) {'''
assert old in d, 'comspec decl'
d = d.replace(old, new, 1)

# ------------------------------------------------------------------ seed clears the witness
old = '''    DeleteFileW(log.c_str());
  };'''
new = '''    DeleteFileW(log.c_str());
    DeleteFileW(witnessPath.c_str());
  };'''
assert old in d, 'seed tail'
d = d.replace(old, new, 1)

# ------------------------------------------------------------------ scenario 9 reads the witness
old = '''    check("9 client-only: the client came back", r.clientInstances >= 1,
          std::to_string(r.clientInstances) + " -- " + r.log);'''
new = '''    // The witness, not a process list. The fixture exits as soon as it has recorded itself, so
    // "is it running now" is the wrong question -- and it was the question that made this suite
    // depend on processes staying alive, which is what left them lying around.
    check("9 client-only: the client actually ran", wait_for_witness(kClientName, 10000),
          witness_text() + " -- " + r.log);'''
assert old in d, 'scenario 9 client'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('fixtures record themselves and exit')

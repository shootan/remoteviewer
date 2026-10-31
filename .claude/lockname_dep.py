import io
BS=chr(92)

p='apps/native_poc/src/updater_effects.hpp'
d=io.open(p,encoding='utf-8').read()
old='''  std::vector<std::wstring> payloadNames;'''
new='''  /**
   * The mutual-exclusion name (design 3.3). A dependency, not a constant.
   *
   * It was hardcoded to the production name, which meant every test that drove this assembly
   * competed with the INSTALLED product for the same machine-wide mutex. That is not theoretical:
   * a run here failed with "another update or installer holds the lock" because something else on
   * the machine had it -- and the same collision in the other direction would have a test block a
   * real update from starting.
   *
   * Production supplies `Global{}GNLinkUpdate`; a test supplies something of its own.
   */
  std::wstring lockName;

  std::vector<std::wstring> payloadNames;'''.replace('{}', BS*2)
assert old in d,'payloadNames'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)

p='apps/native_poc/src/updater_effects.cpp'
d=io.open(p,encoding='utf-8').read()
old='  config.lockName = L"Global'+BS*2+'GNLinkUpdate";'
assert old in d,'lock assign'
d=d.replace(old,'  config.lockName = deps_.lockName;',1)
old='  deps.payloadNames = product_image_names();'
assert old in d,'payload deps'
d=d.replace(old,'''  // The machine-wide name, because one machine has one installation and two updaters replacing
  // the same directory is the thing this exists to prevent.
  deps.lockName = L"Global'''+BS*2+'''GNLinkUpdate";
  deps.payloadNames = product_image_names();''',1)
old='  if (payloadNames.empty()) return fail("payloadNames not set");'
assert old in d,'validate'
d=d.replace(old,'''  if (lockName.empty()) return fail("lockName not set");
'''+old,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)

p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='    deps.payloadNames = {kHostName, kClientName};'
assert old in d,'scen deps'
new='''    // This suite's OWN lock, in the session namespace. It used to inherit the production name and
    // therefore contend with the installed product on this machine -- a run failed on exactly
    // that, and the reverse collision would have a test hold off a real update.
    {
      wchar_t own[128]{};
      swprintf(own, 128, L"Local'''+BS*2+'''GNLinkScenarioTest-%lu", GetCurrentProcessId());
      deps.lockName = own;
    }
    deps.payloadNames = {kHostName, kClientName};'''
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('the lock is a dependency now')

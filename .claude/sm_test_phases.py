import io

p = 'apps/native_poc/src/update_state_machine_test.cpp'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- the fake, per phase
old = '''  RelaunchVerdict relaunchVerdict = RelaunchVerdict::AllBack;'''
new = '''  RelaunchVerdict requiredVerdict = RelaunchVerdict::AllBack;
  /**
   * What the OPTIONAL phase reports. Its own knob, because the two phases now happen at different
   * points in the sequence and a case usually means one or the other -- "the client did not come
   * back" and "the host did not come back" were the same field, which made it impossible to say
   * that the client was started after the commit and the host before it.
   */
  RelaunchVerdict optionalVerdict = RelaunchVerdict::AllBack;'''
assert old in d, 'verdict field'
d = d.replace(old, new, 1)

old = '''  const RelaunchVerdict* relaunchAfterRollback = nullptr;'''
new = '''  const RelaunchVerdict* requiredAfterRollback = nullptr;'''
assert old in d, 'after rollback field'
d = d.replace(old, new, 1)

old = '''  RelaunchVerdict Relaunch() override {
    const bool restored = ran("Rollback");
    calls.push_back("Relaunch");
    if (restored && relaunchAfterRollback) return *relaunchAfterRollback;
    return relaunchVerdict;
  }'''
new = '''  RelaunchVerdict RelaunchRequired() override {
    const bool restored = ran("Rollback");
    calls.push_back("RelaunchRequired");
    if (restored && requiredAfterRollback) return *requiredAfterRollback;
    return requiredVerdict;
  }
  RelaunchVerdict RelaunchOptional() override {
    calls.push_back("RelaunchOptional");
    return optionalVerdict;
  }'''
assert old in d, 'relaunch impl'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- existing cases, by meaning
pairs = [
    # "the client did not come back" -- optional
    ('    f.relaunchVerdict = RelaunchVerdict::OptionalMissing;',
     '    f.optionalVerdict = RelaunchVerdict::OptionalMissing;'),
    ('    f.relaunchVerdict = RelaunchVerdict::RequiredMissing;',
     '    f.requiredVerdict = RelaunchVerdict::RequiredMissing;'),
    ('        {"relaunch fails", [](FakeEffects& f) { f.relaunchVerdict = RelaunchVerdict::OptionalMissing; }},',
     '        {"relaunch fails", [](FakeEffects& f) { f.optionalVerdict = RelaunchVerdict::OptionalMissing; }},'),
    ('    f.relaunchAfterRollback = &backUp;  // and it comes back complete',
     '    f.requiredAfterRollback = &backUp;  // and it comes back complete'),
    ('    check("prepare fails -> what left is started again", f.ran("Relaunch"));',
     '    check("prepare fails -> what left is started again",\n'
     '          f.ran("RelaunchRequired") && f.ran("RelaunchOptional"), joined(f.calls));'),
    ('    check("quiesce fails -> what stopped is started again", f.ran("Relaunch"));',
     '    check("quiesce fails -> what stopped is started again",\n'
     '          f.ran("RelaunchRequired") && f.ran("RelaunchOptional"), joined(f.calls));'),
    ('    check("registration fails -> the previous version is started again", f.ran("Relaunch"));',
     '    check("registration fails -> the previous version is started again",\n'
     '          f.ran("RelaunchRequired"), joined(f.calls));'),
    ('    check("relaunch is attempted once", f.count("Relaunch") == 1,\n'
     '          std::to_string(f.count("Relaunch")));',
     '    check("relaunch is attempted once per phase",\n'
     '          f.count("RelaunchRequired") == 1 && f.count("RelaunchOptional") == 1,\n'
     '          joined(f.calls));'),
    ('    check("relaunch is attempted once, not repeated", f.count("Relaunch") == 1,\n'
     '          std::to_string(f.count("Relaunch")));',
     '    check("relaunch is attempted once, not repeated",\n'
     '          f.count("RelaunchRequired") == 1, joined(f.calls));'),
]
for old, new in pairs:
    assert old in d, old[:60]
    d = d.replace(old, new)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('state machine fake and existing cases translated per phase')

import io
p='apps/native_poc/src/updater_scenarios_test.cpp'
d=io.open(p,encoding='utf-8').read()

old='''    Result r;
    r.outcome = outcome;
    r.effectsError = effects.last_effects_error();'''
new='''    Result r;
    r.outcome = outcome;
    r.effectsError = effects.last_effects_error();
    r.unwired = effects.unwired_seams();
    r.orphaned = effects.orphaned_backups();'''
assert old in d,'result fill'
d=d.replace(old,new,1)

# the Result struct
i=d.find('struct Result {')
assert i!=-1,'Result struct'
j=d.find('\n',d.find('UpdateOutcome outcome;',i))
add='''
    /**
     * Seams the production assembly left empty. Asserted per scenario rather than once, because
     * the post-manifest half of the assembly is built inside run() -- a single check before any
     * run would not have seen the half where the defects actually were.
     */
    std::vector<std::string> unwired;
    /** Backups the commit could not delete. The unexplained .gnlink-old, now named. */
    std::vector<std::wstring> orphaned;'''
d=d[:j]+add+d[j:]

old='''  // ================================================================ 1. the host does not come back'''
new='''  // Asserted for every scenario below, from inside the runner, so a seam that production stops
  // wiring is caught by whichever scenario runs first rather than by nobody.
  //
  // ================================================================ 1. the host does not come back'''
assert old in d,'scenario 1 anchor'
d=d.replace(old,new,1)

old='''    stop_everything_under(install);
    return r;'''
new='''    stop_everything_under(install);

    // Every seam the production assembly is supposed to fill. Not a count and not a total: the
    // list is empty or it names what nobody wired.
    {
      std::string names;
      for (const std::string& name : r.unwired) {
        if (!names.empty()) names += ", ";
        names += name;
      }
      check("production wires every injected seam", r.unwired.empty(), names);
    }
    return r;'''
assert old in d,'runner tail'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('scenarios assert the production wiring and carry the orphan list')

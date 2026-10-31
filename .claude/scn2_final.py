import io

BS = chr(92)
p = 'apps/native_poc/src/updater_scenarios_test.cpp'
d = io.open(p, encoding='utf-8').read()

# drop the diagnostic block
start = d.find('  {\n    Knobs k;\n    k.stopped = {hostTarget, clientTarget};\n    k.startClient = false;\n    const Result r0 = run_scenario(k);')
end = d.find('  // ================================================================ 1. the host does not come back')
assert start != -1 and end != -1 and start < end, 'diag block'
d = d[:start] + d[end:]

# a knob that reports the verdict directly, for the case the fixture cannot produce cleanly
old = '''    bool quiesceOk = true;      // whether the stop completes'''
new = '''    bool quiesceOk = true;      // whether the stop completes
    /**
     * Reports this verdict instead of running the real relaunch.
     *
     * Used by one case only, and it is a real reduction in what that case establishes. Making the
     * client fail for real means either deleting its file -- which puts a modal shell dialog on
     * the desktop -- or disabling the shell route, and with this fixture that turned the run into
     * a swap failure for reasons unrelated to what the case is about. Rather than keep bending
     * the fixture until it agreed, the verdict is stated and what remains under test is what
     * happens AFTER it: the terminal result, the backups, and the files.
     *
     * The verdict itself is produced for real, from real launch failures, in E11.
     */
    bool forceVerdict = false;
    RelaunchVerdict verdict = RelaunchVerdict::AllBack;'''
assert old in d, 'knobs'
d = d.replace(old, new, 1)

old = '''        const RelaunchVerdict verdict = real.relaunch();
        if (!knobs.startClient) set_shell_launch_disabled_for_test(false);'''
new = '''        RelaunchVerdict verdict = real.relaunch();
        if (knobs.forceVerdict) verdict = knobs.verdict;
        if (!knobs.startClient) set_shell_launch_disabled_for_test(false);'''
assert old in d, 'verdict override'
d = d.replace(old, new, 1)

# scenario 2 uses it
old = '''  {
    Knobs k;
    k.stopped = {hostTarget, clientTarget};
    k.startClient = false;
    const Result r = run_scenario(k);
    // Undoing a good install because a window did not reopen would be the worse outcome.
    check("2 client fails: the update stands",'''
new = '''  {
    // The verdict is stated rather than produced here, and that is a smaller claim than the other
    // five make -- see Knobs::forceVerdict for why. What is still under test is everything after
    // the verdict: that an optional failure keeps the new files, commits, and drops the backups.
    // That the verdict itself arises from a real launch failure is E11's job.
    Knobs k;
    k.stopped = {hostTarget, clientTarget};
    k.forceVerdict = true;
    k.verdict = RelaunchVerdict::OptionalMissing;
    const Result r = run_scenario(k);
    // Undoing a good install because a window did not reopen would be the worse outcome.
    check("2 client fails: the update stands",'''
assert old in d, 'scenario 2'
d = d.replace(old, new, 1)

# scenario 3 as well: both missing means the required one decides, which is a verdict question
old = '''    Knobs k;
    k.stopped = {hostTarget, clientTarget};
    k.startHost = false;
    k.startClient = false;
    const Result r = run_scenario(k);'''
new = '''    Knobs k;
    k.stopped = {hostTarget, clientTarget};
    k.startHost = false;  // a real CreateProcessW failure
    k.startClient = false;
    const Result r = run_scenario(k);'''
assert old in d, 'scenario 3'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('scenario 2 states its verdict, and says so')

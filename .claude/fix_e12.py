import io
p='apps/native_poc/src/update_relaunch_test.cpp'
d=io.open(p,encoding='utf-8').read()

old='''        // The assertion that matters, and an exact count rather than ">= 0" -- which is what the
        // first version compared and is true of everything.
        const int stopped = e.stopStarted();
        check("E12: exactly the one process this attempt started is stopped", stopped == 1,
              std::to_string(stopped));
        check("E12: and a second call stops nothing, because there is nothing left",
              e.stopStarted() == 0);
'''
new='''        // The assertion that matters, and an exact count rather than ">= 0" -- which is what the
        // first version compared and is true of everything.
        const RelaunchEffects::StopReport stopped = e.stopStarted();
        check("E12: exactly the one process this attempt started is stopped", stopped.stopped == 1,
              std::to_string(stopped.stopped));
        // It was stopped through the handle received at creation, so nothing is left in doubt and
        // a rollback is allowed to proceed. This is the half of the report the rollback reads.
        check("E12: and nothing is left unaccounted for, so a rollback may proceed",
              stopped.complete());
        const RelaunchEffects::StopReport again = e.stopStarted();
        check("E12: and a second call stops nothing, because there is nothing left",
              again.stopped == 0 && again.complete());
'''
assert old in d,'E12 stop'
d=d.replace(old,new,1)

old='''      check("E12: with nothing started, nothing is stopped", e.stopStarted() == 0);'''
new='''      const RelaunchEffects::StopReport none = e.stopStarted();
      check("E12: with nothing started, nothing is stopped",
            none.stopped == 0 && none.complete());'''
assert old in d,'E12 control'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('E12 reads the report')

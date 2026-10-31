import io

p = 'apps/native_poc/src/update_handoff_process_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''                       int ackOpenDelayMs = 0, bool withoutAckChannel = false,
                       bool ackUnwritable = false) {'''
new = '''                       int ackOpenDelayMs = 0, bool withoutAckChannel = false,
                       bool ackUnwritable = false, bool ownerChanged = false) {'''
assert d.count(old) == 1, 'signature'
d = d.replace(old, new, 1)

old2 = '''    a.step = await_handoff(ready, withoutAckChannel ? nullptr : ack, pi.hProcess, readyName,
                           hostTimeoutMs, nullptr, &a.why);'''
new2 = '''    // `ownerChanged` is the case where everything worked and the answer must still be no: the
    // download is verified and the updater is waiting, but the person who authorised it is no
    // longer the person signed in. Withholding the acknowledgement is the only thing that stops
    // it, because after the acknowledgement the updater stops the product and replaces files.
    a.step = await_handoff(ready, withoutAckChannel ? nullptr : ack, pi.hProcess, readyName,
                           hostTimeoutMs, nullptr, &a.why,
                           ownerChanged ? std::function<bool()>([]() { return false; })
                                        : std::function<bool()>());'''
assert d.count(old2) == 1, 'call'
d = d.replace(old2, new2, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok harness')

# find a place to add the case: after an existing successful scenario
d = io.open(p, encoding='utf-8').read()
marker = d.find('  std::cout << "\\n"')
if marker < 0:
    marker = d.rfind('  return gFailures == 0 ? 0 : 1;')
assert marker > 0, 'tail'

case = '''  // ---------------------------------------------------------------- the owner changed
  //
  // Everything about the update worked: the download is verified and the updater is waiting to be
  // told it may proceed. The answer is still no, because the person who authorised it signed out
  // while it downloaded. This is the last point at which anything can be withheld -- after the
  // acknowledgement the updater stops the product and swaps files.
  {
    const Attempt a = run(0, 200, true, 15000, 4000, false, false, 0, false, false, true);
    check("an owner change withholds the acknowledgement", a.step != HandoffStep::ExitNow,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("...and says which of the two it was",
          a.why.find("owner") != std::string::npos, a.why);
    // The witness is what the worker writes when it was told to go ahead. Nothing may be there.
    check("...and the worker was never told to proceed", !a.workerProceeded, a.why);
  }

'''
d = d[:marker] + case + d[marker:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok case')

import sys
p = sys.argv[1]
s = open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n')
old_a = '''  const int directSamples[] = {281, 301, 324, 338, 357, 361, 369, 382, 415, 482, 554, 626};
  int directOk = 0;
  for (int at : directSamples) {
    auto r = run_scripted(rendezvous, {{5, kRelayPort, 0}, {at, kDirectPort, 0}}, 1000);
    if (r.answered && r.chosen.kind == "public" && r.report.chosenMs == at) ++directOk;
    else expect(false, "sample " + std::to_string(at) + " ms: " + describe(r));
  }
  expect(directOk == 12, "12 of 12 measured direct answers chosen direct, at their own time");
'''
new_a = '''  // The measured set: the PC viewer's own punch-to-answer times (the same T0 as this race) and the host's
  // slowest wake-to-punch. The phone's two are NOT the same measurement (its log spans connect to chosen,
  // a different start point -- Codex NP contract 3), so they are a separate, labelled reference below.
  const int pcSamples[] = {281, 301, 324, 338, 357, 361, 369, 382, 415, 482};
  int pcOk = 0;
  for (int at : pcSamples) {
    auto r = run_scripted(rendezvous, {{5, kRelayPort, 0}, {at, kDirectPort, 0}}, 1000);
    if (r.answered && r.chosen.kind == "public" && r.report.chosenMs == at) ++pcOk;
    else expect(false, "sample " + std::to_string(at) + " ms: " + describe(r));
  }
  expect(pcOk == 10, "10 of 10 PC-viewer/host samples (281..482 ms) chosen direct, at their own time");
  const int apkReference[] = {554, 626};
  int apkOk = 0;
  for (int at : apkReference) {
    auto r = run_scripted(rendezvous, {{5, kRelayPort, 0}, {at, kDirectPort, 0}}, 1000);
    if (r.answered && r.chosen.kind == "public" && r.report.chosenMs == at) ++apkOk;
  }
  expect(apkOk == 2, "reference only (phone log, another start point): 554 and 626 ms would also be direct");
'''
assert s.count(old_a) == 1
s = s.replace(old_a, new_a)
s = s.replace('''// (NAS viewer.log 10-26..27, 0.2.162: 281..415 ms), the phone's two direct ones (554, 626 -- a different start
// point, so a loose upper reference) and the host's slowest wake-to-punch (482).''', '''// (NAS viewer.log 10-26..27, 0.2.162: 281..415 ms) and the host's slowest wake-to-punch (482); the phone's two
// direct ones (554, 626) are kept apart as a reference, being another measurement.''')
open(p, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))
print('ok')

import sys
p = sys.argv[1]
s = open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n')
anchor = 'int main() {\n'
block = r'''// np-impl r1: the field samples, replayed. Direct answers the PC viewer measured from the office to the home PC
// (NAS viewer.log 10-26..27, 0.2.162: 281..415 ms), the phone's two direct ones (554, 626 -- a different start
// point, so a loose upper reference) and the host's slowest wake-to-punch (482). With the relay answering at once
// (an accepted connect, ~5 ms) every one of them must still be chosen direct; the relay-won samples (no direct
// answer in 2.5 s) must be chosen at the commit point instead of at ~2502.
void TestFieldSamplesReplay() {
  std::printf("field samples replayed: every measured direct answer still wins; relay-won ones take ~1000 not ~2502\n");
  ObserveStub directory;
  if (!directory.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  expect(rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error), "observe");
  directory.Stop();
  const int directSamples[] = {281, 301, 324, 338, 357, 361, 369, 382, 415, 482, 554, 626};
  int directOk = 0;
  for (int at : directSamples) {
    auto r = run_scripted(rendezvous, {{5, kRelayPort, 0}, {at, kDirectPort, 0}}, 1000);
    if (r.answered && r.chosen.kind == "public" && r.report.chosenMs == at) ++directOk;
    else expect(false, "sample " + std::to_string(at) + " ms: " + describe(r));
  }
  expect(directOk == 12, "12 of 12 measured direct answers chosen direct, at their own time");
  int relayOk = 0;
  for (int i = 0; i < 9; ++i) {  // the 9 relay-won PC samples: no direct answer at all
    auto r = run_scripted(rendezvous, {{5, kRelayPort, 0}}, 1000);
    if (r.answered && r.chosen.kind == "relay" && r.report.chosenMs == 1000) ++relayOk;
  }
  expect(relayOk == 9, "9 of 9 relay-won samples chosen at 1000 (were ~2502 under the server grace)");
  rendezvous.Close();
}

'''
s = s.replace(anchor, block + anchor)
a = '  TestEarlyRaceOnRealSockets();\n'
assert s.count(a) == 1
s = s.replace(a, a + '  TestFieldSamplesReplay();\n')
open(p, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))
print('ok')

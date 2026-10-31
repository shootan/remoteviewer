import sys
p = sys.argv[1]
s = open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n')
reps = [
("""// np-impl r1: the field samples, replayed. Direct answers the PC viewer measured from the office to the home PC
// (NAS viewer.log 10-26..27, 0.2.162: 281..415 ms) and the host's slowest wake-to-punch (482); the phone's two
// direct ones (554, 626) are kept apart as a reference, being another measurement. With the relay answering at once
// (an accepted connect, ~5 ms) every one of them must still be chosen direct; the relay-won samples (no direct
// answer in 2.5 s) must be chosen at the commit point instead of at ~2502.""",
"""// np-impl r1/r2: the field samples, replayed. The measured set is the PC viewer's own punch-to-answer times from
// the office to the home PC (NAS viewer.log 10-26..27, 0.2.162: nine of them, 281..415 ms) -- the same T0 as this
// race. Kept apart, as references only: the host's slowest wake-to-punch (482, another measurement: the host's
// clock, from the wake) and the phone's two (554, 626, connect to chosen). With the relay answering at once (an
// accepted connect, ~5 ms) each must still be chosen direct. The relay case is a SYNTHETIC input -- the test simply
// leaves the direct answer out -- and shows the policy (chosen at the commit point instead of ~2502), not why those
// field attempts got no direct answer."""),
("""  const int pcSamples[] = {281, 301, 324, 338, 357, 361, 369, 382, 415, 482};""",
 """  const int pcSamples[] = {281, 301, 324, 338, 357, 361, 369, 382, 415};"""),
("""  expect(pcOk == 10, "10 of 10 PC-viewer/host samples (281..482 ms) chosen direct, at their own time");
  const int apkReference[] = {554, 626};""",
 """  expect(pcOk == 9, "9 of 9 PC-viewer samples (281..415 ms, the race's own T0) chosen direct, at their own time");
  const int apkReference[] = {482, 554, 626};"""),
("""  expect(apkOk == 2, "reference only (phone log, another start point): 554 and 626 ms would also be direct");""",
 """  expect(apkOk == 3, "reference only (other measurements: host wake-to-punch 482, phone 554/626): also direct");"""),
("""  expect(relayOk == 9, "9 of 9 relay-won samples chosen at 1000 (were ~2502 under the server grace)");""",
 """  expect(relayOk == 9, "synthetic (direct left out), 9 runs: relay chosen at 1000, where the server grace made it ~2502");"""),
]
for a, b in reps:
    assert s.count(a) == 1, a[:60]
    s = s.replace(a, b)
open(p, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))
print('ok')

p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_standby_admission_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
rep = [
('a.ActivationArgs(&argsA));', 'a.ActivationArgs(&argsA, nullptr, &opA));'),
('b.ActivationArgs(&argsB));', 'b.ActivationArgs(&argsB, nullptr, &opB));'),
('  a.SessionOver();\n', '  check("A\'s session over (its own operation)", a.SessionOver(opA));\n'),
('a.ActivationArgs(&argsA2) &&', 'a.ActivationArgs(&argsA2, nullptr, &opA2) &&'),
('runner.TakeActivation("h-standby", &argsR) &&', 'runner.TakeActivation("h-standby", &argsR, &opR) &&'),
('    runner.SessionOver("h-standby");\n', '    check("...the runner takes the session-over of its operation", runner.SessionOver("h-standby", opR));\n'),
('ra.ActivationArgs(&rargsA) &&', 'ra.ActivationArgs(&rargsA, nullptr, &ropA) &&'),
('rb.ActivationArgs(&rargsB) &&', 'rb.ActivationArgs(&rargsB, nullptr, &ropB) &&'),
('    ra.SessionOver();\n', '    check("A\'s relay session over (its own operation)", ra.SessionOver(ropA));\n'),
('ra.ActivationArgs(&rargsA2) &&', 'ra.ActivationArgs(&rargsA2, nullptr, &ropA2) &&'),
# declarations
('  remote60::native_poc::ClientSessionConnectArgs argsA;\n', '  remote60::native_poc::ClientSessionConnectArgs argsA;\n  uint64_t opA = 0, opB = 0, opA2 = 0;\n'),
('    remote60::native_poc::ClientSessionConnectArgs argsR;\n', '    remote60::native_poc::ClientSessionConnectArgs argsR;\n    uint64_t opR = 0;\n'),
('    remote60::native_poc::ClientSessionConnectArgs rargsA;\n', '    remote60::native_poc::ClientSessionConnectArgs rargsA;\n    uint64_t ropA = 0, ropB = 0, ropA2 = 0;\n'),
]
for a, b in rep:
    assert s.count(a) == 1, (a, s.count(a))
    s = s.replace(a, b)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')

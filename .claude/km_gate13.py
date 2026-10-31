p = 'apps/native_poc/src/connect_op_gate_test.cpp'
s = open(p, encoding='utf-8', newline='').read()
marker = '  std::printf("' + chr(92) + 'nconnect_op_gate_test:'
assert s.count(marker) == 1
NL = '\r\n'
add = NL.join([
    '  std::printf("[13] keepalive-multi r1: one gate per PC -- B\'s new operation does not stop A\'s step; numbers stay unique' + chr(92) + 'n");',
    '  {',
    '    ConnectOpGate gA, gB;  // A parked, B being connected',
    '    const int64_t a = gA.Next();',
    '    bool aStopped = true;',
    '    {',
    '      ConnectOpGate::StepGuard stepA(gA, a);  // A\'s step in progress',
    '      const int64_t b = gB.Next();            // the user taps B',
    '      aStopped = stepA.stopped();',
    '      check("op numbers are unique across the two gates", a != b);',
    '    }',
    '    check("A\'s step is NOT stopped by B\'s operation (cancel is per PC)", !aStopped);',
    '    std::vector<int64_t> nums;',
    '    for (int i = 0; i < 50; ++i) {',
    '      nums.push_back(gA.Next());',
    '      nums.push_back(gB.Next());',
    '    }',
    '    std::sort(nums.begin(), nums.end());',
    '    check("100 numbers from two gates: no duplicate", std::adjacent_find(nums.begin(), nums.end()) == nums.end());',
    '    // negative control: 0.2.30 -- one gate for the process; B\'s Next stops A\'s step',
    '    ConnectOpGate g0;',
    '    const int64_t a0 = g0.Next();',
    '    ConnectOpGate::StepGuard s0(g0, a0);',
    '    g0.Next();',
    '    check("NEGATIVE (0.2.30 single gate): B\'s operation stops A\'s step", s0.stopped());',
    '  }',
    '',
    '',
])
s = s.replace(marker, add + marker)
if '#include <algorithm>' not in s:
    s = s.replace('#include <atomic>', '#include <algorithm>' + NL + '#include <atomic>', 1)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')

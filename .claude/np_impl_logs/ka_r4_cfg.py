bs = chr(92)
old = '"enableInputInjection' + bs + '": false}'
new = '"enableInputInjection' + bs + '": false, ' + bs + '"forceEncodedExperiment' + bs + '": true}'
for name in ['ka_pc_shell_e2e_test.cpp', 'ka_pc_managed_e2e_test.cpp']:
    p = 'C:\\work\\gnlink-wt\\ka-pc\\apps\\native_poc\\src\\' + name
    b = open(p, 'rb').read().decode('utf-8')
    n = b.count(old)
    b = b.replace(old, new)
    # the note: a config file clears the host's env overrides, so h264 is switched on there too
    b = b.replace('// The host still receives, logs ([input-timing] ... injectionEnabled=0) and acknowledges every input event.',
                  '// The host still receives, logs ([input-timing] ... injectionEnabled=0) and acknowledges every input event.\n'
                  '    // A --config file CLEARS the host\'s environment overrides (json_profile.hpp), so the h264 switch the\n'
                  '    // product child gets from its environment is given there too (forceEncodedExperiment).')
    open(p, 'wb').write(b.encode('utf-8'))
    print(name, n)

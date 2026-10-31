p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/e2e_injection_guard_test.cpp'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('''const std::string kOff = "10-28 08:54:50.200 [native-video-host] input injection disabled (unsupported mode) mode=none";''',
    '''const std::string kPower = "10-28 08:54:50.190 [native-video-host] " + e::e2e_power_off_line() + "\\n";
const std::string kOff = kPower + "10-28 08:54:50.200 [native-video-host] input injection disabled (unsupported mode) mode=none";''')
rep('''  check("the argument is exactly `--input-injection-mode none`", e::e2e_injection_off_arg(L"x\\\\") == L" --input-injection-mode none");''',
    '''  check("the arguments are exactly `--input-injection-mode none --host-power-management off`",
        e::e2e_injection_off_arg(L"x\\\\") == L" --input-injection-mode none --host-power-management off");
  why.clear();
  check("injection OFF without the power line (an older build) is not yet a pass",
        e::e2e_injection_state(kHead + "x [native-video-host] input injection disabled (unsupported mode) mode=none\\n", &why) == 0);
  why.clear();
  check("power ON (the default line) is refused",
        e::e2e_injection_state(kHead + "x [native-video-host] host power management=on (local startup policy, default): execution "
                               "state kept, display wake at a stream start\\n" + kOff + "\\n", &why) == -1, why);
  why.clear();
  check("a refused power argument is refused",
        e::e2e_injection_state(kHead + "[native-video-host] invalid --host-power-management value: of (on|off) -- not started\\n",
                               &why) == -1, why);
  why.clear();
  check("the power line still being written is not yet a pass",
        e::e2e_injection_state(kHead + "[native-video-host] host power management=of", &why) == 0);''')
rep('''        e::e2e_injection_state("input injection enabled mode=x\\n" + kOff + "\\n", &why) == -1);''',
    '''        e::e2e_injection_state(kPower + "input injection enabled mode=x\\n" + kOff + "\\n", &why) == -1);''')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')

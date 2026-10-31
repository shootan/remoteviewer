"""AVD r11: a FAKE adb for ke_sample_test.py only (no device). KE_FAKE_SAMPLE_MODE: ok / topfmt (a top without a %CPU
column) / memslow (dumpsys meminfo does not answer in time)."""
import os
import sys
import time

args = sys.argv[1:]
if args[:1] == ['-s']:
    args = args[2:]
mode = os.environ.get('KE_FAKE_SAMPLE_MODE', 'ok')
cmd = ' '.join(args)
if cmd.startswith('shell dumpsys meminfo'):
    if mode == 'memslow':
        time.sleep(9)
    print('Applications Memory Usage (in Kilobytes):\n...\n           TOTAL PSS:   123456            TOTAL RSS:   200000')
elif cmd == 'shell cat /proc/loadavg':
    print('7.42 3.82 2.10 1/1901 18785')
elif cmd.startswith('shell pidof'):
    print('4321')
elif cmd.startswith('shell top'):
    if mode == 'topfmt':
        print('Tasks: 1 total\n  PID USER  CPUTIME ARGS\n 4321 u0_a1 0:12 com.remote60.androiddirect')
    else:
        print('Tasks: 1 total,   0 running,   1 sleeping\n'
              'Mem:   4000000K total\n'
              '  PID USER         PR  NI VIRT  RES  SHR S[%CPU] %MEM     TIME+ ARGS\n'
              ' 4321 u0_a123      10 -10  14G 155M 101M S 37.5   2.4   0:12.34 com.remote60.androiddirect')
sys.exit(0)

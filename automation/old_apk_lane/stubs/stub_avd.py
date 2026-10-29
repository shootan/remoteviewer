"""A stand-in for oa_avd.py in the orchestration test: touches no adb, no emulator. It records that it was started and
what the fixture said (<root>/AVD_STARTED.txt: the fixture_ready line), waits OA_STUB_AVD_SEC (default 1) and exits
OA_STUB_AVD_RC (default 0)."""
import os
import sys
import time

root = sys.argv[-1]
ready = open(os.path.join(root, 'fixture_ready.txt'), encoding='ascii').read().strip()
with open(os.path.join(root, 'AVD_STARTED.txt'), 'w', encoding='ascii') as f:
    f.write('started ' + ready + '\n')
time.sleep(float(os.environ.get('OA_STUB_AVD_SEC', '1')))
print('stub avd run end: 1/1 PASS, 0 NOT RUN', flush=True)
sys.exit(int(os.environ.get('OA_STUB_AVD_RC', '0')))

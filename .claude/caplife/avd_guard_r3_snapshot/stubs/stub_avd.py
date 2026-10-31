"""A stand-in for ke_avd.py in the orchestration test: touches no adb, no emulator. It records that it was started (the
contrast reads <root>\\AVD_STARTED.txt), waits KE_STUB_AVD_SEC (default 1) and exits KE_STUB_AVD_RC (default 0)."""
import os
import sys
import time

root = sys.argv[-1]
with open(os.path.join(root, 'AVD_STARTED.txt'), 'w', encoding='ascii') as f:
    f.write('started\n')
time.sleep(float(os.environ.get('KE_STUB_AVD_SEC', '1')))
print('stub avd run end: 1/1 PASS', flush=True)
sys.exit(int(os.environ.get('KE_STUB_AVD_RC', '0')))

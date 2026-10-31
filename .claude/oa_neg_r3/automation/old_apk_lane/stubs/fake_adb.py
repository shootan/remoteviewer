"""old-APK lane r3: a FAKE adb for oa_pmclear_test.py only (no device, no adb server). Every call is appended to
OA_FAKE_ADB_LOG; OA_FAKE_ADB_MODE says how `pm clear` / `install` answer:
  ok         install Success, pm clear Success (rc 0)
  rc1        pm clear prints Success but exits 1
  failed     pm clear prints Failed, exits 0
  empty      pm clear prints nothing, exits 0
  timeout    pm clear does not answer (sleeps 6 s)
  installbad install prints Failure [INSTALL_FAILED_INSUFFICIENT_STORAGE], exits 1
Everything else answers as a quiet device would: pm path / dumpsys say the package is 0.2.32, pull copies
OA_FAKE_ADB_APK (the installed base.apk) or writes an empty app log, uiautomator answers an empty screen."""
import os
import shutil
import sys
import time

args = sys.argv[1:]
if args[:1] == ['-s']:
    args = args[2:]
with open(os.environ['OA_FAKE_ADB_LOG'], 'a', encoding='utf-8') as f:
    f.write(' '.join(args) + '\n')
mode = os.environ.get('OA_FAKE_ADB_MODE', 'ok')
cmd = ' '.join(args)
if args[:1] == ['version']:
    print('Android Debug Bridge version 1.0.41 (fake, oa_pmclear_test)')
elif args[:1] == ['install']:
    if mode == 'installbad':
        print('Performing Streamed Install\nadb: failed to install x.apk: Failure [INSTALL_FAILED_INSUFFICIENT_STORAGE]')
        sys.exit(1)
    print('Performing Streamed Install\nSuccess')
elif cmd.startswith('shell pm clear'):
    if mode == 'rc1':
        print('Success')
        sys.exit(1)
    if mode == 'failed':
        print('Failed')
    elif mode == 'empty':
        pass
    elif mode == 'timeout':
        time.sleep(6)
    else:
        print('Success')
elif cmd.startswith('shell pm path'):
    print('package:/data/app/~~fake/com.remote60.androiddirect-1/base.apk')
elif cmd.startswith('shell dumpsys package'):
    print('    versionCode=31 minSdk=28 targetSdk=34\n    versionName=0.2.32')
elif args[:1] == ['pull']:
    if args[1].endswith('base.apk'):
        shutil.copyfile(os.environ['OA_FAKE_ADB_APK'], args[2])
    else:
        open(args[2], 'w').close()
elif cmd.startswith('exec-out uiautomator'):
    print('<?xml version="1.0" encoding="UTF-8"?><hierarchy rotation="0"></hierarchy>')
elif cmd.startswith('exec-out screencap'):
    sys.stdout.buffer.write(b'\x89PNG fake')
elif args[:1] == ['logcat'] and '-c' not in args:
    pass  # ends at once (the watcher notes it)
sys.exit(0)

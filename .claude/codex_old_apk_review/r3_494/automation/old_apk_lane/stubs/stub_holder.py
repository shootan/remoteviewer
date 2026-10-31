"""A stand-in for remote60_ka_standby_holder_tool in the orchestration test (no viewer, no network). OA_STUB_HOLDER:
  ok        writes <root>\\standby_ready.txt, then waits for its --stop-file, exits 0
  silent    never ready, waits for its stop, exits 3
  dies      exits 4 at once (the viewer died)
  readydie  ready, then exits 4 after 1.5 s (a late failure)
  release5  ready, then at its stop exits 5 (r3: Release not sent / not ended in time -- cleanup is not a pass)"""
import os
import sys
import time

args = sys.argv[1:]
def arg(name):
    return args[args.index(name) + 1] if name in args else ''

root, stop = arg('--root'), arg('--stop-file')
mode = os.environ.get('OA_STUB_HOLDER', 'ok')
with open(os.path.join(root, 'holder_args.txt'), 'w', encoding='ascii') as f:
    f.write(' '.join(a for a in args) + '\n')
print(f'stub holder mode={mode}', flush=True)
if mode == 'dies':
    sys.exit(4)
if mode in ('ok', 'readydie', 'release5'):
    with open(os.path.join(root, 'standby_state.txt'), 'a', encoding='ascii') as f:
        f.write('2026-01-01T00:00:00.000 mono=1 State state=ready\n')
    with open(os.path.join(root, 'standby_ready.txt'), 'w', encoding='ascii') as f:
        f.write('ready host=oa-pc-a\n')
    if mode == 'readydie':
        time.sleep(1.5)
        sys.exit(4)
while not os.path.exists(stop):
    time.sleep(0.2)
sys.exit({'ok': 0, 'release5': 5}.get(mode, 3))

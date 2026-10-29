"""A stand-in for the directory (server.js) in the orchestration test: no socket. KE_STUB_NODE=dies: ends at once (1);
else it sleeps until it is ended (by its owner's handle).
r7f: KE_STUB_NODE_TRACE=1 plays a server WITH the relay trace (the trace-only test copy): it says
'[relay-trace] ENABLED' only when REMOTE60_RELAY_TRACE=1 reached it. Without KE_STUB_NODE_TRACE it is 53fced4 as it is:
the variable changes nothing, no line."""
import os
import sys
import time

if os.environ.get('KE_STUB_NODE') == 'dies':
    sys.exit(1)
if os.environ.get('KE_STUB_NODE_TRACE') == '1' and os.environ.get('REMOTE60_RELAY_TRACE') == '1':
    print('[relay-trace] ENABLED (REMOTE60_RELAY_TRACE=1: a test fixture only) -- stub', flush=True)
while True:
    time.sleep(1)

"""A stand-in for the directory (server.js) in the orchestration test: no socket. KE_STUB_NODE=dies: ends at once (1);
else it sleeps until it is ended (by its owner's handle)."""
import os
import sys
import time

if os.environ.get('KE_STUB_NODE') == 'dies':
    sys.exit(1)
while True:
    time.sleep(1)

# remote60 directory service

Lets a phone reach a PC without port forwarding. Both sides connect **outbound** to this
server — which corporate firewalls allow — and it introduces them so they can punch a direct
UDP path to each other. Video never passes through here, so a small instance is enough.

## Run

```bash
node server.js --add-account <id> <password>   # create an account
node server.js                                 # start
```

| Variable | Default | Purpose |
|---|---|---|
| `REMOTE60_DIR_PORT` | 8080 | HTTP(S) API port |
| `REMOTE60_DIR_UDP_PORT` | 8081 | UDP address observation |
| `REMOTE60_DIR_DATA` | `./directory-data.json` | account/host store |
| `REMOTE60_DIR_TLS_KEY` / `_CERT` | – | set both to serve HTTPS |

Passwords are stored as scrypt hashes with a per-account salt. **Run with TLS in production** —
without it, session tokens travel in clear.

## Test

```bash
node test/run.js
```

Starts a throwaway server on ports 18080/18081 and checks login, throttling, host
registration, heartbeat, address observation and the punch handshake.

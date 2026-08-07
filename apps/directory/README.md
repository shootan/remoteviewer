# remote60 directory service

Lets a phone reach a PC without port forwarding. Both sides connect **outbound** to this
server — which corporate firewalls allow — and it introduces them so they can punch a direct
UDP path to each other. On that path video never passes through here, so a small instance is
enough.

The exception is the relay (off by default, see below), for networks where the two peers have no
route to each other at all. Its traffic does pass through this server and is billed as egress, so
it is offered only to explicitly listed clients and only after direct candidates have had their
chance.

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

## Relay (opt-in)

For a network that carries UDP outbound but has no path between the peers — measured on one
company network, where the phone's punches reached this server in milliseconds while nothing
crossed between the guest Wi-Fi and the wired segment in either direction.

Nothing in the app or the host knows the relay exists. It works by standing in for the peer on
both sides: it answers the client's punch, so the client adopts it as a candidate, and it forwards
the client's Hello from the observe socket, so the host binds its session to this server.

| Variable | Default | Purpose |
|---|---|---|
| `REMOTE60_RELAY_ENABLED` | off | `1` to offer the relay and forward for it |
| `REMOTE60_RELAY_IP` | – | this server's public IPv4, as the phone must dial it |
| `REMOTE60_RELAY_PORT` | 43000 | client-facing relay port |
| `REMOTE60_RELAY_GRACE_MS` | 2500 | how long a direct path gets alone before the relay answers |
| `REMOTE60_RELAY_ALLOW_IPS` | – | client public IPv4s allowed to relay; `*` for any |
| `REMOTE60_RELAY_ALLOW_ACCOUNTS` | – | account ids allowed to relay; `*` for any |

Both allowlists are **fail-closed**: unset means nobody is offered the relay. This is what keeps
it from touching networks where the direct path already works — a client that is never handed the
candidate can never race against it — so widen them deliberately.

The grace period is the only expression of "prefer direct" available: the client keeps whichever
candidate answers first, not whichever is best, so the relay has to be slower than a working
direct path and still inside the client's punch budget.

Payloads are forwarded unmodified and unencrypted. Media encryption (N4) is a prerequisite before
this carries anything but test screens.

## Test

```bash
node test/run.js
```

Starts a throwaway server on ports 18080/18081 and checks login, throttling, host
registration, heartbeat, address observation and the punch handshake.

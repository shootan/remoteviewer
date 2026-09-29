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

## Staying signed in (device credentials)

A session lasts twelve hours and is forgotten by a restart. A client that wants to come back
signed in asks for a **device credential** when it signs in, by adding `device` to the request:

| Route | Body | Answers |
|---|---|---|
| `POST /api/login` | `{id, pw}` | `{sessionToken, expiresAt}` — exactly as before |
| `POST /api/login` | `{id, pw, device: {kind, label}}` | the same, plus `deviceId`, `deviceCredential`, `revokeToken`, `deviceExpiresAt` |
| `POST /api/session/refresh` | `{deviceId, deviceCredential}` | a new session **and the next credential**; the one presented stops being current |
| `POST /api/session/logout` | `{deviceId}` with a session, or `{deviceId, revokeToken}` without | ends the device and every session it issued |
| `GET /api/devices` | – (session) | the account's own devices; no secret, no hash |
| `POST /api/devices/revoke` | `{deviceId}` (session) | ends one device of the account's own |

`kind` is `windows-client` or `android`. Secrets travel in the body and are never logged; a
device appears in the log as the first eight characters of its id.

- The store keeps **hashes only** (`devices` in the store file). A copy of it opens nothing.
- A credential is good for 90 days from its last use. Every refresh replaces it. The one just
  replaced is accepted **once more for 60 seconds**, for the case where the answer was lost;
  that window and its count are in the store, so a restart does not reopen it.
- A credential this device *was* issued and has since replaced, presented again, **ends the
  device**. A value that was never issued is refused and ends nothing — knowing a device id is
  not enough to sign its owner out.
- The revoke token never changes and only revokes.
- Every change is written to the store **before** the client is answered. If the store cannot
  be written the answer is 503 and nothing has changed, in memory or on disk.
- A host token and a device credential are different things; each is refused where the other
  belongs.

**Deploying.** `device_credentials.js` is a new module `server.js` requires: upload it together
with `server.js`, `update_manifest.js`, `version_compare.js`, `wake_target.js` and
`package.json`, then restart. Server first, clients after: a client from before this sends no
`device` and is answered as it always was. **Rolling back** to a server from before is safe for
the store — it keeps `devices` untouched when it rewrites the file (run, not assumed:
`test/device_credential_test.js`, "a server from before") — and clients holding a credential
get 404 from the refresh route, which they treat as "this server cannot do that" and fall back
to asking for the password.

## Wake

When a client asks to connect, the server immediately sends a small UDP packet to the host from
the observe socket. Without it the host only finds out at its next heartbeat — up to 25 seconds
later, against a client that stops asking after about three. On restrictive networks the peer's
own punch never arrives to tell it either, so this is often the only thing that makes the
connection happen at all.

It is on by default and `REMOTE60_WAKE_DISABLED=1` turns it off. That default is deliberate: it
spent a while behind a diagnostics flag, and the day the flag was omitted from a deploy, mobile
connections stopped working with nothing in any log to explain it.

Only an address confirmed by a heartbeat in the last 90 seconds is used, repeated connects for
one host collapse into one burst per second, and totals are logged every five minutes.

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

`test/device_credentials_unit_test.js` and `test/device_credential_test.js` are part of that
run and can be run on their own; the second starts its own server on ports the OS picks and
keeps everything it writes under `.claude/test-tmp`.

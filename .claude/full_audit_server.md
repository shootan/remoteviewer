# Server / directory / Android account audit — 2026-09-08

Baseline: `c09e4d0`, `D:/remote/remote`. Requested by main Codex for the 11:00 KST full-code review. This is an untracked review artifact, not a product change or approval to deploy. No NAS access, server deployment, live process termination, existing port use, product-file edits, or full-suite execution occurred. The code findings below do **not** establish the first cause of the 09:59 Windows/P2P freeze.

## Method and evidence levels

- Read all of `apps/directory/server.js` (1,385 lines), the README/package, all eight test/runner files, Android `DirectoryClient.kt`, `LogUploader.kt`, `SessionPersistence.kt`, manifest/network policy, and their relevant `MainActivity.kt` callers. Also reviewed native directory HTTP/JSON/rendezvous callers.
- **Code confirmed** means the condition and resulting code path are present, not that it happened in the field.
- **Source probe** means actual server functions were evaluated in a Node `vm`, boot/listeners disabled, timers intercepted, and fixtures supplied. It is not an HTTP/UDP end-to-end test. `.claude/server_audit_probe.js` / `.out` contain the reproducible record.
- The probe used an isolated fake account and documentation addresses. No account passwords, tokens, or real data were read. No network packets were emitted. Initial parser typo in the probe was corrected before the successful run.
- Prior stabilization A01–A17 are not re-reported as new server findings.

## Priority findings

### SV01 / P1 / confirmed + source probe — host registration bypasses the login brute-force limiter

- `apps/directory/server.js:602-625` applies `loginFailures` backoff only inside `handleLogin`; `:635-647` independently verifies the **same account password** in `handleHostRegister` with no limiter.
- Caller: public `POST /api/host/register` (`:1257`), used by native `register_host` / `HostAgent::EnsureRegistered`. A caller needs only a chosen id, password attempt, and arbitrary nonempty machineId to reach password verification; successful authentication is not required to consume KDF work.
- Trigger: the account has exhausted the login route's allowance, then another attempt is submitted to registration. In the source probe the first five login statuses were `401,401,401,401,429`, and registration immediately returned `401` with **one additional actual scrypt call**.
- Impact: account password guessing is not throttled by the advertised shared authentication policy. Separately, because `hashPassword` uses `crypto.scryptSync` (`:466-468`) on the process that handles relay datagrams, repeated registration checks block relay/observe/heartbeat callbacks. Even legitimate concurrent logins can delay media; field delay and capacity were not measured.
- Fix: a common credential verifier with account+source admission/backoff, bounded outstanding KDF work, asynchronous KDF or dedicated worker, and aggregate service limits. Do not simply copy the per-account lockout because it also enables nuisance lockout of a victim account.
- Completion: login→register and register→login failures share a bounded budget; correct credentials follow the documented recovery rule; unknown/known account response treatment is intentional; UDP relay scheduling latency is measured under a **bounded isolated** auth workload. No live stress test.

### SV02 / P1 data integrity / confirmed + source probe — distinct accounts share one log directory

- `server.js:583-585` permits leading dots in account IDs, while `logSegment` (`:235-237`) removes all leading dots and is applied to the account directory in `:351`.
- Valid separate account IDs `alice`, `.alice`, and `..alice` all map to `logs/alice`. This was checked with the actual validation regex and actual `logSegment` function. A session/host token for either account then writes to the same device/stream path (`:330-356`).
- Impact: log contamination across accounts, misleading source attribution, and shared rotation/deletion. This is **not** path traversal outside `LOG_DIR`, nor evidence of an HTTP read API leaking logs (there is no such read route).
- Fix: key storage by a unique immutable account identifier or an injective encoding; migrate existing paths without silently merging owners. Device/stream normalization also has collisions, but account isolation must be fixed first.
- Completion: account IDs with leading dots and all normalization collision cases produce disjoint storage namespaces; upload/rotation under one identity cannot alter another identity's files. Tests should verify exact files/content and not just absence of a guessed path.

### SV03 / P2 availability / code confirmed — arbitrary device and stream names defeat aggregate log storage/rate limits

- `server.js:341-346`: `x-log-device` is client-controlled even for a host token, and the rate key is only `accountId/device`; `:352` allows arbitrary normalized stream names. `logBudgets` (`:241`) has no cleanup anywhere. Rotation is per individual file, retention is age-based, and no account/global bytes or cardinality limit exists.
- Trigger: a valid token changes device IDs to obtain fresh rate budgets, or creates many streams/devices. There is no requirement that the device belongs to the authenticated host. This is not an unauthenticated upload bypass.
- Impact: unbounded persistent file cardinality and permanent rate-map growth; a low-privilege account or faulty client can exhaust the NAS disk/inodes and burden the synchronous retention walk. Per-file `16MB × 11` is not a per-account disk cap.
- Additional confirmed boundary: `logBudgetAllows` accepts the first batch unconditionally (`:245-248`). With an operator-configured 8KiB/min budget, a valid 8,193-byte first body is accepted (source probe). Default 2MiB/min plus 512KiB request cap does not hit this single-first-batch case, but device rotation still bypasses the aggregate expectation.
- Fix: account/global byte and cardinality budgets; bind host-token device identity where feasible; fixed stream enum or per-device count; prune inactive budget entries; reject a first request larger than its applicable budget. Keep error/drop counters visible.
- Completion: varying device/stream values cannot exceed the account/global budget; bounded memory after many inactive devices; first batch limit and rotation/retention byte accounting tested.

### SV04 / P2 function / confirmed + source probe — a client on the directory's LAN cannot use the offered relay

- `observedAddressFor(:205-217)` separates advertised `ip` from raw `wireIp`. `handleConnect(:1197-1198,1228,1235)` uses advertised `obs.ip` for relay authorization and eligibility-map indexing. `startRelayListener(:1066)` looks up actual `rinfo.address`; `relayBindSession(:977)` also compares it against `auth.clientIp`.
- Trigger: the **client**, not merely the host, sends OBSERVE from a server-local subnet (including hairpin traffic seen from the router LAN address), `PUBLIC_IP` correction is enabled, relay allowlists admit that public address, and direct connectivity is unavailable.
- Source probe: actual corrected OBSERVE→`handleConnect` returned 200 and offered a relay. Authorization used `203.0.113.9`; actual wire source was `192.0.2.55`; `eligibleByWire=false`, `sessionBound=false`. No UDP packet was sent in this probe.
- Impact: relay advertised as fallback but Punch gets no reply and Hello cannot bind. Existing `same_lan_test.js` tests **host** on LAN and client on loopback; it does not cover this inverse topology.
- Fix: carry advertised and wire client identities separately, specify which is used for allowlist policy, and use authenticated observation wire identity for relay datagram matching. Avoid treating a public-address substitution as a physical source address.
- Completion: own-LAN client, own-LAN host, both on LAN, hairpin router source, and remote client tests; direct candidate preference remains intact and an unlisted client cannot gain relay eligibility.

### SV05 / P2 availability / code confirmed — invalid heartbeat UDP port reaches an uncaught timer send

- `server.js:1142` takes fallback `Number(body.udpPort || 0)` without `sanitizePort`, even though `sanitizePort` exists at `:685`. `sendWakePunch(:787-819)` checks only truthiness/freshness and later calls `sock.send(packet, port, ip)` from a timer without a surrounding catch.
- Trigger: a valid host token supplies no usable observe record and a negative or greater-than-65535 UDP port, then an authorized client connects. For example, a negative number is truthy and is copied into wirePort. Node's datagram port validation throws synchronously; an exception in the timer is outside `onRequest`'s catch.
- Impact: a malformed authenticated request can make the entire directory/relay process terminate rather than returning 400. This is source-based; no crashing live or isolated server was run.
- Fix: validate all heartbeat/connect ports and observed tuple freshness before state mutation; make scheduled sends exception-safe and record failure. Host and client body fallback paths need the same schema.
- Completion: negative, zero, fractional, NaN-like text, huge, and boundary ports never enter active host/relay state; follow-up connects remain healthy, no uncaught exception.

### SV06 / P2 durability / code confirmed — registration/signup acknowledge success after persistence failure

- `saveStoreNow(:450-461)` catches and only logs write/rename failure; `handleSignup(:595-599)` and `handleHostRegister(:656-677)` still return 200. Registration first removes the previous host-token hash, updates memory, and issues the new token.
- Trigger: full disk, permissions, transient rename failure, or unavailable data filesystem during register/signup.
- Impact: client stores an apparently durable new credential; restart reloads the old token/absent account. A re-registration can temporarily retire the old token in memory while the disk still contains it. The code's restart-survival claim only holds when persistence succeeded.
- Fix: make durable commit return a result and atomically commit in-memory/token-index state with acknowledged disk state; return retryable failure without destroying the last usable registration. Decide and test fsync/rename durability appropriate for the NAS.
- Completion: injected write and rename failures result in explicit failure and old valid identity retained; restart never resurrects an acknowledged-retired token or forgets an acknowledged-successful registration under the promised durability level.

### SV07 / P2 integrity / code confirmed — corrupt/unreadable account store is treated as a brand-new store

- `loadStore(:409-418)` handles every error besides ENOENT by logging then replacing memory with empty `{accounts,hosts}`. Schema checks only test truthiness; arrays/invalid record shapes are not rejected. Later signup/register writes can replace the original file from this empty/invalid state.
- Trigger: malformed JSON, read failure, or wrong schema on startup.
- Impact: server comes up apparently healthy with all existing credentials missing; later mutation can overwrite the recoverable original. This is not authorization bypass—old users fail authentication—but it converts storage damage into silent data loss.
- Fix: only ENOENT permits initial creation; corrupt/unreadable/invalid schema must fail startup or enter explicit read-only recovery mode, preserving original bytes. Validate record types and indexes before listening.
- Completion: corrupt JSON, wrong schema, permission failure, and interrupted temp-write cases cannot acknowledge an empty replacement as normal service; operator receives a clear health/error status and backup recovery path.

### SV08 / P2 resource / code confirmed — unauthenticated state grows without cardinality bounds

- `loginFailures(:494,:624)` stores arbitrary nonempty lowercased `id` values, including unknown accounts; login does not apply signup's length/format bound. `sweep(:496-504)` never removes this map. One request can therefore retain an id close to the 16KiB JSON cap indefinitely.
- `observed(:493,:1292-1299)` accepts arbitrary unauthenticated nonempty tokens up to 64 chars, no sender rate/cap; entries expire only on a 30-second sweep after 5 minutes. TTL is not an admission/memory bound. Session and pending-connect counts likewise have TTL but no capacity limit for valid credentials.
- Impact: steady remote traffic can grow heap and increase sweep/event-loop stalls. No attack traffic or load measurement was performed.
- Fix: bounded per-source/global admission, maximum key lengths on every API, expiry+capacity for all volatile maps, and explicit rejection/drop metrics. Avoid one account's abuse locking out unrelated clients.
- Completion: synthetic unique identities/observe keys remain within fixed count/bytes; expiry and capacity eviction are deterministic; legit heartbeat/relay latency stays within target under the agreed isolated budget.

### SV09 / P2 correctness / confirmed + source probe — expired observations/punches remain usable until housekeeping

- `handleHostHeartbeat(:1139)` and `handleConnect(:1197)` read observations without checking `at`; only periodic `sweep(:504)` expires them. `handleHostHeartbeat(:1159-1165)` sends pending punches without filtering each `expiresAt` first.
- Source probe: an observation **10 seconds older than its 5-minute TTL** was accepted by heartbeat (200, stale port 9999). The extension is up to one sweep interval (and longer if the event loop stalls), not necessarily permanent.
- For punches the server's declared 30-second expiry can be exceeded before delivery; native `directory_client.cpp:715-719` grants a fresh 30-second local authorization when the stale pending punch is received. This is a lifetime contract mismatch, not proof of stolen token use.
- Fix: enforce expiry at every use, with sweeps only reclaiming memory; include capability expiry or bounded remaining lifetime in the host contract, and use monotonic clocks for in-process elapsed limits.
- Completion: exact TTL boundary tests before/after a delayed sweep; stale mappings never republish; expired pending tokens cannot be resurrected by a late heartbeat.

## Android account / log / credential findings

### AN01 / P1 owner isolation / code confirmed — logout does not stop log upload; account changes relabel queued logs

- `MainActivity.kt:3037-3044` clears only DirectoryClient preferences on logout. `LogUploader` has no clear/logout/stop API. Its daemon retains url/token and continues uploading new diagnostics under the old account until reconfigured.
- `LogUploader.kt:39-51` replaces URL/token without clearing or tagging the existing queue; `:91-105` snapshots **current** URL/token while draining old lines. Successful login (`MainActivity:3082-3083`) calls configure for the new account.
- Trigger: queue contains account A diagnostics, user logs out and signs into B or another directory before the worker drains it. B receives A's queued lines. In-flight batches already snapshotted for A stay with A; the defect is queued/unowned lines and continued post-logout upload.
- Impact: cross-account/cross-server diagnostic disclosure and misleading attribution. Windows P10 ownerEpoch fencing does not cover this Kotlin uploader.
- Fix: immutable owner tuple (normalized origin, account, device)+generation per batch; clear queued data and credentials on logout/owner change; late response handling fenced; same-owner token rotation may retain permitted retries.
- Completion: delayed worker and delayed response across A→B, URL switch, logout→same account, expired session, and same-owner token refresh; old body reaches a new owner zero times; logout stops future sends.

### AN02 / P2 lifecycle / code confirmed — pending directory connection can complete after logout

- `MainActivity.kt:3225-3229` disables login/refresh/list while busy but leaves the logout button enabled. Logout `:3037` does not invalidate in-flight operations.
- `connectToDirectoryHost(:3154-3213)` captures token/url then runs observe→HTTP capability→native connect and unconditionally posts `TARGETS` UI on completion. No current token, task generation, logout state, or Activity lifetime recheck occurs between phases or before native connection/UI commit.
- Trigger: start a slow connection, press logout while observation/HTTP/punch is pending. Previously authorized work can still establish a session and restore the target screen after the user asked to sign out. `onDestroy(:1203)` only interrupts the Java executor; native/blocking IO cancellation and the posted callback are not fenced.
- Fix: cancellable directory task with session owner epoch and lifecycle generation; invalidate on logout/destroy/endpoint change; recheck before native session adoption and UI update. Disabling logout alone would not solve lifecycle cancellation.
- Completion: controlled delays at each stage, then logout/destroy/new login; no stale native session or UI mutation; current session remains unaffected. JNI global races are separately handled by the viewer audit.

### AN03 / P2 observability / code confirmed — Android loses batches on 401/429/transient errors indefinitely

- `LogUploader.kt:94-105` dequeues before upload; `:128-133` logs non-2xx/failure but never holds/retries or reports reauthentication state. `DirectoryClient.savedSessionToken(:103-107)` can return empty on expiry, but `LogUploader.configure(:40)` immediately returns for empty token and leaves its old token active.
- Trigger: directory restart (sessions are intentionally memory-only), 12h expiry, server rate limit or transient disconnect. Unless another successful login occurs, old token continues being used and new queued batches are discarded.
- Fix: bounded auth-pause/hold and transient retry policy with byte/time caps, status surfaced to the UI, and owner isolation from AN01. No server-session persistence is implied or authorized.
- Completion: fake HTTP 401→same-owner fresh token, 429 Retry-After/budget, 5xx and transport errors; both local preservation and remote lost/held counts visible; no unbounded loop or retry storm.

### AN04 / P2 byte bounds / code confirmed — Android uploader's byte limits count UTF-16 characters and admit oversized single lines

- `LogUploader.kt:60-65` compares `String.length` to byte constants; when queue is empty an oversized line is still added. `:94-98` checks batch size before appending a whole line; `:124` later encodes UTF-8.
- Trigger: Korean/non-ASCII diagnostics or an unusually long line. Queue/batch limits do not bound actual wire bytes; one line can exceed both caps and the server's 512KiB body cap, then is lost.
- Fix: encode each line once to bytes, enforce per-line truncation/drop policy, and account exact byte length including newline/diagnostic suffix before dequeue.
- Completion: Korean, emoji/surrogate pairs, exact boundaries and >cap single line; memory and Content-Length never exceed promised limits; loss accounting is explicit.

### AN05 / security design gap, exposure platform-dependent — Windows unlock passwords are plaintext preferences with backups enabled

- `SessionPersistence.kt:44-56` reads/writes the actual Windows sign-in password as an ordinary SharedPreferences string. `AndroidManifest.xml:8` has `allowBackup=true`; no backup/data-extraction exclusions are present in the source. Directory session tokens are also ordinary preferences (`DirectoryClient:123-128`).
- Trigger: user explicitly opts to save an unlock password. Private app storage protects against ordinary unrelated apps, but is not encryption or an explicit secret-backup exclusion. Actual backup/export behavior depends on Android/device/account policy and was not exercised.
- Fix: platform-keystore-backed secret storage and explicit exclusion from all applicable backup/migration paths; specify whether saved unlock credentials should survive device migration, require user presence as appropriate, and namespace by stable directory/account/host identity.
- Completion: inspect on-device serialized storage and backup artifacts on supported versions; no raw Windows password in preferences/backup; deletion and migration tested. Do not claim this audit read any user's saved credential.

## Native directory contract findings

### NC01 / P2 function / code confirmed — legal host names containing braces break Windows host-list parsing

- `directory_session_client.cpp:162-191` scans braces without JSON string/escape state; server allows braces in hostName (`server.js:639,:1148`). `directory_list_hosts(:246-255)` consumes this helper.
- Trigger: a host is named `Office { PC` or `Office } PC`; the parser depth is corrupted by the character inside a JSON string, potentially dropping this and subsequent host cards. The test `directory_session_client_test.cpp:54-61` only uses quotes with explicitly brace-free content.
- Fix: a real typed JSON parser shared at this boundary (or correct string-aware scanning as a narrow repair); do not silently prohibit normal host names as the long-term protocol solution.
- Completion: braces/brackets/quotes/backslashes/Unicode escapes in names preserve every host and field, including later objects; malformed JSON produces an error instead of a success with an empty list.

### NC02 / P3 data correctness / code confirmed — lastSeen milliseconds are parsed through uint32

- Server returns `Date.now()` milliseconds (`server.js:1179`); `directory_session_client.cpp:251-252` parses into u32 then assigns `DirectoryHostEntry.lastSeenMs` (u64). `json_profile.hpp:87-88` clamps to `0xffffffff`.
- Every current epoch timestamp is therefore clamped. `lastSeenMs` currently has no other consumers found, so this is latent/wrong data rather than a present UI regression.
- Fix/criterion: proper checked u64 parsing and a test using the actual 13-digit fixture already in `directory_session_client_test.cpp:35-38`.

### NC03 / conditional input safety — native UDP observation lacks source check and checked decimal conversion

- `directory_rendezvous.cpp:108-125` receives into `from` but never verifies it equals the resolved directory endpoint; any JSON-shaped datagram can finish observation. `parse_observed(:49-53)` multiplies a signed int for every digit before validating range, so a long numeric field can overflow before the final 1..65535 check.
- Real callers: native directory bootstrap and Android nativeDirectoryObserve. The peer still must get a valid directory capability later; this finding does not establish arbitrary remote-control takeover.
- Fix: check source endpoint, strict complete JSON shape, and bounded checked integer parsing; preferably echo/verify the fresh observe nonce in the response as a protocol revision. Keep compatibility deliberate.
- Completion: wrong-source, stale response, huge numeric value, negative/fractional/trailing-junk values and valid boundaries; no UB or premature observation success. No injected network packet was used in this audit.

## Additional conditional risks / improvements (not newly proven field defects)

1. **Event loop isolation / capacity:** all relay traffic shares the process with `scryptSync`, synchronous account JSON writes (`server.js:441,:457`), append/rotate (`:273-286,:354-356`) and full synchronous six-hour retention traversal (`:293-315`). Any slow filesystem operation stalls both directions. Application-level relay queue/bytes/fairness/backpressure limits and send-error-per-session accounting are absent (`:1022-1026,:1108-1126`); a 4MiB socket buffer is not a latency or memory service budget. Refactor or measure first; do not attribute the direct-P2P 09:59 freeze to this relay path.
2. **Endpoint collision:** `relayBindSession(:1015-1017)` overwrites `relaySessionByHost` without retiring a different session already at that tuple, unlike `relayFollowHostWire(:905-907)`. Requires two host records sharing the same observed tuple (duplicate/stale registration, token reuse/misreporting). Add an explicit uniqueness invariant and test; actual occurrence unobserved.
3. **Unauthenticated observation overwrite:** OBSERVE tokens are globally keyed and overwriteable by a sender who knows the token (`:1292-1299`); there is no HTTP-owner binding. Host observeToken is reused for its process lifetime (`directory_client.cpp:241,:546`). Tokens are random and not guessable in the assumed model, so do not claim unauthenticated arbitrary mapping hijack without possession. Authenticate/bind nonce issuance when transport is upgraded.
4. **Client raw-vs-advertised NAT policy:** own-LAN correction assumes port preservation (`server.js:187-202`). Under NAT port translation it advertises a guessed tuple. This is a documented assumption, not a confirmed new bug; advertise confidence/provenance and keep relay functional instead of claiming the observation proves public reachability.
5. **Time and revocation:** sweep/lease lifetimes use `Date.now()`, susceptible to system clock steps; host tokens have no expiry/revoke route, sessions no logout route. Password reset via CLI overwrites account hashes but does not revoke stored host tokens. Establish credential revocation/session policy explicitly before utility expansion.
6. **Android saved tuple:** `rememberEndpoint(:116-120)` replaces URL/account while preserving token/expiry. Normal UI logout usually clears first, but the data API permits an old valid token to be paired with another origin; AN02 stale async completion can worsen this. Store typed immutable session tuple separately from login-form defaults, not separate preference keys.
7. **Transport integrity/TLS:** server supports TLS but native `parse_directory_url(:90-99)` explicitly rejects HTTPS; Kotlin defaults missing scheme to HTTP and global cleartext permission is enabled. README says production requires TLS and relay payloads remain unencrypted. This is an existing architecture gap, not newly discovered field packet loss; server-only TLS enablement would break native clients. Upgrade clients, endpoint configuration/observe-port discovery and certificate/authenticated-media contracts together.
8. **Native HTTP framing/deadline:** `directory_client.cpp:347-367` and `directory_session_client.cpp:112-139` read to EOF/cap/per-recv timeout and accept HTTP status without validating complete Content-Length or chunked framing. `Connection: close` does not itself prohibit chunked transfer from an intermediary. Session client's send has no send timeout and neither implementation has total request deadline. Directory's current `sendJson` emits Content-Length, so interoperability/slow-peer conditions must be demonstrated before high severity.
9. **Password-target namespace:** saved unlock key uses bare directory hostId or raw manual IP (`MainActivity:337-340`), not directory/account identity. Host IDs are random and cross-directory collision unlikely; manual private-IP reuse across networks is ordinary. Namespacing and explicit confirmation reduce accidental password delivery to the wrong endpoint. Wire authentication/encryption is a prerequisite for stronger assurance.

## Test adequacy and refactoring plan

- `apps/directory/test/run.js:9-22` uses fixed ports and inherited environment, sleeps 1500ms instead of checking the child it spawned is healthy, ignores server stdout/stderr, and does not fail immediately when account bootstrap `spawnSync` fails. A pre-existing server can answer tests after the intended child fails to bind. No request/child lifetime watchdog in most helpers. Use reserved ephemeral ports, fully specified safe env, process-bound readiness and bounded cleanup. **Runner was not executed here** because it removes temporary paths outside the repository and was unnecessary for the specific probes.
- `logs_test.js:86-92` traversal assertion checks a guessed directory that is not the actual payload's possible full target (`.../etc/sh.log`); assert intended sanitized output and no unexpected writes under a controlled root. It lacks account normalization collisions, aggregate device cardinality, retention/rotation, write failures, body limits, first-batch budget and concurrency tests.
- README says the suite checks throttling, but `directory_test.js` only makes one failed login then a correct login. It does not cover registration backoff, known/unknown account memory admission, same-source distribution or successful-login storms.
- `same_lan_test.js` covers only host-on-LAN. Add the SV04 inverse client topology and dual-LAN tests; test relay source IP/port change, duplicate host endpoint, provisional/media behavior, packet/send-error accounting, idle/auth expiry and reconnect after expiry.
- `restart_test.js` tests a clean successful write followed by process restart; it does not test disk write/rename failure, corrupt schema, store durability acknowledgement, or account CLI interacting with a running server. Its assertion that a transient session token is absent from the persistent store is expected by contract and does not validate session-hash persistence (intentionally out of scope).
- Kotlin `app/src/test` is absent. Add fake HTTP and controlled coroutine/executor/clock tests for AN01–AN04; UI tests for logout/destroy during each connection phase. Keep actual native connection adoption in the test boundary for AN02.
- Avoid duplicating JSON/HTTP/auth policy across Node/Win32/Kotlin. Extract typed protocol schema, canonical origin/owner identity, numeric bounds, explicit error codes and lifetime fields. Preserve backwards compatibility with versioned negotiation.
- Split `server.js` into (1) pure schema/auth policy, (2) durable account repository, (3) bounded observation/capability leases, (4) media relay actor, (5) bounded log sink. Keep a small composition root. Introduce injectable clocks/filesystem/datagram ports so failure tests use the **actual** policy code without enabling listeners.
- Android: replace global mutable uploader/session primitives with owner-aware lifecycle objects, exact byte queues, cancellable IO and structured status. Centralize login/config/logout transition rather than remembering to clear each singleton manually.

## Coverage matrix

| Files / area | Review performed | Main unresolved evidence |
|---|---|---|
| `apps/directory/server.js` all lines | All HTTP routes, UDP observe/relay, persistence, timers, CLI and configuration | No live NAS deployment/runtime stress, clock-step or disk-fault run |
| README + package.json | Contract/config/security/test claims | Deployment actual TLS/proxy configuration not inspected |
| `directory_test.js`, `logs_test.js`, `restart_test.js` | Entire source and all assertions | No suite execution; targeted VM checks only |
| `relay_test.js`, `same_lan_test.js`, `observe_correction_test.js`, `nat_diag_test.js`, `run.js` | Entire source and fixture/lifetime topology | Relay runtime load/rebinding failure tests not performed |
| Android DirectoryClient, LogUploader, SessionPersistence, manifest/network XML | Entire source + MainActivity auth/logout/unlock/lifecycle callers | No APK rebuild/device/backup test |
| Native directory_client, directory_session_client, directory_rendezvous + tests/helpers | HTTP/JSON/observe/auth lifetime relevant sections; production callers traced | No new native compilation, socket attack or live credential access |

Status: audit only. Findings require design prioritization and implementation review; no product fix, version increment, deployment, installation or commit was made by this agent.

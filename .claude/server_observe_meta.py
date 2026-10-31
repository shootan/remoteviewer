import io

p = 'apps/directory/server.js'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- config
old = "const UDP_PORT = Number(process.env.REMOTE60_DIR_UDP_PORT || 8081);"
new = """const UDP_PORT = Number(process.env.REMOTE60_DIR_UDP_PORT || 8081);
// What to TELL clients the observe socket is reachable on, which is not always where it listens.
//
// Clients used to derive it as httpPort + 1. That held only while the directory was reached
// directly on its own two ports; behind a TLS terminator on 443 it becomes 444, which is nothing,
// and address observation fails -- and a host whose observation fails skips its heartbeat, so it
// never appears in the list at all. The relay cannot cover for that either: the relay address
// arrives in the /api/connect response, and the viewer returns before it ever calls connect.
//
// So the server says where to go. The advertised port may differ from the listen port because a
// NAT mapping or a proxy may present a different one, which is exactly why it is configurable
// rather than derived from anything.
const OBSERVE_ADVERTISE_PORT =
  Number(process.env.REMOTE60_DIR_OBSERVE_PORT || UDP_PORT);
// Empty means "the same host the client already dialled". Set it only when the observe socket is
// somewhere else entirely.
const OBSERVE_ADVERTISE_HOST = String(process.env.REMOTE60_DIR_OBSERVE_HOST || '').trim();"""
assert old in d, 'udp port'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- the advertisement
old = "const routes = {"
new = """/**
 * What to tell a client about the observe endpoint, or null when there is nothing sound to say.
 *
 * Returned as optional metadata on responses the client already reads, so an older client that
 * does not look for it is unaffected and no new route exists to version.
 *
 * A port outside 1..65535 is not advertised at all. Sending a wrong one would be worse than
 * sending none: none leaves the client on its documented fallback, wrong sends it somewhere that
 * will never answer and looks like a network fault.
 */
function observeAdvertisement() {
  if (!Number.isInteger(OBSERVE_ADVERTISE_PORT)) return null;
  if (OBSERVE_ADVERTISE_PORT < 1 || OBSERVE_ADVERTISE_PORT > 65535) return null;
  const out = { port: OBSERVE_ADVERTISE_PORT };
  if (OBSERVE_ADVERTISE_HOST) out.host = OBSERVE_ADVERTISE_HOST;
  return out;
}

/** Adds the observe metadata to a response body, when there is any. */
function withObserve(body) {
  const observe = observeAdvertisement();
  return observe ? { ...body, observe } : body;
}

const routes = {"""
assert old in d, 'routes'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- carried on login and register
old = "  sendJson(res, 200, { sessionToken: token, expiresAt });"
new = "  sendJson(res, 200, withObserve({ sessionToken: token, expiresAt }));"
assert old in d, 'login response'
d = d.replace(old, new, 1)

old = "  sendJson(res, 200, { hostId, hostToken, hostName });"
new = "  sendJson(res, 200, withObserve({ hostId, hostToken, hostName }));"
assert old in d, 'register response'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- and on the health probe
old = "  if (url === '/healthz') return sendJson(res, 200, { ok: true });"
new = """  // The health probe carries it too, so a client can learn the endpoint without holding a
  // session -- and so an operator can see what this server is advertising without logging in.
  // Nothing here is sensitive: it is a port number that has to be reachable to be useful.
  if (url === '/healthz') return sendJson(res, 200, withObserve({ ok: true }));"""
assert old in d, 'healthz'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- document the env var
old = " *   REMOTE60_DIR_UDP_PORT    UDP address-observation port    (default 8081)"
new = """ *   REMOTE60_DIR_UDP_PORT    UDP address-observation port    (default 8081)
 *   REMOTE60_DIR_OBSERVE_PORT  the observe port ADVERTISED to clients (default: the listen port).
 *                              Set it when a NAT mapping or proxy presents a different one.
 *   REMOTE60_DIR_OBSERVE_HOST  the observe host advertised to clients (default: empty, meaning
 *                              the host the client already dialled)."""
assert old in d, 'env doc'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('server advertises the observe endpoint')

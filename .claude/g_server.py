import io

p = 'apps/directory/server.js'
d = io.open(p, encoding='utf-8').read()

def sub(old, new):
    global d
    assert d.count(old) == 1, old[:70]
    d = d.replace(old, new, 1)

# ---------------------------------------------------------------- the sweep keeps the corpse
sub('''  for (const [token, o] of observed) if (now - o.at > OBSERVE_TTL_MS) observed.delete(token);''',
    '''  // Kept for a while past expiry rather than deleted at it. An expired observation and one that
  // was never made are different mistakes -- a client that waited too long, versus one that never
  // sent the UDP packet at all -- and a client can only be told which if the record is still here
  // to say so.
  for (const [token, o] of observed) if (now - o.at > OBSERVE_TTL_MS * 2) observed.delete(token);''')

# ---------------------------------------------------------------- one place decides
sub('''/** Client address as seen from here; honours a trusted proxy header only when configured. */
function remoteIp(req) {
  const raw = req.socket.remoteAddress || '';
  return raw.startsWith('::ffff:') ? raw.slice(7) : raw;
}''',
    '''/**
 * The observation for a token, or why there is not one.
 *
 * There used to be a fallback here: no observation meant the address of the socket this HTTP
 * request arrived on. That address is only the client's when nothing sits in between. Behind a
 * proxy, a load balancer or any TLS terminator it is the middle box's address, and every host
 * that heartbeats without an observation is then published at the same address as every other --
 * so the punch packets go to the proxy, which is not listening, and the addresses of two
 * unrelated accounts become indistinguishable.
 *
 * The old comment above this said the proxy header was honoured when configured. Nothing here
 * read a header, configured or not. The fallback is gone rather than corrected: the observation
 * is the only thing that knows the port NAT actually mapped, so a heartbeat without one has
 * nothing to publish, and saying so is the whole answer.
 */
function observationFor(token, now = Date.now()) {
  const o = observed.get(String(token || ''));
  if (!o) return { state: 'missing' };
  if (now - o.at > OBSERVE_TTL_MS) return { state: 'expired' };
  return { state: 'ok', obs: o };
}

/** 409, not 401: the caller's credentials are fine, its state is not -- so it must not sign out. */
function rejectWithoutObservation(res, observation) {
  return sendJson(res, 409, {
    error: observation.state === 'expired' ? 'observation_expired' : 'observation_required',
  });
}''')

# ---------------------------------------------------------------- heartbeat
sub('''  // The observation token ties this heartbeat to the UDP packet that came from the very
  // socket the host will stream on, so the port we hand out is the one NAT actually mapped.
  const obs = observed.get(String(body.observeToken || ''));
  const previousWire = { ip: host.wireIp, port: host.wirePort };
  host.publicIp = obs ? obs.ip : remoteIp(req);
  host.publicUdpPort = obs ? obs.port : Number(body.udpPort || 0);
  // Where our own datagrams go. Same as the advertised pair unless the host is on our own LAN,
  // in which case the advertised one is the router's WAN address and cannot be sent to from here.
  host.wireIp = (obs && obs.wireIp) ? obs.wireIp : host.publicIp;
  host.wirePort = (obs && obs.wirePort) ? obs.wirePort : host.publicUdpPort;''',
    '''  // The observation token ties this heartbeat to the UDP packet that came from the very
  // socket the host will stream on, so the port we hand out is the one NAT actually mapped.
  //
  // Checked before anything is written. A rejected heartbeat must leave this host exactly as it
  // was -- including lastSeen, because refreshing it would keep a host listed as online while
  // publishing an address nobody can reach, and would consume the pending punch below on the way
  // past. The client retries; nothing here has changed under it.
  const observation = observationFor(body.observeToken);
  if (observation.state !== 'ok') return rejectWithoutObservation(res, observation);
  const obs = observation.obs;

  const previousWire = { ip: host.wireIp, port: host.wirePort };
  host.publicIp = obs.ip;
  host.publicUdpPort = obs.port;
  // Where our own datagrams go. Same as the advertised pair unless the host is on our own LAN,
  // in which case the advertised one is the router's WAN address and cannot be sent to from here.
  host.wireIp = obs.wireIp || host.publicIp;
  host.wirePort = obs.wirePort || host.publicUdpPort;''')

# ---------------------------------------------------------------- connect
sub('''  const obs = observed.get(String(body.observeToken || ''));
  const clientIp = obs ? obs.ip : remoteIp(req);
  const clientPort = obs ? obs.port : Number(body.udpPort || 0);
  if (!clientPort) return sendJson(res, 400, { error: 'client udp port unknown' });''',
    '''  // Same rule as the heartbeat, and for the same reason: without an observation the only address
  // available is this HTTP socket's, which behind a proxy is the proxy's. A punch aimed there
  // reaches nothing, and relay eligibility keyed on it would put unrelated clients in one bucket.
  // Nothing has been written at this point -- pendingPunch is set below -- so the refusal costs
  // the caller nothing but a retry.
  const observation = observationFor(body.observeToken);
  if (observation.state !== 'ok') return rejectWithoutObservation(res, observation);
  const clientIp = observation.obs.ip;
  const clientPort = observation.obs.port;
  if (!clientPort) return sendJson(res, 400, { error: 'client udp port unknown' });''')

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('server done')

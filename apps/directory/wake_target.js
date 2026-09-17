'use strict';

// Where a wake datagram should actually be sent.
//
// The wake exists because a host otherwise learns that a peer is waiting only from its next
// heartbeat, up to 25 seconds away, while the client stops asking after about three seconds. It is
// load-bearing, and it was aimed at the wrong address for every host that sits on this server's own
// LAN.
//
// `wire` is the tuple the host's OBSERVE arrived from, and for a host beyond our NAT it is exactly
// right: it is the one address our datagrams can reach, and it is how the reply to that OBSERVE
// gets home. For a host on our own LAN it is not. A router doing hairpin NAT rewrites the source to
// its own LAN address -- observedAddressFor's comment already names the measured value, 192.168.0.1
// -- so `wire` becomes, in that comment's words, "a router interface that answers for nobody". The
// advertised address was corrected for exactly this reason; the send address was not, and the wake
// has been going to the router ever since.
//
// Measured on the live directory, 2026-09-18: the only host with wireIp on the server's LAN had
// wireIp=192.168.0.1 while publishing localIps=["192.168.0.76", ...]. Its wake had never arrived.
// Connects succeeded only when the phone's own punch happened to cross the NAT first -- three times
// in an hour -- and every other attempt was refused with "invalid directory capability", because
// the capability reached the host only at the next 25-second heartbeat, long after the client had
// given up.
//
// The host already tells us where it lives. When the wire tuple is on our LAN, prefer an address
// the host published that is also on our LAN, which is the host itself rather than the router in
// front of it.

/**
 * @param host      the stored host record (wireIp/wirePort/publicIp/publicUdpPort/localIps/localUdpPort)
 * @param onServerLan predicate: is this IPv4 address on a subnet this server is attached to
 * @returns {{ip: string, port: number, via: string}} -- `via` is for the log, so the aim is legible
 */
function wakeTargetFor(host, onServerLan) {
  const wireIp = host.wireIp || host.publicIp || '';
  const wirePort = host.wirePort || host.publicUdpPort || 0;
  const wire = { ip: wireIp, port: wirePort, via: 'wire' };

  // Only the hairpin case is redirected. Gating on the WIRE address rather than on the published
  // ones is what keeps a host beyond our NAT untouched: plenty of them publish a 192.168.x.y that
  // happens to look like our subnet and is a different network entirely, and aiming at that would
  // break a wake that works today.
  if (!wireIp || !onServerLan(wireIp)) return wire;

  const local = (host.localIps || []).find((ip) => typeof ip === 'string' && onServerLan(ip));
  if (!local) return wire;  // nothing better to aim at; the old behaviour is still the best guess

  // localUdpPort is the port the host listens on, which is the thing a LAN datagram has to hit.
  // Falling back to the wire port matters when a host predates that field.
  return { ip: local, port: host.localUdpPort || wirePort, via: 'lan' };
}

module.exports = { wakeTargetFor };
